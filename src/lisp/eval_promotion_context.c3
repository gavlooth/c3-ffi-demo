module lisp;

import std::io;
import main;

// =============================================================================
// SECTION 2.4: FRAME PUSH/POP (promotion context + scoped copy)
// =============================================================================
fn void promotion_context_begin(Interp* interp, PromotionContext* ctx, usz budget = usz.max) {
    if (interp == null || ctx == null) return;
    interp.promotion_epoch_counter++;
    if (interp.promotion_epoch_counter == 0) interp.promotion_epoch_counter = 1;
    ctx.epoch = interp.promotion_epoch_counter;
    ctx.budget = budget;
    ctx.aborted = false;
    ctx.memo_head = null;
    ctx.prev = interp.active_promotion_ctx;
    interp.active_promotion_ctx = ctx;
}

fn void promotion_context_end(Interp* interp, PromotionContext* ctx) {
    if (interp == null || ctx == null) return;
    if (interp.active_promotion_ctx == ctx) {
        interp.active_promotion_ctx = ctx.prev;
    }
}

fn bool promotion_context_consume(PromotionContext* ctx, usz units = 1) @inline {
    if (ctx == null) return true;
    if (ctx.aborted) return false;
    if (ctx.budget == usz.max) return true;
    if (ctx.budget < units) {
        ctx.aborted = true;
        return false;
    }
    ctx.budget -= units;
    return true;
}

fn Value* promotion_context_lookup(PromotionContext* ctx, Value* src) @inline {
    if (ctx == null || src == null) return null;
    PromotionMemoEntry* e = ctx.memo_head;
    while (e != null) {
        if (e.src == src) return e.dst;
        e = e.next;
    }
    return null;
}

fn void promotion_context_remember(PromotionContext* ctx, Value* src, Value* dst, Interp* interp) {
    if (ctx == null || src == null || dst == null || interp == null || interp.current_scope == null) return;
    if (ctx.aborted) return;
    PromotionMemoEntry* e = (PromotionMemoEntry*)interp.current_scope.alloc(PromotionMemoEntry.sizeof);
    e.src = src;
    e.dst = dst;
    e.next = ctx.memo_head;
    ctx.memo_head = e;
}

fn Value* copy_to_parent_site_ctx(Value* v, Interp* interp, uint site, PromotionContext* ctx) {
    if (v == null) return null;
    if (ctx != null) {
        Value* cached = promotion_context_lookup(ctx, v);
        if (cached != null) return cached;
    }
    bool can_track = promotion_context_consume(ctx, 1);
    Value* copied = copy_to_parent_site(v, interp, site);
    if (can_track && ctx != null && copied != null) {
        promotion_context_remember(ctx, v, copied, interp);
    }
    return copied;
}

// Boundary-aware copy helper:
// only copy when value belongs to source_scope; otherwise reuse as-is.
fn Value* copy_value_if_owned_by_scope(Value* v, Interp* interp, main::ScopeRegion* source_scope, uint site) {
    if (v == null) return null;
    if (source_scope != null && !main::is_in_scope((void*)v, source_scope)) return v;

    switch (v.tag) {
        case NIL:
            return make_nil(interp);
        case INT:
            return make_int(interp, v.int_val);
        case DOUBLE:
            return make_double(interp, v.double_val);
        case SYMBOL:
            return make_symbol(interp, v.sym_val);
        case STRING:
            return make_string(interp, v.str_chars[:v.str_len]);
        case ERROR:
            return make_error(interp, v.str_chars[:v.str_len]);
        case PRIMITIVE:
        case HASHMAP:
        case ARRAY:
        case METHOD_TABLE:
        case TYPE_INFO:
        case MODULE:
            return v;
        case INSTANCE:
        case FFI_HANDLE:
            if (boundary_can_reuse_value(v, interp)) return v;
            return copy_to_parent_site_ctx(v, interp, site, interp.active_promotion_ctx);
        default:
            return copy_to_parent_site_ctx(v, interp, site, interp.active_promotion_ctx);
    }
}
