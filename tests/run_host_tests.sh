#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
test_binary=$(mktemp /tmp/ledworm-tests.XXXXXX)
trap 'rm -f "$test_binary"' EXIT
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -fsanitize=address,undefined -fno-omit-frame-pointer -g \
    -Itests/host_stubs -Isrc tests/multiplayer_test.cpp \
    src/ledworm.cpp src/player_manager.cpp src/matrix_renderer.cpp \
    -o "$test_binary"
"$test_binary"
