# Performance benchmark (openshot-bench). Reuses the golden suite's Scene + Recipes.
find_package(Qt5 COMPONENTS Gui REQUIRED)

add_executable(openshot-bench
  main.cpp
  Scenarios.cpp
  ../golden/Harness.cpp
  ../golden/Image.cpp
  ../golden/Report.cpp
  ../golden/Recipes.cpp
)
target_include_directories(openshot-bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../golden)
target_link_libraries(openshot-bench PRIVATE openshot Qt5::Gui)

file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../golden/media/" BENCH_GOLDEN_MEDIA_DIR)
file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/media/" BENCH_MEDIA_DIR)
file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/results" BENCH_RESULTS_DIR)
target_compile_definitions(openshot-bench PRIVATE
  BENCH_GOLDEN_MEDIA_DIR="${BENCH_GOLDEN_MEDIA_DIR}"
  BENCH_MEDIA_DIR="${BENCH_MEDIA_DIR}"
  BENCH_RESULTS_DIR="${BENCH_RESULTS_DIR}"
  BENCH_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
  BENCH_BUILD_TYPE="${CMAKE_BUILD_TYPE}")
