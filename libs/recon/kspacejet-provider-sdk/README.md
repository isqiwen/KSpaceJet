# kspacejet-provider-sdk

`kspacejet-provider-sdk` publishes the versioned C Provider ABI v1.  Its CMake
target is `KSpaceJet::provider_sdk`; it is header-only so a Provider does not
link against a KSpaceJet C++ runtime or exchange STL objects across a DLL/SO
boundary.

```cmake
target_link_libraries(my_provider PRIVATE KSpaceJet::provider_sdk)
target_compile_definitions(my_provider PRIVATE KSJ_PROVIDER_BUILDING_PLUGIN=1)
```

The public header is:

```c
#include <kspacejet/provider/v1/provider.h>
```

## ABI boundary

`ksj_provider_query` is the single dynamic-library entry symbol.  It returns a
versioned provider descriptor and a `ksj_provider_api_v1` lifecycle table:

`operator_create` → `execution_context_create` → `key_state_init` →
`operator_on_start` → zero or more `operator_process_batch` →
`operator_on_scan_end` or `operator_on_cancel` → reset/destroy.

All descriptors have `struct_size`, ABI major/minor, capability bits and
reserved fields through `ksj_provider_abi_header`.  Strings and errors are
borrowed UTF-8 views; C++ exceptions, cross-module `new/delete`, and STL types
are prohibited at the ABI boundary.

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
`isolated-strict-online` deployment claim; that profile requires a separately
implemented and approved worker fault boundary.
