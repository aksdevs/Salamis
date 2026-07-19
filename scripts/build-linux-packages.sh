#!/usr/bin/env sh
# SPDX-License-Identifier: GPL-3.0-only

set -eu

build_dir="${1:-build-linux-package}"

cmake -S . -B "$build_dir" -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_dir" --config Release
ctest --test-dir "$build_dir" --build-config Release --output-on-failure

cmake --build "$build_dir" --target package --config Release

printf '\nCreated packages:\n'
find "$build_dir" -maxdepth 1 \( -name '*.deb' -o -name '*.rpm' -o -name '*.tar.gz' \) -print
