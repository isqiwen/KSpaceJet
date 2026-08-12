#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <typeindex>
#include <type_traits>
#include <utility>

namespace ksj::process_runtime {

// MessageLoop is the common base for objects that own a worker thread and
// consume process-local messages posted from other threads.
//
// It owns only the threading mechanics: worker lifetime, stop requests,
// queue synchronization, asynchronous post(), synchronous send(), and dispatch
// into the derived class. It deliberately does not define network protocol
// formats, reconstruction semantics, socket routing, or payload layout.
//
// Callers post strongly-typed message objects instead of packing pointers and
// integers into generic slots. The concrete type documents the message meaning
// and allows payload ownership to be expressed with normal C++ RAII types such
// as std::unique_ptr.
class MessageLoop {
public:
  MessageLoop() = default;
  MessageLoop(const MessageLoop&) = delete;
  MessageLoop& operator=(const MessageLoop&) = delete;
  virtual ~MessageLoop();

  void set_title(std::string title);
  [[nodiscard]] const std::string& title() const noexcept;

  [[nodiscard]] bool start();
  [[nodiscard]] bool stop(std::chrono::milliseconds timeout);
  void request_stop() noexcept;
  [[nodiscard]] bool is_running() const noexcept;
  [[nodiscard]] bool stop_requested() const noexcept;
  [[nodiscard]] bool wait_until_idle(std::chrono::milliseconds timeout);

  // Queue a message and return immediately. The message is moved into the
  // loop's private queue and later handled by the worker thread.
  template <typename T> [[nodiscard]] bool post(T&& message) {
    using MessageType = std::decay_t<T>;
    static_assert(std::is_move_constructible_v<MessageType>);
    return enqueue(std::make_unique<QueuedTypedMessage<MessageType>>(std::forward<T>(message), nullptr));
  }

  // Queue a message and wait until the worker thread has handled it. The
  // returned value is the derived handler's success/failure result.
  template <typename T> [[nodiscard]] bool send(T&& message) {
    using MessageType = std::decay_t<T>;
    static_assert(std::is_move_constructible_v<MessageType>);
    auto completion = std::make_shared<std::promise<bool>>();
    auto future = completion->get_future();
    if (!enqueue(std::make_unique<QueuedTypedMessage<MessageType>>(std::forward<T>(message), completion))) {
      return false;
    }
    return future.get();
  }

  [[nodiscard]] int queued_message_count() const;
  [[nodiscard]] int processed_message_count() const noexcept;

protected:
  // Derived loops dispatch the queued message to their strongly-typed handler.
  // The raw pointer is valid only for the duration of this call; use
  // try_handle<T>() to keep type checks and casts in one place.
  virtual bool handle_queued_message(std::type_index message_type, void* message);
  virtual void on_loop_started() {}
  virtual void on_loop_stopped() {}

  template <typename MessageType, typename Handler>
  [[nodiscard]] static std::optional<bool> try_handle(std::type_index message_type, void* message, Handler&& handler) {
    if (message_type != std::type_index(typeid(MessageType))) {
      return std::nullopt;
    }

    auto& typed_message = *static_cast<MessageType*>(message);
    if constexpr (std::is_void_v<std::invoke_result_t<Handler, MessageType&>>) {
      std::invoke(std::forward<Handler>(handler), typed_message);
      return true;
    } else {
      return static_cast<bool>(std::invoke(std::forward<Handler>(handler), typed_message));
    }
  }

private:
  struct QueuedMessageBase {
    explicit QueuedMessageBase(std::shared_ptr<std::promise<bool>> completion_signal) noexcept
        : completion(std::move(completion_signal)) {}
    virtual ~QueuedMessageBase() = default;
    virtual bool dispatch(MessageLoop& loop) = 0;

    std::shared_ptr<std::promise<bool>> completion;
  };

  template <typename T> struct QueuedTypedMessage final : QueuedMessageBase {
    template <typename U>
    QueuedTypedMessage(U&& payload, std::shared_ptr<std::promise<bool>> completion_signal)
        : QueuedMessageBase(std::move(completion_signal)), message(std::forward<U>(payload)) {}

    bool dispatch(MessageLoop& loop) override { return loop.dispatch_message(message); }

    T message;
  };

  template <typename T> bool dispatch_message(T& message) {
    return handle_queued_message(std::type_index(typeid(T)), static_cast<void*>(&message));
  }

  [[nodiscard]] bool enqueue(std::unique_ptr<QueuedMessageBase> message);
  void run(std::stop_token stop_token);

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::condition_variable stopped_cv_;
  std::condition_variable idle_cv_;
  std::deque<std::unique_ptr<QueuedMessageBase>> queue_;
  std::jthread worker_;
  std::string title_{"MessageLoop"};
  bool running_{false};
  int active_message_count_{0};
  int processed_message_count_{0};
};

} // namespace ksj::process_runtime
