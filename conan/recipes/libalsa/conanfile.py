"""Use the host ALSA library together with its device configuration and plugins."""

from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.gnu import PkgConfig
from conan.tools.system import package_manager


class SystemAlsa(ConanFile):
    name = "libalsa"
    version = "system"
    package_type = "shared-library"
    settings = "os", "arch"
    options = {"shared": [True]}
    default_options = {"shared": True}

    def validate(self):
        if self.settings.os != "Linux":
            raise ConanInvalidConfiguration("System ALSA is only available on Linux")

    def system_requirements(self):
        package_manager.Apt(self).install(["libasound2-dev"], update=True, check=True)
        package_manager.Dnf(self).install(["alsa-lib-devel"], update=True, check=True)
        package_manager.PacMan(self).install(["alsa-lib"], update=True, check=True)
        package_manager.Zypper(self).install(["alsa-devel"], update=True, check=True)
        package_manager.Apk(self).install(["alsa-lib-dev"], update=True, check=True)

    def package_info(self):
        self.cpp_info.includedirs = []
        self.cpp_info.libdirs = []
        alsa = PkgConfig(self, "alsa")
        alsa.fill_cpp_info(self.cpp_info)
        self.cpp_info.set_property("system_package_version", str(alsa.version))
        self.cpp_info.set_property("cmake_file_name", "ALSA")
        self.cpp_info.set_property("cmake_target_name", "ALSA::ALSA")
