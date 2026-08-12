#include "kspacejet/crash/handler.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>

#include "kspacejet/crash/breadcrumb.hpp"
#include "kspacejet/crash/ring_buffer.hpp"
#include "kspacejet/logging/logging.hpp"

#if defined(__linux__)
#include <cerrno>
#include <csignal>
#include <ctime>

#include <cxxabi.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace ksj::crash {

namespace {

#if defined(__linux__)

constexpr int kAbsoluteMaxDepth = 200;
constexpr std::size_t kAltStackSize = 64 * 1024;
constexpr std::size_t kBreadcrumbDumpLimit = 32;
constexpr const char* kFallbackProcessName = "unknown-process";
constexpr const char* kFallbackThreadName = "unnamed-thread";
constexpr const char* kDefaultDebuggerEnvVar = "USE_GDB_ON_FAULT";

const char* g_process_name = kFallbackProcessName;
const char* g_debugger_env_var = kDefaultDebuggerEnvVar;
const char* g_debugger_request = nullptr;
bool g_enable_debugger_from_env = true;
bool g_print_readable_stack = true;
bool g_use_altstack = true;
int g_max_frames = kAbsoluteMaxDepth;
volatile std::sig_atomic_t g_crash_handler_active = 0;
std::terminate_handler g_previous_terminate_handler = nullptr;
std::string g_pending_crash_report;

thread_local bool g_thread_registration_completed = false;
thread_local bool g_thread_name_initialized = false;
thread_local bool g_thread_altstack_installed = false;
thread_local std::array<char, RingBuffer::kThreadNameCapacity> g_thread_name_storage{};
thread_local std::unique_ptr<std::byte[]> g_thread_altstack_storage{};
thread_local bool g_thread_crash_context_initialized = false;
thread_local ThreadCrashContext g_thread_crash_context{};
thread_local std::array<char, 64> g_context_current_message{};
thread_local std::array<char, 64> g_context_scan_uid{};
thread_local std::array<char, 128> g_context_scan_title{};
thread_local std::array<char, 128> g_context_stage{};
thread_local std::array<char, 512> g_context_ismrmrd_dataset_path{};
thread_local std::array<char, 512> g_context_output{};

[[noreturn]] void ExitProcessImmediately(int exit_code) noexcept {
  std::_Exit(exit_code);
}

[[noreturn]] void ExitImmediately(int signal_number) {
  const int exit_code = signal_number > 0 ? 128 + signal_number : EXIT_FAILURE;
  ExitProcessImmediately(exit_code);
}

bool EnterCrashHandlerContext() {
  if (g_crash_handler_active != 0) {
    return false;
  }
  g_crash_handler_active = 1;
  return true;
}

void CopyTextToBuffer(std::string_view source, char* destination, std::size_t destination_size) noexcept {
  if (destination == nullptr || destination_size == 0) {
    return;
  }

  const std::size_t copy_size = std::min(source.size(), destination_size - 1);
  if (copy_size > 0) {
    std::memcpy(destination, source.data(), copy_size);
  }
  destination[copy_size] = '\0';
  if (copy_size + 1 < destination_size) {
    std::memset(destination + copy_size + 1, 0, destination_size - copy_size - 1);
  }
}

std::uint64_t CurrentThreadId() noexcept {
  return static_cast<std::uint64_t>(::syscall(SYS_gettid));
}

std::uint64_t SignalSafeMonotonicMilliseconds() noexcept {
  timespec timestamp{};
  if (::clock_gettime(CLOCK_MONOTONIC, &timestamp) != 0) {
    return 0;
  }
  return static_cast<std::uint64_t>(timestamp.tv_sec) * 1000ULL +
         static_cast<std::uint64_t>(timestamp.tv_nsec) / 1000000ULL;
}

void BeginCrashReport() noexcept {
  try {
    g_pending_crash_report.clear();
  } catch (...) {}
}

void FlushCrashOutput() noexcept {
  std::fflush(stderr);
  std::fflush(stdout);
  try {
    if (ksj::logging::IsConfigured() && !g_pending_crash_report.empty()) {
      ksj::logging::Log(ksj::logging::Level::Critical, g_pending_crash_report, "kspacejet-crash", 0, "CrashHandler");
      ksj::logging::Flush();
      g_pending_crash_report.clear();
    }
  } catch (...) {}
}

void EmitCrashText(const char* text) noexcept {
  if (text == nullptr || text[0] == '\0') {
    return;
  }

  std::fputs(text, stderr);
  try {
    g_pending_crash_report.append(text);
  } catch (...) {}
}

void EmitCrashFormatted(const char* format, ...) noexcept {
  if (format == nullptr) {
    return;
  }

  std::array<char, 4096> buffer{};
  va_list args;
  va_start(args, format);
  va_list args_copy;
  va_copy(args_copy, args);
  const int required = std::vsnprintf(buffer.data(), buffer.size(), format, args);
  va_end(args);

  if (required < 0) {
    va_end(args_copy);
    return;
  }

  if (static_cast<std::size_t>(required) < buffer.size()) {
    va_end(args_copy);
    EmitCrashText(buffer.data());
    return;
  }

  try {
    std::string long_buffer(static_cast<std::size_t>(required) + 1U, '\0');
    std::vsnprintf(long_buffer.data(), long_buffer.size(), format, args_copy);
    long_buffer.resize(static_cast<std::size_t>(required));
    EmitCrashText(long_buffer.c_str());
  } catch (...) {
    buffer[buffer.size() - 2] = '\n';
    buffer[buffer.size() - 1] = '\0';
    EmitCrashText(buffer.data());
  }
  va_end(args_copy);
}

std::string ShellQuote(std::string_view value) {
  std::string quoted;
  quoted.reserve(value.size() + 2);
  quoted.push_back('\'');
  for (const char ch : value) {
    if (ch == '\'') {
      quoted.append("'\\''");
    } else {
      quoted.push_back(ch);
    }
  }
  quoted.push_back('\'');
  return quoted;
}

std::string TrimTrailingNewlines(std::string value) {
  while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
    value.pop_back();
  }
  return value;
}

std::string RunCommand(std::string command) {
  std::string output;
  FILE* pipe = ::popen(command.c_str(), "r");
  if (pipe == nullptr) {
    return output;
  }

  char buffer[512];
  while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    output.append(buffer);
  }
  (void)::pclose(pipe);
  return TrimTrailingNewlines(std::move(output));
}

std::string DemangleSymbolName(const char* symbol_name) {
  if (symbol_name == nullptr) {
    return {};
  }

  int demangle_status = 0;
  char* demangled_name = abi::__cxa_demangle(symbol_name, nullptr, nullptr, &demangle_status);
  if (demangle_status == 0 && demangled_name != nullptr) {
    std::string readable_name(demangled_name);
    std::free(demangled_name);
    return readable_name;
  }

  std::free(demangled_name);
  return symbol_name;
}

std::string ResolveSourceLocation(const Dl_info& symbol_info, std::uint64_t program_counter) {
  if (symbol_info.dli_fname == nullptr) {
    return {};
  }

  const auto module_base = reinterpret_cast<std::uint64_t>(symbol_info.dli_fbase);
  const auto program_offset =
    (module_base != 0 && program_counter >= module_base) ? (program_counter - module_base) : program_counter;

  std::ostringstream command;
  command << "addr2line -f -C -e " << ShellQuote(symbol_info.dli_fname) << " 0x" << std::hex << program_offset
          << " 2>/dev/null";
  const std::string addr2line_output = RunCommand(command.str());
  if (addr2line_output.empty()) {
    return {};
  }

  const auto newline = addr2line_output.find('\n');
  if (newline == std::string::npos) {
    return {};
  }

  const std::string location = addr2line_output.substr(newline + 1);
  if (location == "??:0" || location == "?:0") {
    return {};
  }
  return location;
}

void AssignCurrentThreadName(std::string_view thread_name) noexcept {
  if (thread_name.empty()) {
    g_thread_name_storage[0] = '\0';
    g_thread_name_initialized = false;
    return;
  }

  CopyTextToBuffer(thread_name, g_thread_name_storage.data(), g_thread_name_storage.size());
  g_thread_name_initialized = true;

  std::array<char, 16> pthread_name{};
  CopyTextToBuffer(thread_name, pthread_name.data(), pthread_name.size());
  (void)::pthread_setname_np(::pthread_self(), pthread_name.data());
}

void EnsureDefaultThreadName() noexcept {
  if (g_thread_name_initialized) {
    return;
  }

  std::array<char, RingBuffer::kThreadNameCapacity> detected_name{};
  if (::pthread_getname_np(::pthread_self(), detected_name.data(), detected_name.size()) == 0 &&
      detected_name[0] != '\0') {
    AssignCurrentThreadName(detected_name.data());
    return;
  }

  std::array<char, RingBuffer::kThreadNameCapacity> generated_name{};
  constexpr std::string_view kThreadPrefix = "tid-";
  std::copy(kThreadPrefix.begin(), kThreadPrefix.end(), generated_name.begin());
  const auto thread_id = static_cast<unsigned long long>(CurrentThreadId());
  const auto [end, error] = std::to_chars(generated_name.data() + kThreadPrefix.size(),
                                          generated_name.data() + generated_name.size() - 1, thread_id);
  if (error == std::errc{}) {
    *end = '\0';
  }
  AssignCurrentThreadName(generated_name.data());
}

const char* CurrentThreadNameCString() noexcept {
  return g_thread_name_initialized ? g_thread_name_storage.data() : kFallbackThreadName;
}

void PrintThreadCrashContext() {
  EmitCrashFormatted("Process: %s pid=%ld\n", g_process_name, static_cast<long>(::getpid()));
  EmitCrashFormatted("Thread: %s tid=%llu\n", CurrentThreadNameCString(),
                     static_cast<unsigned long long>(CurrentThreadId()));

  if (!g_thread_crash_context_initialized) {
    return;
  }

  EmitCrashFormatted("Current message: %s\n",
                     g_context_current_message[0] != '\0' ? g_context_current_message.data() : "unknown");
  EmitCrashFormatted("ScanUID: %s\n", g_context_scan_uid[0] != '\0' ? g_context_scan_uid.data() : "unknown");
  EmitCrashFormatted("ScanTitle: %s\n", g_context_scan_title[0] != '\0' ? g_context_scan_title.data() : "unknown");
  EmitCrashFormatted("Stage: %s\n", g_context_stage[0] != '\0' ? g_context_stage.data() : "unknown");
  EmitCrashFormatted("ISMRMRD dataset: %s\n",
                     g_context_ismrmrd_dataset_path[0] != '\0' ? g_context_ismrmrd_dataset_path.data() : "unknown");
  EmitCrashFormatted("Output: %s\n", g_context_output[0] != '\0' ? g_context_output.data() : "unknown");
}

base::Status InstallAlternateSignalStackForCurrentThread() {
  if (g_thread_altstack_installed || !g_use_altstack) {
    return base::Status::Ok();
  }

  if (!g_thread_altstack_storage) {
    g_thread_altstack_storage = std::unique_ptr<std::byte[]>(new (std::nothrow) std::byte[kAltStackSize]);
    if (!g_thread_altstack_storage) {
      return base::Status::OutOfMemory("unable to allocate crash altstack for current thread");
    }
  }

  stack_t altstack{};
  altstack.ss_sp = g_thread_altstack_storage.get();
  altstack.ss_size = kAltStackSize;
  altstack.ss_flags = 0;
  if (::sigaltstack(&altstack, nullptr) != 0) {
    return base::Status::IoError("sigaltstack failed: " + std::string(std::strerror(errno)));
  }

  g_thread_altstack_installed = true;
  return base::Status::Ok();
}

void EnsureCurrentThreadReadyForBreadcrumbs() noexcept {
  if (!g_thread_registration_completed) {
    (void)RegisterCurrentThread({});
    return;
  }
  EnsureDefaultThreadName();
}

void PrintReadableCallStack(void* const* trace, int trace_size, int start_index = 0, bool force_print = false) {
  if (!force_print && !g_print_readable_stack) {
    return;
  }

  EmitCrashText("\n\n ---------- Readable Call stack ----------- \n");

  if (start_index < 0) {
    start_index = 0;
  }

  for (int index = start_index; index < trace_size; ++index) {
    const int frame_index = index - start_index;
    Dl_info symbol_info{};
    const auto program_counter = reinterpret_cast<std::uint64_t>(trace[index]);
    if (!::dladdr(trace[index], &symbol_info)) {
      EmitCrashFormatted("#%-2d <unresolved> (pc=0x%llx)\n", frame_index,
                         static_cast<unsigned long long>(program_counter));
      continue;
    }

    std::string function_name = DemangleSymbolName(symbol_info.dli_sname);
    if (function_name.empty()) {
      function_name = "<unknown>";
    }
    const std::string source_location = ResolveSourceLocation(symbol_info, program_counter);
    const auto module_base = reinterpret_cast<std::uint64_t>(symbol_info.dli_fbase);
    const auto module_offset =
      (module_base != 0 && program_counter >= module_base) ? (program_counter - module_base) : program_counter;

    if (source_location.empty()) {
      EmitCrashFormatted("#%-2d %s [%s+0x%llx] (pc=0x%llx)\n", frame_index, function_name.c_str(),
                         symbol_info.dli_fname != nullptr ? symbol_info.dli_fname : "<unknown>",
                         static_cast<unsigned long long>(module_offset),
                         static_cast<unsigned long long>(program_counter));
    } else {
      EmitCrashFormatted(
        "#%-2d %s @ %s [%s+0x%llx] (pc=0x%llx)\n", frame_index, function_name.c_str(), source_location.c_str(),
        symbol_info.dli_fname != nullptr ? symbol_info.dli_fname : "<unknown>",
        static_cast<unsigned long long>(module_offset), static_cast<unsigned long long>(program_counter));
    }
  }
}

void CaptureAndPrintReadableCallStack() {
  if (!g_print_readable_stack || g_max_frames <= 0) {
    return;
  }

  std::array<void*, kAbsoluteMaxDepth> trace{};
  const int frame_count = std::clamp(g_max_frames, 1, kAbsoluteMaxDepth);
  const int trace_size = ::backtrace(trace.data(), frame_count);
  if (trace_size > 0) {
    PrintReadableCallStack(trace.data(), trace_size);
  }
}

void PrintRecentBreadcrumbs() {
  std::size_t printed = 0;
  GlobalRingBuffer().for_each_recent(kBreadcrumbDumpLimit, [&](const RingBuffer::Entry& entry) noexcept {
    if (printed == 0) {
      EmitCrashText("\n\n ---------- Recent breadcrumbs ----------- \n");
    }
    ++printed;
    EmitCrashFormatted("#%-2llu +%llums [tid=%llu %s] %s: %s\n", static_cast<unsigned long long>(entry.sequence),
                       static_cast<unsigned long long>(entry.monotonic_ms),
                       static_cast<unsigned long long>(entry.thread_id),
                       entry.thread_name[0] != '\0' ? entry.thread_name.data() : kFallbackThreadName,
                       entry.category[0] != '\0' ? entry.category.data() : "general",
                       entry.message[0] != '\0' ? entry.message.data() : "<empty>");
  });

  if (printed == 0) {
    EmitCrashText("\n\n ---------- Recent breadcrumbs ----------- \n");
    EmitCrashText("<no breadcrumbs recorded>\n");
  }
}

void PrintDebuggerHint() {
  if (!g_enable_debugger_from_env || g_debugger_env_var == nullptr || *g_debugger_env_var == '\0') {
    return;
  }

  EmitCrashText("------------------------------------\n\n\n");
  EmitCrashFormatted("--- set environment variable %s ---\n", g_debugger_env_var);
  EmitCrashText("--- to start GDB on the faulted process ---\n\n\n");
}

void LaunchDebuggerIfRequested() {
  if (g_debugger_request == nullptr || *g_debugger_request == '\0') {
    return;
  }

  pid_t child = 0;
  char pid[20];
  char gdb_name[] = "gdb";
  char* argv[] = {gdb_name, const_cast<char*>(g_process_name), pid, nullptr};

  EmitCrashText("Starting gdb...\n\n");
  switch ((child = ::fork())) {
    case 0:
      {
        const auto [end, error] = std::to_chars(pid, pid + sizeof(pid) - 1, static_cast<long>(::getppid()));
        if (error == std::errc{}) {
          *end = '\0';
        } else {
          pid[0] = '\0';
        }
      }
      ::execvp(argv[0], argv);
      EmitCrashFormatted("Failed to start gdb: %s\n", std::strerror(errno));
      ExitProcessImmediately(EXIT_FAILURE);
    case -1:
      EmitCrashFormatted("Failed to fork gdb helper: %s\n", std::strerror(errno));
      break;
    default:
      (void)::waitpid(child, nullptr, 0);
      break;
  }
}

void PrintTerminateReason() {
  EnsureDefaultThreadName();
  EmitCrashFormatted("\n\nUnhandled C++ termination in %s on thread %s (tid=%llu)\n", g_process_name,
                     CurrentThreadNameCString(), static_cast<unsigned long long>(CurrentThreadId()));
  PrintThreadCrashContext();

  const std::exception_ptr current_exception = std::current_exception();
  if (current_exception == nullptr) {
    EmitCrashText("Reason: no active exception was captured by std::terminate().\n");
    return;
  }

  try {
    std::rethrow_exception(current_exception);
  } catch (const std::exception& exception) {
    EmitCrashFormatted("Reason: unhandled std::exception: %s\n", exception.what());
  } catch (...) {
    EmitCrashText("Reason: unhandled non-std exception.\n");
  }
}

void SignalHandler(int signal_number, siginfo_t* signal_info, void* user_context) {
  (void)user_context;

  const void* fault_address = signal_info != nullptr ? signal_info->si_addr : nullptr;
  const int delivered_signal = signal_info != nullptr ? signal_info->si_signo : signal_number;

  if (!EnterCrashHandlerContext()) {
    ExitImmediately(delivered_signal);
  }

  BeginCrashReport();
  const char* const signal_name = ::strsignal(delivered_signal);
  const char* const fault_address_text = fault_address == nullptr ? "null address" : nullptr;
  EmitCrashFormatted("\n\nCaught signal %d (%s) at %s\n", delivered_signal,
                     signal_name != nullptr ? signal_name : "unknown",
                     fault_address_text != nullptr ? fault_address_text : "<non-null address>");
  if (fault_address != nullptr) {
    EmitCrashFormatted("Fault address: %p\n", fault_address);
  }
  PrintThreadCrashContext();

  CaptureAndPrintReadableCallStack();
  PrintRecentBreadcrumbs();
  PrintDebuggerHint();

  FlushCrashOutput();
  LaunchDebuggerIfRequested();
  ExitImmediately(delivered_signal);
}

void TerminateHandler() {
  if (!EnterCrashHandlerContext()) {
    ExitImmediately(SIGABRT);
  }

  BeginCrashReport();
  PrintTerminateReason();
  CaptureAndPrintReadableCallStack();
  PrintRecentBreadcrumbs();
  PrintDebuggerHint();
  FlushCrashOutput();
  LaunchDebuggerIfRequested();

  if (g_previous_terminate_handler != nullptr && g_previous_terminate_handler != &TerminateHandler) {
    g_previous_terminate_handler();
  }

  ExitImmediately(SIGABRT);
}

base::Status RegisterSignalHandler(int signal_number, const struct sigaction& action) {
  if (::sigaction(signal_number, &action, nullptr) == 0) {
    return base::Status::Ok();
  }

  return base::Status::IoError("sigaction(" + std::to_string(signal_number) + ") failed: " + std::strerror(errno));
}

#endif

} // namespace

base::Status InstallCrashHandler(int argc, char* argv[], InstallOptions options) {
#if defined(__linux__)
  if (!options.enabled) {
    return base::Status::Ok();
  }

  g_process_name =
    (argv != nullptr && argc > 0 && argv[0] != nullptr && argv[0][0] != '\0') ? argv[0] : kFallbackProcessName;
  g_enable_debugger_from_env = options.enable_debugger_from_env;
  g_debugger_env_var = (options.debugger_env_var != nullptr && options.debugger_env_var[0] != '\0')
                         ? options.debugger_env_var
                         : (options.enable_debugger_from_env ? kDefaultDebuggerEnvVar : nullptr);
  g_debugger_request =
    (options.enable_debugger_from_env && g_debugger_env_var != nullptr) ? std::getenv(g_debugger_env_var) : nullptr;
  g_print_readable_stack = options.print_readable_stack;
  g_use_altstack = options.install_altstack;
  g_max_frames = static_cast<int>(std::clamp<std::size_t>(options.max_frames, 1, kAbsoluteMaxDepth));

  const base::Status thread_status = RegisterCurrentThread({});
  if (!thread_status.ok()) {
    return thread_status;
  }

  struct sigaction action{};
  action.sa_sigaction = &SignalHandler;
  action.sa_flags = static_cast<int>(SA_RESETHAND | SA_SIGINFO);
  if (g_use_altstack) {
    action.sa_flags |= SA_ONSTACK;
  }
  if (::sigemptyset(&action.sa_mask) != 0) {
    return base::Status::IoError("sigemptyset failed: " + std::string(std::strerror(errno)));
  }

  constexpr std::array<int, 5> kSignals = {
    SIGILL, SIGFPE, SIGSEGV, SIGBUS, SIGABRT,
  };
  for (const int signal_number : kSignals) {
    const base::Status status = RegisterSignalHandler(signal_number, action);
    if (!status.ok()) {
      return status;
    }
  }

  if (options.capture_terminate) {
    g_previous_terminate_handler = std::set_terminate(&TerminateHandler);
  }

  RecordBreadcrumb("crash", "installed crash handler");
#else
  (void)argc;
  (void)argv;
  (void)options;
#endif

  return base::Status::Ok();
}

void DumpCurrentThreadStack(std::size_t max_frames) {
#if defined(__linux__)
  EnsureDefaultThreadName();
  BeginCrashReport();

  const int skip_frames = 1;
  const int requested_frames =
    static_cast<int>(std::clamp<std::size_t>(max_frames, 1, kAbsoluteMaxDepth - skip_frames));
  const int frame_capacity = requested_frames + skip_frames;

  std::array<void*, kAbsoluteMaxDepth> trace{};
  const int trace_size = ::backtrace(trace.data(), frame_capacity);

  EmitCrashFormatted("\nCurrent thread diagnostic stack: %s (tid=%llu)\n", CurrentThreadNameCString(),
                     static_cast<unsigned long long>(CurrentThreadId()));

  if (trace_size <= skip_frames) {
    EmitCrashText("<stack unavailable>\n");
    FlushCrashOutput();
    return;
  }

  PrintReadableCallStack(trace.data(), trace_size, skip_frames, true);
  FlushCrashOutput();
#else
  (void)max_frames;
#endif
}

base::Status RegisterCurrentThread(std::string_view thread_name) {
#if defined(__linux__)
  if (thread_name.empty()) {
    EnsureDefaultThreadName();
  } else {
    AssignCurrentThreadName(thread_name);
  }

  const base::Status altstack_status = InstallAlternateSignalStackForCurrentThread();
  if (!altstack_status.ok()) {
    return altstack_status;
  }

  g_thread_registration_completed = true;
#else
  (void)thread_name;
#endif

  return base::Status::Ok();
}

void SetThreadCrashContext(const ThreadCrashContext& context) noexcept {
#if defined(__linux__)
  CopyTextToBuffer(context.current_message, g_context_current_message.data(), g_context_current_message.size());
  CopyTextToBuffer(context.scan_uid, g_context_scan_uid.data(), g_context_scan_uid.size());
  CopyTextToBuffer(context.scan_title, g_context_scan_title.data(), g_context_scan_title.size());
  CopyTextToBuffer(context.stage, g_context_stage.data(), g_context_stage.size());
  CopyTextToBuffer(context.ismrmrd_dataset_path, g_context_ismrmrd_dataset_path.data(),
                   g_context_ismrmrd_dataset_path.size());
  CopyTextToBuffer(context.output, g_context_output.data(), g_context_output.size());
  g_thread_crash_context = {
    .current_message = g_context_current_message.data(),
    .scan_uid = g_context_scan_uid.data(),
    .scan_title = g_context_scan_title.data(),
    .stage = g_context_stage.data(),
    .ismrmrd_dataset_path = g_context_ismrmrd_dataset_path.data(),
    .output = g_context_output.data(),
  };
  g_thread_crash_context_initialized = true;
#else
  (void)context;
#endif
}

void ClearThreadCrashContext() noexcept {
#if defined(__linux__)
  g_context_current_message[0] = '\0';
  g_context_scan_uid[0] = '\0';
  g_context_scan_title[0] = '\0';
  g_context_stage[0] = '\0';
  g_context_ismrmrd_dataset_path[0] = '\0';
  g_context_output[0] = '\0';
  g_thread_crash_context = {};
  g_thread_crash_context_initialized = false;
#endif
}

ScopedThreadRegistration::ScopedThreadRegistration(std::string_view thread_name) {
#if defined(__linux__)
  if (g_thread_name_initialized) {
    had_previous_name_ = true;
    CopyTextToBuffer(g_thread_name_storage.data(), previous_name_, ScopedThreadRegistration::kStoredThreadNameCapacity);
  }
#else
  (void)thread_name;
#endif
  status_ = RegisterCurrentThread(thread_name);
}

ScopedThreadRegistration::~ScopedThreadRegistration() {
#if defined(__linux__)
  if (had_previous_name_) {
    AssignCurrentThreadName(previous_name_);
  } else {
    AssignCurrentThreadName({});
  }
#endif
}

void RecordBreadcrumb(const Breadcrumb& breadcrumb) noexcept {
#if defined(__linux__)
  EnsureCurrentThreadReadyForBreadcrumbs();
  GlobalRingBuffer().push(CurrentThreadId(), CurrentThreadNameCString(),
                          breadcrumb.category.empty() ? std::string_view("general") : breadcrumb.category,
                          breadcrumb.message.empty() ? std::string_view("<empty>") : breadcrumb.message,
                          SignalSafeMonotonicMilliseconds());
#else
  (void)breadcrumb;
#endif
}

void RecordBreadcrumb(std::string_view category, std::string_view message) noexcept {
  RecordBreadcrumb(Breadcrumb{category, message});
}

} // namespace ksj::crash
