package memory

import (
	"fmt"
	"io"
)

// ArenaGenerator generates arena allocator code
// Used for cyclic data that doesn't escape function scope
type ArenaGenerator struct {
	w io.Writer
}

// NewArenaGenerator creates a new arena generator
func NewArenaGenerator(w io.Writer) *ArenaGenerator {
	return &ArenaGenerator{w: w}
}

func (g *ArenaGenerator) emit(format string, args ...interface{}) {
	fmt.Fprintf(g.w, format, args...)
}

// GenerateArenaRuntime generates the arena allocator runtime
func (g *ArenaGenerator) GenerateArenaRuntime() {
	g.emit(`/* Arena Allocator */
/* For bulk allocation/deallocation of cyclic structures */

#define ARENA_BLOCK_SIZE 4096

typedef struct ArenaBlock {
    char* memory;
    size_t size;
    size_t used;
    struct ArenaBlock* next;
} ArenaBlock;

typedef struct ArenaExternal {
    void* ptr;
    void (*cleanup)(void*);
    struct ArenaExternal* next;
} ArenaExternal;

typedef struct Arena {
    ArenaBlock* current;
    ArenaBlock* blocks;
    size_t block_size;
    ArenaExternal* externals;
} Arena;

Arena* arena_create(void) {
    Arena* a = malloc(sizeof(Arena));
    if (!a) return NULL;
    a->current = NULL;
    a->blocks = NULL;
    a->block_size = ARENA_BLOCK_SIZE;
    a->externals = NULL;
    return a;
}

void* arena_alloc(Arena* a, size_t size) {
    if (!a) return NULL;

    /* Align to 8 bytes */
    size = (size + 7) & ~(size_t)7;

    if (!a->current || a->current->used + size > a->current->size) {
        /* Need new block */
        size_t block_size = a->block_size;
        if (size > block_size) block_size = size;

        ArenaBlock* b = malloc(sizeof(ArenaBlock));
        if (!b) return NULL;
        b->memory = malloc(block_size);
        if (!b->memory) {
            free(b);
            return NULL;
        }
        b->size = block_size;
        b->used = 0;
        b->next = a->blocks;
        a->blocks = b;
        a->current = b;
    }

    void* ptr = a->current->memory + a->current->used;
    a->current->used += size;
    return ptr;
}

Obj* arena_mk_int(Arena* a, long i) {
    Obj* x = arena_alloc(a, sizeof(Obj));
    if (!x) return NULL;
    x->mark = 0;  /* Arena objects don't use RC */
    x->scc_id = -1;
    x->is_pair = 0;
    x->scan_tag = 0;
    x->i = i;
    return x;
}

Obj* arena_mk_pair(Arena* a, Obj* car, Obj* cdr) {
    Obj* x = arena_alloc(a, sizeof(Obj));
    if (!x) return NULL;
    x->mark = 0;
    x->scc_id = -1;
    x->is_pair = 1;
    x->scan_tag = 0;
    x->a = car;
    x->b = cdr;
    return x;
}

void arena_register_external(Arena* a, void* ptr, void (*cleanup)(void*)) {
    if (!a || !ptr) return;
    ArenaExternal* e = malloc(sizeof(ArenaExternal));
    if (!e) return;
    e->ptr = ptr;
    e->cleanup = cleanup;
    e->next = a->externals;
    a->externals = e;
}

void arena_destroy(Arena* a) {
    if (!a) return;

    /* Clean up external pointers */
    ArenaExternal* e = a->externals;
    while (e) {
        ArenaExternal* next = e->next;
        if (e->cleanup) {
            e->cleanup(e->ptr);
        }
        free(e);
        e = next;
    }

    /* Free all blocks */
    ArenaBlock* b = a->blocks;
    while (b) {
        ArenaBlock* next = b->next;
        free(b->memory);
        free(b);
        b = next;
    }

    free(a);
}

void arena_reset(Arena* a) {
    if (!a) return;

    /* Clean up externals */
    ArenaExternal* e = a->externals;
    while (e) {
        ArenaExternal* next = e->next;
        if (e->cleanup) {
            e->cleanup(e->ptr);
        }
        free(e);
        e = next;
    }
    a->externals = NULL;

    /* Reset all blocks */
    ArenaBlock* b = a->blocks;
    while (b) {
        b->used = 0;
        b = b->next;
    }
    a->current = a->blocks;
}

`)
}
