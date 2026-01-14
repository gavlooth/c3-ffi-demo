/*
 * test_effect_primitives.c - Tests for Effect System Primitives
 *
 * Tests the Lisp-level primitives added to runtime.c:
 * - prim_perform, prim_raise, prim_resume
 * - prim_yield, prim_ask, prim_emit
 * - prim_handler_push, prim_handler_pop
 * - prim_effect_type_register, mk_resumption_obj
 *
 * These primitives bridge the effect system to OmniLisp code.
 */

/* Forward declarations for effect primitives from runtime.c */
extern Obj* prim_effect_init(void);
extern Obj* prim_perform(Obj* effect_name, Obj* payload);
extern Obj* prim_raise(Obj* message);
extern Obj* prim_resume(Obj* resumption_obj, Obj* value);
extern Obj* prim_yield(Obj* value);
extern Obj* prim_ask(Obj* prompt);
extern Obj* prim_emit(Obj* value);
extern Obj* prim_effect_type_register(Obj* name);
extern Obj* prim_perform_typed(Obj* effect_type_obj, Obj* payload);
extern Obj* prim_handler_push(Obj* clauses, Obj* return_clause, Obj* env);
extern Obj* prim_handler_pop(void);
extern Obj* mk_resumption_obj(Resumption* r);

/* ============================================================
 * Effect Primitives Tests
 * ============================================================ */

static void test_prim_effect_init(void) {
	/* Should initialize effect system and return nothing */
	Obj* result = prim_effect_init();
	ASSERT(result != NULL);
	ASSERT(IS_BOXED(result) && result->tag == TAG_NOTHING);

	/* Safe to call multiple times */
	result = prim_effect_init();
	ASSERT(result != NULL);
}

static void test_prim_effect_type_register(void) {
	prim_effect_init();

	/* Register a new effect type with symbol name */
	Obj* sym_name = mk_sym("TestEffect");
	Obj* result = prim_effect_type_register(sym_name);

	ASSERT(result != NULL);
	ASSERT(IS_BOXED(result));
	ASSERT(result->tag == TAG_EFFECT_TYPE);
	ASSERT(result->ptr != NULL);

	/* The effect type should be findable by name */
	EffectType* found = effect_type_find("TestEffect");
	ASSERT(found != NULL);
	ASSERT(found == (EffectType*)result->ptr);
}

static void test_prim_effect_type_register_string(void) {
	prim_effect_init();

	/* Register a new effect type with string name */
	Obj* str_name = mk_string("StringEffect");
	Obj* result = prim_effect_type_register(str_name);

	ASSERT(result != NULL);
	ASSERT(result->tag == TAG_EFFECT_TYPE);

	EffectType* found = effect_type_find("StringEffect");
	ASSERT(found != NULL);
}

static void test_prim_perform_without_handler(void) {
	prim_effect_init();

	/* Performing without a handler should return NULL */
	Obj* effect_name = mk_sym("Ask");
	Obj* payload = mk_int(42);

	/* This should print an error and return NULL */
	Obj* result = prim_perform(effect_name, payload);
	ASSERT(result == NULL);
}

static void test_prim_raise_without_handler(void) {
	prim_effect_init();

	/* Raising without a handler should return NULL */
	Obj* message = mk_string("Test error");
	Obj* result = prim_raise(message);
	ASSERT(result == NULL);
}

static void test_prim_resume_invalid(void) {
	prim_effect_init();

	/* Resume with NULL should return NULL */
	Obj* result = prim_resume(NULL, mk_int(1));
	ASSERT(result == NULL);

	/* Resume with non-resumption should return NULL */
	Obj* not_resumption = mk_int(42);
	result = prim_resume(not_resumption, mk_int(1));
	ASSERT(result == NULL);
}

static void test_mk_resumption_obj(void) {
	prim_effect_init();

	/* Create a resumption (NULL continuation for test) */
	Resumption* r = resumption_create(NULL, EFFECT_ASK, NULL);
	ASSERT(r != NULL);

	/* Wrap in Obj */
	Obj* obj = mk_resumption_obj(r);
	ASSERT(obj != NULL);
	ASSERT(IS_BOXED(obj));
	ASSERT(obj->tag == TAG_RESUMPTION);
	ASSERT(obj->ptr != NULL);

	/* mk_resumption_obj increments refcount */
	ASSERT(r->refcount == 2);

	/* Cleanup - dec original ref */
	resumption_dec_ref(r);
}

static void test_prim_handler_push_pop(void) {
	prim_effect_init();

	/* Initially no handler */
	ASSERT(handler_current() == NULL);

	/* Push a handler with empty clauses */
	Obj* result = prim_handler_push(NULL, NULL, NULL);
	ASSERT(result != NULL);
	ASSERT(IS_BOXED(result));
	ASSERT(result->tag == TAG_HANDLER);

	/* Handler should be current */
	ASSERT(handler_current() != NULL);

	/* Pop the handler */
	Obj* pop_result = prim_handler_pop();
	ASSERT(pop_result != NULL);
	ASSERT(IS_BOXED(pop_result) && pop_result->tag == TAG_NOTHING);

	/* Handler stack should be empty */
	ASSERT(handler_current() == NULL);
}

static void test_prim_yield_without_handler(void) {
	prim_effect_init();

	/* Yield without handler should return NULL */
	Obj* result = prim_yield(mk_int(42));
	ASSERT(result == NULL);
}

static void test_prim_ask_without_handler(void) {
	prim_effect_init();

	/* Ask without handler should return NULL */
	Obj* result = prim_ask(NULL);
	ASSERT(result == NULL);
}

static void test_prim_emit_without_handler(void) {
	prim_effect_init();

	/* Emit without handler should return NULL */
	Obj* result = prim_emit(mk_string("log message"));
	ASSERT(result == NULL);
}

static void test_prim_perform_named_effect(void) {
	prim_effect_init();

	/* Register a custom effect */
	Obj* name = mk_sym("CustomEffect");
	Obj* type_obj = prim_effect_type_register(name);
	ASSERT(type_obj != NULL);

	/* Perform should fail without handler but not crash */
	Obj* result = prim_perform(mk_sym("CustomEffect"), mk_int(100));
	ASSERT(result == NULL);
}

static void test_prim_effect_invalid_name(void) {
	prim_effect_init();

	/* Perform with invalid name (integer) should fail gracefully */
	Obj* result = prim_perform(mk_int(42), NULL);
	ASSERT(result == NULL);

	/* NULL name should fail */
	result = prim_perform(NULL, NULL);
	ASSERT(result == NULL);
}

static void test_effect_type_register_invalid_name(void) {
	prim_effect_init();

	/* Register with invalid name should fail */
	Obj* result = prim_effect_type_register(mk_int(42));
	ASSERT(result == NULL);

	result = prim_effect_type_register(NULL);
	ASSERT(result == NULL);
}

/* ============================================================
 * Test Runner
 * ============================================================ */

static void run_effect_primitives_tests(void) {
	TEST_SUITE("effect_primitives");

	RUN_TEST(test_prim_effect_init);
	RUN_TEST(test_prim_effect_type_register);
	RUN_TEST(test_prim_effect_type_register_string);
	RUN_TEST(test_prim_perform_without_handler);
	RUN_TEST(test_prim_raise_without_handler);
	RUN_TEST(test_prim_resume_invalid);
	RUN_TEST(test_mk_resumption_obj);
	RUN_TEST(test_prim_handler_push_pop);
	RUN_TEST(test_prim_yield_without_handler);
	RUN_TEST(test_prim_ask_without_handler);
	RUN_TEST(test_prim_emit_without_handler);
	RUN_TEST(test_prim_perform_named_effect);
	RUN_TEST(test_prim_effect_invalid_name);
	RUN_TEST(test_effect_type_register_invalid_name);
}
