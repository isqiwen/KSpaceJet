# `ksj-research`

`ksj-research` is an installed KSpaceJet research application scaffold. Its help and JSON
help report `status: "scaffold"`; listed command names are reserved, and every requested
operational action currently returns an `unimplemented` error.

When `KSJ_BUILD_APPLICATIONS=ON`, `ksj-research` participates in the default build and,
when installation rules are enabled, the standard installation. It cannot add fields,
callbacks, dependencies, or fast paths to the reconstruction runtime.

`KSJ_BUILD_RESEARCH` controls only `tests/research` targets. It does not control this
application.
