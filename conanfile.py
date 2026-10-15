"""Conan dependency graph for the native Whizzardry build.

SDL_mixer 3.2.4 and utf8proc 2.10.0 are not yet in ConanCenter.
Those two (and utfcpp/Catch2) remain explicitly pinned by CMake FetchContent.
"""
from conan import ConanFile
from conan.tools.cmake import CMakeDeps, CMakeToolchain
from conan.tools.env import VirtualBuildEnv, VirtualRunEnv


class WhizzardryDependencies(ConanFile):
    settings = "os", "arch", "compiler", "build_type"

    def requirements(self):
        self.requires("sdl/3.4.14")
        self.requires("sdl_image/3.4.4")
        self.requires("ffmpeg/7.1.3")
        self.requires("zlib/1.3.1")
        self.requires("libjpeg/9f")
        self.requires("glm/1.0.3")
        self.requires("fmt/11.2.0")

    def build_requirements(self):
        self.tool_requires("glslang/1.4.313.0")

    def configure(self):
        # SDL keeps ALSA support on Linux, without pulling in PulseAudio and
        # the whole unrelated sound-server dependency graph.
        self.options["sdl"].pulse = False
        # Keep JPEG and PNG; avoid heavyweight optional image codecs.
        self.options["sdl_image"].with_avif = False
        self.options["sdl_image"].with_libtiff = False
        self.options["sdl_image"].with_libwebp = False
        # Do not bring in codec libraries which the game never calls. The
        # built-in FFmpeg video/audio decoders and demuxers remain enabled.
        self.options["ffmpeg"].with_programs = False
        self.options["ffmpeg"].with_sdl = False
        self.options["ffmpeg"].with_asm = False
        self.options["ffmpeg"].with_libalsa = False
        self.options["ffmpeg"].with_pulse = False
        self.options["ffmpeg"].with_vaapi = False
        self.options["ffmpeg"].with_vdpau = False
        self.options["ffmpeg"].with_xcb = False
        self.options["ffmpeg"].with_xlib = False
        self.options["ffmpeg"].with_freetype = False
        self.options["ffmpeg"].with_fontconfig = False
        self.options["ffmpeg"].with_libaom = False
        self.options["ffmpeg"].with_libdav1d = False
        self.options["ffmpeg"].with_libmp3lame = False
        self.options["ffmpeg"].with_libvpx = False
        self.options["ffmpeg"].with_libx264 = False
        self.options["ffmpeg"].with_libx265 = False
        self.options["ffmpeg"].with_libsvtav1 = False
        self.options["ffmpeg"].with_opus = False
        self.options["ffmpeg"].with_vorbis = False
        self.options["ffmpeg"].with_lzma = False
        self.options["ffmpeg"].with_bzip2 = False

    def generate(self):
        toolchain = CMakeToolchain(self)
        toolchain.user_presets_path = False
        toolchain.generate()

        deps = CMakeDeps(self)
        # Normalize ConanCenter CMake targets to the existing project API.
        deps.set_property("sdl", "cmake_file_name", "SDL3")
        deps.set_property("sdl", "cmake_target_name", "SDL3::SDL3")
        deps.set_property("sdl", "cmake_target_aliases", ["SDL3::Headers"])
        deps.set_property("sdl_image", "cmake_file_name", "SDL3_image")
        deps.set_property("sdl_image", "cmake_target_name", "SDL3_image::SDL3_image")
        deps.set_property("ffmpeg", "cmake_file_name", "FFMPEG")
        deps.set_property("ffmpeg", "cmake_target_name", "PkgConfig::FFMPEG")
        deps.set_property("libjpeg", "cmake_file_name", "JPEG")
        deps.set_property("libjpeg", "cmake_target_name", "JPEG::JPEG")
        deps.set_property("zlib", "cmake_file_name", "ZLIB")
        deps.set_property("zlib", "cmake_target_name", "ZLIB::ZLIB")
        deps.set_property("glm", "cmake_file_name", "glm")
        deps.set_property("glm", "cmake_target_name", "glm::glm-header-only")
        deps.generate()

        VirtualBuildEnv(self).generate()
        VirtualRunEnv(self).generate()
