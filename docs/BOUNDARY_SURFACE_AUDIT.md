# Boundary Surface Audit

Generated: 2026-03-05T19:39:52Z
Policy file: `scripts/boundary_facade_policy.txt`

## Summary

- total direct legacy callsites scanned: 28
- allowed by policy: 23
- ignored by policy globs: 5
- violations: 0

## Allowed Callsites

```text
src/lisp/eval_boundary_api.c3:85:    return copy_to_parent(v, interp);
src/lisp/eval_promotion_copy.c3:26:        current_new.cons_val.car = copy_to_parent(current_old.cons_val.car, interp);
src/lisp/eval_promotion_copy.c3:41:            current_new.cons_val.cdr = copy_to_parent(old_cdr, interp);
src/lisp/eval_promotion_copy.c3:134:    result.partial_val.first_arg = copy_to_parent(v.partial_val.first_arg, interp);
src/lisp/eval_promotion_copy.c3:135:    result.partial_val.second_arg = copy_to_parent(v.partial_val.second_arg, interp);
src/lisp/eval_promotion_copy.c3:147:    Value* promoted_thunk = copy_to_parent(v.iterator_val, interp);
src/lisp/eval_promotion_copy.c3:161:fn Value* copy_to_parent(Value* v, Interp* interp) {
src/lisp/eval_promotion_copy.c3:226:    return copy_to_parent(v, interp);
src/lisp/eval_boundary_api.c3:242:    return promote_to_escape(v, interp);
src/lisp/eval_promotion_escape.c3:21:    return promote_to_escape(copied, interp);
src/lisp/eval_promotion_escape.c3:50:        current_new.cons_val.car = promote_to_escape(current_old.cons_val.car, interp);
src/lisp/eval_promotion_escape.c3:62:            current_new.cons_val.cdr = promote_to_escape(old_cdr, interp);
src/lisp/eval_promotion_escape.c3:105:    result.partial_val.first_arg = promote_to_escape(v.partial_val.first_arg, interp);
src/lisp/eval_promotion_escape.c3:106:    result.partial_val.second_arg = promote_to_escape(v.partial_val.second_arg, interp);
src/lisp/eval_promotion_escape.c3:190:    result.iterator_val = promote_to_escape(v.iterator_val, interp);
src/lisp/eval_promotion_escape.c3:305:fn Value* promote_to_escape(Value* v, Interp* interp) {
src/lisp/eval_boundary_api.c3:256:    return promote_to_root(v, interp);
src/lisp/eval_promotion_escape.c3:336:fn Value* promote_to_root(Value* v, Interp* interp) {
src/lisp/eval_env_copy.c3:145:    env.parent = copy_env_to_scope_inner(env.parent, interp, depth + 1, active_ctx);
src/lisp/eval_env_copy.c3:152:    new_env.parent = copy_env_to_scope_inner(env.parent, interp, depth + 1, active_ctx);
src/lisp/eval_env_copy.c3:160:fn Env* copy_env_to_scope_inner(Env* env, Interp* interp, usz depth, PromotionContext* active_ctx) {
src/lisp/eval_env_copy.c3:194:    Env* copied = copy_env_to_scope_inner(env, interp, depth, active_ctx);
src/lisp/eval_boundary_api.c3:304:    main::scope_splice_escapes(parent, child);
```

## Ignored Callsites

```text
src/lisp/tests_tests.c3:4593:        Value* reused = copy_to_parent(reusable, interp);
src/lisp/tests_tests.c3:4594:        Value* copied = copy_to_parent(disjoint, interp);
src/lisp/tests_tests.c3:4121:        Value* esc_list = promote_to_escape(tmp_list, interp);
src/lisp/tests_tests.c3:4319:        Value* escaped = promote_to_escape(src, interp);
src/lisp/tests_tests.c3:5210:        Value* promoted = promote_to_escape(src, interp);
```

## Violations

```text
<none>
```
