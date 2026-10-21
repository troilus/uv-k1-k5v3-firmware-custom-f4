#!/usr/bin/env bash
set -euo pipefail

# ---------------------------------------------
# Usage:
#   ./compile-with-docker.sh [Preset] [CMake options...]
# Examples:
#   ./compile-with-docker.sh Custom
#   ./compile-with-docker.sh Bandscope -DENABLE_SPECTRUM=ON
#   ./compile-with-docker.sh Broadcast -DENABLE_FEAT_F4HWN_GAME=ON -DENABLE_NOAA=ON
#   ./compile-with-docker.sh LBJRadio -DENABLE_CN_FONT=ON -DENABLE_LBJ_TX_LOCK=OFF
#   ./compile-with-docker.sh All
# Default preset: "Custom"
# ---------------------------------------------

IMAGE=uvk1-uvk5v3
PRESET=${1:-Custom}
shift || true  # remove preset from arguments if present

# Any remaining args will be treated as CMake cache variables
EXTRA_ARGS=("$@")

# ---------------------------------------------
# Validate preset name
# ---------------------------------------------
if [[ ! "$PRESET" =~ ^(Custom|Bandscope|Broadcast|Basic|RescueOps|Game|Fusion|LBJRadio|All)$ ]]; then
  echo "❌ Unknown preset: '$PRESET'"
  echo "Valid presets are: Custom, Bandscope, Broadcast, Basic, RescueOps, Game, Fusion, LBJRadio, All"
  exit 1
fi

# ---------------------------------------------
# Build the Docker image (only needed once)
# ---------------------------------------------
if [[ "$(docker images -q $IMAGE)" == "" ]]; then
  echo "Building Docker image..."
  docker build -t "$IMAGE" .
fi

# ---------------------------------------------
# Clean existing CMake cache to ensure toolchain reload
# ---------------------------------------------
rm -rf build
export MSYS_NO_PATHCONV=1
# ---------------------------------------------
# Function to build one preset
# ---------------------------------------------
build_preset() {
  local preset="$1"
  echo ""
  echo "=== 🚀 Building preset: ${preset} ==="
  echo "---------------------------------------------"
  # Configure, then build with ninja directly: "cmake --build" inside the
  # container's cmake 3.25 can return success without running ninja, which
  # leaves no image behind and only shows up much later as a missing file.
  # ninja -C always prints what it does and fails loudly.
  docker run --rm \
    -u $(id -u):$(id -g) \
    -v "$PWD":/src -w /src "$IMAGE" \
    bash -c "set -eox pipefail; \
             which arm-none-eabi-gcc && arm-none-eabi-gcc --version && \
             cmake --preset ${preset} ${EXTRA_ARGS[@]+"${EXTRA_ARGS[@]}"} | tee /tmp/configure.log && \
             build_dir=\$(sed -n 's/^-- Build files have been written to: //p' /tmp/configure.log | tail -1) && \
             echo \"build dir: \$build_dir\" && \
             test -f \"\$build_dir/build.ninja\" && \
             ninja -C \"\$build_dir\" && \
             ls -l \"\$build_dir\"/*.bin"
  echo "✅ Done: ${preset}"
}

# ---------------------------------------------
# Handle 'All' preset
# ---------------------------------------------
if [[ "$PRESET" == "All" ]]; then
  PRESETS=(Bandscope Broadcast Basic RescueOps Game Fusion)
  for p in "${PRESETS[@]}"; do
    build_preset "$p"
  done
  echo ""
  echo "🎉 All presets built successfully!"
else
  build_preset "$PRESET"
fi
