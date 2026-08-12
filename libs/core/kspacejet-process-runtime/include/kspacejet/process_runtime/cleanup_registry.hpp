#pragma once

#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace ksj::process_runtime {

using cleanup_executor = std::function<int()>;
using cleanup_error_info_fetcher = std::function<std::string(int)>;

class CleanupExecutor {
public:
  explicit CleanupExecutor(cleanup_executor executor);
  CleanupExecutor(cleanup_executor executor, cleanup_error_info_fetcher info_fetcher);

  [[nodiscard]] int Execute() const;
  [[nodiscard]] std::string GetErrorInfo(int err_code) const;

private:
  cleanup_executor executor_;
  cleanup_error_info_fetcher info_fetcher_;
};

class CleanupHelper {
public:
  explicit CleanupHelper(std::string name, std::mutex* clear_mutex = nullptr);
  ~CleanupHelper();

  void Add(const std::string& tag, cleanup_executor target);
  void Add(const std::string& tag, cleanup_executor target, cleanup_error_info_fetcher info_fetcher);
  int Clear();
  int Clear(const std::string& tag);
  void RemoveAll();
  void Remove(const std::string& tag);

private:
  int DoClear();
  int DoClear(const std::string& tag);

private:
  std::map<std::string, std::vector<CleanupExecutor>> executors_;
  std::mutex mutex_;
  std::string name_;
  std::mutex* clear_mutex_;
};

CleanupHelper* cleanup_helper();
CleanupHelper* quit_scan_helper();

} // namespace ksj::process_runtime
