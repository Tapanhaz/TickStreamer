# !!! Not Tested !!!!
set -e

BUILD_TYPE=${1:-Release}

echo "Building for: macOS ARM64 ($BUILD_TYPE)"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/versions.env"

BOOST_VERSION_U="${BOOST_VERSION//./_}"
BOOST_DIR="external/boost_${BOOST_VERSION_U}"

if [ ! -d "$BOOST_DIR" ]; then
    echo "Downloading Boost headers $BOOST_VERSION..."
    curl -L \
      "https://archives.boost.io/release/${BOOST_VERSION}/source/boost_${BOOST_VERSION_U}.tar.gz" \
      -o boost.tar.gz
    tar -xzf boost.tar.gz -C external/
    rm boost.tar.gz
fi

echo "Building zstd for macOS ARM64..."
cd external/zstd/lib
make clean || true
make -j$(sysctl -n hw.ncpu) \
    CC=clang \
    AR=ar \
    CFLAGS="-O3 -march=armv8-a" \
    libzstd.a

if [ ! -f libzstd.a ]; then
    echo "ERROR: zstd build failed!"
    exit 1
fi
cd ../../..

echo "Building simdjson for macOS ARM64..."
cd external/simdjson
rm -rf build-macos
mkdir -p build-macos
cd build-macos

cmake .. \
    -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
    -DBUILD_SHARED_LIBS=OFF \
    -DSIMDJSON_JUST_LIBRARY=ON \
    -DCMAKE_OSX_ARCHITECTURES=arm64

make -j$(sysctl -n hw.ncpu)

if [ ! -f libsimdjson.a ]; then
    echo "ERROR: simdjson build failed!"
    exit 1
fi
cd ../../..

echo "Building stream_ticks for macOS ARM64..."
rm -rf build-macos
mkdir -p build-macos
cd build-macos

cmake .. \
    -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
    -DBUILD_STATIC=OFF \
    -DCMAKE_OSX_ARCHITECTURES=arm64

cmake --build . -j$(sysctl -n hw.ncpu)

cpack -G TGZ

cd ..

echo ""
echo "==================================="
echo "Build complete!"
echo "Binary: build-macos/bin/stream_ticks"
echo "Package: build-macos/*.tar.gz"
echo "==================================="

file build-macos/bin/stream_ticks
echo ""
echo "Dependencies:"
otool -L build-macos/bin/stream_ticks