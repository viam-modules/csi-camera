from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.gnu import PkgConfig
from conan.tools.system import package_manager

required_conan_version = ">=2.0"


class GStreamerSystemConan(ConanFile):
    """gstreamer as a host library, in the style of conancenter's libudev/system.

    The camera source plugins (nvarguscamerasrc on Jetson, libcamerasrc on Pi)
    only exist on the host and dlopen the host's libgstreamer, so the module
    must link that same copy rather than a conan-built one.
    """

    name = "gstreamer"
    version = "system"
    description = "GStreamer core, base and app libraries from the host"
    homepage = "https://gstreamer.freedesktop.org/"
    license = "LGPL-2.1-or-later"
    package_type = "shared-library"
    settings = "os", "arch", "compiler", "build_type"

    _pkgs = ["gstreamer-1.0", "gstreamer-base-1.0", "gstreamer-app-1.0"]

    def layout(self):
        pass

    def validate(self):
        if self.settings.os != "Linux":
            raise ConanInvalidConfiguration("gstreamer/system is only supported on Linux.")

    def package_id(self):
        self.info.clear()

    def system_requirements(self):
        apt = package_manager.Apt(self)
        apt.install(["libgstreamer1.0-dev", "libgstreamer-plugins-base1.0-dev"], update=True, check=True)

    def package_info(self):
        self.cpp_info.includedirs = []
        self.cpp_info.libdirs = []
        for pkg in self._pkgs:
            comp = self.cpp_info.components[pkg]
            comp.includedirs = []
            comp.libdirs = []
            pc = PkgConfig(self, pkg)
            pc.fill_cpp_info(comp)
            comp.set_property("pkg_config_name", pkg)
            comp.set_property("system_package_version", str(pc.version))
