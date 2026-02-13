#!/usr/bin/env bash
# Pika Lisp Test Runner - Build and run all tests

set -e

echo "Building..."
c3c build 2>&1 | grep -v "Warning:" | grep -v "Note:" || true

echo ""
echo "Running tests..."
./build/main 2>&1 | grep -E "^\[|^===|PASSED|ERROR|OK"

echo ""
echo "Done."
