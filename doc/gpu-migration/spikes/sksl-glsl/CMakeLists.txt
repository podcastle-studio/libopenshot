# Spike: does one SkSL source, emitted to GLSL ES, give the two stacks the same pixels?
#
# Standalone on purpose — it links Skia and OpenCV directly and is not part of the library build.
#
#   cmake -S . -B build && cmake --build build
#   ./build/parity_check testdata/checker_256.png 6 out/
cmake_minimum_required(VERSION 3.16)
project(sksl-glsl-spike CXX)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../../cmake/Modules")
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Skia REQUIRED)
find_package(OpenCV REQUIRED COMPONENTS core imgproc)

set(IPL "${CMAKE_CURRENT_SOURCE_DIR}/../../../../src/effects/image-processing-lib")

add_executable(parity_check parity_check.cpp "${IPL}/src/Effects/effects.cpp")
target_compile_features(parity_check PRIVATE cxx_std_20)
target_include_directories(parity_check PRIVATE ${SKIA_INCLUDE_DIRS} "${IPL}/src/Effects")
target_link_libraries(parity_check PRIVATE ${SKIA_LIBRARIES} ${OpenCV_LIBS})
