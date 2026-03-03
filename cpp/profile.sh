#!/bin/bash

# -------- CONFIG --------
APP_NAME="1brc"
CPP_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$CPP_DIR/build"
FLAMEGRAPH_DIR="$CPP_DIR/../FlameGraph"
OUTPUT_DIR="$CPP_DIR"
SAMPLE_FILE="$CPP_DIR/sample.output"
SVG_FILE="$OUTPUT_DIR/sample.svg"

# -------- STEP 1: Start sampling in background --------
cd "$FLAMEGRAPH_DIR" || exit 1
sample "$APP_NAME" -wait -f "$SAMPLE_FILE" &
SAMPLE_PID=$!

# -------- STEP 2: Build with CMake + Conan --------
cd "$CPP_DIR" || exit 1

# Install Conan dependencies and generate CMakePresets toolchain
# cmake_layout puts generators in build/Release/generators/ automatically;
# do NOT pass --output-folder here or it will double-nest.
conan install . --build=missing -s build_type=Release

# Configure and build using the preset Conan generated (CMakeUserPresets.json)
cmake --preset conan-release
cmake --build --preset conan-release --parallel

# -------- STEP 3: Run program --------
"$BUILD_DIR/Release/$APP_NAME"

# -------- STEP 4: Wait for sampling to finish --------
wait $SAMPLE_PID

# -------- STEP 5: Generate FlameGraph in C++ folder --------
cd "$FLAMEGRAPH_DIR" || exit 1
cat "$SAMPLE_FILE" | ./stackcollapse-sample.awk | ./flamegraph.pl > "$SVG_FILE"

# -------- STEP 6: Open in arc --------
open -a "Arc" "$SVG_FILE"

echo "FlameGraph generated at: $SVG_FILE"