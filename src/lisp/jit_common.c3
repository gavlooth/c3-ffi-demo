module lisp;

import std::io;
import std::collections::list;
import main;

faultdef JIT_COMPILE_FAILED;

// =============================================================================
// Context boundary state — saved/restored at every StackCtx switch point.
// Replaces 12-field manual save/restore (24 lines per site → 2 lines).
// =============================================================================

struct SavedInterpState {
    Env*        jit_env;
    Env*        match_env;
    usz         eval_depth;
    InterpFlags flags;
    Expr*       tco_expr;
    Env*        tco_env;
    main::ScopeRegion* tco_recycle;
    usz         reset_depth;
    usz         handler_count;
    main::ScopeRegion* scope;
    bool        escape_env_mode;
}

fn void save_interp_state(Interp* interp, SavedInterpState* s) {
    s.jit_env         = interp.jit_env;
    s.match_env       = interp.match_env;
    s.eval_depth      = interp.eval_depth;
    s.flags           = interp.flags;
    s.tco_expr        = interp.jit_tco_expr;
    s.tco_env         = interp.jit_tco_env;
    s.tco_recycle     = interp.tco_recycle_scope;
    s.reset_depth     = interp.reset_depth;
    s.handler_count   = interp.handler_count;
    s.scope           = interp.current_scope;
    s.escape_env_mode = interp.escape_env_mode;
}

fn void restore_interp_state(Interp* interp, SavedInterpState* s) {
    interp.jit_env           = s.jit_env;
    interp.match_env         = s.match_env;
    interp.eval_depth        = s.eval_depth;
    interp.flags             = s.flags;
    interp.jit_tco_expr      = s.tco_expr;
    interp.jit_tco_env       = s.tco_env;
    interp.tco_recycle_scope = s.tco_recycle;
    interp.reset_depth       = s.reset_depth;
    interp.handler_count     = s.handler_count;
    interp.current_scope     = s.scope;
    interp.escape_env_mode   = s.escape_env_mode;
}
