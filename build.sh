#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"

BUILD_TYPE="Release"
RUN_TESTS=false
CLEAN_BUILD=false

for arg in "$@"; do
    case $arg in
        --debug)
            BUILD_TYPE="Debug"
            ;;
        --test)
            RUN_TESTS=true
            ;;
        --clean)
            CLEAN_BUILD=true
            ;;
        --help)
            echo "Usage: $0 [--debug] [--test] [--clean]"
            exit 0
            ;;
    esac
done

if [ "$CLEAN_BUILD" = true ]; then
    echo "Cleaning build directory..."
    rm -rf build
fi

echo "Building in $BUILD_TYPE mode..."

cmake -G Ninja -B build \
      -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

ninja -C build

if [ "$RUN_TESTS" = true ]; then
    echo "Running Tests..."
    cd build
    ctest --output-on-failure --verbose
fi