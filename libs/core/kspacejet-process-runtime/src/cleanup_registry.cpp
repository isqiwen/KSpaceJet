#include "kspacejet/process_runtime/cleanup_registry.hpp"

#include <exception>
#include <utility>

#include "kspacejet/logging/logging.hpp"

namespace ksj::process_runtime {

namespace {

std::mutex g_cleanup_shared_mutex;
CleanupHelper g_cleanup_instance("cleanup helper", &g_cleanup_shared_mutex);
CleanupHelper g_quit_scan_instance("quit helper", &g_cleanup_shared_mutex);

} // namespace

CleanupExecutor::CleanupExecutor(cleanup_executor executor) : executor_(std::move(executor)) {}

CleanupExecutor::CleanupExecutor(cleanup_executor executor, cleanup_error_info_fetcher info_fetcher)
    : executor_(std::move(executor)), info_fetcher_(std::move(info_fetcher)) {}

int CleanupExecutor::Execute() const {
  return executor_();
}

std::string CleanupExecutor::GetErrorInfo(int err_code) const {
  try {
    return info_fetcher_(err_code);
  } catch (const std::exception& err) {
    return std::string("from CleanupExecutor: calling info fetcher failed due to \"") + err.what() + "\"";
  } catch (...) {
    return "from CleanupExecutor: calling info fetcher failed with unknown reason";
  }
}

CleanupHelper::CleanupHelper(std::string name, std::mutex* clear_mutex)
    : name_(std::move(name)), clear_mutex_(clear_mutex) {}

CleanupHelper::~CleanupHelper() {
  RemoveAll();
}

void CleanupHelper::Add(const std::string& tag, cleanup_executor target) {
  std::lock_guard<std::mutex> lock(mutex_);
  executors_[tag].emplace_back(std::move(target));
}

void CleanupHelper::Add(const std::string& tag, cleanup_executor target, cleanup_error_info_fetcher info_fetcher) {
  std::lock_guard<std::mutex> lock(mutex_);
  executors_[tag].emplace_back(std::move(target), std::move(info_fetcher));
}

int CleanupHelper::Clear() {
  if (clear_mutex_ != nullptr) {
    std::unique_lock<std::mutex> shared_lock(*clear_mutex_);
    std::unique_lock<std::mutex> lock(mutex_);
    return DoClear();
  }

  std::unique_lock<std::mutex> lock(mutex_);
  return DoClear();
}

int CleanupHelper::Clear(const std::string& tag) {
  if (clear_mutex_ != nullptr) {
    std::unique_lock<std::mutex> shared_lock(*clear_mutex_);
    std::unique_lock<std::mutex> lock(mutex_);
    return DoClear(tag);
  }

  std::unique_lock<std::mutex> lock(mutex_);
  return DoClear(tag);
}

int CleanupHelper::DoClear() {
  int success_count = 0;
  int total_count = 0;

  for (auto& [tag, executor_list] : executors_) {
    for (auto& executor : executor_list) {
      ++total_count;
      try {
        const int state = executor.Execute();
        if (state != 0) {
          KSJ_LOG_ERROR("cleanup(executors) failed, {}", executor.GetErrorInfo(state));
        }
        ++success_count;
      } catch (const std::exception& err) {
        KSJ_LOG_ERROR("cleanup(executors) error occured: {}", err.what());
      } catch (...) {
        KSJ_LOG_ERROR("cleanup(executors) unknown error occured");
      }
    }
    executor_list.clear();
  }

  executors_.clear();

  KSJ_LOG_INFO("[INFO] cleanup {} of {} from {}", success_count, total_count, name_);

  return success_count == total_count ? 0 : success_count;
}

int CleanupHelper::DoClear(const std::string& tag) {
  const auto it = executors_.find(tag);
  if (it == executors_.end()) {
    KSJ_LOG_INFO("[INFO] cleanup 0 of 0 with tag {} from {}", tag, name_);
    return 0;
  }

  int success_count = 0;
  int total_count = 0;

  for (auto& executor : it->second) {
    ++total_count;
    try {
      const int state = executor.Execute();
      if (state != 0) {
        KSJ_LOG_ERROR("cleanup(executors) failed, {}", executor.GetErrorInfo(state));
      }
      ++success_count;
    } catch (const std::exception& err) {
      KSJ_LOG_ERROR("cleanup(executors) error occured: {}", err.what());
    } catch (...) {
      KSJ_LOG_ERROR("cleanup(executors) unknown error occured");
    }
  }

  it->second.clear();
  executors_.erase(it);

  KSJ_LOG_INFO("[INFO] cleanup {} of {} with tag {} from {}", success_count, total_count, tag, name_);

  return success_count == total_count ? 0 : success_count;
}

void CleanupHelper::RemoveAll() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (executors_.empty()) {
    return;
  }

  for (auto& [tag, executor_list] : executors_) {
    executor_list.clear();
  }

  executors_.clear();

  KSJ_LOG_INFO("[INFO] remove all by {}", name_);
}

void CleanupHelper::Remove(const std::string& tag) {
  std::lock_guard<std::mutex> lock(mutex_);

  const auto it = executors_.find(tag);
  const std::size_t count = it == executors_.end() ? 0 : it->second.size();
  if (it != executors_.end()) {
    it->second.clear();
    executors_.erase(it);
  }

  KSJ_LOG_INFO("[INFO] remove {} with tag {} from {}", static_cast<int>(count), tag, name_);
}

CleanupHelper* cleanup_helper() {
  return &g_cleanup_instance;
}

CleanupHelper* quit_scan_helper() {
  return &g_quit_scan_instance;
}

} // namespace ksj::process_runtime
