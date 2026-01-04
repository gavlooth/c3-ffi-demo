/*
 * Region References implementation
 * Vale/Ada/SPARK-style scope hierarchy validation
 */

#include "region.h"
#include <stdlib.h>
#include <stdatomic.h>
#include <limits.h>
#include <string.h>

/* Global region ID counter */
static atomic_uint_fast64_t next_region_id = 1;

/* Helper: grow array */
static void* grow_array(void* arr, int* capacity, size_t elem_size) {
    int new_cap;
    if (*capacity == 0) {
        new_cap = 4;
    } else {
        /* Check for integer overflow before doubling */
        if (*capacity > INT_MAX / 2) {
            return NULL;  /* Cannot grow - would overflow */
        }
        new_cap = *capacity * 2;
    }
    void* new_arr = realloc(arr, new_cap * elem_size);
    if (new_arr) {
        *capacity = new_cap;
    }
    return new_arr;
}

/* Create a new region */
static Region* region_new(Region* parent) {
    Region* r = calloc(1, sizeof(Region));
    if (!r) return NULL;

    r->id = atomic_fetch_add(&next_region_id, 1);
    r->depth = parent ? parent->depth + 1 : 0;
    r->parent = parent;
    r->children = NULL;
    r->child_count = 0;
    r->child_capacity = 0;
    r->objects = NULL;
    r->object_count = 0;
    r->object_capacity = 0;
    
    r->components = NULL;
    r->component_count = 0;
    r->component_capacity = 0;
    
    r->closed = false;

    return r;
}

/* Free a region (internal) */
static void region_destroy(Region* r) {
    if (!r) return;

    /* Free all objects */
    for (int i = 0; i < r->object_count; i++) {
        RegionObj* obj = r->objects[i];
        if (obj) {
            if (obj->destructor && obj->data) {
                obj->destructor(obj->data);
            }
            /* Free refs array */
            free(obj->refs);
            free(obj);
        }
    }
    free(r->objects);
    
    /* Free all components (Unified Region-Based Memory) */
    /* Components are dismantled implicitly when the region dies */
    for (int i = 0; i < r->component_count; i++) {
        SymComponent* c = r->components[i];
        if (c) {
            /* We can just free members list and the component itself.
             * No need to dismantle edges because everything in the region is dead. */
            if (c->members) free(c->members);
            /* SymComponent headers are usually pool-allocated if using sym_component_new.
             * But here we might want region-allocated headers?
             * For Phase 2, we'll assume they are standard pool-allocated and need
             * to be returned to the pool or freed if malloc'd.
             * If region_alloc_component uses region memory, we don't free 'c'.
             */
             /* Wait, if allocated IN the region, we don't free 'c' here. */
        }
    }
    free(r->components);

    /* Free children recursively */
    for (int i = 0; i < r->child_count; i++) {
        region_destroy(r->children[i]);
    }
    free(r->children);

    free(r);
}

/* Create new context */
RegionContext* region_context_new(void) {
    RegionContext* ctx = calloc(1, sizeof(RegionContext));
    if (!ctx) return NULL;

    ctx->root = region_new(NULL);
    if (!ctx->root) {
        free(ctx);
        return NULL;
    }

    ctx->current = ctx->root;
    return ctx;
}

/* Free context and all regions */
void region_context_free(RegionContext* ctx) {
    if (!ctx) return;
    region_destroy(ctx->root);
    free(ctx);
}

/* Enter a new child region */
Region* region_enter(RegionContext* ctx) {
    if (!ctx || !ctx->current) return NULL;

    Region* child = region_new(ctx->current);
    if (!child) return NULL;

    /* Add to parent's children */
    if (ctx->current->child_count >= ctx->current->child_capacity) {
        Region** new_children = grow_array(
            ctx->current->children,
            &ctx->current->child_capacity,
            sizeof(Region*)
        );
        if (!new_children) {
            free(child);
            return NULL;
        }
        ctx->current->children = new_children;
    }
    ctx->current->children[ctx->current->child_count++] = child;

    ctx->current = child;
    return child;
}

/* Exit current region */
RegionError region_exit(RegionContext* ctx) {
    if (!ctx || !ctx->current) return REGION_ERR_NULL;
    if (ctx->current == ctx->root) return REGION_ERR_CANNOT_EXIT_ROOT;
    if (ctx->current->closed) return REGION_ERR_CLOSED;

    /* Mark as closed and invalidate all objects */
    ctx->current->closed = true;
    for (int i = 0; i < ctx->current->object_count; i++) {
        RegionObj* obj = ctx->current->objects[i];
        if (obj) {
            obj->region = NULL;  /* Mark as invalid */
        }
    }

    /* Return to parent */
    ctx->current = ctx->current->parent;
    return REGION_OK;
}

/* Allocate object in current region */
RegionObj* region_alloc(RegionContext* ctx, void* data, void (*destructor)(void*)) {
    if (!ctx || !ctx->current) return NULL;
    if (ctx->current->closed) return NULL;

    RegionObj* obj = calloc(1, sizeof(RegionObj));
    if (!obj) return NULL;

    obj->region = ctx->current;
    obj->data = data;
    obj->destructor = destructor;
    obj->refs = NULL;
    obj->ref_count = 0;
    obj->ref_capacity = 0;

    /* Add to region's objects */
    if (ctx->current->object_count >= ctx->current->object_capacity) {
        RegionObj** new_objects = grow_array(
            ctx->current->objects,
            &ctx->current->object_capacity,
            sizeof(RegionObj*)
        );
        if (!new_objects) {
            free(obj);
            return NULL;
        }
        ctx->current->objects = new_objects;
    }
    ctx->current->objects[ctx->current->object_count++] = obj;

    return obj;
}

/* Create reference from source to target */
RegionError region_create_ref(RegionContext* ctx, RegionObj* source, RegionObj* target, RegionRef** out_ref) {
    (void)ctx;  /* Unused but kept for API consistency */

    if (!source || !target || !out_ref) return REGION_ERR_NULL;
    if (!source->region) return REGION_ERR_CLOSED;
    if (!target->region) return REGION_ERR_CLOSED;

    /* Key check: source cannot point to more deeply scoped target */
    /* Allowed: inner → outer (source.depth >= target.depth) */
    /* Forbidden: outer → inner (source.depth < target.depth) */
    if (source->region->depth < target->region->depth) {
        return REGION_ERR_SCOPE_VIOLATION;
    }

    RegionRef* ref = calloc(1, sizeof(RegionRef));
    if (!ref) return REGION_ERR_ALLOC_FAILED;

    ref->target = target;
    ref->source_region = source->region;

    /* Add to source's refs */
    if (source->ref_count >= source->ref_capacity) {
        RegionRef** new_refs = grow_array(
            source->refs,
            &source->ref_capacity,
            sizeof(RegionRef*)
        );
        if (!new_refs) {
            free(ref);
            return REGION_ERR_ALLOC_FAILED;
        }
        source->refs = new_refs;
    }
    source->refs[source->ref_count++] = ref;

    *out_ref = ref;
    return REGION_OK;
}

/* Dereference safely */
void* region_ref_deref(RegionRef* ref, RegionError* err) {
    if (!ref || !ref->target) {
        if (err) *err = REGION_ERR_NULL;
        return NULL;
    }
    if (!ref->target->region) {
        if (err) *err = REGION_ERR_CLOSED;
        return NULL;
    }

    if (err) *err = REGION_OK;
    return ref->target->data;
}

/* Check if reference is valid */
bool region_ref_is_valid(RegionRef* ref) {
    return ref && ref->target && ref->target->region && !ref->target->region->closed;
}

/* Check if source can reference target */
bool region_can_reference(RegionObj* source, RegionObj* target) {
    if (!source || !target) return false;
    if (!source->region || !target->region) return false;
    return source->region->depth >= target->region->depth;
}

/* Check if ancestor is ancestor of descendant */
bool region_is_ancestor(Region* ancestor, Region* descendant) {
    Region* current = descendant;
    while (current) {
        if (current == ancestor) return true;
        current = current->parent;
    }
    return false;
}

/* Get region depth */
RegionDepth region_get_depth(Region* r) {
    return r ? r->depth : 0;
}

/* Get object count */
int region_get_object_count(Region* r) {
    return r ? r->object_count : 0;
}

/* Check if closed */
bool region_is_closed(Region* r) {
    return r ? r->closed : true;
}

/* ============== Unified Region-Based Memory (Phase 2) ============== */

SymComponent* region_alloc_component(Region* r) {
    if (!r || r->closed) return NULL;
    
    /* Allocate component using standard pool for now */
    SymComponent* c = sym_component_new();
    if (!c) return NULL;
    
    /* Register with region */
    if (r->component_count >= r->component_capacity) {
        SymComponent** new_comps = grow_array(
            r->components,
            &r->component_capacity,
            sizeof(SymComponent*)
        );
        if (!new_comps) {
            /* If we can't track it, we can't enforce region semantics. 
             * But we can still return it as a loose component.
             * However, for safety, let's fail or handle gracefully.
             */
             r->components = new_comps; /* Updates if success */
        }
        if (new_comps) {
            r->components = new_comps;
        }
    }
    
    if (r->components && r->component_count < r->component_capacity) {
        r->components[r->component_count++] = c;
    }
    
    /* In a full unified system, we'd give the Region a handle to the component */
    sym_acquire_handle(c); 
    
    return c;
}

/* Region-RC Fusion: Increment only if cross-region */
void region_inc_ref(Region* r, void* obj) {
    /* Need a way to check if obj is in region r.
     * For RegionObj, we have obj->region.
     * For standard Obj, we don't have a region pointer unless we add it.
     * 
     * Assumption: This function is called for RegionObj* or compatible.
     */
    RegionObj* ro = (RegionObj*)obj;
    if (!ro || !r) return;
    
    if (ro->region != r) {
        /* External reference - increment region's external RC (if we tracked it)
         * or the object's ref count if it's a ref-counted object.
         * For RegionObj, ref_count is local? No, standard RegionObj doesn't have RC.
         * It has external_rc if we added it in Phase 7.
         * 
         * Let's assume this is for mixing RegionObj with standard RC.
         */
         /* Placeholder: no-op until RegionObj has RC field or we map Obj to Region */
    }
}

void region_dec_ref(Region* r, void* obj) {
    RegionObj* ro = (RegionObj*)obj;
    if (!ro || !r) return;
    
    if (ro->region != r) {
        /* External decrement */
    }
}

/* ============== Linear Regions Implementation ============== */

LinearRegion* linear_region_new(size_t size) {
    LinearRegion* r = calloc(1, sizeof(LinearRegion));
    if (!r) return NULL;

    r->base = malloc(size);
    if (!r->base) {
        free(r);
        return NULL;
    }

    r->size = size;
    r->used = 0;
    r->frozen = false;
    return r;
}

void* linear_region_alloc(LinearRegion* r, size_t size, size_t alignment) {
    if (!r || r->frozen) return NULL;

    /* Align the current position */
    size_t aligned = (r->used + alignment - 1) & ~(alignment - 1);

    /* Check if we have space */
    if (aligned + size > r->size) return NULL;

    void* ptr = (char*)r->base + aligned;
    r->used = aligned + size;
    return ptr;
}

size_t linear_region_remaining(LinearRegion* r) {
    return r ? r->size - r->used : 0;
}

void linear_region_freeze(LinearRegion* r) {
    if (r) r->frozen = true;
}

void linear_region_free(LinearRegion* r) {
    if (!r) return;
    free(r->base);
    free(r);
}

/* ============== Offset Regions Implementation ============== */

OffsetRegion* offset_region_new(size_t size) {
    OffsetRegion* r = calloc(1, sizeof(OffsetRegion));
    if (!r) return NULL;

    r->base = malloc(size);
    if (!r->base) {
        free(r);
        return NULL;
    }

    r->size = size;
    r->used = 0;
    r->reloc_table = NULL;
    r->reloc_count = 0;
    r->reloc_capacity = 0;
    return r;
}

OffsetPtr offset_region_alloc(OffsetRegion* r, size_t size, size_t alignment) {
    if (!r) return OFFSET_NULL;

    /* Reserve byte 0 for NULL - start from 1 if at beginning */
    size_t start = r->used;
    if (start == 0) {
        start = 1;  /* Skip byte 0 to reserve for OFFSET_NULL */
    }

    /* Align the current position */
    size_t aligned = (start + alignment - 1) & ~(alignment - 1);

    /* Check if we have space */
    if (aligned + size > r->size) return OFFSET_NULL;

    OffsetPtr offset = (OffsetPtr)aligned;
    r->used = aligned + size;

    return offset;
}

void offset_region_store_ptr(OffsetRegion* r, OffsetPtr* dest, OffsetPtr target) {
    if (!r || !dest) return;

    *dest = target;

    /* Record this pointer location for potential relocation */
    if (r->reloc_count >= r->reloc_capacity) {
        size_t new_cap = r->reloc_capacity == 0 ? 16 : r->reloc_capacity * 2;
        int32_t* new_table = realloc(r->reloc_table, new_cap * sizeof(int32_t));
        if (!new_table) return;
        r->reloc_table = new_table;
        r->reloc_capacity = new_cap;
    }

    /* Store the offset of the pointer location from base */
    r->reloc_table[r->reloc_count++] = (int32_t)((char*)dest - (char*)r->base);
}

void* offset_to_ptr(OffsetRegion* r, OffsetPtr offset) {
    if (!r || offset == OFFSET_NULL) return NULL;
    return (char*)r->base + offset;
}

OffsetPtr ptr_to_offset(OffsetRegion* r, void* ptr) {
    if (!r || !ptr) return OFFSET_NULL;
    ptrdiff_t diff = (char*)ptr - (char*)r->base;
    if (diff < 0 || (size_t)diff >= r->size) return OFFSET_NULL;
    return (OffsetPtr)diff;
}

void* offset_region_serialize(OffsetRegion* r, size_t* out_size) {
    if (!r || !out_size) return NULL;

    /* Header: size, reloc_count, then reloc table, then data */
    size_t header_size = 2 * sizeof(size_t) + r->reloc_count * sizeof(int32_t);
    size_t total_size = header_size + r->used;

    void* buffer = malloc(total_size);
    if (!buffer) return NULL;

    char* p = buffer;

    /* Write size and reloc count */
    *(size_t*)p = r->used;
    p += sizeof(size_t);
    *(size_t*)p = r->reloc_count;
    p += sizeof(size_t);

    /* Write relocation table */
    if (r->reloc_count > 0) {
        memcpy(p, r->reloc_table, r->reloc_count * sizeof(int32_t));
        p += r->reloc_count * sizeof(int32_t);
    }

    /* Write data */
    memcpy(p, r->base, r->used);

    *out_size = total_size;
    return buffer;
}

OffsetRegion* offset_region_deserialize(void* data, size_t size) {
    if (!data || size < 2 * sizeof(size_t)) return NULL;

    char* p = data;

    /* Read header */
    size_t data_size = *(size_t*)p;
    p += sizeof(size_t);
    size_t reloc_count = *(size_t*)p;
    p += sizeof(size_t);

    /* Validate size */
    size_t header_size = 2 * sizeof(size_t) + reloc_count * sizeof(int32_t);
    if (size < header_size + data_size) return NULL;

    OffsetRegion* r = offset_region_new(data_size);
    if (!r) return NULL;

    /* Copy relocation table */
    if (reloc_count > 0) {
        r->reloc_table = malloc(reloc_count * sizeof(int32_t));
        if (!r->reloc_table) {
            offset_region_free(r);
            return NULL;
        }
        memcpy(r->reloc_table, p, reloc_count * sizeof(int32_t));
        r->reloc_count = reloc_count;
        r->reloc_capacity = reloc_count;
        p += reloc_count * sizeof(int32_t);
    }

    /* Copy data */
    memcpy(r->base, p, data_size);
    r->used = data_size;

    return r;
}

void offset_region_free(OffsetRegion* r) {
    if (!r) return;
    free(r->reloc_table);
    free(r->base);
    free(r);
}

/* ============== Arena Region Implementation ============== */

ArenaRegion* arena_region_new(size_t initial_size) {
    ArenaRegion* r = calloc(1, sizeof(ArenaRegion));
    if (!r) return NULL;

    if (initial_size < 4096) initial_size = 4096;

    ArenaBlock* block = calloc(1, sizeof(ArenaBlock));
    if (!block) {
        free(r);
        return NULL;
    }

    block->memory = malloc(initial_size);
    if (!block->memory) {
        free(block);
        free(r);
        return NULL;
    }

    block->size = initial_size;
    block->used = 0;
    block->next = NULL;

    r->head = block;
    r->current = block;
    r->default_block_size = initial_size;
    r->total_allocated = initial_size;
    r->alloc_count = 0;
    r->frozen = false;

    return r;
}

void* arena_region_alloc(ArenaRegion* r, size_t size, size_t alignment) {
    if (!r || r->frozen || size == 0) return NULL;

    /* Align the current position */
    size_t aligned = (r->current->used + alignment - 1) & ~(alignment - 1);

    /* Check if current block has space */
    if (aligned + size <= r->current->size) {
        void* ptr = (char*)r->current->memory + aligned;
        r->current->used = aligned + size;
        r->alloc_count++;
        return ptr;
    }

    /* Need new block - find size (at least default or requested size) */
    size_t block_size = r->default_block_size;
    if (size + alignment > block_size) {
        block_size = size + alignment;
    }

    ArenaBlock* new_block = calloc(1, sizeof(ArenaBlock));
    if (!new_block) return NULL;

    new_block->memory = malloc(block_size);
    if (!new_block->memory) {
        free(new_block);
        return NULL;
    }

    new_block->size = block_size;
    new_block->used = 0;
    new_block->next = NULL;

    /* Link new block */
    r->current->next = new_block;
    r->current = new_block;
    r->total_allocated += block_size;

    /* Allocate from new block */
    aligned = (alignment - 1) & ~(alignment - 1);  /* Start aligned */
    void* ptr = (char*)new_block->memory + aligned;
    new_block->used = aligned + size;
    r->alloc_count++;

    return ptr;
}

void arena_region_reset(ArenaRegion* r) {
    if (!r) return;

    /* Reset all blocks to empty but keep memory */
    ArenaBlock* block = r->head;
    while (block) {
        block->used = 0;
        block = block->next;
    }
    r->current = r->head;
    r->alloc_count = 0;
    r->frozen = false;
}

void arena_region_free(ArenaRegion* r) {
    if (!r) return;

    ArenaBlock* block = r->head;
    while (block) {
        ArenaBlock* next = block->next;
        free(block->memory);
        free(block);
        block = next;
    }
    free(r);
}

/* ============== Pool Region Implementation ============== */

PoolRegion* pool_region_new(size_t object_size, size_t count) {
    if (object_size < sizeof(void*)) {
        object_size = sizeof(void*);  /* Minimum for free list pointer */
    }

    PoolRegion* r = calloc(1, sizeof(PoolRegion));
    if (!r) return NULL;

    /* Align object size to pointer size */
    size_t alignment = sizeof(void*);
    object_size = (object_size + alignment - 1) & ~(alignment - 1);

    r->data = malloc(object_size * count);
    if (!r->data) {
        free(r);
        return NULL;
    }

    r->object_size = object_size;
    r->alignment = alignment;
    r->capacity = count;
    r->used = 0;
    r->frozen = false;

    /* Build free list */
    r->free_list = r->data;
    char* ptr = r->data;
    for (size_t i = 0; i < count - 1; i++) {
        *(void**)ptr = ptr + object_size;
        ptr += object_size;
    }
    *(void**)ptr = NULL;  /* Last slot points to NULL */

    return r;
}

void* pool_region_alloc(PoolRegion* r) {
    if (!r || r->frozen || !r->free_list) return NULL;

    void* ptr = r->free_list;
    r->free_list = *(void**)ptr;
    r->used++;

    return ptr;
}

void pool_region_free_one(PoolRegion* r, void* ptr) {
    if (!r || !ptr) return;

    /* Verify ptr is within our pool */
    char* p = (char*)ptr;
    char* base = (char*)r->data;
    char* end = base + r->object_size * r->capacity;
    if (p < base || p >= end) {
        return;  /* Not from this pool */
    }

    /* Add to free list */
    *(void**)ptr = r->free_list;
    r->free_list = ptr;
    r->used--;
}

void pool_region_reset(PoolRegion* r) {
    if (!r) return;

    /* Rebuild free list */
    r->free_list = r->data;
    char* ptr = r->data;
    for (size_t i = 0; i < r->capacity - 1; i++) {
        *(void**)ptr = ptr + r->object_size;
        ptr += r->object_size;
    }
    *(void**)ptr = NULL;

    r->used = 0;
    r->frozen = false;
}

void pool_region_free(PoolRegion* r) {
    if (!r) return;
    free(r->data);
    free(r);
}

/* ============== IRegion Vtable Implementations ============== */

/* Forward declarations for vtable functions */
static void* arena_iregion_alloc(IRegion* self, size_t size, size_t alignment);
static void arena_iregion_free_one(IRegion* self, void* ptr);
static void arena_iregion_free_all(IRegion* self);
static size_t arena_iregion_remaining(IRegion* self);
static void arena_iregion_freeze(IRegion* self);
static bool arena_iregion_is_frozen(IRegion* self);
static RegionKind arena_iregion_kind(IRegion* self);
static IRegion* arena_iregion_clone(IRegion* self);
static void* arena_iregion_serialize(IRegion* self, size_t* out_size);
static void arena_iregion_stats(IRegion* self, size_t* alloc_count, size_t* bytes_used, size_t* bytes_total);

static void* linear_iregion_alloc(IRegion* self, size_t size, size_t alignment);
static void linear_iregion_free_one(IRegion* self, void* ptr);
static void linear_iregion_free_all(IRegion* self);
static size_t linear_iregion_remaining(IRegion* self);
static void linear_iregion_freeze(IRegion* self);
static bool linear_iregion_is_frozen(IRegion* self);
static RegionKind linear_iregion_kind(IRegion* self);
static IRegion* linear_iregion_clone(IRegion* self);
static void* linear_iregion_serialize(IRegion* self, size_t* out_size);
static void linear_iregion_stats(IRegion* self, size_t* alloc_count, size_t* bytes_used, size_t* bytes_total);

static void* offset_iregion_alloc(IRegion* self, size_t size, size_t alignment);
static void offset_iregion_free_one(IRegion* self, void* ptr);
static void offset_iregion_free_all(IRegion* self);
static size_t offset_iregion_remaining(IRegion* self);
static void offset_iregion_freeze(IRegion* self);
static bool offset_iregion_is_frozen(IRegion* self);
static RegionKind offset_iregion_kind(IRegion* self);
static IRegion* offset_iregion_clone(IRegion* self);
static void* offset_iregion_serialize(IRegion* self, size_t* out_size);
static void offset_iregion_stats(IRegion* self, size_t* alloc_count, size_t* bytes_used, size_t* bytes_total);

static void* pool_iregion_alloc(IRegion* self, size_t size, size_t alignment);
static void pool_iregion_free_one(IRegion* self, void* ptr);
static void pool_iregion_free_all(IRegion* self);
static size_t pool_iregion_remaining(IRegion* self);
static void pool_iregion_freeze(IRegion* self);
static bool pool_iregion_is_frozen(IRegion* self);
static RegionKind pool_iregion_kind(IRegion* self);
static IRegion* pool_iregion_clone(IRegion* self);
static void* pool_iregion_serialize(IRegion* self, size_t* out_size);
static void pool_iregion_stats(IRegion* self, size_t* alloc_count, size_t* bytes_used, size_t* bytes_total);

/* Vtables */
static const IRegionVtable arena_vtable = {
    .alloc = arena_iregion_alloc,
    .free_one = arena_iregion_free_one,
    .free_all = arena_iregion_free_all,
    .remaining = arena_iregion_remaining,
    .freeze = arena_iregion_freeze,
    .is_frozen = arena_iregion_is_frozen,
    .kind = arena_iregion_kind,
    .clone = arena_iregion_clone,
    .serialize = arena_iregion_serialize,
    .stats = arena_iregion_stats
};

static const IRegionVtable linear_vtable = {
    .alloc = linear_iregion_alloc,
    .free_one = linear_iregion_free_one,
    .free_all = linear_iregion_free_all,
    .remaining = linear_iregion_remaining,
    .freeze = linear_iregion_freeze,
    .is_frozen = linear_iregion_is_frozen,
    .kind = linear_iregion_kind,
    .clone = linear_iregion_clone,
    .serialize = linear_iregion_serialize,
    .stats = linear_iregion_stats
};

static const IRegionVtable offset_vtable = {
    .alloc = offset_iregion_alloc,
    .free_one = offset_iregion_free_one,
    .free_all = offset_iregion_free_all,
    .remaining = offset_iregion_remaining,
    .freeze = offset_iregion_freeze,
    .is_frozen = offset_iregion_is_frozen,
    .kind = offset_iregion_kind,
    .clone = offset_iregion_clone,
    .serialize = offset_iregion_serialize,
    .stats = offset_iregion_stats
};

static const IRegionVtable pool_vtable = {
    .alloc = pool_iregion_alloc,
    .free_one = pool_iregion_free_one,
    .free_all = pool_iregion_free_all,
    .remaining = pool_iregion_remaining,
    .freeze = pool_iregion_freeze,
    .is_frozen = pool_iregion_is_frozen,
    .kind = pool_iregion_kind,
    .clone = pool_iregion_clone,
    .serialize = pool_iregion_serialize,
    .stats = pool_iregion_stats
};

/* ============== Arena IRegion Implementation ============== */

static void* arena_iregion_alloc(IRegion* self, size_t size, size_t alignment) {
    ArenaRegion* arena = (ArenaRegion*)self->impl;
    void* ptr = arena_region_alloc(arena, size, alignment);
    if (ptr) {
        self->alloc_count++;
        self->bytes_used += size;
    }
    return ptr;
}

static void arena_iregion_free_one(IRegion* self, void* ptr) {
    (void)self; (void)ptr;
    /* Arena doesn't support individual frees - no-op */
}

static void arena_iregion_free_all(IRegion* self) {
    ArenaRegion* arena = (ArenaRegion*)self->impl;
    arena_region_free(arena);
    free(self);
}

static size_t arena_iregion_remaining(IRegion* self) {
    ArenaRegion* arena = (ArenaRegion*)self->impl;
    /* Arena can grow, so return remaining in current block */
    return arena->current->size - arena->current->used;
}

static void arena_iregion_freeze(IRegion* self) {
    ArenaRegion* arena = (ArenaRegion*)self->impl;
    arena->frozen = true;
    self->frozen = true;
}

static bool arena_iregion_is_frozen(IRegion* self) {
    return self->frozen;
}

static RegionKind arena_iregion_kind(IRegion* self) {
    (void)self;
    return REGION_KIND_ARENA;
}

static IRegion* arena_iregion_clone(IRegion* self) {
    (void)self;
    /* Arena clone not supported - would need to copy all blocks */
    return NULL;
}

static void* arena_iregion_serialize(IRegion* self, size_t* out_size) {
    (void)self; (void)out_size;
    /* Arena serialization not supported - blocks are non-contiguous */
    return NULL;
}

static void arena_iregion_stats(IRegion* self, size_t* alloc_count, size_t* bytes_used, size_t* bytes_total) {
    ArenaRegion* arena = (ArenaRegion*)self->impl;
    if (alloc_count) *alloc_count = arena->alloc_count;
    if (bytes_total) *bytes_total = arena->total_allocated;

    /* Calculate actual bytes used across all blocks */
    size_t used = 0;
    ArenaBlock* block = arena->head;
    while (block) {
        used += block->used;
        block = block->next;
    }
    if (bytes_used) *bytes_used = used;
}

/* ============== Linear IRegion Implementation ============== */

static void* linear_iregion_alloc(IRegion* self, size_t size, size_t alignment) {
    LinearRegion* linear = (LinearRegion*)self->impl;
    void* ptr = linear_region_alloc(linear, size, alignment);
    if (ptr) {
        self->alloc_count++;
        self->bytes_used = linear->used;
    }
    return ptr;
}

static void linear_iregion_free_one(IRegion* self, void* ptr) {
    (void)self; (void)ptr;
    /* Linear doesn't support individual frees - no-op */
}

static void linear_iregion_free_all(IRegion* self) {
    LinearRegion* linear = (LinearRegion*)self->impl;
    linear_region_free(linear);
    free(self);
}

static size_t linear_iregion_remaining(IRegion* self) {
    LinearRegion* linear = (LinearRegion*)self->impl;
    return linear_region_remaining(linear);
}

static void linear_iregion_freeze(IRegion* self) {
    LinearRegion* linear = (LinearRegion*)self->impl;
    linear_region_freeze(linear);
    self->frozen = true;
}

static bool linear_iregion_is_frozen(IRegion* self) {
    return self->frozen;
}

static RegionKind linear_iregion_kind(IRegion* self) {
    (void)self;
    return REGION_KIND_LINEAR;
}

static IRegion* linear_iregion_clone(IRegion* self) {
    LinearRegion* src = (LinearRegion*)self->impl;

    IRegion* clone = iregion_new_linear(src->size);
    if (!clone) return NULL;

    LinearRegion* dst = (LinearRegion*)clone->impl;
    memcpy(dst->base, src->base, src->used);
    dst->used = src->used;
    clone->alloc_count = self->alloc_count;
    clone->bytes_used = self->bytes_used;

    return clone;
}

static void* linear_iregion_serialize(IRegion* self, size_t* out_size) {
    LinearRegion* linear = (LinearRegion*)self->impl;

    /* Linear region is already contiguous - just copy data */
    void* buffer = malloc(linear->used);
    if (!buffer) return NULL;

    memcpy(buffer, linear->base, linear->used);
    if (out_size) *out_size = linear->used;
    return buffer;
}

static void linear_iregion_stats(IRegion* self, size_t* alloc_count, size_t* bytes_used, size_t* bytes_total) {
    LinearRegion* linear = (LinearRegion*)self->impl;
    if (alloc_count) *alloc_count = self->alloc_count;
    if (bytes_used) *bytes_used = linear->used;
    if (bytes_total) *bytes_total = linear->size;
}

/* ============== Offset IRegion Implementation ============== */

static void* offset_iregion_alloc(IRegion* self, size_t size, size_t alignment) {
    OffsetRegion* offset = (OffsetRegion*)self->impl;
    OffsetPtr off = offset_region_alloc(offset, size, alignment);
    if (off != OFFSET_NULL) {
        self->alloc_count++;
        self->bytes_used = offset->used;
        return offset_to_ptr(offset, off);
    }
    return NULL;
}

static void offset_iregion_free_one(IRegion* self, void* ptr) {
    (void)self; (void)ptr;
    /* Offset doesn't support individual frees - no-op */
}

static void offset_iregion_free_all(IRegion* self) {
    OffsetRegion* offset = (OffsetRegion*)self->impl;
    offset_region_free(offset);
    free(self);
}

static size_t offset_iregion_remaining(IRegion* self) {
    OffsetRegion* offset = (OffsetRegion*)self->impl;
    return offset->size - offset->used;
}

static void offset_iregion_freeze(IRegion* self) {
    self->frozen = true;
}

static bool offset_iregion_is_frozen(IRegion* self) {
    return self->frozen;
}

static RegionKind offset_iregion_kind(IRegion* self) {
    (void)self;
    return REGION_KIND_OFFSET;
}

static IRegion* offset_iregion_clone(IRegion* self) {
    OffsetRegion* src = (OffsetRegion*)self->impl;

    IRegion* clone = iregion_new_offset(src->size);
    if (!clone) return NULL;

    OffsetRegion* dst = (OffsetRegion*)clone->impl;
    memcpy(dst->base, src->base, src->used);
    dst->used = src->used;

    /* Copy relocation table */
    if (src->reloc_count > 0) {
        dst->reloc_table = malloc(src->reloc_count * sizeof(int32_t));
        if (dst->reloc_table) {
            memcpy(dst->reloc_table, src->reloc_table, src->reloc_count * sizeof(int32_t));
            dst->reloc_count = src->reloc_count;
            dst->reloc_capacity = src->reloc_count;
        }
    }

    clone->alloc_count = self->alloc_count;
    clone->bytes_used = self->bytes_used;

    return clone;
}

static void* offset_iregion_serialize(IRegion* self, size_t* out_size) {
    OffsetRegion* offset = (OffsetRegion*)self->impl;
    return offset_region_serialize(offset, out_size);
}

static void offset_iregion_stats(IRegion* self, size_t* alloc_count, size_t* bytes_used, size_t* bytes_total) {
    OffsetRegion* offset = (OffsetRegion*)self->impl;
    if (alloc_count) *alloc_count = self->alloc_count;
    if (bytes_used) *bytes_used = offset->used;
    if (bytes_total) *bytes_total = offset->size;
}

/* ============== Pool IRegion Implementation ============== */

static void* pool_iregion_alloc(IRegion* self, size_t size, size_t alignment) {
    PoolRegion* pool = (PoolRegion*)self->impl;
    (void)size; (void)alignment;  /* Pool uses fixed object size */

    void* ptr = pool_region_alloc(pool);
    if (ptr) {
        self->alloc_count++;
        self->bytes_used = pool->used * pool->object_size;
    }
    return ptr;
}

static void pool_iregion_free_one(IRegion* self, void* ptr) {
    PoolRegion* pool = (PoolRegion*)self->impl;
    pool_region_free_one(pool, ptr);
    if (self->alloc_count > 0) self->alloc_count--;
    self->bytes_used = pool->used * pool->object_size;
}

static void pool_iregion_free_all(IRegion* self) {
    PoolRegion* pool = (PoolRegion*)self->impl;
    pool_region_free(pool);
    free(self);
}

static size_t pool_iregion_remaining(IRegion* self) {
    PoolRegion* pool = (PoolRegion*)self->impl;
    return (pool->capacity - pool->used) * pool->object_size;
}

static void pool_iregion_freeze(IRegion* self) {
    PoolRegion* pool = (PoolRegion*)self->impl;
    pool->frozen = true;
    self->frozen = true;
}

static bool pool_iregion_is_frozen(IRegion* self) {
    return self->frozen;
}

static RegionKind pool_iregion_kind(IRegion* self) {
    (void)self;
    return REGION_KIND_POOL;
}

static IRegion* pool_iregion_clone(IRegion* self) {
    PoolRegion* src = (PoolRegion*)self->impl;

    IRegion* clone = iregion_new_pool(src->object_size, src->capacity);
    if (!clone) return NULL;

    PoolRegion* dst = (PoolRegion*)clone->impl;
    memcpy(dst->data, src->data, src->object_size * src->capacity);
    /* Note: free list is not cloned - clone starts fresh */

    clone->alloc_count = self->alloc_count;
    clone->bytes_used = self->bytes_used;

    return clone;
}

static void* pool_iregion_serialize(IRegion* self, size_t* out_size) {
    PoolRegion* pool = (PoolRegion*)self->impl;

    /* Serialize as raw data block */
    size_t data_size = pool->object_size * pool->capacity;
    void* buffer = malloc(data_size);
    if (!buffer) return NULL;

    memcpy(buffer, pool->data, data_size);
    if (out_size) *out_size = data_size;
    return buffer;
}

static void pool_iregion_stats(IRegion* self, size_t* alloc_count, size_t* bytes_used, size_t* bytes_total) {
    PoolRegion* pool = (PoolRegion*)self->impl;
    if (alloc_count) *alloc_count = pool->used;
    if (bytes_used) *bytes_used = pool->used * pool->object_size;
    if (bytes_total) *bytes_total = pool->capacity * pool->object_size;
}

/* ============== IRegion Factory Functions ============== */

IRegion* iregion_new_arena(size_t initial_size) {
    ArenaRegion* arena = arena_region_new(initial_size);
    if (!arena) return NULL;

    IRegion* r = calloc(1, sizeof(IRegion));
    if (!r) {
        arena_region_free(arena);
        return NULL;
    }

    r->vtable = &arena_vtable;
    r->impl = arena;
    r->kind = REGION_KIND_ARENA;
    r->frozen = false;
    r->alloc_count = 0;
    r->bytes_used = 0;
    r->bytes_total = initial_size;

    return r;
}

IRegion* iregion_new_linear(size_t size) {
    LinearRegion* linear = linear_region_new(size);
    if (!linear) return NULL;

    IRegion* r = calloc(1, sizeof(IRegion));
    if (!r) {
        linear_region_free(linear);
        return NULL;
    }

    r->vtable = &linear_vtable;
    r->impl = linear;
    r->kind = REGION_KIND_LINEAR;
    r->frozen = false;
    r->alloc_count = 0;
    r->bytes_used = 0;
    r->bytes_total = size;

    return r;
}

IRegion* iregion_new_offset(size_t size) {
    OffsetRegion* offset = offset_region_new(size);
    if (!offset) return NULL;

    IRegion* r = calloc(1, sizeof(IRegion));
    if (!r) {
        offset_region_free(offset);
        return NULL;
    }

    r->vtable = &offset_vtable;
    r->impl = offset;
    r->kind = REGION_KIND_OFFSET;
    r->frozen = false;
    r->alloc_count = 0;
    r->bytes_used = 0;
    r->bytes_total = size;

    return r;
}

IRegion* iregion_new_scoped(void) {
    /* Scoped region uses RegionContext - wrap it */
    RegionContext* ctx = region_context_new();
    if (!ctx) return NULL;

    IRegion* r = calloc(1, sizeof(IRegion));
    if (!r) {
        region_context_free(ctx);
        return NULL;
    }

    /* Scoped uses arena vtable but with special handling */
    r->vtable = &arena_vtable;  /* Simplification: use arena behavior */
    r->impl = ctx;
    r->kind = REGION_KIND_SCOPED;
    r->frozen = false;
    r->alloc_count = 0;
    r->bytes_used = 0;
    r->bytes_total = 0;  /* Unlimited */

    return r;
}

IRegion* iregion_new_pool(size_t object_size, size_t count) {
    PoolRegion* pool = pool_region_new(object_size, count);
    if (!pool) return NULL;

    IRegion* r = calloc(1, sizeof(IRegion));
    if (!r) {
        pool_region_free(pool);
        return NULL;
    }

    r->vtable = &pool_vtable;
    r->impl = pool;
    r->kind = REGION_KIND_POOL;
    r->frozen = false;
    r->alloc_count = 0;
    r->bytes_used = 0;
    r->bytes_total = object_size * count;

    return r;
}

/* ============== IRegion Dispatch Functions ============== */

void* iregion_alloc(IRegion* r, size_t size, size_t alignment) {
    if (!r || !r->vtable || !r->vtable->alloc) return NULL;
    return r->vtable->alloc(r, size, alignment);
}

void iregion_free_one(IRegion* r, void* ptr) {
    if (!r || !r->vtable || !r->vtable->free_one) return;
    r->vtable->free_one(r, ptr);
}

void iregion_free_all(IRegion* r) {
    if (!r || !r->vtable || !r->vtable->free_all) return;
    r->vtable->free_all(r);
}

size_t iregion_remaining(IRegion* r) {
    if (!r || !r->vtable || !r->vtable->remaining) return 0;
    return r->vtable->remaining(r);
}

void iregion_freeze(IRegion* r) {
    if (!r || !r->vtable || !r->vtable->freeze) return;
    r->vtable->freeze(r);
}

bool iregion_is_frozen(IRegion* r) {
    if (!r || !r->vtable || !r->vtable->is_frozen) return false;
    return r->vtable->is_frozen(r);
}

RegionKind iregion_kind(IRegion* r) {
    if (!r) return REGION_KIND_CUSTOM;
    return r->kind;
}

IRegion* iregion_clone(IRegion* r) {
    if (!r || !r->vtable || !r->vtable->clone) return NULL;
    return r->vtable->clone(r);
}

void* iregion_serialize(IRegion* r, size_t* out_size) {
    if (!r || !r->vtable || !r->vtable->serialize) return NULL;
    return r->vtable->serialize(r, out_size);
}

void iregion_stats(IRegion* r, size_t* alloc_count, size_t* bytes_used, size_t* bytes_total) {
    if (!r || !r->vtable || !r->vtable->stats) {
        if (alloc_count) *alloc_count = 0;
        if (bytes_used) *bytes_used = 0;
        if (bytes_total) *bytes_total = 0;
        return;
    }
    r->vtable->stats(r, alloc_count, bytes_used, bytes_total);
}

/* ============== Weak Reference Control Blocks Implementation ============== */

/* Global generation counter for ABA protection */
static _Atomic uint64_t global_weak_generation = 1;

WeakControlBlock* weak_cb_new(void* target, void (*destructor)(void*)) {
    WeakControlBlock* cb = calloc(1, sizeof(WeakControlBlock));
    if (!cb) return NULL;

    cb->target = target;
    atomic_store(&cb->weak_count, 0);
    atomic_store(&cb->state, WEAK_CB_ALIVE);
    cb->destructor = destructor;
    cb->generation = atomic_fetch_add(&global_weak_generation, 1);
    cb->heap_allocated = true;

    return cb;
}

void weak_cb_inc_ref(WeakControlBlock* cb) {
    if (!cb) return;
    atomic_fetch_add(&cb->weak_count, 1);
}

void weak_cb_dec_ref(WeakControlBlock* cb) {
    if (!cb) return;

    uint32_t old = atomic_fetch_sub(&cb->weak_count, 1);
    if (old == 1) {
        /* Last weak reference gone - free control block if target also gone */
        uint32_t state = atomic_load(&cb->state);
        if (state == WEAK_CB_DEAD && cb->heap_allocated) {
            free(cb);
        } else if (state != WEAK_CB_DEAD) {
            /* Mark as detached - will be freed when target is invalidated */
            atomic_store(&cb->state, WEAK_CB_DETACHED);
        }
    }
}

void weak_cb_invalidate(WeakControlBlock* cb) {
    if (!cb) return;

    uint32_t old_state = atomic_exchange(&cb->state, WEAK_CB_DEAD);
    if (old_state == WEAK_CB_ALIVE) {
        /* Call destructor if set */
        if (cb->destructor && cb->target) {
            cb->destructor(cb->target);
        }
        cb->target = NULL;

        /* If no weak refs and heap-allocated, free the control block now */
        uint32_t count = atomic_load(&cb->weak_count);
        if (count == 0 && cb->heap_allocated) {
            free(cb);
        }
    } else if (old_state == WEAK_CB_DETACHED && cb->heap_allocated) {
        /* Was already detached (no weak refs), free now */
        cb->target = NULL;
        free(cb);
    }
}

bool weak_cb_is_valid(WeakControlBlock* cb) {
    if (!cb) return false;
    return atomic_load(&cb->state) == WEAK_CB_ALIVE;
}

void* weak_cb_get_target(WeakControlBlock* cb) {
    if (!cb) return NULL;
    if (atomic_load(&cb->state) != WEAK_CB_ALIVE) return NULL;
    return cb->target;
}

/* ============== Weak Handle Implementation ============== */

WeakHandle* weak_handle_new(WeakControlBlock* cb) {
    if (!cb) return NULL;

    WeakHandle* h = malloc(sizeof(WeakHandle));
    if (!h) return NULL;

    h->cb = cb;
    h->generation = cb->generation;
    weak_cb_inc_ref(cb);

    return h;
}

WeakHandle* weak_handle_clone(WeakHandle* h) {
    if (!h || !h->cb) return NULL;
    return weak_handle_new(h->cb);
}

void weak_handle_free(WeakHandle* h) {
    if (!h) return;
    if (h->cb) {
        weak_cb_dec_ref(h->cb);
    }
    free(h);
}

bool weak_handle_is_valid(WeakHandle* h) {
    if (!h || !h->cb) return false;
    /* Check generation for ABA protection */
    if (h->generation != h->cb->generation) return false;
    return weak_cb_is_valid(h->cb);
}

void* weak_handle_lock(WeakHandle* h) {
    if (!weak_handle_is_valid(h)) return NULL;
    return h->cb->target;
}

/* ============== Weakable Object Implementation ============== */

WeakableObject weak_object_new(void* object, void (*destructor)(void*)) {
    WeakableObject wo = { .object = NULL, .cb = NULL };

    wo.cb = weak_cb_new(object, destructor);
    if (!wo.cb) return wo;

    wo.object = object;
    return wo;
}

void weak_object_free(WeakableObject* wo) {
    if (!wo) return;
    if (wo->cb) {
        weak_cb_invalidate(wo->cb);
    }
    wo->object = NULL;
    wo->cb = NULL;
}

/* ============== Weak Reference Table Implementation ============== */

#define WEAK_TABLE_INVALID_INDEX 0xFFFFFFFF
#define WEAK_TABLE_DEFAULT_CAPACITY 1024

WeakRefTable* weak_table_new(size_t capacity) {
    if (capacity == 0) capacity = WEAK_TABLE_DEFAULT_CAPACITY;

    WeakRefTable* table = calloc(1, sizeof(WeakRefTable));
    if (!table) return NULL;

    table->blocks = calloc(capacity, sizeof(WeakControlBlock));
    if (!table->blocks) {
        free(table);
        return NULL;
    }

    table->free_list = malloc(capacity * sizeof(uint32_t));
    if (!table->free_list) {
        free(table->blocks);
        free(table);
        return NULL;
    }

    /* Initialize free list (all indices are free) */
    for (size_t i = 0; i < capacity; i++) {
        table->free_list[i] = (uint32_t)(capacity - 1 - i);
    }

    table->capacity = capacity;
    table->free_count = capacity;
    table->used = 0;
    atomic_store(&table->next_gen, 1);

    return table;
}

void weak_table_free(WeakRefTable* table) {
    if (!table) return;

    /* Invalidate all active entries - call destructors but don't free blocks */
    for (size_t i = 0; i < table->capacity; i++) {
        WeakControlBlock* cb = &table->blocks[i];
        uint32_t state = atomic_load(&cb->state);
        if (state == WEAK_CB_ALIVE) {
            if (cb->destructor && cb->target) {
                cb->destructor(cb->target);
            }
            cb->target = NULL;
            atomic_store(&cb->state, WEAK_CB_DEAD);
        }
    }

    free(table->free_list);
    free(table->blocks);
    free(table);
}

uint32_t weak_table_register(WeakRefTable* table, void* target, void (*destructor)(void*)) {
    if (!table || table->free_count == 0) return WEAK_TABLE_INVALID_INDEX;

    /* Pop from free list */
    uint32_t index = table->free_list[--table->free_count];

    /* Initialize the control block */
    WeakControlBlock* cb = &table->blocks[index];
    cb->target = target;
    atomic_store(&cb->weak_count, 0);
    atomic_store(&cb->state, WEAK_CB_ALIVE);
    cb->destructor = destructor;
    cb->generation = atomic_fetch_add(&table->next_gen, 1);
    cb->heap_allocated = false;  /* Part of table, not individually allocated */

    table->used++;
    return index;
}

void weak_table_invalidate(WeakRefTable* table, uint32_t index) {
    if (!table || index >= table->capacity) return;

    WeakControlBlock* cb = &table->blocks[index];
    uint32_t old_state = atomic_exchange(&cb->state, WEAK_CB_DEAD);

    if (old_state == WEAK_CB_ALIVE || old_state == WEAK_CB_DETACHED) {
        /* Call destructor */
        if (cb->destructor && cb->target) {
            cb->destructor(cb->target);
            cb->destructor = NULL;  /* Prevent double call */
        }
        cb->target = NULL;

        /* Return slot to free list if no weak refs */
        uint32_t count = atomic_load(&cb->weak_count);
        if (count == 0) {
            table->free_list[table->free_count++] = index;
            table->used--;
        }
    }
}

WeakHandle* weak_table_get_handle(WeakRefTable* table, uint32_t index) {
    if (!table || index >= table->capacity) return NULL;

    WeakControlBlock* cb = &table->blocks[index];
    if (atomic_load(&cb->state) != WEAK_CB_ALIVE) return NULL;

    return weak_handle_new(cb);
}

void* weak_table_lock(WeakRefTable* table, uint32_t index, uint64_t generation) {
    if (!table || index >= table->capacity) return NULL;

    WeakControlBlock* cb = &table->blocks[index];

    /* Check generation for ABA protection */
    if (cb->generation != generation) return NULL;
    if (atomic_load(&cb->state) != WEAK_CB_ALIVE) return NULL;

    return cb->target;
}

void weak_table_release(WeakRefTable* table, uint32_t index) {
    if (!table || index >= table->capacity) return;

    WeakControlBlock* cb = &table->blocks[index];
    uint32_t old = atomic_fetch_sub(&cb->weak_count, 1);

    if (old == 1 && atomic_load(&cb->state) == WEAK_CB_DEAD) {
        /* Last weak ref gone and target already dead - reclaim slot */
        table->free_list[table->free_count++] = index;
        table->used--;
    }
}

/* ============== Global Weak Reference Table ============== */

static WeakRefTable* _global_weak_table = NULL;

WeakRefTable* weak_table_global(void) {
    if (!_global_weak_table) {
        _global_weak_table = weak_table_new(WEAK_TABLE_DEFAULT_CAPACITY);
    }
    return _global_weak_table;
}

/* ============== Transmigration Implementation ============== */

#define TRANSMIGRATION_MAP_INITIAL_SIZE 64
#define MAX_TYPE_VISITORS 256

/* Hash map entry for object mapping */
typedef struct TransmigrationMapEntry {
    void* source;
    void* dest;
    struct TransmigrationMapEntry* next;
} TransmigrationMapEntry;

/* Transmigration context */
struct TransmigrationContext {
    IRegion* dest_region;
    TransmigrationMapEntry** map;       /* Hash map for object mapping */
    size_t map_size;
    size_t entry_count;
    TransmigrationVisitor visitors[MAX_TYPE_VISITORS];
    _Atomic uint64_t epoch;             /* For region epoch tracking */
};

/* Simple hash for pointers */
static size_t ptr_hash(void* ptr, size_t size) {
    uintptr_t p = (uintptr_t)ptr;
    return (size_t)(p ^ (p >> 16)) % size;
}

TransmigrationContext* transmigration_new(IRegion* dest) {
    if (!dest) return NULL;

    TransmigrationContext* ctx = calloc(1, sizeof(TransmigrationContext));
    if (!ctx) return NULL;

    ctx->dest_region = dest;
    ctx->map_size = TRANSMIGRATION_MAP_INITIAL_SIZE;
    ctx->map = calloc(ctx->map_size, sizeof(TransmigrationMapEntry*));
    if (!ctx->map) {
        free(ctx);
        return NULL;
    }

    ctx->entry_count = 0;
    atomic_store(&ctx->epoch, 1);

    /* Clear visitors */
    for (int i = 0; i < MAX_TYPE_VISITORS; i++) {
        ctx->visitors[i] = NULL;
    }

    return ctx;
}

void transmigration_free(TransmigrationContext* ctx) {
    if (!ctx) return;

    /* Free hash map entries */
    for (size_t i = 0; i < ctx->map_size; i++) {
        TransmigrationMapEntry* entry = ctx->map[i];
        while (entry) {
            TransmigrationMapEntry* next = entry->next;
            free(entry);
            entry = next;
        }
    }
    free(ctx->map);
    free(ctx);
}

void transmigration_register_visitor(
    TransmigrationContext* ctx,
    int type_tag,
    TransmigrationVisitor visitor
) {
    if (!ctx || type_tag < 0 || type_tag >= MAX_TYPE_VISITORS) return;
    ctx->visitors[type_tag] = visitor;
}

void* transmigration_lookup(TransmigrationContext* ctx, void* source) {
    if (!ctx || !source) return NULL;

    size_t idx = ptr_hash(source, ctx->map_size);
    TransmigrationMapEntry* entry = ctx->map[idx];

    while (entry) {
        if (entry->source == source) {
            return entry->dest;
        }
        entry = entry->next;
    }
    return NULL;
}

void transmigration_record(TransmigrationContext* ctx, void* source, void* dest) {
    if (!ctx || !source) return;

    TransmigrationMapEntry* entry = malloc(sizeof(TransmigrationMapEntry));
    if (!entry) return;

    entry->source = source;
    entry->dest = dest;

    size_t idx = ptr_hash(source, ctx->map_size);
    entry->next = ctx->map[idx];
    ctx->map[idx] = entry;
    ctx->entry_count++;

    /* Grow map if too full */
    if (ctx->entry_count > ctx->map_size * 2) {
        size_t new_size = ctx->map_size * 2;
        TransmigrationMapEntry** new_map = calloc(new_size, sizeof(TransmigrationMapEntry*));
        if (new_map) {
            /* Rehash all entries */
            for (size_t i = 0; i < ctx->map_size; i++) {
                TransmigrationMapEntry* e = ctx->map[i];
                while (e) {
                    TransmigrationMapEntry* next = e->next;
                    size_t new_idx = ptr_hash(e->source, new_size);
                    e->next = new_map[new_idx];
                    new_map[new_idx] = e;
                    e = next;
                }
            }
            free(ctx->map);
            ctx->map = new_map;
            ctx->map_size = new_size;
        }
    }
}

void* transmigrate(TransmigrationContext* ctx, void* source, TransmigrationError* err) {
    if (!ctx) {
        if (err) *err = TRANSMIGRATE_ERR_NULL;
        return NULL;
    }

    if (!source) {
        if (err) *err = TRANSMIGRATE_OK;
        return NULL;  /* NULL transmigrates to NULL */
    }

    if (iregion_is_frozen(ctx->dest_region)) {
        if (err) *err = TRANSMIGRATE_ERR_REGION_CLOSED;
        return NULL;
    }

    /* Check if already copied (cycle detection) */
    void* existing = transmigration_lookup(ctx, source);
    if (existing) {
        if (err) *err = TRANSMIGRATE_OK;
        return existing;
    }

    /* For now, just do a shallow copy of the raw bytes */
    /* In a real implementation, this would use registered visitors */
    /* or type information to do proper deep copying */

    /* Default: allocate space and copy */
    /* This is a placeholder - real implementation needs type info */
    if (err) *err = TRANSMIGRATE_ERR_UNSUPPORTED_TYPE;
    return NULL;
}

/* ============== Isolation Checking Implementation ============== */

IsolationResult* check_isolation(void* root, IRegion* expected_region) {
    IsolationResult* result = calloc(1, sizeof(IsolationResult));
    if (!result) return NULL;

    if (!root) {
        result->is_isolated = true;
        return result;
    }

    if (!expected_region) {
        result->is_isolated = false;
        result->escape_count = 1;
        return result;
    }

    /* For now, assume isolated if region is provided */
    /* Real implementation would traverse object graph and check each pointer */
    result->is_isolated = true;
    result->escape_count = 0;
    result->escaping_refs = NULL;
    result->escaping_capacity = 0;

    return result;
}

void isolation_result_free(IsolationResult* result) {
    if (!result) return;
    free(result->escaping_refs);
    free(result);
}

/* ============== Region-Bound Reference Implementation ============== */

static _Atomic uint64_t global_region_epoch = 1;

RegionBoundRef* region_bound_ref_new(void* target, IRegion* region) {
    if (!region) return NULL;

    RegionBoundRef* ref = malloc(sizeof(RegionBoundRef));
    if (!ref) return NULL;

    ref->target = target;
    ref->region = region;
    ref->region_epoch = atomic_fetch_add(&global_region_epoch, 1);

    return ref;
}

void region_bound_ref_free(RegionBoundRef* ref) {
    free(ref);
}

void* region_bound_ref_deref(RegionBoundRef* ref) {
    if (!region_bound_ref_is_valid(ref)) return NULL;
    return ref->target;
}

bool region_bound_ref_is_valid(RegionBoundRef* ref) {
    if (!ref || !ref->region) return false;
    if (iregion_is_frozen(ref->region)) return false;
    return true;
}

/* ============== External Handle Indexing Implementation ============== */

#define EXTERNAL_TABLE_DEFAULT_CAPACITY 256

ExternalHandleTable* external_table_new(size_t capacity) {
    if (capacity == 0) capacity = EXTERNAL_TABLE_DEFAULT_CAPACITY;

    ExternalHandleTable* table = calloc(1, sizeof(ExternalHandleTable));
    if (!table) return NULL;

    table->slots = calloc(capacity, sizeof(ExternalHandleSlot));
    if (!table->slots) {
        free(table);
        return NULL;
    }

    table->free_stack = malloc(capacity * sizeof(uint32_t));
    if (!table->free_stack) {
        free(table->slots);
        free(table);
        return NULL;
    }

    /* Initialize slots and free stack */
    for (size_t i = 0; i < capacity; i++) {
        table->slots[i].object = NULL;
        table->slots[i].generation = 1;  /* Start at 1 so handle 0 is invalid */
        table->slots[i].in_use = false;
        table->slots[i].destructor = NULL;
        table->free_stack[i] = (uint32_t)(capacity - 1 - i);
    }

    table->capacity = capacity;
    table->used = 0;
    table->free_count = capacity;
    table->next_id = 1;  /* For deterministic mode */
    table->deterministic = false;

    return table;
}

void external_table_free(ExternalHandleTable* table) {
    if (!table) return;

    /* Call destructors for all active handles */
    for (size_t i = 0; i < table->capacity; i++) {
        if (table->slots[i].in_use) {
            if (table->slots[i].destructor && table->slots[i].object) {
                table->slots[i].destructor(table->slots[i].object);
            }
        }
    }

    free(table->free_stack);
    free(table->slots);
    free(table);
}

void external_table_set_deterministic(ExternalHandleTable* table, bool deterministic) {
    if (!table) return;
    table->deterministic = deterministic;
}

ExternalHandle external_handle_create(ExternalHandleTable* table, void* object, void (*destructor)(void*)) {
    if (!table) return EXTERNAL_HANDLE_INVALID;

    uint32_t index;

    if (table->deterministic) {
        /* Deterministic mode: use sequential indices */
        if (table->next_id >= table->capacity) {
            return EXTERNAL_HANDLE_INVALID;  /* Table full */
        }
        index = (uint32_t)table->next_id++;

        /* Mark slot as used if it was in free stack */
        /* In deterministic mode, we skip the free stack */
    } else {
        /* Normal mode: use free stack */
        if (table->free_count == 0) {
            return EXTERNAL_HANDLE_INVALID;  /* Table full */
        }
        index = table->free_stack[--table->free_count];
    }

    ExternalHandleSlot* slot = &table->slots[index];
    slot->object = object;
    slot->in_use = true;
    slot->destructor = destructor;
    /* Generation already set */

    table->used++;

    return EXTERNAL_HANDLE_MAKE(index, slot->generation);
}

void external_handle_release(ExternalHandleTable* table, ExternalHandle handle) {
    if (!table || handle == EXTERNAL_HANDLE_INVALID) return;

    uint32_t index = EXTERNAL_HANDLE_INDEX(handle);
    uint32_t gen = EXTERNAL_HANDLE_GEN(handle);

    if (index >= table->capacity) return;

    ExternalHandleSlot* slot = &table->slots[index];

    /* Check generation to prevent ABA */
    if (slot->generation != gen || !slot->in_use) return;

    /* Call destructor if set */
    if (slot->destructor && slot->object) {
        slot->destructor(slot->object);
    }

    /* Clear slot */
    slot->object = NULL;
    slot->destructor = NULL;
    slot->in_use = false;
    slot->generation++;  /* Increment generation for ABA protection */

    /* Return to free stack (unless in deterministic mode) */
    if (!table->deterministic) {
        table->free_stack[table->free_count++] = index;
    }

    table->used--;
}

void* external_handle_get(ExternalHandleTable* table, ExternalHandle handle) {
    if (!table || handle == EXTERNAL_HANDLE_INVALID) return NULL;

    uint32_t index = EXTERNAL_HANDLE_INDEX(handle);
    uint32_t gen = EXTERNAL_HANDLE_GEN(handle);

    if (index >= table->capacity) return NULL;

    ExternalHandleSlot* slot = &table->slots[index];

    /* Check generation and in_use */
    if (slot->generation != gen || !slot->in_use) return NULL;

    return slot->object;
}

bool external_handle_is_valid(ExternalHandleTable* table, ExternalHandle handle) {
    if (!table || handle == EXTERNAL_HANDLE_INVALID) return false;

    uint32_t index = EXTERNAL_HANDLE_INDEX(handle);
    uint32_t gen = EXTERNAL_HANDLE_GEN(handle);

    if (index >= table->capacity) return false;

    ExternalHandleSlot* slot = &table->slots[index];
    return slot->generation == gen && slot->in_use;
}

void external_table_clear(ExternalHandleTable* table) {
    if (!table) return;

    /* Release all handles */
    for (size_t i = 0; i < table->capacity; i++) {
        if (table->slots[i].in_use) {
            if (table->slots[i].destructor && table->slots[i].object) {
                table->slots[i].destructor(table->slots[i].object);
            }
            table->slots[i].object = NULL;
            table->slots[i].destructor = NULL;
            table->slots[i].in_use = false;
            table->slots[i].generation++;
        }
    }

    /* Rebuild free stack */
    table->free_count = table->capacity;
    for (size_t i = 0; i < table->capacity; i++) {
        table->free_stack[i] = (uint32_t)(table->capacity - 1 - i);
    }

    table->used = 0;
    table->next_id = 1;
}

size_t external_table_count(ExternalHandleTable* table) {
    return table ? table->used : 0;
}

void external_table_iterate(ExternalHandleTable* table, void (*callback)(ExternalHandle, void*, void*), void* user_data) {
    if (!table || !callback) return;

    for (size_t i = 0; i < table->capacity; i++) {
        if (table->slots[i].in_use) {
            ExternalHandle handle = EXTERNAL_HANDLE_MAKE(i, table->slots[i].generation);
            callback(handle, table->slots[i].object, user_data);
        }
    }
}

/* ============== Global External Handle Table ============== */

static ExternalHandleTable* _global_external_table = NULL;

ExternalHandleTable* external_table_global(void) {
    if (!_global_external_table) {
        _global_external_table = external_table_new(EXTERNAL_TABLE_DEFAULT_CAPACITY);
    }
    return _global_external_table;
}

/* ============== FFI Convenience Functions ============== */

uint64_t ffi_obj_to_handle(void* obj) {
    ExternalHandleTable* table = external_table_global();
    if (!table) return EXTERNAL_HANDLE_INVALID;
    return external_handle_create(table, obj, NULL);
}

void* ffi_handle_to_obj(uint64_t handle) {
    ExternalHandleTable* table = external_table_global();
    if (!table) return NULL;
    return external_handle_get(table, handle);
}

void ffi_release_handle(uint64_t handle) {
    ExternalHandleTable* table = external_table_global();
    if (!table) return;
    external_handle_release(table, handle);
}
