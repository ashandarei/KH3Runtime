#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
build_dir=$(mktemp -d)
trap 'rm -rf -- "$build_dir"' EXIT
cxx=${CXX:-g++}
flags=(-std=c++20 -O1 -g -fsanitize=undefined -fno-sanitize-recover=undefined)
"$cxx" "${flags[@]}" tests/color_palette_test.cpp \
    KH3APProbeCpp/kh3ap/features/colors/color_palette.cpp \
    -o "$build_dir/palette"
"$build_dir/palette"
"$cxx" "${flags[@]}" -Itests/color_stubs -IKH3APProbeCpp/kh3ap/core \
    tests/color_apply_state_test.cpp \
    KH3APProbeCpp/kh3ap/features/colors/color_apply_state.cpp \
    -o "$build_dir/state"
"$build_dir/state"
"$cxx" "${flags[@]}" tests/color_timing_test.cpp -o "$build_dir/timing"
"$build_dir/timing"
