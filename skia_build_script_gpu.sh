#!/bin/bash
#
# fetch and build a GPU-capable Skia (Graphite over Vulkan), then generate
# install_skia_gpu.sh
# (runs entirely in user directory)
#
# Sibling of skia_build_script.sh — NEVER modify that one to add GPU support.
# The CPU build must stay reproducible as the no-GPU fallback. These two scripts
# differ only in: the output directory, the GPU GN args, the install prefix, and
# the fact that this one reuses an existing checkout instead of wiping it.
# Everything else is byte-identical on purpose; `diff skia_build_script.sh
# skia_build_script_gpu.sh` is part of reviewing a change to either.
#
# To target a different Skia release, change SKIA_MILESTONE below to any
# `chrome/m###` branch (e.g. m147 to match canvaskit-wasm 0.41.x).
#
# Knobs:
#   SKIA_MILESTONE      chrome/m### branch to build           (default m147)
#   SKIA_ENABLE_GANESH  also build the Ganesh Vulkan backend  (default false)
#   SKIA_FORCE_CLONE=1  delete $HOME/skia-stable and re-clone (default: reuse)
#

set -e

# Skia release branch to build (chrome/m###). Keep this in sync with the
# front end's canvaskit-wasm milestone so CPU/version-driven differences vanish.
SKIA_MILESTONE="${SKIA_MILESTONE:-m147}"

# Graphite is the backend this fork targets (see doc/GPU-RENDERING.md, "Decisions").
# Ganesh is kept behind a knob because the decisions log wants both available
# before the phase-3 feature checks; the plan's default for step 2.1 is false.
SKIA_ENABLE_GANESH="${SKIA_ENABLE_GANESH:-false}"
if [[ "$SKIA_ENABLE_GANESH" != "true" && "$SKIA_ENABLE_GANESH" != "false" ]]; then
    echo "Error: SKIA_ENABLE_GANESH must be 'true' or 'false' (got '$SKIA_ENABLE_GANESH')"
    exit 1
fi

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
RED='\033[0;31m'
NC='\033[0m'

# Exit if running as root
if [[ $EUID -eq 0 ]]; then
    echo -e "${RED}Error: Do not run this script as root or with sudo!${NC}"
    echo "This script should run as your regular user."
    echo "It will ask for sudo password only when needed for dependency installation."
    exit 1
fi

echo -e "${GREEN}=== Building Stable Skia (Graphite / Vulkan) ===${NC}"
echo -e "${BLUE}This will build Skia chrome/${SKIA_MILESTONE} (stable) with:${NC}"
echo "  ✓ Graphite over Vulkan (Ganesh: ${SKIA_ENABLE_GANESH})"
echo "  ✓ Full text and font support"
echo "  ✓ PNG and JPEG support"
echo "  ✓ SVG and PDF support"
echo "  ✓ Stable, tested codebase"
echo ""

# Check for required tools
echo -e "${YELLOW}Checking dependencies...${NC}"
MISSING_DEPS=""

if ! command -v git &> /dev/null; then
    MISSING_DEPS="$MISSING_DEPS git"
fi

if ! command -v python3 &> /dev/null; then
    MISSING_DEPS="$MISSING_DEPS python3"
fi

if ! command -v clang &> /dev/null; then
    MISSING_DEPS="$MISSING_DEPS clang"
fi

if ! command -v clang++ &> /dev/null; then
    MISSING_DEPS="$MISSING_DEPS clang++"
fi

if ! command -v ninja &> /dev/null; then
    MISSING_DEPS="$MISSING_DEPS ninja-build"
fi

if ! command -v rsync &> /dev/null; then
    MISSING_DEPS="$MISSING_DEPS rsync"
fi

if [[ -n "$MISSING_DEPS" ]]; then
    echo -e "${YELLOW}Installing missing dependencies:$MISSING_DEPS${NC}"
    sudo apt update
    sudo apt install -y $MISSING_DEPS
fi

# Skia's Vulkan backend is loaded through a proc-address getter supplied by the
# application, so libvulkan is NOT a build-time dependency. It is a *runtime*
# one: warn, do not install, do not fail.
if ! ldconfig -p 2>/dev/null | grep -q 'libvulkan\.so\.1'; then
    echo -e "${YELLOW}Note: libvulkan.so.1 not found. The build does not need it, but running"
    echo -e "      anything against this Skia does:  sudo apt install libvulkan1 vulkan-tools${NC}"
fi

# ---------------------------------------------------------------------------
# 0.  Prepare build directory in user's home
# ---------------------------------------------------------------------------
BUILD_DIR="$HOME/skia-stable"
echo -e "${YELLOW}Preparing build directory: $BUILD_DIR${NC}"
if [[ "${SKIA_FORCE_CLONE:-0}" == "1" ]]; then
    echo -e "${YELLOW}SKIA_FORCE_CLONE=1 — removing $BUILD_DIR (this also drops out/Release-CPU)${NC}"
    rm -rf "$BUILD_DIR"
fi
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Verify we're in the right place
echo "Working in: $(pwd)"
echo "Running as user: $(whoami)"

# ---------------------------------------------------------------------------
# 1.  Clone Skia and check out chrome/${SKIA_MILESTONE}
#     Unlike the CPU script this REUSES an existing checkout: both builds live
#     in one source tree as out/Release-CPU and out/Release-GPU, and wiping it
#     would destroy the CPU build we depend on as the fallback.
# ---------------------------------------------------------------------------
if [[ -d "$BUILD_DIR/skia/.git" ]]; then
    echo -e "${YELLOW}Reusing existing checkout: $BUILD_DIR/skia${NC}"
    cd skia
    if ! git rev-parse --verify --quiet "chrome/${SKIA_MILESTONE}" > /dev/null; then
        echo -e "${YELLOW}Fetching chrome/${SKIA_MILESTONE}...${NC}"
        git fetch origin "chrome/${SKIA_MILESTONE}:chrome/${SKIA_MILESTONE}"
    fi
    if [[ -n "$(git status --porcelain --untracked-files=no)" ]]; then
        echo -e "${RED}Error: $BUILD_DIR/skia has local modifications.${NC}"
        echo "Commit, stash or discard them, or re-run with SKIA_FORCE_CLONE=1."
        exit 1
    fi
else
    echo -e "${YELLOW}Cloning Skia stable branch...${NC}"
    git clone https://skia.googlesource.com/skia.git
    cd skia
fi

echo -e "${YELLOW}Checking out stable release chrome/${SKIA_MILESTONE}...${NC}"
git checkout "chrome/${SKIA_MILESTONE}"

echo -e "${YELLOW}Syncing dependencies...${NC}"
python3 tools/git-sync-deps

# ---------------------------------------------------------------------------
# 2.  Configure Graphite/Vulkan build
# ---------------------------------------------------------------------------
echo -e "${YELLOW}Creating Graphite/Vulkan build configuration...${NC}"
mkdir -p out/Release-GPU

cat > out/Release-GPU/args.gn <<'EOF'
is_component_build = false
is_debug          = false
is_official_build = true
target_cpu        = "x64"

# GPU back-ends: Graphite over Vulkan. skia_use_vma defaults to skia_use_vulkan
# (gn/skia.gni), so the allocator comes along without being named here.
# EVERY ARG BELOW THIS BLOCK IS IDENTICAL TO skia_build_script.sh — keep it that
# way so text rendering cannot drift between the CPU and GPU builds.
skia_enable_graphite = true
skia_use_vulkan   = true
skia_enable_ganesh = __SKIA_ENABLE_GANESH__
skia_use_gl       = false
skia_use_egl      = false
skia_use_metal    = false
skia_use_dawn     = false
skia_use_direct3d = false

# Text and font
skia_use_freetype             = true
skia_use_fontconfig           = true
skia_enable_fontmgr_fontconfig = true
skia_use_harfbuzz             = true
skia_use_icu                  = true

# Image codecs
skia_use_libpng_decode        = true
skia_use_libpng_encode        = true
skia_use_libjpeg_turbo_decode = true
skia_use_libjpeg_turbo_encode = true
skia_use_zlib                 = true

# Disable other image formats
skia_use_libwebp_decode       = false
skia_use_libwebp_encode       = false
skia_use_libheif              = false
skia_use_libavif              = false
skia_use_libjxl_decode        = false
skia_use_libjxl_encode        = false

# Enable extras
skia_enable_svg   = true
skia_enable_pdf   = true
skia_use_expat    = true
skia_enable_skshaper = true

# Use bundled deps
skia_use_system_freetype2     = false
skia_use_system_harfbuzz      = false
skia_use_system_icu           = false
skia_use_system_libjpeg_turbo = false
skia_use_system_libpng        = false
skia_use_system_zlib          = false
skia_use_system_expat         = false

# Disable tools & tests
skia_enable_tools = false
skia_enable_tests = false
skia_enable_skottie = false
skia_enable_particles = false
skia_use_lua = false

cc  = "clang"
cxx = "clang++"

extra_cflags = [
  "-O3", "-DNDEBUG", "-fPIC", "-fno-exceptions", "-fno-rtti"
]
# Skia m147 requires C++20 (uses std::countl_zero/popcount from <bit>).
# Forcing c++17 here would override Skia's own -std=c++20 and break the build.
extra_cflags_cc = [ "-std=c++20" ]
extra_ldflags   = [ "-fPIC" ]
EOF

# Bake the Ganesh choice into args.gn (the heredoc above is quoted, by design).
sed -i "s/__SKIA_ENABLE_GANESH__/${SKIA_ENABLE_GANESH}/" out/Release-GPU/args.gn

echo -e "${YELLOW}Generating build files...${NC}"
bin/gn gen out/Release-GPU

# ---------------------------------------------------------------------------
# 3.  Build
# ---------------------------------------------------------------------------
echo -e "${YELLOW}Building Skia (this will take a while)...${NC}"
echo "Build location: $(pwd)/out/Release-GPU/"
# Build ONLY the static library target — not tests/tools (which don't always
# compile cleanly and aren't needed here).
ninja -C out/Release-GPU skia

# ---------------------------------------------------------------------------
# 4.  Verify and create the installer
# ---------------------------------------------------------------------------
if [[ -f out/Release-GPU/libskia.a ]]; then
    echo -e "${GREEN}Build successful!${NC}"
    ls -lh out/Release-GPU/libskia.a
    echo "Build completed in: $(pwd)"

    # -----------------------------------------------------------------------
    # 4a. Generate install_skia_gpu.sh
    # -----------------------------------------------------------------------
    cat > install_skia_gpu.sh <<'INSTALL_SCRIPT'
#!/bin/bash
#
# install_skia_gpu.sh — install a Graphite/Vulkan Skia build
#
# Installs to its OWN prefix so it coexists with the CPU build in /usr/local.
# Select it at configure time with:  cmake -DSkia_ROOT=/usr/local/skia-gpu ...
#

set -e
INSTALL_PREFIX="${INSTALL_PREFIX:-/usr/local/skia-gpu}"
# Where the Skia tree is, derived from this script's own location rather than
# $(pwd): sudo keeps the caller's working directory, so a `sudo
# ~/skia-stable/skia/install_skia_gpu.sh` from anywhere else would otherwise
# create the prefix directories and then fail on the first copy.
SKIA_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ $EUID -ne 0 ]]; then
    echo "Please run with sudo:  sudo ./install_skia_gpu.sh"
    exit 1
fi

if [[ "$INSTALL_PREFIX" == "/usr/local" ]]; then
    echo "Refusing to install the GPU build over the CPU build in /usr/local."
    echo "Pick another INSTALL_PREFIX (default: /usr/local/skia-gpu)."
    exit 1
fi

if [[ ! -f "$SKIA_DIR/out/Release-GPU/libskia.a" ]]; then
    echo "No build at $SKIA_DIR/out/Release-GPU/libskia.a."
    echo "Run skia_build_script_gpu.sh first."
    exit 1
fi

echo "Installing Skia (Graphite/Vulkan) from $SKIA_DIR to $INSTALL_PREFIX …"

install -d "$INSTALL_PREFIX/lib" \
           "$INSTALL_PREFIX/include" \
           "$INSTALL_PREFIX/lib/pkgconfig"

echo "  • Copying static library"
install -m 644 "$SKIA_DIR/out/Release-GPU/libskia.a" \
               "$INSTALL_PREFIX/lib/"

echo "  • Copying public headers"
rsync -a --delete "$SKIA_DIR/include"          "$INSTALL_PREFIX/include/skia/"

# SkRuntimeEffect (the text glow shader) transitively includes
# "modules/skcms/skcms.h", and skcms.h in turn includes "src/skcms_public.h"
# relative to itself — so the whole module directory has to land under the same
# include root. Without this, cmake/Modules/FindSkia.cmake has to go hunting for
# a Skia source tree (SKIA_MODULES_ROOT).
echo "  • Copying skcms module headers"
install -d "$INSTALL_PREFIX/include/skia/modules"
rsync -a --delete "$SKIA_DIR/modules/skcms"    "$INSTALL_PREFIX/include/skia/modules/"

# Graphite's VulkanBackendContext requires the caller to supply a
# VulkanMemoryAllocator, and Skia's own VMA-backed implementation is reachable
# only through skgpu::VulkanMemoryAllocators::Make, declared in a private header
# ("we cannot really expose this to clients in a meaningful way" — its own
# comment). The symbol IS in libskia.a. These two headers are self-contained
# (they include nothing but public headers), so install them at their source-tree
# paths, where their relative #includes still resolve, rather than having every
# caller hand-redeclare a private symbol.
echo "  • Copying the two private GPU headers Graphite bootstrap needs"
install -d "$INSTALL_PREFIX/include/skia/src/gpu/vk/vulkanmemoryallocator"
install -m 644 "$SKIA_DIR/src/gpu/GpuTypesPriv.h" \
               "$INSTALL_PREFIX/include/skia/src/gpu/"
install -m 644 "$SKIA_DIR/src/gpu/vk/vulkanmemoryallocator/VulkanMemoryAllocatorPriv.h" \
               "$INSTALL_PREFIX/include/skia/src/gpu/vk/vulkanmemoryallocator/"

# Skia m147's include/gpu/vk/VulkanPreferredFeatures.h uses Vulkan 1.4 types
# (VkPhysicalDeviceVulkan14Features, VkPhysicalDeviceHostImageCopyFeatures, ...).
# Ubuntu 24.04 ships 1.3.275 in libvulkan-dev, so including that header against
# the system headers does not compile. Skia builds against the vendored headers
# in third_party/externals/vulkan-headers, so ship exactly those and have
# consumers put this directory ahead of /usr/include. Only the C headers: the
# C++ bindings are 20 MB of vulkan.hpp that nothing here uses.
echo "  • Copying the Vulkan headers Skia was built against"
install -d "$INSTALL_PREFIX/include/skia-vulkan"
rsync -a --delete --prune-empty-dirs \
      --include='*/' --include='*.h' --exclude='*' \
      "$SKIA_DIR/third_party/externals/vulkan-headers/include/" \
      "$INSTALL_PREFIX/include/skia-vulkan/"

cat > "$INSTALL_PREFIX/lib/pkgconfig/skia.pc" <<EOF
prefix=$INSTALL_PREFIX
exec_prefix=\${prefix}
libdir=\${exec_prefix}/lib
includedir=\${prefix}/include

Name: Skia
Description: Skia Graphics Library (Graphite/Vulkan, Stable)
Version: chrome-__SKIA_MILESTONE__
Libs: -L\${libdir} -lskia
Libs.private: -lfreetype -lfontconfig -lharfbuzz -licuuc -licudata -lpng -ljpeg -lexpat -lz -lpthread -ldl -lm
Cflags: -I\${includedir}/skia -I\${includedir}/skia-vulkan
EOF

echo -e "\n✔ Installation complete!"
echo ""
echo "This prefix is deliberately NOT on the default pkg-config path, so the CPU"
echo "build in /usr/local stays the default. To use the GPU build:"
echo "  cmake -S . -B cmake-build-gpu -DCMAKE_BUILD_TYPE=Release -DSkia_ROOT=$INSTALL_PREFIX"
INSTALL_SCRIPT
    # Bake the milestone into the generated installer's skia.pc Version field.
    sed -i "s/__SKIA_MILESTONE__/${SKIA_MILESTONE}/g" install_skia_gpu.sh
    chmod +x install_skia_gpu.sh

    echo ""
    echo -e "${BLUE}Build completed successfully!${NC}"
    echo -e "${GREEN}All files are in your user directory: $BUILD_DIR${NC}"
    echo ""
    echo -e "${BLUE}Next steps:${NC}"
    echo "  1. Install Skia:   cd $BUILD_DIR/skia && sudo ./install_skia_gpu.sh"
    echo "  2. Configure with: cmake -DSkia_ROOT=/usr/local/skia-gpu ..."
    echo ""
    echo -e "${GREEN}Files ownership: $(ls -ld $BUILD_DIR/skia | awk '{print $3":"$4}')${NC}"
else
    echo -e "${RED}Build failed — libskia.a not found.${NC}"
    exit 1
fi
