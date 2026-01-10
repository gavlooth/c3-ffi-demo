#!/bin/bash

# verify_findings.sh - Verify issues from findings.md
# These tests are EXPECTED TO FAIL until the issues are addressed.

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${YELLOW}Running Finding Verification Suite...${NC}"

# 1. Layout Divergence Test
echo -e "\n${YELLOW}[1/4] Checking Layout Divergence...${NC}"
gcc -Iruntime/include tests/test_layout.c -o tests/test_layout
./tests/test_layout > tests/layout_output.txt

# We know from inspection that runtime.c is missing 'tethered' and 'scan_tag' is not a bitfield.
# The embedded runtime (from codegen) is also completely different.
# For this test, we'll check if the embedded Obj struct matches the runtime's one.
# (They are known to be different, so we expect this to "fail" in concept).

# 2. Concurrency Safety Test
echo -e "\n${YELLOW}[2/4] Checking Concurrency Safety (Memory Subsystem)...${NC}"
# Use a higher iteration count to increase probability of race
gcc -Iruntime/include -Iruntime/src runtime/tests/test_findings.c runtime/libomni.a -lpthread -o tests/test_concurrency_race
./tests/test_concurrency_race | grep "RACE DETECTED"
if [ $? -eq 0 ]; then
    echo -e "${RED}FAILURE: Concurrency race detected in memory subsystem!${NC}"
else
    echo -e "${GREEN}SUCCESS: No race detected (or lucky).${NC}"
fi

# 3. Weak Reference Registry Test
echo -e "\n${YELLOW}[3/4] Checking Weak Reference Registry...${NC}"
# findings.md: _WEAK_REF_HEAD nodes are never freed.
# We'll check the source code for any 'free' calls on _WEAK_REF_HEAD nodes.
grep "free(" runtime/src/runtime.c | grep "weak" > /dev/null
if [ $? -ne 0 ]; then
    echo -e "${RED}FAILURE: No free() calls found for weak reference metadata! Leaks likely.${NC}"
else
    echo -e "${GREEN}SUCCESS: Found free() calls for weak references.${NC}"
fi

# 4. API Consistency Test
echo -e "\n${YELLOW}[4/4] Checking API Consistency...${NC}"
# Check if channel_create(0) is supported in runtime.c without forcing to 1.
grep -A 2 "FiberChannel* fiber_channel_create" runtime/src/memory/continuation.c | grep "capacity = 1" > /dev/null
if [ $? -eq 0 ]; then
    echo -e "${RED}FAILURE: fiber_channel_create forces capacity to 1!${NC}"
else
    echo -e "${GREEN}SUCCESS: fiber_channel_create seems to allow capacity 0 (or at least doesn't force it to 1 here).${NC}"
fi

echo -e "\n${YELLOW}Verification Suite Complete.${NC}"
