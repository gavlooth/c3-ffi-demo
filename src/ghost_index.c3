/**
 * =============================================================================
 * GHOST INDEX — O(1) Ghost Table Lookup
 * =============================================================================
 *
 * Provides O(1) lookup of ghost tables by combining the source region ID and
 * generation into a single 64-bit key. Uses open-addressing hash map with
 * linear probing.
 *
 * Key composition: (region_id << 32) | generation
 * This ensures unique keys for each (region_id, generation) pair.
 */
module main;

import std::collections::list;

// =============================================================================
// GHOST INDEX STRUCTURES
// =============================================================================

/** Sentinel value for empty slots in the ghost index. */
const ulong GHOST_INDEX_EMPTY = ulong.max;

/** Sentinel value for tombstoned (deleted) slots in the ghost index. */
const ulong GHOST_INDEX_TOMBSTONE = ulong.max - (ulong)1;

/**
 * GhostIndexEntry — A single entry in the ghost index hash map.
 *
 * Fields:
 *   key       — Combined (region_id << 32) | generation, or GHOST_INDEX_EMPTY
 *   host_id   — Region ID that holds the ghost table
 *   ghost_idx — Index into host region's inherited_ghost_tables list
 */
struct GhostIndexEntry {
    ulong    key;       // (source_region_id << 32) | source_generation
    RegionId host_id;   // Which region holds the ghost table
    usz      ghost_idx; // Index into host's inherited_ghost_tables
}

/**
 * GhostIndex — O(1) hash map for ghost table lookup.
 *
 * Uses open-addressing with linear probing. Automatically resizes when
 * load factor exceeds 0.75.
 *
 * Fields:
 *   entries  — Fixed-size array of entries (power of 2)
 *   count    — Number of occupied entries
 *   capacity — Total slots (always power of 2)
 */
struct GhostIndex {
    List{GhostIndexEntry} entries;
    usz count;
    usz capacity;
}

/**
 * Lookup result for ghost index queries.
 */
struct GhostLookupResult {
    bool     found;
    RegionId host_id;
    usz      ghost_idx;
}

// =============================================================================
// GHOST INDEX FUNCTIONS
// =============================================================================

/**
 * Compose a lookup key from region ID and generation.
 */
fn ulong ghost_index_make_key(RegionId region_id, Generation gen) @inline {
    return ((ulong)(uint)region_id << 32) | (ulong)(uint)gen;
}

/**
 * Hash function for ghost index keys.
 * Uses splitmix64-style mixing for good distribution.
 */
fn usz ghost_index_hash(ulong key) @inline {
    ulong h = key;
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccd;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53;
    h ^= h >> 33;
    return (usz)h;
}

/**
 * Initialize the ghost index with a given capacity.
 *
 * @param initial_capacity Minimum number of slots (will be rounded up to power of 2)
 */
fn void GhostIndex.init(GhostIndex* self, usz initial_capacity) {
    // Round up to power of 2
    usz cap = 16; // minimum
    while (cap < initial_capacity) cap *= 2;

    self.capacity = cap;
    self.count = 0;

    // Initialize all entries as empty
    for (usz i = 0; i < cap; i++) {
        GhostIndexEntry empty = { .key = GHOST_INDEX_EMPTY, .host_id = 0, .ghost_idx = 0 };
        self.entries.push(empty);
    }
}

/**
 * Resize the ghost index when load factor is too high.
 * Called automatically by insert when load > 75%.
 */
fn void GhostIndex.resize(GhostIndex* self) {
    usz new_capacity = self.capacity * 2;
    List{GhostIndexEntry} old_entries = self.entries;

    // Create new empty entries
    self.entries = {};
    for (usz i = 0; i < new_capacity; i++) {
        GhostIndexEntry empty = { .key = GHOST_INDEX_EMPTY, .host_id = 0, .ghost_idx = 0 };
        self.entries.push(empty);
    }
    self.capacity = new_capacity;
    self.count = 0;

    // Re-insert all existing entries (skip empty and tombstone slots)
    foreach (&entry : old_entries) {
        if (entry.key != GHOST_INDEX_EMPTY && entry.key != GHOST_INDEX_TOMBSTONE) {
            self.insert_entry(entry.key, entry.host_id, entry.ghost_idx);
        }
    }

    old_entries.free();
}

/**
 * Insert an entry into the ghost index by raw key.
 * Checks for duplicates and updates in-place if the key already exists.
 * Also reuses tombstone slots during probing.
 *
 * @param key       The 64-bit composite key
 * @param host_id   Region holding the ghost table
 * @param ghost_idx Index into host's ghost table list
 */
fn void GhostIndex.insert_entry(GhostIndex* self, ulong key, RegionId host_id, usz ghost_idx) {
    // Check load factor and resize if needed (> 75%)
    if (self.count * 4 >= self.capacity * 3) {
        self.resize();
    }

    usz mask = self.capacity - 1;
    usz idx = ghost_index_hash(key) & mask;

    // Linear probing: check for duplicates, reuse tombstones
    usz first_tombstone = usz.max;  // Track first tombstone for reuse
    while (self.entries[idx].key != GHOST_INDEX_EMPTY) {
        if (self.entries[idx].key == key) {
            // Duplicate key found — update in-place
            self.entries[idx].host_id = host_id;
            self.entries[idx].ghost_idx = ghost_idx;
            return;
        }
        if (self.entries[idx].key == GHOST_INDEX_TOMBSTONE && first_tombstone == usz.max) {
            first_tombstone = idx;
        }
        idx = (idx + 1) & mask;
    }

    // Use tombstone slot if available, otherwise use the empty slot
    usz target = (first_tombstone != usz.max) ? first_tombstone : idx;
    self.entries[target].key = key;
    self.entries[target].host_id = host_id;
    self.entries[target].ghost_idx = ghost_idx;
    self.count++;
}

/**
 * Insert a ghost table mapping into the index.
 *
 * @param source_id  The dead region's ID
 * @param gen        The dead region's generation at time of death
 * @param host_id    The region holding the ghost table
 * @param ghost_idx  Index into host's inherited_ghost_tables list
 */
fn void GhostIndex.insert(GhostIndex* self, RegionId source_id, Generation gen,
                          RegionId host_id, usz ghost_idx) {
    ulong key = ghost_index_make_key(source_id, gen);
    self.insert_entry(key, host_id, ghost_idx);
}

/**
 * Look up a ghost table by source region ID and generation.
 *
 * @param source_id  The dead region's ID
 * @param gen        The dead region's generation
 * @return GhostLookupResult with found=true if entry exists
 */
fn GhostLookupResult GhostIndex.lookup(GhostIndex* self, RegionId source_id, Generation gen) {
    if (self.capacity == 0) {
        return { .found = false, .host_id = 0, .ghost_idx = 0 };
    }

    ulong key = ghost_index_make_key(source_id, gen);
    usz mask = self.capacity - 1;
    usz idx = ghost_index_hash(key) & mask;
    usz start = idx;

    // Linear probing (skip tombstones, stop at empty)
    do {
        if (self.entries[idx].key == key) {
            return {
                .found = true,
                .host_id = self.entries[idx].host_id,
                .ghost_idx = self.entries[idx].ghost_idx
            };
        }
        if (self.entries[idx].key == GHOST_INDEX_EMPTY) {
            // Empty slot means key not in table
            return { .found = false, .host_id = 0, .ghost_idx = 0 };
        }
        // Tombstone slots are skipped — continue probing
        idx = (idx + 1) & mask;
    } while (idx != start);

    return { .found = false, .host_id = 0, .ghost_idx = 0 };
}

/**
 * Update an existing entry in the ghost index.
 *
 * Finds an entry by its source region ID and generation, then updates
 * the host_id and ghost_idx to new values. Used when re-promoting
 * inherited ghost tables to a new parent.
 *
 * @param source_id      The dead region's ID (key part 1)
 * @param gen            The dead region's generation (key part 2)
 * @param new_host_id    The new region holding the ghost table
 * @param new_ghost_idx  The new index into host's inherited_ghost_tables
 * @return true if entry was found and updated, false if not found
 */
fn bool GhostIndex.update(GhostIndex* self, RegionId source_id, Generation gen,
                          RegionId new_host_id, usz new_ghost_idx) {
    if (self.capacity == 0) {
        return false;
    }

    ulong key = ghost_index_make_key(source_id, gen);
    usz mask = self.capacity - 1;
    usz idx = ghost_index_hash(key) & mask;
    usz start = idx;

    // Linear probing to find the entry (skip tombstones, stop at empty)
    do {
        if (self.entries[idx].key == key) {
            // Found it — update the values
            self.entries[idx].host_id = new_host_id;
            self.entries[idx].ghost_idx = new_ghost_idx;
            return true;
        }
        if (self.entries[idx].key == GHOST_INDEX_EMPTY) {
            // Empty slot means key not in table
            return false;
        }
        // Tombstone slots are skipped — continue probing
        idx = (idx + 1) & mask;
    } while (idx != start);

    return false;
}

/**
 * Remove an entry from the ghost index by source region ID and generation.
 *
 * Uses tombstone-based deletion: the entry's key is replaced with
 * GHOST_INDEX_TOMBSTONE so that linear probing continues past it.
 *
 * @param source_id  The dead region's ID
 * @param gen        The dead region's generation
 * @return true if entry was found and removed, false if not found
 */
fn bool GhostIndex.remove(GhostIndex* self, RegionId source_id, Generation gen) {
    if (self.capacity == 0) {
        return false;
    }

    ulong key = ghost_index_make_key(source_id, gen);
    usz mask = self.capacity - 1;
    usz idx = ghost_index_hash(key) & mask;
    usz start = idx;

    // Linear probing to find the entry (skip tombstones, stop at empty)
    do {
        if (self.entries[idx].key == key) {
            // Found it — mark as tombstone
            self.entries[idx].key = GHOST_INDEX_TOMBSTONE;
            self.count--;
            return true;
        }
        if (self.entries[idx].key == GHOST_INDEX_EMPTY) {
            // Empty slot means key not in table
            return false;
        }
        // Tombstone slots are skipped — continue probing
        idx = (idx + 1) & mask;
    } while (idx != start);

    return false;
}

/**
 * Free all resources held by the ghost index.
 */
fn void GhostIndex.release(GhostIndex* self) {
    self.entries.free();
    self.count = 0;
    self.capacity = 0;
}
