/* test_sym_concurrency.c - Symmetric RC thread-safety tests */
#include "test_framework.h"

static Obj* sym_cycle_producer(Obj** caps, Obj** args, int nargs) {
    (void)caps; (void)args; (void)nargs;
    
    for (int i = 0; i < 100; i++) {
        sym_enter_scope();
        
        /* Create a cycle */
        Obj* a_data = mk_int(i);
        Obj* b_data = mk_int(i + 1);
        
        SymObj* sa = sym_alloc(a_data);
        SymObj* sb = sym_alloc(b_data);
        
        sym_link(sa, sb);
        sym_link(sb, sa);
        
        sym_exit_scope();
        /* sa and sb should be freed here along with a_data and b_data */
    }
    
    return mk_int(0);
}

void test_symmetric_rc_concurrency(void) {
    Obj* closure = mk_closure(sym_cycle_producer, NULL, NULL, 0, 0);
    Obj* threads[10];
    
    for (int i = 0; i < 10; i++) {
        threads[i] = spawn_thread(closure);
    }
    
    for (int i = 0; i < 10; i++) {
        Obj* res = thread_join(threads[i]);
        ASSERT_NOT_NULL(res);
        dec_ref(res);
        dec_ref(threads[i]);
    }
    
    dec_ref(closure);
    PASS();
}

void run_sym_concurrency_tests(void) {
    TEST_SUITE("Symmetric RC Concurrency");
    RUN_TEST(test_symmetric_rc_concurrency);
}
