# KSpaceJet type registry

[`registry.json`](registry.json) is the readable, checked-in source of
the exact executable payload types. It contains only types implemented by a
current Provider or by the graph/runtime public-adapter path; planned
interfaces use semantic names only and must not add speculative registry
entries. The public adapter currently resolves only the opaque
`ismrmrd.acquisition`, `ismrmrd.waveform`, and `ismrmrd.image` message
envelopes. `ksj.control-message` is an internal generic
control/diagnostic type, not a public ISMRMRD binding.

Each entry separates two concerns:

- `identity` is the complete machine structural descriptor: TypeRef, payload
  kind and element type, ordered dimensions, layout/strides, memory-domain
  eligibility, alignment, and mutability.
- `payload_semantics` and `metadata_semantics` explain what those bytes mean
  to a developer. They are intentionally readable documentation rather than
  copied hash literals in every Provider contract.

`tools/type_registry/generate.py` canonicalizes the structural descriptor as
compact sorted-key UTF-8 JSON and computes:

```text
SHA-256("kspacejet.type-identity\\0" + canonical-structural-json)
```

It emits the C++ factories in
`<kspacejet/recon/type_registry.hpp>` and the C Provider helpers in
`<kspacejet/provider/type_registry.h>`. Do not edit generated headers by
hand. Regenerate after an approved registry change:

```sh
python3 tools/type_registry/generate.py --project-root .
python3 tools/type_registry/generate.py --project-root . --check
```

A semantic change never edits an existing type in place. Allocate a distinct
TypeRef, then add its structural descriptor, semantics, generated factories,
Providers, contracts, and tests together.
