#include "kspacejet/process_runtime/cleanup_registry.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>

namespace {

using namespace std::chrono_literals;

TEST(CleanupRegistryTests, FinalClearDrainsEntryAddedWhileFirstClearWasRunning) {
  ksj::process_runtime::CleanupHelper helper{"test cleanup helper"};
  std::atomic<int> first_callback_count{0};
  std::atomic<int> late_callback_count{0};
  std::atomic<int> first_clear_result{-1};
  std::promise<void> first_callback_started_promise;
  auto first_callback_started = first_callback_started_promise.get_future();
  std::promise<void> release_first_callback_promise;
  auto release_first_callback = release_first_callback_promise.get_future().share();

  helper.Add("first", [&] {
    ++first_callback_count;
    first_callback_started_promise.set_value();
    release_first_callback.wait();
    return 0;
  });

  std::thread clearer([&] {
    first_clear_result = helper.Clear();
  });
  if (first_callback_started.wait_for(2s) != std::future_status::ready) {
    release_first_callback_promise.set_value();
    clearer.join();
    FAIL() << "first cleanup callback did not start";
    return;
  }

  std::promise<void> late_add_done_promise;
  auto late_add_done = late_add_done_promise.get_future();
  std::thread late_adder([&] {
    helper.Add("late", [&] {
      ++late_callback_count;
      return 0;
    });
    late_add_done_promise.set_value();
  });

  EXPECT_EQ(late_add_done.wait_for(50ms), std::future_status::timeout);
  release_first_callback_promise.set_value();
  clearer.join();
  late_adder.join();

  EXPECT_EQ(first_clear_result.load(), 0);
  EXPECT_EQ(first_callback_count.load(), 1);
  EXPECT_EQ(late_callback_count.load(), 0);

  EXPECT_EQ(helper.Clear(), 0);
  EXPECT_EQ(late_callback_count.load(), 1);
  EXPECT_EQ(helper.Clear(), 0);
  EXPECT_EQ(late_callback_count.load(), 1);
}

} // namespace
