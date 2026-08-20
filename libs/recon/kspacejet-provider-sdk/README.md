# kspacejet-provider-sdk

`kspacejet-provider-sdk` publishes the C Provider ABI. Its CMake
target is `KSpaceJet::provider_sdk`; it is header-only so a Provider does not
link against a KSpaceJet C++ runtime or exchange STL objects across a DLL/SO
boundary.

```cmake
target_link_libraries(my_provider PRIVATE KSpaceJet::provider_sdk)
target_compile_definitions(my_provider PRIVATE KSJ_PROVIDER_BUILDING_PLUGIN=1)
```

The public header is:

```c
#include <kspacejet/provider/provider.h>
```

Provider implementations that consume or produce a registered payload type
also include the generated registry surface:

```c
#include <kspacejet/provider/type_registry.h>
```

Use the generated `ksj_type_registry_<type>()` factory to make a
descriptor and `ksj_type_registry_matches_<type>()` after normal
ABI-header validation. Do not copy a type reference, type-identity digest, or
payload-layout literal into a Provider. The registry is the single readable
definition; the ABI carries its one exact `type_identity_digest` only for
machine comparison.

## ABI boundary

`ksj_provider_query` is the single dynamic-library entry symbol.  It returns a
provider descriptor and a `ksj_provider_api` lifecycle table:

`operator_create` → `execution_context_create` → `key_state_init` →
`operator_on_start` → zero or more `operator_process_batch` →
`operator_on_scan_end` or `operator_on_cancel` → reset/destroy.

All descriptors have `struct_size`, capability bits and reserved fields
through `ksj_provider_abi_header`. Strings and errors are
borrowed UTF-8 views; C++ exceptions, cross-module `new/delete`, and STL types
are prohibited at the ABI boundary.

`ksj_type_descriptor_view` has a readable `type_ref` such as
`ksj.image-frame`, one registry-derived `type_identity_digest`, and the
structural ABI fields needed by an in-process callback. The old split payload,
metadata, and descriptor digest fields are not part of this ABI.

## Host-enforced firing capabilities

`operator_process_batch` and normal `operator_on_scan_end` receive an opaque,
host-owned `ksj_firing_lease` plus callback tables.  They can obtain only:

- input batch views valid for the stated lease lifetime;
- pre-accounted scratch and key-state views;
- a bounded `ksj_output_grant`, which must be mapped, filled, sealed and then
  host-validated before fan-out commit;
- bounded input-retention handles; and
- registered async tokens, completed or released exactly once.

Providers cannot publish directly or retain a callback/raw payload pointer.
`on_cancel` intentionally receives no firing lease or output-grant callback:
it may stop/release provider work but must never emit ordinary MRI data.

This SDK defines neither a loader nor a process-isolation/data-plane protocol.
Current in-process providers are therefore not evidence for an
`isolated-provider-runtime` deployment claim; that profile requires a
separately implemented and approved worker fault boundary.
