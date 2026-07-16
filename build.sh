#!/usr/bin/env bash
set -e

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_type=${1:-debug}

case "$build_type" in
  debug|Debug)
    cmake_build_type=Debug
    ;;
  release|Release)
    cmake_build_type=Release
    ;;
  *)
    echo "usage: ./build.sh [debug|release]" >&2
    exit 1
    ;;
esac

cmake \
  -S "$repo_root" \
  -B "$repo_root/build" \
  -DBUILD_TESTING=OFF \
  -DCMAKE_BUILD_TYPE="$cmake_build_type"

cmake --build "$repo_root/build"
