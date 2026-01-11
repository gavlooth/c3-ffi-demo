/*
 * region_codegen.c - Region-RC Code Generation Extensions Implementation
 *
 * Implements code generation for Region-RC specific features.
 */

#include "region_codegen.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Issue 1 P2: Track current position for last_use check */
static int omni_codegen_current_pos = 0;

/* ============== Region Lifecycle Code Generation ============== */

void omni_codegen_region_create(CodeGenContext* ctx, RegionInfo* region) {
    if (!ctx || !region) return;

    char region_var[64];
    if (region->name) {
        snprintf(region_var, sizeof(region_var), "_region_%s", region->name);
    } else {
        snprintf(region_var, sizeof(region_var), "_region_%d", region->region_id);
    }

    /* Emit region creation comment */
    omni_codegen_emit_raw(ctx, "/* Region %s: start_pos=%d, end_pos=%d */\n",
                           region->name ? region->name : "(anon)",
                           region->start_pos, region->end_pos);

    /* Emit region_create() call - Issue 1 P2: Track position for last_use check */
    omni_codegen_emit(ctx, "struct Region* %s = region_create();  /* Line: %d */\n", region_var, ctx->current_pos);

    /* Issue 1 P2: Update current position after emitting region_create */
    ctx->current_pos++;

    /* Emit variable list comment */
    if (region->var_count > 0) {
        omni_codegen_emit_raw(ctx, "/* Variables in this region: ");
        for (size_t i = 0; i < region->var_count && i < 5; i++) {
            omni_codegen_emit_raw(ctx, "%s%s", i > 0 ? ", " : "", region->variables[i]);
        }
        if (region->var_count > 5) {
            omni_codegen_emit_raw(ctx, "... (%zu more)", region->var_count - 5);
        }
        omni_codegen_emit_raw(ctx, " */\n");
    }
}

void omni_codegen_region_destroy(CodeGenContext* ctx, RegionInfo* region) {
    if (!ctx || !region) return;

    char region_var[64];
    if (region->name) {
        snprintf(region_var, sizeof(region_var), "_region_%s", region->name);
    } else {
        snprintf(region_var, sizeof(region_var), "_region_%d", region->region_id);
    }

    /* Emit region cleanup */
    omni_codegen_emit(ctx, "region_exit(%s);  /* Mark scope as inactive */\n", region_var);
    omni_codegen_emit(ctx, "region_destroy_if_dead(%s);  /* Free if no external refs */\n", region_var);
}

/* ============== Transmigration Code Generation ============== */

/* Issue 1 P2: Function declaration */
void omni_codegen_escape_repair(CodeGenContext* ctx,
                                const char* var_name,
                                const char* dst_region_var,
                                EscapeRepairStrategy strategy);

/* Issue 1 P2: Choose between transmigrate vs retain based on escape type
 *
 * Issue 1 P2: Implemented RETAIN_REGION support for demonstration.
 * Strategy selection is controlled by an environment variable for testing.
 *
 * Future enhancement: Use size-based heuristic (small → transmigrate, large → retain)
 */
static EscapeRepairStrategy choose_escape_repair_strategy(CodeGenContext* ctx,
                                                     const char* var_name) {
    (void)ctx;
    (void)var_name;

    /* Check environment variable for strategy override (for testing) */
    char* strategy_env = getenv("OMNILISP_REPAIR_STRATEGY");
    if (strategy_env) {
        if (strcmp(strategy_env, "retain") == 0) {
            return ESCAPE_REPAIR_RETAIN_REGION;
        } else if (strcmp(strategy_env, "transmigrate") == 0) {
            return ESCAPE_REPAIR_TRANSMIGRATE;
        }
    }

    /* Default: Always transmigrate (conservative default) */
    return ESCAPE_REPAIR_TRANSMIGRATE;

    /* Future: Size-based strategy
     * if (size < RETAIN_THRESHOLD) {
     *     return ESCAPE_REPAIR_TRANSMIGRATE;
     * } else {
     *     return ESCAPE_REPAIR_RETAIN_REGION;
     * }
     */
}

bool omni_should_transmigrate(CodeGenContext* ctx, const char* var_name) {
    if (!ctx || !ctx->analysis || !var_name) return false;

    /* Check escape classification */
    EscapeClass esc = omni_get_escape_class(ctx->analysis, var_name);

    /* Transmigrate if value escapes scope */
    return (esc == ESCAPE_RETURN || esc == ESCAPE_CLOSURE || esc == ESCAPE_GLOBAL);
}

void omni_codegen_transmigrate_on_escape(CodeGenContext* ctx,
                                         const char* var_name,
                                         EscapeClass escape_class) {
    if (!ctx || !var_name) return;

    /* Only transmigrate on specific escape types */
    if (escape_class != ESCAPE_RETURN &&
        escape_class != ESCAPE_CLOSURE &&
        escape_class != ESCAPE_GLOBAL) {
        return;
    }

    /* Get source and target regions */
    const char* src_region = omni_get_var_region_name(ctx, var_name);
    const char* dst_region = "_caller_region";  // Default for returns

    /* Emit transmigrate call */
    omni_codegen_emit_raw(ctx, "/* %s escapes scope - transmigrate from %s to %s */\n",
                          var_name,
                          src_region ? src_region : "(local)",
                          dst_region);

    omni_codegen_emit(ctx, "%s = transmigrate(%s, %s, %s);\n",
                      var_name, var_name,
                      src_region ? src_region : "_local_region",
                      dst_region);
}

/* Issue 1 P2: Emit escape repair based on chosen strategy
 *
 * Emits either:
 * - transmigrate: Copies value graph to destination region
 * - retain: Increments RC to keep source region alive
 *
 * Full implementation requires last-use analysis for release insertion.
 * For now, only transmigrate is implemented.
 *
 * TODO: Add size-based decision (small → transmigrate, large → retain)
 * TODO: Implement retain/release with last-use analysis
 */
void omni_codegen_escape_repair(CodeGenContext* ctx,
                                const char* var_name,
                                const char* dst_region_var,
                                EscapeRepairStrategy strategy) {
    if (!ctx || !var_name) return;

    const char* src_region = omni_get_var_region_name(ctx, var_name);

    if (strategy == ESCAPE_REPAIR_TRANSMIGRATE) {
        /* Emit transmigrate call */
        omni_codegen_emit_raw(ctx, "/* %s escapes scope - transmigrate from %s to %s */\n",
                               var_name,
                               src_region ? src_region : "(local)",
                               dst_region_var);
        omni_codegen_emit(ctx, "%s = transmigrate(%s, %s, %s);\n",
                           var_name, var_name,
                           src_region ? src_region : "_local_region",
                           dst_region_var);
    } else if (strategy == ESCAPE_REPAIR_RETAIN_REGION) {
        /* Issue 1 P2: Emit retain/release */
        const char* src_region = omni_get_var_region_name(ctx, var_name);
        if (src_region) {
            omni_codegen_emit_raw(ctx, "/* %s escapes scope - retain %s */\n",
                                  var_name, src_region);
            omni_codegen_emit(ctx, "region_retain_internal(%s);\n", src_region);
        }
        omni_codegen_emit(ctx, "%s = %s;  /* Still points to source region */\n",
                           var_name, var_name);
    } else {
        /* Fallback to transmigrate */
        omni_codegen_emit_raw(ctx, "/* %s escapes scope - transmigrate from %s to %s */\n",
                              var_name,
                              src_region ? src_region : "(local)",
                              dst_region_var);
        omni_codegen_emit(ctx, "%s = transmigrate(%s, %s, %s);\n",
                           var_name, var_name,
                           src_region ? src_region : "_local_region",
                           dst_region_var);
    }
}

void omni_codegen_transmigrate_return(CodeGenContext* ctx, const char* return_var) {
    if (!ctx || !return_var) return;

    const char* src_region = omni_get_var_region_name(ctx, return_var);

    /* Emit transmigrate in return statement */
    omni_codegen_emit_raw(ctx, "/* Transmigrate return value to caller region */\n");
    omni_codegen_emit(ctx, "return transmigrate(%s, %s, _caller_region);\n",
                      return_var,
                      src_region ? src_region : "_local_region");
}

/* ============== Tethering Code Generation ============== */

void omni_codegen_tether_start(CodeGenContext* ctx, const char* region_name) {
    if (!ctx || !region_name) return;

    omni_codegen_emit(ctx, "region_tether_start(%s);  /* Keep alive during call */\n",
                      region_name);
}

void omni_codegen_tether_end(CodeGenContext* ctx, const char* region_name) {
    if (!ctx || !region_name) return;

    omni_codegen_emit(ctx, "region_tether_end(%s);  /* Release borrow */\n",
                      region_name);
}

void omni_codegen_tether_params(CodeGenContext* ctx, OmniValue* lambda) {
    if (!ctx || !lambda) return;
    if (lambda->tag != OMNI_LAMBDA && lambda->tag != OMNI_REC_LAMBDA) return;

    /* Get parameters */
    OmniValue* params = lambda->lambda.params;
    if (!params || params->tag != OMNI_CELL) return;

    /* Find parameters that need tethering (those from outer regions) */
    OmniValue* p = params;
    while (p && p->tag == OMNI_CELL && !omni_is_nil(p)) {
        OmniValue* param = p->cell.car;
        if (param && param->tag == OMNI_SYM) {
            const char* param_name = param->str_val;

            /* Check if this parameter comes from a different region */
            const char* param_region = omni_get_var_region_name(ctx, param_name);

            if (param_region && strcmp(param_region, "_local_region") != 0) {
                /* This parameter is from a different region - need tethering */
                omni_codegen_tether_start(ctx, param_region);
            }
        }
        p = p->cell.cdr;
    }
}

/* ============== Region-Aware Allocation ============== */

void omni_codegen_alloc_in_region(CodeGenContext* ctx,
                                   const char* var_name,
                                   const char* type,
                                   const char* constructor,
                                   const char* args) {
    if (!ctx || !var_name) return;

    /* Determine which region to use */
    const char* region_name = "_local_region";  // Default

    /* If variable is assigned to a specific region, use that */
    if (ctx->analysis && ctx->analysis->current_region) {
        if (ctx->analysis->current_region->name) {
            static char buf[64];
            snprintf(buf, sizeof(buf), "_region_%s", ctx->analysis->current_region->name);
            region_name = buf;
        }
    }

    /* Emit allocation with region parameter */
    omni_codegen_emit(ctx, "Obj* %s = %s(%s, %s);\n",
                      var_name,
                      constructor ? constructor : "mk_int_region",
                      region_name,
                      args ? args : "0");
}

/* ============== Integration Helpers ============== */

const char* omni_get_var_region_name(CodeGenContext* ctx, const char* var_name) {
    if (!ctx || !ctx->analysis || !var_name) {
        return "_local_region";  /* Default region */
    }

    /* Look up which region contains this variable */
    RegionInfo* region = omni_get_var_region(ctx->analysis, var_name);
    if (region) {
        static char buf[64];
        if (region->name) {
            snprintf(buf, sizeof(buf), "_region_%s", region->name);
            return buf;
        } else {
            snprintf(buf, sizeof(buf), "_region_%d", region->region_id);
            return buf;
        }
    }

    return "_local_region";  /* Default */
}

/* ============== Issue 1 P2: Emit Region Release at Last Use ============== */

/*
 * omni_codegen_emit_region_releases_at_pos - Emit region_release_internal at last-use positions
 *
 * Issue 1 P2: When a variable reaches its last use, emit region_release_internal()
 * to decrement the region's external reference count (for Region-RC).
 *
 * This is called after emitting each statement/expression to check if any variables
 * reached their last-use position at this code location.
 *
 * Generated code pattern:
 *   region_release_internal(omni_obj_region(var_name));
 *
 * Args:
 *   ctx: Code generation context
 *   position: Current codegen position (corresponds to last_use values in analysis)
 */
void omni_codegen_emit_region_releases_at_pos(CodeGenContext* ctx, int position) {
    if (!ctx || !ctx->analysis || !ctx->analysis->var_usages) {
        return;  /* No analysis data available */
    }

    /* Iterate through all variables and emit release for those at last_use */
    for (VarUsage* u = ctx->analysis->var_usages; u; u = u->next) {
        /* Skip if not a last-use position match */
        if (u->last_use != position) {
            continue;
        }

        /* Skip if variable was escaped (returned, captured, stored globally) */
        /* Escaped variables are retained externally, don't release them here */
        if (u->flags & (VAR_USAGE_ESCAPED | VAR_USAGE_RETURNED | VAR_USAGE_CAPTURED)) {
            continue;
        }

        /* Get region name for this variable */
        const char* region_name = omni_get_var_region_name(ctx, u->name);
        if (!region_name) {
            continue;  /* No region assigned */
        }

        /* Emit region_release_internal() call */
        omni_codegen_emit_raw(ctx, 
            "/* %s: last use at pos %d - release region reference */\n",
            u->name, position);
        omni_codegen_emit(ctx, "region_release_internal(%s);\n", region_name);
    }
}
