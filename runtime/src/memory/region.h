/*
 * Region References - Vale/Ada/SPARK-style scope hierarchy validation
 *
 * Key invariant: A pointer cannot point to an object in a more deeply scoped region.
 * This prevents dangling references when inner scopes exit.
 *
 * Region hierarchy:
 *   Region A (depth 0)
 *     └── Region B (depth 1)
 *           └── Region C (depth 2)
 *
 * Allowed: C → B, C → A, B → A (inner can point to outer)
 * Forbidden: A → B, A → C, B → C (outer cannot point to inner)
 */

#ifndef REGION_H
#define REGION_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "component.h"

/* Region ID type */
typedef uint64_t RegionID;

/* Region depth (0 = outermost) */
typedef uint32_t RegionDepth;

/* Forward declarations */
typedef struct Region Region;
typedef struct RegionObj RegionObj;
typedef struct RegionRef RegionRef;
typedef struct RegionContext RegionContext;

/* Region - an isolated memory region with scope hierarchy */
struct Region {
    RegionID id;
    RegionDepth depth;
    Region* parent;
    Region** children;
    int child_count;
    int child_capacity;
    RegionObj** objects;
    int object_count;
    int object_capacity;
    
    /* Component Tracking (Unified Region-Based Memory) */
    SymComponent** components;
    int component_count;
    int component_capacity;
    
    bool closed;
};

/* RegionObj - an object allocated within a region */
struct RegionObj {
    Region* region;
    void* data;
    void (*destructor)(void*);
    RegionRef** refs;
    int ref_count;
    int ref_capacity;
};

/* RegionRef - a reference that carries region information */
struct RegionRef {
    RegionObj* target;
    Region* source_region;
};

/* RegionContext - manages the region hierarchy */
struct RegionContext {
    Region* root;
    Region* current;
};

/* Error codes */
typedef enum {
    REGION_OK = 0,
    REGION_ERR_NULL = -1,
    REGION_ERR_CLOSED = -2,
    REGION_ERR_SCOPE_VIOLATION = -3,
    REGION_ERR_CANNOT_EXIT_ROOT = -4,
    REGION_ERR_ALLOC_FAILED = -5
} RegionError;

/* Context management */
RegionContext* region_context_new(void);
void region_context_free(RegionContext* ctx);

/* Region operations */
Region* region_enter(RegionContext* ctx);
RegionError region_exit(RegionContext* ctx);
RegionDepth region_get_depth(Region* r);
int region_get_object_count(Region* r);
bool region_is_closed(Region* r);

/* Object operations */
RegionObj* region_alloc(RegionContext* ctx, void* data, void (*destructor)(void*));
SymComponent* region_alloc_component(Region* r);
RegionError region_create_ref(RegionContext* ctx, RegionObj* source, RegionObj* target, RegionRef** out_ref);

/* Region-RC Fusion Operations */
void region_inc_ref(Region* r, void* obj);
void region_dec_ref(Region* r, void* obj);

/* Reference operations */
void* region_ref_deref(RegionRef* ref, RegionError* err);
bool region_ref_is_valid(RegionRef* ref);

/* Utility functions */
bool region_can_reference(RegionObj* source, RegionObj* target);
bool region_is_ancestor(Region* ancestor, Region* descendant);

/* ============== Linear Regions (Backlog 1a) ============== */
/*
 * Linear regions guarantee single ownership:
 * - Objects allocated in a linear region cannot escape
 * - Perfect for FFI: pass contiguous memory, no GC interaction
 * - Bulk deallocation when region closes
 */

typedef struct LinearRegion {
    void* base;              /* Base address of contiguous memory block */
    size_t size;             /* Total size of region */
    size_t used;             /* Bytes currently used */
    bool frozen;             /* True if no more allocations allowed */
} LinearRegion;

/* Create a new linear region with given size */
LinearRegion* linear_region_new(size_t size);

/* Allocate within a linear region (returns offset from base) */
void* linear_region_alloc(LinearRegion* r, size_t size, size_t alignment);

/* Get remaining space in linear region */
size_t linear_region_remaining(LinearRegion* r);

/* Freeze the region (no more allocations, ready for FFI) */
void linear_region_freeze(LinearRegion* r);

/* Free the entire linear region */
void linear_region_free(LinearRegion* r);

/* ============== Offset Regions (Backlog 1b) ============== */
/*
 * Offset regions use relative pointers:
 * - All pointers are offsets from region base
 * - Data can be memmove'd, mmapped, or serialized without fixups
 * - Perfect for IPC, disk serialization, network transmission
 */

typedef int32_t OffsetPtr;  /* Relative pointer as offset from region base */

#define OFFSET_NULL 0       /* Null offset pointer */

typedef struct OffsetRegion {
    void* base;              /* Base address of memory block */
    size_t size;             /* Total size */
    size_t used;             /* Bytes used */
    int32_t* reloc_table;    /* Relocation entries for offset pointers */
    size_t reloc_count;
    size_t reloc_capacity;
} OffsetRegion;

/* Create new offset region */
OffsetRegion* offset_region_new(size_t size);

/* Allocate within offset region, returns offset from base */
OffsetPtr offset_region_alloc(OffsetRegion* r, size_t size, size_t alignment);

/* Store an offset pointer (records relocation for later) */
void offset_region_store_ptr(OffsetRegion* r, OffsetPtr* dest, OffsetPtr target);

/* Convert offset to absolute pointer (for reading) */
void* offset_to_ptr(OffsetRegion* r, OffsetPtr offset);

/* Convert absolute pointer to offset (for writing) */
OffsetPtr ptr_to_offset(OffsetRegion* r, void* ptr);

/* Serialize region to buffer (ready for FFI/disk/network) */
void* offset_region_serialize(OffsetRegion* r, size_t* out_size);

/* Deserialize region from buffer (creates new region) */
OffsetRegion* offset_region_deserialize(void* data, size_t size);

/* Free offset region */
void offset_region_free(OffsetRegion* r);

/* ============== IRegion Vtable (Backlog 2) ============== */
/*
 * Pluggable region backends via vtable interface.
 * Allows different allocation strategies to be used interchangeably:
 * - Arena: Bulk allocation, bulk free (cyclic structures)
 * - Linear: Bump allocation, FFI-friendly
 * - Offset: Position-independent, serializable
 * - Scoped: Scope-hierarchy with reference validation
 *
 * Inspired by Vale's IRegion interface.
 */

/* Region kind enumeration */
typedef enum {
    REGION_KIND_ARENA,      /* Bulk alloc/free, no individual frees */
    REGION_KIND_LINEAR,     /* Bump allocator, freeze-then-use */
    REGION_KIND_OFFSET,     /* Position-independent with relocation */
    REGION_KIND_SCOPED,     /* Scope hierarchy with ref validation */
    REGION_KIND_POOL,       /* Fixed-size object pool */
    REGION_KIND_CUSTOM      /* User-defined backend */
} RegionKind;

/* Forward declaration */
typedef struct IRegion IRegion;

/* Region vtable - function pointers for region operations */
typedef struct IRegionVtable {
    /* Allocate memory of given size and alignment */
    void* (*alloc)(IRegion* self, size_t size, size_t alignment);

    /* Free a single allocation (may be no-op for arena/linear) */
    void (*free_one)(IRegion* self, void* ptr);

    /* Free entire region (bulk deallocation) */
    void (*free_all)(IRegion* self);

    /* Query remaining capacity (0 if unlimited) */
    size_t (*remaining)(IRegion* self);

    /* Freeze region (no more allocations allowed) */
    void (*freeze)(IRegion* self);

    /* Check if region is frozen */
    bool (*is_frozen)(IRegion* self);

    /* Get region kind */
    RegionKind (*kind)(IRegion* self);

    /* Clone region (deep copy for isolation) */
    IRegion* (*clone)(IRegion* self);

    /* Serialize region to buffer (optional, returns NULL if unsupported) */
    void* (*serialize)(IRegion* self, size_t* out_size);

    /* Get statistics (allocations, bytes used, etc.) */
    void (*stats)(IRegion* self, size_t* alloc_count, size_t* bytes_used, size_t* bytes_total);
} IRegionVtable;

/* IRegion - abstract region interface */
struct IRegion {
    const IRegionVtable* vtable;
    void* impl;              /* Pointer to concrete implementation */
    RegionKind kind;
    bool frozen;
    size_t alloc_count;      /* Number of allocations */
    size_t bytes_used;       /* Bytes currently used */
    size_t bytes_total;      /* Total capacity */
};

/* IRegion lifecycle */
IRegion* iregion_new_arena(size_t initial_size);
IRegion* iregion_new_linear(size_t size);
IRegion* iregion_new_offset(size_t size);
IRegion* iregion_new_scoped(void);
IRegion* iregion_new_pool(size_t object_size, size_t count);

/* IRegion operations (dispatch through vtable) */
void* iregion_alloc(IRegion* r, size_t size, size_t alignment);
void iregion_free_one(IRegion* r, void* ptr);
void iregion_free_all(IRegion* r);
size_t iregion_remaining(IRegion* r);
void iregion_freeze(IRegion* r);
bool iregion_is_frozen(IRegion* r);
RegionKind iregion_kind(IRegion* r);
IRegion* iregion_clone(IRegion* r);
void* iregion_serialize(IRegion* r, size_t* out_size);
void iregion_stats(IRegion* r, size_t* alloc_count, size_t* bytes_used, size_t* bytes_total);

/* Convenience macro for typed allocation */
#define IREGION_ALLOC(r, type) \
    ((type*)iregion_alloc((r), sizeof(type), _Alignof(type)))

#define IREGION_ALLOC_ARRAY(r, type, count) \
    ((type*)iregion_alloc((r), sizeof(type) * (count), _Alignof(type)))

/* ============== Arena Region (for IRegion) ============== */
/*
 * Arena allocator: fast bump allocation, bulk deallocation.
 * Perfect for cyclic structures where individual frees are impossible.
 */

#include "arena.h"

typedef struct ArenaRegion {
    ArenaBlock* head;        /* First block */
    ArenaBlock* current;     /* Current block for allocation */
    size_t default_block_size;
    size_t total_allocated;
    size_t alloc_count;
    bool frozen;
} ArenaRegion;

/* Arena-specific functions */
ArenaRegion* arena_region_new(size_t initial_size);
void* arena_region_alloc(ArenaRegion* r, size_t size, size_t alignment);
void arena_region_reset(ArenaRegion* r);  /* Reset without freeing memory */
void arena_region_free(ArenaRegion* r);

/* ============== Pool Region (for IRegion) ============== */
/*
 * Fixed-size object pool: O(1) alloc/free, no fragmentation.
 * Perfect for homogeneous allocations (e.g., all AST nodes).
 */

typedef struct PoolRegion {
    void* data;              /* Contiguous memory block */
    void* free_list;         /* Linked list of free slots */
    size_t object_size;      /* Size of each object */
    size_t alignment;        /* Alignment requirement */
    size_t capacity;         /* Total number of slots */
    size_t used;             /* Number of allocated slots */
    bool frozen;
} PoolRegion;

/* Pool-specific functions */
PoolRegion* pool_region_new(size_t object_size, size_t count);
void* pool_region_alloc(PoolRegion* r);
void pool_region_free_one(PoolRegion* r, void* ptr);
void pool_region_reset(PoolRegion* r);
void pool_region_free(PoolRegion* r);

/* ============== Weak Reference Control Blocks (Backlog 3) ============== */
/*
 * Control block approach for weak references:
 * - Control block is a separate allocation tracking object lifecycle
 * - Weak refs point to control block, not directly to object
 * - O(1) invalidation when target is freed (just mark control block)
 * - Control block freed when last weak ref is dropped
 *
 * Memory layout:
 *   WeakControlBlock -----> target object
 *        ^
 *        |
 *   WeakHandle ----+
 *   WeakHandle ----+
 *   ...
 */

/* Forward declarations */
typedef struct WeakControlBlock WeakControlBlock;
typedef struct WeakHandle WeakHandle;

/* Control block states */
typedef enum {
    WEAK_CB_ALIVE,      /* Target is still valid */
    WEAK_CB_DEAD,       /* Target has been freed */
    WEAK_CB_DETACHED    /* Control block orphaned (for debugging) */
} WeakControlBlockState;

/* Control block - tracks a single object */
struct WeakControlBlock {
    void* target;                   /* Pointer to tracked object */
    _Atomic uint32_t weak_count;    /* Number of weak handles pointing here */
    _Atomic uint32_t state;         /* WeakControlBlockState */
    void (*destructor)(void*);      /* Optional destructor for target */
    uint64_t generation;            /* Generation counter for ABA protection */
    bool heap_allocated;            /* True if this cb was malloc'd (not from table) */
};

/* Weak handle - user-facing weak reference */
struct WeakHandle {
    WeakControlBlock* cb;           /* Pointer to control block */
    uint64_t generation;            /* Generation at time of creation */
};

/* Control block operations */
WeakControlBlock* weak_cb_new(void* target, void (*destructor)(void*));
void weak_cb_inc_ref(WeakControlBlock* cb);
void weak_cb_dec_ref(WeakControlBlock* cb);
void weak_cb_invalidate(WeakControlBlock* cb);
bool weak_cb_is_valid(WeakControlBlock* cb);
void* weak_cb_get_target(WeakControlBlock* cb);

/* Weak handle operations */
WeakHandle* weak_handle_new(WeakControlBlock* cb);
WeakHandle* weak_handle_clone(WeakHandle* h);
void weak_handle_free(WeakHandle* h);
bool weak_handle_is_valid(WeakHandle* h);
void* weak_handle_lock(WeakHandle* h);  /* Returns target if valid, NULL otherwise */

/* Convenience: create object with control block */
typedef struct {
    void* object;
    WeakControlBlock* cb;
} WeakableObject;

WeakableObject weak_object_new(void* object, void (*destructor)(void*));
void weak_object_free(WeakableObject* wo);

/* ============== Weak Reference Table (Backlog 3b) ============== */
/*
 * Global table for efficient weak reference management.
 * Uses slot-based allocation for control blocks.
 */

typedef struct WeakRefTable {
    WeakControlBlock* blocks;       /* Array of control blocks */
    uint32_t* free_list;            /* Stack of free indices */
    size_t capacity;                /* Total slots */
    size_t free_count;              /* Available slots */
    size_t used;                    /* Used slots */
    _Atomic uint64_t next_gen;      /* Generation counter */
} WeakRefTable;

WeakRefTable* weak_table_new(size_t capacity);
void weak_table_free(WeakRefTable* table);
uint32_t weak_table_register(WeakRefTable* table, void* target, void (*destructor)(void*));
void weak_table_invalidate(WeakRefTable* table, uint32_t index);
WeakHandle* weak_table_get_handle(WeakRefTable* table, uint32_t index);
void* weak_table_lock(WeakRefTable* table, uint32_t index, uint64_t generation);
void weak_table_release(WeakRefTable* table, uint32_t index);

/* Global default table */
WeakRefTable* weak_table_global(void);

/* ============== Transmigration/Isolation (Backlog 4) ============== */
/*
 * Transmigration: Deep copy object graph from one region to another.
 * Isolation: Ensure object and dependencies are isolated in single region.
 *
 * Use cases:
 * - Object escaping its original region (return from function)
 * - Sending object between threads (message passing)
 * - Serializing object graph to persistent region
 *
 * Key invariant: After transmigration, all pointers in the copied graph
 * point within the destination region (no dangling refs to source).
 */

/* Transmigration context - tracks object mapping during copy */
typedef struct TransmigrationContext TransmigrationContext;

/* Transmigration error codes */
typedef enum {
    TRANSMIGRATE_OK = 0,
    TRANSMIGRATE_ERR_NULL = -1,
    TRANSMIGRATE_ERR_ALLOC_FAILED = -2,
    TRANSMIGRATE_ERR_CYCLE_DETECTED = -3,
    TRANSMIGRATE_ERR_UNSUPPORTED_TYPE = -4,
    TRANSMIGRATE_ERR_REGION_CLOSED = -5
} TransmigrationError;

/* Object visitor function for transmigration */
typedef void* (*TransmigrationVisitor)(
    TransmigrationContext* ctx,
    void* source_obj,
    TransmigrationError* err
);

/* Transmigration context operations */
TransmigrationContext* transmigration_new(IRegion* dest);
void transmigration_free(TransmigrationContext* ctx);

/* Register visitor for custom types */
void transmigration_register_visitor(
    TransmigrationContext* ctx,
    int type_tag,
    TransmigrationVisitor visitor
);

/* Core transmigration function */
void* transmigrate(TransmigrationContext* ctx, void* source, TransmigrationError* err);

/* Lookup already-copied object (for cycle handling) */
void* transmigration_lookup(TransmigrationContext* ctx, void* source);

/* Record object mapping (source -> dest) */
void transmigration_record(TransmigrationContext* ctx, void* source, void* dest);

/* ============== Isolation Checking ============== */
/*
 * Isolation check: Verify an object graph is fully contained in a region.
 * Returns true if all reachable objects are in the same region.
 */

typedef struct IsolationResult {
    bool is_isolated;           /* True if fully isolated */
    int escape_count;           /* Number of escaping references */
    void** escaping_refs;       /* Array of escaping pointers (if any) */
    int escaping_capacity;
} IsolationResult;

/* Check if object graph is isolated to a region */
IsolationResult* check_isolation(void* root, IRegion* expected_region);
void isolation_result_free(IsolationResult* result);

/* ============== Region-Bound References ============== */
/*
 * Region-bound reference: A reference that carries its region context.
 * Used for safe cross-region references with validation.
 */

typedef struct RegionBoundRef {
    void* target;               /* Target object */
    IRegion* region;            /* Region the target belongs to */
    uint64_t region_epoch;      /* Epoch for staleness detection */
} RegionBoundRef;

RegionBoundRef* region_bound_ref_new(void* target, IRegion* region);
void region_bound_ref_free(RegionBoundRef* ref);
void* region_bound_ref_deref(RegionBoundRef* ref);
bool region_bound_ref_is_valid(RegionBoundRef* ref);

/* ============== External Handle Indexing (Backlog 5) ============== */
/*
 * External handles for FFI and deterministic object identity.
 *
 * Features:
 * - Stable integer IDs for objects (not raw pointers)
 * - Deterministic allocation order for replay/debugging
 * - Safe invalidation when objects are freed
 * - Lookup by ID for FFI callbacks
 *
 * Use cases:
 * - FFI: Give external C code stable handles to OmniLisp objects
 * - Serialization: Deterministic object IDs across runs
 * - Debugging: Stable identifiers for logging/tracing
 * - IPC: Pass object references between processes
 */

/* Handle type - 32-bit index with 32-bit generation */
typedef uint64_t ExternalHandle;

#define EXTERNAL_HANDLE_INVALID 0
#define EXTERNAL_HANDLE_INDEX(h) ((uint32_t)((h) & 0xFFFFFFFF))
#define EXTERNAL_HANDLE_GEN(h) ((uint32_t)(((h) >> 32) & 0xFFFFFFFF))
#define EXTERNAL_HANDLE_MAKE(idx, gen) (((uint64_t)(gen) << 32) | (uint64_t)(idx))

/* External handle slot */
typedef struct ExternalHandleSlot {
    void* object;               /* Pointer to managed object */
    uint32_t generation;        /* Generation for ABA protection */
    bool in_use;                /* True if slot is occupied */
    void (*destructor)(void*);  /* Optional destructor */
} ExternalHandleSlot;

/* External handle table */
typedef struct ExternalHandleTable {
    ExternalHandleSlot* slots;  /* Array of slots */
    size_t capacity;            /* Total slots */
    size_t used;                /* Used slots */
    uint32_t* free_stack;       /* Stack of free indices */
    size_t free_count;          /* Number of free slots */
    uint64_t next_id;           /* For deterministic ID allocation */
    bool deterministic;         /* If true, allocate in order */
} ExternalHandleTable;

/* Table lifecycle */
ExternalHandleTable* external_table_new(size_t capacity);
void external_table_free(ExternalHandleTable* table);

/* Set deterministic mode (allocate handles in order) */
void external_table_set_deterministic(ExternalHandleTable* table, bool deterministic);

/* Handle operations */
ExternalHandle external_handle_create(ExternalHandleTable* table, void* object, void (*destructor)(void*));
void external_handle_release(ExternalHandleTable* table, ExternalHandle handle);
void* external_handle_get(ExternalHandleTable* table, ExternalHandle handle);
bool external_handle_is_valid(ExternalHandleTable* table, ExternalHandle handle);

/* Bulk operations */
void external_table_clear(ExternalHandleTable* table);
size_t external_table_count(ExternalHandleTable* table);
void external_table_iterate(ExternalHandleTable* table, void (*callback)(ExternalHandle, void*, void*), void* user_data);

/* Global default table */
ExternalHandleTable* external_table_global(void);

/* FFI convenience functions */
uint64_t ffi_obj_to_handle(void* obj);
void* ffi_handle_to_obj(uint64_t handle);
void ffi_release_handle(uint64_t handle);

#endif /* REGION_H */
