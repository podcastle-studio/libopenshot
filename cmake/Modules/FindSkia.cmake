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
find_path(SKIA_INCLUDE_DIR
        NAMES core/SkCanvas.h
        PATHS
        ${PC_SKIA_INCLUDE_DIRS}
        /usr/local/include/skia
        /usr/include/skia
        /opt/skia/include
        PATH_SUFFIXES skia
)

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