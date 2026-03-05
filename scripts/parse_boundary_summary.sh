#!/usr/bin/env bash
set -euo pipefail

normal_log="${1:-build/boundary_hardening_normal.log}"
asan_log="${2:-build/boundary_hardening_asan.log}"
out_json="${3:-build/boundary_hardening_summary.json}"

extract_summary_line() {
  local log_file="$1"
  local suite="$2"
  grep -E "OMNI_TEST_SUMMARY suite=${suite}( |$)" "$log_file" | tail -n 1 || true
}

extract_summary_field_from_line() {
  local line="$1"
  local key="$2"
  echo "$line" | tr ' ' '\n' | awk -F= -v k="$key" '$1 == k { print $2; exit }'
}

extract_summary_field() {
  local log_file="$1"
  local suite="$2"
  local key="$3"
  local line
  line="$(extract_summary_line "$log_file" "$suite")"
  if [[ -z "$line" ]]; then
    return 0
  fi
  extract_summary_field_from_line "$line" "$key"
}

json_num_or_null() {
  local val="$1"
  if [[ -n "$val" ]]; then
    printf "%s" "$val"
  else
    printf "null"
  fi
}

json_bool_from_presence() {
  local val="$1"
  if [[ -n "$val" ]]; then
    printf "true"
  else
    printf "false"
  fi
}

emit_stage_json() {
  local log_file="$1"

  local stack_engine_line
  local scope_region_line
  local unified_line
  local compiler_line
  local harness_line
  local fiber_line

  stack_engine_line="$(extract_summary_line "$log_file" "stack_engine")"
  scope_region_line="$(extract_summary_line "$log_file" "scope_region")"
  unified_line="$(extract_summary_line "$log_file" "unified")"
  compiler_line="$(extract_summary_line "$log_file" "compiler")"
  harness_line="$(extract_summary_line "$log_file" "stack_affinity_harness")"
  fiber_line="$(extract_summary_line "$log_file" "fiber_temp_pool")"

  local se_pass se_fail sr_pass sr_fail un_pass un_fail cc_pass cc_fail
  local ah_pass ah_fail
  local ft_enabled ft_hits ft_misses ft_returns ft_drops ft_pooled ft_peak
  local ft_ctx_hits ft_ctx_returns ft_ctx_pools
  local ft_lc_clone ft_lc_destroy ft_lc_defer ft_lc_flush
  local ft_eligible_slow ft_bypass_large ft_bypass_escape

  se_pass="$(extract_summary_field_from_line "$stack_engine_line" "pass")"
  se_fail="$(extract_summary_field_from_line "$stack_engine_line" "fail")"
  sr_pass="$(extract_summary_field_from_line "$scope_region_line" "pass")"
  sr_fail="$(extract_summary_field_from_line "$scope_region_line" "fail")"
  un_pass="$(extract_summary_field_from_line "$unified_line" "pass")"
  un_fail="$(extract_summary_field_from_line "$unified_line" "fail")"
  cc_pass="$(extract_summary_field_from_line "$compiler_line" "pass")"
  cc_fail="$(extract_summary_field_from_line "$compiler_line" "fail")"
  ah_pass="$(extract_summary_field_from_line "$harness_line" "pass")"
  ah_fail="$(extract_summary_field_from_line "$harness_line" "fail")"

  ft_enabled="$(extract_summary_field_from_line "$fiber_line" "enabled")"
  ft_hits="$(extract_summary_field_from_line "$fiber_line" "hits")"
  ft_misses="$(extract_summary_field_from_line "$fiber_line" "misses")"
  ft_returns="$(extract_summary_field_from_line "$fiber_line" "returns")"
  ft_drops="$(extract_summary_field_from_line "$fiber_line" "drop_frees")"
  ft_pooled="$(extract_summary_field_from_line "$fiber_line" "pooled")"
  ft_peak="$(extract_summary_field_from_line "$fiber_line" "peak")"
  ft_ctx_hits="$(extract_summary_field_from_line "$fiber_line" "ctx_hits")"
  ft_ctx_returns="$(extract_summary_field_from_line "$fiber_line" "ctx_returns")"
  ft_ctx_pools="$(extract_summary_field_from_line "$fiber_line" "ctx_pools")"
  ft_lc_clone="$(extract_summary_field_from_line "$fiber_line" "lc_clone")"
  ft_lc_destroy="$(extract_summary_field_from_line "$fiber_line" "lc_destroy")"
  ft_lc_defer="$(extract_summary_field_from_line "$fiber_line" "lc_defer")"
  ft_lc_flush="$(extract_summary_field_from_line "$fiber_line" "lc_flush")"
  ft_eligible_slow="$(extract_summary_field_from_line "$fiber_line" "eligible_slow")"
  ft_bypass_large="$(extract_summary_field_from_line "$fiber_line" "bypass_large")"
  ft_bypass_escape="$(extract_summary_field_from_line "$fiber_line" "bypass_escape")"

  cat <<EOF
{
  "stack_engine": {
    "present": $(json_bool_from_presence "$stack_engine_line"),
    "pass": $(json_num_or_null "$se_pass"),
    "fail": $(json_num_or_null "$se_fail")
  },
  "scope_region": {
    "present": $(json_bool_from_presence "$scope_region_line"),
    "pass": $(json_num_or_null "$sr_pass"),
    "fail": $(json_num_or_null "$sr_fail")
  },
  "unified": {
    "present": $(json_bool_from_presence "$unified_line"),
    "pass": $(json_num_or_null "$un_pass"),
    "fail": $(json_num_or_null "$un_fail")
  },
  "compiler": {
    "present": $(json_bool_from_presence "$compiler_line"),
    "pass": $(json_num_or_null "$cc_pass"),
    "fail": $(json_num_or_null "$cc_fail")
  },
  "stack_affinity_harness": {
    "present": $(json_bool_from_presence "$harness_line"),
    "pass": $(json_num_or_null "$ah_pass"),
    "fail": $(json_num_or_null "$ah_fail")
  },
  "fiber_temp_pool": {
    "present": $(json_bool_from_presence "$fiber_line"),
    "enabled": $(json_num_or_null "$ft_enabled"),
    "hits": $(json_num_or_null "$ft_hits"),
    "misses": $(json_num_or_null "$ft_misses"),
    "returns": $(json_num_or_null "$ft_returns"),
    "drop_frees": $(json_num_or_null "$ft_drops"),
    "pooled": $(json_num_or_null "$ft_pooled"),
    "peak": $(json_num_or_null "$ft_peak"),
    "ctx_hits": $(json_num_or_null "$ft_ctx_hits"),
    "ctx_returns": $(json_num_or_null "$ft_ctx_returns"),
    "ctx_pools": $(json_num_or_null "$ft_ctx_pools"),
    "lc_clone": $(json_num_or_null "$ft_lc_clone"),
    "lc_destroy": $(json_num_or_null "$ft_lc_destroy"),
    "lc_defer": $(json_num_or_null "$ft_lc_defer"),
    "lc_flush": $(json_num_or_null "$ft_lc_flush"),
    "eligible_slow": $(json_num_or_null "$ft_eligible_slow"),
    "bypass_large": $(json_num_or_null "$ft_bypass_large"),
    "bypass_escape": $(json_num_or_null "$ft_bypass_escape")
  }
}
EOF
}

if [[ ! -f "$normal_log" ]]; then
  echo "missing normal log: $normal_log" >&2
  exit 1
fi
if [[ ! -f "$asan_log" ]]; then
  echo "missing asan log: $asan_log" >&2
  exit 1
fi

generated_at="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"

mkdir -p "$(dirname "$out_json")"
cat > "$out_json" <<EOF
{
  "generated_at_utc": "${generated_at}",
  "normal": $(emit_stage_json "$normal_log"),
  "asan": $(emit_stage_json "$asan_log")
}
EOF

echo "Wrote boundary summary: $out_json"
