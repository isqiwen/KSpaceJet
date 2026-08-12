# Parallelism Convention

KSpaceJet runs on machines with different CPU counts, processor topologies, affinity masks, and container limits. Parallel
algorithm code must therefore use a runtime thread budget; it must not encode the developer machine's core count.

The detailed runtime model, including KeyShard scheduling, NUMA placement, gang permits, multi-scan fairness and proof
boundaries, is defined in [MRI Pipeline, Parallelism and Proof Model](../architecture/streaming_pipeline_parallelism_theory.md).

## Parallelize At One Dominant Level

Choose the highest useful independent work level, such as scan, channel, slice, frame, or block. When that level has
enough work to occupy the process thread budget, lower levels must remain serial. For example, radial SBI reconstructs
independent slices in parallel and keeps each slice's frame-level NUFFT and FFT work single-threaded.

A thread count of one is valid when it explicitly disables nested parallelism. It does not mean that the algorithm
assumes a single-core machine.

Do not enable nested OpenMP merely because an inner loop is parallelizable. Nested teams multiply the outer worker
count and can cause oversubscription, memory-bandwidth contention, extra scratch memory, and unstable latency.

## Runtime Thread Budget

OpenMP code that owns a top-level parallel region must limit its team by all of the following:

- the number of independent work items;
- processors available to the process or its affinity mask;
- the OpenMP maximum thread count, including `OMP_NUM_THREADS`;
- `OMP_THREAD_LIMIT`.

The effective budget is therefore equivalent to:

```cpp
const int thread_budget =
  std::max(1, std::min({omp_get_num_procs(), omp_get_max_threads(), omp_get_thread_limit()}));
const int thread_count = std::min(work_item_count, thread_budget);
```

Do not hard-code values such as 4, 8, 20, or 28 as a machine-wide worker count. A smaller algorithm-specific cap is
allowed only when a benchmark shows that the kernel saturates before the runtime budget, and the reason is documented.

The calculation above applies only when the caller owns the process's dominant parallel region. An Operator running
inside the KSpaceJet streaming runtime does not own the machine-wide budget: it must use the host-granted permit count
from its `ExecutionPlan`. It must not recalculate `omp_get_num_procs()` and start that many threads while other scans or
Operator instances are active. A multithreaded backend call is a gang task whose coordinator and backend threads all
consume the same host permit pool.

Open providers should accept a caller-owned executor or a documented thread budget. Existing OpenMP paths must follow
the same budget and nesting rules.

## Numerical Backends

KSpaceJet schedules parallel work above numerics kernels. IPP and MKL therefore remain single-threaded by default to avoid
hidden nested teams. A backend may use internal threading only when the caller is serial, the policy is benchmarked on
representative machines, and no outer channel/slice/block parallel region is active.

## Validation

Changes to parallel execution require:

- correctness comparison against a representative ISMRMRD fixture;
- repeated performance trials without unrelated CPU or I/O load;
- tests with at least one restricted budget such as `OMP_NUM_THREADS=2` or `OMP_THREAD_LIMIT=2`;
- checks for thread-private scratch and race-free shared state.

Thread affinity such as `OMP_PLACES` or `OMP_PROC_BIND` is deployment policy. Do not hard-code it in algorithms because
homogeneous servers, hybrid desktop CPUs, virtual machines, and containers require different placement strategies.
