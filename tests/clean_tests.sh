#!/bin/bash

# Builds all test plugins.

set -e

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

for dir in "$SCRIPT_DIR"/*/; do
    [ -f "${dir}Makefile" ] || continue

    echo "==> Building $(basename "$dir")"
    make -C "$dir" clean
done