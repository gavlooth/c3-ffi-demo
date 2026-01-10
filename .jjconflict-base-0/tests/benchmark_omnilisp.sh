#!/bin/bash
# benchmark_omnilisp.sh - Performance benchmark for OmniLisp

OMNILISP="./csrc/omnilisp"
ITERATIONS=100

echo "=========================================="
echo "OmniLisp Performance Benchmark"
echo "=========================================="
echo ""

# Test 1: Integer Addition
echo "Test 1: Integer Addition (+ 100 200)"
total=0
for i in $(seq 1 $ITERATIONS); do
  start=$(date +%s%N)
  result=$($OMNILISP -e '(+ 100 200)' 2>/dev/null)
  end=$(date +%s%N)
  elapsed=$((end - start))
  total=$((total + elapsed))
done
avg=$((total / ITERATIONS))
echo "  Result: $result"
echo "  Average time: ${avg}ns over $ITERATIONS iterations"
echo ""

# Test 2: Integer Multiplication
echo "Test 2: Integer Multiplication (* 123 456)"
total=0
for i in $(seq 1 $ITERATIONS); do
  start=$(date +%s%N)
  result=$($OMNILISP -e '(* 123 456)' 2>/dev/null)
  end=$(date +%s%N)
  elapsed=$((end - start))
  total=$((total + elapsed))
done
avg=$((total / ITERATIONS))
echo "  Result: $result"
echo "  Average time: ${avg}ns over $ITERATIONS iterations"
echo ""

# Test 3: Integer Subtraction
echo "Test 3: Integer Subtraction (- 500 250)"
total=0
for i in $(seq 1 $ITERATIONS); do
  start=$(date +%s%N)
  result=$($OMNILISP -e '(- 500 250)' 2>/dev/null)
  end=$(date +%s%N)
  elapsed=$((end - start))
  total=$((total + elapsed))
done
avg=$((total / ITERATIONS))
echo "  Result: $result"
echo "  Average time: ${avg}ns over $ITERATIONS iterations"
echo ""

# Test 4: Integer Division
echo "Test 4: Integer Division (/ 1000 10)"
total=0
for i in $(seq 1 $ITERATIONS); do
  start=$(date +%s%N)
  result=$($OMNILISP -e '(/ 1000 10)' 2>/dev/null)
  end=$(date +%s%N)
  elapsed=$((end - start))
  total=$((total + elapsed))
done
avg=$((total / ITERATIONS))
echo "  Result: $result"
echo "  Average time: ${avg}ns over $ITERATIONS iterations"
echo ""

# Test 5: Comparison Operations
echo "Test 5: Comparison (< 50 100)"
total=0
for i in $(seq 1 $ITERATIONS); do
  start=$(date +%s%N)
  result=$($OMNILISP -e '(< 50 100)' 2>/dev/null)
  end=$(date +%s%N)
  elapsed=$((end - start))
  total=$((total + elapsed))
done
avg=$((total / ITERATIONS))
echo "  Result: $result"
echo "  Average time: ${avg}ns over $ITERATIONS iterations"
echo ""

# Test 6: Complex Expression
echo "Test 6: Complex (+ (* 2 3) (* 4 5))"
total=0
for i in $(seq 1 $ITERATIONS); do
  start=$(date +%s%N)
  result=$($OMNILISP -e '(+ (* 2 3) (* 4 5))' 2>/dev/null)
  end=$(date +%s%N)
  elapsed=$((end - start))
  total=$((total + elapsed))
done
avg=$((total / ITERATIONS))
echo "  Result: $result"
echo "  Average time: ${avg}ns over $ITERATIONS iterations"
echo ""

echo "=========================================="
echo "Benchmark Complete"
echo "=========================================="
