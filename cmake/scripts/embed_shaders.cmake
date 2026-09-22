# Turn src/effects/image-processing-lib/shaders/*.sksl into a C++ header of string constants.
#
# The .sksl files are the single source: the editor loads them directly through
# CanvasKit, and this embeds the same bytes into libopenshot so the export has no
# runtime data-path dependency. Nothing edits the generated header; edit the shader.
#
# Invoked at build time (not configure time) with -DSHADER_DIR and -DOUTPUT so the
# header regenerates whenever a shader changes.

file(GLOB SHADER_FILES "${SHADER_DIR}/*.sksl")
list(SORT SHADER_FILES)

set(GENERATED "// GENERATED FROM ${SHADER_DIR} -- DO NOT EDIT.\n")
string(APPEND GENERATED "// Edit the .sksl files; this header is rebuilt from them.\n\n")
string(APPEND GENERATED "#pragma once\n\nnamespace openshot {\nnamespace shaders {\n\n")

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
endforeach()

string(APPEND GENERATED "} // namespace shaders\n} // namespace openshot\n")

# Only rewrite when the content changed, so a no-op build stays a no-op.
set(EXISTING "")
if(EXISTS "${OUTPUT}")
  file(READ "${OUTPUT}" EXISTING)
endif()
if(NOT EXISTING STREQUAL GENERATED)
  file(WRITE "${OUTPUT}" "${GENERATED}")
  message(STATUS "Embedded ${SHADER_DIR} -> ${OUTPUT}")
endif()
