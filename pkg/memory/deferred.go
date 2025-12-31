package memory

import (
	"fmt"
	"io"
)

// DeferredGenerator generates deferred reference counting code
// Used as fallback for mutable cyclic structures
type DeferredGenerator struct {
	w io.Writer
}

// NewDeferredGenerator creates a new deferred generator
func NewDeferredGenerator(w io.Writer) *DeferredGenerator {
	return &DeferredGenerator{w: w}
}

func (g *DeferredGenerator) emit(format string, args ...interface{}) {
	fmt.Fprintf(g.w, format, args...)
}

// GenerateDeferredRuntime generates the deferred RC runtime
func (g *DeferredGenerator) GenerateDeferredRuntime() {
	g.emit(`/* Deferred Reference Counting */
/* For mutable cyclic structures - bounded processing per safe point */

typedef struct DeferredDec {
    Obj* obj;
    int count;
    struct DeferredDec* next;
} DeferredDec;

typedef struct DeferredContext {
    DeferredDec* pending;
    int pending_count;
    int batch_size;     /* Max decrements per safe point */
    int total_deferred;
} DeferredContext;

DeferredContext DEFERRED_CTX = {NULL, 0, 32, 0};

/* O(1) deferral */
void defer_decrement(Obj* obj) {
    if (!obj) return;
    DEFERRED_CTX.total_deferred++;

    /* Check if already in pending list */
    DeferredDec* d = DEFERRED_CTX.pending;
    while (d) {
        if (d->obj == obj) {
            d->count++;
            return;
        }
        d = d->next;
    }

    /* Add new entry */
    DeferredDec* entry = malloc(sizeof(DeferredDec));
    if (!entry) {
        /* Fallback: immediate decrement */
        dec_ref(obj);
        return;
    }
    entry->obj = obj;
    entry->count = 1;
    entry->next = DEFERRED_CTX.pending;
    DEFERRED_CTX.pending = entry;
    DEFERRED_CTX.pending_count++;
}

/* Process up to batch_size decrements */
void process_deferred(void) {
    int processed = 0;
    while (DEFERRED_CTX.pending && processed < DEFERRED_CTX.batch_size) {
        DeferredDec* d = DEFERRED_CTX.pending;
        DEFERRED_CTX.pending = d->next;
        DEFERRED_CTX.pending_count--;

        /* Apply decrements */
        while (d->count > 0) {
            dec_ref(d->obj);
            d->count--;
            processed++;
            if (processed >= DEFERRED_CTX.batch_size) break;
        }

        if (d->count > 0) {
            /* Put back for next round */
            d->next = DEFERRED_CTX.pending;
            DEFERRED_CTX.pending = d;
            DEFERRED_CTX.pending_count++;
        } else {
            free(d);
        }
    }
}

/* Check if we should process deferred decrements */
int should_process_deferred(void) {
    return DEFERRED_CTX.pending_count > DEFERRED_CTX.batch_size * 2;
}

/* Flush all pending decrements */
void flush_deferred(void) {
    while (DEFERRED_CTX.pending) {
        DeferredDec* d = DEFERRED_CTX.pending;
        DEFERRED_CTX.pending = d->next;

        while (d->count > 0) {
            dec_ref(d->obj);
            d->count--;
        }
        free(d);
    }
    DEFERRED_CTX.pending_count = 0;
}

/* Safe point: check and maybe process deferred */
void safe_point(void) {
    if (should_process_deferred()) {
        process_deferred();
    }
}

`)
}
