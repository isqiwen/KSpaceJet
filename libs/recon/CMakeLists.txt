ksj_include_project_bootstrap()

# The reconstruction layer owns the versioned pipeline contracts, graph compiler and
# bounded runtime.  Applications consume these shared libraries; they do not embed a
# second implementation of PipelineDefinition or scan admission.
add_subdirectory(kspacejet-provider-sdk)
add_subdirectory(kspacejet-provider-loader)
add_subdirectory(kspacejet-recon-contracts)
add_subdirectory(kspacejet-recon-graph)
add_subdirectory(kspacejet-recon-runtime)
