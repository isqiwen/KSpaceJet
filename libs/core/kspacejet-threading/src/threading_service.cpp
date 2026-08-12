#include "kspacejet/threading/threading_service.hpp"

#include <algorithm>
#include <utility>

namespace ksj::threading {

namespace {

std::size_t normalize_worker_count(std::size_t worker_count) noexcept {
  return std::max<std::size_t>(1, worker_count);
}

} // namespace

ThreadPoolLease::LeaseState::LeaseState(std::string lease_name, std::size_t workers, ThreadPool::ShutdownPolicy policy,
                                        ThreadPoolOptions options)
    : name(std::move(lease_name)), pool(workers, std::move(options)), release_policy(policy) {}

ThreadPoolLease::LeaseState::~LeaseState() {
  shutdown(release_policy);
}

bool ThreadPoolLease::LeaseState::released() const {
  std::lock_guard lock(mutex);
  return is_released;
}

void ThreadPoolLease::LeaseState::shutdown(ThreadPool::ShutdownPolicy policy) noexcept {
  {
    std::lock_guard lock(mutex);
    if (is_released) {
      return;
    }
    is_released = true;
  }

  pool.shutdown(policy);
}

ThreadPoolLease::ThreadPoolLease(std::shared_ptr<LeaseState> state) : state_(std::move(state)) {}

ThreadPoolLease::ThreadPoolLease(ThreadPoolLease&& other) noexcept : state_(std::move(other.state_)) {}

ThreadPoolLease& ThreadPoolLease::operator=(ThreadPoolLease&& other) noexcept {
  if (this != &other) {
    release();
    state_ = std::move(other.state_);
  }
  return *this;
}

ThreadPoolLease::~ThreadPoolLease() {
  release();
}

std::string_view ThreadPoolLease::name() const noexcept {
  return state_ ? std::string_view(state_->name) : std::string_view{};
}

void ThreadPoolLease::clear_pending() {
  if (state_) {
    state_->pool.clear_pending();
  }
}

void ThreadPoolLease::wait() const {
  if (state_) {
    state_->pool.wait();
  }
}

bool ThreadPoolLease::wait_until(const std::chrono::steady_clock::time_point deadline) const {
  return !state_ || state_->pool.wait_until(deadline);
}

void ThreadPoolLease::shutdown(ThreadPool::ShutdownPolicy policy) noexcept {
  if (state_) {
    state_->shutdown(policy);
  }
}

void ThreadPoolLease::release() noexcept {
  if (state_) {
    state_->shutdown(state_->release_policy);
    state_.reset();
  }
}

std::size_t ThreadPoolLease::worker_count() const {
  return state_ ? state_->pool.worker_count() : 0;
}

std::size_t ThreadPoolLease::active_count() const {
  return state_ ? state_->pool.active_count() : 0;
}

std::size_t ThreadPoolLease::queued_count() const {
  return state_ ? state_->pool.queued_count() : 0;
}

bool ThreadPoolLease::idle() const {
  return !state_ || state_->pool.idle();
}

bool ThreadPoolLease::accepting_tasks() const {
  return state_ && !state_->released() && state_->pool.accepting_tasks();
}

std::vector<ThreadPoolWorkerInfo> ThreadPoolLease::worker_infos() const {
  return state_ ? state_->pool.worker_infos() : std::vector<ThreadPoolWorkerInfo>{};
}

ThreadingService::ThreadingService(std::size_t max_workers_per_lease)
    : max_workers_per_lease_(normalize_worker_count(max_workers_per_lease)) {}

ThreadingService::~ThreadingService() {
  shutdown_all(ThreadPool::ShutdownPolicy::discard_pending);
}

ThreadPoolLease ThreadingService::acquire(WorkerRequest request) {
  const auto worker_count = resolve_worker_count(request);
  auto state = std::make_shared<ThreadPoolLease::LeaseState>(std::move(request.name), worker_count,
                                                             request.release_policy, std::move(request.pool_options));

  {
    std::lock_guard lock(mutex_);
    leases_.push_back(state);
  }

  return ThreadPoolLease(std::move(state));
}

void ThreadingService::shutdown_all(ThreadPool::ShutdownPolicy policy) noexcept {
  for (auto& state : active_states()) {
    state->shutdown(policy);
  }
}

std::size_t ThreadingService::active_lease_count() const {
  return active_states().size();
}

std::size_t ThreadingService::active_worker_count() const {
  std::size_t total = 0;
  for (auto& state : active_states()) {
    total += state->pool.worker_count();
  }
  return total;
}

std::size_t ThreadingService::resolve_worker_count(const WorkerRequest& request) const noexcept {
  const auto minimum = normalize_worker_count(request.min_workers);
  const auto maximum = std::max(minimum, normalize_worker_count(request.max_workers));
  const auto requested = normalize_worker_count(request.preferred_workers);
  return std::min(max_workers_per_lease_, std::clamp(requested, minimum, maximum));
}

std::vector<std::shared_ptr<ThreadPoolLease::LeaseState>> ThreadingService::active_states() const {
  std::vector<std::shared_ptr<ThreadPoolLease::LeaseState>> states;
  std::lock_guard lock(mutex_);
  for (auto it = leases_.begin(); it != leases_.end();) {
    if (auto state = it->lock()) {
      if (!state->released()) {
        states.push_back(std::move(state));
      }
      ++it;
    } else {
      it = leases_.erase(it);
    }
  }
  return states;
}

} // namespace ksj::threading
