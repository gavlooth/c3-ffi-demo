/*
 * bench_complex.c - Complex Real-World RC-G Workloads
 *
 * Benchmarks simulating real-world usage patterns.
 */

#include "rcg_bench_framework.h"


/* ========== Quicksort (List Operations) ========== */

static Obj* quicksort_region(Region* r, Obj* list) {
    if (!list || list->tag == TAG_INT) {
        return list;  /* Empty or single element */
    }

    /* Partition */
    Obj* pivot = list->a;
    Obj* current = list->b;
    Obj* less = NULL;
    Obj* greater = NULL;
    Obj** less_tail = &less;
    Obj** greater_tail = &greater;

    while (current && current->tag == TAG_PAIR) {
        Obj* val = current->a;
        if (obj_to_int(val) < obj_to_int(pivot)) {
            *less_tail = mk_cell_region(r, val, NULL);
            less_tail = &((*less_tail)->b);
        } else {
            *greater_tail = mk_cell_region(r, val, NULL);
            greater_tail = &((*greater_tail)->b);
        }
        current = current->b;
    }

    /* Recursively sort */
    Obj* sorted_less = quicksort_region(r, less);
    Obj* sorted_greater = quicksort_region(r, greater);

    /* Combine: sorted_less ++ pivot ++ sorted_greater */
    Obj* result = mk_cell_region(r, pivot, sorted_greater);
    Obj* combined = NULL;
    Obj** tail = &combined;

    current = sorted_less;
    while (current) {
        *tail = mk_cell_region(r, current->a, NULL);
        tail = &((*tail)->b);
        current = current->b;
    }
    *tail = result;

    return combined;
}

void bench_complex_quicksort(void) {
    const int size = g_config.size_small;
    BenchResult result = bench_start("complex_quicksort", size);

    Region* r = region_create();
    Obj* list = build_list_region(r, size);

    Obj* sorted = quicksort_region(r, list);
    (void)sorted;

    region_exit(r);
    bench_end(&result, size);
    bench_report(&result);
}

/* ========== Graph Traversal (BFS) ========== */

typedef struct GraphNode {
    Obj* value;
    struct GraphNode** neighbors;
    int neighbor_count;
} GraphNode;

static GraphNode* create_graph_region(Region* r, int size) {
    GraphNode* nodes = calloc(size, sizeof(GraphNode));

    for (int i = 0; i < size; i++) {
        nodes[i].value = mk_int_region(r, i);
        nodes[i].neighbor_count = 0;
        nodes[i].neighbors = NULL;
    }

    /* Create random edges */
    for (int i = 0; i < size; i++) {
        int edges = 2 + (i % 5);  /* 2-6 edges per node */
        nodes[i].neighbors = malloc(edges * sizeof(GraphNode*));

        for (int e = 0; e < edges; e++) {
            int target = (i + e * 7) % size;
            nodes[i].neighbors[e] = &nodes[target];
            nodes[i].neighbor_count++;
        }
    }

    return nodes;
}

static void bfs_graph(Region* r, GraphNode* start, int size) {
    GraphNode** queue = calloc(size, sizeof(GraphNode*));
    int* visited = calloc(size, sizeof(int));
    int head = 0, tail = 0;

    queue[tail++] = start;
    visited[obj_to_int(start->value)] = 1;

    while (head < tail) {
        GraphNode* current = queue[head++];

        /* Process node - allocate some result */
        mk_int_region(r, obj_to_int(current->value));

        for (int i = 0; i < current->neighbor_count; i++) {
            GraphNode* neighbor = current->neighbors[i];
            int idx = obj_to_int(neighbor->value);
            if (!visited[idx]) {
                visited[idx] = 1;
                queue[tail++] = neighbor;
            }
        }
    }

    free(queue);
    free(visited);
}

void bench_complex_graph_bfs(void) {
    const int size = 1000;
    BenchResult result = bench_start("complex_graph_bfs", size);

    Region* graph_r = region_create();
    GraphNode* graph = create_graph_region(graph_r, size);

    Region* result_r = region_create();
    bfs_graph(result_r, &graph[0], size);

    /* Cleanup graph */
    for (int i = 0; i < size; i++) {
        free(graph[i].neighbors);
    }
    free(graph);

    region_exit(result_r);
    region_exit(graph_r);
    bench_end(&result, size);
    bench_report(&result);
}

/* ========== Memoization Cache ========== */

typedef struct {
    int key;
    Obj* value;
    Region* region;
} CacheEntry;

static CacheEntry* cache = NULL;
static int cache_size = 0;
static int cache_capacity = 0;

static Obj* fib_memoized(int n) {
    /* Check cache */
    for (int i = 0; i < cache_size; i++) {
        if (cache[i].key == n) {
            return cache[i].value;
        }
    }

    /* Compute */
    Obj* result;
    if (n <= 1) {
        result = mk_int(n);
    } else {
        Obj* f1 = fib_memoized(n - 1);
        Obj* f2 = fib_memoized(n - 2);
        result = prim_add(f1, f2);
    }

    /* Store in cache - create new region for this entry */
    if (cache_size >= cache_capacity) {
        cache_capacity = cache_capacity == 0 ? 100 : cache_capacity * 2;
        cache = realloc(cache, cache_capacity * sizeof(CacheEntry));
    }

    Region* r = region_create();
    Obj* cached_value = mk_int_region(r, obj_to_int(result));
    cache[cache_size].key = n;
    cache[cache_size].value = cached_value;
    cache[cache_size].region = r;
    cache_size++;

    return result;
}

void bench_complex_memoization(void) {
    const int n = 30;
    BenchResult result = bench_start("complex_memoization", n);

    cache = NULL;
    cache_size = 0;
    cache_capacity = 0;

    Obj* fib = fib_memoized(n);
    (void)fib;

    /* Cleanup cache */
    for (int i = 0; i < cache_size; i++) {
        region_exit(cache[i].region);
    }
    free(cache);

    bench_end(&result, n);
    bench_report(&result);
}

/* ========== Pipeline Processing ========== */

typedef struct {
    Obj* value;
    int stage;
} PipelineItem;

static void pipeline_stage(Region* r, Obj* input, int stage) {
    /* Simulate processing at each stage */
    for (int i = 0; i < 10; i++) {
        mk_int_region(r, stage * 1000 + obj_to_int(input));
    }
}

void bench_complex_pipeline(void) {
    const int items = g_config.size_small;
    const int stages = 5;
    BenchResult result = bench_start("complex_pipeline", items * stages);

    Region* r = region_create();

    /* Create input items */
    for (int i = 0; i < items; i++) {
        Obj* item = mk_int_region(r, i);

        /* Process through stages */
        for (int stage = 0; stage < stages; stage++) {
            pipeline_stage(r, item, stage);
        }
    }

    region_exit(r);
    bench_end(&result, items * stages);
    bench_report(&result);
}

/* ========== State Machine ========== */

typedef enum {
    STATE_IDLE,
    STATE_RUNNING,
    STATE_PAUSED,
    STATE_STOPPED
} State;

typedef struct {
    State current_state;
    int counter;
    Region* region;
} StateMachine;

static StateMachine* sm_create(void) {
    StateMachine* sm = malloc(sizeof(StateMachine));
    sm->current_state = STATE_IDLE;
    sm->counter = 0;
    sm->region = region_create();
    return sm;
}

static void sm_transition(StateMachine* sm, State new_state) {
    sm->current_state = new_state;
    mk_int_region(sm->region, new_state);
    sm->counter++;
}

static void sm_destroy(StateMachine* sm) {
    region_exit(sm->region);
    free(sm);
}

void bench_complex_state_machine(void) {
    const int transitions = g_config.size_medium;
    BenchResult result = bench_start("complex_state_machine", transitions);

    StateMachine* sm = sm_create();

    for (int i = 0; i < transitions; i++) {
        switch (sm->current_state) {
            case STATE_IDLE:
                sm_transition(sm, STATE_RUNNING);
                break;
            case STATE_RUNNING:
                sm_transition(sm, (i % 3 == 0) ? STATE_PAUSED : STATE_STOPPED);
                break;
            case STATE_PAUSED:
                sm_transition(sm, STATE_RUNNING);
                break;
            case STATE_STOPPED:
                sm_transition(sm, STATE_IDLE);
                break;
        }
    }

    sm_destroy(sm);
    bench_end(&result, transitions);
    bench_report(&result);
}

/* ========== Nested Data Processing ========== */

void bench_complex_nested_processing(void) {
    const int outer = 100;
    const int inner = 100;
    BenchResult result = bench_start("complex_nested_processing", outer * inner);

    Region* r = region_create();

    /* Create nested structure */
    for (int i = 0; i < outer; i++) {
        Obj* outer_obj = mk_int_region(r, i);

        for (int j = 0; j < inner; j++) {
            Obj* inner_obj = mk_cell_region(r,
                mk_int_region(r, j),
                mk_cell_region(r, outer_obj, NULL)
            );
            (void)inner_obj;
        }
    }

    region_exit(r);
    bench_end(&result, outer * inner);
    bench_report(&result);
}

/* ========== Run All Complex Workload Benchmarks ========== */

int main_complex(int argc, char** argv) {
    bench_parse_args(argc, argv);
    csv_header();

    printf("=== RC-G Complex Workload Benchmarks ===\n");
    printf("Iterations: %d\n\n", g_config.iterations);

    BENCH_SECTION("Algorithms");
    RUN_BENCH(bench_complex_quicksort);
    RUN_BENCH(bench_complex_graph_bfs);

    BENCH_SECTION("Caching");
    RUN_BENCH(bench_complex_memoization);

    BENCH_SECTION("Data Flow");
    RUN_BENCH(bench_complex_pipeline);

    BENCH_SECTION("State Management");
    RUN_BENCH(bench_complex_state_machine);

    BENCH_SECTION("Nested Data");
    RUN_BENCH(bench_complex_nested_processing);

    printf("\n=== All complex workload benchmarks complete ===\n");
    return 0;
}
