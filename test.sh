#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

cmake \
  -S "$repo_root" \
  -B "$repo_root/build" \
  -DBUILD_TESTING=ON \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build "$repo_root/build"
ctest --test-dir "$repo_root/build" --output-on-failure
