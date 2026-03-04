module lisp;

import std::collections::list;
import std::io;
import main;

// =============================================================================
// SECTION 5: ENVIRONMENT
// =============================================================================
// =============================================================================
// SECTION 5: ENVIRONMENT
// =============================================================================

// Initial capacity for dynamically-sized Env binding arrays
const usz ENV_INITIAL_CAPACITY = 4;

// Threshold: build hash table when binding_count exceeds this
const usz ENV_HASH_THRESHOLD = 16;

// Sentinel for empty hash table slots
const SymbolId ENV_HASH_EMPTY = 0xFFFFFFFF;

/**
 * Binding — Single name-value binding.
 */
struct Binding {
    SymbolId name;
    Value*   value;
}

/**
 * EnvHashEntry — Open-addressing hash table entry for Env.
 */
struct EnvHashEntry {
    SymbolId key;    // ENV_HASH_EMPTY = empty slot
    usz      index;  // Index into bindings array
}

// Per-boundary memo table entry used by promotion/copy fallback.
struct PromotionMemoEntry {
    Value*              src;
    Value*              dst;
    PromotionMemoEntry* next;
}

// Shared context for one promotion/copy boundary.
// Supports nested boundaries via `prev`.
struct PromotionContext {
    uint                epoch;
    usz                 budget;
    bool                aborted;
    PromotionMemoEntry* memo_head;
    PromotionContext*   prev;
}

/**
 * Env — Linked list of environment frames.
 * Bindings array is heap-allocated and grows dynamically.
 * For frames with >ENV_HASH_THRESHOLD bindings, a hash table provides O(1) lookup.
 */
struct Env {
    Binding*              bindings;
    usz                   binding_count;
    usz                   capacity;
    Env*                  parent;
    EnvHashEntry*         hash_table;     // NULL if not using hash
    usz                   hash_capacity;  // Power of 2
    bool                  persistent;     // true = allocated in root_region, skip in deep_copy_env
    uint                  scope_gen;      // Scope generation stamp for O(1) membership check
    bool                  is_inline;
    Binding[4]            inline_bindings;
}

fn Env* make_env(Interp* interp, Env* parent) {
    // When enabled, env frames are allocated in the current scope ESCAPE lane
    // so they survive TEMP-lane resets across TCO bounces.
    bool use_escape_lane = interp.escape_env_mode;
    main::ScopeRegion* target = interp.current_scope;

    Env* env = use_escape_lane ? interp.alloc_env_escape() : interp.alloc_env();
    env.is_inline = true;
    env.bindings = &env.inline_bindings[0];
    env.binding_count = 0;
    env.capacity = 4;
    env.parent = parent;
    env.hash_table = null;
    env.hash_capacity = 0;
    env.persistent = false;
    env.scope_gen = use_escape_lane ? target.escape_generation : target.generation;
    if (use_escape_lane) {
        main::scope_register_dtor_escape(target, (void*)env, &scope_dtor_env);
    } else {
        main::scope_register_dtor(target, (void*)env, &scope_dtor_env);
    }
    return env;
}

/**
 * Build or rebuild the hash table for an Env frame.
 * Hash capacity is next power of 2 >= 2 * binding_count.
 */
fn void Env.build_hash_table(Env* self) {
    // Determine capacity (next power of 2 >= 2 * count)
    usz cap = 32;
    while (cap < self.binding_count * 2) cap *= 2;

    // Free old hash table if exists
    if (self.hash_table != null) {
        mem::free(self.hash_table);
    }

    self.hash_capacity = cap;
    self.hash_table = (EnvHashEntry*)mem::malloc(EnvHashEntry.sizeof * cap);
    // Initialize all slots to empty
    for (usz i = 0; i < cap; i++) {
        self.hash_table[i].key = ENV_HASH_EMPTY;
    }
    // Insert all existing bindings
    usz mask = cap - 1;
    for (usz i = 0; i < self.binding_count; i++) {
        usz slot = (usz)self.bindings[i].name & mask;
        while (self.hash_table[slot].key != ENV_HASH_EMPTY) {
            slot = (slot + 1) & mask;
        }
        self.hash_table[slot].key = self.bindings[i].name;
        self.hash_table[slot].index = i;
    }
}

/**
 * Look up a SymbolId in the hash table. Returns index into bindings or ~(usz)0 if not found.
 */
<* @require self.hash_table != null : "hash_lookup called without hash table" *>
fn usz Env.hash_lookup(Env* self, SymbolId name) {
    usz mask = self.hash_capacity - 1;
    usz slot = (usz)name & mask;
    for (;;) {
        SymbolId k = self.hash_table[slot].key;
        if (k == name) return self.hash_table[slot].index;
        if (k == ENV_HASH_EMPTY) return ~(usz)0;
        slot = (slot + 1) & mask;
    }
}

/**
 * Insert into the hash table (must have space — caller ensures load factor).
 */
<* @require self.hash_table != null : "hash_insert called without hash table" *>
fn void Env.hash_insert(Env* self, SymbolId name, usz index) {
    usz mask = self.hash_capacity - 1;
    usz slot = (usz)name & mask;
    while (self.hash_table[slot].key != ENV_HASH_EMPTY && self.hash_table[slot].key != name) {
        slot = (slot + 1) & mask;
    }
    self.hash_table[slot].key = name;
    self.hash_table[slot].index = index;
}

fn void Env.define(Env* self, SymbolId name, Value* value) {
    // Fast path: use hash table if available
    if (self.hash_table != null) {
        usz idx = self.hash_lookup(name);
        if (idx != ~(usz)0) {
            self.bindings[idx].value = value;
            return;
        }
    } else {
        // Linear scan for small frames
        for (usz i = 0; i < self.binding_count; i++) {
            if (self.bindings[i].name == name) {
                self.bindings[i].value = value;
                return;
            }
        }
    }
    // Grow if needed
    if (self.binding_count >= self.capacity) {
        usz new_cap = self.capacity * 2;
        Binding* new_bindings = (Binding*)mem::malloc(Binding.sizeof * new_cap);
        for (usz i = 0; i < self.binding_count; i++) {
            new_bindings[i] = self.bindings[i];
        }
        if (!self.is_inline) {
            mem::free(self.bindings);
        }
        self.is_inline = false;
        self.bindings = new_bindings;
        self.capacity = new_cap;
    }
    usz new_idx = self.binding_count;
    self.bindings[new_idx].name = name;
    self.bindings[new_idx].value = value;
    self.binding_count++;

    // Build hash table if we crossed the threshold
    if (self.hash_table == null && self.binding_count > ENV_HASH_THRESHOLD) {
        self.build_hash_table();
    } else if (self.hash_table != null) {
        // Check load factor: rebuild if > 70%
        if (self.binding_count * 10 > self.hash_capacity * 7) {
            self.build_hash_table();
        } else {
            self.hash_insert(name, new_idx);
        }
    }
}

// Barrier-aware env write helper for dual-lane rollout.
// If target env is in current scope's ESCAPE lane, promote TEMP values first.
fn Value* promote_for_env_target(Env* target, Value* value, Interp* interp) {
    if (target == null || interp == null || interp.current_scope == null) return value;
    if (value == null) return null;

    // Dual-lane intra-scope target: TEMP -> ESCAPE lane promotion.
    if (target.scope_gen == interp.current_scope.escape_generation) {
        return boundary_promote_to_escape(value, interp);
    }

    return value;
}

fn void env_define_with_barrier(Env* env, SymbolId name, Value* value, Interp* interp) {
    if (env == null) return;
    Value* stored = promote_for_env_target(env, value, interp);
    env.define(name, stored);
}

// Barrier-aware env set helper for dual-lane rollout.
// Walks parent chain and applies TEMP->ESCAPE promotion at the actual write site.
fn void? env_set_with_barrier(Env* env, SymbolId name, Value* value, Interp* interp) {
    Env* cur = env;
    while (cur != null) {
        if (cur.hash_table != null) {
            usz idx = cur.hash_lookup(name);
            if (idx != ~(usz)0) {
                Value* stored = promote_for_env_target(cur, value, interp);
                cur.bindings[idx].value = stored;
                return;
            }
        } else {
            for (usz i = 0; i < cur.binding_count; i++) {
                if (cur.bindings[i].name == name) {
                    Value* stored = promote_for_env_target(cur, value, interp);
                    cur.bindings[i].value = stored;
                    return;
                }
            }
        }
        cur = cur.parent;
    }
    return UNBOUND_VARIABLE~;
}

fn Value* Env.lookup(Env* self, SymbolId name) {
    // Fast path: use hash table if available
    if (self.hash_table != null) {
        usz idx = self.hash_lookup(name);
        if (idx != ~(usz)0) {
            return self.bindings[idx].value;
        }
    } else {
        // Linear scan for small frames
        for (usz i = 0; i < self.binding_count; i++) {
            if (self.bindings[i].name == name) {
                return self.bindings[i].value;
            }
        }
    }
    // Search parent
    if (self.parent != null) {
        return self.parent.lookup(name);
    }
    return null;  // Not found
}

fn void? Env.set(Env* self, SymbolId name, Value* value) {
    if (self.hash_table != null) {
        usz idx = self.hash_lookup(name);
        if (idx != ~(usz)0) {
            self.bindings[idx].value = value;
            return;
        }
    } else {
        for (usz i = 0; i < self.binding_count; i++) {
            if (self.bindings[i].name == name) {
                self.bindings[i].value = value;
                return;
            }
        }
    }
    if (self.parent != null) return self.parent.set(name, value);
    return UNBOUND_VARIABLE~;
}

fn Env* Env.extend(Env* self, Interp* interp, SymbolId name, Value* value) {
    Env* new_env = make_env(interp, self);
    new_env.define(name, value);
    return new_env;
}

