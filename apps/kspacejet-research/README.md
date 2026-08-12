# `ksj-research`

`ksj-research` is the runner for frozen, reproducible experiments against
KSpaceJet, Gadgetron, BART Streams, and conditionally MRIReco.jl. It owns baseline
locks, dataset freezing, deterministic replay schedules, external collection, analysis,
and claim audits.

When `KSJ_BUILD_APPLICATIONS=ON`, `ksj-research` participates in the default build and,
when installation rules are enabled, the standard installation. It cannot add fields,
callbacks, dependencies, or fast paths to the reconstruction runtime.

`KSJ_BUILD_RESEARCH` controls only `tests/research` targets. It does not control this
application.
