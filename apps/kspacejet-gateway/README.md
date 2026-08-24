# `ksj-gateway`

`ksj-gateway` is an installed KSpaceJet application scaffold. Its help and JSON help report
`status: "scaffold"`; `--version` is available, while a requested `--config` operation returns
an `unimplemented` error.

It does not implement external-system integration, authentication, connection supervision,
routing, image delivery, a data-plane service, session forwarding, Connector management, or
scanner integration. It must not be treated as evidence of a gateway or transport capability.

The future candidate-stable architecture is [KSpaceJet gateway architecture](../../docs/architecture/KSpaceJet_gateway_architecture.md).
It defines this executable as the sole external-integration composition root after P5-009 onward
is accepted; until then the current scaffold behavior is intentional and accurate.
