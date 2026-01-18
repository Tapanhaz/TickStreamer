
set -e

TARGET_ARCH=${1:-x86_64}  # OR arm64
BUILD_TYPE=${2:-Release}

echo "Building for: $TARGET_ARCH ($BUILD_TYPE)"

export CC=clang
export CXX=clang++

source "$(dirname "$0")/versions.env"

if [ ! -f /usr/bin/llvm-ar ]; then
    LLVM_AR=$(ls /usr/bin/llvm-ar-* 2>/dev/null | head -1)
    if [ -n "$LLVM_AR" ]; then
        echo "Creating llvm-ar symlink..."
        sudo ln -sf "$LLVM_AR" /usr/bin/llvm-ar
    fi
fi


if [ "$TARGET_ARCH" = "arm64" ]; then
    export TARGET=aarch64-linux-musl
    CLANG_TARGET="clang --target=aarch64-linux-musl"
    CMAKE_TARGET="-DCMAKE_C_COMPILER_TARGET=aarch64-linux-musl -DCMAKE_CXX_COMPILER_TARGET=aarch64-linux-musl"
    MARCH="-march=armv8-a"
    BUILD_SUFFIX="arm"
else
    export TARGET=x86_64-linux-musl
    CLANG_TARGET="clang --target=x86_64-linux-musl"
    CMAKE_TARGET="-DCMAKE_C_COMPILER_TARGET=x86_64-linux-musl -DCMAKE_CXX_COMPILER_TARGET=x86_64-linux-musl"
    MARCH="-march=native"
    BUILD_SUFFIX="x86"
fi

echo "Building zstd for $TARGET_ARCH..."
cd external/zstd/lib
make clean || true
make -j$(nproc) \
    CC="$CLANG_TARGET" \
    AR=llvm-ar \
    LD=ld.lld \
    libzstd.a

if [ ! -f libzstd.a ]; then
    echo "ERROR: zstd build failed!"
    exit 1
fi
cd ../../..

BOOST_VERSION_U="${BOOST_VERSION//./_}"
BOOST_DIR="external/boost_${BOOST_VERSION_U}"

if [ ! -d "$BOOST_DIR" ]; then
    echo "Downloading Boost headers $BOOST_VERSION..."
    wget -q "https://archives.boost.io/release/${BOOST_VERSION}/source/boost_${BOOST_VERSION_U}.tar.gz"
    tar -xzf "boost_${BOOST_VERSION_U}.tar.gz" -C external/
    rm "boost_${BOOST_VERSION_U}.tar.gz"
fi

echo "Building simdjson for $TARGET_ARCH..."
cd external/simdjson
rm -rf build-$BUILD_SUFFIX
mkdir -p build-$BUILD_SUFFIX
cd build-$BUILD_SUFFIX
cmake .. \
    -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_LINKER=ld.lld \
    -DSIMDJSON_JUST_LIBRARY=ON \
    $CMAKE_TARGET
make -j$(nproc)

if [ ! -f libsimdjson.a ]; then
    echo "ERROR: simdjson build failed!"
    exit 1
fi
cd ../../..

echo "Building stream_ticks for $TARGET_ARCH..."
rm -rf build-$BUILD_SUFFIX
mkdir -p build-$BUILD_SUFFIX
cd build-$BUILD_SUFFIX

cmake .. \
    -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
    -DBUILD_STATIC=ON \
    -DUSE_MUSL=ON \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_LINKER=ld.lld \
    $CMAKE_TARGET

cmake --build . -j$(nproc)

cpack -G TGZ

cd ..

echo ""
echo "==================================="
echo "Build complete!"
echo "Binary: build-$BUILD_SUFFIX/bin/stream_ticks"
echo "Package: build-$BUILD_SUFFIX/*.tar.gz"
echo "==================================="

file build-$BUILD_SUFFIX/bin/stream_ticks
echo ""
echo "Dependencies:"
ldd build-$BUILD_SUFFIX/bin/stream_ticks 2>&1 || echo "Not applicable."