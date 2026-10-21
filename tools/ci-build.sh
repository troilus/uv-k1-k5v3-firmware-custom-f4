#!/usr/bin/env bash
#
# Runs *inside* the build container (see compile-with-docker.sh):
#
#     bash tools/ci-build.sh <Preset> [CMake cache variables...]
#
# Configure the preset, then build it with ninja. The build is done with
# "ninja -C" rather than "cmake --build": cmake 3.25 in the container can
# return success from "cmake --build" without running anything, which leaves
# no image behind and only shows up later as a missing file. ninja always
# prints what it does, and the two checks at the end turn a silent no-op into
# a failed step.

set -euo pipefail

preset=${1:-Custom}
shift || true

echo "=== toolchain ==="
which arm-none-eabi-gcc
arm-none-eabi-gcc --version | head -1

echo "=== configure ${preset} ==="
cmake --preset "$preset" "$@" 2>&1 | tee /tmp/configure.log

build_dir=$(sed -n 's/^-- Build files have been written to: //p' /tmp/configure.log | tail -1)
build_dir=${build_dir:-build/${preset}}
echo "=== build ${build_dir} ==="
test -f "${build_dir}/build.ninja"

ninja -C "${build_dir}"
ls -l "${build_dir}"/*.bin
