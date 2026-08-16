# KSpaceJet Provider starter template

Start a new Provider with
`ksj provider init <provider-slug> <operator-id> --output <directory>`. The
command copies this installed template into a new directory and rejects an
existing destination. It removes the `.in` suffixes and renames both
`src/operators/operator.{hpp,cpp}` and `contracts/operator.json` to the
concrete Operator slug. Replace every remaining `@UPPERCASE_PLACEHOLDER@`,
and then add the completed Provider to `providers/CMakeLists.txt`.
Use a hyphenated `@PROVIDER_SLUG@` for the directory/output name and an
underscore-only `@PROVIDER_TARGET@` for the internal CMake target. This is
deliberately an unbuilt scaffold: it forces an author to define the Provider's
exact ABI, typed interfaces, capability limits, and lifecycle rather than accidentally
shipping a generic no-op plugin.

The layout is mandatory for in-tree Providers:

```text
contracts/<operator>.json          one Provider-owned contract per Operator
src/provider_entry.cpp             only ksj_provider_query
src/provider_api.hpp               private Provider API declaration used by the entrypoint
src/provider_api.cpp               Provider descriptor and lifecycle dispatch
src/provider_state.hpp             shared private state and opaque-handle definitions only
src/operators/<operator>.hpp       private descriptor and lifecycle declarations
src/operators/<operator>.cpp       one substantive implementation per Operator
src/support/<domain>.{hpp,cpp}     optional, functionally named shared multi-Operator domain code
```

## Workflow

1. Choose the Provider boundary first: Operators in one Provider must share a
   coherent domain, exact payload ABI, lifecycle/resource model, and trust /
   release boundary.
2. Write the exact `OperatorContract` JSON for each Operator before coding.
   Each port names a readable registry `type_ref`; it does not copy payload,
   metadata, or descriptor digests. It owns only the typed interface
   (`operator_id` and ports). Provider configuration grammar is also
   Provider-owned until the model exposes a configuration-schema field. Do not
   add scheduling, rate, resource, terminal, reorder/forward, or
   profile-selection fields to an OperatorContract: those are node-owned
   `NodePlanningRequirements` bound in `PlanBuildRequest`.
   The executable Provider ABI remains the capability upper bound.
3. Fill the Provider descriptor and callback table in `provider_api.cpp`; keep
   `provider_entry.cpp` as a thin `noexcept` C ABI boundary only.
   `provider_state.hpp` may contain only Provider-shared private state and
   concrete opaque-handle definitions. Every Operator gets its private
   `operators/<operator>.hpp` from the start; a single-Operator Provider binds
   its callbacks directly to that Operator, while a multi-Operator Provider
   adds only the dispatch it needs. Use
   `<kspacejet/provider/detail/provider_support.hpp>` for generic ABI/error
   helpers. Put genuinely shared domain code in a functionally named
   `support/<domain>` unit, never `provider_common` or `provider_internal`.
   Use the same functional namespaces (`api`, `operators`, `state`, or a
   domain name) rather than a generic `internal` namespace.
   Include `<kspacejet/provider/type_registry.h>` and use the generated type
   factories and matchers instead of hard-coding type identity values.
   Record unexpected Provider lifecycle or algorithm failures with
   `<kspacejet/logging/logging.hpp>` and `KSJ_LOG_*`. A Provider uses the
   host-configured core logger only: it never configures, redirects, flushes,
   or shuts down logging itself, and it must not use stdout/stderr as a
   diagnostic channel.
4. Implement each Operator in its own paired `src/operators/*.hpp` and
   `src/operators/*.cpp`; never put a second algorithm into the entrypoint or
   a catch-all source file.
5. Add focused Provider-loader tests for descriptor identity, configuration,
   normal processing, terminal behavior, cancellation and invalid ABI input.
6. Update CMake's explicit source and installed-contract lists. Do not rely on
   source globs or export a second compatibility module.

The template's source files are skeletal on purpose. For a complete, buildable
reference, inspect an in-tree Provider with the same lifecycle shape, not an
unrelated algorithm.

`contracts/operator.json` starts as a conservative one-input/one-output typed
interface. Replace the Operator and port placeholders. Then create the
node-owned `NodePlanningRequirements` in the pipeline/plan input that selects
finite scheduling, rate, resource, terminal, and topology requirements for
each use of that interface.
