/*@semantic
id: file::profile.c
kind: file
name: profile.c
summary: Performance profiling utilities for OmniLisp runtime.
responsibility:
  - Track function call counts and execution times
  - Identify performance hotspots
  - Track memory allocations per call site
  - Generate profiling reports
inputs:
  - Function calls instrumented via profiling macros
outputs:
  - Profiling statistics and reports
side_effects:
  - global_state (maintains profiling registry)
  - io (prints profiling reports)
related_symbols:
  - omni.h
tags:
  - profiling
  - performance
  - developer-tools
  - issue-27-p4
*/

#include "../include/omni.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Forward declarations */
/* apply is prim_apply in piping.c */

/* ============================================================================
 * Profiling Data Structures
 * ============================================================================ */

#define MAX_PROFILE_ENTRIES 1024
#define MAX_NAME_LENGTH 256

/*@semantic
id: struct::ProfileEntry
kind: struct
name: ProfileEntry
summary: Statistics for a single profiled function/expression.
tags:
  - profiling
  - statistics
*/
typedef struct ProfileEntry {
    char name[MAX_NAME_LENGTH];
    uint64_t call_count;
    uint64_t total_ns;
    uint64_t min_ns;
    uint64_t max_ns;
    size_t alloc_bytes;
    size_t alloc_count;
    bool active;
} ProfileEntry;

/*@semantic
id: global::g_profile_entries
kind: global
name: g_profile_entries
summary: Global array of profiling entries.
tags:
  - profiling
  - registry
*/
static ProfileEntry g_profile_entries[MAX_PROFILE_ENTRIES];
static int g_profile_count = 0;
static bool g_profiling_enabled = false;

/* Current profiling context for nested calls (reserved for future use) */
__attribute__((unused)) static const char* g_current_profile_name = NULL;
__attribute__((unused)) static uint64_t g_current_profile_start = 0;

/* ============================================================================
 * Time Utilities
 * ============================================================================ */

/*@semantic
id: function::get_time_ns
kind: function
name: get_time_ns
summary: Get current time in nanoseconds using clock_gettime.
tags:
  - profiling
  - timing
  - internal
*/
static uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* ============================================================================
 * Profile Entry Management
 * ============================================================================ */

/*@semantic
id: function::find_or_create_entry
kind: function
name: find_or_create_entry
summary: Find existing profile entry by name or create a new one.
tags:
  - profiling
  - internal
*/
static ProfileEntry* find_or_create_entry(const char* name) {
    /* First, look for existing entry */
    for (int i = 0; i < g_profile_count; i++) {
        if (g_profile_entries[i].active &&
            strcmp(g_profile_entries[i].name, name) == 0) {
            return &g_profile_entries[i];
        }
    }

    /* Create new entry if space available */
    if (g_profile_count >= MAX_PROFILE_ENTRIES) {
        return NULL;
    }

    ProfileEntry* entry = &g_profile_entries[g_profile_count++];
    strncpy(entry->name, name, MAX_NAME_LENGTH - 1);
    entry->name[MAX_NAME_LENGTH - 1] = '\0';
    entry->call_count = 0;
    entry->total_ns = 0;
    entry->min_ns = UINT64_MAX;
    entry->max_ns = 0;
    entry->alloc_bytes = 0;
    entry->alloc_count = 0;
    entry->active = true;

    return entry;
}

/*@semantic
id: function::record_profile_sample
kind: function
name: record_profile_sample
summary: Record a single profiling sample with timing data.
tags:
  - profiling
  - internal
*/
static void record_profile_sample(const char* name, uint64_t elapsed_ns) {
    ProfileEntry* entry = find_or_create_entry(name);
    if (!entry) return;

    entry->call_count++;
    entry->total_ns += elapsed_ns;
    if (elapsed_ns < entry->min_ns) entry->min_ns = elapsed_ns;
    if (elapsed_ns > entry->max_ns) entry->max_ns = elapsed_ns;
}

/* ============================================================================
 * Public Profiling API
 * ============================================================================ */

/*@semantic
id: function::prim_profile_enable
kind: function
name: prim_profile_enable
summary: Enable profiling globally.
tags:
  - profiling
  - control
*/
Obj* prim_profile_enable(void) {
    g_profiling_enabled = true;
    return mk_bool(true);
}

/*@semantic
id: function::prim_profile_disable
kind: function
name: prim_profile_disable
summary: Disable profiling globally.
tags:
  - profiling
  - control
*/
Obj* prim_profile_disable(void) {
    g_profiling_enabled = false;
    return mk_bool(false);
}

/*@semantic
id: function::prim_profile_reset
kind: function
name: prim_profile_reset
summary: Reset all profiling data.
tags:
  - profiling
  - control
*/
Obj* prim_profile_reset(void) {
    for (int i = 0; i < g_profile_count; i++) {
        g_profile_entries[i].active = false;
    }
    g_profile_count = 0;
    return mk_nothing();
}

/*@semantic
id: function::prim_profile
kind: function
name: prim_profile
summary: Profile execution of a closure and return result with timing.
tags:
  - profiling
  - execution
*/
Obj* prim_profile(Obj* name_obj, Obj* thunk) {
    const char* name = "anonymous";

    if (name_obj && !IS_IMMEDIATE(name_obj)) {
        if (name_obj->tag == TAG_STRING || name_obj->tag == TAG_SYM) {
            name = (const char*)name_obj->ptr;
        }
    }

    if (!thunk || IS_IMMEDIATE(thunk) || thunk->tag != TAG_CLOSURE) {
        return mk_error("profile: second argument must be a function");
    }

    uint64_t start = get_time_ns();
    Obj* result = prim_apply(thunk, NULL);
    uint64_t elapsed = get_time_ns() - start;

    record_profile_sample(name, elapsed);

    /* Return result with timing info */
    Obj* timing = mk_dict();
    dict_set(timing, mk_keyword("result"), result);
    dict_set(timing, mk_keyword("elapsed-ns"), mk_int((long)elapsed));
    dict_set(timing, mk_keyword("elapsed-ms"), mk_float((double)elapsed / 1000000.0));
    dict_set(timing, mk_keyword("name"), mk_string(name));

    return timing;
}

/*@semantic
id: function::prim_time
kind: function
name: prim_time
summary: Simple timing of a closure execution, prints result.
tags:
  - profiling
  - execution
*/
Obj* prim_time(Obj* thunk) {
    if (!thunk || IS_IMMEDIATE(thunk) || thunk->tag != TAG_CLOSURE) {
        return mk_error("time: argument must be a function");
    }

    uint64_t start = get_time_ns();
    Obj* result = prim_apply(thunk, NULL);
    uint64_t elapsed = get_time_ns() - start;

    double ms = (double)elapsed / 1000000.0;
    if (ms < 1.0) {
        printf("Elapsed: %.3f us\n", (double)elapsed / 1000.0);
    } else if (ms < 1000.0) {
        printf("Elapsed: %.3f ms\n", ms);
    } else {
        printf("Elapsed: %.3f s\n", ms / 1000.0);
    }

    return result;
}

/*@semantic
id: function::prim_call_counts
kind: function
name: prim_call_counts
summary: Return dict of function call counts.
tags:
  - profiling
  - reporting
*/
Obj* prim_call_counts(void) {
    Obj* counts = mk_dict();

    for (int i = 0; i < g_profile_count; i++) {
        if (g_profile_entries[i].active) {
            dict_set(counts,
                    mk_keyword(g_profile_entries[i].name),
                    mk_int((long)g_profile_entries[i].call_count));
        }
    }

    return counts;
}

/*@semantic
id: function::prim_hot_spots
kind: function
name: prim_hot_spots
summary: Return list of hot spots sorted by total time.
tags:
  - profiling
  - reporting
*/
Obj* prim_hot_spots(Obj* limit_obj) {
    int limit = 10;
    if (limit_obj && IS_IMMEDIATE_INT(limit_obj)) {
        limit = (int)INT_IMM_VALUE(limit_obj);
    }
    if (limit > g_profile_count) limit = g_profile_count;
    if (limit <= 0) limit = 10;

    /* Create sorted array of indices by total_ns */
    int* indices = malloc(sizeof(int) * g_profile_count);
    if (!indices) return mk_array(1);

// REVIEWED:NAIVE
    for (int i = 0; i < g_profile_count; i++) {
        indices[i] = i;
    }

    /* Simple bubble sort (good enough for small arrays) */
    for (int i = 0; i < g_profile_count - 1; i++) {
        for (int j = 0; j < g_profile_count - i - 1; j++) {
            if (g_profile_entries[indices[j]].total_ns <
                g_profile_entries[indices[j + 1]].total_ns) {
                int tmp = indices[j];
                indices[j] = indices[j + 1];
                indices[j + 1] = tmp;
            }
        }
    }

    /* Build result array */
    Obj* result = mk_array(limit);
    for (int i = 0; i < limit && i < g_profile_count; i++) {
        ProfileEntry* entry = &g_profile_entries[indices[i]];
        if (!entry->active) continue;

        Obj* item = mk_dict();
        dict_set(item, mk_keyword("name"), mk_string(entry->name));
        dict_set(item, mk_keyword("calls"), mk_int((long)entry->call_count));
        dict_set(item, mk_keyword("total-ms"),
                mk_float((double)entry->total_ns / 1000000.0));
        dict_set(item, mk_keyword("avg-ms"),
                mk_float(entry->call_count > 0 ?
                        (double)entry->total_ns / (double)entry->call_count / 1000000.0 : 0.0));
        dict_set(item, mk_keyword("min-ms"),
                mk_float(entry->min_ns < UINT64_MAX ?
                        (double)entry->min_ns / 1000000.0 : 0.0));
        dict_set(item, mk_keyword("max-ms"),
                mk_float((double)entry->max_ns / 1000000.0));

        array_push(result, item);
    }

    free(indices);
    return result;
}

/*@semantic
id: function::prim_profile_report
kind: function
name: prim_profile_report
summary: Generate and print a detailed profiling report.
tags:
  - profiling
  - reporting
*/
Obj* prim_profile_report(void) {
    printf("\n=== Profiling Report ===\n\n");

    if (g_profile_count == 0) {
        printf("No profiling data collected.\n\n");
        return mk_nothing();
    }

    /* Sort by total time */
    int* indices = malloc(sizeof(int) * g_profile_count);
    if (!indices) {
        printf("Memory allocation failed.\n");
        return mk_nothing();
    }

    for (int i = 0; i < g_profile_count; i++) {
        indices[i] = i;
    }

// REVIEWED:NAIVE
    for (int i = 0; i < g_profile_count - 1; i++) {
        for (int j = 0; j < g_profile_count - i - 1; j++) {
            if (g_profile_entries[indices[j]].total_ns <
                g_profile_entries[indices[j + 1]].total_ns) {
                int tmp = indices[j];
                indices[j] = indices[j + 1];
                indices[j + 1] = tmp;
            }
        }
    }

    /* Calculate total time */
    uint64_t total_time = 0;
    for (int i = 0; i < g_profile_count; i++) {
        if (g_profile_entries[i].active) {
            total_time += g_profile_entries[i].total_ns;
        }
    }

    /* Print header */
    printf("%-30s %10s %12s %12s %10s\n",
           "Function", "Calls", "Total (ms)", "Avg (ms)", "Pct");
    printf("%-30s %10s %12s %12s %10s\n",
           "--------", "-----", "----------", "--------", "---");

    /* Print entries */
    for (int i = 0; i < g_profile_count; i++) {
        ProfileEntry* entry = &g_profile_entries[indices[i]];
        if (!entry->active) continue;

        double total_ms = (double)entry->total_ns / 1000000.0;
        double avg_ms = entry->call_count > 0 ?
                        total_ms / (double)entry->call_count : 0.0;
        double pct = total_time > 0 ?
                     (double)entry->total_ns * 100.0 / (double)total_time : 0.0;

        printf("%-30.30s %10lu %12.3f %12.3f %9.1f%%\n",
               entry->name,
               (unsigned long)entry->call_count,
               total_ms,
               avg_ms,
               pct);
    }

    printf("\n");
    free(indices);
    return mk_nothing();
}

/*@semantic
id: function::prim_profile_entry
kind: function
name: prim_profile_entry
summary: Get profiling data for a specific function.
tags:
  - profiling
  - introspection
*/
Obj* prim_profile_entry(Obj* name_obj) {
    if (!name_obj || IS_IMMEDIATE(name_obj)) {
        return mk_error("profile-entry: name must be a string");
    }

    const char* name = NULL;
    if (name_obj->tag == TAG_STRING || name_obj->tag == TAG_SYM) {
        name = (const char*)name_obj->ptr;
    } else {
        return mk_error("profile-entry: name must be a string");
    }

    for (int i = 0; i < g_profile_count; i++) {
        if (g_profile_entries[i].active &&
            strcmp(g_profile_entries[i].name, name) == 0) {
            ProfileEntry* entry = &g_profile_entries[i];

            Obj* result = mk_dict();
            dict_set(result, mk_keyword("name"), mk_string(entry->name));
            dict_set(result, mk_keyword("calls"), mk_int((long)entry->call_count));
            dict_set(result, mk_keyword("total-ns"), mk_int((long)entry->total_ns));
            dict_set(result, mk_keyword("total-ms"),
                    mk_float((double)entry->total_ns / 1000000.0));
            dict_set(result, mk_keyword("avg-ns"),
                    mk_int(entry->call_count > 0 ?
                          (long)(entry->total_ns / entry->call_count) : 0));
            dict_set(result, mk_keyword("min-ns"),
                    mk_int(entry->min_ns < UINT64_MAX ? (long)entry->min_ns : 0));
            dict_set(result, mk_keyword("max-ns"), mk_int((long)entry->max_ns));

            return result;
        }
    }

    return mk_nothing();  /* Not found */
}

/*@semantic
id: function::prim_benchmark
kind: function
name: prim_benchmark
summary: Run a function multiple times and return timing statistics.
tags:
  - profiling
  - benchmarking
*/
Obj* prim_benchmark(Obj* iterations_obj, Obj* thunk) {
    if (!iterations_obj || !IS_IMMEDIATE_INT(iterations_obj)) {
        return mk_error("benchmark: first argument must be an integer");
    }
    if (!thunk || IS_IMMEDIATE(thunk) || thunk->tag != TAG_CLOSURE) {
        return mk_error("benchmark: second argument must be a function");
    }

    long iterations = INT_IMM_VALUE(iterations_obj);
    if (iterations <= 0) iterations = 1;

    uint64_t total_ns = 0;
    uint64_t min_ns = UINT64_MAX;
    uint64_t max_ns = 0;

    printf("Running %ld iterations...\n", iterations);

    for (long i = 0; i < iterations; i++) {
        uint64_t start = get_time_ns();
        prim_apply(thunk, NULL);
        uint64_t elapsed = get_time_ns() - start;

        total_ns += elapsed;
        if (elapsed < min_ns) min_ns = elapsed;
        if (elapsed > max_ns) max_ns = elapsed;
    }

    double avg_ns = (double)total_ns / (double)iterations;
    double avg_ms = avg_ns / 1000000.0;

    printf("Benchmark complete:\n");
    printf("  Iterations: %ld\n", iterations);
    printf("  Total: %.3f ms\n", (double)total_ns / 1000000.0);
    printf("  Average: %.3f ms (%.0f ns)\n", avg_ms, avg_ns);
    printf("  Min: %.3f ms\n", (double)min_ns / 1000000.0);
    printf("  Max: %.3f ms\n", (double)max_ns / 1000000.0);

    Obj* result = mk_dict();
    dict_set(result, mk_keyword("iterations"), mk_int(iterations));
    dict_set(result, mk_keyword("total-ms"), mk_float((double)total_ns / 1000000.0));
    dict_set(result, mk_keyword("avg-ms"), mk_float(avg_ms));
    dict_set(result, mk_keyword("avg-ns"), mk_float(avg_ns));
    dict_set(result, mk_keyword("min-ms"), mk_float((double)min_ns / 1000000.0));
    dict_set(result, mk_keyword("max-ms"), mk_float((double)max_ns / 1000000.0));

    return result;
}

/*@semantic
id: function::prim_profile_memory
kind: function
name: prim_profile_memory
summary: Profile memory allocation of a closure execution.
tags:
  - profiling
  - memory
*/
Obj* prim_profile_memory(Obj* thunk) {
    if (!thunk || IS_IMMEDIATE(thunk) || thunk->tag != TAG_CLOSURE) {
        return mk_error("profile-memory: argument must be a function");
    }

    /* Note: This is a placeholder. Full memory profiling would require
     * hooking into the allocator, which is complex. For now, we just
     * time the execution and note that memory profiling is limited. */

    uint64_t start = get_time_ns();
    Obj* result = prim_apply(thunk, NULL);
    uint64_t elapsed = get_time_ns() - start;

    Obj* profile = mk_dict();
    dict_set(profile, mk_keyword("result"), result);
    dict_set(profile, mk_keyword("elapsed-ms"), mk_float((double)elapsed / 1000000.0));
    dict_set(profile, mk_keyword("note"),
            mk_string("Full memory profiling requires allocator hooks"));

    return profile;
}

/*@semantic
id: function::prim_profiling_enabled_p
kind: function
name: prim_profiling_enabled_p
summary: Check if profiling is enabled.
tags:
  - profiling
  - introspection
*/
Obj* prim_profiling_enabled_p(void) {
    return mk_bool(g_profiling_enabled);
}

/*@semantic
id: function::prim_profile_count
kind: function
name: prim_profile_count
summary: Return number of profiled entries.
tags:
  - profiling
  - introspection
*/
Obj* prim_profile_count(void) {
    return mk_int(g_profile_count);
}
