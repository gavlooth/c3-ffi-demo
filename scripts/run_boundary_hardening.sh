#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

# Boundary-hardening profile defaults.
: "${OMNI_BOUNDARY_ENABLE_FIBER_TEMP:=1}"
: "${OMNI_BOUNDARY_ENABLE_AFFINITY_HARNESS:=1}"
: "${OMNI_BOUNDARY_QUIET:=1}"
: "${OMNI_BOUNDARY_SUMMARY:=1}"
: "${OMNI_BOUNDARY_ASSERT_SUMMARY:=1}"

normal_log="build/boundary_hardening_normal.log"
asan_log="build/boundary_hardening_asan.log"

extract_summary_line() {
  local log_file="$1"
  local suite="$2"
  grep -E "OMNI_TEST_SUMMARY suite=${suite}( |$)" "$log_file" | tail -n 1 || true
}

extract_summary_field() {
  local line="$1"
  local key="$2"
  echo "$line" | tr ' ' '\n' | awk -F= -v k="$key" '$1 == k { print $2; exit }'
}

assert_suite_fail_zero() {
  local log_file="$1"
  local suite="$2"
  local stage="$3"
  local line
  line="$(extract_summary_line "$log_file" "$suite")"
  if [[ -z "$line" ]]; then
    echo "FAIL: [$stage] missing OMNI_TEST_SUMMARY for suite=$suite"
    return 1
  fi
  local fail_val
  fail_val="$(extract_summary_field "$line" "fail")"
  if [[ -z "$fail_val" ]]; then
    echo "FAIL: [$stage] summary for suite=$suite has no fail= field"
    echo "  line: $line"
    return 1
  fi
  if [[ "$fail_val" != "0" ]]; then
    echo "FAIL: [$stage] suite=$suite reported fail=$fail_val"
    echo "  line: $line"
    return 1
  fi
  return 0
}

assert_fiber_temp_enabled() {
  local log_file="$1"
  local stage="$2"
  local line
  line="$(extract_summary_line "$log_file" "fiber_temp_pool")"
  if [[ -z "$line" ]]; then
    echo "FAIL: [$stage] missing OMNI_TEST_SUMMARY for suite=fiber_temp_pool"
    return 1
  fi
  local enabled
  enabled="$(extract_summary_field "$line" "enabled")"
  if [[ "$enabled" != "1" ]]; then
    echo "FAIL: [$stage] expected fiber_temp_pool enabled=1, got enabled=${enabled:-<missing>}"
    echo "  line: $line"
    return 1
  fi
  return 0
}

assert_stage_summary() {
  local log_file="$1"
  local stage="$2"

  assert_suite_fail_zero "$log_file" "stack_engine" "$stage"
  assert_suite_fail_zero "$log_file" "scope_region" "$stage"
  assert_suite_fail_zero "$log_file" "unified" "$stage"
  assert_suite_fail_zero "$log_file" "compiler" "$stage"

  if [[ "$OMNI_BOUNDARY_ENABLE_AFFINITY_HARNESS" == "1" ]]; then
    assert_suite_fail_zero "$log_file" "stack_affinity_harness" "$stage"
  fi

  if [[ "$OMNI_BOUNDARY_ENABLE_FIBER_TEMP" == "1" ]]; then
    assert_fiber_temp_enabled "$log_file" "$stage"
  fi
}

base_env=(
  "LD_LIBRARY_PATH=/usr/local/lib"
)

if [[ "$OMNI_BOUNDARY_ENABLE_FIBER_TEMP" == "1" ]]; then
  base_env+=("OMNI_FIBER_TEMP=1")
fi
if [[ "$OMNI_BOUNDARY_ENABLE_AFFINITY_HARNESS" == "1" ]]; then
  base_env+=("OMNI_STACK_AFFINITY_HARNESS=1")
fi
if [[ "$OMNI_BOUNDARY_QUIET" == "1" ]]; then
  base_env+=("OMNI_TEST_QUIET=1")
fi
if [[ "$OMNI_BOUNDARY_SUMMARY" == "1" ]]; then
  base_env+=("OMNI_TEST_SUMMARY=1")
fi

if [[ "$OMNI_BOUNDARY_ASSERT_SUMMARY" == "1" && "$OMNI_BOUNDARY_SUMMARY" != "1" ]]; then
  echo "FAIL: OMNI_BOUNDARY_ASSERT_SUMMARY=1 requires OMNI_BOUNDARY_SUMMARY=1"
  exit 1
fi

echo "=== Boundary Hardening: Stage 1 (normal build) ==="
c3c build

echo ""
echo "=== Boundary Hardening: Stage 2 (normal run) ==="
env "${base_env[@]}" ./build/main | tee "$normal_log"

echo ""
echo "=== Boundary Hardening: Stage 3 (ASAN build) ==="
c3c clean
c3c build --sanitize=address

echo ""
echo "=== Boundary Hardening: Stage 4 (ASAN run) ==="
env \
  "${base_env[@]}" \
  "ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:abort_on_error=1" \
  ./build/main | tee "$asan_log"

if [[ "$OMNI_BOUNDARY_ASSERT_SUMMARY" == "1" ]]; then
  echo ""
  echo "=== Boundary Hardening: Stage 5 (summary assertions) ==="
  assert_stage_summary "$normal_log" "normal"
  assert_stage_summary "$asan_log" "asan"
  echo "Summary assertions passed."
fi

echo ""
echo "Boundary hardening profile passed."
echo "  normal log: $normal_log"
echo "  asan log:   $asan_log"
