/*
 * region_codegen.h - Region-RC Code Generation Extensions
 *
 * Extends the code generator to emit Region-RC specific code:
 * - region_create/region_destroy lifecycle
 * - transmigrate calls for escaping values
 * - tethering for RegionRef arguments
 */

#ifndef OMNI_REGION_CODEGEN_H
#define OMNI_REGION_CODEGEN_H

#include "codegen.h"
#include "../analysis/analysis.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============== Region Lifecycle Code Generation ============== */

/*
 * Emit region_create at function entry or dominator block.
 *
 * Generated code pattern:
 *   Region* _local_region = region_create();
 *   // All local allocations use _local_region
 */
void omni_codegen_region_create(CodeGenContext* ctx, RegionInfo* region);

/*
 * Emit region_exit and region_destroy_if_dead at scope exit.
 *
 * Generated code pattern:
 *   region_exit(_local_region);  // Sets scope_alive = false
 *   region_destroy_if_dead(_local_region);  // Frees if no external refs
 */
void omni_codegen_region_destroy(CodeGenContext* ctx, RegionInfo* region);

/* ============== Transmigration Code Generation ============== */

/*
 * Emit transmigrate call for values that escape their region.
 *
 * Generated code pattern when returning local data:
 *   if (omni_should_transmigrate(x)) {
 *       return transmigrate(x, _local_region, _caller_region);
 *   }
 *
 * Args:
 *   ctx: Code generation context
 *   var_name: Name of variable that may escape
 *   escape_class: Type of escape (RETURN, ARG, CLOSURE, etc.)
 */
void omni_codegen_transmigrate_on_escape(CodeGenContext* ctx,
                                         const char* var_name,
                                         EscapeClass escape_class);

/*
 * Emit transmigrate call at return site.
 *
 * Generated code pattern:
 *   return transmigrate(result, _local_region, _caller_region);
 */
void omni_codegen_transmigrate_return(CodeGenContext* ctx, const char* return_var);

/* ============== Issue 1 P2: Escape Repair Strategy ============== */

/*
 * Emit escape repair based on chosen strategy.
 *
 * Issue 1 P2 TODO: This function is a stub for the retain/release insertion feature.
 * Full implementation requires:
 * - Size-based strategy selection (small → transmigrate, large → retain)
 * - Last-use analysis for release insertion
 * - region_retain_internal() call when choosing RETAIN
 * - region_release_internal() call at last-use position
 *
 * Args:
 *   ctx: Code generation context
 *   var_name: Name of variable that escapes
 *   dst_region_var: Destination region variable (e.g., "_caller_region")
 *   strategy: ESCAPE_REPAIR_TRANSMIGRATE or ESCAPE_REPAIR_RETAIN_REGION
 */
void omni_codegen_escape_repair(CodeGenContext* ctx,
                                const char* var_name,
                                const char* dst_region_var,
                                EscapeRepairStrategy strategy);

/* ============== Tethering Code Generation ============== */

/*
 * Emit tether_start call at function entry for RegionRef arguments.
 *
 * Generated code pattern:
 *   region_tether_start(arg_region);  // Keep arg's region alive
 */
void omni_codegen_tether_start(CodeGenContext* ctx, const char* region_name);

/*
 * Emit tether_end call at function exit for RegionRef arguments.
 *
 * Generated code pattern:
 *   region_tether_end(arg_region);  // Release borrow
 */
void omni_codegen_tether_end(CodeGenContext* ctx, const char* region_name);

/*
 * Emit all tethering calls for function parameters.
 *
 * Analyzes which parameters are RegionRefs and emits:
 * - tether_start at entry
 * - tether_end at all exit points
 */
void omni_codegen_tether_params(CodeGenContext* ctx, OmniValue* lambda);

/* ============== Region-Aware Allocation ============== */

/*
 * Emit allocation using region-aware constructor.
 *
 * Generated code pattern:
 *   Value* x = mk_int_region(_local_region, 42);
 *   // Instead of: Value* x = mk_int(42);
 */
void omni_codegen_alloc_in_region(CodeGenContext* ctx,
                                   const char* var_name,
                                   const char* type,
                                   const char* constructor,
                                   const char* args);

/* ============== Integration Helpers ============== */

/*
 * Check if a variable's value should trigger transmigration.
 * Returns true if:
 * - Variable escapes its scope (return, closure capture, global)
 * - Variable is in a different region than target
 */
bool omni_should_transmigrate(CodeGenContext* ctx, const char* var_name);

/*
 * Get the region name for a given variable.
 * Returns the region that contains this variable, or NULL.
 */
const char* omni_get_var_region_name(CodeGenContext* ctx, const char* var_name);

#ifdef __cplusplus
}
#endif

#endif /* OMNI_REGION_CODEGEN_H */
