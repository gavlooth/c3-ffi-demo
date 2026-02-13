# Pika Lisp — Claude Code Instructions

## Build & Test
- Build: `c3c build`
- Run tests: `./build/main`
- All tests must pass before committing

## Conventions
- Single-param lambdas with currying (multi-param desugars to nested lambdas)
- `_` is a wildcard token (T_UNDERSCORE), NOT a symbol — never use as lambda param name
- C3 slices are INCLUSIVE: `buffer[0..n]` = n+1 elements, use `buffer[:n]` for n elements
- Stdlib functions defined in `register_stdlib()` as Pika code via `run()`
- Tests go in `run_advanced_tests()` or appropriate test function in eval.c3

## Audit Mode
When auditing for naive implementations or production readiness:
- **DO NOT** create pull requests or make fixes automatically
- **ONLY** report findings as a prioritized list with file:line references
- Let the human decide what to fix and when

## Post-Implementation (MANDATORY)
After every implementation session, ALWAYS do all three:
1. **Update tasks**: Mark completed tasks as done, create new ones for follow-up work
2. **Update `memory/CHANGELOG.md`**: Date, summary of changes, files modified, test count before/after
3. **Update `memory/MEMORY.md`**: Reflect new test counts, architecture changes, new features, and updated TODOs
