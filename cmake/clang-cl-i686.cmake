# Native LLVM cross-build using an xwin CRT/Windows SDK installation.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86)
set(CMAKE_C_COMPILER clang-cl)
set(CMAKE_CXX_COMPILER clang-cl)
set(CMAKE_C_COMPILER_TARGET i686-pc-windows-msvc)
set(CMAKE_CXX_COMPILER_TARGET i686-pc-windows-msvc)
find_program(CMAKE_AR NAMES llvm-lib REQUIRED)
find_program(CMAKE_LINKER NAMES lld-link REQUIRED)
find_program(CMAKE_RC_COMPILER NAMES llvm-rc REQUIRED)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(WINSDK_ROOT "$ENV{WINSDK_ROOT}" CACHE PATH "xwin CRT/SDK root")
if(NOT IS_DIRECTORY "${WINSDK_ROOT}/crt/include")
    message(FATAL_ERROR "Set WINSDK_ROOT to an xwin installation containing crt/ and sdk/")
endif()
set(CMAKE_C_FLAGS_INIT "/FIwindows.h /imsvc${WINSDK_ROOT}/crt/include /imsvc${WINSDK_ROOT}/sdk/include/ucrt /imsvc${WINSDK_ROOT}/sdk/include/shared /imsvc${WINSDK_ROOT}/sdk/include/um")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_C_FLAGS_INIT}")
set(sdk_link_options "/libpath:${WINSDK_ROOT}/crt/lib/x86 /libpath:${WINSDK_ROOT}/sdk/lib/ucrt/x86 /libpath:${WINSDK_ROOT}/sdk/lib/um/x86")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${sdk_link_options}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${sdk_link_options}")
set(WIZ8_IMPORT_COMPILE_OPTIONS --target=i686-pc-windows-msvc)
set(WIZ8_RESOURCE_INCLUDE_OPTIONS
    /C1252
    "/I${WINSDK_ROOT}/sdk/include/um"
    "/I${WINSDK_ROOT}/sdk/include/shared"
    "/I${WINSDK_ROOT}/sdk/include/ucrt")
set(CMAKE_C_STANDARD 90)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows source includes use case-insensitive paths. Let Clang resolve them
# the same way on Linux without rewriting the historical source files.
get_filename_component(wiz8_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
file(GLOB_RECURSE wiz8_headers
    "${wiz8_root}/include/*.h" "${wiz8_root}/include/*.H" "${wiz8_root}/include/*.hpp" "${wiz8_root}/include/*.inc"
    "${wiz8_root}/src/*.h" "${wiz8_root}/src/*.H" "${wiz8_root}/src/*.hpp")
set(header_overlay "{\"version\":0,\"case-sensitive\":false,\"roots\":[")
set(separator "")
foreach(header IN LISTS wiz8_headers)
    string(APPEND header_overlay "${separator}{\"type\":\"file\",\"name\":\"${header}\",\"external-contents\":\"${header}\"}")
    set(separator ",")
endforeach()
string(APPEND header_overlay "]}")
file(WRITE "${CMAKE_BINARY_DIR}/windows-includes.json" "${header_overlay}")
string(APPEND CMAKE_C_FLAGS_INIT " -Xclang -ivfsoverlay -Xclang ${CMAKE_BINARY_DIR}/windows-includes.json")
string(APPEND CMAKE_CXX_FLAGS_INIT " -Xclang -ivfsoverlay -Xclang ${CMAKE_BINARY_DIR}/windows-includes.json")
