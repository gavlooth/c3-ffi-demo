/*
 * region_codegen.c - Region-RC Code Generation Extensions Implementation
 *
 * Implements code generation for Region-RC specific features.
 */

#include "region_codegen.h"
#include <string.h>
#include <stdio.h>

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

    /* Emit region_create() call */
    omni_codegen_emit(ctx, "struct Region* %s = region_create();\n", region_var);

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
