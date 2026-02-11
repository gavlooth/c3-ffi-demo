/**
 * =============================================================================
 * REGION MEMORY SYSTEM v3 — C3 Implementation
 * =============================================================================
 *
 * A hierarchical, region-based memory management system designed for safe,
 * efficient object lifecycle management without garbage collection.
 *
 * ## ARCHITECTURE OVERVIEW
 *
 * The system is organized around these core concepts:
 *
 * 1. **Regions** — Hierarchical memory containers forming a tree structure.
 *    Each region owns objects and can have child regions. When a region dies,
 *    its objects are automatically promoted to the parent region.
 *
 * 2. **Handles** — Opaque references to objects/regions with generation counters
 *    for safe validation. A handle becomes invalid when its target is freed.
 *
 * 3. **Sparse Sets** — O(1) insert/remove/contains data structure with
 *    cache-friendly iteration. Used for tracking live slots and regions.
 *
 * 4. **Pools** — Contiguous packed storage for objects within a region.
 *    Uses swap-and-pop compaction on removal to maintain cache locality.
 *    - Small objects (≤16 bytes): stored inline in the slot
 *    - Large objects (>16 bytes): allocated from arena blocks
 *
 * 5. **Slot Tables** — Indirection layer between handles and pool storage.
 *    Each slot tracks an object's routing state: LIVE, FORWARDED, or DEAD.
 *
 * 6. **Ghost Tables** — When a region dies but had live objects, its slot
 *    table becomes a "ghost table" held by the parent. This allows existing
 *    handles to the dead region to still resolve via forwarding pointers.
 *
 * ## KEY PROPERTIES
 *
 * - **O(1) allocation and deallocation** via recycled ID pools
 * - **O(1) handle validation** using generation counters
 * - **Automatic object promotion** — objects survive their region's death
 * - **No dangling pointers** — handles to dead objects are detectable
 * - **Cache-friendly iteration** — packed arrays for hot data paths
 *
 * ## MEMORY LAYOUT
 *
 * ```
 * RegionRegistry (singleton)
 *  └── Region (tree structure)
 *       ├── Pool (packed object storage)
 *       │    ├── PoolSlot[] (inline or arena-backed)
 *       │    └── ArenaBlock[] (64KB chunks for large objects)
 *       ├── SlotTable (handle → pool indirection)
 *       │    └── ObjectRecord[] (LIVE/FORWARDED/DEAD)
 *       └── GhostTable[] (inherited from dead children)
 * ```
 */
module main;

import std::collections::list;
import std::io;
import lisp;

// =============================================================================
// SECTION 1: PRIMITIVE TYPE ALIASES
// =============================================================================
//
// These typedefs establish domain-specific types for clarity and type safety.
// All are backed by uint for consistent 32-bit indexing.
//

/** Unique identifier for a region in the registry. */
typedef RegionId   = uint;

/**
 * Slot table ID — the developer-facing identifier carried in ObjectHandle.
 * Points to an ObjectRecord in a region's SlotTable.
 */
typedef SlotId     = uint;

/**
 * Pool-internal stable identifier — NEVER exposed outside the pool.
 * Used by ObjectRecord.live_info to locate physical object data.
 * Stable across swap-and-pop compaction (unlike array indices).
 */
typedef PoolId     = uint;

/**
 * Transient array index into Pool.packed_slots.
 * Changes when objects are removed (swap-and-pop), so never stored in handles.
 */
typedef PoolIndex  = uint;

/**
 * Generation counter for handle validation.
 * Incremented each time a slot/region is recycled, invalidating old handles.
 */
typedef Generation = uint;

/** Reference count for region lifetime management. */
typedef RefCount   = uint;

// =============================================================================
// SECTION 2: HANDLE TYPES
// =============================================================================
//
// Handles are opaque references that clients use to access objects/regions.
// They contain enough information to validate their target still exists.
//

/**
 * ObjectHandle — Reference to an object within the memory system.
 *
 * Fields:
 *   region_id  — Which region originally owned this object
 *   slot_id    — Index into that region's SlotTable
 *   generation — Must match slot's generation for handle to be valid
 *
 * Usage:
 *   ObjectHandle handle = region.allocate_typed(&region, int, 42);
 *   int* ptr = registry.dereference_as(&registry, int, handle);
 *
 * Note: If the region dies, the handle may still be valid (object promoted
 * to parent), but resolution now goes through a ghost table.
 */
struct ObjectHandle {
    RegionId   region_id;
    SlotId     slot_id;
    Generation generation;
}

/**
 * RegionHandle — Reference to a region within the registry.
 *
 * Fields:
 *   region_id  — Index into RegionRegistry.region_storage
 *   generation — Must match region's generation for handle to be valid
 */
struct RegionHandle {
    RegionId   region_id;
    Generation generation;
}

/** Sentinel value representing "no object". Used in lieu of null/optional. */
const ObjectHandle INVALID_OBJECT_HANDLE = { 0, 0, 0 };

/**
 * Sentinel value representing "no region" or "use default parent".
 * Pass to create_region() to create under root.
 */
const RegionHandle INVALID_REGION_HANDLE = { 0, 0 };

// =============================================================================
// SECTION 3: SPARSE SET
// =============================================================================
//
// A Sparse Set provides O(1) insert, remove, and contains operations while
// also supporting O(n) cache-friendly iteration over live elements.
//
// How it works:
// - `key_position_index[key]` tells us where `key` lives in `packed_keys`
// - `packed_keys[0..live_count]` is a dense array of all live keys
// - On removal, we swap the removed key with the last key (swap-and-pop)
//
// Trade-off: Uses O(max_key) space for the sparse array, but provides
// constant-time operations and contiguous iteration.
//

/**
 * SparseSet — O(1) set operations with cache-friendly iteration.
 *
 * Used for:
 * - Tracking live slots in SlotTable.live_slot_tracker
 * - Tracking live regions in RegionRegistry.live_region_tracker
 */
struct SparseSet {
    /**
     * Sparse array: key_position_index[key] = position in packed_keys.
     * Grows dynamically to accommodate the largest key seen.
     */
    List{uint} key_position_index;

    /**
     * Dense array of live keys. Only [0..live_count) are valid.
     * Keys beyond live_count are garbage (leftover from removals).
     */
    List{uint} packed_keys;

    /** Number of live keys currently in the set. */
    uint live_count;
}

/**
 * Check if a key is present in the set.
 *
 * Algorithm:
 * 1. Bounds check: key must be within sparse array range
 * 2. Position check: sparse[key] must point to valid dense index
 * 3. Verification: dense[position] must equal key (handles stale entries)
 *
 * @param key The key to check for membership
 * @return true if key is in the set, false otherwise
 */
fn bool SparseSet.contains(SparseSet* self, uint key) {
    // Key too large — definitely not in set
    if (key >= (uint)self.key_position_index.len()) return false;

    uint pos = self.key_position_index[(usz)key];

    // Position must be within live range AND dense[pos] must verify the key
    // (the verification handles the case where sparse[key] has stale data)
    return pos < self.live_count && self.packed_keys[(usz)pos] == key;
}

/**
 * Insert a key into the set. No-op if already present.
 *
 * Algorithm:
 * 1. Early exit if already present (idempotent)
 * 2. Grow sparse array if needed to fit key
 * 3. Append key to dense array (or reuse slot beyond live_count)
 * 4. Update sparse array to point to new position
 * 5. Increment live_count
 *
 * @param key The key to insert
 */
fn void SparseSet.insert(SparseSet* self, uint key) {
    // Idempotent: don't insert duplicates
    if (self.contains(key)) return;

    // Grow sparse array if needed to accommodate this key
    while (self.key_position_index.len() <= (usz)key) {
        self.key_position_index.push(0);
    }

    // Append to dense array (reuse existing capacity if available)
    if (self.packed_keys.len() <= (usz)self.live_count) {
        self.packed_keys.push(key);
    } else {
        self.packed_keys[(usz)self.live_count] = key;
    }

    // Update sparse → dense mapping
    self.key_position_index[(usz)key] = self.live_count;
    self.live_count += 1;
}

/**
 * Remove a key from the set. No-op if not present.
 *
 * Algorithm (swap-and-pop):
 * 1. Early exit if not present
 * 2. Get position of key to remove
 * 3. Get the last key in dense array
 * 4. Move last key into removed key's position
 * 5. Update sparse array for moved key
 * 6. Decrement live_count (effectively "pops" the last slot)
 *
 * Note: The removed key's sparse entry becomes stale but that's OK —
 * contains() verifies by checking if dense[pos] == key.
 *
 * @param key The key to remove
 */
fn void SparseSet.remove(SparseSet* self, uint key) {
    // Nothing to remove if not present
    if (!self.contains(key)) return;

    uint pos = self.key_position_index[(usz)key];
    uint last_key = self.packed_keys[(usz)(self.live_count - 1)];

    // Swap: move last key into the gap
    self.packed_keys[(usz)pos] = last_key;
    self.key_position_index[(usz)last_key] = pos;

    // Pop: shrink live region (removed key's slot is now garbage)
    self.live_count -= 1;
}

/**
 * Get a slice of all live keys for iteration.
 *
 * The returned slice is valid only until the next insert/remove.
 * Iteration order is not guaranteed to be stable across modifications.
 *
 * @return Slice of live keys [0..live_count)
 */
fn uint[] SparseSet.get_live_keys(SparseSet* self) {
    return self.packed_keys.array_view()[:self.live_count];
}

/**
 * Free all memory owned by this SparseSet.
 * Call when the owning data structure is destroyed.
 */
fn void SparseSet.release(SparseSet* self) {
    self.key_position_index.free();
    self.packed_keys.free();
}

// =============================================================================
// SECTION 4: POOL (Object Storage)
// =============================================================================
//
// The Pool provides contiguous packed storage for objects within a region.
// It uses a two-tier allocation strategy:
//
// - **Inline storage** (≤16 bytes): Data stored directly in PoolSlot.inline_data
//   No heap allocation, excellent cache locality, zero fragmentation.
//
// - **Arena allocation** (>16 bytes): Data stored in large arena blocks (64KB).
//   Freed chunks tracked in a free-list for reuse. Simple bump allocation
//   when no free chunk available.
//
// The Pool maintains stable PoolIds that never change, even as objects are
// removed and the array compacts. This is critical because ObjectRecords
// in the SlotTable hold PoolIds to reference their object data.
//

// -----------------------------------------------------------------------------
// 4.1 Pool Slot — Per-object metadata and optional inline storage
// -----------------------------------------------------------------------------

/**
 * Size threshold for inline vs. arena allocation.
 * Objects ≤16 bytes are stored in PoolSlot.inline_data (no heap allocation).
 * Objects >16 bytes are allocated from arena blocks.
 *
 * 16 bytes chosen because:
 * - Fits common small types: int, float, pointers, small structs
 * - Aligns well with CPU cache lines
 * - Keeps PoolSlot reasonably sized
 */
const usz INLINE_THRESHOLD = 16;

/**
 * PoolSlot — Metadata and storage for one object in the pool.
 *
 * Fields:
 *   size      — Object size in bytes
 *   type_id   — Runtime type identifier for destructor dispatch
 *   owner_id  — Which PoolId owns this slot (for reverse lookup during compaction)
 *   is_inline — true if data in inline_data, false if in heap_ptr
 *   heap_ptr  — Pointer to arena-allocated data (valid if !is_inline)
 *   inline_data — Small object storage (valid if is_inline)
 *
 * Note: Only one of heap_ptr/inline_data is valid based on is_inline flag.
 */
struct PoolSlot {
    usz    size;        // Object size in bytes
    typeid type_id;     // Runtime type ID for destructor lookup
    PoolId owner_id;    // Reverse mapping: which PoolId owns this slot

    bool   is_inline;   // true = data in inline_data, false = data in heap_ptr

    // Storage (union-like usage based on is_inline):
    void*  heap_ptr;        // Arena pointer (>16 bytes objects)
    char[16] inline_data;   // Inline storage (≤16 bytes objects)
}

// -----------------------------------------------------------------------------
// 4.2 Arena Block — Large contiguous memory chunk for heap allocations
// -----------------------------------------------------------------------------

/**
 * Size of each arena block.
 * 64KB chosen as a balance between:
 * - Large enough to amortize allocation overhead
 * - Small enough to not waste too much memory on the last block
 * - Matches common OS page allocation granularity
 */
const usz ARENA_SIZE = 64 * 1024;  // 64KB per arena

/**
 * ArenaBlock — A contiguous memory chunk for allocating large objects.
 *
 * Allocation is bump-pointer style: just increment `used`.
 * When an arena fills up, a new one is allocated.
 *
 * Fields:
 *   data     — Pointer to the raw memory (malloc'd)
 *   capacity — Total size of the block (always ARENA_SIZE)
 *   used     — Bytes currently allocated from this block
 */
struct ArenaBlock {
    char* data;      // Raw memory block
    usz   capacity;  // Total capacity (ARENA_SIZE)
    usz   used;      // Current allocation offset
}

/**
 * FreeListEntry — Tracks a freed chunk within an arena for reuse.
 *
 * When an object is freed, its memory is added to the free list.
 * Future allocations check the free list before bumping the arena pointer.
 *
 * Note: This is a simplified free-list. A production implementation might
 * coalesce adjacent free chunks or use a more sophisticated allocator.
 */
struct FreeListEntry {
    usz offset;  // Offset within the arena where this free chunk starts
    usz size;    // Size of the free chunk in bytes
}

// -----------------------------------------------------------------------------
// 4.3 Pool Structure
// -----------------------------------------------------------------------------

/**
 * Pool — Packed object storage with stable IDs and two-tier allocation.
 *
 * The pool maintains three parallel arrays for the packed representation:
 * - packed_slots: The actual PoolSlot data
 * - position_owner_index: Maps position → PoolId (for compaction)
 * - pool_id_position_index: Maps PoolId → position (for lookup)
 *
 * PoolIds are stable (never change), while positions change on compaction.
 * This allows ObjectRecords to hold PoolIds that remain valid through removals.
 *
 * Allocation flow:
 * 1. Get or recycle a PoolId
 * 2. Store object data (inline or arena-based on size)
 * 3. Append slot to packed array
 * 4. Update bidirectional indices
 *
 * Removal flow (swap-and-pop):
 * 1. Free object storage (arena → free list, inline → no-op)
 * 2. Swap removed slot with last slot
 * 3. Update indices for moved slot
 * 4. Recycle the PoolId for reuse
 */
struct Pool {
    // --- Packed storage ---
    List{PoolSlot} packed_slots;  // Dense array of live object slots
    uint slot_count;               // Number of live slots (< packed_slots.len)

    // --- Bidirectional index for stable IDs ---
    List{PoolIndex} pool_id_position_index;  // PoolId → position in packed_slots
    List{PoolId}    position_owner_index;    // position → which PoolId owns it

    // --- ID recycling ---
    List{PoolId} recycled_pool_ids;  // Stack of reusable PoolIds
    PoolId       next_pool_id;       // Next fresh PoolId if recycle stack empty

    // --- Arena allocation for large objects ---
    List{ArenaBlock} arenas;         // List of 64KB arena blocks
    List{FreeListEntry} free_list;   // Freed chunks available for reuse
}

// -----------------------------------------------------------------------------
// 4.4 Arena Allocation Helpers
// -----------------------------------------------------------------------------

/**
 * Create a new arena block.
 *
 * Allocates ARENA_SIZE bytes from the system allocator.
 *
 * @return Initialized ArenaBlock with full capacity and zero usage
 */
fn ArenaBlock Pool.new_arena(Pool* self) {
    ArenaBlock arena = {
        .data     = (char*)mem::malloc(ARENA_SIZE),
        .capacity = ARENA_SIZE,
        .used     = 0
    };
    return arena;
}

/**
 * Allocate memory from the arena system.
 *
 * Algorithm:
 * 1. Check free-list for a chunk ≥ requested size
 *    - Exact fit: remove entry from free-list
 *    - Larger: shrink the free entry
 * 2. If no free chunk, bump-allocate from current arena
 * 3. If current arena full, create new arena
 *
 * @param size Number of bytes to allocate
 * @return Pointer to allocated memory (never null)
 */
fn void* Pool.arena_alloc(Pool* self, usz size) {
    // --- Try to reuse from free-list first ---
    for (usz i = 0; i < self.free_list.len(); i++) {
        if (self.free_list[i].size >= size) {
            // Found a suitable free chunk
            void* ptr = self.arenas[0].data + self.free_list[i].offset;

            if (self.free_list[i].size == size) {
                // Exact fit: remove entry (swap with last and pop)
                self.free_list[i] = self.free_list[self.free_list.len() - 1];
                (void)self.free_list.pop();
            } else {
                // Partial fit: shrink the free entry
                self.free_list[i].offset += size;
                self.free_list[i].size -= size;
            }
            return ptr;
        }
    }

    // --- No free chunk available, allocate from arena ---

    // Create first arena if none exist
    if (self.arenas.len() == 0) {
        self.arenas.push(self.new_arena());
    }

    ArenaBlock* current = &self.arenas[self.arenas.len() - 1];

    // Need a new arena if current one can't fit this allocation
    if (current.used + size > current.capacity) {
        self.arenas.push(self.new_arena());
        current = &self.arenas[self.arenas.len() - 1];
    }

    // Bump-allocate from current arena
    void* ptr = current.data + current.used;
    current.used += size;
    return ptr;
}

/**
 * Return memory to the arena's free-list for reuse.
 *
 * Finds which arena owns this pointer and adds an entry to the free-list.
 * Future allocations may reuse this chunk.
 *
 * Note: This implementation doesn't coalesce adjacent free chunks.
 * A production system might want to merge them to reduce fragmentation.
 *
 * @param ptr  Pointer to free (must have been returned by arena_alloc)
 * @param size Size of the allocation being freed
 */
fn void Pool.arena_free(Pool* self, void* ptr, usz size) {
    // Find which arena contains this pointer
    for (usz i = 0; i < self.arenas.len(); i++) {
        char* arena_start = self.arenas[i].data;
        char* arena_end = arena_start + self.arenas[i].capacity;

        if ((char*)ptr >= arena_start && (char*)ptr < arena_end) {
            // Found the owning arena — add to free-list
            usz offset = (usz)((char*)ptr - arena_start);
            FreeListEntry entry = { .offset = offset, .size = size };
            self.free_list.push(entry);
            return;
        }
    }
    // If we get here, ptr wasn't from any arena (bug in caller)
}

// -----------------------------------------------------------------------------
// 4.5 Pool Operations
// -----------------------------------------------------------------------------

/**
 * Allocate storage for a new object in the pool.
 *
 * @param source_data Pointer to the object data to copy into the pool
 * @param size        Size of the object in bytes
 * @param type_id     Runtime type ID for destructor lookup
 * @return Stable PoolId that can be used to reference this object
 *
 * Algorithm:
 * 1. Get a PoolId (recycle from stack, or allocate fresh)
 * 2. Create a PoolSlot with object metadata
 * 3. Copy data to inline storage or arena based on size
 * 4. Append slot to packed array
 * 5. Update bidirectional index mappings
 */
fn PoolId Pool.allocate(Pool* self, void* source_data, usz size, typeid type_id) {
    // --- Get or recycle a stable pool ID ---
    PoolId pid;
    if (self.recycled_pool_ids.len() > 0) {
        // Reuse a recycled ID
        pid = self.recycled_pool_ids.pop()!!;
    } else {
        // Allocate a fresh ID
        pid = self.next_pool_id;
        self.next_pool_id = (PoolId)((uint)self.next_pool_id + 1);

        // Grow sparse index to accommodate new ID
        while (self.pool_id_position_index.len() <= (usz)(uint)pid) {
            self.pool_id_position_index.push((PoolIndex)0);
        }
    }

    // --- Create the slot with object metadata ---
    PoolSlot slot;
    slot.size = size;
    slot.type_id = type_id;
    slot.owner_id = pid;

    // --- Store data based on size (inline vs. arena) ---
    if (size <= INLINE_THRESHOLD) {
        // INLINE: store directly in slot's inline_data array
        slot.is_inline = true;
        mem::copy(&slot.inline_data[0], source_data, size);
    } else {
        // ARENA: allocate from arena and copy data there
        slot.is_inline = false;
        void* arena_ptr = self.arena_alloc(size);
        mem::copy(arena_ptr, source_data, size);
        slot.heap_ptr = arena_ptr;
    }

    // --- Append to packed array ---
    PoolIndex packed_position = (PoolIndex)self.slot_count;

    if (self.packed_slots.len() <= (usz)self.slot_count) {
        // Need to grow the arrays
        self.packed_slots.push(slot);
        self.position_owner_index.push(pid);
    } else {
        // Reuse existing capacity (from previous removals)
        self.packed_slots[(usz)self.slot_count] = slot;
        self.position_owner_index[(usz)self.slot_count] = pid;
    }

    // --- Update bidirectional index ---
    self.pool_id_position_index[(usz)(uint)pid] = packed_position;
    self.slot_count += 1;

    return pid;
}

/**
 * Remove an object from the pool.
 *
 * Uses swap-and-pop compaction to maintain packed storage:
 * 1. Free the object's storage (arena → free-list, inline → no-op)
 * 2. If not the last slot, move last slot into the gap
 * 3. Update indices for the moved slot
 * 4. Decrement slot count (the "pop")
 * 5. Recycle the PoolId for future reuse
 *
 * @param pid The PoolId of the object to remove
 *
 * Note: This does NOT run the destructor. That's the caller's responsibility
 * (typically Region.free_object or Pool.destroy_all).
 */
fn void Pool.remove(Pool* self, PoolId pid) {
    PoolIndex position = self.pool_id_position_index[(usz)(uint)pid];
    uint last_position = self.slot_count - 1;

    PoolSlot* slot = &self.packed_slots[(usz)(uint)position];

    // --- Free the object's storage ---
    if (!slot.is_inline) {
        // Arena-allocated: add to free-list for reuse
        self.arena_free(slot.heap_ptr, slot.size);
    }
    // Inline objects: no cleanup needed (storage is part of PoolSlot)

    // --- Swap-and-pop compaction ---
    if ((uint)position != last_position) {
        // Move the last slot into the gap left by removal
        PoolId displaced_owner = self.position_owner_index[(usz)last_position];

        self.packed_slots[(usz)(uint)position] =
            self.packed_slots[(usz)last_position];
        self.position_owner_index[(usz)(uint)position] = displaced_owner;

        // Update the moved slot's PoolId → position mapping
        self.pool_id_position_index[(usz)(uint)displaced_owner] = position;
    }

    // --- Finalize removal ---
    self.slot_count -= 1;
    self.recycled_pool_ids.push(pid);  // Recycle the ID for reuse
}

/**
 * Get the PoolSlot for a given PoolId.
 *
 * @param pid The PoolId to look up
 * @return Pointer to the PoolSlot (valid until next remove)
 */
fn PoolSlot* Pool.get_slot(Pool* self, PoolId pid) {
    PoolIndex position = self.pool_id_position_index[(usz)(uint)pid];
    return &self.packed_slots[(usz)(uint)position];
}

/**
 * Get a pointer to the raw object data for a given PoolId.
 *
 * Handles both inline and arena-allocated objects transparently.
 *
 * @param pid The PoolId to look up
 * @return Pointer to the object data (valid until object is removed)
 */
fn void* Pool.get_data(Pool* self, PoolId pid) {
    PoolSlot* slot = self.get_slot(pid);
    if (slot.is_inline) {
        return (void*)&slot.inline_data[0];
    } else {
        return slot.heap_ptr;
    }
}

/**
 * Destroy all objects in the pool and release all memory.
 *
 * Called when a region is destroyed. This:
 * 1. Runs destructors for all live objects
 * 2. Frees all arena blocks
 * 3. Frees all internal lists
 *
 * After this call, the Pool is in an invalid state and should not be used.
 */
fn void Pool.destroy_all(Pool* self) {
    // --- Run destructors for all live objects ---
    for (uint i = 0; i < self.slot_count; i++) {
        PoolSlot* slot = &self.packed_slots[(usz)i];
        void* data = slot.is_inline ? (void*)&slot.inline_data[0] : slot.heap_ptr;
        destructor_run(slot.type_id, data);
    }

    // --- Free all arena blocks ---
    foreach (&arena : self.arenas) {
        mem::free(arena.data);
    }

    // --- Free internal lists ---
    self.packed_slots.free();
    self.pool_id_position_index.free();
    self.position_owner_index.free();
    self.recycled_pool_ids.free();
    self.arenas.free();
    self.free_list.free();
}

// =============================================================================
// SECTION 5: SLOT TABLE
// =============================================================================
//
// The Slot Table is an indirection layer between ObjectHandles and the Pool.
// Each entry (ObjectRecord) tracks the routing state of one object:
//
// - LIVE: Object exists in this region's pool. live_info.pool_id locates it.
// - FORWARDED: Object was promoted to another region. forward_target is new handle.
// - DEAD: Slot was freed and may be recycled. Handle is invalid.
//
// Why indirection?
// - Handles can remain stable when objects move (via forwarding)
// - Enables generation-based validation without scanning all handles
// - Supports object promotion when regions die
//

/**
 * SlotKind — The routing state of an ObjectRecord.
 *
 * DEPRECATED: This enum is kept for compatibility with ghost tables but
 * the primary ObjectRecord now uses escaped/forwarded booleans instead.
 *
 * LIVE:      Object data exists in this region's pool
 * FORWARDED: Object was promoted; follow forward_target to find it
 * DEAD:      Slot is free and will be recycled; handle is invalid
 */
enum SlotKind : char {
    LIVE,
    FORWARDED,
    DEAD
}

// -----------------------------------------------------------------------------
// 5.1 Object Record
// -----------------------------------------------------------------------------

/**
 * ObjectLiveInfo — Metadata for a LIVE object.
 *
 * When SlotKind == LIVE, this tells us where to find the object:
 *   pool_id — The stable ID in this region's Pool
 *   size    — Object size in bytes (cached for promotion)
 *   type_id — Runtime type ID (cached for destructor dispatch)
 */
struct ObjectLiveInfo {
    PoolId pool_id;     // Stable reference into Pool
    usz    size;        // Object size in bytes
    typeid type_id;     // Runtime type for destructor
}

/**
 * ObjectRecord — Per-slot metadata with escape tracking.
 *
 * This is the core of the indirection layer. An ObjectHandle's slot_id
 * indexes into the SlotTable to find the ObjectRecord.
 *
 * States:
 *   !forwarded && !escaped → LOCAL: object lives here, dies with region
 *   !forwarded && escaped  → ESCAPED: object lives here, will be promoted
 *   forwarded              → FORWARDED: object was promoted, follow forward_target
 *
 * The generation counter is incremented on each reuse. A handle is only
 * valid if its generation matches the record's current generation.
 */
struct ObjectRecord {
    Generation generation;  // Validity check - incremented on each slot reuse

    bool escaped;           // Handle crossed region boundary?
    bool forwarded;         // Was promoted to parent?

    // Tagged union based on `forwarded`:
    union {
        ObjectLiveInfo live_info;       // If !forwarded: location + metadata
        ObjectHandle   forward_target;  // If forwarded: where it went
    }
}

// -----------------------------------------------------------------------------
// 5.2 Slot Table Structure
// -----------------------------------------------------------------------------

/**
 * SlotTable — Manages ObjectRecords and slot ID recycling.
 *
 * Fields:
 *   object_records    — Array of ObjectRecords indexed by SlotId
 *   recycled_slot_ids — Stack of freed SlotIds available for reuse
 *   live_slot_tracker — SparseSet for O(1) live slot enumeration
 */
struct SlotTable {
    List{ObjectRecord} object_records;     // Indexed by SlotId
    List{SlotId}       recycled_slot_ids;  // Free slots for recycling
    SparseSet          live_slot_tracker;  // Track which slots are alive
}

// -----------------------------------------------------------------------------
// 5.3 Slot Table Operations
// -----------------------------------------------------------------------------

/**
 * Result of allocating a new slot.
 * Contains both the SlotId and the new generation for building handles.
 */
struct SlotAllocationResult {
    SlotId     slot_id;
    Generation generation;
}

/**
 * Allocate a new slot in the table.
 *
 * Either recycles a previously freed slot (with bumped generation)
 * or allocates a fresh slot.
 *
 * @return SlotAllocationResult with the new slot_id and generation
 *
 * Note: The caller must fill in the record's live_info after allocation.
 * The slot is initialized with escaped=false and forwarded=false.
 */
fn SlotAllocationResult SlotTable.allocate(SlotTable* self) {
    SlotId sid;

    if (self.recycled_slot_ids.len() > 0) {
        // Recycle a previously freed slot
        sid = self.recycled_slot_ids.pop()!!;
    } else {
        // Allocate a fresh slot at the end of the array
        sid = (SlotId)self.object_records.len();
        ObjectRecord new_record;
        new_record.generation = (Generation)0;
        new_record.escaped = false;
        new_record.forwarded = false;
        self.object_records.push(new_record);
    }

    // Bump generation to invalidate any old handles to this slot
    usz idx = (usz)(uint)sid;
    self.object_records[idx].generation =
        (Generation)((uint)self.object_records[idx].generation + 1);
    Generation gen = self.object_records[idx].generation;

    // Initialize escape tracking fields for the new allocation
    self.object_records[idx].escaped = false;
    self.object_records[idx].forwarded = false;

    // Track this slot as live
    self.live_slot_tracker.insert((uint)sid);

    SlotAllocationResult result;
    result.slot_id = sid;
    result.generation = gen;
    return result;
}

/**
 * Release a slot back to the recycling pool.
 *
 * Bumps generation (invalidating old handles) and makes slot available for reuse.
 * The slot is no longer considered live after this.
 *
 * @param sid The SlotId to release
 */
fn void SlotTable.release(SlotTable* self, SlotId sid) {
    usz idx = (usz)(uint)sid;

    // Bump generation so old handles become invalid
    self.object_records[idx].generation =
        (Generation)((uint)self.object_records[idx].generation + 1);

    // Reset state flags
    self.object_records[idx].escaped = false;
    self.object_records[idx].forwarded = false;

    // Remove from live tracking and add to recycle stack
    self.live_slot_tracker.remove((uint)sid);
    self.recycled_slot_ids.push(sid);
}

/**
 * Get the ObjectRecord for a given SlotId.
 *
 * @param sid The SlotId to look up
 * @return Pointer to the ObjectRecord
 */
fn ObjectRecord* SlotTable.get_record(SlotTable* self, SlotId sid) {
    return &self.object_records[(usz)(uint)sid];
}

/**
 * Destroy the slot table and release all memory.
 */
fn void SlotTable.destroy_all(SlotTable* self) {
    self.object_records.free();
    self.recycled_slot_ids.free();
    self.live_slot_tracker.release();
}

// =============================================================================
// SECTION 6: GHOST TABLE
// =============================================================================
//
// When a region dies but its objects were promoted to a parent, the original
// handles still reference the dead region's ID. A GhostTable is a snapshot
// of the dead region's forwarding records that allows those handles to resolve.
//
// The ghost table is inherited by the parent region. When resolving a handle
// to a dead region, we search the parent's ghost tables for the matching
// source region ID and generation.
//

/**
 * GhostTable — Remnant of a dead region's slot table.
 *
 * Contains only forwarding records (FORWARDED entries) so that existing
 * handles pointing to the dead region can still resolve to their new locations.
 *
 * Fields:
 *   source_region_id   — The ID of the region that died
 *   source_generation  — The generation of that region when it died
 *   forwarding_records — The slot table's object records (all FORWARDED or DEAD)
 */
struct GhostTable {
    RegionId           source_region_id;    // Which region died
    Generation         source_generation;   // Its generation at death
    List{ObjectRecord} forwarding_records;  // Forwarding stubs for lookup
}

// =============================================================================
// SECTION 7: REGION
// =============================================================================
//
// A Region is a hierarchical memory container. Key properties:
//
// - Regions form a tree with a single root
// - Each region owns a Pool (object storage) and SlotTable (indirection)
// - Regions are reference-counted (+1 for parent, +1 for each child)
// - When refcount hits 0, the region dies and objects promote to parent
//
// Object promotion on region death:
// 1. For each LIVE object, allocate a copy in parent region
// 2. Convert original slot to FORWARDED pointing to new handle
// 3. Create ghost table from slot table (for handle resolution)
// 4. Pass ghost table to parent
//

/** Sentinel value for "no parent" (root region). */
const RegionId NO_PARENT = (RegionId)uint.max;

/**
 * Region — A hierarchical memory container.
 *
 * Fields:
 *   id, generation — Identity (generation for handle validation)
 *   refcount       — Reference count (parent + children hold refs)
 *   parent         — Parent region ID (NO_PARENT for root)
 *   pool           — Object storage (packed PoolSlots)
 *   slot_table     — Indirection layer (ObjectRecords)
 *   inherited_ghost_tables — Ghost tables from dead child regions
 *   live_object_count — Number of live objects (for promotion decision)
 */
struct Region {
    // --- Identity ---
    RegionId   id;
    Generation generation;

    // --- Lifetime ---
    RefCount refcount;
    RegionId parent;  // NO_PARENT for root

    // --- Storage ---
    Pool      pool;
    SlotTable slot_table;
    List{GhostTable} inherited_ghost_tables;

    // --- Metadata ---
    uint live_object_count;
}

// -----------------------------------------------------------------------------
// 7.2 Typed Object Allocation (compile-time generic)
// -----------------------------------------------------------------------------

/**
 * Allocate a typed object in this region.
 *
 * This is a compile-time macro that infers size and type from the value.
 * The object is copied into the region's pool.
 *
 * Usage:
 *   int x = 42;
 *   ObjectHandle h = region.allocate_typed(&region, int, x);
 *
 * @param $Type The type of the object (compile-time)
 * @param value The value to copy into the region
 * @return ObjectHandle for accessing the allocated object
 */
macro ObjectHandle Region.allocate_typed(&self, $Type, value) {
    void* source_data = (void*)&value;
    usz size = $Type.sizeof;
    typeid tid = $Type.typeid;

    // Allocate storage in pool
    PoolId pid = self.pool.allocate(source_data, size, tid);

    // Allocate slot for indirection
    SlotAllocationResult slot_result = self.slot_table.allocate();
    SlotId slot_id = slot_result.slot_id;
    Generation gen = slot_result.generation;

    // Initialize the object record (escaped/forwarded already false from allocate)
    ObjectRecord* record = self.slot_table.get_record(slot_id);
    record.live_info.pool_id = pid;
    record.live_info.size = size;
    record.live_info.type_id = tid;

    self.live_object_count += 1;

    // Build and return handle
    ObjectHandle handle;
    handle.region_id = self.id;
    handle.slot_id = slot_id;
    handle.generation = gen;
    return handle;
}

// -----------------------------------------------------------------------------
// 7.3 Raw Object Allocation
// -----------------------------------------------------------------------------

/**
 * Allocate raw bytes as an object in this region.
 *
 * Used internally for object promotion and when the caller has
 * already prepared the data.
 *
 * @param data   Pointer to the source data
 * @param size   Size in bytes
 * @param tid    Runtime type ID for destructor dispatch
 * @return ObjectHandle for the new object
 */
fn ObjectHandle Region.allocate_raw(Region* self, void* data, usz size, typeid tid) {
    // Allocate storage in pool
    PoolId pid = self.pool.allocate(data, size, tid);

    // Allocate slot for indirection
    SlotAllocationResult slot_result = self.slot_table.allocate();
    SlotId slot_id = slot_result.slot_id;
    Generation gen = slot_result.generation;

    // Initialize the object record (escaped/forwarded already false from allocate)
    ObjectRecord* record = self.slot_table.get_record(slot_id);
    record.live_info.pool_id = pid;
    record.live_info.size = size;
    record.live_info.type_id = tid;

    self.live_object_count += 1;

    // Build and return handle
    ObjectHandle handle;
    handle.region_id = self.id;
    handle.slot_id = slot_id;
    handle.generation = gen;
    return handle;
}

// -----------------------------------------------------------------------------
// 7.4 Explicit Object Deallocation
// -----------------------------------------------------------------------------

/**
 * Explicitly free an object before its region dies.
 *
 * This is optional — objects are automatically freed when their region dies.
 * Use this for early cleanup of expensive resources.
 *
 * @param handle The object to free
 *
 * Behavior by state:
 * - !forwarded: Run destructor, remove from pool, release slot
 * - forwarded: Just release the forwarding stub
 * - generation mismatch: No-op (already freed)
 */
fn void Region.free_object(Region* self, ObjectHandle handle) {
    ObjectRecord* record = self.slot_table.get_record(handle.slot_id);

    // Check for stale handle (generation mismatch means already freed)
    if ((uint)record.generation != (uint)handle.generation) {
        return;  // Already freed — no-op
    }

    if (record.forwarded) {
        // Object was promoted — just clean up the forwarding stub
        self.slot_table.release(handle.slot_id);
    } else {
        // Object is live here — run destructor and remove
        PoolSlot* pool_slot = self.pool.get_slot(record.live_info.pool_id);
        void* data = pool_slot.is_inline ? (void*)&pool_slot.inline_data[0] : pool_slot.heap_ptr;

        // Run destructor, remove from pool, release slot
        destructor_run(record.live_info.type_id, data);
        self.pool.remove(record.live_info.pool_id);
        self.slot_table.release(handle.slot_id);
        self.live_object_count -= 1;
    }
}

// -----------------------------------------------------------------------------
// 7.5 Region Destruction Internals
// -----------------------------------------------------------------------------

/**
 * Destroy the region's internal data structures.
 *
 * Called after all objects have been promoted or destroyed.
 * Frees pool, slot table, and inherited ghost tables.
 *
 * Note: If this region had a parent, inherited ghost tables were already
 * promoted to the parent in Phase 3b (promote_inherited_ghost_tables),
 * so the list is empty and we just free the list structure itself.
 * If this region had no parent (root), we free the ghost table contents here.
 */
fn void Region.destroy_internals(Region* self) {
    self.pool.destroy_all();
    self.slot_table.destroy_all();

    // Free inherited ghost tables (if any remain - typically empty after promotion)
    // Only non-empty if this is the root region or promotion was skipped
    foreach (&ghost : self.inherited_ghost_tables) {
        ghost.forwarding_records.free();
    }
    self.inherited_ghost_tables.free();
}

// =============================================================================
// SECTION 8: DESTRUCTOR REGISTRY
// =============================================================================
//
// The destructor registry maps type IDs to destructor functions.
// When an object is freed or its region dies, we look up the type's
// destructor and call it with a pointer to the object data.
//
// This enables RAII-style cleanup for types that need it (file handles,
// network connections, etc.).
//

/** Function signature for destructors: takes a pointer to the object. */
alias DestructorFn = fn void(void* ptr);

/** Fault for when a destructor is not found in the registry. */
faultdef DESTRUCTOR_NOT_FOUND;

/** Sentinel value for empty slots in the destructor registry hash map. */
const usz DESTRUCTOR_EMPTY_KEY = usz.max;

/**
 * DestructorRegistryEntry - A single entry in the hash map.
 */
struct DestructorRegistryEntry {
    usz          key;   // typeid cast to usz, or DESTRUCTOR_EMPTY_KEY if empty
    DestructorFn func;  // The destructor function
}

/**
 * DestructorRegistry - O(1) hash map for type ID to destructor lookup.
 *
 * Uses open-addressing with linear probing. Automatically resizes when
 * load factor exceeds 75%. Initial capacity is 16 slots.
 */
struct DestructorRegistry {
    List{DestructorRegistryEntry} entries;
    usz count;
    usz capacity;
}

/**
 * Hash function for typeid keys.
 * Uses splitmix64-style mixing for good distribution.
 */
fn usz destructor_registry_hash(usz key) @inline {
    ulong h = (ulong)key;
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccd;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53;
    h ^= h >> 33;
    return (usz)h;
}

/**
 * Initialize the destructor registry.
 * Must be called before any register/find operations.
 */
fn void DestructorRegistry.init(DestructorRegistry* self) {
    usz initial_capacity = 16;
    self.capacity = initial_capacity;
    self.count = 0;
    self.entries = {};  // Ensure we start with an empty list
    for (usz i = 0; i < initial_capacity; i++) {
        DestructorRegistryEntry empty = { .key = DESTRUCTOR_EMPTY_KEY, .func = null };
        self.entries.push(empty);
    }
}

/**
 * Release all resources held by the registry.
 */
fn void DestructorRegistry.release(DestructorRegistry* self) {
    self.entries.free();
    self.count = 0;
    self.capacity = 0;
}

/**
 * Resize the hash map when load factor is too high.
 */
fn void DestructorRegistry.resize(DestructorRegistry* self) {
    usz new_capacity = self.capacity * 2;
    List{DestructorRegistryEntry} old_entries = self.entries;
    self.entries = {};
    for (usz i = 0; i < new_capacity; i++) {
        DestructorRegistryEntry empty = { .key = DESTRUCTOR_EMPTY_KEY, .func = null };
        self.entries.push(empty);
    }
    self.capacity = new_capacity;
    self.count = 0;
    foreach (&entry : old_entries) {
        if (entry.key != DESTRUCTOR_EMPTY_KEY) {
            self.insert_entry(entry.key, entry.func);
        }
    }
    old_entries.free();
}

/**
 * Internal: Insert an entry by raw key.
 */
fn void DestructorRegistry.insert_entry(DestructorRegistry* self, usz key, DestructorFn func) {
    if (self.count * 4 >= self.capacity * 3) {
        self.resize();
    }
    usz mask = self.capacity - 1;
    usz idx = destructor_registry_hash(key) & mask;
    while (self.entries[idx].key != DESTRUCTOR_EMPTY_KEY) {
        if (self.entries[idx].key == key) {
            self.entries[idx].func = func;
            return;
        }
        idx = (idx + 1) & mask;
    }
    self.entries[idx].key = key;
    self.entries[idx].func = func;
    self.count++;
}

/**
 * Register a destructor for a type.
 *
 * @param tid  The runtime type ID
 * @param func The destructor function to call
 */
fn void DestructorRegistry.register(DestructorRegistry* self, typeid tid, DestructorFn func) {
    usz key = (usz)tid;
    self.insert_entry(key, func);
}

/**
 * Find the destructor for a type. O(1) average case.
 *
 * @param tid The runtime type ID to look up
 * @return The destructor function, or null if none registered
 */
fn DestructorFn? DestructorRegistry.find(DestructorRegistry* self, typeid tid) {
    if (self.capacity == 0) {
        return DESTRUCTOR_NOT_FOUND~;
    }
    usz key = (usz)tid;
    usz mask = self.capacity - 1;
    usz idx = destructor_registry_hash(key) & mask;
    usz start = idx;
    do {
        if (self.entries[idx].key == key) {
            return self.entries[idx].func;
        }
        if (self.entries[idx].key == DESTRUCTOR_EMPTY_KEY) {
            return DESTRUCTOR_NOT_FOUND~;
        }
        idx = (idx + 1) & mask;
    } while (idx != start);
    return DESTRUCTOR_NOT_FOUND~;
}

/** Global destructor registry instance. */
DestructorRegistry g_destructor_registry;

/**
 * Run the destructor for an object (if one is registered).
 *
 * Called automatically when objects are freed.
 *
 * @param tid The object's runtime type ID
 * @param ptr Pointer to the object data
 */
fn void destructor_run(typeid tid, void* ptr) {
    if (try func = g_destructor_registry.find(tid)) {
        func(ptr);
    }
}

// =============================================================================
// SECTION 9: REGION REGISTRY
// =============================================================================
//
// The RegionRegistry is the central coordinator for all regions.
// It manages:
// - Region storage and lifecycle
// - Parent-child relationships
// - ID generation and recycling
// - Ghost table indexing
//
// The registry maintains a single root region that is the ultimate parent
// of all other regions. The root never dies (it has permanent refcount).
//

/**
 * RegionRegistry — Central manager for all regions in the system.
 *
 * Key operations:
 * - init(): Create the root region
 * - create_region(): Create a child region under a parent
 * - retain_region(): Increment refcount
 * - release_region(): Decrement refcount (may trigger destruction)
 * - is_valid_object(): Check if an ObjectHandle is still valid
 * - dereference(): Get a pointer to an object's data
 */
struct RegionRegistry {
    // --- Region storage ---
    List{Region} region_storage;       // Indexed by RegionId
    List{bool}   region_alive_flags;   // Is region at index alive?
    List{Generation} region_generations; // Current generation per slot

    // --- Liveness tracking ---
    SparseSet live_region_tracker;     // O(1) live region enumeration

    // --- ID recycling ---
    List{RegionId} recycled_region_ids; // Free IDs for reuse
    RegionId       next_region_id;      // Next fresh ID if recycle empty

    // --- Parent-child relationships ---
    List{List{RegionId}} child_region_lists; // parent_id → list of child IDs

    // --- Ghost table index ---
    // O(1) hash map lookup: (region_id, generation) -> (host_id, ghost_idx)
    GhostIndex ghost_index;

    // --- Root region ---
    RegionId root_id;
}

// -----------------------------------------------------------------------------
// 9.2 Initialization
// -----------------------------------------------------------------------------

/**
 * Initialize the registry and create the root region.
 *
 * The root region:
 * - Has ID 0 and generation 1
 * - Has NO_PARENT (it's the ultimate ancestor)
 * - Has refcount 1 (permanent, never released)
 * - Is the default parent for new regions
 */
fn void RegionRegistry.init(RegionRegistry* self) {
    RegionId root = (RegionId)0;
    Generation gen = (Generation)1;

    // Create root region
    Region root_region;
    root_region.id = root;
    root_region.generation = gen;
    root_region.refcount = (RefCount)1;  // Permanent — never released
    root_region.parent = NO_PARENT;
    root_region.live_object_count = 0;

    // Initialize storage
    self.region_storage.push(root_region);
    self.region_alive_flags.push(true);
    self.region_generations.push(gen);
    self.live_region_tracker.insert(0);

    // Initialize empty child list for root
    List{RegionId} empty_list;
    self.child_region_lists.push(empty_list);

    // Initialize ghost index for O(1) ghost table lookup
    self.ghost_index.init(16);

    self.root_id = root;
    self.next_region_id = (RegionId)1;
}

// -----------------------------------------------------------------------------
// 9.3 Region Creation
// -----------------------------------------------------------------------------

/**
 * Create a new child region under a parent.
 *
 * @param parent_handle Handle to the parent region.
 *                      Pass INVALID_REGION_HANDLE to create under root.
 * @return Handle to the newly created region
 *
 * The new region:
 * - Gets a fresh or recycled RegionId
 * - Has generation = previous generation + 1
 * - Has refcount 2 (+1 creator, +1 parent)
 * - Is registered as a child of the parent
 */
fn RegionHandle RegionRegistry.create_region(RegionRegistry* self, RegionHandle parent_handle) {
    // --- Resolve parent (default to root if invalid handle) ---
    RegionId parent_id;
    if (self.is_valid_region(parent_handle)) {
        parent_id = parent_handle.region_id;
    } else {
        parent_id = self.root_id;
    }

    // --- Get or recycle a RegionId ---
    RegionId id;
    if (self.recycled_region_ids.len() > 0) {
        id = self.recycled_region_ids.pop()!!;
    } else {
        id = self.next_region_id;
        self.next_region_id = (RegionId)((uint)self.next_region_id + 1);
    }

    // --- Grow storage arrays if needed ---
    usz idx = (usz)(uint)id;
    while (self.region_generations.len() <= idx) {
        self.region_generations.push((Generation)0);
        self.region_storage.push({});
        self.region_alive_flags.push(false);
        self.child_region_lists.push({});
    }

    // --- Bump generation to invalidate old handles ---
    self.region_generations[idx] =
        (Generation)((uint)self.region_generations[idx] + 1);
    Generation gen = self.region_generations[idx];

    // --- Create the region ---
    Region region;
    region.id = id;
    region.generation = gen;
    region.refcount = (RefCount)2;  // +1 creator, +1 parent
    region.parent = parent_id;
    region.live_object_count = 0;

    self.region_storage[idx] = region;
    self.region_alive_flags[idx] = true;
    self.live_region_tracker.insert((uint)id);

    // --- Register as child of parent ---
    usz parent_idx = (usz)(uint)parent_id;
    self.child_region_lists[parent_idx].push(id);

    // --- Return handle ---
    RegionHandle result;
    result.region_id = id;
    result.generation = gen;
    return result;
}

// -----------------------------------------------------------------------------
// 9.4 Reference Counting
// -----------------------------------------------------------------------------

/**
 * Increment a region's reference count.
 *
 * Call this when taking a new reference to a region (e.g., storing the handle).
 *
 * @param handle The region to retain
 * @return The same handle (for chaining)
 */
fn RegionHandle RegionRegistry.retain_region(RegionRegistry* self, RegionHandle handle) {
    assert(self.is_valid_region(handle));
    usz idx = (usz)(uint)handle.region_id;
    self.region_storage[idx].refcount =
        (RefCount)((uint)self.region_storage[idx].refcount + 1);
    return handle;
}

/**
 * Decrement a region's reference count.
 *
 * If refcount reaches 0, the region is destroyed:
 * - Objects are promoted to parent
 * - Children are reparented or destroyed
 * - Ghost table is transferred to parent
 *
 * @param handle The region to release
 */
fn void RegionRegistry.release_region(RegionRegistry* self, RegionHandle handle) {
    assert(self.is_valid_region(handle));
    usz idx = (usz)(uint)handle.region_id;
    self.region_storage[idx].refcount =
        (RefCount)((uint)self.region_storage[idx].refcount - 1);

    if ((uint)self.region_storage[idx].refcount == 0) {
        self.destroy_region(handle.region_id);
    }
}

// -----------------------------------------------------------------------------
// 9.5 Region Destruction (with automatic promotion)
// -----------------------------------------------------------------------------

/**
 * Destroy a region and promote its objects to the parent.
 *
 * This is called when refcount reaches 0. The destruction proceeds in phases:
 *
 * PHASE 0: INVALIDATE REGION CONTINUATIONS
 *   Mark any continuations belonging to this region as invalidated.
 *
 * PHASE 1: PROMOTE SURVIVORS TO PARENT
 *   For each LIVE object, allocate a copy in parent and convert to FORWARDED.
 *
 * PHASE 2: RELEASE CHILDREN
 *   Decrement each child's refcount. If child survives, reparent to grandparent.
 *
 * PHASE 3: TRANSFER GHOST TABLE
 *   If we have forwarding records, create a GhostTable and give it to parent.
 *
 * PHASE 4: REMOVE FROM PARENT'S CHILD LIST
 *   Clean up the parent's child list.
 *
 * PHASE 5: DESTROY INTERNALS
 *   Free pool, slot table, inherited ghost tables.
 *
 * PHASE 6: RECYCLE REGION ID
 *   Mark as dead and add ID to recycle stack.
 *
 * @param id The RegionId to destroy
 */
fn void RegionRegistry.destroy_region(RegionRegistry* self, RegionId id) {
    usz idx = (usz)(uint)id;
    Region* region = &self.region_storage[idx];
    RegionId parent_id = region.parent;
    bool has_parent = (parent_id != NO_PARENT);

    // ============================================================
    //  PHASE 0: INVALIDATE REGION CONTINUATIONS
    // ============================================================
    // Any continuations whose home_region is this dying region
    // must be marked as invalidated so they cannot be resumed
    if (g_prompt_stack != null) {
        g_prompt_stack.invalidate_region_continuations(id, region.generation);
    }

    // ============================================================
    //  PHASE 1: PROMOTE ESCAPED OBJECTS, DESTROY LOCAL OBJECTS
    // ============================================================
    // Only objects that escaped (handles crossed region boundary) are promoted.
    // Local objects (never escaped) are destroyed with the region.
    if (has_parent) {
        usz parent_idx = (usz)(uint)parent_id;
        Region* parent_region = &self.region_storage[parent_idx];

        uint[] live_slots = region.slot_table.live_slot_tracker.get_live_keys();

        for (usz i = 0; i < live_slots.len; i++) {
            SlotId sid = (SlotId)live_slots[i];
            ObjectRecord* record = region.slot_table.get_record(sid);

            if (record.forwarded) {
                continue;  // Already promoted
            }

            if (record.escaped) {
                // Object escaped: promote to parent
                PoolSlot* pool_slot = region.pool.get_slot(record.live_info.pool_id);
                void* data = pool_slot.is_inline ?
                    (void*)&pool_slot.inline_data[0] : pool_slot.heap_ptr;

                ObjectHandle new_handle = parent_region.allocate_raw(
                    data,
                    pool_slot.size,
                    pool_slot.type_id
                );

                record.forwarded = true;
                record.forward_target = new_handle;
            } else {
                // Object is local: destroy with region
                PoolSlot* pool_slot = region.pool.get_slot(record.live_info.pool_id);
                void* data = pool_slot.is_inline ?
                    (void*)&pool_slot.inline_data[0] : pool_slot.heap_ptr;

                destructor_run(record.live_info.type_id, data);
                // Slot will be cleaned up with region
            }
        }
    }

    // ============================================================
    //  PHASE 2: RELEASE CHILDREN (reparent survivors)
    // ============================================================
    // Each child loses its reference from this region.
    // Surviving children get reparented to the grandparent.
    usz child_list_idx = (usz)(uint)id;

    // Snapshot children (list may change during iteration)
    List{RegionId} children_snapshot;
    foreach (child_id : self.child_region_lists[child_list_idx]) {
        children_snapshot.push(child_id);
    }

    foreach (child_id : children_snapshot) {
        usz child_idx = (usz)(uint)child_id;
        if (self.region_alive_flags[child_idx]) {
            Region* child = &self.region_storage[child_idx];
            child.refcount = (RefCount)((uint)child.refcount - 1);

            if ((uint)child.refcount == 0) {
                // Child dies too — recursive destruction
                self.destroy_region(child_id);
            } else {
                // Child survives — reparent to grandparent
                child.parent = parent_id;
                if (has_parent) {
                    usz grandparent_idx = (usz)(uint)parent_id;
                    self.child_region_lists[grandparent_idx].push(child_id);
                    child.refcount = (RefCount)((uint)child.refcount + 1);
                }
            }
        }
    }
    children_snapshot.free();
    self.child_region_lists[child_list_idx].clear();

    // ============================================================
    //  PHASE 3: TRANSFER GHOST TABLE TO PARENT
    // ============================================================
    // If we have forwarding records, parent needs to keep them
    // so existing handles to this dead region can still resolve
    self.transfer_ghost_table(id, parent_id, has_parent);

    // ============================================================
    //  PHASE 3b: PROMOTE INHERITED GHOST TABLES
    // ============================================================
    // If this region inherited ghost tables from its dead children,
    // those must be promoted to the parent so handles to deeply
    // nested dead regions can still resolve
    if (has_parent && region.inherited_ghost_tables.len() > 0) {
        self.promote_inherited_ghost_tables(id, parent_id);
    }

    // ============================================================
    //  PHASE 4: REMOVE FROM PARENT'S CHILD LIST
    // ============================================================
    if (has_parent) {
        usz parent_idx_remove = (usz)(uint)parent_id;
        List{RegionId}* parent_children = &self.child_region_lists[parent_idx_remove];

        // Find and remove this region from parent's child list (swap-and-pop)
        for (usz i = 0; i < parent_children.len(); i++) {
            if ((uint)(*parent_children)[i] == (uint)id) {
                usz last_idx = parent_children.len() - 1;
                if (i != last_idx) {
                    (*parent_children)[i] = (*parent_children)[last_idx];
                }
                (void)parent_children.pop();
                break;
            }
        }
    }

    // ============================================================
    //  PHASE 5: DESTROY INTERNALS
    // ============================================================
    region.destroy_internals();

    // ============================================================
    //  PHASE 6: RECYCLE REGION ID
    // ============================================================
    self.live_region_tracker.remove((uint)id);
    self.region_alive_flags[idx] = false;
    self.recycled_region_ids.push(id);
}

// -----------------------------------------------------------------------------
// 9.6 Ghost Table Transfer
// -----------------------------------------------------------------------------

/**
 * Transfer this region's slot table to the parent as a ghost table.
 *
 * Called during region destruction. If any slots are FORWARDED, we need
 * to preserve them so handles to this dead region can still resolve.
 *
 * @param dying_id   The region being destroyed
 * @param parent_id  The parent region (or NO_PARENT)
 * @param has_parent Whether parent exists
 */
fn void RegionRegistry.transfer_ghost_table(
    RegionRegistry* self, RegionId dying_id, RegionId parent_id, bool has_parent
) {
    if (!has_parent) return;

    usz dying_idx = (usz)(uint)dying_id;
    Region* dying_region = &self.region_storage[dying_idx];

    // --- Check if any forwarding stubs exist ---
    bool has_forwarding_records = false;
    uint[] live_slots = dying_region.slot_table.live_slot_tracker.get_live_keys();
    for (usz i = 0; i < live_slots.len; i++) {
        ObjectRecord* record =
            dying_region.slot_table.get_record((SlotId)live_slots[i]);
        if (record.forwarded) {
            has_forwarding_records = true;
            break;
        }
    }

    // No forwarding records — nothing to transfer
    if (!has_forwarding_records) return;

    // --- Create ghost table from slot table ---
    GhostTable ghost = {
        .source_region_id   = dying_id,
        .source_generation  = dying_region.generation,
        .forwarding_records = dying_region.slot_table.object_records  // Move ownership
    };

    // Add to parent's ghost table list and get the index
    usz parent_idx = (usz)(uint)parent_id;
    usz ghost_idx = self.region_storage[parent_idx].inherited_ghost_tables.len();
    self.region_storage[parent_idx].inherited_ghost_tables.push(ghost);

    // Register in ghost index for O(1) lookup
    self.ghost_index.insert(dying_id, dying_region.generation, parent_id, ghost_idx);
}

// -----------------------------------------------------------------------------
// 9.6b Inherited Ghost Table Promotion
// -----------------------------------------------------------------------------

/**
 * Promote inherited ghost tables from a dying region to its parent.
 *
 * When a region dies, it may have inherited ghost tables from its own dead
 * children. These ghost tables must be transferred to the grandparent so that
 * handles to deeply nested dead regions can still resolve.
 *
 * For each inherited ghost table:
 * 1. Add it to the parent's inherited_ghost_tables list
 * 2. Update the ghost index to point to the new location (parent, new index)
 * 3. Clear (but don't free) the dying region's list since ownership transferred
 *
 * @param dying_id  The region being destroyed
 * @param parent_id The parent region that will inherit the ghost tables
 */
fn void RegionRegistry.promote_inherited_ghost_tables(
    RegionRegistry* self, RegionId dying_id, RegionId parent_id
) {
    usz dying_idx = (usz)(uint)dying_id;
    usz parent_idx = (usz)(uint)parent_id;

    Region* dying_region = &self.region_storage[dying_idx];
    Region* parent_region = &self.region_storage[parent_idx];

    // Transfer each inherited ghost table to the parent
    for (usz i = 0; i < dying_region.inherited_ghost_tables.len(); i++) {
        GhostTable* ghost = &dying_region.inherited_ghost_tables[i];

        // Get the new index in parent's list (before we push)
        usz new_ghost_idx = parent_region.inherited_ghost_tables.len();

        // Update the ghost index: same source key, but new host and index
        // The key is (source_region_id, source_generation) which stays the same
        self.ghost_index.update(
            ghost.source_region_id,
            ghost.source_generation,
            parent_id,
            new_ghost_idx
        );

        // Transfer ownership of the ghost table to parent
        parent_region.inherited_ghost_tables.push(*ghost);
    }

    // Clear the dying region's list (ownership transferred, don't free the GhostTables)
    dying_region.inherited_ghost_tables.clear();
}

// -----------------------------------------------------------------------------
// 9.7 Validation
// -----------------------------------------------------------------------------

/**
 * Check if a RegionHandle is still valid.
 *
 * A handle is valid if:
 * - The region ID exists
 * - The region is alive
 * - The generation matches
 *
 * @param handle The handle to validate
 * @return true if valid, false otherwise
 */
fn bool RegionRegistry.is_valid_region(RegionRegistry* self, RegionHandle handle) {
    usz idx = (usz)(uint)handle.region_id;
    if (idx >= self.region_storage.len()) return false;
    if (!self.region_alive_flags[idx]) return false;
    return self.region_generations[idx] == handle.generation;
}

/**
 * Check if an ObjectHandle is still valid.
 *
 * A handle is valid if either:
 * - It points to a live region with matching generation
 * - If forwarded, the forward target must also be valid
 * - It points to a dead region that has a ghost table with FORWARDED entry
 *
 * @param handle The handle to validate
 * @return true if valid, false otherwise
 */
fn bool RegionRegistry.is_valid_object(RegionRegistry* self, ObjectHandle handle) {
    usz idx = (usz)(uint)handle.region_id;

    // --- Try live region first ---
    if (idx < self.region_storage.len() && self.region_alive_flags[idx]) {
        if ((uint)self.region_generations[idx] != (uint)handle.generation) {
            return false;
        }

        Region* region = &self.region_storage[idx];
        usz slot_idx = (usz)(uint)handle.slot_id;
        if (slot_idx >= region.slot_table.object_records.len()) {
            return false;
        }

        ObjectRecord* record = region.slot_table.get_record(handle.slot_id);
        if ((uint)record.generation != (uint)handle.generation) {
            return false;
        }

        // Valid if not forwarded, or if forward target is valid
        if (record.forwarded) {
            return self.is_valid_object(record.forward_target);
        }
        return true;
    }

    // --- Try ghost tables ---
    return self.is_valid_ghost_handle(handle);
}

// -----------------------------------------------------------------------------
// 9.8 Global Dereference
// -----------------------------------------------------------------------------

/**
 * Get a pointer to an object's data.
 *
 * Handles both live and forwarded objects transparently.
 * Follows forwarding chains until a non-forwarded object is found.
 *
 * @param handle The object to dereference
 * @return Pointer to the object's data
 *
 * Panics if handle is invalid (generation mismatch).
 */
fn void* RegionRegistry.dereference(RegionRegistry* self, ObjectHandle handle) {
    usz idx = (usz)(uint)handle.region_id;

    // --- Live region path ---
    if (idx < self.region_storage.len() && self.region_alive_flags[idx]) {
        Region* region = &self.region_storage[idx];
        ObjectRecord* record = region.slot_table.get_record(handle.slot_id);
        assert((uint)record.generation == (uint)handle.generation, "stale handle");

        if (record.forwarded) {
            // Object was promoted — follow the chain
            return self.dereference(record.forward_target);
        } else {
            // Object is here — return data pointer
            PoolSlot* pool_slot = region.pool.get_slot(record.live_info.pool_id);
            return pool_slot.is_inline ?
                (void*)&pool_slot.inline_data[0] : pool_slot.heap_ptr;
        }
    }

    // --- Ghost table path (region is dead, but object may be forwarded) ---
    return self.dereference_via_ghost(handle);
}

/**
 * Dereference via ghost table (for handles to dead regions).
 *
 * Searches ghost tables for the matching region ID and follows the
 * forwarding pointer.
 *
 * @param handle Handle to an object in a dead region
 * @return Pointer to the object's data
 */
fn void* RegionRegistry.dereference_via_ghost(RegionRegistry* self, ObjectHandle handle) {
    // O(1) lookup in ghost index
    GhostLookupResult result = self.ghost_index.lookup(handle.region_id, handle.generation);

    if (!result.found) {
        unreachable("no ghost table found for handle");
    }

    // Get the ghost table directly by index
    usz host_idx = (usz)(uint)result.host_id;
    Region* host_region = &self.region_storage[host_idx];
    GhostTable* ghost = &host_region.inherited_ghost_tables[result.ghost_idx];

    ObjectRecord* record = &ghost.forwarding_records[(usz)(uint)handle.slot_id];
    assert((uint)record.generation == (uint)handle.generation, "stale ghost handle");

    // Ghost tables should only contain forwarded records
    if (record.forwarded) {
        // Follow the forwarding pointer
        return self.dereference(record.forward_target);
    } else {
        unreachable("ghost table has non-forwarded record");
    }
}

/**
 * Check if a handle to a dead region is valid via ghost table.
 */
fn bool RegionRegistry.is_valid_ghost_handle(RegionRegistry* self, ObjectHandle handle) {
    // O(1) lookup in ghost index
    GhostLookupResult result = self.ghost_index.lookup(handle.region_id, handle.generation);

    if (!result.found) return false;

    usz host_idx = (usz)(uint)result.host_id;
    Region* host_region = &self.region_storage[host_idx];
    GhostTable* ghost = &host_region.inherited_ghost_tables[result.ghost_idx];

    usz slot_idx = (usz)(uint)handle.slot_id;
    if (slot_idx >= ghost.forwarding_records.len()) return false;
    ObjectRecord* record = &ghost.forwarding_records[slot_idx];
    return (uint)record.generation == (uint)handle.generation && record.forwarded;
}

// -----------------------------------------------------------------------------
// 9.9 Typed Dereference
// -----------------------------------------------------------------------------

/**
 * Dereference and cast to a specific type.
 *
 * Usage:
 *   int* ptr = registry.dereference_as(&registry, int, handle);
 *
 * @param $Type The expected type (compile-time)
 * @param handle The object handle
 * @return Typed pointer to the object
 */
macro RegionRegistry.dereference_as(&self, $Type, handle) {
    void* ptr = self.dereference(handle);
    return ($Type*)ptr;
}

// =============================================================================
// SECTION 10: FORWARDING CHAIN COMPRESSION
// =============================================================================
//
// When objects are promoted multiple times (child → parent → grandparent),
// handles can end up with long forwarding chains. This degrades dereference
// performance from O(1) to O(chain length).
//
// Chain compression optimizes this by short-circuiting: when we traverse a
// chain to find the final LIVE object, we update all intermediate FORWARDED
// entries to point directly to the final location.
//

/**
 * Result of a compressed dereference operation.
 * Returns both the data pointer and the final handle (for caching).
 */
struct DereferenceResult {
    void*        data;          // Pointer to object data
    ObjectHandle final_handle;  // The final LIVE handle (for future use)
}

/**
 * Dereference with chain compression.
 *
 * Same as dereference(), but also compresses the forwarding chain.
 * All intermediate forwarded entries are updated to point directly
 * to the final non-forwarded object.
 *
 * Use this for frequently-accessed objects with potentially long chains.
 *
 * @param handle The object to dereference
 * @return DereferenceResult with data pointer and final handle
 */
fn DereferenceResult RegionRegistry.dereference_compressed(
    RegionRegistry* self, ObjectHandle handle
) {
    List{ObjectHandle} chain;  // Track intermediates for compression
    ObjectHandle current = handle;

    for (;;) {
        ObjectRecord* record = self.resolve_object_record(current);

        if (!record.forwarded) {
            // Found the final object
            Region* region = &self.region_storage[(usz)(uint)current.region_id];
            PoolSlot* pool_slot = region.pool.get_slot(record.live_info.pool_id);
            void* data = pool_slot.is_inline ? (void*)&pool_slot.inline_data[0] : pool_slot.heap_ptr;

            // Compress: update all intermediates to point directly here
            foreach (&intermediate : chain) {
                ObjectRecord* inter_record = self.resolve_object_record(*intermediate);
                inter_record.forwarded = true;
                inter_record.forward_target = current;
            }
            chain.free();

            return { .data = data, .final_handle = current };
        } else {
            // Add to chain and continue following
            chain.push(current);
            current = record.forward_target;
        }
    }
}

/**
 * Resolve an ObjectHandle to its ObjectRecord.
 *
 * Searches both live regions and ghost tables.
 *
 * @param handle The handle to resolve
 * @return Pointer to the ObjectRecord
 */
fn ObjectRecord* RegionRegistry.resolve_object_record(
    RegionRegistry* self, ObjectHandle handle
) {
    usz idx = (usz)(uint)handle.region_id;

    // Try live region first
    if (idx < self.region_storage.len() && self.region_alive_flags[idx]) {
        return self.region_storage[idx].slot_table.get_record(handle.slot_id);
    }

    // O(1) lookup in ghost tables
    GhostLookupResult result = self.ghost_index.lookup(handle.region_id, handle.generation);
    if (result.found) {
        usz host_idx = (usz)(uint)result.host_id;
        Region* host = &self.region_storage[host_idx];
        GhostTable* ghost = &host.inherited_ghost_tables[result.ghost_idx];
        return &ghost.forwarding_records[(usz)(uint)handle.slot_id];
    }

    unreachable("object record not found in any live or ghost table");
}

// =============================================================================
// SECTION 11: OPTIONAL MANUAL OVERRIDES
// =============================================================================
//
// These operations are available but NEVER required. The system handles
// object lifecycle automatically. Use these for performance optimization
// when you know an object will outlive its region.
//

/**
 * Manually promote an object to a different region.
 *
 * Use when you know an object will outlive its current region and want
 * to avoid the cost of automatic promotion during region destruction.
 *
 * @param handle The object to promote
 * @param target The destination region
 * @return New handle in the target region
 *
 * The original handle becomes a forwarding stub pointing to the new location.
 */
fn ObjectHandle RegionRegistry.manual_promote(
    RegionRegistry* self, ObjectHandle handle, RegionHandle target
) {
    assert(self.is_valid_object(handle));
    assert(self.is_valid_region(target));

    Region* source = &self.region_storage[(usz)(uint)handle.region_id];
    ObjectRecord* record = source.slot_table.get_record(handle.slot_id);

    if (record.forwarded) {
        // Object already promoted elsewhere — follow and re-promote
        return self.manual_promote(record.forward_target, target);
    }

    // Object is live here — promote it
    PoolSlot* pool_slot = source.pool.get_slot(record.live_info.pool_id);
    void* data = pool_slot.is_inline ? (void*)&pool_slot.inline_data[0] : pool_slot.heap_ptr;

    // Allocate in target region
    Region* target_region = &self.region_storage[(usz)(uint)target.region_id];
    ObjectHandle new_handle = target_region.allocate_raw(
        data, pool_slot.size, pool_slot.type_id
    );

    // Remove from source and convert to forwarding stub
    source.pool.remove(record.live_info.pool_id);
    record.forwarded = true;
    record.forward_target = new_handle;
    source.live_object_count -= 1;

    return new_handle;
}

/**
 * Manually extend a region's lifetime by retaining it via an object handle.
 *
 * This is for advanced use cases where you need to keep a region alive
 * because you hold an object handle into it.
 *
 * @param object_handle An object in the region to extend
 * @return RegionHandle (retained) for the object's region
 */
fn RegionHandle RegionRegistry.manual_extend(
    RegionRegistry* self, ObjectHandle object_handle
) {
    RegionHandle region_handle = {
        .region_id  = object_handle.region_id,
        .generation = self.region_generations[(usz)(uint)object_handle.region_id]
    };
    return self.retain_region(region_handle);
}

// =============================================================================
// SECTION 12: MAIN (Test Entry Point)
// =============================================================================

// =============================================================================
// SECTION 13: THREAD-LOCAL REGISTRY
// =============================================================================
//
// Each thread gets its own independent RegionRegistry (and thus its own region
// hierarchy). No synchronization needed for intra-thread operations.
//
// Usage pattern:
// 1. Call thread_registry_init() once per thread at startup
// 2. Use convenience functions (create_region, release_region, etc.)
// 3. Call thread_registry_shutdown() before thread exit
//

/**
 * Thread-local registry pointer.
 * Each thread has its own independent RegionRegistry instance.
 */
tlocal RegionRegistry* g_thread_registry;

/**
 * Initialize the thread-local region registry for the current thread.
 * Must be called once per thread before using any region operations.
 * Creates the thread's root region.
 */
fn void thread_registry_init() {
    // Allocate a new registry for this thread
    RegionRegistry* reg = (RegionRegistry*)mem::malloc(RegionRegistry.sizeof);
    *reg = {};  // Zero initialize
    reg.init();
    g_thread_registry = reg;

    // Initialize prompt stack for continuations
    if (!prompt_stack_is_initialized()) {
        prompt_stack_init();
    }
}

/**
 * Shutdown the thread-local registry and free all regions.
 * Call before thread exit to clean up resources.
 */
fn void thread_registry_shutdown() {
    if (g_thread_registry == null) return;

    // Shutdown prompt stack
    if (prompt_stack_is_initialized()) {
        prompt_stack_shutdown();
    }

    // TODO: destroy all regions, free the registry
    // For now, just free the registry struct
    g_thread_registry.ghost_index.release();
    // Free region storage, etc.
    g_thread_registry.region_storage.free();
    g_thread_registry.region_alive_flags.free();
    g_thread_registry.region_generations.free();
    g_thread_registry.live_region_tracker.release();
    g_thread_registry.recycled_region_ids.free();
    foreach (&children : g_thread_registry.child_region_lists) {
        children.free();
    }
    g_thread_registry.child_region_lists.free();
    mem::free(g_thread_registry);
    g_thread_registry = null;
}

/**
 * Get the current thread's registry.
 * Panics if thread_registry_init() was not called.
 */
fn RegionRegistry* thread_registry() @inline {
    assert(g_thread_registry != null, "thread_registry_init() not called");
    return g_thread_registry;
}

// -----------------------------------------------------------------------------
// 13.1 Convenience Functions Using Thread-Local Registry
// -----------------------------------------------------------------------------

/**
 * Create a region in the current thread's registry.
 */
fn RegionHandle create_region(RegionHandle parent = INVALID_REGION_HANDLE) @inline {
    return thread_registry().create_region(parent);
}

/**
 * Retain a region in the current thread's registry.
 */
fn RegionHandle retain_region(RegionHandle handle) @inline {
    return thread_registry().retain_region(handle);
}

/**
 * Release a region in the current thread's registry.
 */
fn void release_region(RegionHandle handle) @inline {
    thread_registry().release_region(handle);
}

/**
 * Check if an object handle is valid in the current thread's registry.
 */
fn bool is_valid_object(ObjectHandle handle) @inline {
    return thread_registry().is_valid_object(handle);
}

/**
 * Check if a region handle is valid in the current thread's registry.
 */
fn bool is_valid_region(RegionHandle handle) @inline {
    return thread_registry().is_valid_region(handle);
}

/**
 * Dereference an object in the current thread's registry.
 */
fn void* dereference(ObjectHandle handle) @inline {
    return thread_registry().dereference(handle);
}

/**
 * Get the root region of the current thread's registry.
 */
fn RegionHandle thread_root_region() @inline {
    RegionRegistry* reg = thread_registry();
    return {
        .region_id = reg.root_id,
        .generation = reg.region_generations[(usz)(uint)reg.root_id]
    };
}

// -----------------------------------------------------------------------------
// 13.2 Typed Convenience Macros
// -----------------------------------------------------------------------------

/**
 * Dereference and cast to a specific type using the thread-local registry.
 *
 * Usage:
 *   int* ptr = dereference_as(int, handle);
 *
 * @param $Type The expected type (compile-time)
 * @param handle The object handle
 * @return Typed pointer to the object
 */
macro dereference_as($Type, handle) {
    return thread_registry().dereference_as($Type, handle);
}

/**
 * Allocate a typed object in a specific region.
 * Uses the current thread's registry.
 *
 * Usage:
 *   ObjectHandle h = allocate_in(region_handle, int, 42);
 *
 * @param region_handle The region to allocate in
 * @param $Type The type of object (compile-time)
 * @param value The value to store
 * @return ObjectHandle for the allocated object
 */
macro ObjectHandle allocate_in(region_handle, $Type, value) {
    RegionRegistry* reg = thread_registry();
    usz idx = (usz)(uint)region_handle.region_id;
    Region* region = &reg.region_storage[idx];
    return region.allocate_typed($Type, value);
}

// -----------------------------------------------------------------------------
// 13.3 Write Barrier for Escape Tracking
// -----------------------------------------------------------------------------

/**
 * Write barrier for storing object handles.
 *
 * Call this whenever storing an ObjectHandle into a data structure
 * that lives in a different region than the handle's home region.
 *
 * If the handle crosses a region boundary, marks the object as escaped.
 *
 * @param storing_region The region where the handle is being stored
 * @param handle The handle being stored
 */
fn void write_barrier(RegionId storing_region, ObjectHandle handle) {
    if ((uint)handle.region_id == (uint)storing_region) {
        return;  // Same region, no escape
    }

    // Cross-region store: mark as escaped
    usz idx = (usz)(uint)handle.region_id;
    if (idx < thread_registry().region_storage.len() &&
        thread_registry().region_alive_flags[idx]) {
        Region* home = &thread_registry().region_storage[idx];
        ObjectRecord* rec = home.slot_table.get_record(handle.slot_id);
        if ((uint)rec.generation == (uint)handle.generation) {
            rec.escaped = true;
        }
    }
}

/**
 * Macro for storing handles with automatic barrier.
 *
 * Usage:
 *   store_handle(storing_region, &my_struct.handle_field, some_handle);
 *
 * @param storing_region The region where the container lives
 * @param $location Pointer to where the handle will be stored
 * @param handle The handle being stored
 */
macro store_handle(storing_region, $location, handle) {
    write_barrier(storing_region.region_id, handle);
    *$location = handle;
}

// =============================================================================
// SECTION 14: MAIN (Test Entry Point)
// =============================================================================

/**
 * Check if a C-string matches a target string.
 */
fn bool str_eq(char* s, char[] target) {
    for (usz i = 0; i < target.len; i++) {
        if (s[i] != target[i]) return false;
    }
    return s[target.len] == 0;
}

/**
 * Main entry point.
 *
 * Usage:
 *   ./main         - Run all tests
 *   ./main -repl   - Start interactive REPL
 *   ./main --repl  - Start interactive REPL
 */
fn int main(int argc, char** argv) {
    // Check for REPL flag
    bool run_repl = false;
    for (int i = 1; i < argc; i++) {
        if (str_eq(argv[i], "-repl") || str_eq(argv[i], "--repl")) {
            run_repl = true;
            break;
        }
    }

    if (run_repl) {
        // Start REPL mode
        io::printn("Starting Lisp REPL...");

        // Initialize thread-local registry first
        thread_registry_init();

        // Initialize interpreter
        lisp::Interp* interp = (lisp::Interp*)mem::malloc(lisp::Interp.sizeof);
        interp.init();
        lisp::register_primitives(interp);

        // Run REPL
        lisp::repl(interp);

        // Clean up
        mem::free(interp);
        thread_registry_shutdown();
        return 0;
    }

    // Run tests mode
    io::printfn("Region Memory System v3 initialized");

    // Run destructor registry tests (O(1) lookup implementation)
    run_destructor_registry_tests();

    // Run ghost lookup tests (O(1) optimization verification)
    run_ghost_lookup_tests();

    // Run context capture/restore tests (x86_64 assembly primitives)
    // Note: These tests exercise low-level CPU state manipulation
    run_context_tests();

    // Run delimited continuation tests (reset/shift/resume system)
    // Note: These tests verify the continuation machinery including:
    // - Prompt stack operations
    // - Continuation allocation and lifecycle
    // - Region integration
    // - Effect handler patterns
    run_delimited_tests();

    // Run Lisp interpreter tests
    lisp::run_lisp_tests();

    // Initialize thread-local registry (replaces manual RegionRegistry creation)
    thread_registry_init();
    io::printfn("Thread-local registry initialized, root id=%d", (uint)thread_registry().root_id);

    // Create a child region using convenience function
    RegionHandle child = create_region();
    io::printfn("Child region created: id=%d, gen=%d",
        (uint)child.region_id, (uint)child.generation);

    // Clean up
    release_region(child);  // Release our reference
    thread_registry_shutdown();

    io::printfn("System ready.");
    return 0;
}
