import re

from conan import ConanFile
from conan.tools.build import check_min_cppstd
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import copy, load


class ViamCsi(ConanFile):
    name = "viam-csi"
    license = "Apache-2.0"
    url = "https://github.com/viam-modules/csi-camera"
    package_type = "application"

    settings = "os", "compiler", "build_type", "arch"
    # `target` only selects which meta.json lands in module.tar.gz; the binary
    # detects jetson vs pi at runtime.
    options = {"with_tests": [True, False], "target": ["jetson", "pi"]}
    default_options = {
        "with_tests": False,
        "target": "jetson",
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
        "meta.json",
        "meta-pi.json",
        "first_run.sh",
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
        self.requires("viam-cpp-sdk/0.41.1")
        # Host gstreamer; recipe lives in etc/conan/gstreamer (see bin/build.sh).
        self.requires("gstreamer/system")

    def build_requirements(self):
        if self.options.with_tests:
            self.test_requires("gtest/1.16.0")

    def layout(self):
        cmake_layout(self, src_folder=".")

    def generate(self):
        tc = CMakeToolchain(self)
        tc.variables["VIAM_CSI_ENABLE_TESTS"] = self.options.with_tests
        tc.variables["VIAM_CSI_TARGET"] = str(self.options.target)
        tc.generate()
        CMakeDeps(self).generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

        # CPack assembles module.tar.gz from the CMake install rules
        cmake.build(target="package")
        copy(self, "module.tar.gz", src=self.build_folder, dst=self.package_folder)

    def deploy(self):
        copy(self, "module.tar.gz", src=self.package_folder, dst=self.deploy_folder)
