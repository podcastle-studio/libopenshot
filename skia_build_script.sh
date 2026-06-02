#!/bin/bash
#
# fetch and build a CPU-only Skia (no GPU), then generate install_skia.sh
# (runs entirely in user directory)
#
# To target a different Skia release, change SKIA_MILESTONE below to any
# `chrome/m###` branch (e.g. m147 to match canvaskit-wasm 0.41.x).
#

set -e

# Skia release branch to build (chrome/m###). Keep this in sync with the
# front end's canvaskit-wasm milestone so CPU/version-driven differences vanish.
SKIA_MILESTONE="${SKIA_MILESTONE:-m147}"

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

echo -e "${GREEN}=== Building Stable Skia (No GPU) ===${NC}"
echo -e "${BLUE}This will build Skia chrome/${SKIA_MILESTONE} (stable) with:${NC}"
echo "  ✓ CPU rendering only (no GPU)"
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

if [[ -n "$MISSING_DEPS" ]]; then
    echo -e "${YELLOW}Installing missing dependencies:$MISSING_DEPS${NC}"
    sudo apt update
    sudo apt install -y $MISSING_DEPS
fi

# ---------------------------------------------------------------------------
# 0.  Prepare build directory in user's home
# ---------------------------------------------------------------------------
BUILD_DIR="$HOME/skia-stable"
echo -e "${YELLOW}Preparing build directory: $BUILD_DIR${NC}"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Verify we're in the right place
echo "Working in: $(pwd)"
echo "Running as user: $(whoami)"

# ---------------------------------------------------------------------------
# 1.  Clone Skia and check out chrome/m131
# ---------------------------------------------------------------------------
echo -e "${YELLOW}Cloning Skia stable branch...${NC}"
git clone https://skia.googlesource.com/skia.git
cd skia

echo -e "${YELLOW}Checking out stable release chrome/${SKIA_MILESTONE}...${NC}"
git checkout "chrome/${SKIA_MILESTONE}"

echo -e "${YELLOW}Syncing dependencies...${NC}"
python3 tools/git-sync-deps

# ---------------------------------------------------------------------------
# 2.  Configure CPU-only build
# ---------------------------------------------------------------------------
echo -e "${YELLOW}Creating CPU-only build configuration...${NC}"
mkdir -p out/Release-CPU

cat > out/Release-CPU/args.gn <<'EOF'
is_component_build = false
is_debug          = false
is_official_build = true
target_cpu        = "x64"

# Disable all GPU back-ends (arg was renamed skia_enable_gpu -> skia_enable_ganesh
# in newer milestones; the old name is silently ignored).
skia_enable_ganesh = false
skia_use_gl       = false
skia_use_egl      = false
skia_use_vulkan   = false
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

echo -e "${YELLOW}Generating build files...${NC}"
bin/gn gen out/Release-CPU

# ---------------------------------------------------------------------------
# 3.  Build
# ---------------------------------------------------------------------------
echo -e "${YELLOW}Building Skia (this will take a while)...${NC}"
echo "Build location: $(pwd)/out/Release-CPU/"
# Build ONLY the static library target — not tests/tools (which don't always
# compile cleanly and aren't needed here).
ninja -C out/Release-CPU skia

# ---------------------------------------------------------------------------
# 4.  Verify and create the installer
# ---------------------------------------------------------------------------
if [[ -f out/Release-CPU/libskia.a ]]; then
    echo -e "${GREEN}Build successful!${NC}"
    ls -lh out/Release-CPU/libskia.a
    echo "Build completed in: $(pwd)"

    # -----------------------------------------------------------------------
    # 4a. Generate install_skia.sh
    # -----------------------------------------------------------------------
    cat > install_skia.sh <<'INSTALL_SCRIPT'
#!/bin/bash
#
# install_skia.sh — install a CPU-only Skia build
#

set -e
INSTALL_PREFIX="${INSTALL_PREFIX:-/usr/local}"
SKIA_DIR="$(pwd)"

if [[ $EUID -ne 0 ]]; then
    echo "Please run with sudo:  sudo ./install_skia.sh"
    exit 1
fi

echo "Installing Skia to $INSTALL_PREFIX …"

install -d "$INSTALL_PREFIX/lib" \
           "$INSTALL_PREFIX/include" \
           "$INSTALL_PREFIX/lib/pkgconfig"

echo "  • Copying static library"
install -m 644 "$SKIA_DIR/out/Release-CPU/libskia.a" \
               "$INSTALL_PREFIX/lib/"

echo "  • Copying public headers"
rsync -a --delete "$SKIA_DIR/include"          "$INSTALL_PREFIX/include/skia/"

cat > "$INSTALL_PREFIX/lib/pkgconfig/skia.pc" <<EOF
prefix=$INSTALL_PREFIX
exec_prefix=\${prefix}
libdir=\${exec_prefix}/lib
includedir=\${prefix}/include

Name: Skia
Description: Skia Graphics Library (CPU-only, Stable)
Version: chrome-__SKIA_MILESTONE__
Libs: -L\${libdir} -lskia
Libs.private: -lfreetype -lfontconfig -lharfbuzz -licuuc -licudata -lpng -ljpeg -lexpat -lz -lpthread -ldl -lm
Cflags: -I\${includedir}/skia
EOF

echo -e "\n✔ Installation complete!"
echo ""
echo "To use in your project:"
echo "  Compile: g++ -c myfile.cpp  \$(pkg-config --cflags skia)"
echo "  Link:    g++ myfile.o       \$(pkg-config --libs skia)"
INSTALL_SCRIPT
    # Bake the milestone into the generated installer's skia.pc Version field.
    sed -i "s/__SKIA_MILESTONE__/${SKIA_MILESTONE}/g" install_skia.sh
    chmod +x install_skia.sh

    echo ""
    echo -e "${BLUE}Build completed successfully!${NC}"
    echo -e "${GREEN}All files are in your user directory: $BUILD_DIR${NC}"
    echo ""
    echo -e "${BLUE}Next steps:${NC}"
    echo "  1. Install Skia:   cd $BUILD_DIR/skia && sudo ./install_skia.sh"
    echo "  2. Test your installation with pkg-config --cflags --libs skia"
    echo ""
    echo -e "${GREEN}Files ownership: $(ls -ld $BUILD_DIR/skia | awk '{print $3":"$4}')${NC}"
else
    echo -e "${RED}Build failed — libskia.a not found.${NC}"
    exit 1
fi