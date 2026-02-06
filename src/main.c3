// Region Memory System — C3 Implementation v3
module main;

import std::collections::list;
import std::io;

// =============================================================================
// 1. Primitives
// =============================================================================

typedef RegionId   = uint;
typedef SlotId     = uint;     // slot table ID — developer-facing, carried in ObjectHandle
typedef PoolId     = uint;     // pool internal stable ID — never exposed outside pool
typedef PoolIndex  = uint;
typedef Generation = uint;
typedef RefCount   = uint;

// =============================================================================
// 2. Handles
// =============================================================================

struct ObjectHandle {
    RegionId   region_id;
    SlotId     slot_id;
    Generation generation;
}

struct RegionHandle {
    RegionId   region_id;
    Generation generation;
}

const ObjectHandle INVALID_OBJECT_HANDLE = { 0, 0, 0 };
const RegionHandle INVALID_REGION_HANDLE = { 0, 0 };

// =============================================================================
// 3. Sparse Set
// =============================================================================
// O(1) insert/remove/contains with cache-friendly iteration
// - key_position_index: sparse array mapping key -> position in packed_keys
// - packed_keys: dense array of live keys (swap-remove on delete)

struct SparseSet {
    List{uint} key_position_index;
    List{uint} packed_keys;
    uint live_count;
}

fn bool SparseSet.contains(SparseSet* self, uint key) {
    if (key >= (uint)self.key_position_index.len()) return false;
    uint pos = self.key_position_index[(usz)key];
    return pos < self.live_count && self.packed_keys[(usz)pos] == key;
}

fn void SparseSet.insert(SparseSet* self, uint key) {
    if (self.contains(key)) return;
    while (self.key_position_index.len() <= (usz)key) {
        self.key_position_index.push(0);
    }
    if (self.packed_keys.len() <= (usz)self.live_count) {
        self.packed_keys.push(key);
    } else {
        self.packed_keys[(usz)self.live_count] = key;
    }
    self.key_position_index[(usz)key] = self.live_count;
    self.live_count += 1;
}

fn void SparseSet.remove(SparseSet* self, uint key) {
    if (!self.contains(key)) return;
    uint pos = self.key_position_index[(usz)key];
    uint last_key = self.packed_keys[(usz)(self.live_count - 1)];
    self.packed_keys[(usz)pos] = last_key;
    self.key_position_index[(usz)last_key] = pos;
    self.live_count -= 1;
}

fn uint[] SparseSet.get_live_keys(SparseSet* self) {
    return self.packed_keys.array_view()[:self.live_count];
}

fn void SparseSet.release(SparseSet* self) {
    self.key_position_index.free();
    self.packed_keys.free();
}

// =============================================================================
// 4. Pool
// =============================================================================
// Contiguous packed object storage within a region. Swap-and-pop compaction on removal.
// PoolId is the pool's internal stable identifier. It never leaves the pool — the slot
// table's ObjectRecord holds one to locate the object's physical data.

// 4.1 Pool Slot
const usz INLINE_THRESHOLD = 16;  // inline objects ≤ 16 bytes

struct PoolSlot {
    usz    size;
    typeid type_id;
    PoolId owner_id;    // which PoolId owns this slot (for reverse lookup)
    bool   is_inline;   // true if data is in inline_data, false if heap_ptr
    void*  heap_ptr;    // large objects (>16 bytes) - only valid if !is_inline
    char[16] inline_data;  // small objects (≤16 bytes) - only valid if is_inline
}

// 4.2 Arena Block
const usz ARENA_SIZE = 64 * 1024;  // 64KB per arena

struct ArenaBlock {
    char* data;
    usz   capacity;
    usz   used;
}

struct FreeListEntry {
    usz offset;  // offset in arena
    usz size;    // size of free chunk
}

// 4.3 Pool Structure
struct Pool {
    List{PoolSlot} packed_slots;
    uint slot_count;

    List{PoolIndex} pool_id_position_index;  // PoolId -> position in packed_slots
    List{PoolId}    position_owner_index;    // position -> which PoolId is here

    List{PoolId} recycled_pool_ids;
    PoolId       next_pool_id;

    // Arena allocation for large objects
    List{ArenaBlock} arenas;
    List{FreeListEntry} free_list;  // tracks freed chunks in arenas
}

// 4.4 Arena Allocation Helpers

fn ArenaBlock Pool.new_arena(Pool* self) {
    ArenaBlock arena = {
        .data     = (char*)mem::malloc(ARENA_SIZE),
        .capacity = ARENA_SIZE,
        .used     = 0
    };
    return arena;
}

fn void* Pool.arena_alloc(Pool* self, usz size) {
    // Try to reuse from free-list first
    for (usz i = 0; i < self.free_list.len(); i++) {
        if (self.free_list[i].size >= size) {
            void* ptr = self.arenas[0].data + self.free_list[i].offset;  // simplified: use first arena
            
            // If exact fit, remove from free-list
            if (self.free_list[i].size == size) {
                self.free_list[i] = self.free_list[self.free_list.len() - 1];
                (void)self.free_list.pop();
            } else {
                // Partial fit: adjust free entry
                self.free_list[i].offset += size;
                self.free_list[i].size -= size;
            }
            return ptr;
        }
    }

    // No free chunk available, allocate from current arena
    if (self.arenas.len() == 0) {
        self.arenas.push(self.new_arena());
    }

    ArenaBlock* current = &self.arenas[self.arenas.len() - 1];
    
    // Need new arena?
    if (current.used + size > current.capacity) {
        self.arenas.push(self.new_arena());
        current = &self.arenas[self.arenas.len() - 1];
    }

    void* ptr = current.data + current.used;
    current.used += size;
    return ptr;
}

fn void Pool.arena_free(Pool* self, void* ptr, usz size) {
    // Find which arena this pointer belongs to
    for (usz i = 0; i < self.arenas.len(); i++) {
        char* arena_start = self.arenas[i].data;
        char* arena_end = arena_start + self.arenas[i].capacity;
        
        if ((char*)ptr >= arena_start && (char*)ptr < arena_end) {
            usz offset = (usz)((char*)ptr - arena_start);
            FreeListEntry entry = { .offset = offset, .size = size };
            self.free_list.push(entry);
            return;
        }
    }
}

// 4.5 Pool Operations
fn PoolId Pool.allocate(Pool* self, void* source_data, usz size, typeid type_id) {
    // get or recycle a stable pool ID
    PoolId pid;
    if (self.recycled_pool_ids.len() > 0) {
        pid = self.recycled_pool_ids.pop()!!;
    } else {
        pid = self.next_pool_id;
        self.next_pool_id = (PoolId)((uint)self.next_pool_id + 1);
        while (self.pool_id_position_index.len() <= (usz)(uint)pid) {
            self.pool_id_position_index.push((PoolIndex)0);
        }
    }

    // create slot based on size
    PoolSlot slot;
    slot.size = size;
    slot.type_id = type_id;
    slot.owner_id = pid;

    if (size <= INLINE_THRESHOLD) {
        // INLINE: store directly in slot
        slot.is_inline = true;
        mem::copy(&slot.inline_data[0], source_data, size);
    } else {
        // ARENA: allocate from arena
        slot.is_inline = false;
        void* arena_ptr = self.arena_alloc(size);
        mem::copy(arena_ptr, source_data, size);
        slot.heap_ptr = arena_ptr;
    }

    // append to packed array
    PoolIndex packed_position = (PoolIndex)self.slot_count;

    if (self.packed_slots.len() <= (usz)self.slot_count) {
        self.packed_slots.push(slot);
        self.position_owner_index.push(pid);
    } else {
        self.packed_slots[(usz)self.slot_count] = slot;
        self.position_owner_index[(usz)self.slot_count] = pid;
    }

    self.pool_id_position_index[(usz)(uint)pid] = packed_position;
    self.slot_count += 1;

    return pid;
}

fn void Pool.remove(Pool* self, PoolId pid) {
    PoolIndex position = self.pool_id_position_index[(usz)(uint)pid];
    uint last_position = self.slot_count - 1;

    PoolSlot* slot = &self.packed_slots[(usz)(uint)position];

    // free the object's storage
    if (!slot.is_inline) {
        // Arena-allocated: add to free-list for reuse
        self.arena_free(slot.heap_ptr, slot.size);
    }
    // Inline objects: no cleanup needed

    // swap-and-pop: move last entry into the gap
    if ((uint)position != last_position) {
        PoolId displaced_owner = self.position_owner_index[(usz)last_position];

        self.packed_slots[(usz)(uint)position] =
            self.packed_slots[(usz)last_position];
        self.position_owner_index[(usz)(uint)position] = displaced_owner;
        self.pool_id_position_index[(usz)(uint)displaced_owner] = position;
    }

    self.slot_count -= 1;
    self.recycled_pool_ids.push(pid);
}

fn PoolSlot* Pool.get_slot(Pool* self, PoolId pid) {
    PoolIndex position = self.pool_id_position_index[(usz)(uint)pid];
    return &self.packed_slots[(usz)(uint)position];
}

fn void* Pool.get_data(Pool* self, PoolId pid) {
    PoolSlot* slot = self.get_slot(pid);
    if (slot.is_inline) {
        return (void*)&slot.inline_data[0];
    } else {
        return slot.heap_ptr;
    }
}

fn void Pool.destroy_all(Pool* self) {
    // Run destructors for all live objects
    for (uint i = 0; i < self.slot_count; i++) {
        PoolSlot* slot = &self.packed_slots[(usz)i];
        void* data = slot.is_inline ? (void*)&slot.inline_data[0] : slot.heap_ptr;
        destructor_run(slot.type_id, data);
    }

    // Free all arena blocks
    foreach (&arena : self.arenas) {
        mem::free(arena.data);
    }

    self.packed_slots.free();
    self.pool_id_position_index.free();
    self.position_owner_index.free();
    self.recycled_pool_ids.free();
    self.arenas.free();
    self.free_list.free();
}

// =============================================================================
// 5. Slot Table
// =============================================================================
// Indirection layer between handles and pool. Each record tracks one object's routing state.

enum SlotKind : char {
    LIVE,
    FORWARDED,
    DEAD
}

// 5.1 Object Record
// An ObjectRecord is the indirection/routing state of one object. It tells you:
// is this object live here (and where in the pool), forwarded elsewhere (and where),
// or dead?

struct ObjectLiveInfo {
    PoolId pool_id;     // stable ID in this region's pool
    usz    size;
    typeid type_id;
}

struct ObjectRecord {
    SlotKind   kind;
    Generation generation;
    union {
        ObjectLiveInfo live_info;
        ObjectHandle   forward_target;
    }
}

// 5.2 Slot Table Structure
struct SlotTable {
    List{ObjectRecord} object_records;
    List{SlotId}       recycled_slot_ids;
    SparseSet          live_slot_tracker;
}

// 5.3 Slot Table Operations
struct SlotAllocationResult {
    SlotId     slot_id;
    Generation generation;
}

fn SlotAllocationResult SlotTable.allocate(SlotTable* self) {
    SlotId sid;
    if (self.recycled_slot_ids.len() > 0) {
        sid = self.recycled_slot_ids.pop()!!;
    } else {
        sid = (SlotId)self.object_records.len();
        ObjectRecord new_record;
        new_record.kind = DEAD;
        new_record.generation = (Generation)0;
        self.object_records.push(new_record);
    }

    usz idx = (usz)(uint)sid;
    self.object_records[idx].generation =
        (Generation)((uint)self.object_records[idx].generation + 1);
    Generation gen = self.object_records[idx].generation;

    self.live_slot_tracker.insert((uint)sid);

    SlotAllocationResult result;
    result.slot_id = sid;
    result.generation = gen;
    return result;
}

fn void SlotTable.release(SlotTable* self, SlotId sid) {
    usz idx = (usz)(uint)sid;
    self.object_records[idx].kind = DEAD;
    self.object_records[idx].generation =
        (Generation)((uint)self.object_records[idx].generation + 1);
    self.live_slot_tracker.remove((uint)sid);
    self.recycled_slot_ids.push(sid);
}

fn ObjectRecord* SlotTable.get_record(SlotTable* self, SlotId sid) {
    return &self.object_records[(usz)(uint)sid];
}

fn void SlotTable.destroy_all(SlotTable* self) {
    self.object_records.free();
    self.recycled_slot_ids.free();
    self.live_slot_tracker.release();
}

// =============================================================================
// 6. Ghost Table
// =============================================================================
// Remnant of a dead region's slot table. Contains only forwarding records so that
// existing handles to a dead region can still resolve.

struct GhostTable {
    RegionId           source_region_id;
    Generation         source_generation;
    List{ObjectRecord} forwarding_records;
}

// =============================================================================
// 7. Region
// =============================================================================

const RegionId NO_PARENT = (RegionId)uint.max;

struct Region {
    // --- identity ---
    RegionId   id;
    Generation generation;

    // --- lifetime ---
    RefCount refcount;
    RegionId parent;

    // --- storage ---
    Pool      pool;
    SlotTable slot_table;
    List{GhostTable} inherited_ghost_tables;

    // --- metadata ---
    uint live_object_count;
}

// 7.2 Allocate Object (typed, compile-time generic)
macro ObjectHandle Region.allocate_typed(&self, $Type, value) {
    void* source_data = (void*)&value;
    usz size = $Type.sizeof;
    typeid tid = $Type.typeid;

    PoolId pid = self.pool.allocate(source_data, size, tid);

    SlotAllocationResult slot_result = self.slot_table.allocate();
    SlotId slot_id = slot_result.slot_id;
    Generation gen = slot_result.generation;

    ObjectRecord* record = self.slot_table.get_record(slot_id);
    record.kind = LIVE;
    record.live_info.pool_id = pid;
    record.live_info.size = size;
    record.live_info.type_id = tid;

    self.live_object_count += 1;

    ObjectHandle handle;
    handle.region_id = self.id;
    handle.slot_id = slot_id;
    handle.generation = gen;
    return handle;
}

// 7.3 Allocate Object (raw bytes)
fn ObjectHandle Region.allocate_raw(Region* self, void* data, usz size, typeid tid) {
    PoolId pid = self.pool.allocate(data, size, tid);

    SlotAllocationResult slot_result = self.slot_table.allocate();
    SlotId slot_id = slot_result.slot_id;
    Generation gen = slot_result.generation;

    ObjectRecord* record = self.slot_table.get_record(slot_id);
    record.kind = LIVE;
    record.live_info.pool_id = pid;
    record.live_info.size = size;
    record.live_info.type_id = tid;

    self.live_object_count += 1;

    ObjectHandle handle;
    handle.region_id = self.id;
    handle.slot_id = slot_id;
    handle.generation = gen;
    return handle;
}

// 7.4 Free Object (explicit, optional)
fn void Region.free_object(Region* self, ObjectHandle handle) {
    ObjectRecord* record = self.slot_table.get_record(handle.slot_id);
    assert(record.generation == handle.generation);

    switch (record.kind) {
        case LIVE:
            PoolSlot* pool_slot = self.pool.get_slot(record.live_info.pool_id);
            void* data = pool_slot.is_inline ? (void*)&pool_slot.inline_data[0] : pool_slot.heap_ptr;
            destructor_run(record.live_info.type_id, data);
            self.pool.remove(record.live_info.pool_id);
            self.slot_table.release(handle.slot_id);
            self.live_object_count -= 1;

        case FORWARDED:
            self.slot_table.release(handle.slot_id);

        case DEAD:
            // no-op
    }
}

// 7.5 Destroy Region Internals
fn void Region.destroy_internals(Region* self) {
    self.pool.destroy_all();
    self.slot_table.destroy_all();
    foreach (&ghost : self.inherited_ghost_tables) {
        ghost.forwarding_records.free();
    }
    self.inherited_ghost_tables.free();
}

// =============================================================================
// 8. Destructor Registry
// =============================================================================

alias DestructorFn = fn void(void* ptr);

struct DestructorRegistry {
    List{typeid}       registered_type_ids;
    List{DestructorFn} destructor_functions;
}

fn void DestructorRegistry.register(DestructorRegistry* self, typeid tid, DestructorFn func) {
    self.registered_type_ids.push(tid);
    self.destructor_functions.push(func);
}

fn DestructorFn? DestructorRegistry.find(DestructorRegistry* self, typeid tid) {
    for (usz i = 0; i < self.registered_type_ids.len(); i++) {
        if (self.registered_type_ids[i] == tid) {
            return self.destructor_functions[i];
        }
    }
    return null;
}

DestructorRegistry g_destructor_registry;

fn void destructor_run(typeid tid, void* ptr) {
    if (try func = g_destructor_registry.find(tid)) {
        func(ptr);
    }
}

// =============================================================================
// 9. Region Registry
// =============================================================================

struct RegionRegistry {
    // --- region storage ---
    List{Region} region_storage;
    List{bool}   region_alive_flags;
    List{Generation} region_generations;

    // --- liveness tracking ---
    SparseSet live_region_tracker;

    // --- ID recycling ---
    List{RegionId} recycled_region_ids;
    RegionId       next_region_id;

    // --- nesting ---
    List{List{RegionId}} child_region_lists;

    // --- ghost table index ---
    List{RegionId} ghost_source_ids;    // dead region ID
    List{RegionId} ghost_host_ids;      // region holding its ghost table

    // --- root ---
    RegionId root_id;
}

// 9.2 Initialization
fn void RegionRegistry.init(RegionRegistry* self) {
    RegionId root = (RegionId)0;
    Generation gen = (Generation)1;

    Region root_region;
    root_region.id = root;
    root_region.generation = gen;
    root_region.refcount = (RefCount)1;
    root_region.parent = NO_PARENT;
    root_region.live_object_count = 0;

    self.region_storage.push(root_region);
    self.region_alive_flags.push(true);
    self.region_generations.push(gen);
    self.live_region_tracker.insert(0);
    List{RegionId} empty_list;
    self.child_region_lists.push(empty_list);
    self.root_id = root;
    self.next_region_id = (RegionId)1;
}

// 9.3 Create Region
// Use INVALID_REGION_HANDLE to create under root, or pass a valid parent handle
fn RegionHandle RegionRegistry.create_region(RegionRegistry* self, RegionHandle parent_handle) {
    // resolve parent — default to root if invalid handle passed
    RegionId parent_id;
    if (self.is_valid_region(parent_handle)) {
        parent_id = parent_handle.region_id;
    } else {
        parent_id = self.root_id;
    }

    // allocate or recycle an ID
    RegionId id;
    if (self.recycled_region_ids.len() > 0) {
        id = self.recycled_region_ids.pop()!!;
    } else {
        id = self.next_region_id;
        self.next_region_id = (RegionId)((uint)self.next_region_id + 1);
    }

    // grow arrays if needed
    usz idx = (usz)(uint)id;
    while (self.region_generations.len() <= idx) {
        self.region_generations.push((Generation)0);
        self.region_storage.push({});
        self.region_alive_flags.push(false);
        self.child_region_lists.push({});
    }

    // bump generation
    self.region_generations[idx] =
        (Generation)((uint)self.region_generations[idx] + 1);
    Generation gen = self.region_generations[idx];

    // create region
    Region region;
    region.id = id;
    region.generation = gen;
    region.refcount = (RefCount)2;  // +1 creator, +1 parent
    region.parent = parent_id;
    region.live_object_count = 0;

    self.region_storage[idx] = region;
    self.region_alive_flags[idx] = true;
    self.live_region_tracker.insert((uint)id);

    // register as child of parent
    usz parent_idx = (usz)(uint)parent_id;
    self.child_region_lists[parent_idx].push(id);

    RegionHandle result;
    result.region_id = id;
    result.generation = gen;
    return result;
}

// 9.4 Retain / Release
fn RegionHandle RegionRegistry.retain_region(RegionRegistry* self, RegionHandle handle) {
    assert(self.is_valid_region(handle));
    usz idx = (usz)(uint)handle.region_id;
    self.region_storage[idx].refcount =
        (RefCount)((uint)self.region_storage[idx].refcount + 1);
    return handle;
}

fn void RegionRegistry.release_region(RegionRegistry* self, RegionHandle handle) {
    assert(self.is_valid_region(handle));
    usz idx = (usz)(uint)handle.region_id;
    self.region_storage[idx].refcount =
        (RefCount)((uint)self.region_storage[idx].refcount - 1);

    if ((uint)self.region_storage[idx].refcount == 0) {
        self.destroy_region(handle.region_id);
    }
}

// 9.5 Destroy Region (automatic promotion)
fn void RegionRegistry.destroy_region(RegionRegistry* self, RegionId id) {
    usz idx = (usz)(uint)id;
    Region* region = &self.region_storage[idx];
    RegionId parent_id = region.parent;
    bool has_parent = (parent_id != NO_PARENT);

    // ============================================================
    //  PHASE 1: PROMOTE SURVIVORS TO PARENT
    // ============================================================
    if (region.live_object_count > 0 && has_parent) {
        usz parent_idx = (usz)(uint)parent_id;
        Region* parent_region = &self.region_storage[parent_idx];

        uint[] live_slots = region.slot_table.live_slot_tracker.get_live_keys();

        for (usz i = 0; i < live_slots.len; i++) {
            SlotId sid = (SlotId)live_slots[i];
            ObjectRecord* record = region.slot_table.get_record(sid);

            switch (record.kind) {
                case LIVE:
                    PoolSlot* pool_slot =
                        region.pool.get_slot(record.live_info.pool_id);

                    void* data = pool_slot.is_inline ? (void*)&pool_slot.inline_data[0] : pool_slot.heap_ptr;

                    // allocate copy in parent region
                    ObjectHandle new_handle = parent_region.allocate_raw(
                        data,
                        pool_slot.size,
                        pool_slot.type_id
                    );

                    // convert to forwarding stub
                    record.kind = FORWARDED;
                    record.forward_target = new_handle;
                case FORWARDED: nextcase;  // already forwarded elsewhere, leave it
                case DEAD:  // skip
            }
        }
    }

    // ============================================================
    //  PHASE 2: RELEASE CHILDREN (reparent survivors)
    // ============================================================
    usz child_list_idx = (usz)(uint)id;
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
                self.destroy_region(child_id);
            } else {
                // child survives — reparent to grandparent
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
    self.transfer_ghost_table(id, parent_id, has_parent);

    // ============================================================
    //  PHASE 4: REMOVE FROM PARENT'S CHILD LIST
    // ============================================================
    if (has_parent) {
        usz parent_idx_remove = (usz)(uint)parent_id;
        List{RegionId}* parent_children = &self.child_region_lists[parent_idx_remove];
        for (usz i = 0; i < parent_children.len(); i++) {
            if ((uint)(*parent_children)[i] == (uint)id) {
                // swap with last and pop (manual swap_remove)
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

// 9.6 Ghost Table Transfer
fn void RegionRegistry.transfer_ghost_table(
    RegionRegistry* self, RegionId dying_id, RegionId parent_id, bool has_parent
) {
    if (!has_parent) return;

    usz dying_idx = (usz)(uint)dying_id;
    Region* dying_region = &self.region_storage[dying_idx];

    // check if any forwarding stubs exist
    bool has_forwarding_records = false;
    uint[] live_slots = dying_region.slot_table.live_slot_tracker.get_live_keys();
    for (usz i = 0; i < live_slots.len; i++) {
        ObjectRecord* record =
            dying_region.slot_table.get_record((SlotId)live_slots[i]);
        if (record.kind == FORWARDED) {
            has_forwarding_records = true;
            break;
        }
    }

    if (!has_forwarding_records) return;

    // move object records to ghost table
    GhostTable ghost = {
        .source_region_id   = dying_id,
        .source_generation  = dying_region.generation,
        .forwarding_records = dying_region.slot_table.object_records  // move
    };

    usz parent_idx = (usz)(uint)parent_id;
    self.region_storage[parent_idx].inherited_ghost_tables.push(ghost);

    // register in ghost index
    self.ghost_source_ids.push(dying_id);
    self.ghost_host_ids.push(parent_id);
}

// 9.7 Validation
fn bool RegionRegistry.is_valid_region(RegionRegistry* self, RegionHandle handle) {
    usz idx = (usz)(uint)handle.region_id;
    if (idx >= self.region_storage.len()) return false;
    if (!self.region_alive_flags[idx]) return false;
    return self.region_generations[idx] == handle.generation;
}

fn bool RegionRegistry.is_valid_object(RegionRegistry* self, ObjectHandle handle) {
    usz idx = (usz)(uint)handle.region_id;

    // check live region first
    if (idx < self.region_storage.len() && self.region_alive_flags[idx]) {
        if (self.region_generations[idx] != handle.generation) return false;
        Region* region = &self.region_storage[idx];
        usz slot_idx = (usz)(uint)handle.slot_id;
        if (slot_idx >= region.slot_table.object_records.len()) return false;
        ObjectRecord* record = region.slot_table.get_record(handle.slot_id);
        return record.generation == handle.generation && record.kind != DEAD;
    }

    // check ghost tables
    return self.is_valid_ghost_handle(handle);
}

// 9.8 Global Dereference
fn void* RegionRegistry.dereference(RegionRegistry* self, ObjectHandle handle) {
    usz idx = (usz)(uint)handle.region_id;

    // live region path
    if (idx < self.region_storage.len() && self.region_alive_flags[idx]) {
        Region* region = &self.region_storage[idx];
        ObjectRecord* record = region.slot_table.get_record(handle.slot_id);
        assert(record.generation == handle.generation);

        switch (record.kind) {
            case LIVE:
                PoolSlot* pool_slot = region.pool.get_slot(record.live_info.pool_id);
                return pool_slot.is_inline ? (void*)&pool_slot.inline_data[0] : pool_slot.heap_ptr;

            case FORWARDED:
                return self.dereference(record.forward_target);

            case DEAD:
                unreachable("use-after-free: dereferencing dead object");
        }
    }

    // ghost table path
    return self.dereference_via_ghost(handle);
}

fn void* RegionRegistry.dereference_via_ghost(RegionRegistry* self, ObjectHandle handle) {
    for (usz i = 0; i < self.ghost_source_ids.len(); i++) {
        if ((uint)self.ghost_source_ids[i] == (uint)handle.region_id) {
            RegionId host_id = self.ghost_host_ids[i];
            usz host_idx = (usz)(uint)host_id;
            Region* host_region = &self.region_storage[host_idx];

            foreach (&ghost : host_region.inherited_ghost_tables) {
                if ((uint)ghost.source_region_id == (uint)handle.region_id
                    && ghost.source_generation == handle.generation) {

                    ObjectRecord* record =
                        &ghost.forwarding_records[(usz)(uint)handle.slot_id];
                    assert(record.generation == handle.generation);

                    switch (record.kind) {
                        case FORWARDED:
                            return self.dereference(record.forward_target);
                        case DEAD:
                            unreachable("use-after-free in ghost table");
                        default:
                            unreachable("ghost table has live record");
                    }
                }
            }
        }
    }
    unreachable("no ghost table found for handle");
}

fn bool RegionRegistry.is_valid_ghost_handle(RegionRegistry* self, ObjectHandle handle) {
    for (usz i = 0; i < self.ghost_source_ids.len(); i++) {
        if ((uint)self.ghost_source_ids[i] == (uint)handle.region_id) {
            RegionId host_id = self.ghost_host_ids[i];
            usz host_idx = (usz)(uint)host_id;
            Region* host_region = &self.region_storage[host_idx];

            foreach (&ghost : host_region.inherited_ghost_tables) {
                if ((uint)ghost.source_region_id == (uint)handle.region_id
                    && ghost.source_generation == handle.generation) {
                    usz slot_idx = (usz)(uint)handle.slot_id;
                    if (slot_idx >= ghost.forwarding_records.len()) return false;
                    ObjectRecord* record = &ghost.forwarding_records[slot_idx];
                    return record.generation == handle.generation
                        && record.kind == FORWARDED;
                }
            }
        }
    }
    return false;
}

// 9.9 Typed Dereference
macro RegionRegistry.dereference_as(&self, $Type, handle) {
    void* ptr = self.dereference(handle);
    return ($Type*)ptr;
}

// =============================================================================
// 10. Forwarding Chain Compression
// =============================================================================

struct DereferenceResult {
    void*        data;
    ObjectHandle final_handle;
}

fn DereferenceResult RegionRegistry.dereference_compressed(
    RegionRegistry* self, ObjectHandle handle
) {
    List{ObjectHandle} chain;
    ObjectHandle current = handle;

    for (;;) {
        ObjectRecord* record = self.resolve_object_record(current);

        switch (record.kind) {
            case LIVE:
                Region* region = &self.region_storage[(usz)(uint)current.region_id];
                PoolSlot* pool_slot = region.pool.get_slot(record.live_info.pool_id);
                void* data = pool_slot.is_inline ? (void*)&pool_slot.inline_data[0] : pool_slot.heap_ptr;

                // compress all intermediates to point directly here
                foreach (&intermediate : chain) {
                    ObjectRecord* inter_record = self.resolve_object_record(*intermediate);
                    inter_record.kind = FORWARDED;
                    inter_record.forward_target = current;
                }
                chain.free();

                return { .data = data, .final_handle = current };

            case FORWARDED:
                chain.push(current);
                current = record.forward_target;

            case DEAD:
                chain.free();
                unreachable("dangling forwarding chain");
        }
    }
}

fn ObjectRecord* RegionRegistry.resolve_object_record(
    RegionRegistry* self, ObjectHandle handle
) {
    usz idx = (usz)(uint)handle.region_id;

    // try live region
    if (idx < self.region_storage.len() && self.region_alive_flags[idx]) {
        return self.region_storage[idx].slot_table.get_record(handle.slot_id);
    }

    // try ghost tables
    for (usz i = 0; i < self.ghost_source_ids.len(); i++) {
        if ((uint)self.ghost_source_ids[i] == (uint)handle.region_id) {
            RegionId host_id = self.ghost_host_ids[i];
            usz host_idx = (usz)(uint)host_id;
            Region* host = &self.region_storage[host_idx];

            foreach (&ghost : host.inherited_ghost_tables) {
                if ((uint)ghost.source_region_id == (uint)handle.region_id
                    && ghost.source_generation == handle.generation) {
                    return &ghost.forwarding_records[(usz)(uint)handle.slot_id];
                }
            }
        }
    }

    unreachable("object record not found in any live or ghost table");
}

// =============================================================================
// 11. Optional Manual Overrides
// =============================================================================
// Available but never required. For performance optimization only.

fn ObjectHandle RegionRegistry.manual_promote(
    RegionRegistry* self, ObjectHandle handle, RegionHandle target
) {
    assert(self.is_valid_object(handle));
    assert(self.is_valid_region(target));

    Region* source = &self.region_storage[(usz)(uint)handle.region_id];
    ObjectRecord* record = source.slot_table.get_record(handle.slot_id);

    switch (record.kind) {
        case LIVE:
            PoolSlot* pool_slot = source.pool.get_slot(record.live_info.pool_id);
            void* data = pool_slot.is_inline ? (void*)&pool_slot.inline_data[0] : pool_slot.heap_ptr;
            Region* target_region = &self.region_storage[(usz)(uint)target.region_id];
            ObjectHandle new_handle = target_region.allocate_raw(
                data, pool_slot.size, pool_slot.type_id
            );

            source.pool.remove(record.live_info.pool_id);
            record.kind = FORWARDED;
            record.forward_target = new_handle;
            source.live_object_count -= 1;

            return new_handle;

        case FORWARDED:
            return self.manual_promote(record.forward_target, target);

        case DEAD:
            unreachable("promoting dead object");
    }
}

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
// 12. Main (test entry point)
// =============================================================================

fn void main() {
    io::printfn("Region Memory System v3 initialized");

    // Basic test: initialize registry
    RegionRegistry registry;
    registry.init();
    io::printfn("Root region created: id=%d", (uint)registry.root_id);

    // Create a child region (pass INVALID_REGION_HANDLE to use root as parent)
    RegionHandle child = registry.create_region(INVALID_REGION_HANDLE);
    io::printfn("Child region created: id=%d, gen=%d",
        (uint)child.region_id, (uint)child.generation);

    io::printfn("System ready.");
}
