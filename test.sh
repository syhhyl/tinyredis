#!/usr/bin/env bash
set -euo pipefail

TINYREDIS_BUILD_TESTS=1 cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
