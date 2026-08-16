# Provider implementation layout

A Provider is one independently loaded dynamic-library and trust boundary. An
Operator is a single-purpose pipeline operation exposed by that Provider. A
Provider may expose multiple Operators only when they share a coherent data
domain, ABI, lifecycle, resource model, and release boundary.

## Product catalog and planned interfaces

[`catalog.json`](catalog.json) is the canonical in-tree product
Provider/Operator map. It distinguishes `implemented-development` entries,
which have a module and exact `OperatorContract`, from `planned` entries.
Planned Operators live only under [`interfaces/`](interfaces/): an
`OperatorInterface` reserves their identity, semantic ports, and
configuration-key boundary, but is neither a loadable Provider nor a
Pipeline-resolvable contract.

Provider IDs, Provider slugs, and Operator IDs are globally unique within this
catalog so an identity is reserved exactly once before implementation.

Do not add a `planned` entry to this CMake tree, a Provider descriptor, a
ResolvedPipeline, or an ExecutionPlan. Promote it atomically only with its
implementation, exact contract, tests, and catalog status. Planned interfaces
make no claim of runtime support until that atomic promotion is complete.

All in-tree Providers use this layout:

```text
providers/kspacejet-<provider>/
  CMakeLists.txt
  README.md
  contracts/
    <operator>.json                # one contract per pipeline-resolvable Operator
  src/
    provider_entry.cpp             # only the exported ksj_provider_query ABI boundary
    provider_api.hpp               # private Provider API declaration used by the entrypoint
    provider_api.cpp               # descriptor construction and lifecycle dispatch
    provider_state.hpp             # only shared private state and opaque-handle definitions
    operators/
      <operator>.hpp               # private descriptor and lifecycle declarations
      <operator>.cpp               # exactly one substantive Operator implementation
    support/                        # optional, meaningful shared multi-Operator domain code
      <domain>.{hpp,cpp}
```

The ABI entrypoint must delegate immediately to `provider_api.cpp`; it must
not contain an algorithm, mutable operator state, or a second dispatch model.
`provider_state.hpp` owns only shared private state and opaque ABI-handle
definitions, not an inventory of Operator declarations. Every Operator owns a
private `.hpp` and `.cpp` from its first implementation onward. An Operator
implementation owns its algorithm-specific configuration, validation,
processing and terminal behavior. Generic ABI checks, descriptor helpers and
error helpers come from the SDK-private
`<kspacejet/provider/detail/provider_support.hpp>` header. Multiple Operators
may share code only through a functionally named `src/support/<domain>` unit;
never use a generic `provider_common` or `provider_internal` dump file, and
never put domain code in another Provider or the host runtime.
Use the same functional names for namespaces (`api`, `operators`, `state`, or
the domain name), rather than an `internal` catch-all namespace.

Provider diagnostics use `<kspacejet/logging/logging.hpp>` and `KSJ_LOG_*`.
Providers only emit events through the host-configured core logger; they never
configure, redirect, flush, or shut it down, and they never write diagnostic
text directly to stdout or stderr. Log unexpected ABI/lifecycle failures, not
ordinary per-firing validation rejections that the host already receives via
the Provider error view.

`OperatorContract` never declares a per-Operator execution-profile list.
Profile selection belongs to `PipelineDefinition`, `MachinePolicy`, admission,
and the runtime. An OperatorContract declares only its identity and typed
ports; node-owned `NodePlanningRequirements` selects plan-specific scheduling,
resource, rate, terminal, and topology bounds. The Provider ABI declares the
executable capability upper bound and lifecycle.

Use [`sdk/templates/provider`](../sdk/templates/provider) as the starting
scaffold. The template is intentionally unbuilt: copy it into a new Provider,
replace every placeholder, add one `operators/*.cpp` and one contract JSON per
pipeline-resolvable Operator, then register the Provider explicitly in
`providers/CMakeLists.txt`. A pure ABI conformance fixture may omit its
contract only when its README explicitly says it is not Pipeline-resolvable.

Do not add compatibility aliases or preserve an obsolete module, target,
contract or source filename: KSpaceJet is unreleased and uses one canonical
name for each Provider and Operator.
