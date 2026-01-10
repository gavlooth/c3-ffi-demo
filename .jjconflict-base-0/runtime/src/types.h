/*
 * types.h - Compatibility header for modular memory management
 *
 * This provides forward declarations needed by the memory modules.
 * The actual Value type is defined in the OmniLisp runtime.
 */

#ifndef OMNI_TYPES_H
#define OMNI_TYPES_H

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Forward declaration - Value is used by some analysis functions */
typedef struct Value Value;

/* For modules that don't need full Value definition, this is enough */

#endif /* OMNI_TYPES_H */
