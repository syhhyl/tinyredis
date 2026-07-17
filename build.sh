#!/usr/bin/env bash
set -e

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_type=${1:-Release}

cmake \
  -S "$repo_root" \
  -B "$repo_root/build" \
  -DBUILD_TESTING=OFF \
  -DCMAKE_BUILD_TYPE="$build_type"

cmake --build "$repo_root/build"
