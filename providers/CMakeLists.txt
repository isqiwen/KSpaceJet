ksj_include_project_bootstrap()

# Providers are separately loadable algorithm extensions, not internal libraries linked into ksj-recon. Keep their build
# and install topology explicit so a product build can ship trusted algorithm modules without creating a second
# reconstruction data plane.
add_subdirectory(kspacejet-kspace-conditioning)
add_subdirectory(kspacejet-cartesian-recon)
add_subdirectory(kspacejet-coil-combine)
add_subdirectory(kspacejet-noncartesian-recon)
add_subdirectory(kspacejet-image-ops)
add_subdirectory(kspacejet-calibration)
