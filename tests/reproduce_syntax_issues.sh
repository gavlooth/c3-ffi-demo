#!/bin/bash

COMPILER="./csrc/omnilisp"

echo "=== Syntax Reproduction Test ==="
echo "Compiler: $COMPILER"
echo "Date: $(date)"
echo "--------------------------------"

# Function to test a code snippet
test_syntax() {
    local name="$1"
    local code="$2"
    local file="tests/repro_${name}.omni"

    echo "$code" > "$file"
    echo "Testing: $name"
    echo "Code: $code"
    
    # Run compiler in check/compile mode only
    # We expect failures, so we capture output and exit code
    OUT=$($COMPILER -c "$file" 2>&1)
    RET=$?

    if [ $RET -ne 0 ]; then
        echo "❌ FAILED (Expected failure for unimplemented syntax)"
        echo "Output: $OUT"
    else
        echo "✅ SUCCESS (Unexpectedly passed?)"
        echo "Output: $OUT"
    fi
    echo "--------------------------------"
}

# 1. Dictionary Literals
test_syntax "dict_literal" "(define d #{:a 1})"

# 2. Signed Integers
test_syntax "signed_int" "(define x +123)"

# 3. Partial Float (.5)
test_syntax "partial_float_leading_dot" "(define f .5)"

# 4. Partial Float (3.)
test_syntax "partial_float_trailing_dot" "(define f 3.)"

# Clean up
rm tests/repro_*.omni

echo "Done."
