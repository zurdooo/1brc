#!/bin/bash

set -euo pipefail

# -------- CONFIG --------
APP_NAME="1brc"
CPP_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$CPP_DIR/build"
OUTPUT_DIR="$CPP_DIR"
SAMPLE_FILE="$CPP_DIR/sample.output"
SVG_FILE="$OUTPUT_DIR/sample.svg"
PERF_DATA_FILE="$CPP_DIR/perf.data"

OS_NAME="$(uname -s)"

ensure_flamegraph_dir() {
	local candidate

	for candidate in "$CPP_DIR/../FlameGraph" "$CPP_DIR/FlameGraph"; do
		if [[ -d "$candidate" ]]; then
			echo "$candidate"
			return
		fi
	done

	if ! command -v git >/dev/null 2>&1; then
		echo "Error: FlameGraph directory not found and git is unavailable to clone it." >&2
		echo "Please install git and clone https://github.com/brendangregg/FlameGraph" >&2
		exit 1
	fi

	candidate="$CPP_DIR/FlameGraph"
	echo "FlameGraph not found. Cloning into: $candidate"
	git clone https://github.com/brendangregg/FlameGraph.git "$candidate"
	echo "$candidate"
}

FLAMEGRAPH_DIR="$(ensure_flamegraph_dir)"

open_svg() {
	if command -v xdg-open >/dev/null 2>&1; then
		xdg-open "$SVG_FILE" >/dev/null 2>&1 || true
		return
	fi

	if command -v open >/dev/null 2>&1; then
		open "$SVG_FILE" >/dev/null 2>&1 || true
		return
	fi

	echo "FlameGraph generated at: $SVG_FILE"
}

# -------- STEP 1: Build with CMake + Conan --------
cd "$CPP_DIR" || exit 1

# Install Conan dependencies and generate CMakePresets toolchain
# cmake_layout puts generators in build/Release/generators/ automatically;
# do NOT pass --output-folder here or it will double-nest.
conan install . --build=missing -s build_type=Release

# Configure and build using the preset Conan generated (CMakeUserPresets.json)
cmake --preset conan-release
cmake --build --preset conan-release --parallel

# -------- STEP 2: Profile program and generate FlameGraph --------
cd "$FLAMEGRAPH_DIR" || exit 1

if [[ "$OS_NAME" == "Darwin" ]]; then
	if ! command -v sample >/dev/null 2>&1; then
		echo "Error: 'sample' command not found. This script expects macOS developer tools." >&2
		exit 1
	fi

	sample "$APP_NAME" -wait -f "$SAMPLE_FILE" &
	SAMPLE_PID=$!

	"$BUILD_DIR/Release/$APP_NAME"
	wait "$SAMPLE_PID"

	cat "$SAMPLE_FILE" | ./stackcollapse-sample.awk | ./flamegraph.pl > "$SVG_FILE"
else
	if ! command -v perf >/dev/null 2>&1; then
		echo "Error: 'perf' command not found. Install linux perf tools first." >&2
		exit 1
	fi

	perf record -F 99 -g -o "$PERF_DATA_FILE" -- "$BUILD_DIR/Release/$APP_NAME"
	perf script -i "$PERF_DATA_FILE" | ./stackcollapse-perf.pl | ./flamegraph.pl > "$SVG_FILE"
fi

open_svg

echo "FlameGraph generated at: $SVG_FILE"