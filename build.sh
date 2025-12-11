#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
rm -r ./build || echo "No folder ./build found, creating one ..."

cmake -G Ninja -B build \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

ninja -C build