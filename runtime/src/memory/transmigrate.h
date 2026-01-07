#ifndef OMNI_TRANSMIGRATE_H
#define OMNI_TRANSMIGRATE_H

#include "region_core.h"
#include "../../include/omni.h" // For Obj definition and types

// Forward declaration to avoid circular dependency
// Value is defined in src/runtime/types.h but transmigrate operates
// on opaque pointers for maximum flexibility
struct Value;

/*
 * Transmigrates (moves) an object graph from its current region
 * to the destination region.
 *
 * This is the core operation for handling data that escapes its
 * original region (e.g., returning local data from a function).
 *
 * Strategy:
 * - Small graphs (< 4KB): Deep copy with cycle detection
 * - Large graphs: Arena promotion (append source arena to destination)
 *
 * Args:
 *   root: The root object to transmigrate (must be a Value*)
 *   dest_region: The destination region
 *
 * Returns:
 *   Pointer to the transmigrated object in dest_region
 */
void* transmigrate(void* root, Region* src_region, Region* dest_region);

#endif // OMNI_TRANSMIGRATE_H
