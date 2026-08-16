# kspacejet-image-ops

`ksj-image-ops` is a Provider ABI plugin that groups independently
selectable, stateless image-domain Operators sharing one exact float32 image
ABI and lifecycle. It is an open development component, not a clinical
image-processing component.

It exposes three serial, synchronous Operators:

```text
image_scale_float32   {"factor":<finite-decimal>}
image_offset_float32  {"offset":<finite-decimal>}
image_clamp_float32   {"maximum":<finite-decimal>,"minimum":<finite-decimal>}
```

Every firing consumes one immutable host-normal, row-major `ksj.image-frame`
float32 payload and seals exactly one payload of the same descriptor and byte
size. The Provider obtains and checks that descriptor through the generated
type registry, which keeps the readable TypeRef and machine identity in one
place. All three use a Provider-owned finite decimal grammar: no whitespace,
exponent, leading `+`, leading zero, trailing fractional zero, or `-0`; clamp
also requires the keys in the order shown and `minimum <= maximum`. Examples:

```json
{"factor":1.5}
{"offset":-0.25}
{"maximum":0.75,"minimum":-0.5}
```

The transforms are, respectively, multiply by the configured factor, add the configured offset,
and clamp to the configured closed interval. Clamp preserves `NaN`; it maps
negative and positive infinity to the nearest interval endpoint. The generic
`OperatorContract` does not yet carry a Provider config schema, so
this exact config grammar is part of this Provider's documented ABI boundary.

Each Operator copies the input `semantic_key_hash` and `order_key` into its
output seal descriptor. The Provider ABI has no `item_ordinal` field in a seal
descriptor; the runtime edge owns that identity sidecar and must carry it
through every one-to-one forwarding stage.

The Provider ABI supports one 1 MiB input, one 1 MiB output, zero scratch,
zero retention, no asynchronous work, no private threads, and no direct file
or network I/O. Those are executable capability upper bounds, not
OperatorContract fields. A `PlanBuildRequest` binds node-owned
`NodePlanningRequirements` for each use's finite scheduling, resource, and
topology reservation. The plugin does not own a BufferPool, edge, reorder
queue, or sink. The host must reserve the output slot and edge credit before
calling it.

The typed interface declarations are
[image_scale_float32.json](contracts/image_scale_float32.json),
[image_offset_float32.json](contracts/image_offset_float32.json), and
[image_clamp_float32.json](contracts/image_clamp_float32.json). They contain
only Operator identity and ports; the Provider descriptor does not duplicate
them as operator digests. Production registration must replace the development
bundle convention with a signed bundle manifest and trust policy.

## Development bundle identity

Until a signed content-addressed bundle manifest exists, `bundle_digest` is
reproducible rather than an opaque fixture. It is SHA-256 over NUL-delimited
UTF-8 fields, including the final NUL: the domain separator
`kspacejet.provider-bundle`, Provider ID, then descriptor-order operator IDs.
The current descriptor order is scale, offset, clamp. This command must
produce the literal in `src/provider_api.cpp`:

```sh
printf '%s\0' \
  'kspacejet.provider-bundle' \
  'org.kspacejet.image-ops' \
  'image_scale_float32' \
  'image_offset_float32' \
  'image_clamp_float32' |
  sha256sum
```

Its result is
`8db8232be7b7166a6e6aa2ddd896d1a8ef9aa6c1811ba3530b3796d60f5e6b53`.
Changing the Provider identity or descriptor-order operator IDs changes this
digest.
