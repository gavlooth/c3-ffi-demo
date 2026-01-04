#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include "../include/omni.h"
#include "../src/memory/component.h"

/* Automatic API Wrappers (from runtime.c) */
SymObj* sym_alloc(Obj* data);
Obj* sym_get_data(SymObj* obj);

#define ITERATIONS 100
#define GRAPH_SIZE 1000
#define ACCESS_COUNT 100

double get_time_sec() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* Baseline: Standard RC (simulated cyclic overhead) */
void bench_rc_baseline() {
    double start = get_time_sec();
    
    for (int i = 0; i < ITERATIONS; i++) {
        Obj** nodes = malloc(GRAPH_SIZE * sizeof(Obj*));
        
        /* Allocation */
        for (int j = 0; j < GRAPH_SIZE; j++) {
            nodes[j] = mk_int(j);
            inc_ref(nodes[j]); /* Simulate graph link */
        }
        
        /* Traversal (Simulate RC overhead) */
        long sum = 0;
        for (int k = 0; k < ACCESS_COUNT; k++) {
            for (int j = 0; j < GRAPH_SIZE; j++) {
                inc_ref(nodes[j]); /* Access overhead */
                sum += obj_to_int(nodes[j]);
                dec_ref(nodes[j]);
            }
        }
        
        /* Cleanup */
        for (int j = 0; j < GRAPH_SIZE; j++) {
            dec_ref(nodes[j]); /* Link */
            dec_ref(nodes[j]); /* Root */
        }
        free(nodes);
    }
    
    printf("RC Baseline: %.4f seconds\n", get_time_sec() - start);
}

/* Component Tethering */
void bench_component() {
    double start = get_time_sec();
    
    for (int i = 0; i < ITERATIONS; i++) {
        SymComponent* c = sym_component_new();
        sym_acquire_handle(c);
        
        /* Allocation (Slab + Component) */
        SymObj** nodes = malloc(GRAPH_SIZE * sizeof(SymObj*));
        for (int j = 0; j < GRAPH_SIZE; j++) {
            nodes[j] = sym_alloc(mk_int(j));
            sym_component_add_member(c, nodes[j]);
        }
        
        /* Traversal (Zero-Cost Tether) */
        long sum = 0;
        {
            SymTetherToken t = sym_tether_begin(c);
            for (int k = 0; k < ACCESS_COUNT; k++) {
                for (int j = 0; j < GRAPH_SIZE; j++) {
                    /* No RC overhead here! */
                    sum += obj_to_int(sym_get_data(nodes[j]));
                }
            }
            sym_tether_end(t);
        }
        
        /* Cleanup (Bulk Dismantle) */
        sym_release_handle(c);
        free(nodes);
    }
    
    printf("Component Tethering: %.4f seconds\n", get_time_sec() - start);
}

int main() {
    printf("Benchmarking Component Tethering vs RC (%d iterations, %d nodes, %d accesses/iter)...\n", 
           ITERATIONS, GRAPH_SIZE, ACCESS_COUNT);
    
    bench_rc_baseline();
    bench_component();
    
    return 0;
}
