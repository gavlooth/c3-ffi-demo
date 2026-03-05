#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

policy_file="${OMNI_BOUNDARY_FACADE_POLICY_FILE:-scripts/boundary_facade_policy.txt}"
out_file="${1:-docs/BOUNDARY_SURFACE_AUDIT.md}"
strict="${OMNI_BOUNDARY_AUDIT_STRICT:-0}"

declare -a ignore_globs=()
declare -A allowed_pairs=()
declare -a symbols=(
  copy_to_parent
  promote_to_escape
  promote_to_root
  copy_env_to_scope_inner
  scope_splice_escapes
)

trim() {
  local s="$1"
  s="${s#"${s%%[![:space:]]*}"}"
  s="${s%"${s##*[![:space:]]}"}"
  printf '%s' "$s"
}

load_policy() {
  if [[ ! -f "$policy_file" ]]; then
    echo "FAIL: missing policy file: $policy_file"
    exit 1
  fi
  while IFS= read -r raw || [[ -n "$raw" ]]; do
    local line
    line="$(trim "${raw%%#*}")"
    [[ -z "$line" ]] && continue
    local kind a b extra
    read -r kind a b extra <<< "$line"
    case "$kind" in
      ignore_glob)
        [[ -n "${a:-}" && -z "${b:-}" ]] || { echo "FAIL: invalid ignore_glob line: $raw"; exit 1; }
        ignore_globs+=("$a")
        ;;
      allow)
        [[ -n "${a:-}" && -n "${b:-}" && -z "${extra:-}" ]] || { echo "FAIL: invalid allow line: $raw"; exit 1; }
        allowed_pairs["$a:$b"]=1
        ;;
      *)
        echo "FAIL: unknown policy directive '$kind' in $policy_file"
        exit 1
        ;;
    esac
  done < "$policy_file"
}

is_ignored_file() {
  local file="$1"
  local glob
  for glob in "${ignore_globs[@]}"; do
    case "$file" in
      $glob) return 0 ;;
    esac
  done
  return 1
}

emit_report() {
  local timestamp="$1"
  local total="$2"
  local ignored="$3"
  local allowed="$4"
  local violations="$5"
  local allowed_lines="$6"
  local ignored_lines="$7"
  local violation_lines="$8"

  cat > "$out_file" <<EOF
# Boundary Surface Audit

Generated: ${timestamp}
Policy file: \`${policy_file}\`

## Summary

- total direct legacy callsites scanned: ${total}
- allowed by policy: ${allowed}
- ignored by policy globs: ${ignored}
- violations: ${violations}

## Allowed Callsites

\`\`\`text
${allowed_lines}
\`\`\`

## Ignored Callsites

\`\`\`text
${ignored_lines}
\`\`\`

## Violations

\`\`\`text
${violation_lines}
\`\`\`
EOF
}

main() {
  load_policy

  local total=0
  local ignored=0
  local allowed=0
  local violations=0
  local -a allowed_lines_arr=()
  local -a ignored_lines_arr=()
  local -a violation_lines_arr=()

  local sym
  for sym in "${symbols[@]}"; do
    local pattern="\\b${sym}\\("
    while IFS= read -r line; do
      [[ -z "$line" ]] && continue
      total=$((total + 1))
      local file="${line%%:*}"

      if is_ignored_file "$file"; then
        ignored=$((ignored + 1))
        ignored_lines_arr+=("$line")
        continue
      fi

      if [[ -n "${allowed_pairs["$sym:$file"]:-}" ]]; then
        allowed=$((allowed + 1))
        allowed_lines_arr+=("$line")
      else
        violations=$((violations + 1))
        violation_lines_arr+=("$line")
      fi
    done < <(rg -n --no-heading "$pattern" src/lisp || true)
  done

  local now
  now="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"

  local allowed_blob ignored_blob violations_blob
  allowed_blob="$(printf '%s\n' "${allowed_lines_arr[@]:-}")"
  ignored_blob="$(printf '%s\n' "${ignored_lines_arr[@]:-}")"
  violations_blob="$(printf '%s\n' "${violation_lines_arr[@]:-}")"

  emit_report "$now" "$total" "$ignored" "$allowed" "$violations" \
    "${allowed_blob:-<none>}" "${ignored_blob:-<none>}" "${violations_blob:-<none>}"

  echo "Wrote boundary surface audit: $out_file"
  echo "  total=$total allowed=$allowed ignored=$ignored violations=$violations"

  if ((violations > 0)); then
    if [[ "$strict" == "1" ]]; then
      echo "FAIL: boundary surface violations detected."
      exit 1
    fi
    echo "WARN: boundary surface violations detected."
  fi
}

main "$@"
