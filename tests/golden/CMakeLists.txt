# Golden-frame visual regression harness (openshot-golden).
# Standalone executable, no Catch2. Registered with CTest as "golden".
find_package(Qt5 COMPONENTS Gui REQUIRED)

add_executable(openshot-golden
  main.cpp
  Harness.cpp
  Image.cpp
  Report.cpp
  Recipes.cpp
  scenarios/Readers.cpp
  scenarios/Transform.cpp
  scenarios/Compositing.cpp
  scenarios/ClipFx.cpp
  scenarios/Effects.cpp
  scenarios/Transitions.cpp
  scenarios/Text.cpp
  scenarios/Subtitles.cpp
  scenarios/Time.cpp
  scenarios/Export.cpp
  scenarios/Unit.cpp
)
target_include_directories(openshot-golden PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(openshot-golden PRIVATE openshot Qt5::Gui)

file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/media/" GOLDEN_MEDIA_DIR)
file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/expected" GOLDEN_EXPECTED_DIR)
target_compile_definitions(openshot-golden PRIVATE
  GOLDEN_MEDIA_DIR="${GOLDEN_MEDIA_DIR}"
  GOLDEN_EXPECTED_DIR="${GOLDEN_EXPECTED_DIR}")

add_test(NAME golden
  COMMAND openshot-golden --out "${CMAKE_BINARY_DIR}/golden-out" --report "${CMAKE_BINARY_DIR}/golden-report")
set_tests_properties(golden PROPERTIES LABELS golden)

add_custom_target(golden-check
  COMMAND openshot-golden --out "${CMAKE_BINARY_DIR}/golden-out" --report "${CMAKE_BINARY_DIR}/golden-report"
  DEPENDS openshot-golden USES_TERMINAL)
add_custom_target(golden-update
  COMMAND openshot-golden --update --out "${CMAKE_BINARY_DIR}/golden-out"
  DEPENDS openshot-golden USES_TERMINAL)
