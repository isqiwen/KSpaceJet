import re
from pathlib import Path

from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.cmake import CMakeDeps, CMakeToolchain


class KSpaceJetConan(ConanFile):
    """Dependency graph for the standalone, open-source KSpaceJet framework.

    This recipe deliberately describes dependencies only.  KSpaceJet is an
    application framework, not a Conan package consumed by downstream CMake
    projects, so the project targets continue to be built by the root
    CMakeLists.txt.
    """

    name = "kspacejet"
    version = "7.1.0"
    package_type = "application"

    settings = "os", "arch", "compiler", "build_type"
    options = {
        "with_performance_analysis": [True, False],
    }
    default_options = {
        "with_performance_analysis": False,
        # The framework is distributed as shared libraries.  Apply the same
        # policy to every Conan dependency that exposes a ``shared`` option;
        # header-only dependencies simply ignore it.
        "*:shared": True,
        # KSpaceJet uses Boost.Math headers only.  Boost 1.91's Cobalt I/O SSL
        # component is not required by the framework and can be omitted from
        # the shared graph; otherwise the current CCI recipe may expect the
        # optional boost_cobalt_io_ssl library even when b2 does not emit it.
        "boost/*:without_cobalt": True,
        # KSpaceJet is a headless streaming service.  Keep OpenCV's GUI
        # frontend and Wayland stack out of the portable runtime graph.
        "opencv/*:highgui": False,
        # ksj_image exposes the OpenCV BM3D backend, which is implemented in
        # the contrib xphoto module.  Keep that dependency explicit rather
        # than relying on a system OpenCV build with an accidental module set.
        "opencv/*:xphoto": True,
        "opencv/*:with_wayland": False,
        "opencv/*:with_ffmpeg": False,
        "opencv/*:with_openexr": False,
        "opencv/*:with_quirc": False,
    }

    def requirements(self):
        is_linux = str(self.settings.os) == "Linux"

        # Core and numerical framework.
        self.requires("boost/1.91.0")
        # The ITK and OpenCV recipes currently converge on Eigen 3.4.0. Keep
        # the portable baseline on that valid graph; moving to Eigen 5
        # requires an ITK/OpenCV upgrade or separated image backends.
        self.requires("eigen/3.4.0")
        self.requires("intel-oneapi/2026.1@kspacejet/stable")

        # I/O and image backends.
        self.requires("cli11/2.6.2")
        # Conan Center no longer resolves ITK 5.3's json-c/0.17 transitive
        # recipe with Conan 2. Use the maintained 5.4 line and validate image
        # behaviour against the existing reconstruction benchmarks.
        self.requires("itk/5.4.6")
        # ISMRMRD is the only open raw-acquisition input contract.  Its local
        # recipe keeps HDF5 aligned with matio and is exported from this tree.
        self.requires("ismrmrd/1.15.0@kspacejet/stable")
        self.requires("matio/1.5.27")
        self.requires("nlohmann_json/3.12.0")
        self.requires("opencv/4.14.0")
        self.requires("spdlog/1.17.0")

        if is_linux:
            self.requires("libnuma/2.0.19")

        if self.options.with_performance_analysis:
            if not is_linux:
                raise ConanInvalidConfiguration(
                    "KSpaceJet performance analysis requires Linux gperftools support; use with_performance_analysis=False on Windows."
                )
            self.requires("gperftools/2.17.2")

    def build_requirements(self):
        """Add test-only tools to the test dependency graph.

        ``test_requires()`` is a Conan 2 declaration API, not a recipe hook.
        It must therefore be called from ``build_requirements()``.  Product
        profiles set the standard ``tools.build:skip_test`` configuration to
        avoid resolving GTest, while the dedicated unit-test profile sets it
        to ``False``.
        """
        if not self.conf.get("tools.build:skip_test", default=True, check_type=bool):
            self.test_requires("gtest/1.17.0")

    def _cmake_preset_prefix(self):
        """Return a CMake-preset namespace unique to this Conan output tree.

        Conan's CMakeToolchain writes a ``CMakePresets.json`` to every
        generators folder.  A developer can legitimately retain more than one
        such folder and CMake then reads all of their entries through a
        previously generated ``CMakeUserPresets.json``.  The default
        ``conan-release`` name collides in that situation.  Use the complete
        output-folder path (relative to the source tree when possible) as a
        stable, portable namespace instead.
        """
        generators_folder = Path(self.generators_folder).resolve()
        source_folder = Path(self.source_folder or self.recipe_folder).resolve()
        try:
            namespace = generators_folder.relative_to(source_folder)
        except ValueError:
            # A custom output folder may live outside the source tree.  Keep
            # its absolute path in the namespace rather than falling back to
            # a potentially ambiguous basename.
            namespace = generators_folder

        namespace_text = "-".join(namespace.parts)
        sanitized = re.sub(r"[^A-Za-z0-9_.-]+", "-", namespace_text).strip("-.")
        return f"conan-{sanitized or 'build'}"

    def generate(self):
        toolchain = CMakeToolchain(self)
        # KSpaceJet owns the top-level CMakePresets.json and directly points
        # each project preset at its matching generated toolchain.  Do not
        # create or mutate a repository-root CMakeUserPresets.json: that file
        # is user-owned and adding every Conan output folder there causes
        # duplicate preset names.  The unique prefix also makes already
        # generated, Conan-owned include files coexist during migration.
        toolchain.user_presets_path = False
        toolchain.presets_prefix = self._cmake_preset_prefix()
        toolchain.variables["KSJ_DEPENDENCY_PROVIDER"] = "conan"
        toolchain.variables["KSJ_ENABLE_BUNDLED_INTEL"] = True
        toolchain.generate()

        dependencies = CMakeDeps(self)
        dependencies.generate()
