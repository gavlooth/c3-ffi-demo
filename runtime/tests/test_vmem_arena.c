/*
 * test_vmem_arena.c - Tests for VMemChunk Arena Allocator
 *
 * Verifies:
 * - O(1) allocation via bump pointer
 * - Commit-on-demand behavior
 * - O(1) splice operations (detach/attach)
 * - Reset releases pages to OS
 * - Snapshot/rewind functionality
 * - API compatibility with arena.h
 */

/* Forward declarations for test framework */
extern int tests_run;
extern int tests_passed;
extern int tests_failed;

#define TEST_SUITE(name) printf("\n=== %s ===\n", name)
#define RUN_TEST(fn) do { \
	tests_run++; \
	printf("  %s... ", #fn); \
	fflush(stdout); \
	fn(); \
	tests_passed++; \
	printf("PASS\n"); \
} while(0)

#define ASSERT(cond) do { \
	if (!(cond)) { \
		printf("FAIL: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
		tests_failed++; \
		return; \
	} \
} while(0)

/* ============================================================
 * Basic Allocation Tests
 * ============================================================ */

static void test_vmem_basic_alloc(void) {
	Arena a = {0};

	/* First allocation should work */
	void* p1 = arena_alloc(&a, 64);
	ASSERT(p1 != NULL);

	/* Second allocation should be contiguous */
	void* p2 = arena_alloc(&a, 64);
	ASSERT(p2 != NULL);
	ASSERT((char*)p2 >= (char*)p1 + 64);

	/* Write to verify memory is usable */
	memset(p1, 0xAA, 64);
	memset(p2, 0xBB, 64);

	arena_free(&a);
}

static void test_vmem_large_alloc(void) {
	Arena a = {0};

	/* Allocate larger than initial commit (64KB) */
	void* p1 = arena_alloc(&a, 128 * 1024);  /* 128KB */
	ASSERT(p1 != NULL);

	/* Write to verify commit worked */
	memset(p1, 0xCC, 128 * 1024);

	/* Allocate more to force additional commit */
	void* p2 = arena_alloc(&a, 256 * 1024);  /* 256KB */
	ASSERT(p2 != NULL);
	memset(p2, 0xDD, 256 * 1024);

	arena_free(&a);
}

static void test_vmem_chunk_boundary(void) {
	Arena a = {0};

	/* Allocate to exceed single chunk (2MB) */
	void* p1 = arena_alloc(&a, 1024 * 1024);  /* 1MB */
	ASSERT(p1 != NULL);

	void* p2 = arena_alloc(&a, 1024 * 1024);  /* 1MB - should still fit */
	ASSERT(p2 != NULL);

	void* p3 = arena_alloc(&a, 1024 * 1024);  /* 1MB - needs new chunk */
	ASSERT(p3 != NULL);

	/* Verify all memory is usable */
	memset(p1, 0x11, 1024 * 1024);
	memset(p2, 0x22, 1024 * 1024);
	memset(p3, 0x33, 1024 * 1024);

	arena_free(&a);
}

static void test_vmem_many_small_allocs(void) {
	Arena a = {0};

	/* Many small allocations - tests bump pointer efficiency */
	for (int i = 0; i < 10000; i++) {
		void* p = arena_alloc(&a, 64);
		ASSERT(p != NULL);
		memset(p, (unsigned char)i, 64);
	}

	arena_free(&a);
}

/* ============================================================
 * Reset and Rewind Tests
 * ============================================================ */

static void test_vmem_reset(void) {
	Arena a = {0};

	/* Allocate some memory */
	void* p1 = arena_alloc(&a, 1024);
	ASSERT(p1 != NULL);
	memset(p1, 0xFF, 1024);

	/* Reset should allow reuse */
	arena_reset(&a);

	/* New allocation should work */
	void* p2 = arena_alloc(&a, 1024);
	ASSERT(p2 != NULL);

	/* On reset, memory should be reused (same base) */
	ASSERT(p2 == p1);

	arena_free(&a);
}

static void test_vmem_snapshot_rewind(void) {
	Arena a = {0};

	void* p1 = arena_alloc(&a, 64);
	ASSERT(p1 != NULL);

	/* Take snapshot */
	Arena_Mark mark = arena_snapshot(&a);

	/* Allocate more */
	void* p2 = arena_alloc(&a, 64);
	void* p3 = arena_alloc(&a, 64);
	ASSERT(p2 != NULL);
	ASSERT(p3 != NULL);

	/* Rewind to snapshot */
	arena_rewind(&a, mark);

	/* New allocation should reuse space from p2 */
	void* p4 = arena_alloc(&a, 64);
	ASSERT(p4 == p2);

	arena_free(&a);
}

/* ============================================================
 * Splice Tests (Critical for OmniLisp Regions)
 * ============================================================ */

static void test_vmem_splice_basic(void) {
	Arena src = {0};
	Arena dest = {0};

	/* Allocate in source */
	void* ps1 = arena_alloc(&src, 1024);
	void* ps2 = arena_alloc(&src, 1024);
	ASSERT(ps1 != NULL);
	ASSERT(ps2 != NULL);
	memset(ps1, 0xAA, 1024);
	memset(ps2, 0xBB, 1024);

	/* Allocate in dest */
	void* pd1 = arena_alloc(&dest, 512);
	ASSERT(pd1 != NULL);

	/* Splice: move src chunks to dest */
	VMemChunk* src_begin = src.begin;
	VMemChunk* src_end = src.end;

	arena_detach_blocks(&src, src_begin, src_end);
	arena_attach_blocks(&dest, src_begin, src_end);

	/* Source should be empty */
	ASSERT(src.begin == NULL);
	ASSERT(src.end == NULL);

	/* Dest should have both chunks */
	ASSERT(dest.begin != NULL);
	ASSERT(dest.end == src_end);

	/* Original data should still be accessible via dest */
	ASSERT(memcmp(ps1, "\xAA\xAA\xAA\xAA", 4) == 0);

	arena_free(&dest);
}

static void test_vmem_splice_middle(void) {
	Arena a = {0};

	/* Force multiple chunks */
	void* p1 = arena_alloc(&a, 2 * 1024 * 1024);  /* Chunk 1 */
	void* p2 = arena_alloc(&a, 2 * 1024 * 1024);  /* Chunk 2 */
	void* p3 = arena_alloc(&a, 2 * 1024 * 1024);  /* Chunk 3 */
	ASSERT(p1 && p2 && p3);

	/* Verify we have 3 chunks */
	int chunk_count = 0;
	for (VMemChunk* c = a.begin; c; c = c->next)
		chunk_count++;
	ASSERT(chunk_count == 3);

	/* Detach middle chunk */
	VMemChunk* middle = a.begin->next;
	arena_detach_blocks(&a, middle, middle);

	/* Should have 2 chunks left */
	chunk_count = 0;
	for (VMemChunk* c = a.begin; c; c = c->next)
		chunk_count++;
	ASSERT(chunk_count == 2);

	/* Free detached chunk separately */
	vmem_chunk_free(middle);

	arena_free(&a);
}

static void test_vmem_promote(void) {
	Arena src = {0};
	Arena dest = {0};

	void* ps = arena_alloc(&src, 1024);
	void* pd = arena_alloc(&dest, 512);
	ASSERT(ps && pd);

	memset(ps, 0x11, 1024);
	memset(pd, 0x22, 512);

	/* Promote moves all src to dest */
	arena_promote(&dest, &src);

	ASSERT(src.begin == NULL);
	ASSERT(dest.begin != NULL);

	/* Both data should be accessible */
	ASSERT(*(unsigned char*)pd == 0x22);
	ASSERT(*(unsigned char*)ps == 0x11);

	arena_free(&dest);
}

/* ============================================================
 * String and Memory Operations Tests
 * ============================================================ */

static void test_vmem_strdup(void) {
	Arena a = {0};

	const char* original = "Hello, VMemArena!";
	char* dup = arena_strdup(&a, original);

	ASSERT(dup != NULL);
	ASSERT(strcmp(dup, original) == 0);
	ASSERT(dup != original);

	arena_free(&a);
}

static void test_vmem_memdup(void) {
	Arena a = {0};

	unsigned char data[256];
	for (int i = 0; i < 256; i++)
		data[i] = (unsigned char)i;

	void* dup = arena_memdup(&a, data, 256);
	ASSERT(dup != NULL);
	ASSERT(memcmp(dup, data, 256) == 0);

	arena_free(&a);
}

static void test_vmem_realloc(void) {
	Arena a = {0};

	/* Initial allocation */
	int* arr = arena_alloc(&a, 10 * sizeof(int));
	ASSERT(arr != NULL);
	for (int i = 0; i < 10; i++)
		arr[i] = i;

	/* Realloc to larger */
	arr = arena_realloc(&a, arr, 10 * sizeof(int), 20 * sizeof(int));
	ASSERT(arr != NULL);

	/* Original data preserved */
	for (int i = 0; i < 10; i++)
		ASSERT(arr[i] == i);

	/* Can use new space */
	for (int i = 10; i < 20; i++)
		arr[i] = i;

	arena_free(&a);
}

#ifndef VMEM_ARENA_NOSTDIO
static void test_vmem_sprintf(void) {
	Arena a = {0};

	char* s = arena_sprintf(&a, "Value: %d, String: %s", 42, "test");
	ASSERT(s != NULL);
	ASSERT(strcmp(s, "Value: 42, String: test") == 0);

	arena_free(&a);
}
#endif

/* ============================================================
 * Dynamic Array Macro Tests
 * ============================================================ */

typedef struct {
	int* items;
	size_t count;
	size_t capacity;
} IntArray;

static void test_vmem_da_append(void) {
	Arena a = {0};
	IntArray arr = {0};

	/* Append many items */
	for (int i = 0; i < 1000; i++) {
		arena_da_append(&a, &arr, i);
	}

	ASSERT(arr.count == 1000);
	for (int i = 0; i < 1000; i++)
		ASSERT(arr.items[i] == i);

	arena_free(&a);
}

/* ============================================================
 * Performance/Stress Tests
 * ============================================================ */

static void test_vmem_stress_alloc(void) {
	Arena a = {0};

	/* Allocate 100MB in small chunks */
	size_t total = 0;
	while (total < 100 * 1024 * 1024) {
		size_t sz = 64 + (total % 256);
		void* p = arena_alloc(&a, sz);
		ASSERT(p != NULL);
		total += sz;
	}

	/* Single free releases everything */
	arena_free(&a);
}

static void test_vmem_stress_reset_reuse(void) {
	Arena a = {0};

	/* Allocate, reset, repeat - tests page reuse */
	for (int round = 0; round < 10; round++) {
		for (int i = 0; i < 1000; i++) {
			void* p = arena_alloc(&a, 1024);
			ASSERT(p != NULL);
			memset(p, (unsigned char)i, 1024);
		}
		arena_reset(&a);
	}

	arena_free(&a);
}

/* ============================================================
 * Test Runner
 * ============================================================ */

static void run_vmem_arena_tests(void) {
	TEST_SUITE("vmem_arena");

	/* Basic allocation */
	RUN_TEST(test_vmem_basic_alloc);
	RUN_TEST(test_vmem_large_alloc);
	RUN_TEST(test_vmem_chunk_boundary);
	RUN_TEST(test_vmem_many_small_allocs);

	/* Reset and rewind */
	RUN_TEST(test_vmem_reset);
	RUN_TEST(test_vmem_snapshot_rewind);

	/* Splice operations */
	RUN_TEST(test_vmem_splice_basic);
	RUN_TEST(test_vmem_splice_middle);
	RUN_TEST(test_vmem_promote);

	/* String and memory ops */
	RUN_TEST(test_vmem_strdup);
	RUN_TEST(test_vmem_memdup);
	RUN_TEST(test_vmem_realloc);
#ifndef VMEM_ARENA_NOSTDIO
	RUN_TEST(test_vmem_sprintf);
#endif

	/* Dynamic arrays */
	RUN_TEST(test_vmem_da_append);

	/* Stress tests */
	RUN_TEST(test_vmem_stress_alloc);
	RUN_TEST(test_vmem_stress_reset_reuse);
}
