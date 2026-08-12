ksj_include_project_bootstrap()

# Providers are separately loadable algorithm extensions, not internal
# libraries linked into ksj-recon.  Keep their build and install topology
# explicit so a product build can ship a trusted reference plugin without
# creating a second reconstruction data plane.
add_subdirectory(kspacejet-reference-cartesian)
