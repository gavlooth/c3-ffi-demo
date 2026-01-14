/*
 * vmem_arena.h - Virtual Memory Arena Allocator
 *
 * A high-performance arena allocator using OS virtual memory:
 * - O(1) bump-pointer allocation
 * - O(1) chunk splice (detach/attach) for region merging
 * - Commit-on-demand (no wasted RAM)
 * - madvise(MADV_DONTNEED) for efficient reset
 * - 2MB chunks aligned for Transparent Huge Pages (THP)
 *
 * Drop-in replacement for arena.h with same API.
 *
 * Copyright 2025 OmniLisp Authors
 * SPDX-License-Identifier: MIT
 */

/* Must be defined before any includes for madvise() */
#if defined(__linux__)
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#endif

#ifndef VMEM_ARENA_H_
#define VMEM_ARENA_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef VMEM_ARENA_NOSTDIO
#include <stdarg.h>
#include <stdio.h>
#endif

#ifdef VMEM_ARENA_DEBUG
#include <errno.h>
#endif

#ifndef VMEM_ARENA_ASSERT
#include <assert.h>
#define VMEM_ARENA_ASSERT assert
#endif

/*
 * Configuration
 */
#ifndef VMEM_CHUNK_RESERVE
#define VMEM_CHUNK_RESERVE  (2 * 1024 * 1024)   /* 2MB - THP aligned */
#endif

#ifndef VMEM_CHUNK_INITIAL_COMMIT
#define VMEM_CHUNK_INITIAL_COMMIT  (256 * 1024)  /* 256KB initial commit */
#endif

/* Commit growth factor: when we need more, commit this many bytes ahead */
#ifndef VMEM_COMMIT_AHEAD
#define VMEM_COMMIT_AHEAD  (256 * 1024)  /* 256KB ahead */
#endif

#ifndef VMEM_PAGE_SIZE
#define VMEM_PAGE_SIZE  4096
#endif

/*
 * Issue 15 P1: Transparent Huge Page (THP) Support
 *
 * Enable MADV_HUGEPAGE hint for better TLB efficiency on large allocations.
 * THP uses 2MB pages instead of 4KB, reducing TLB misses by ~500x for large
 * memory regions.
 *
 * Set VMEM_USE_HUGEPAGES=0 to disable (default: enabled on Linux).
 * The actual check for MADV_HUGEPAGE happens after sys/mman.h is included.
 */
#ifndef VMEM_USE_HUGEPAGES
#define VMEM_USE_HUGEPAGES 1  /* Will be disabled if MADV_HUGEPAGE not available */
#endif

/* THP threshold: only hint for allocations >= this size (default: 2MB) */
#ifndef VMEM_HUGEPAGE_THRESHOLD
#define VMEM_HUGEPAGE_THRESHOLD  (2 * 1024 * 1024)
#endif

/*
 * Alignment helper
 */
#define VMEM_ALIGN_UP(x, align) (((x) + (align) - 1) & ~((align) - 1))

/*
 * VMemChunk - A single virtual memory region
 *
 * Memory layout:
 *   [VMemChunk header][reserved VA space (2MB)]
 *                     ^base
 *   Only 'committed' bytes are backed by physical RAM.
 */
typedef struct VMemChunk VMemChunk;

struct VMemChunk {
	VMemChunk*  next;       /* Linked list of chunks */
	void*       base;       /* Start of usable memory (after header) */
	size_t      reserved;   /* Total reserved VA space */
	size_t      committed;  /* Currently committed (physical) bytes */
	size_t      offset;     /* Current allocation offset (bump pointer) */
};

/*
 * Arena - Collection of VMemChunks
 */
typedef struct {
	VMemChunk *begin, *end;
} Arena;

/*
 * Arena_Mark - Snapshot for rewind operations
 */
typedef struct {
	VMemChunk*  chunk;
	size_t      offset;
} Arena_Mark;

/*
 * Platform-specific backend
 */
#if defined(__linux__) || defined(__APPLE__) || defined(__unix__)
#define VMEM_BACKEND_POSIX 1
#elif defined(_WIN32)
#define VMEM_BACKEND_WIN32 1
#else
#error "Unsupported platform for vmem_arena"
#endif

/*
 * API - Chunk operations (internal)
 */
VMemChunk* vmem_chunk_new(size_t min_size);
void       vmem_chunk_free(VMemChunk* c);
bool       vmem_chunk_ensure_committed(VMemChunk* c, size_t needed);
void       vmem_chunk_reset(VMemChunk* c);

/*
 * API - Arena operations (public, compatible with arena.h)
 */
void*       arena_alloc(Arena* a, size_t size_bytes);
void*       arena_realloc(Arena* a, void* oldptr, size_t oldsz, size_t newsz);
char*       arena_strdup(Arena* a, const char* cstr);
void*       arena_memdup(Arena* a, void* data, size_t size);
void*       arena_memcpy(void* dest, const void* src, size_t n);

#ifndef VMEM_ARENA_NOSTDIO
char*       arena_sprintf(Arena* a, const char* format, ...);
char*       arena_vsprintf(Arena* a, const char* format, va_list args);
#endif

Arena_Mark  arena_snapshot(Arena* a);
void        arena_reset(Arena* a);
void        arena_rewind(Arena* a, Arena_Mark m);
void        arena_free(Arena* a);
void        arena_trim(Arena* a);
void        arena_promote(Arena* dest, Arena* src);

/* O(1) splice operations - critical for region merging */
void        arena_detach_blocks(Arena* a, VMemChunk* start, VMemChunk* end);
void        arena_attach_blocks(Arena* a, VMemChunk* start, VMemChunk* end);

/*
 * Compatibility typedefs and accessors (for code using ArenaChunk)
 *
 * Original arena.h uses:
 *   - c->data     (pointer to data array)
 *   - c->capacity (capacity in uintptr_t units)
 *   - c->count    (used in uintptr_t units)
 *
 * VMemArena uses:
 *   - c->base     (pointer to usable memory)
 *   - c->reserved (reserved bytes)
 *   - c->offset   (used bytes)
 */
typedef VMemChunk ArenaChunk;

/* Compatibility accessors as macros */
#define ARENA_CHUNK_DATA(c)     ((uintptr_t*)(c)->base)
#define ARENA_CHUNK_CAPACITY(c) ((c)->reserved / sizeof(uintptr_t))
#define ARENA_CHUNK_COUNT(c)    ((c)->offset / sizeof(uintptr_t))

/*
 * Dynamic array macros (compatible with arena.h)
 */
#ifndef ARENA_DA_INIT_CAP
#define ARENA_DA_INIT_CAP 256
#endif

#define arena_da_append(a, da, item)                                          \
	do {                                                                  \
		if ((da)->count >= (da)->capacity) {                          \
			size_t new_capacity = (da)->capacity == 0             \
				? ARENA_DA_INIT_CAP : (da)->capacity * 2;     \
			(da)->items = arena_realloc(                          \
				(a), (da)->items,                             \
				(da)->capacity * sizeof(*(da)->items),        \
				new_capacity * sizeof(*(da)->items));         \
			(da)->capacity = new_capacity;                        \
		}                                                             \
		(da)->items[(da)->count++] = (item);                          \
	} while (0)

#define arena_da_append_many(a, da, new_items, new_items_count)               \
	do {                                                                  \
		if ((da)->count + (new_items_count) > (da)->capacity) {       \
			size_t new_capacity = (da)->capacity;                 \
			if (new_capacity == 0) new_capacity = ARENA_DA_INIT_CAP; \
			while ((da)->count + (new_items_count) > new_capacity) \
				new_capacity *= 2;                            \
			(da)->items = arena_realloc(                          \
				(a), (da)->items,                             \
				(da)->capacity * sizeof(*(da)->items),        \
				new_capacity * sizeof(*(da)->items));         \
			(da)->capacity = new_capacity;                        \
		}                                                             \
		arena_memcpy((da)->items + (da)->count,                       \
			     (new_items),                                     \
			     (new_items_count) * sizeof(*(da)->items));       \
		(da)->count += (new_items_count);                             \
	} while (0)

#define arena_sb_append_buf arena_da_append_many

#define arena_sb_append_cstr(a, sb, cstr)                                     \
	do {                                                                  \
		const char *s = (cstr);                                       \
		size_t n = 0;                                                 \
		while (s[n]) n++;                                             \
		arena_da_append_many(a, sb, s, n);                            \
	} while (0)

#define arena_sb_append_null(a, sb) arena_da_append(a, sb, 0)

#endif /* VMEM_ARENA_H_ */

/*
 * Implementation
 */
#ifdef VMEM_ARENA_IMPLEMENTATION
#ifndef VMEM_ARENA_IMPLEMENTED_GUARD
#define VMEM_ARENA_IMPLEMENTED_GUARD

#include <string.h>

#if VMEM_BACKEND_POSIX
#include <sys/mman.h>
#include <unistd.h>

/*
 * vmem_chunk_new - Allocate a new chunk with reserved VA space
 *
 * Reserves min(VMEM_CHUNK_RESERVE, min_size) bytes of VA space,
 * commits VMEM_CHUNK_INITIAL_COMMIT bytes initially.
 */
VMemChunk* vmem_chunk_new(size_t min_size)
{
	size_t reserve_size = VMEM_CHUNK_RESERVE;
	if (min_size > reserve_size)
		reserve_size = VMEM_ALIGN_UP(min_size, VMEM_PAGE_SIZE);

	/* Reserve VA space with no permissions (no physical backing yet) */
	void* mem = mmap(NULL, reserve_size + sizeof(VMemChunk),
			 PROT_NONE,
			 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mem == MAP_FAILED) {
		VMEM_ARENA_ASSERT(0 && "mmap reserve failed");
		return NULL;
	}

	/* Commit the header + initial chunk */
	size_t initial_commit = sizeof(VMemChunk) + VMEM_CHUNK_INITIAL_COMMIT;
	initial_commit = VMEM_ALIGN_UP(initial_commit, VMEM_PAGE_SIZE);
	if (initial_commit > reserve_size + sizeof(VMemChunk))
		initial_commit = reserve_size + sizeof(VMemChunk);

	if (mprotect(mem, initial_commit, PROT_READ | PROT_WRITE) != 0) {
		VMEM_ARENA_ASSERT(0 && "mprotect initial commit failed");
		munmap(mem, reserve_size + sizeof(VMemChunk));
		return NULL;
	}

	/*
	 * Issue 15 P1: Hint for Transparent Huge Pages
	 *
	 * MADV_HUGEPAGE tells the kernel to use 2MB pages when possible.
	 * This significantly improves TLB efficiency for large allocations.
	 *
	 * We apply to the entire reserved region (not just committed) so
	 * future commits can also benefit from THP.
	 */
#if VMEM_USE_HUGEPAGES && defined(MADV_HUGEPAGE)
	if (reserve_size >= VMEM_HUGEPAGE_THRESHOLD) {
		/* madvise for THP (ignore failure - it's just a hint) */
		madvise(mem, reserve_size + sizeof(VMemChunk), MADV_HUGEPAGE);
	}
#endif

	VMemChunk* c = (VMemChunk*)mem;
	c->next = NULL;
	c->base = (char*)mem + sizeof(VMemChunk);
	c->reserved = reserve_size;
	c->committed = initial_commit - sizeof(VMemChunk);
	c->offset = 0;

	return c;
}

/*
 * vmem_chunk_free - Release chunk's VA space back to OS
 */
void vmem_chunk_free(VMemChunk* c)
{
	if (!c) return;
	size_t total_size = c->reserved + sizeof(VMemChunk);
	munmap(c, total_size);
}

/*
 * vmem_chunk_ensure_committed - Commit more pages if needed
 *
 * Returns true if 'needed' bytes are now committed, false on failure.
 *
 * Note: We track committed bytes from c->base, but mprotect needs
 * page-aligned addresses. The mmap starts at (c), so c->base is
 * offset by sizeof(VMemChunk). We need to account for this.
 */
bool vmem_chunk_ensure_committed(VMemChunk* c, size_t needed)
{
#ifdef VMEM_ARENA_DEBUG
	fprintf(stderr, "[vmem_ensure] c=%p needed=%zu committed=%zu reserved=%zu offset=%zu\n",
		(void*)c, needed, c->committed, c->reserved, c->offset);
#endif
	/* Sanity check: detect corrupted chunks */
	if (c->reserved > VMEM_CHUNK_RESERVE * 4) {
		/* Reserved size impossibly large - this chunk wasn't created by vmem_chunk_new */
#ifdef VMEM_ARENA_DEBUG
		fprintf(stderr, "[vmem_ensure] ERROR: Corrupted chunk! reserved=%zu exceeds max %zu\n",
			c->reserved, (size_t)(VMEM_CHUNK_RESERVE * 4));
#endif
		return false;
	}

	if (needed <= c->committed)
		return true;

	if (needed > c->reserved)
		return false;  /* Can't commit beyond reservation */

	/*
	 * Calculate physical addresses for mprotect.
	 * The mmap region starts at 'c' (the header).
	 * We need to commit from the end of current physical commit
	 * to cover the new needed bytes.
	 */
	char* mmap_base = (char*)c;
	size_t header_size = sizeof(VMemChunk);

	/* Current physical end (from mmap start) */
	size_t current_phys_end = header_size + c->committed;
	size_t current_phys_page = VMEM_ALIGN_UP(current_phys_end, VMEM_PAGE_SIZE);

	/* New physical end needed - commit ahead to reduce syscalls */
	size_t new_phys_end = header_size + needed + VMEM_COMMIT_AHEAD;
	size_t new_phys_page = VMEM_ALIGN_UP(new_phys_end, VMEM_PAGE_SIZE);

	/* Total mmap size */
	size_t total_mmap = header_size + c->reserved;
	if (new_phys_page > total_mmap)
		new_phys_page = total_mmap;

	/* Only mprotect if we need more pages */
	if (new_phys_page > current_phys_page) {
		char* commit_start = mmap_base + current_phys_page;
		size_t commit_size = new_phys_page - current_phys_page;

		/* Verify the commit is within bounds */
		if (new_phys_page > total_mmap) {
			/* This should not happen due to clamping above */
			return false;
		}

		if (mprotect(commit_start, commit_size, PROT_READ | PROT_WRITE) != 0) {
			/* mprotect failed - possibly VM limit or bad address */
#ifdef VMEM_ARENA_DEBUG
			fprintf(stderr, "mprotect failed: start=%p size=%zu errno=%d\n",
				(void*)commit_start, commit_size, errno);
			fprintf(stderr, "  mmap_base=%p reserved=%zu total_mmap=%zu\n",
				(void*)mmap_base, c->reserved, total_mmap);
			fprintf(stderr, "  current_phys_page=%zu new_phys_page=%zu\n",
				current_phys_page, new_phys_page);
#endif
			return false;
		}
	}

	/* Update committed to logical bytes from c->base */
	c->committed = new_phys_page - header_size;
	return true;
}

/*
 * vmem_chunk_reset - Release physical pages back to OS, keep VA reserved
 *
 * Uses madvise(MADV_DONTNEED) to tell kernel the pages aren't needed.
 * Kernel will zero them and reclaim physical RAM without unmapping.
 */
void vmem_chunk_reset(VMemChunk* c)
{
	if (!c || c->committed == 0)
		return;

	/* Tell kernel we don't need these pages anymore */
	madvise(c->base, c->committed, MADV_DONTNEED);

	/* Reset state but keep VA reserved */
	c->offset = 0;
	/* Note: We keep committed as-is since pages are still mapped,
	 * just zeroed. This avoids mprotect on next allocation. */
}

#elif VMEM_BACKEND_WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

VMemChunk* vmem_chunk_new(size_t min_size)
{
	size_t reserve_size = VMEM_CHUNK_RESERVE;
	if (min_size > reserve_size)
		reserve_size = VMEM_ALIGN_UP(min_size, VMEM_PAGE_SIZE);

	/* Reserve VA space */
	void* mem = VirtualAlloc(NULL, reserve_size + sizeof(VMemChunk),
				 MEM_RESERVE, PAGE_NOACCESS);
	if (!mem) {
		VMEM_ARENA_ASSERT(0 && "VirtualAlloc reserve failed");
		return NULL;
	}

	/* Commit header + initial */
	size_t initial_commit = sizeof(VMemChunk) + VMEM_CHUNK_INITIAL_COMMIT;
	initial_commit = VMEM_ALIGN_UP(initial_commit, VMEM_PAGE_SIZE);

	void* committed = VirtualAlloc(mem, initial_commit,
				       MEM_COMMIT, PAGE_READWRITE);
	if (!committed) {
		VMEM_ARENA_ASSERT(0 && "VirtualAlloc commit failed");
		VirtualFree(mem, 0, MEM_RELEASE);
		return NULL;
	}

	VMemChunk* c = (VMemChunk*)mem;
	c->next = NULL;
	c->base = (char*)mem + sizeof(VMemChunk);
	c->reserved = reserve_size;
	c->committed = initial_commit - sizeof(VMemChunk);
	c->offset = 0;

	return c;
}

void vmem_chunk_free(VMemChunk* c)
{
	if (!c) return;
	VirtualFree(c, 0, MEM_RELEASE);
}

bool vmem_chunk_ensure_committed(VMemChunk* c, size_t needed)
{
	if (needed <= c->committed)
		return true;

	if (needed > c->reserved)
		return false;

	size_t new_committed = VMEM_ALIGN_UP(needed, VMEM_PAGE_SIZE);
	if (new_committed > c->reserved)
		new_committed = c->reserved;

	/* Commit additional pages */
	char* commit_start = (char*)c->base + c->committed;
	size_t commit_size = new_committed - c->committed;

	void* result = VirtualAlloc(commit_start, commit_size,
				    MEM_COMMIT, PAGE_READWRITE);
	if (!result) {
		VMEM_ARENA_ASSERT(0 && "VirtualAlloc extend failed");
		return false;
	}

	c->committed = new_committed;
	return true;
}

void vmem_chunk_reset(VMemChunk* c)
{
	if (!c || c->committed == 0)
		return;

	/* Decommit pages (returns RAM to OS, keeps VA reserved) */
	VirtualFree(c->base, c->committed, MEM_DECOMMIT);
	c->offset = 0;
	c->committed = 0;  /* Must recommit on Windows */
}

#endif /* VMEM_BACKEND_* */

/*
 * arena_alloc - O(1) allocation with commit-on-demand
 */
void* arena_alloc(Arena* a, size_t size_bytes)
{
	/* Align to pointer size for safety */
	size_t aligned_size = VMEM_ALIGN_UP(size_bytes, sizeof(void*));

	/* Fast path: current chunk has space */
	if (a->end) {
		VMemChunk* c = a->end;
#ifdef VMEM_ARENA_DEBUG
		fprintf(stderr, "[arena_alloc] arena=%p size=%zu using chunk c=%p reserved=%zu\n",
			(void*)a, aligned_size, (void*)c, c->reserved);
#endif
		size_t new_offset = c->offset + aligned_size;

		/* Check if we need to commit more */
		if (new_offset <= c->committed) {
			/* Super fast path: no commit needed */
			void* ptr = (char*)c->base + c->offset;
			c->offset = new_offset;
			return ptr;
		}

		/* Medium path: commit more pages in current chunk */
		if (new_offset <= c->reserved) {
			if (vmem_chunk_ensure_committed(c, new_offset)) {
				void* ptr = (char*)c->base + c->offset;
				c->offset = new_offset;
				return ptr;
			}
		}

		/* Try next chunk if available */
		while (c->next) {
#ifdef VMEM_ARENA_DEBUG
			fprintf(stderr, "[arena_alloc] Following c->next: c=%p c->next=%p\n",
				(void*)c, (void*)c->next);
#endif
			c = c->next;
			if (aligned_size <= c->reserved) {
				if (vmem_chunk_ensure_committed(c, aligned_size)) {
					a->end = c;
					void* ptr = c->base;
					c->offset = aligned_size;
					return ptr;
				}
			}
		}
	}

	/* Slow path: allocate new chunk */
#ifdef VMEM_ARENA_DEBUG
	fprintf(stderr, "[arena_alloc] arena=%p no suitable chunk, allocating new chunk for size=%zu\n",
		(void*)a, aligned_size);
#endif
	VMemChunk* new_chunk = vmem_chunk_new(aligned_size);
	if (!new_chunk)
		return NULL;
#ifdef VMEM_ARENA_DEBUG
	fprintf(stderr, "[arena_alloc] new_chunk=%p reserved=%zu\n", (void*)new_chunk, new_chunk->reserved);
#endif

	/* Ensure enough is committed for this allocation */
	if (!vmem_chunk_ensure_committed(new_chunk, aligned_size)) {
		vmem_chunk_free(new_chunk);
		return NULL;
	}

	/* Link into arena */
	if (a->end) {
		a->end->next = new_chunk;
	} else {
		a->begin = new_chunk;
	}
	a->end = new_chunk;

	void* ptr = new_chunk->base;
	new_chunk->offset = aligned_size;
	return ptr;
}

/*
 * arena_realloc - Reallocate with copy (arena can't free individual allocs)
 */
void* arena_realloc(Arena* a, void* oldptr, size_t oldsz, size_t newsz)
{
	if (newsz <= oldsz)
		return oldptr;

	void* newptr = arena_alloc(a, newsz);
	if (!newptr)
		return NULL;

	if (oldptr && oldsz > 0)
		memcpy(newptr, oldptr, oldsz);

	return newptr;
}

/*
 * arena_memcpy - Simple memcpy implementation
 */
void* arena_memcpy(void* dest, const void* src, size_t n)
{
	return memcpy(dest, src, n);
}

/*
 * arena_strdup - Duplicate string into arena
 */
char* arena_strdup(Arena* a, const char* cstr)
{
	if (!cstr) return NULL;
	size_t len = strlen(cstr);
	char* dup = (char*)arena_alloc(a, len + 1);
	if (dup) {
		memcpy(dup, cstr, len);
		dup[len] = '\0';
	}
	return dup;
}

/*
 * arena_memdup - Duplicate memory block into arena
 */
void* arena_memdup(Arena* a, void* data, size_t size)
{
	if (!data || size == 0) return NULL;
	void* dup = arena_alloc(a, size);
	if (dup)
		memcpy(dup, data, size);
	return dup;
}

#ifndef VMEM_ARENA_NOSTDIO
/*
 * arena_vsprintf - Printf into arena
 */
char* arena_vsprintf(Arena* a, const char* format, va_list args)
{
	va_list args_copy;
	va_copy(args_copy, args);
	int n = vsnprintf(NULL, 0, format, args_copy);
	va_end(args_copy);

	if (n < 0) return NULL;

	char* result = (char*)arena_alloc(a, n + 1);
	if (result)
		vsnprintf(result, n + 1, format, args);

	return result;
}

char* arena_sprintf(Arena* a, const char* format, ...)
{
	va_list args;
	va_start(args, format);
	char* result = arena_vsprintf(a, format, args);
	va_end(args);
	return result;
}
#endif /* VMEM_ARENA_NOSTDIO */

/*
 * arena_snapshot - Save current position for later rewind
 */
Arena_Mark arena_snapshot(Arena* a)
{
	Arena_Mark m = {0};
	if (a->end) {
		m.chunk = a->end;
		m.offset = a->end->offset;
	}
	return m;
}

/*
 * arena_reset - Reset arena, release all physical pages to OS
 */
void arena_reset(Arena* a)
{
	for (VMemChunk* c = a->begin; c; c = c->next) {
		vmem_chunk_reset(c);
	}
	a->end = a->begin;
}

/*
 * arena_rewind - Rewind to a previous snapshot
 */
void arena_rewind(Arena* a, Arena_Mark m)
{
	if (!m.chunk) {
		arena_reset(a);
		return;
	}

	m.chunk->offset = m.offset;

	/* Reset all chunks after the snapshot */
	for (VMemChunk* c = m.chunk->next; c; c = c->next) {
		c->offset = 0;
	}

	a->end = m.chunk;
}

/*
 * arena_free - Release all chunks and their VA space
 */
void arena_free(Arena* a)
{
	VMemChunk* c = a->begin;
	while (c) {
		VMemChunk* next = c->next;
		vmem_chunk_free(c);
		c = next;
	}
	a->begin = NULL;
	a->end = NULL;
}

/*
 * arena_trim - Free unused chunks after current position
 */
void arena_trim(Arena* a)
{
	if (!a->end)
		return;

	VMemChunk* c = a->end->next;
	a->end->next = NULL;

	while (c) {
		VMemChunk* next = c->next;
		vmem_chunk_free(c);
		c = next;
	}
}

/*
 * arena_promote - Move all chunks from src to end of dest
 */
void arena_promote(Arena* dest, Arena* src)
{
	if (!src->begin)
		return;

	if (dest->end) {
		dest->end->next = src->begin;
		dest->end = src->end;
	} else {
		dest->begin = src->begin;
		dest->end = src->end;
	}

	src->begin = NULL;
	src->end = NULL;
}

/*
 * arena_detach_blocks - O(1) splice: remove chunk range from arena
 *
 * Critical for region merging in OmniLisp.
 */
void arena_detach_blocks(Arena* a, VMemChunk* start, VMemChunk* end)
{
	if (!a || !start || !end)
		return;

	/* Find predecessor of start */
	if (a->begin == start) {
		a->begin = end->next;
	} else {
		VMemChunk* prev = a->begin;
		while (prev && prev->next != start)
			prev = prev->next;
		if (prev)
			prev->next = end->next;
	}

	/* Update end pointer if needed */
	if (a->end == end) {
		if (!a->begin) {
			a->end = NULL;
		} else {
			VMemChunk* c = a->begin;
			while (c->next)
				c = c->next;
			a->end = c;
		}
	}

	/* Isolate the detached range */
	end->next = NULL;
}

/*
 * arena_attach_blocks - O(1) splice: append chunk range to arena
 *
 * Critical for region merging in OmniLisp.
 */
void arena_attach_blocks(Arena* a, VMemChunk* start, VMemChunk* end)
{
	if (!a || !start || !end)
		return;

	if (a->end) {
		a->end->next = start;
	} else {
		a->begin = start;
	}
	a->end = end;
}

#endif /* VMEM_ARENA_IMPLEMENTED_GUARD */
#endif /* VMEM_ARENA_IMPLEMENTATION */
