# Testing convention

Unit tests use CTest and are enabled with `KSJ_BUILD_UNIT_TESTS=ON` through the
unit-test presets. New provider tests should create standard ISMRMRD HDF5
fixtures and validate acquisition metadata, sample layout, trajectory layout and
streaming backpressure. Do not add fixtures for obsolete private data formats.

For numerical hot paths, benchmark throughput, allocation behavior and thread
oversubscription before choosing a non-Eigen backend policy.
