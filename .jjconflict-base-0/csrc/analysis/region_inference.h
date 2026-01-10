#ifndef OMNI_REGION_INFERENCE_H
#define OMNI_REGION_INFERENCE_H

#include "analysis.h"
#include "../ast/ast.h"

/* Compiler context - minimal definition */
typedef struct CompilerCtx {
    AnalysisContext* analysis;
    /* Other fields... */
} CompilerCtx;

/*
 * Main entry point for region inference pass.
 *
 * Analyzes variable interactions, groups them into regions,
 * and computes liveness bounds for region_create/destroy placement.
 *
 * Args:
 *   ctx: Compiler context containing analysis information
 *
 * This implements:
 * 1. Build Variable Interaction Graph (VIG)
 * 2. Find Connected Components (Candidate Regions)
 * 3. Liveness Analysis for each Component
 * 4. Dominator Placement for region lifecycle
 */
void infer_regions(CompilerCtx* ctx);

#endif // OMNI_REGION_INFERENCE_H
