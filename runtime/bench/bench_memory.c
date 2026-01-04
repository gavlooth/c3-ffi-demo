#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include "../include/omni.h"
#include "../src/memory/component.h"
#include "../src/memory/arena.h"

/* Wrappers for internal APIs */
SymObj* sym_alloc(Obj* data);
Obj* sym_get_data(SymObj* obj);

#define ITERATIONS 100
#define NODE_COUNT 1000
#define ACCESS_COUNT 100

double get_time_sec() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* 1. Pure ASAP: Deep Tree (Recursive Free) */
void bench_asap_tree() {
    double start = get_time_sec();
    for (int i = 0; i < ITERATIONS; i++) {
        /* Build a binary tree */
        Obj* root = mk_int(0);
        for (int j = 0; j < NODE_COUNT; j++) {
            /* Allocates pairs - ASAP frees recursively */
            root = mk_pair(mk_int(j), root); 
        }
        /* Single recursive free call at scope exit */
        free_tree(root); 
    }
    printf("Pure ASAP (Tree):       %.4f sec\n", get_time_sec() - start);
}

/* 2. Standard RC: Shared DAG */
void bench_rc_dag() {
    double start = get_time_sec();
    for (int i = 0; i < ITERATIONS; i++) {
        Obj* shared = mk_int(42);
        inc_ref(shared); /* Simulate sharing */
        
        Obj* root = NULL;
        for (int j = 0; j < NODE_COUNT; j++) {
            /* Each node points to shared object */
            inc_ref(shared);
            root = mk_pair(shared, root);
        }
        
        /* Cleanup */
        dec_ref(root);   /* Cascades */
        dec_ref(shared); /* Final release */
    }
    printf("Standard RC (DAG):      %.4f sec\n", get_time_sec() - start);
}

/* 3. Arena: Local Cycles */
void bench_arena_cycle() {
    double start = get_time_sec();
    for (int i = 0; i < ITERATIONS; i++) {
        Arena* a = arena_create(4096);
        Obj** nodes = malloc(NODE_COUNT * sizeof(Obj*));
        
        /* Fast bump allocation */
        for (int j = 0; j < NODE_COUNT; j++) {
            nodes[j] = (Obj*)arena_alloc(a, sizeof(Obj));
            if (nodes[j]) {
                nodes[j]->tag = TAG_PAIR;
                nodes[j]->is_pair = 1;
            }
        }
        
        /* Create cycles */
        for (int j = 0; j < NODE_COUNT; j++) {
            if (nodes[j]) {
                nodes[j]->a = nodes[(j + 1) % NODE_COUNT]; /* Cycle */
            }
        }
        
        /* O(1) Bulk Free */
        arena_destroy(a);
        free(nodes);
    }
    printf("Arena (Local Cycle):    %.4f sec\n", get_time_sec() - start);
}

/* 4. Component: Escaping Cycles */
void bench_component_cycle() {
    double start = get_time_sec();
    for (int i = 0; i < ITERATIONS; i++) {
        SymComponent* c = sym_component_new();
        sym_acquire_handle(c);
        
        SymObj** nodes = malloc(NODE_COUNT * sizeof(SymObj*));
        for (int j = 0; j < NODE_COUNT; j++) {
            nodes[j] = sym_alloc(mk_int(j));
            sym_component_add_member(c, nodes[j]);
        }
        
        /* Zero-Cost Access */
        {
            SymTetherToken t = sym_tether_begin(c);
            volatile long sum = 0;
            for (int k = 0; k < ACCESS_COUNT; k++) {
                for (int j = 0; j < NODE_COUNT; j++) {
                    sum += obj_to_int(sym_get_data(nodes[j]));
                }
            }
            sym_tether_end(t);
        }
        
        sym_release_handle(c);
        free(nodes);
    }
    printf("Component (Escaping):   %.4f sec\n", get_time_sec() - start);
}

int main() {
    printf("=== Memory Strategy Benchmarks ===\n");
    printf("Iterations: %d, Nodes: %d\n\n", ITERATIONS, NODE_COUNT);
    
    bench_asap_tree();
    bench_rc_dag();
    bench_arena_cycle();
    bench_component_cycle();
    
    return 0;
}
