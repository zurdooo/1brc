#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_DIR="$ROOT_DIR/cpp"
EXPECTED_FILE="$ROOT_DIR/out_expected.txt"
ACTUAL_FILE="$ROOT_DIR/out_cpp.txt"

if [[ ! -f "$EXPECTED_FILE" ]]; then
  echo "Expected output not found. Generating via calculate_average_baseline.sh..."
  "$ROOT_DIR/calculate_average_baseline.sh" > "$EXPECTED_FILE"
fi

echo "Building C++ implementation..."
(
  cd "$CPP_DIR"
  g++ -std=c++23 1brc.cpp -o 1brc
)

echo "Running C++ implementation and capturing output..."
(
  cd "$CPP_DIR"
  # Capture only the final results line (e.g. "{Abha=...}") and ignore timing logs.
  ./1brc | sed -n '/^{.*}$/p'
) > "$ACTUAL_FILE"

echo "Comparing outputs..."
diff --color=always <("$ROOT_DIR/tocsv.sh" < "$EXPECTED_FILE") <("$ROOT_DIR/tocsv.sh" < "$ACTUAL_FILE")

echo "✅ Outputs match."
