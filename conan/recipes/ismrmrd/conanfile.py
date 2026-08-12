import os

from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import copy, get, replace_in_file


class IsmrmrdConan(ConanFile):
    """Conan recipe for the open ISMRMRD MRI raw-data interchange format."""

    name = "ismrmrd"
    version = "1.15.0"
    package_type = "shared-library"
    license = "MIT"
    url = "https://github.com/ismrmrd/ismrmrd"
    description = "ISMRMRD MRI raw-data format and HDF5 dataset library"
    settings = "os", "arch", "compiler", "build_type"

    def requirements(self):
        # Match matio's HDF5 version so the KSpaceJet graph has a single HDF5 ABI.
        self.requires("hdf5/1.14.6", transitive_headers=True)
        self.requires("pugixml/1.16")

    def layout(self):
        cmake_layout(self)

    def source(self):
        get(
            self,
            url="https://github.com/ismrmrd/ismrmrd/archive/refs/tags/v1.15.0.tar.gz",
            sha256="57739de6d93203adc86f5e510729b7e2a45917bcab480dd1c1a6b6d16508a42c",
            strip_root=True,
        )

    def generate(self):
        dependencies = CMakeDeps(self)
        dependencies.generate()

        toolchain = CMakeToolchain(self)
        toolchain.variables["USE_HDF5_DATASET_SUPPORT"] = True
        toolchain.variables["BUILD_STATIC"] = False
        toolchain.variables["BUILD_TESTS"] = False
        toolchain.variables["BUILD_UTILITIES"] = False
        toolchain.variables["BUILD_EXAMPLES"] = False
        toolchain.generate()

    def build(self):
        # Conan Center's current package exports the stable generic target,
        # while upstream selects the historical shared-only alias.
        replace_in_file(
            self,
            os.path.join(self.source_folder, "CMakeLists.txt"),
            "set(ISMRMRD_PUGIXML_LIBRARIES pugixml::shared)",
            "set(ISMRMRD_PUGIXML_LIBRARIES pugixml::pugixml)",
        )
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()
        copy(self, "LICENSE", src=self.source_folder, dst=f"{self.package_folder}/licenses")

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "ISMRMRD")
        self.cpp_info.set_property("cmake_target_name", "ISMRMRD::ISMRMRD")
        library = self.cpp_info.components["ismrmrd"]
        library.set_property("cmake_target_name", "ISMRMRD::ismrmrd")
        library.libs = ["ismrmrd"]
        # Conan's HDF5 recipe exports the C library component as
        # `hdf5::hdf5`, not `hdf5::hdf5_c`.  Keep this dependency on the
        # ISMRMRD component so downstream shared-library consumers receive
        # the HDF5 ABI transitively on Linux and Windows.
        library.requires = ["hdf5::hdf5", "pugixml::pugixml"]
