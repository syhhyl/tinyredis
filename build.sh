set -e

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

cmake -S . -B build -DCMAKE_BUILD_TYPE="$cmake_build_type"
cmake --build build
