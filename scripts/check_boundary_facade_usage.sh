#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

is_ignored_file() {
  local file="$1"
  case "$file" in
    src/lisp/tests_*.c3) return 0 ;;
  esac
  return 1
}

is_allowed_callsite() {
  local symbol="$1"
  local file="$2"
  case "$symbol:$file" in
    copy_to_parent:src/lisp/eval_promotion_copy.c3) return 0 ;;
    copy_to_parent:src/lisp/eval_boundary_api.c3) return 0 ;;
    promote_to_escape:src/lisp/eval_promotion_escape.c3) return 0 ;;
    promote_to_escape:src/lisp/eval_boundary_api.c3) return 0 ;;
    promote_to_root:src/lisp/eval_promotion_escape.c3) return 0 ;;
    promote_to_root:src/lisp/eval_boundary_api.c3) return 0 ;;
    copy_env_to_scope_inner:src/lisp/eval_env_copy.c3) return 0 ;;
    scope_splice_escapes:src/lisp/eval_boundary_api.c3) return 0 ;;
  esac
  return 1
}

check_symbol_usage() {
  local symbol="$1"
  local -n violations_ref="$2"
  local pattern
  pattern="\\b${symbol}\\("
  while IFS= read -r line; do
    [[ -z "$line" ]] && continue
    local file="${line%%:*}"
    if is_ignored_file "$file"; then
      continue
    fi
    if is_allowed_callsite "$symbol" "$file"; then
      continue
    fi
    violations_ref+=("$line")
  done < <(rg -n --no-heading "$pattern" src/lisp || true)
}

main() {
  local -a legacy_symbols=(
    copy_to_parent
    promote_to_escape
    promote_to_root
    copy_env_to_scope_inner
    scope_splice_escapes
  )
  local -a violations=()

  for symbol in "${legacy_symbols[@]}"; do
    check_symbol_usage "$symbol" violations
  done

  if ((${#violations[@]} == 0)); then
    echo "OK: boundary facade guard found no disallowed legacy boundary callsites."
    return 0
  fi

  echo "FAIL: disallowed direct legacy boundary callsites detected."
  echo "Use boundary_* facade entry points instead."
  echo ""
  for v in "${violations[@]}"; do
    echo "  $v"
  done
  return 1
}

main "$@"
