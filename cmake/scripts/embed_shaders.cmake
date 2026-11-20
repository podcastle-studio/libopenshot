# Turn the .sksl files of one or more directories into a C++ header of string constants.
#
# Two directories today: src/effects/image-processing-lib/shaders/ (the prelude and the transition
# fragments, which the editor loads unchanged through CanvasKit) and src/shaders/ (the per-clip
# effects only the export runs). The .sksl files are the source; this embeds their bytes so the
# export has no runtime data-path dependency. Nothing edits the generated header; edit the shader.
#
# Invoked at build time (not configure time) with -DSHADER_DIRS (directories separated by "|")
# and -DOUTPUT, so the header regenerates whenever a shader changes.

cmake_policy(SET CMP0057 NEW)   # if(IN_LIST), which -P script mode does not enable by default
string(REPLACE "|" ";" SHADER_DIR_LIST "${SHADER_DIRS}")
set(SHADER_FILES "")
set(SEEN_STEMS "")
foreach(DIR ${SHADER_DIR_LIST})
  file(GLOB DIR_FILES "${DIR}/*.sksl")
  list(SORT DIR_FILES)
  foreach(SHADER ${DIR_FILES})
    get_filename_component(STEM "${SHADER}" NAME_WE)
    # One namespace for every stem: the planner and ByName() look fragments up by stem alone.
    if(STEM IN_LIST SEEN_STEMS)
      message(FATAL_ERROR "embed_shaders: ${STEM}.sksl exists in more than one of ${SHADER_DIRS}")
    endif()
    list(APPEND SEEN_STEMS "${STEM}")
    list(APPEND SHADER_FILES "${SHADER}")
  endforeach()
endforeach()

set(GENERATED "// GENERATED FROM ${SHADER_DIRS} -- DO NOT EDIT.\n")
string(APPEND GENERATED "// Edit the .sksl files; this header is rebuilt from them.\n\n")
string(APPEND GENERATED "#pragma once\n\n#include <cstring>\n\nnamespace openshot {\nnamespace shaders {\n\n")
set(LOOKUP "")

foreach(SHADER ${SHADER_FILES})
  get_filename_component(NAME "${SHADER}" NAME_WE)
  # _prelude -> kPrelude, border_reflected_move -> kBorderReflectedMove
  string(REGEX REPLACE "^_" "" SYMBOL "${NAME}")
  string(REPLACE "_" ";" PARTS "${SYMBOL}")
  set(SYMBOL "")
  foreach(PART ${PARTS})
    string(SUBSTRING "${PART}" 0 1 FIRST)
    string(SUBSTRING "${PART}" 1 -1 REST)
    string(TOUPPER "${FIRST}" FIRST)
    string(APPEND SYMBOL "${FIRST}${REST}")
  endforeach()
  file(READ "${SHADER}" CONTENT)
  # A raw string literal needs a delimiter the content cannot contain.
  string(APPEND GENERATED "inline constexpr char k${SYMBOL}[] = R\"SKSLSRC(\n${CONTENT})SKSLSRC\";\n\n")
  string(APPEND LOOKUP "\tif (std::strcmp(name, \"${NAME}\") == 0) return k${SYMBOL};\n")
endforeach()

# By file stem, for hosts driven by the effect planner (image-processing-lib/src/Planner), which
# names each pass's fragment the way the editor does: "zoom", "blur", "zoom_blur_forward", ...
# Returns the same pointer every time, which GpuEffect's program cache relies on.
string(APPEND GENERATED "inline const char* ByName(const char* name)\n{\n${LOOKUP}\treturn nullptr;\n}\n\n")
string(APPEND GENERATED "} // namespace shaders\n} // namespace openshot\n")

# Only rewrite when the content changed, so a no-op build stays a no-op.
set(EXISTING "")
if(EXISTS "${OUTPUT}")
  file(READ "${OUTPUT}" EXISTING)
endif()
if(NOT EXISTING STREQUAL GENERATED)
  file(WRITE "${OUTPUT}" "${GENERATED}")
  message(STATUS "Embedded ${SHADER_DIRS} -> ${OUTPUT}")
endif()
