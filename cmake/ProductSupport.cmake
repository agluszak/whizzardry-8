# Helpers shared by more than one product component. Component-local helpers
# live in the component's own CMakeLists.txt.

function(wiz8_add_import_library NAME DEF_FILE)
    # Consumer ABI describes the retail DLL, never the partial SURRENDER target.
    cmake_parse_arguments(ARG "PRESERVE_C_DECORATION" "" "IMPORT_OBJECTS" ${ARGN})
    string(TOLOWER "${NAME}" target_stem)
    set(import_library "${CMAKE_BINARY_DIR}/${target_stem}.lib")
    get_filename_component(import_def "${DEF_FILE}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    set(import_objects)
    if(ARG_PRESERVE_C_DECORATION AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        # VC6 LIB treats C names in a DEF as undecorated and prepends '_'.
        # Object export directives already carry compiler decoration, so LIB
        # preserves both the caller symbol and the DLL's exact import name.
        # This object contains directives only, never function bodies.
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${import_def}")
        file(STRINGS "${import_def}" import_names REGEX "^[ \t]*_[A-Za-z0-9_]+@[0-9]+[ \t]*$")
        file(STRINGS "${import_def}" library_name REGEX "^LIBRARY ")
        set(import_source "${CMAKE_BINARY_DIR}/${target_stem}-exports.cpp")
        set(import_object "${CMAKE_BINARY_DIR}/${target_stem}-exports.obj")
        set(import_def "${CMAKE_BINARY_DIR}/${target_stem}-library.def")
        set(import_directives "")
        foreach(import_name IN LISTS import_names)
            string(STRIP "${import_name}" import_name)
            string(APPEND import_directives "#pragma comment(linker, \"/export:${import_name}\")\n")
        endforeach()
        file(WRITE "${import_source}" "${import_directives}")
        file(WRITE "${import_def}" "${library_name}\n")
        add_custom_command(
            OUTPUT "${import_object}"
            COMMAND "${CMAKE_CXX_COMPILER}" ${WIZ8_IMPORT_COMPILE_OPTIONS} /nologo /c
                "/Fo${import_object}" "${import_source}"
            DEPENDS "${import_source}"
            VERBATIM
        )
        list(APPEND import_objects "${import_object}")
    endif()
    set(def_library "${import_library}")
    set(combine_imports)
    if(ARG_IMPORT_OBJECTS)
        set(def_library "${CMAKE_BINARY_DIR}/${target_stem}-def.lib")
        # Place custom DLL members before the ordinary library's terminators.
        set(combine_imports COMMAND "${CMAKE_AR}" /nologo
            "/out:${import_library}" ${ARG_IMPORT_OBJECTS} "${def_library}")
    endif()
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        find_program(LLVM_DLLTOOL NAMES llvm-dlltool REQUIRED)
        set(import_command "${LLVM_DLLTOOL}" -m i386 --no-leading-underscore
            -d "${import_def}" -l "${def_library}")
    else()
        set(import_command "${CMAKE_AR}" /nologo /machine:ix86
            "/def:${import_def}" ${import_objects} "/out:${def_library}")
    endif()
    add_custom_command(
        OUTPUT "${import_library}"
        COMMAND ${import_command}
        ${combine_imports}
        DEPENDS "${DEF_FILE}" "${import_def}" ${import_objects} ${ARG_IMPORT_OBJECTS}
        VERBATIM
    )
    add_custom_target(${target_stem}_import_library DEPENDS "${import_library}")
    set(${NAME}_IMPORT_LIBRARY "${import_library}" PARENT_SCOPE)
    set(${NAME}_IMPORT_TARGET "${target_stem}_import_library" PARENT_SCOPE)
endfunction()

# VC6 LINK under Wine needs a COFF object rather than RC's raw .res.
function(wiz8_compile_resource OUT RC_FILE)
    get_filename_component(stem "${RC_FILE}" NAME_WE)
    set(resource_res "${CMAKE_CURRENT_BINARY_DIR}/${stem}_resource.res")
    set(resource_object "${CMAKE_CURRENT_BINARY_DIR}/${stem}_resource.obj")
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        # LLD combines raw resources with its generated application manifest.
        add_custom_command(
            OUTPUT "${resource_res}"
            COMMAND "${CMAKE_RC_COMPILER}" ${WIZ8_RESOURCE_INCLUDE_OPTIONS}
                "/fo${resource_res}" "${CMAKE_CURRENT_SOURCE_DIR}/${RC_FILE}"
            DEPENDS "${RC_FILE}"
            VERBATIM
        )
        set_source_files_properties("${resource_res}" PROPERTIES
            EXTERNAL_OBJECT TRUE GENERATED TRUE)
        set(${OUT} "${resource_res}" PARENT_SCOPE)
        return()
    endif()
    find_program(CVTRES_EXECUTABLE cvtres.exe REQUIRED)
    add_custom_command(
        OUTPUT "${resource_object}"
        COMMAND "${CMAKE_RC_COMPILER}" ${WIZ8_RESOURCE_INCLUDE_OPTIONS}
            "/fo${resource_res}" "${CMAKE_CURRENT_SOURCE_DIR}/${RC_FILE}"
        COMMAND "${CVTRES_EXECUTABLE}" /nologo /machine:ix86
            "/out:${resource_object}" "${resource_res}"
        DEPENDS "${RC_FILE}"
        VERBATIM
    )
    set_source_files_properties("${resource_object}" PROPERTIES
        EXTERNAL_OBJECT TRUE
        GENERATED TRUE
    )
    set(${OUT} "${resource_object}" PARENT_SCOPE)
endfunction()
