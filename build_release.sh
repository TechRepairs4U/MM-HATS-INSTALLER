#!/bin/sh

# Get version from CMakeLists.txt
VERSION=$(grep "set(HATS_TOOLS_VERSION" sphaira/CMakeLists.txt | sed 's/.*"\(.*\)".*/\1/')

rm -rf build
rm -rf build/release

# builds a preset
build_preset() {
    echo Configuring $1 ...
    cmake --preset $1 || { echo "=== CMake configure FAILED ==="; exit 1; }
    echo Building $1 ...
    cmake --build --preset $1 || { echo "=== Build FAILED ==="; exit 1; }
}

echo "=== Building MM HATS INSTALLER NRO ==="
build_preset Release

# Verify NRO was built
if [ ! -f "build/Release/mm-tools.nro" ]; then
    echo "=== ERROR: mm-tools.nro not found! Build may have failed. ==="
    exit 1
fi
echo "=== mm-tools.nro built successfully ==="

rm -rf out

# --- PAYLOAD --- #
echo "=== Building HATS-Installer Payload ==="
(cd payload && make clean && make) || { echo "=== Payload build FAILED ==="; exit 1; }

# Verify payload was built
if [ ! -f "payload/output/hats-installer.bin" ]; then
    echo "=== ERROR: hats-installer.bin not found! Payload build may have failed. ==="
    exit 1
fi
echo "=== hats-installer.bin built successfully ==="

# --- PACKAGE --- #
mkdir -p out/switch/mm-tools/
mkdir -p out/config/mm-tools/

cp build/Release/mm-tools.nro out/switch/mm-tools/mm-tools.nro
cp payload/output/hats-installer.bin out/switch/mm-tools/hats-installer.bin
cp assets/romfs/hekate_ipl_mod.ini out/config/mm-tools/hekate_ipl_mod.ini
cp config.ini out/config/mm-tools/config.ini
cp releases.json out/config/mm-tools/releases.json

pushd out
zip -r9 MM-HATS-INSTALLER-$VERSION.zip switch config
popd

echo "=== Release built: out/MM-HATS-INSTALLER-$VERSION.zip ==="
