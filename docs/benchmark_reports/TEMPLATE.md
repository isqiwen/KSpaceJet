# Benchmark Report: <suite> on <machine-id>

> 将本模板复制到 `docs/benchmark_reports/<yyyy-mm-dd>/<suite>/<machine-id>/benchmark_report.md` 后填写。删除不适用项，不要把本模板本身当作性能证据。

## Provenance

- Date:
- Git commit or tag:
- Author and reviewer:
- CMake preset and build type:
- Command line:
- Raw CSV / artifact location:

## Environment

- CPU model, socket / NUMA layout, memory, and fixed CPU affinity:
- OS, compiler, and relevant backend versions:
- Backend thread count and Intel/OpenMP wait settings:

## Workload and correctness

- Suite, public API, input sizes, value types, layouts, strides, and timing scope:
- Candidate, reference, and policy backends:
- Trials, process repetitions, calibration, and correctness tolerances:
- Correctness-gate result:

## Results and decision

- Median, mean, confidence interval, and minimum required speedup for each comparable row:
- Unstable, uncovered, or non-comparable cases:
- Recommended policy or threshold change (or explicit decision not to change it):
- Linked source/header change:

## Limitations

- Why this machine and workload do or do not represent the intended target:
- Evidence that must be collected before broader claims or a release decision:
