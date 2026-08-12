# kspacejet-crash

`kspacejet-crash` 提供 KSpaceJet 进程级 crash 诊断能力。该组件属于 `KSpaceJet::core`，用于安装 crash handler、记录 breadcrumb、注册线程诊断上下文、打印可读调用栈，并在故障时输出最近运行轨迹。

该组件是诊断设施，不是故障恢复机制。发生致命信号或未处理 C++ termination 后，进程仍会退出。

## 目录结构

```text
include/kspacejet/crash/handler.hpp
  crash handler 安装、线程注册、线程诊断上下文和栈打印接口

include/kspacejet/crash/breadcrumb.hpp
  breadcrumb 记录接口

include/kspacejet/crash/ring_buffer.hpp
  breadcrumb ring buffer

src/handler.cpp
  Linux crash handler、信号处理、terminate 处理、调用栈输出和 GDB 附加入口

src/ring_buffer.cpp
  最近 breadcrumb 存储实现
```

## Crash Handler

进程启动时应通过 `ksj::process_runtime::crash::install_current_executable_crash_handler()` 或底层 `ksj::crash::InstallCrashHandler()` 安装 crash handler。

`InstallOptions` 控制安装行为：

| 字段 | 含义 |
| --- | --- |
| `enabled` | 是否安装 handler。 |
| `debugger_env_var` | 请求附加调试器的环境变量名，默认 `USE_GDB_ON_FAULT`。 |
| `enable_debugger_from_env` | 是否读取环境变量并在故障时启动 GDB。 |
| `install_altstack` | 是否为当前线程安装 alternate signal stack。 |
| `capture_terminate` | 是否捕获未处理 C++ termination。 |
| `print_readable_stack` | 是否输出可读调用栈。 |
| `max_frames` | 调用栈最大帧数。 |

Linux 下 handler 会处理以下信号：

```text
SIGILL
SIGFPE
SIGSEGV
SIGBUS
SIGABRT
```

故障输出包含：

- 信号编号、信号名称和 fault address。
- 当前线程诊断上下文。
- 可读调用栈。
- 最近 breadcrumb。
- `USE_GDB_ON_FAULT` 调试提示；设置后会尝试启动 GDB 附加到故障进程。

非 Linux 平台上的实现能力有限，应以平台实际支持为准。

## 线程注册和诊断上下文

每个长期运行的 KSpaceJet 线程应注册线程名和 alternate signal stack。推荐使用 RAII 接口：

```cpp
ksj::crash::ScopedThreadRegistration registration("Channel 0");
```

也可以直接调用：

```cpp
ksj::crash::RegisterCurrentThread("Channel 0");
```

线程处理 scan、message 或关键阶段时，可设置当前线程诊断上下文：

```cpp
ksj::crash::SetThreadCrashContext({
  .current_message = "MMS_RUN_PROC",
  .scan_uid = "19",
  .scan_title = "streaming_reconstruction",
  .stage = "dispatch",
  .ismrmrd_dataset_path = "/path/to/acquisition.h5",
  .output = "/path/to/output",
});
```

阶段结束后应调用 `ClearThreadCrashContext()`，避免后续 crash 报告携带过期上下文。

## Breadcrumb

breadcrumb 用于记录低成本的最近运行轨迹：

```cpp
ksj::crash::RecordBreadcrumb("service", "kspacejet-be startup complete");
ksj::crash::RecordBreadcrumb("be.scan", "init_env.begin");
```

breadcrumb 存入全局 ring buffer。故障时 handler 会打印最近记录，用于补充调用栈无法表达的业务阶段信息。

记录规则如下：

- category 应短小稳定，例如 `service`、`be.scan`、`be.message`。
- message 应描述阶段或状态，不应写入大块数据。
- 不应记录敏感信息、完整 raw payload 或高频 per-sample 数据。
- 高频路径只记录关键状态转换，避免冲掉真正有用的故障前轨迹。

## RingBuffer

`RingBuffer` 是固定容量的最近事件缓存，默认容量为 `256`。每条记录包含 sequence、monotonic time、thread id、thread name、category 和 message。

通常业务代码不需要直接操作 `RingBuffer`，应使用 `RecordBreadcrumb()`。单元测试或诊断工具需要检查 ring buffer 行为时，可使用 `GlobalRingBuffer()`。

## 手动栈输出

`DumpCurrentThreadStack(max_frames)` 可在非致命错误路径输出当前线程调用栈，例如 sync barrier timeout、长时间阻塞或运行时异常诊断。

该接口仅用于诊断，不应作为正常控制流的一部分。

## 使用规则

- 所有长期运行的进程入口应安装 crash handler。
- 所有长期运行的 worker/message loop 线程应通过 `ScopedThreadRegistration` 注册线程名。
- 关键生命周期阶段应记录 breadcrumb，例如启动完成、进入主循环、scan start、scan end、abort、stop。
- 线程诊断上下文应在处理当前消息时设置，处理结束后清除。
- crash handler 内部只适合执行诊断输出和立即退出，不应加入需要复杂锁、内存分配或业务恢复的逻辑。

## 维护边界

`kspacejet-crash` 只维护通用 crash 诊断能力，不应包含以下内容：

- 具体重建算法的错误处理。
- scan 生命周期决策。
- FE/BE 重启策略。
- 日志系统替代实现。

进程级安装入口位于 `libs/core/kspacejet-process-runtime`；具体进程在 `apps/` 中调用安装入口并记录本进程 breadcrumb。
