#!/bin/bash

# -------- CONFIG --------
APP_NAME="1brc"
CPP_DIR="$(pwd)"
FLAMEGRAPH_DIR="$CPP_DIR/../FlameGraph"
OUTPUT_DIR="$CPP_DIR"
SAMPLE_FILE="$CPP_DIR/sample.output"
SVG_FILE="$OUTPUT_DIR/sample.svg"

# -------- STEP 1: Start sampling in background --------
cd "$FLAMEGRAPH_DIR" || exit 1
sample "$APP_NAME" -wait -f "$SAMPLE_FILE" &
SAMPLE_PID=$!

# -------- STEP 2: Compile program --------
cd "$CPP_DIR" || exit 1
g++ -std=c++23 -g -O2 1brc.cpp -o "$APP_NAME"
``
# -------- STEP 3: Run program --------
./"$APP_NAME"

# -------- STEP 4: Wait for sampling to finish --------
wait $SAMPLE_PID

# -------- STEP 5: Generate FlameGraph in C++ folder --------
cd "$FLAMEGRAPH_DIR" || exit 1
cat "$SAMPLE_FILE" | ./stackcollapse-sample.awk | ./flamegraph.pl > "$SVG_FILE"

# -------- STEP 6: Open in arc --------
open -a "Arc" "$SVG_FILE"

echo "FlameGraph generated at: $SVG_FILE"