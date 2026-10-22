# Fetches third-party runtime inputs into CACHE_DIR once, verified by SHA-256.
# Run by CTest as the runtime_downloads fixture:
#   cmake -DCACHE_DIR=<dir> -P fetch_downloads.cmake
# Files already present with the expected hash are not downloaded again.
cmake_minimum_required(VERSION 4.4)
if(NOT CACHE_DIR)
    message(FATAL_ERROR "CACHE_DIR is required")
endif()

# Each entry is: archive URL, archive SHA-256, member file, member SHA-256.
# Members of one archive repeat its URL; the archive is downloaded once.

# A retail save (patched Wizardry 8, 2003): a two-character level-23 party just
# outside the Rapax Away Camp. Offered to Wizardry 8 players by its author at
# https://r0k.us/rock/games/wizardry8/; downloaded, never redistributed.
set(downloads
    "https://r0k.us/rock/games/wizardry8/p2-RapaxAC.zip"
    "3726a728af64ec9a17772d8adc51f11e27518b2cfca238fc882dedbea8e0cb6b"
    "p2-RapaxAC.sav"
    "8c1eebc907ec0c045dbd6a3fcbb763afd49ed04295f362fbb0decbac0138a003")

# Wizardry 7 end-game parties for the 7->8 import, one per Wizardry 7 ending,
# offered on the same page (two donated by Chris Beebe). Six level 27-35
# characters each: the Vitalia ending (RichVitalia.end), Vitalia sacrificed for
# the Astral Dominae (noVitalia.end), allied with the T'Rang (QuinnTrang.sav)
# and with the Umpani (QuinnUmpani.sav).
set(imports "https://r0k.us/rock/games/wizardry8/wiz8imports.zip"
    "dd484fbb0ef41b088334e01cde662013e210f054b54bc3e8e9c332e672e38c52")
list(APPEND downloads
    ${imports} "RichVitalia.end" "890fc2336860b24bdb51bff702682c25a462cde40745e7d8275612a739663ddb"
    ${imports} "noVitalia.end" "6dce2d565fa68e774c2dfd0e19f2b0a2aa53a48d4a5c0be12b9259db14af13fc"
    ${imports} "QuinnTrang.sav" "c133408ff39c682fbe1209ca28995d6e92df2fb0838d5d62182c3eaf046dcbe0"
    ${imports} "QuinnUmpani.sav" "796cfc9118284398c9399a6fb7b6396b86a6a5ced2466e6f3d2e9cfdad90791b")

function(file_matches path hash result)
    set(${result} FALSE PARENT_SCOPE)
    if(EXISTS "${path}")
        file(SHA256 "${path}" actual)
        if(actual STREQUAL hash)
            set(${result} TRUE PARENT_SCOPE)
        endif()
    endif()
endfunction()

file(MAKE_DIRECTORY "${CACHE_DIR}")
while(downloads)
    list(POP_FRONT downloads url archive_hash member member_hash)
    set(target "${CACHE_DIR}/${member}")
    file_matches("${target}" "${member_hash}" present)
    if(present)
        continue()
    endif()

    cmake_path(GET url FILENAME archive_name)
    set(archive "${CACHE_DIR}/${archive_name}")
    file_matches("${archive}" "${archive_hash}" archived)
    if(NOT archived)
        message(STATUS "Downloading ${url}")
        file(DOWNLOAD "${url}" "${archive}.part" EXPECTED_HASH SHA256=${archive_hash}
             TLS_VERIFY ON INACTIVITY_TIMEOUT 30 TIMEOUT 300 STATUS status)
        list(GET status 0 code)
        if(NOT code EQUAL 0)
            file(REMOVE "${archive}.part")
            message(FATAL_ERROR "Cannot download ${url}: ${status}")
        endif()
        file(RENAME "${archive}.part" "${archive}")
    endif()

    # Extract beside the cache and rename into place, so concurrent or
    # interrupted runs never leave a partial file under the final name.
    string(RANDOM LENGTH 8 suffix)
    set(staging "${CACHE_DIR}/.extract-${suffix}")
    file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${staging}" PATTERNS "${member}")
    file_matches("${staging}/${member}" "${member_hash}" extracted)
    if(NOT extracted)
        file(REMOVE_RECURSE "${staging}")
        message(FATAL_ERROR "${archive_name} does not contain the expected ${member}")
    endif()
    file(RENAME "${staging}/${member}" "${target}")
    file(REMOVE_RECURSE "${staging}")
endwhile()
