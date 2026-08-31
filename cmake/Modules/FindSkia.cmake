# Find Skia library
# This module defines:
#   SKIA_FOUND - system has Skia
#   SKIA_INCLUDE_DIRS - Skia include directories
#   SKIA_LIBRARIES - Libraries needed to use Skia
#   SKIA_DEFINITIONS - Compiler flags for Skia

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(PC_SKIA QUIET skia)
endif()

# Find include directory
# First try to use pkg-config result if available
if(PC_SKIA_FOUND AND PC_SKIA_INCLUDE_DIRS)
    set(SKIA_INCLUDE_DIR ${PC_SKIA_INCLUDE_DIRS})
    message(STATUS "Using Skia include from pkg-config: ${SKIA_INCLUDE_DIR}")
else()
    # Fallback to manual search
    # Skia headers use paths like "include/core/SkCanvas.h"
    # So we need to find the parent directory of "include"
    find_path(SKIA_INCLUDE_DIR
            NAMES include/core/SkCanvas.h
            PATHS
            /usr/local/include/skia
            /usr/include/skia
            /opt/skia
            PATH_SUFFIXES skia
    )
endif()

# Find library
find_library(SKIA_LIBRARY
        NAMES skia
        PATHS
        ${PC_SKIA_LIBRARY_DIRS}
        /usr/local/lib
        /usr/lib
        /usr/lib/x86_64-linux-gnu
        /opt/skia/lib
)

# Find required dependencies
find_package(Freetype REQUIRED)
find_package(Fontconfig REQUIRED)
find_package(PNG REQUIRED)
find_package(JPEG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(EXPAT REQUIRED)
find_package(Threads REQUIRED)

# Find optional dependencies
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(HARFBUZZ harfbuzz)
    pkg_check_modules(ICU icu-uc)
endif()

# Handle REQUIRED argument
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Skia
        REQUIRED_VARS SKIA_LIBRARY SKIA_INCLUDE_DIR
        VERSION_VAR PC_SKIA_VERSION
)

if(SKIA_FOUND)
    set(SKIA_INCLUDE_DIRS ${SKIA_INCLUDE_DIR})

    # Skia runtime effects (SkRuntimeEffect, used by the text glow shader) transitively
    # include "modules/skcms/skcms.h". That header lives in the Skia source tree's modules/
    # directory, which is not always shipped alongside the installed core headers. If the
    # primary include dir does not provide it, locate a root that does and add it so the
    # `modules/...` include resolves. Override with -DSKIA_SOURCE_DIR=/path or env SKIA_SOURCE_DIR.
    if(NOT EXISTS "${SKIA_INCLUDE_DIR}/modules/skcms/skcms.h")
        find_path(SKIA_MODULES_ROOT
                NAMES modules/skcms/skcms.h
                HINTS ${SKIA_SOURCE_DIR} $ENV{SKIA_SOURCE_DIR}
                PATHS
                ${SKIA_INCLUDE_DIR}
                /usr/local/include/skia
                /usr/include/skia
                /opt/skia
        )
        if(SKIA_MODULES_ROOT)
            message(STATUS "Found Skia skcms module root: ${SKIA_MODULES_ROOT}")
            list(APPEND SKIA_INCLUDE_DIRS ${SKIA_MODULES_ROOT})
        else()
            message(WARNING "skcms module header not found; SkRuntimeEffect (text glow) may fail to compile. "
                            "Set -DSKIA_SOURCE_DIR to your Skia source tree.")
        endif()
    endif()
    # HarfBuzz headers for colour-emoji shaping. No new runtime dependency: Skia is built with
    # HarfBuzz inside it and exports the hb_* symbols from libskia, so only the declarations are
    # missing. The system harfbuzz headers are preferred (the hb API is ABI-stable, so they work
    # against Skia's bundled build); the Skia source tree's copy is the fallback.
    find_path(SKIA_HARFBUZZ_INCLUDE_DIR
            NAMES hb.h
            HINTS ${HARFBUZZ_INCLUDE_DIRS}
            PATHS
            /usr/include/harfbuzz
            /usr/local/include/harfbuzz
            ${SKIA_SOURCE_DIR}/third_party/externals/harfbuzz/src
            $ENV{SKIA_SOURCE_DIR}/third_party/externals/harfbuzz/src
            ${SKIA_MODULES_ROOT}/third_party/externals/harfbuzz/src
    )
    if(SKIA_HARFBUZZ_INCLUDE_DIR)
        set(SKIA_HARFBUZZ_FOUND TRUE)
        message(STATUS "Found HarfBuzz headers for emoji shaping: ${SKIA_HARFBUZZ_INCLUDE_DIR}")
    else()
        set(SKIA_HARFBUZZ_FOUND FALSE)
        message(WARNING "HarfBuzz headers not found; multi-codepoint emoji (ZWJ sequences, skin "
                        "tones, flags) will fall back to their base codepoint. Install "
                        "libharfbuzz-dev or set -DSKIA_SOURCE_DIR to your Skia source tree.")
    endif()

    set(SKIA_LIBRARIES
            ${SKIA_LIBRARY}
            ${FREETYPE_LIBRARIES}
            ${Fontconfig_LIBRARIES}
            ${PNG_LIBRARIES}
            ${JPEG_LIBRARIES}
            ${ZLIB_LIBRARIES}
            ${EXPAT_LIBRARIES}
            ${CMAKE_THREAD_LIBS_INIT}
            ${CMAKE_DL_LIBS}
            m  # math library
    )

    # Add HarfBuzz if found
    if(HARFBUZZ_FOUND)
        list(APPEND SKIA_LIBRARIES ${HARFBUZZ_LIBRARIES})
    endif()

    # Add ICU if found
    if(ICU_FOUND)
        list(APPEND SKIA_LIBRARIES ${ICU_LIBRARIES})
    endif()

    set(SKIA_DEFINITIONS ${PC_SKIA_CFLAGS_OTHER})

    # Create imported target
    if(NOT TARGET Skia::Skia)
        add_library(Skia::Skia UNKNOWN IMPORTED)
        set_target_properties(Skia::Skia PROPERTIES
                IMPORTED_LOCATION "${SKIA_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${SKIA_INCLUDE_DIRS}"
                INTERFACE_COMPILE_OPTIONS "${SKIA_DEFINITIONS}"
        )

        # Add dependencies to the imported target
        target_link_libraries(Skia::Skia INTERFACE
                ${FREETYPE_LIBRARIES}
                ${FONTCONFIG_LIBRARIES}
                ${PNG_LIBRARIES}
                ${JPEG_LIBRARIES}
                ${ZLIB_LIBRARIES}
                ${EXPAT_LIBRARIES}
                ${CMAKE_THREAD_LIBS_INIT}
                ${CMAKE_DL_LIBS}
                m
        )
    endif()
endif()

mark_as_advanced(SKIA_INCLUDE_DIR SKIA_LIBRARY)