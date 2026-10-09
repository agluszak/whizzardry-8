# Pinned dependency manager for native builds. A developer-provided VCPKG_ROOT
# is supported, otherwise the bootstrap lives entirely under the build tree.
set(WIZ8_VCPKG_COMMIT "9e593bb18ea69cc5095e012465dcd675a822ed0d")
set(WIZ8_VCPKG_RELEASE "2026.07.29")

if(DEFINED ENV{VCPKG_ROOT} AND EXISTS "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    file(TO_CMAKE_PATH "$ENV{VCPKG_ROOT}" _wiz8_vcpkg_root)
else()
    set(_wiz8_vcpkg_root "${CMAKE_BINARY_DIR}/_vcpkg")
    find_package(Git REQUIRED)

    if(NOT EXISTS "${_wiz8_vcpkg_root}/.git")
        message(STATUS "Fetching vcpkg ${WIZ8_VCPKG_RELEASE}")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" clone --depth 1 --branch "${WIZ8_VCPKG_RELEASE}"
                    https://github.com/microsoft/vcpkg.git "${_wiz8_vcpkg_root}"
            RESULT_VARIABLE _wiz8_vcpkg_result
        )
        if(NOT _wiz8_vcpkg_result EQUAL 0)
            message(FATAL_ERROR "Could not download pinned vcpkg release")
        endif()
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${_wiz8_vcpkg_root}" rev-parse HEAD
        OUTPUT_VARIABLE _wiz8_vcpkg_head
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _wiz8_vcpkg_result
    )
    if(NOT _wiz8_vcpkg_result EQUAL 0 OR NOT _wiz8_vcpkg_head STREQUAL WIZ8_VCPKG_COMMIT)
        message(FATAL_ERROR "Unexpected vcpkg checkout at ${_wiz8_vcpkg_root}; delete it to restore the pinned release")
    endif()
endif()

if(CMAKE_HOST_WIN32)
    set(_wiz8_vcpkg_executable "${_wiz8_vcpkg_root}/vcpkg.exe")
    set(_wiz8_bootstrap_command cmd /c "${_wiz8_vcpkg_root}/bootstrap-vcpkg.bat")
else()
    set(_wiz8_vcpkg_executable "${_wiz8_vcpkg_root}/vcpkg")
    set(_wiz8_bootstrap_command sh "${_wiz8_vcpkg_root}/bootstrap-vcpkg.sh")
endif()

if(NOT EXISTS "${_wiz8_vcpkg_executable}")
    message(STATUS "Bootstrapping vcpkg")
    execute_process(
        COMMAND ${_wiz8_bootstrap_command} -disableMetrics
        WORKING_DIRECTORY "${_wiz8_vcpkg_root}"
        RESULT_VARIABLE _wiz8_bootstrap_result
    )
    if(NOT _wiz8_bootstrap_result EQUAL 0)
        message(FATAL_ERROR "Failed to bootstrap vcpkg")
    endif()
endif()

set(CMAKE_TOOLCHAIN_FILE "${_wiz8_vcpkg_root}/scripts/buildsystems/vcpkg.cmake"
    CACHE FILEPATH "vcpkg dependency toolchain")
