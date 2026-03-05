#!/usr/bin/env bash
set -euo pipefail

normal_log="${1:-build/boundary_hardening_normal.log}"
asan_log="${2:-build/boundary_hardening_asan.log}"

extract_summary_line() {
  local log_file="$1"
  local suite="$2"
  grep -E "OMNI_TEST_SUMMARY suite=${suite}( |$)" "$log_file" | tail -n 1 || true
}

extract_field_from_line() {
  local line="$1"
  local key="$2"
  echo "$line" | tr ' ' '\n' | awk -F= -v k="$key" '$1 == k { print $2; exit }'
}

emit_stage_row() {
  local stage="$1"
  local log_file="$2"

  local se_line sr_line un_line cc_line ah_line ft_line
  se_line="$(extract_summary_line "$log_file" "stack_engine")"
  sr_line="$(extract_summary_line "$log_file" "scope_region")"
  un_line="$(extract_summary_line "$log_file" "unified")"
  cc_line="$(extract_summary_line "$log_file" "compiler")"
  ah_line="$(extract_summary_line "$log_file" "stack_affinity_harness")"
  ft_line="$(extract_summary_line "$log_file" "fiber_temp_pool")"

  local se_fail sr_fail un_fail cc_fail ah_fail ft_enabled
  se_fail="$(extract_field_from_line "$se_line" "fail")"
  sr_fail="$(extract_field_from_line "$sr_line" "fail")"
  un_fail="$(extract_field_from_line "$un_line" "fail")"
  cc_fail="$(extract_field_from_line "$cc_line" "fail")"
  ah_fail="$(extract_field_from_line "$ah_line" "fail")"
  ft_enabled="$(extract_field_from_line "$ft_line" "enabled")"

  local status="PASS"
  if [[ "${se_fail:-x}" != "0" || "${sr_fail:-x}" != "0" || "${un_fail:-x}" != "0" || "${cc_fail:-x}" != "0" ]]; then
    status="FAIL"
  fi
  if [[ -n "$ah_line" && "${ah_fail:-x}" != "0" ]]; then
    status="FAIL"
  fi

  echo "| ${stage} | ${status} | ${se_fail:-?} | ${sr_fail:-?} | ${un_fail:-?} | ${cc_fail:-?} | ${ah_fail:--} | ${ft_enabled:--} |"
}

echo "## Boundary Hardening Summary"
echo ""
echo "| Stage | Status | stack_engine fail | scope_region fail | unified fail | compiler fail | harness fail | fiber_temp enabled |"
echo "|---|---:|---:|---:|---:|---:|---:|---:|"

if [[ -f "$normal_log" ]]; then
  emit_stage_row "normal" "$normal_log"
else
  echo "| normal | missing log | - | - | - | - | - | - |"
fi

if [[ -f "$asan_log" ]]; then
  emit_stage_row "asan" "$asan_log"
else
  echo "| asan | missing log | - | - | - | - | - | - |"
fi

echo ""
echo "### Artifacts"
echo ""
echo "- normal log: \`${normal_log}\`"
echo "- asan log: \`${asan_log}\`"
echo "- json summary: \`build/boundary_hardening_summary.json\`"
