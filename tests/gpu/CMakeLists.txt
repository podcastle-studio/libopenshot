# GPU tests.
#
# openshot-gpu-smoke — acceptance check for plan step 2.1. Links Skia directly,
# NOT libopenshot: in a normal build libopenshot is compiled against the CPU Skia
# in /usr/local and mixing the two static archives in one binary would be a mess.
# It therefore builds standalone, without the rest of the tree.
#
# openshot-gpu-checks — acceptance checks for step 2.2 (src/gpu). This one DOES
# link libopenshot, so it is only built inside the main tree, and only when that
# tree was configured against a GPU Skia.
#
# Build it against the GPU Skia only:
#   cmake -S tests/gpu -B cmake-build-gpu-smoke -DSkia_ROOT=/usr/local/skia-gpu
#   cmake --build cmake-build-gpu-smoke
# or, inside the main tree, -DENABLE_GPU_SMOKE=ON with the same Skia_ROOT.
cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED PROJECT_NAME)
  project(openshot-gpu-smoke CXX)
  list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/Modules")
  if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
  endif()
endif()

find_package(Skia REQUIRED)
find_package(Vulkan REQUIRED)

# Skia m147's include/gpu/vk/VulkanPreferredFeatures.h uses Vulkan 1.4 types, and
# Ubuntu 24.04's libvulkan-dev is 1.3.275 — including it against the system
# headers does not compile. install_skia_gpu.sh ships the headers Skia itself was
# built against under <prefix>/include/skia-vulkan; use those, and link the system
# loader by path rather than through Vulkan::Vulkan so its /usr/include never
# enters the search path.
get_filename_component(_skia_include_root "${SKIA_INCLUDE_DIR}" DIRECTORY)
set(SKIA_VULKAN_INCLUDE_DIR "${_skia_include_root}/skia-vulkan")
if(NOT EXISTS "${SKIA_VULKAN_INCLUDE_DIR}/vulkan/vulkan_core.h")
  message(FATAL_ERROR
    "No Vulkan headers at ${SKIA_VULKAN_INCLUDE_DIR}. This Skia prefix was not "
    "installed by install_skia_gpu.sh, or is the CPU build. Point -DSkia_ROOT at "
    "the GPU prefix (default /usr/local/skia-gpu).")
endif()

add_executable(openshot-gpu-smoke skia_gpu_smoke.cpp)
target_compile_features(openshot-gpu-smoke PRIVATE cxx_std_20)
target_include_directories(openshot-gpu-smoke BEFORE PRIVATE
  ${SKIA_VULKAN_INCLUDE_DIR} ${SKIA_INCLUDE_DIRS})
target_link_libraries(openshot-gpu-smoke PRIVATE ${SKIA_LIBRARIES} ${Vulkan_LIBRARY})

# --- step 2.2 checks, only inside the main tree against a GPU Skia -----------
if(DEFINED PROJECT_NAME AND TARGET openshot AND SKIA_GPU_FOUND)
  add_executable(openshot-gpu-checks gpu_checks.cpp)
  target_compile_features(openshot-gpu-checks PRIVATE cxx_std_20)
  target_include_directories(openshot-gpu-checks BEFORE PRIVATE
    ${SKIA_VULKAN_INCLUDE_DIR} ${SKIA_INCLUDE_DIRS} ${PROJECT_SOURCE_DIR}/src)
  target_link_libraries(openshot-gpu-checks PRIVATE openshot)

  # What a GPU timeline canvas costs before a GPU consumer exists for it (W12/W13 sequencing).
  add_executable(openshot-gpu-canvas-cost gpu_canvas_cost.cpp)
  target_compile_features(openshot-gpu-canvas-cost PRIVATE cxx_std_20)
  target_include_directories(openshot-gpu-canvas-cost BEFORE PRIVATE
    ${SKIA_VULKAN_INCLUDE_DIR} ${SKIA_INCLUDE_DIRS} ${PROJECT_SOURCE_DIR}/src)
  target_link_libraries(openshot-gpu-canvas-cost PRIVATE openshot)

  # Is each GpuEffect's SkSL the same function as its C++ twin? (W19)
  add_executable(openshot-gpu-effect-parity gpu_effect_parity.cpp)
  target_compile_features(openshot-gpu-effect-parity PRIVATE cxx_std_20)
  target_include_directories(openshot-gpu-effect-parity BEFORE PRIVATE
    ${SKIA_VULKAN_INCLUDE_DIR} ${SKIA_INCLUDE_DIRS} ${PROJECT_SOURCE_DIR}/src)
  target_link_libraries(openshot-gpu-effect-parity PRIVATE openshot)

  # Does SkBlendMode agree with BlendModes.cpp on identical pixels? (W13)
  add_executable(openshot-gpu-blend-parity gpu_blend_parity.cpp)
  target_compile_features(openshot-gpu-blend-parity PRIVATE cxx_std_20)
  target_include_directories(openshot-gpu-blend-parity BEFORE PRIVATE
    ${SKIA_VULKAN_INCLUDE_DIR} ${SKIA_INCLUDE_DIRS} ${PROJECT_SOURCE_DIR}/src)
  target_link_libraries(openshot-gpu-blend-parity PRIVATE openshot)   # openshot links Qt PUBLIC
endif()
