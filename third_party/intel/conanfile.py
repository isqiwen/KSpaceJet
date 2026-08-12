import hashlib
import json
import os
import re

from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.files import copy


class IntelOneapiConan(ConanFile):
    """Packages the reviewed oneAPI payload checked into this repository.

    The recipe never downloads Intel software.  Maintainers stage a reviewed,
    redistributable-only payload in payload/<platform>-<arch>/ through Git LFS;
    end users receive it with the source checkout and need no oneAPI install.
    """

    name = "intel-oneapi"
    version = "2026.1"
    package_type = "shared-library"
    license = "Intel Simplified Software License"
    url = "https://www.intel.com/content/www/us/en/developer/tools/oneapi/overview.html"
    description = "Reviewed Intel oneAPI IPP, oneMKL, and OpenMP runtime payload for KSpaceJet"
    # This is a reviewed prebuilt runtime payload. Its package identity is the
    # platform/architecture, not the caller's C++ ABI or optimization mode.
    settings = "os", "arch"
    exports_sources = "manifest.json", "licenses/*", "payload/*", "cmake/*"
    no_copy_source = True

    _versions = {
        "ipp": "2026.0",
        "mkl": "2026.1",
        "compiler": "2026.1",
    }

    def validate(self):
        if str(self.settings.arch) != "x86_64":
            raise ConanInvalidConfiguration("KSpaceJet's bundled Intel package supports x86_64 only.")
        if str(self.settings.os) not in {"Linux", "Windows"}:
            raise ConanInvalidConfiguration("KSpaceJet's bundled Intel package supports Linux and Windows only.")

        if self.source_folder:
            payload_root = self._payload_root()
            if os.path.isdir(payload_root):
                self._reject_static_libraries(payload_root)

    def _payload_root(self):
        platform = "windows" if str(self.settings.os) == "Windows" else "linux"
        return os.path.join(self.source_folder, "payload", f"{platform}-x86_64", "oneapi")

    def _platform_payload_dir(self):
        return os.path.dirname(self._payload_root())

    def _platform_license_dir(self):
        platform = "windows" if str(self.settings.os) == "Windows" else "linux"
        return os.path.join(self.source_folder, "licenses", platform)

    def _reject_static_libraries(self, payload_root):
        static_libraries = []
        for root, _, filenames in os.walk(payload_root):
            for filename in filenames:
                library_path = os.path.join(root, filename)
                if filename.endswith(".a"):
                    static_libraries.append(library_path)
                    continue
                if str(self.settings.os) != "Windows" or not filename.endswith(".lib"):
                    continue

                # Windows DLL import libraries also use the .lib extension. They
                # are retained only when their archive records identify a DLL.
                with open(library_path, "rb") as library_file:
                    is_dll_import_library = b".dll" in library_file.read().lower()
                if not is_dll_import_library:
                    static_libraries.append(library_path)

        if static_libraries:
            relative_paths = [os.path.relpath(path, self.source_folder) for path in static_libraries]
            raise ConanInvalidConfiguration(
                "KSpaceJet ships Intel shared libraries only. Remove static archives from the payload: "
                + ", ".join(relative_paths[:3])
            )

    def _verify_payload_manifest(self, payload_root):
        manifest_path = os.path.join(self._platform_payload_dir(), "manifest-files.json")
        if not os.path.isfile(manifest_path):
            raise ConanInvalidConfiguration(
                "The bundled Intel payload needs payload/<platform>-x86_64/manifest-files.json with a SHA-256 "
                "entry for every staged file."
            )

        try:
            with open(manifest_path, encoding="utf-8") as manifest_file:
                manifest = json.load(manifest_file)
            schema_version = manifest["schema_version"]
            files = manifest["files"]
        except (OSError, json.JSONDecodeError, KeyError, TypeError) as error:
            raise ConanInvalidConfiguration(f"Invalid Intel payload manifest {manifest_path}: {error}") from error

        if schema_version != 1:
            raise ConanInvalidConfiguration(f"Unsupported Intel payload manifest schema: {schema_version}")
        if not isinstance(files, list) or not files:
            raise ConanInvalidConfiguration(f"Intel payload manifest {manifest_path} contains no files.")

        payload_root_real = os.path.realpath(payload_root)
        expected_paths = set()
        for entry in files:
            if (
                not isinstance(entry, dict)
                or not isinstance(entry.get("path"), str)
                or not isinstance(entry.get("sha256"), str)
            ):
                raise ConanInvalidConfiguration(
                    f"Intel payload manifest {manifest_path} entries require string path and sha256 fields."
                )
            relative_path = entry["path"]
            file_path = os.path.normpath(os.path.join(self._platform_payload_dir(), relative_path))
            if (
                not file_path.startswith(os.path.normpath(payload_root) + os.sep)
                or not os.path.isfile(file_path)
                or not os.path.realpath(file_path).startswith(payload_root_real + os.sep)
            ):
                raise ConanInvalidConfiguration(f"Intel payload manifest contains missing or unsafe path: {relative_path}")
            if not re.fullmatch(r"[0-9a-fA-F]{64}", entry["sha256"]):
                raise ConanInvalidConfiguration(f"Intel payload manifest contains invalid SHA-256: {relative_path}")
            normalized_file_path = os.path.normpath(file_path)
            if normalized_file_path in expected_paths:
                raise ConanInvalidConfiguration(f"Intel payload manifest lists a file more than once: {relative_path}")
            with open(file_path, "rb") as payload_file:
                digest = hashlib.sha256()
                for chunk in iter(lambda: payload_file.read(1024 * 1024), b""):
                    digest.update(chunk)
                actual_sha256 = digest.hexdigest()
            if actual_sha256.lower() != entry["sha256"].lower():
                raise ConanInvalidConfiguration(f"Intel payload hash mismatch: {relative_path}")
            expected_paths.add(normalized_file_path)

        staged_paths = {
            os.path.normpath(os.path.join(root, filename))
            for root, _, filenames in os.walk(payload_root)
            for filename in filenames
        }
        if expected_paths != staged_paths:
            unlisted = sorted(staged_paths - expected_paths)
            missing = sorted(expected_paths - staged_paths)
            raise ConanInvalidConfiguration(
                "Intel payload manifest must list every staged file. "
                f"Unlisted: {unlisted[:3]}; missing: {missing[:3]}"
            )

    def package(self):
        payload_root = self._payload_root()
        required_components = [
            os.path.join(payload_root, "ipp", self._versions["ipp"]),
            os.path.join(payload_root, "mkl", self._versions["mkl"]),
            os.path.join(payload_root, "compiler", self._versions["compiler"]),
        ]
        missing = [component for component in required_components if not os.path.isdir(component)]
        if missing:
            raise ConanInvalidConfiguration(
                "The bundled Intel payload is absent or incomplete. Run `git lfs pull` after cloning a release "
                "checkout, then verify third_party/intel/manifest.json. Missing: " + ", ".join(missing)
            )

        self._reject_static_libraries(payload_root)

        license_dir = self._platform_license_dir()
        if not os.path.isdir(license_dir) or not any(
            os.path.isfile(os.path.join(root, filename))
            for root, _, filenames in os.walk(license_dir)
            for filename in filenames
        ):
            raise ConanInvalidConfiguration(
                "The bundled Intel payload is missing component license and third-party notice copies in "
                + license_dir
            )

        self._verify_payload_manifest(payload_root)

        copy(self, "*", src=payload_root, dst=os.path.join(self.package_folder, "oneapi"))
        copy(
            self,
            "manifest-files.json",
            src=self._platform_payload_dir(),
            dst=os.path.join(self.package_folder, "licenses", "manifests"),
        )
        copy(self, "manifest.json", src=self.source_folder, dst=os.path.join(self.package_folder, "licenses"))
        copy(self, "*", src=os.path.join(self.source_folder, "licenses"), dst=os.path.join(self.package_folder, "licenses"))
        copy(
            self,
            "kspacejet-intel-runtime-dirs.cmake",
            src=os.path.join(self.source_folder, "cmake"),
            dst=os.path.join(self.package_folder, "lib", "cmake", "intel-oneapi"),
        )

    def package_info(self):
        ipp_root = os.path.join("oneapi", "ipp", self._versions["ipp"])
        mkl_root = os.path.join("oneapi", "mkl", self._versions["mkl"])
        compiler_root = os.path.join("oneapi", "compiler", self._versions["compiler"])

        self.cpp_info.set_property("cmake_file_name", "Intel")
        self.cpp_info.set_property(
            "cmake_build_modules", ["lib/cmake/intel-oneapi/kspacejet-intel-runtime-dirs.cmake"]
        )
        # The payload has no conventional package-level include/, lib/, or
        # bin/ directories. Every exported location is component-specific.
        self.cpp_info.includedirs = []
        self.cpp_info.libdirs = []
        self.cpp_info.bindirs = []

        headers = self.cpp_info.components["headers"]
        headers.set_property("cmake_target_name", "Intel::headers")
        headers.includedirs = [os.path.join(ipp_root, "include"), os.path.join(mkl_root, "include")]

        ipp_components = ("ippcore", "ippi", "ipps", "ippcv", "ippdc", "ippvm", "ippcore_tl_omp")
        for component_name in ipp_components:
            component = self.cpp_info.components[component_name]
            component.set_property("cmake_target_name", f"Intel::{component_name}")
            component.libs = [component_name]
            component.includedirs = []
            # The reviewed IPP payload places both Linux .so files and
            # Windows import libraries directly in <ipp>/lib.
            component.libdirs = [os.path.join(ipp_root, "lib")]
            component.requires = ["headers"]

        mkl = self.cpp_info.components["mkl_rt"]
        mkl.set_property("cmake_target_name", "Intel::mkl_rt")
        mkl.libs = ["mkl_rt"]
        mkl.includedirs = []
        # oneMKL ships its Linux and Windows dispatcher libraries directly
        # under <mkl>/lib, like the reviewed IPP payload.
        mkl.libdirs = [os.path.join(mkl_root, "lib")]
        mkl.requires = ["headers", "iomp5"]

        openmp = self.cpp_info.components["iomp5"]
        openmp.set_property("cmake_target_name", "Intel::iomp5")
        openmp.libs = ["libiomp5md" if str(self.settings.os) == "Windows" else "iomp5"]
        openmp.includedirs = []
        openmp.libdirs = [
            os.path.join(compiler_root, "lib", "intel64") if str(self.settings.os) == "Windows"
            else os.path.join(compiler_root, "lib")
        ]
        openmp.requires = ["headers"]
