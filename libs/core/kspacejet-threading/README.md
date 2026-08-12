# kspacejet-threading

`kspacejet-threading` 提供 KSpaceJet 内部统一的 C++20 线程池基础设施。该组件属于 `KSpaceJet::core`，面向运行时服务、后台任务和算法内部并行，不包含 MRI 领域语义。

## 组件职责

```text
include/kspacejet/threading/thread_pool.hpp
  ThreadPool、任务提交、等待、关闭和线程池状态查询

include/kspacejet/threading/threading_service.hpp
  ThreadingService、WorkerRequest、ThreadPoolLease

include/kspacejet/threading/openmp_settings_scope.hpp
  OpenMpSettingsScope，用 RAII 临时调整并恢复 OpenMP runtime 设置

src/thread_pool.cpp
  ThreadPool worker 生命周期、任务队列、等待和关闭实现

src/threading_service.cpp
  线程池租约创建、租约统计和全局关闭实现
```

## ThreadPool

`ThreadPool` 是固定工作线程池。构造和 `resize()` 接收 worker 数量；传入 `0` 时会归一化为 `1`，保证已经提交的任务不会进入无 worker 可执行的状态。

主要接口如下：

| 接口 | 语义 |
| --- | --- |
| `post(function, args...)` | 提交 fire-and-forget 任务，返回任务是否成功入队。调用者不接收结果。 |
| `submit(function, args...)` | 提交任务并返回 `std::future<R>`，用于获取返回值或异常。 |
| `wait()` | 阻塞等待 queued task 和 active task 全部完成。 |
| `wait_for(duration)` | 在给定时长内等待线程池进入 idle 状态。 |
| `wait_until(deadline)` | 在给定时间点前等待线程池进入 idle 状态。 |
| `clear_pending()` | 丢弃尚未开始执行的 queued task。 |
| `shutdown(policy)` | 停止接收新任务，并按策略处理未完成任务。 |
| `worker_count()` | 当前 worker 数量。 |
| `active_count()` | 正在 worker 线程中执行的任务数量。 |
| `queued_count()` | 等待执行的任务数量。 |
| `idle()` | `active_count() == 0 && queued_count() == 0`。 |
| `accepting_tasks()` | 当前是否仍接收新任务。 |
| `unhandled_exception_count()` | fire-and-forget 任务中未捕获异常的累计数量。 |

`post()` 和 `submit()` 均为模板接口，可以接收普通函数、lambda、函数对象以及 move-only callable。接口会在入队前把 callable 和参数包装成无参任务，由 worker 线程统一执行。

## ShutdownPolicy

`ThreadPool::ShutdownPolicy` 定义线程池关闭时如何处理 queued task：

| 策略 | 行为 |
| --- | --- |
| `finish_pending` | 停止接收新任务，等待 queued task 和 active task 执行完成后退出。 |
| `discard_pending` | 停止接收新任务，丢弃 queued task，仅等待 active task 结束。 |

通过 `submit()` 创建、但在 `clear_pending()` 或 `shutdown(discard_pending)` 中被丢弃的任务不会执行；对应 `future` 在取值时会表现为 broken promise。

## ThreadingService

`ThreadingService` 是统一的线程池租约管理服务。它不直接执行任务，而是根据 `WorkerRequest` 创建 `ThreadPoolLease`，并记录当前存活租约和 worker 统计信息。

`WorkerRequest` 字段含义如下：

| 字段 | 含义 |
| --- | --- |
| `name` | 租约名称，用于日志、诊断和统计。 |
| `preferred_workers` | 期望 worker 数量。 |
| `min_workers` | 最小 worker 数量。 |
| `max_workers` | 最大 worker 数量。 |
| `release_policy` | 租约释放时使用的关闭策略。 |

`ThreadingService` 会结合服务自身的 `max_workers_per_lease()` 与请求中的最小/最大值解析最终 worker 数量。后续如果需要实现更严格的进程级并发预算，应在该服务中扩展，而不是让算法各自创建长期线程池。

## ThreadPoolLease

`ThreadPoolLease` 是线程池租约的 RAII 句柄。租约有效期间可使用与 `ThreadPool` 相同风格的 `post()`、`submit()`、`wait()`、`wait_for()`、`clear_pending()` 和状态查询接口。

租约对象销毁时会按 `WorkerRequest::release_policy` 关闭内部线程池。算法或运行时代码应将租约生命周期限定在一次明确的工作范围内，例如一个 scan、一次算法处理阶段或一个后台批处理任务。

## OpenMpSettingsScope

`OpenMpSettingsScope` 用于少数仍需要直接控制 OpenMP runtime 的算法路径。构造时记录当前 `dynamic`、`max_active_levels` 和 `num_threads` 设置，作用域内可临时调整这些设置，析构时自动恢复。

该工具只负责通用 OpenMP runtime 状态保护，不承载任何 MRI 领域语义。调用方仍应确保所在 target 已启用 OpenMP；不需要 OpenMP 的通用算法应优先使用标准 C++ 或 `ThreadingService`。

## 使用规则

- KSpaceJet 算法代码应优先通过 `QueueOpContext::threading_service()` 获取 `ThreadingService`，再申请 `ThreadPoolLease`。
- 新增算法不应直接创建长期 `std::thread`、`boost::thread` 或局部全局线程池。
- KSpaceJet backend 已经按 channel 并行执行 Q 函数；算法内部继续申请 worker 时应控制 worker 数量，避免与 channel 并行叠加导致过度并发。
- 使用共享输出缓冲区时，调用者应在读取结果前执行 `wait()` 或等待所有 `future` 完成。
- `post()` 适合不需要返回值、且任务内部能够自行处理异常的工作；需要结果、错误传播或同步点时应使用 `submit()`。
- `discard_pending` 只适用于 scan abort、任务取消或明确允许丢弃未开始任务的场景。

## 维护边界

`kspacejet-threading` 只维护通用线程基础设施，不应引入以下内容：

- MRI scan、slice、channel、Q opcode 等领域概念。
- `apps/` 下的进程私有类型。
- 特定算法的任务队列或缓存策略。

需要面向 MRI 重建算法暴露线程能力时，开放 provider 应通过其公开执行器契约传递 `ThreadingService`。
