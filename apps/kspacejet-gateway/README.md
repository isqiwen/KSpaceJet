# `ksj-gateway`

`ksj-gateway` is the integration boundary between KSpaceJet and real deployment
systems. It authenticates and supervises external connections, applies site routing,
coordinates with reconstruction-service admission, and routes standard images to the configured
external connector.

The gateway does not own reconstruction algorithms, Provider buffers, resource
admission, or the reconstruction scheduler. Its data-plane contract is the frozen
public MRD/ISMRMRD streaming-session binding; it must not introduce a private
KSpaceJet raw-data envelope. Proprietary scanner or hospital-system adaptation belongs
in separately deployed site connectors upstream or downstream of this process.

When a scanner can connect directly with the public MRD binding, deployment may bypass
the gateway for the lowest possible latency. Gateway-mode copy and hop costs are measured as
end-to-end deployment behavior rather than attributed to the core runtime.
