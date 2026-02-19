import re

from conan import ConanFile
from conan.tools.build import check_min_cppstd
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import load


class ViamCsi(ConanFile):
    name = "viam-csi"
    license = "Apache-2.0"
    url = "https://github.com/viam-modules/csi-camera"
    package_type = "application"

    settings = "os", "compiler", "build_type", "arch"
    options = {"with_tests": [True, False]}
    default_options = {
        "with_tests": False,
        "viam-cpp-sdk/*:shared": False,
    }

    exports_sources = (
        "CMakeLists.txt",
        "LICENSE",
        "main.cpp",
        "csi_camera.cpp",
        "csi_camera.h",
        "utils.cpp",
        "utils.h",
        "constraints.h",
        "tests/*",
    )

    version = "0.0.2"

    def set_version(self):
        content = load(self, "CMakeLists.txt")
        match = re.search(r"project\([^\)]*VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)", content, re.MULTILINE | re.DOTALL)
        if match:
            self.version = match.group(1).strip()

    def validate(self):
        check_min_cppstd(self, 17)

    def requirements(self):
        # Phase 1 scope: migrate viam-cpp-sdk sourcing to Conan.
        self.requires("viam-cpp-sdk/0.20.1")

    def layout(self):
        cmake_layout(self, src_folder=".")

    def generate(self):
        tc = CMakeToolchain(self)
        tc.variables["VIAM_CSI_ENABLE_TESTS"] = self.options.with_tests
        tc.generate()
        CMakeDeps(self).generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()
