# kspacejet-nufft

`kspacejet-nufft` owns generic NUFFT / NUDFT numerical kernels used by non-cartesian reconstruction code.

Current targets:

- `KSpaceJet::nufft`: shared library target for NUFFT support.

Public headers live under `include/kspacejet/nufft`. Implementation details live under `include/kspacejet/nufft/detail` and
`src`. Public algorithms take `ksj::array` views and provide matching pooled-container overloads that forward to the
view implementation. Third-party candidates such as BART are optional private backends selected through the public
`ksj::nufft::Backend` option; their headers and types are not part of the public interface.

`Backend::automatic` uses the exact Eigen direct NUDFT implementation. On the supported local CPU profile, the
direct-DFT benchmark found it faster than BART for every measured cold-plan and caller-workspace-reuse workload.
`Backend::bart` remains an explicit opt-in for BART's single-precision implementation. Set
`Nufft2Options::direct_dft` for BART's exact direct-DFT operator; leave it false to request BART's approximate
gridding NUFFT operator.

This module must not expose Armadillo, matio, BART, MKL, IPP, OpenCV, or ITK types in public API. Large inputs,
outputs, and scratch buffers should use `ksj::array` pooled objects; small scalar configuration objects may stay on
the stack. Implementations that do not depend on a third-party compute backend use the `detail/eigen/eigen_*` file naming
pattern used by the rest of `libs/numerics`.

MRI-specific orchestration stays outside this module: trajectory creation, scan/channel state, streaming backpressure,
debug dump policy, and workflow belong in an independently developed provider.
