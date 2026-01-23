# REVIEWED:NAIVE Optimization Tasks

This document tracks all `REVIEWED:NAIVE` blocks in the codebase with proposed mitigations.

---

## Category 1: Linear Lookup in Symbol Tables / Registries

These are O(n) lookups that should use hash tables for O(1) average-case performance.

### 1.1 codegen/codegen.c:261 - `lookup_symbol`
**Current:** Linear scan through `ctx->symbols.names[]` array
**Impact:** Called frequently during code generation
**Mitigation:** Replace with hash table (e.g., `khash` or custom). Key = symbol name, Value = C name.
```c
// Before: O(n) linear scan
for (size_t i = 0; i < ctx->symbols.count; i++) {
    if (strcmp(ctx->symbols.names[i], name) == 0) { ... }
}

// After: O(1) hash lookup
khash_t(str) *symbol_map;
khiter_t k = kh_get(str, symbol_map, name);
```

### 1.2 codegen/codegen.c:464 - `capture_list_contains`
**Current:** Linear scan through capture list names
**Impact:** Called for each variable reference in closures
**Mitigation:** Use a hash set for O(1) membership testing.
```c
// Use khash or similar for set membership
KHASH_SET_INIT_STR(strset)
khash_t(strset) *capture_set;
```

### 1.3 codegen/codegen.c:2635 - `check_function_defined`
**Current:** Linear scan through `ctx->defined_functions`
**Impact:** Called for every function definition (multiple dispatch)
**Mitigation:** Hash table keyed by function name, value = definition count.

### 1.4 codegen/codegen.c:2649 - `track_function_definition`
**Current:** Linear scan to find/increment definition count
**Impact:** Paired with `check_function_defined`
**Mitigation:** Same hash table as 1.3 - increment count in hash entry.

### 1.5 codegen/spec_db.c:216 - `spec_db_lookup`
**Current:** Linear scan through all signatures in SpecDB
**Impact:** Called during specialization lookup
**Mitigation:** Two-level hash: outer key = func_name, inner = type signature hash.
```c
typedef struct {
    khash_t(sig) *by_func_name;  // func_name -> list of signatures
} SpecDB;
```

### 1.6 codegen/spec_db.c:237 - `spec_db_find_match`
**Current:** Linear scan with type compatibility check
**Impact:** Called during call site resolution
**Mitigation:** Cache common lookups; use type hierarchy for faster matching.

### 1.7 codegen/spec_db.c:273 - `spec_db_get_all`
**Current:** Linear scan collecting all signatures for a function
**Impact:** Called for method enumeration
**Mitigation:** Store signatures in per-function linked list for direct access.

### 1.8 analysis/analysis.c:215 - `find_or_create_var_usage`
**Current:** Linked list traversal for VarUsage lookup
**Impact:** Called for every variable reference during analysis
**Mitigation:** Hash table keyed by variable name.
```c
typedef struct {
    khash_t(var_usage) *usage_map;  // name -> VarUsage*
    VarUsage *usage_list;           // Keep list for iteration
} AnalysisContext;
```

### 1.9 analysis/analysis.c:262 - `find_or_create_escape_info`
**Current:** Linked list traversal for EscapeInfo lookup
**Impact:** Called during escape analysis
**Mitigation:** Hash table keyed by variable name (can share with 1.8).

### 1.10 analysis/analysis.c:287 - `find_or_create_owner_info`
**Current:** Linked list traversal for OwnerInfo lookup
**Impact:** Called during ownership analysis
**Mitigation:** Hash table keyed by variable name.

### 1.11 analysis/analysis.c:1604 - `omni_get_var_usage`
**Current:** Linked list traversal (query function)
**Impact:** External API for variable usage queries
**Mitigation:** Use hash table from 1.8.

### 1.12 analysis/analysis.c:2793 - `omni_get_var_region`
**Current:** Nested loops: regions -> variables
**Impact:** Called to find which region owns a variable
**Mitigation:** Maintain reverse map: var_name -> RegionInfo*.

### 1.13 analysis/analysis.c:4124 - `omni_get_type`
**Current:** Linear scan through type registry
**Impact:** Called for every type reference
**Mitigation:** Hash table in TypeRegistry keyed by type name.

### 1.14 analysis/analysis.c:6125 - `omni_lookup_function_signature`
**Current:** Linear scan through function summaries
**Impact:** Called during type checking
**Mitigation:** Hash table keyed by function name.

### 1.15 analysis/type_env.c:386 - `type_env_lookup`
**Current:** Linear scan through TypeEnv bindings
**Impact:** Called for every variable type lookup
**Mitigation:** Hash table per scope level.
```c
typedef struct TypeEnv {
    khash_t(type_binding) *bindings;
    struct TypeEnv *parent;
} TypeEnv;
```

### 1.16 analysis/region_inference.c:52 - `vig_get_node`
**Current:** Linked list traversal for VIG node lookup
**Impact:** Called frequently during region inference
**Mitigation:** Hash table keyed by variable name.

### 1.17 analysis/component.c:11 - `find_or_create_component`
**Current:** Linked list traversal for ComponentInfo lookup
**Impact:** Called during SCC component building
**Mitigation:** Array indexed by scc_id (IDs are typically small integers).
```c
ComponentInfo **components_by_id;  // Direct index access
size_t components_capacity;
```

### 1.18 analysis/tether_elision.c:12 - `find_matching_exit`
**Current:** Linear scan through tether points for matching exit
**Impact:** Called during tether elision optimization
**Mitigation:** Store entry/exit pairs in a map keyed by region_id.

### 1.19 analysis/static_sym.c:12 - CFG node lookup by position
**Current:** Linear scan through CFG nodes checking position ranges
**Impact:** Called to map source positions to CFG nodes
**Mitigation:** Build interval tree or sorted array with binary search.

### 1.20 ast/ast.c:562 - `omni_dict_get`
**Current:** Linear scan through dict key-value pairs
**Impact:** Called for every dictionary access
**Mitigation:** Use hash table for dict implementation.
```c
typedef struct {
    khash_t(obj) *table;  // OmniValue* key -> OmniValue* value
    // Keep array for ordered iteration if needed
} OmniDict;
```

### 1.21 ast/ast.c:601 - `omni_dict_has`
**Current:** Linear scan for key existence
**Impact:** Called for dictionary membership tests
**Mitigation:** Same hash table as 1.20.

### 1.22 ast/ast.c:626 - `omni_user_type_get_field`
**Current:** Linear scan through field names
**Impact:** Called for every field access on user types
**Mitigation:** For small structs (<8 fields), linear is fine. For larger, use field name hash table or compute field index at compile time.

### 1.23 ast/ast.c:637 - `omni_user_type_set_field`
**Current:** Linear scan through field names
**Impact:** Called for every field assignment
**Mitigation:** Same as 1.22.

### 1.24 runtime/condition.c:68 - `condition_type_lookup`
**Current:** Linear scan through condition registry by name
**Impact:** Called when creating/handling conditions
**Mitigation:** Hash table keyed by condition type name.

### 1.25 runtime/condition.c:78 - `condition_type_lookup_id`
**Current:** Linear scan through condition registry by ID
**Impact:** Called for condition type resolution
**Mitigation:** Array indexed by ID (IDs are sequential integers).
```c
ConditionType **condition_types_by_id;
size_t condition_type_count;
```

### 1.26 runtime/condition.c:206 - `find_slot`
**Current:** Linear scan through condition slots
**Impact:** Called for slot access on conditions
**Mitigation:** Hash table for slots, or sorted array with binary search.

### 1.27 runtime/generic.c:214 - Method dispatch lookup
**Current:** Linear scan through methods list
**Impact:** Called on every generic function invocation (HOT PATH)
**Mitigation:** Method dispatch cache + type signature hash table.
```c
typedef struct {
    khash_t(method_cache) *cache;  // type_sig_hash -> MethodInfo*
    MethodInfo *methods;            // Fallback list
} GenericFunction;
```

### 1.28 runtime/generic.c:296 - Method arity check
**Current:** Linear scan to find method with matching arity
**Impact:** Called during dispatch
**Mitigation:** Group methods by arity in separate lists.

### 1.29 macro/pattern.c:97 - `find_binding`
**Current:** Linked list traversal for pattern binding
**Impact:** Called during macro pattern matching
**Mitigation:** Hash table keyed by variable name.

### 1.30 macro/template.c:15 - `omni_macro_is_bound`
**Current:** Linked list traversal for binding existence
**Impact:** Called during template expansion
**Mitigation:** Hash set for bound names.

### 1.31 macro/template.c:25 - `omni_macro_get_binding`
**Current:** Linked list traversal for binding lookup
**Impact:** Called during template variable substitution
**Mitigation:** Hash table keyed by variable name.

### 1.32 macro/hygiene.c:46 - Reserved symbol check
**Current:** Linear scan through static reserved symbols array
**Impact:** Called for every symbol during hygiene processing
**Mitigation:** Use perfect hash or compile-time generated hash set.
```c
// Generate with gperf or similar
static bool is_reserved_symbol(const char *name) {
    // Perfect hash lookup
}
```

### 1.33 macro/hygiene.c:94 - `omni_macro_lookup_rename`
**Current:** Linked list traversal for rename entries
**Impact:** Called during hygienic macro expansion
**Mitigation:** Hash table keyed by (original_name, mark) pair.

### 1.34 macro/macro.c:263 - `omni_macro_lookup`
**Current:** Linked list traversal for macro definitions
**Impact:** Called to resolve macro invocations
**Mitigation:** Hash table keyed by macro name.

---

## Category 2: O(n²) Algorithms

These need algorithmic improvements.

### 2.1 analysis/region_inference.c:224 - `connect_all_vars`
**Current:** Nested loops connecting all variable pairs - O(n²)
**Impact:** Called to build variable interaction graph
**Mitigation:** This may be inherently O(n²) if all pairs must be connected. Consider:
- Lazy edge creation (only add edges when queried)
- Implicit complete subgraph representation
```c
typedef struct {
    char **vars;
    size_t count;
    bool is_complete;  // If true, all pairs are connected
} VarClique;
```

### 2.2 analysis/region_inference.c:281 - Bound-used var interaction
**Current:** Nested loops connecting bound vars to used vars - O(m×n)
**Impact:** Called during let-binding analysis
**Mitigation:** Same as 2.1 - consider bipartite representation.

### 2.3 analysis/region_inference.c:151 - `var_list_add`
**Current:** O(n) duplicate check before adding
**Impact:** Called for every variable addition
**Mitigation:** Use hash set for O(1) duplicate detection.
```c
typedef struct {
    char **vars;
    size_t count, capacity;
    khash_t(strset) *seen;  // For O(1) duplicate check
} VarList;
```

### 2.4 analysis/region_inference.c:78 - `vig_add_edge`
**Current:** O(n) check for existing edge before adding
**Impact:** Called for every edge addition
**Mitigation:** Use adjacency hash set per node.
```c
typedef struct VIGNode {
    char *var_name;
    khash_t(strset) *neighbors;  // O(1) edge lookup
    struct VIGNode *next;
} VIGNode;
```

### 2.5 analysis/type_env.c:120 - Union type deduplication
**Current:** O(n²) nested loops for member deduplication
**Impact:** Called when creating union types
**Mitigation:** Use hash set for seen types during construction.

### 2.6 analysis/type_env.c:242 - Union type comparison
**Current:** O(n²) all-pairs member comparison
**Impact:** Called for type equality checks
**Mitigation:** Sort members and compare linearly, or use canonical form with hash.

### 2.7 analysis/type_infer.c:138 - Union type compatibility
**Current:** O(n²) member compatibility check
**Impact:** Called during type inference
**Mitigation:** Same as 2.6 - canonical ordering enables O(n) comparison.

### 2.8 analysis/analysis.c:2094 - `cfg_node_add_use`
**Current:** O(n) duplicate check before adding use
**Impact:** Called for every variable use in CFG
**Mitigation:** Hash set for uses per CFG node.

### 2.9 analysis/analysis.c:2364 - `string_set_contains`
**Current:** O(n) linear scan for set membership
**Impact:** Called during liveness analysis (inner loop)
**Mitigation:** Replace with hash set.

### 2.10 analysis/analysis.c:2412 - Live_out minus def computation
**Current:** O(n²) nested loops for set difference
**Impact:** Called during liveness fixed-point iteration
**Mitigation:** Use bit vectors or hash sets for live variable sets.
```c
typedef struct {
    khash_t(strset) *live_in;
    khash_t(strset) *live_out;
} CFGNode;
```

### 2.11 analysis/analysis.c:4272 - Ownership edge update
**Current:** Linear scan through edges to find matching edge
**Impact:** Called during ownership graph updates
**Mitigation:** Hash table keyed by (from_type, from_field) pair.

---

## Category 3: Fixed Buffer / Hardcoded Limits

These need dynamic allocation or size validation.

### 3.1 codegen/typed_array_codegen.c:106 - Dimensions array buffer
**Current:** Fixed 256-byte buffer for dimensions initialization
**Impact:** Overflow risk with high-rank arrays
**Mitigation:** Dynamic allocation based on rank.
```c
size_t buf_size = rank * 20 + 16;  // Estimate per dimension
char *dims_init = malloc(buf_size);
// ... use snprintf with size checking
free(dims_init);
```

### 3.2 codegen/typed_array_codegen.c:155 - Indices buffer (get)
**Current:** Fixed 256-byte buffer for indices array
**Impact:** Same as 3.1
**Mitigation:** Same as 3.1.

### 3.3 codegen/typed_array_codegen.c:183 - Indices buffer (set)
**Current:** Fixed 256-byte buffer for indices array
**Impact:** Same as 3.1
**Mitigation:** Same as 3.1.

### 3.4 runtime/collections.c:929 - List to array conversion
**Current:** Hardcoded 1024 element limit
**Impact:** Truncates lists longer than 1024 elements
**Mitigation:** Two-pass (count then allocate) or dynamic reallocation.
```c
// Option 1: Two-pass
size_t count = list_length(coll);
Obj **arr = malloc(sizeof(Obj*) * count);

// Option 2: Dynamic growth
size_t cap = 64;
Obj **arr = malloc(sizeof(Obj*) * cap);
// ... grow as needed
```

### 3.5 analysis/analysis.c:6058 - Generic type instantiation
**Current:** Fixed 256-byte buffer for instance name
**Impact:** Overflow with long type names or many parameters
**Mitigation:** Calculate required size first, then allocate.

---

## Category 4: Inefficient String Operations

### 4.1 parser/pika_c/pika.c:952 - strlen in loop
**Current:** `strlen(s)` called on every iteration
**Impact:** O(n²) for string of length n
**Mitigation:** Cache string length before loop.
```c
size_t len = strlen(s);
for (size_t i = 0; i < len; i++) { ... }
```

### 4.2 codegen/spec_db.c:128 - In-place space removal
**Current:** In-place modification with pointer manipulation
**Impact:** Minor - already O(n)
**Mitigation:** Consider pre-allocating without spaces, or document as acceptable.

### 4.3 runtime/string_utils.c:145 - String join with strcat
**Current:** Repeated `strcat` calls - O(n²) total
**Impact:** Slow for joining many strings
**Mitigation:** Use pointer arithmetic to track position.
```c
char *pos = result;
for (size_t i = 0; i < count; i++) {
    if (i > 0) {
        memcpy(pos, sep, sep_len);
        pos += sep_len;
    }
    size_t len = strlen(strings[i]);
    memcpy(pos, strings[i], len);
    pos += len;
}
*pos = '\0';
```

### 4.4 runtime/string_utils.c:202 - String replace with strcat
**Current:** Repeated `strcat` for building replacement result
**Impact:** O(n²) for many replacements
**Mitigation:** Same as 4.3 - use position pointer.

### 4.5 runtime/string_utils.c:486 - `prim_string_last_index_of`
**Current:** Reverse linear scan from end
**Impact:** O(n×m) where n=string length, m=substring length
**Mitigation:** Use Boyer-Moore or similar for large strings. For small strings, current approach is acceptable.

### 4.6 runtime/runtime.c:3348 - Union type name construction
**Current:** Loop building union name with strcat
**Impact:** O(n²) for many union members
**Mitigation:** Pre-calculate total length, single allocation, pointer arithmetic.

### 4.7 runtime/runtime.c:3420 - Function type name construction
**Current:** Building Fn type name with strcpy/strcat
**Impact:** O(n²) for many parameters
**Mitigation:** Same as 4.6.

---

## Category 5: Inefficient Sorting

### 5.1 runtime/profile.c:393 - Bubble sort
**Current:** O(n²) bubble sort for profile entries
**Impact:** Slow for large profile datasets
**Mitigation:** Use qsort with custom comparator.
```c
static int compare_profile_by_time(const void *a, const void *b) {
    int ia = *(const int*)a;
    int ib = *(const int*)b;
    if (g_profile_entries[ia].total_ns < g_profile_entries[ib].total_ns) return 1;
    if (g_profile_entries[ia].total_ns > g_profile_entries[ib].total_ns) return -1;
    return 0;
}

qsort(indices, g_profile_count, sizeof(int), compare_profile_by_time);
```

### 5.2 runtime/profile.c:321 - Index array initialization
**Current:** Simple loop initialization - O(n)
**Impact:** Minor - already optimal
**Mitigation:** None needed, but could use memset-based trick if indices are sequential.

---

## Category 6: Miscellaneous

### 6.1 codegen/codegen.c:6375 - `omni_codegen_emit_region_exits`
**Current:** Placeholder/stub implementation
**Impact:** Region exits not emitted optimally
**Mitigation:** Implement proper region exit tracking during codegen.

### 6.2 analysis/analysis.c:1224 - Ownership determination pass
**Current:** Iterates through all var_usages
**Impact:** O(n) per function - acceptable
**Mitigation:** None needed unless profiling shows bottleneck.

### 6.3 analysis/analysis.c:1669 - `infer_type_from_expr`
**Current:** Switch-based type inference
**Impact:** O(1) per expression
**Mitigation:** None needed - already efficient.

### 6.4 analysis/component.c:44 - CFG node by position lookup
**Current:** Linear scan checking position ranges
**Impact:** O(n) per lookup
**Mitigation:** Same as 1.19 - interval tree or binary search.

### 6.5 parser/pika_c/pika.c:1283 - `handle_precedence`
**Current:** Complex precedence handling logic
**Impact:** Called during grammar processing
**Mitigation:** Review algorithm - may need architectural change.

### 6.6 macro/pattern.c:328 - Ellipsis binding accumulation
**Current:** Linear search for target binding in accumulated results
**Impact:** O(n×m) for n iterations × m bindings
**Mitigation:** Use hash table for bindings during accumulation.

### 6.7 macro/template.c:40 - `collect_template_vars`
**Current:** Recursive traversal with duplicate accumulation
**Impact:** O(n²) due to duplicate checking
**Mitigation:** Use hash set for seen variables.

### 6.8 runtime/regex_compile.c:1142 - `pika_regex_replace`
**Current:** String building for regex replacement
**Impact:** Depends on implementation details
**Mitigation:** Use StringBuilder pattern with growth strategy.

---

## Priority Ranking

### P0 - Critical (Hot Paths)
1. **1.27** - generic.c method dispatch (every function call)
2. **1.8-1.11** - analysis.c var tracking (every variable)
3. **1.15** - type_env_lookup (every type check)
4. **1.20-1.21** - dict operations (every dict access)
5. **4.3-4.4** - string join/replace (common ops)

### P1 - High (Compile-time Performance)
1. **1.1-1.4** - codegen symbol table ops
2. **1.5-1.7** - spec_db lookups
3. **2.8-2.10** - liveness analysis
4. **1.16** - VIG node lookup
5. **5.1** - profile sorting

### P2 - Medium (Correctness/Safety)
1. **3.1-3.5** - Fixed buffer overflows
2. **4.1** - strlen in loop

### P3 - Low (Rare Code Paths)
1. **1.24-1.26** - condition system
2. **1.29-1.34** - macro system
3. **6.x** - miscellaneous

---

## Recommended Hash Table Library

For C, consider using one of:
1. **khash** (klib) - Header-only, type-safe macros, very fast
2. **uthash** - Header-only, attaches to existing structs
3. **Custom** - Simple open-addressing with FNV-1a hash

Example khash setup:
```c
#include "khash.h"

// String -> pointer map
KHASH_MAP_INIT_STR(str_ptr, void*)

// String set
KHASH_SET_INIT_STR(str_set)

// Integer -> pointer map
KHASH_MAP_INIT_INT(int_ptr, void*)
```
