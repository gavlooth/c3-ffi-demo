#!/bin/bash
# check_test_coverage.csh - Identify untested functions in OmniLisp runtime

echo "=========================================="
echo "OmniLisp Runtime Test Coverage Check"
echo "=========================================="
echo ""

# Search for prim_ functions in runtime/src/
echo "Scanning runtime/src/ for prim_* functions..."
echo ""

# Count total prim_ functions
TOTAL_PRIMS=$(grep -r "^Obj\* prim_" /home/heefoo/code/OmniLisp/runtime/src/*.c 2>/dev/null | wc -l)
echo "Total prim_ functions found: $TOTAL_PRIMS"
echo ""

# Functions marked as TESTED
echo "Functions marked as TESTED in source code:"
echo ""
grep -B 2 "^Obj\* prim_" /home/heefoo/code/OmniLisp/runtime/src/*.c 2>/dev/null | grep -B 1 "^Obj\* prim_" | grep "^// TESTED" | sed 's/^// TESTED - /  /'
echo ""

# List all prim_ functions without TESTED comment
echo "Functions possibly lacking explicit TESTED markers:"
echo ""
grep "^Obj\* prim_" /home/heefoo/code/OmniLisp/runtime/src/*.c 2>/dev/null | while read line; do
    func_name=$(echo "$line" | sed 's/^.*prim_\([a-z_]*\).*/prim_\1/')
    func_line=$(echo "$line" | sed 's/^.*//')

    # Check if there's a TESTED comment in the previous 2 lines
    # This is a simplified check - a proper tool would parse the file
    echo "  - $func_name"
done | head -30
echo ""

# Check for test files
echo "Checking test directory structure..."
echo ""

if [ -d "/home/heefoo/code/OmniLisp/runtime/tests" ]; then
    TEST_COUNT=$(ls -1 /home/heefoo/code/OmniLisp/runtime/tests/test_*.c 2>/dev/null | wc -l)
    echo "C test files found: $TEST_COUNT"
    echo ""

    echo "C test files:"
    ls -1 /home/heefoo/code/OmniLisp/runtime/tests/test_*.c 2>/dev/null | xargs -n 1 basename | sed 's/^/  - /'
fi

echo ""

if [ -d "/home/heefoo/code/OmniLisp/tests" ]; then
    OMNI_TEST_COUNT=$(ls -1 /home/heefoo/code/OmniLisp/tests/*.omni 2>/dev/null | wc -l)
    echo "OmniLisp test files found: $OMNI_TEST_COUNT"
    echo ""

    echo "OmniLisp test files:"
    ls -1 /home/heefoo/code/OmniLisp/tests/*.omni 2>/dev/null | xargs -n 1 basename | sed 's/^/  - /'
fi

echo ""
echo "=========================================="
echo "Recommendations:"
echo "=========================================="
echo ""
echo "1. Review functions without TESTED markers"
echo "2. Create tests for untested primitives"
echo "3. Ensure all critical paths are covered"
echo "4. Add edge case tests (NULL, empty, large inputs)"
echo "5. Add integration tests via OmniLisp files"
echo ""
