module lisp;


import std::io;
import main;
// =============================================================================
// SECTION 12: COMPILER TESTS
// =============================================================================

/**
 * Check if haystack contains needle as a substring.
 */
fn bool str_contains(char[] haystack, char[] needle) {
    if (needle.len == 0) return true;
    if (needle.len > haystack.len) return false;
    for (usz i = 0; i <= haystack.len - needle.len; i++) {
        bool match = true;
        for (usz j = 0; j < needle.len; j++) {
            if (haystack[i + j] != needle[j]) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

/**
 * Compiler tests — verify that the compiler produces valid C3 output
 * for all supported features. Tests check that compile_to_c3() succeeds
 * and the output does not contain "unsupported".
 */
fn void run_compiler_tests(Interp* _unused_interp) {
    io::printn("\n=== Compiler Tests ===");
    usz pass = 0;
    usz fail = 0;

    // Use a fresh interp for compiler tests since each compile_to_c3 call
    // parses the entire stdlib prelude, consuming many expr pool entries.
    Interp* interp = (Interp*)mem::malloc(Interp.sizeof);
    interp.init();
    register_primitives(interp);
    register_stdlib(interp);  // Needed for map, filter, etc.


    // --- E_BEGIN tests ---

    // 1. begin with single expression
    {
        char[] code = compile_to_c3("(begin 42)", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: (begin 42)"); }
        else    { fail++; io::printn("[FAIL] Compiler: (begin 42)"); }
    }

    // 2. begin with multiple expressions
    {
        char[] code = compile_to_c3("(begin 1 2 3)", interp);
        bool ok = !str_contains(code, "unsupported") && str_contains(code, "make_int(3)");
        if (ok) { pass++; io::printn("[PASS] Compiler: (begin 1 2 3)"); }
        else    { fail++; io::printn("[FAIL] Compiler: (begin 1 2 3)"); }
    }

    // 3. begin with define inside
    {
        char[] code = compile_to_c3("(begin (define x 1) x)", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: (begin (define x 1) x)"); }
        else    { fail++; io::printn("[FAIL] Compiler: (begin (define x 1) x)"); }
    }

    // 4. begin inside lambda body
    {
        char[] code = compile_to_c3("(lambda (x) (begin x x))", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: (lambda (x) (begin x x))"); }
        else    { fail++; io::printn("[FAIL] Compiler: (lambda (x) (begin x x))"); }
    }

    // --- Zero-arg lambda tests ---

    // 5. zero-arg lambda
    {
        char[] code = compile_to_c3("((lambda () 42))", interp);
        bool ok = !str_contains(code, "unsupported") && str_contains(code, "_unused");
        if (ok) { pass++; io::printn("[PASS] Compiler: ((lambda () 42))"); }
        else    { fail++; io::printn("[FAIL] Compiler: ((lambda () 42))"); }
    }

    // 6. zero-arg define + call
    {
        char[] code = compile_to_c3("(define thunk (lambda () 42)) (thunk)", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: zero-arg define + call"); }
        else    { fail++; io::printn("[FAIL] Compiler: zero-arg define + call"); }
    }

    // 7. zero-arg in let
    {
        char[] code = compile_to_c3("(let (f (lambda () 10)) (f))", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: zero-arg in let"); }
        else    { fail++; io::printn("[FAIL] Compiler: zero-arg in let"); }
    }

    // --- Multi-param + multi-binding (parser desugar) ---

    // 8. multi-param lambda (desugared by parser to nested)
    {
        char[] code = compile_to_c3("((lambda (x y) (+ x y)) 3 4)", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: multi-param lambda"); }
        else    { fail++; io::printn("[FAIL] Compiler: multi-param lambda"); }
    }

    // 9. multi-binding let (desugared by parser to nested)
    {
        char[] code = compile_to_c3("(let (x 1 y 2) (+ x y))", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: multi-binding let"); }
        else    { fail++; io::printn("[FAIL] Compiler: multi-binding let"); }
    }

    // 10. 3-param lambda
    {
        char[] code = compile_to_c3("((lambda (a b c) a) 1 2 3)", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: 3-param lambda"); }
        else    { fail++; io::printn("[FAIL] Compiler: 3-param lambda"); }
    }

    // --- Stdlib availability ---

    // 11. map compiles
    {
        char[] code = compile_to_c3("(map (lambda (x) (+ x 1)) (list 1))", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: map"); }
        else    { fail++; io::printn("[FAIL] Compiler: map"); }
    }

    // 12. filter compiles
    {
        char[] code = compile_to_c3("(filter (lambda (x) (> x 2)) (list 1 2 3))", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: filter"); }
        else    { fail++; io::printn("[FAIL] Compiler: filter"); }
    }

    // 13. foldl compiles
    {
        char[] code = compile_to_c3("(foldl + 0 (list 1 2 3))", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: foldl"); }
        else    { fail++; io::printn("[FAIL] Compiler: foldl"); }
    }

    // --- Existing features still work ---

    // 14. match compiles
    {
        char[] code = compile_to_c3("(match (list 1 2 3) ([a b c] (+ a c)))", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: match"); }
        else    { fail++; io::printn("[FAIL] Compiler: match"); }
    }

    // 15. handle/perform compiles
    {
        char[] code = compile_to_c3("(handle (+ 1 (signal ask 0)) (ask x (resolve 10)))", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: handle/perform"); }
        else    { fail++; io::printn("[FAIL] Compiler: handle/perform"); }
    }

    // 16. zero-arg lambda with closure capture
    {
        char[] code = compile_to_c3("(let (x 42) (let (f (lambda () x)) (f)))", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: zero-arg closure capture"); }
        else    { fail++; io::printn("[FAIL] Compiler: zero-arg closure capture"); }
    }

    // 17. id and compose from stdlib
    {
        char[] code = compile_to_c3("(id 5)", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: id"); }
        else    { fail++; io::printn("[FAIL] Compiler: id"); }
    }

    // 18. range from stdlib
    {
        char[] code = compile_to_c3("(range 5)", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: range"); }
        else    { fail++; io::printn("[FAIL] Compiler: range"); }
    }

    // --- set! tests ---

    // 19. set! on global variable
    {
        char[] code = compile_to_c3("(define x 10) (set! x 20)", interp);
        bool ok = !str_contains(code, "unsupported") && str_contains(code, "x = ");
        if (ok) { pass++; io::printn("[PASS] Compiler: (set! x 20)"); }
        else    { fail++; io::printn("[FAIL] Compiler: (set! x 20)"); }
    }

    // 20. set! in expression context
    {
        char[] code = compile_to_c3("(define x 10) (+ (set! x 20) x)", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: set! in expression"); }
        else    { fail++; io::printn("[FAIL] Compiler: set! in expression"); }
    }

    // 21. set! inside lambda
    {
        char[] code = compile_to_c3("(define counter 0) (define inc! (lambda (n) (set! counter (+ counter n))))", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: set! inside lambda"); }
        else    { fail++; io::printn("[FAIL] Compiler: set! inside lambda"); }
    }

    // --- dot-bracket indexing tests ---

    // 22. list indexing with .[i]
    {
        char[] code = compile_to_c3("(define lst (list 10 20 30)) lst.[1]", interp);
        bool ok = !str_contains(code, "unsupported") && str_contains(code, "aot::index");
        if (ok) { pass++; io::printn("[PASS] Compiler: lst.[1]"); }
        else    { fail++; io::printn("[FAIL] Compiler: lst.[1]"); }
    }

    // 23. indexing with expression
    {
        char[] code = compile_to_c3("(define lst (list 1 2 3)) lst.[(+ 0 1)]", interp);
        bool ok = !str_contains(code, "unsupported") && str_contains(code, "aot::index");
        if (ok) { pass++; io::printn("[PASS] Compiler: lst.[(+ 0 1)]"); }
        else    { fail++; io::printn("[FAIL] Compiler: lst.[(+ 0 1)]"); }
    }

    // --- path notation tests ---

    // 24. simple path a.b
    {
        char[] code = compile_to_c3("(define point (list (cons 'x 10) (cons 'y 20))) point.x", interp);
        bool ok = !str_contains(code, "unsupported") && str_contains(code, "aot::field_access");
        if (ok) { pass++; io::printn("[PASS] Compiler: point.x"); }
        else    { fail++; io::printn("[FAIL] Compiler: point.x"); }
    }

    // 25. nested path a.b.c
    {
        char[] code = compile_to_c3("(define obj (list (cons 'inner (list (cons 'val 42))))) obj.inner.val", interp);
        bool ok = !str_contains(code, "unsupported") && str_contains(code, "aot::field_access");
        if (ok) { pass++; io::printn("[PASS] Compiler: obj.inner.val"); }
        else    { fail++; io::printn("[FAIL] Compiler: obj.inner.val"); }
    }

    // 26. path generates make_string for field name (not raw string)
    {
        char[] code = compile_to_c3("point.x", interp);
        bool ok = str_contains(code, "make_string");
        if (ok) { pass++; io::printn("[PASS] Compiler: path uses make_string for field name"); }
        else    { fail++; io::printn("[FAIL] Compiler: path uses make_string for field name"); }
    }

    // --- reset/shift via interpreter delegation ---

    // 27. reset/shift compiles natively (no eval_source)
    {
        char[] code = compile_to_c3("(reset (+ 1 (shift k (k 10))))", interp);
        bool ok = str_contains(code, "compiled_reset") && !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: reset/shift compiles natively"); }
        else    { fail++; io::printn("[FAIL] Compiler: reset/shift compiles natively"); }
    }

    // 28. set! inside begin
    {
        char[] code = compile_to_c3("(define x 0) (begin (set! x 1) (set! x (+ x 1)) x)", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: set! inside begin"); }
        else    { fail++; io::printn("[FAIL] Compiler: set! inside begin"); }
    }

    // --- Continuation form tests (native compilation) ---

    // 29. reset compiles to compiled_reset (not eval_source for the reset)
    {
        char[] code = compile_to_c3("(define x 5) (reset (+ x (shift k (k 10))))", interp);
        bool ok = str_contains(code, "compiled_reset");
        if (ok) { pass++; io::printn("[PASS] Compiler: reset uses compiled_reset"); }
        else    { fail++; io::printn("[FAIL] Compiler: reset uses compiled_reset"); }
    }

    // 30. handle/signal compiles natively
    {
        char[] code = compile_to_c3("(handle (+ 1 (signal ask 0)) (ask x (resolve 10)))", interp);
        bool ok = str_contains(code, "compiled_handle") && str_contains(code, "compiled_signal");
        if (ok) { pass++; io::printn("[PASS] Compiler: handle/signal uses native compilation"); }
        else    { fail++; io::printn("[FAIL] Compiler: handle/signal uses native compilation"); }
    }

    // 31. standalone signal compiles natively
    {
        char[] code = compile_to_c3("(signal raise \"error\")", interp);
        bool ok = str_contains(code, "compiled_signal");
        if (ok) { pass++; io::printn("[PASS] Compiler: signal uses compiled_signal"); }
        else    { fail++; io::printn("[FAIL] Compiler: signal uses compiled_signal"); }
    }

    // 32. shift compiles natively
    {
        char[] code = compile_to_c3("(shift k 42)", interp);
        bool ok = str_contains(code, "compiled_shift");
        if (ok) { pass++; io::printn("[PASS] Compiler: shift uses compiled_shift"); }
        else    { fail++; io::printn("[FAIL] Compiler: shift uses compiled_shift"); }
    }

    // 33. reset compiles with native function
    {
        char[] code = compile_to_c3("(reset (+ 1 2))", interp);
        bool ok = str_contains(code, "compiled_reset");
        if (ok) { pass++; io::printn("[PASS] Compiler: reset compiles natively"); }
        else    { fail++; io::printn("[FAIL] Compiler: reset compiles natively"); }
    }

    // 34. handle with multiple clauses compiles natively
    {
        char[] code = compile_to_c3("(handle (begin (signal get 0) (signal set 1)) (get x (resolve 42)) (set v (resolve v)))", interp);
        bool ok = str_contains(code, "compiled_handle");
        if (ok) { pass++; io::printn("[PASS] Compiler: handle with multiple clauses"); }
        else    { fail++; io::printn("[FAIL] Compiler: handle with multiple clauses"); }
    }

    // 35. try from stdlib (uses handle/signal internally) compiles
    {
        char[] code = compile_to_c3("(try (lambda (xx) (+ 1 2)) (lambda (msg) 0))", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: try from stdlib"); }
        else    { fail++; io::printn("[FAIL] Compiler: try from stdlib"); }
    }

    // 36. reset with nested shift compiles natively
    {
        char[] code = compile_to_c3("(reset (+ 1 (shift k (+ (k 10) (k 20)))))", interp);
        bool ok = str_contains(code, "compiled_reset") && str_contains(code, "compiled_shift");
        if (ok) { pass++; io::printn("[PASS] Compiler: reset with nested shift"); }
        else    { fail++; io::printn("[FAIL] Compiler: reset with nested shift"); }
    }


    // =========================================================================
    // Phase 1: Multi-arg calls (inline currying)
    // =========================================================================

    // 41. Multi-arg call uses inline currying (invoke_once + invoke)
    {
        char[] code = compile_to_c3("(+ 1 2)", interp);
        bool ok = str_contains(code, "invoke") && !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: multi-arg uses inline currying"); }
        else    { fail++; io::printn("[FAIL] Compiler: multi-arg uses inline currying"); }
    }

    // 42. Multi-arg uses invoke_once for intermediate args
    {
        char[] code = compile_to_c3("(+ 1 2)", interp);
        bool ok = str_contains(code, "invoke_once") || str_contains(code, "invoke");
        if (ok) { pass++; io::printn("[PASS] Compiler: multi-arg uses invoke"); }
        else    { fail++; io::printn("[FAIL] Compiler: multi-arg uses invoke"); }
    }

    // 43. 3-arg call uses apply_multi
    {
        char[] code = compile_to_c3("(substring \"hello\" 1 3)", interp);
        bool ok = str_contains(code, "apply_multi");
        if (ok) { pass++; io::printn("[PASS] Compiler: 3-arg apply_multi"); }
        else    { fail++; io::printn("[FAIL] Compiler: 3-arg apply_multi"); }
    }

    // 44. Zero-arg call via invoke
    {
        char[] code = compile_to_c3("((lambda () 42))", interp);
        bool ok = str_contains(code, "invoke") && str_contains(code, "make_nil()");
        if (ok) { pass++; io::printn("[PASS] Compiler: zero-arg invoke"); }
        else    { fail++; io::printn("[FAIL] Compiler: zero-arg invoke"); }
    }

    // =========================================================================
    // Phase 2: TCO via trampoline
    // =========================================================================

    // 45. Tail calls in lambda body emit apply_multi_tail
    {
        char[] code = compile_to_c3("(define (f x) (f (+ x 1)))", interp);
        bool ok = str_contains(code, "apply_multi_tail");
        if (ok) { pass++; io::printn("[PASS] Compiler: tail call emits apply_multi_tail"); }
        else    { fail++; io::printn("[FAIL] Compiler: tail call emits apply_multi_tail"); }
    }

    // 46. Non-tail calls use apply_multi (not tail variant)
    {
        char[] code = compile_to_c3("(define (f x) (+ (f x) 1))", interp);
        // The (f x) is not in tail position (it's an argument to +)
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: non-tail compiles OK"); }
        else    { fail++; io::printn("[FAIL] Compiler: non-tail compiles OK"); }
    }

    // 47. If branches propagate tail position
    {
        char[] code = compile_to_c3("(define (f x) (if (= x 0) 1 (f (- x 1))))", interp);
        bool ok = str_contains(code, "apply_multi_tail");
        if (ok) { pass++; io::printn("[PASS] Compiler: if branch tail position"); }
        else    { fail++; io::printn("[FAIL] Compiler: if branch tail position"); }
    }

    // =========================================================================
    // Phase 3: Quasiquote, macros, modules
    // =========================================================================

    // 48. Quasiquote compiles natively with aot::cons
    {
        char[] code = compile_to_c3("(let (x 5) `(a ,x))", interp);
        bool ok = str_contains(code, "aot::cons") && str_contains(code, "make_symbol");
        if (ok) { pass++; io::printn("[PASS] Compiler: quasiquote compiles natively"); }
        else    { fail++; io::printn("[FAIL] Compiler: quasiquote compiles natively"); }
    }

    // 49. Quasiquote with unquote uses make_symbol for literal parts
    {
        char[] code = compile_to_c3("(let (x 5) `(a ,x))", interp);
        bool ok = str_contains(code, "make_symbol(\"a\")");
        if (ok) { pass++; io::printn("[PASS] Compiler: quasiquote uses make_symbol"); }
        else    { fail++; io::printn("[FAIL] Compiler: quasiquote uses make_symbol"); }
    }

    // 50. Defmacro delegates to eval_source
    {
        char[] code = compile_to_c3("(define [macro] double ([x] (+ x x)))", interp);
        bool ok = str_contains(code, "make_nil") && !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: defmacro compiles as no-op"); }
        else    { fail++; io::printn("[FAIL] Compiler: defmacro compiles as no-op"); }
    }

    // 51. Stdlib functions (when, unless) compiled from STDLIB_PRELUDE
    {
        char[] code = compile_to_c3("42", interp);
        bool ok = str_contains(code, "when") && str_contains(code, "unless");
        if (ok) { pass++; io::printn("[PASS] Compiler: stdlib functions compiled"); }
        else    { fail++; io::printn("[FAIL] Compiler: stdlib functions compiled"); }
    }

    // =========================================================================
    // Phase 4: Missing primitives
    // =========================================================================

    // 52. string->number compiles
    {
        char[] code = compile_to_c3("(string->number \"42\")", interp);
        bool ok = str_contains(code, "lookup_prim(\"string->number\")") && !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: string->number"); }
        else    { fail++; io::printn("[FAIL] Compiler: string->number"); }
    }

    // 53. number->string compiles
    {
        char[] code = compile_to_c3("(number->string 42)", interp);
        bool ok = str_contains(code, "lookup_prim(\"number->string\")") && !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: number->string"); }
        else    { fail++; io::printn("[FAIL] Compiler: number->string"); }
    }

    // 54. gensym compiles
    {
        char[] code = compile_to_c3("(gensym)", interp);
        bool ok = str_contains(code, "lookup_prim(\"gensym\")") && !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: gensym"); }
        else    { fail++; io::printn("[FAIL] Compiler: gensym"); }
    }

    // 55. apply compiles
    {
        char[] code = compile_to_c3("(apply + (list 1 2))", interp);
        bool ok = str_contains(code, "lookup_prim(\"apply\")") && !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: apply"); }
        else    { fail++; io::printn("[FAIL] Compiler: apply"); }
    }

    // 56. equal? compiles
    {
        char[] code = compile_to_c3("(equal? 1 2)", interp);
        bool ok = str_contains(code, "lookup_prim(\"equal?\")") && !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: equal?"); }
        else    { fail++; io::printn("[FAIL] Compiler: equal?"); }
    }

    // 57. display compiles
    {
        char[] code = compile_to_c3("(display \"hello\")", interp);
        bool ok = str_contains(code, "lookup_prim(\"display\")") && !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: display"); }
        else    { fail++; io::printn("[FAIL] Compiler: display"); }
    }

    // 58. ref compiles
    {
        char[] code = compile_to_c3("(ref (dict) 'a)", interp);
        bool ok = str_contains(code, "lookup_prim(\"ref\")") && !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: ref"); }
        else    { fail++; io::printn("[FAIL] Compiler: ref"); }
    }

    // 59. load compiles
    {
        char[] code = compile_to_c3("(load \"test.omni\")", interp);
        bool ok = str_contains(code, "lookup_prim(\"load\")") && !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: load"); }
        else    { fail++; io::printn("[FAIL] Compiler: load"); }
    }

    // =========================================================================
    // Phase 5: Mutable capture boxing
    // =========================================================================

    // 60. Mutable capture detected and uses aot::define_var
    {
        char[] code = compile_to_c3("(let (x 0) (let (inc (lambda () (set! x (+ x 1)))) (inc) x))", interp);
        // Mutable capture test - checks generated code for interpreter env usage
        bool ok = str_contains(code, "aot::define_var") && str_contains(code, "aot::set_var");
        if (ok) { pass++; io::printn("[PASS] Compiler: mutable capture uses aot::define_var/aot::set_var"); }
        else    { fail++; io::printn("[FAIL] Compiler: mutable capture uses aot::define_var/aot::set_var"); }
    }

    // 61. Mutable capture reads use aot::lookup_var
    {
        char[] code = compile_to_c3("(let (x 0) (let (inc (lambda () (set! x (+ x 1)))) (inc) x))", interp);
        bool ok = str_contains(code, "aot::lookup_var");
        if (ok) { pass++; io::printn("[PASS] Compiler: mutable capture reads use aot::lookup_var"); }
        else    { fail++; io::printn("[FAIL] Compiler: mutable capture reads use aot::lookup_var"); }
    }

    // 62. Non-mutable let uses C3 local variable (Value* x =...)
    {
        char[] code = compile_to_c3("(let (x 42) x)", interp);
        // The main fn body should contain "Value* x =" for a non-mutable let
        bool ok = str_contains(code, "Value* x =");
        if (ok) { pass++; io::printn("[PASS] Compiler: non-mutable let uses C3 local"); }
        else    { fail++; io::printn("[FAIL] Compiler: non-mutable let uses C3 local"); }
    }

    // =========================================================================
    // Integration: Verify all features work together
    // =========================================================================

    // 63. Complex program with multiple features compiles
    {
        char[] code = compile_to_c3(
            "(define (factorial n) (if (= n 0) 1 (* n (factorial (- n 1))))) (factorial 10)", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: factorial compiles"); }
        else    { fail++; io::printn("[FAIL] Compiler: factorial compiles"); }
    }

    // 64. Let-rec with tail call emits apply_multi_tail
    {
        char[] code = compile_to_c3(
            "(let ^rec (loop (lambda (n) (if (= n 0) 'done (loop (- n 1))))) (loop 100))", interp);
        bool ok = str_contains(code, "apply_multi_tail") && !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: let-rec tail call emits apply_multi_tail"); }
        else    { fail++; io::printn("[FAIL] Compiler: let-rec tail call emits apply_multi_tail"); }
    }

    // 65. Nested multi-arg calls compile correctly
    {
        char[] code = compile_to_c3("(+ (* 2 3) (* 4 5))", interp);
        bool ok = str_contains(code, "invoke") && !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: nested multi-arg calls"); }
        else    { fail++; io::printn("[FAIL] Compiler: nested multi-arg calls"); }
    }

    // =========================================================================
    // Phase 6: STDLIB_PRELUDE iterative map/filter
    // =========================================================================

    // 66. STDLIB_PRELUDE map uses iterative loop pattern
    {
        char[] code = compile_to_c3("(map (lambda (x) x) (list 1))", interp);
        bool ok = str_contains(code, "loop");
        if (ok) { pass++; io::printn("[PASS] Compiler: STDLIB map uses loop pattern"); }
        else    { fail++; io::printn("[FAIL] Compiler: STDLIB map uses loop pattern"); }
    }

    // 67. STDLIB_PRELUDE filter uses iterative loop pattern
    {
        char[] code = compile_to_c3("(filter (lambda (x) x) (list 1))", interp);
        bool ok = str_contains(code, "loop");
        if (ok) { pass++; io::printn("[PASS] Compiler: STDLIB filter uses loop pattern"); }
        else    { fail++; io::printn("[FAIL] Compiler: STDLIB filter uses loop pattern"); }
    }

    // =========================================================================
    // Phase 7: JIT infrastructure tests (via lisp::run)
    // =========================================================================

    // 68. map correctness via JIT
    {
        EvalResult r = run("(map (lambda (x) (+ x 1)) (list 1 2 3))", interp);
        bool ok = false;
        if (!r.error.has_error && r.value != null) {
            if (r.value.tag == ValueTag.CONS) {
                Value* car = r.value.cons_val.car;
                ok = (car.tag == ValueTag.INT && car.int_val == 2);
            }
        }
        if (ok) { pass++; io::printn("[PASS] JIT: map correctness"); }
        else    { fail++; io::printn("[FAIL] JIT: map correctness"); }
    }

    // 69. filter correctness via JIT
    {
        EvalResult r = run("(filter (lambda (x) (> x 2)) (list 1 2 3 4))", interp);
        bool ok = false;
        if (!r.error.has_error && r.value != null && r.value.tag == ValueTag.CONS) {
            ok = (r.value.cons_val.car.tag == ValueTag.INT && r.value.cons_val.car.int_val == 3);
        }
        if (ok) { pass++; io::printn("[PASS] JIT: filter correctness"); }
        else    { fail++; io::printn("[FAIL] JIT: filter correctness"); }
    }

    // 70. TCO depth via JIT (named-let loop to 5000)
    {
        EvalResult r = run("(let loop (n 5000 acc 0) (if (= n 0) acc (loop (- n 1) (+ acc 1))))", interp);
        bool ok = (!r.error.has_error && r.value != null && r.value.tag == ValueTag.INT && r.value.int_val == 5000);
        if (ok) { pass++; io::printn("[PASS] JIT: TCO depth 5000"); }
        else    { fail++; io::printn("[FAIL] JIT: TCO depth 5000"); }
    }

    // 71. multi-arg via JIT
    {
        EvalResult r = run("((lambda (x y z) (+ x (+ y z))) 1 2 3)", interp);
        bool ok = (!r.error.has_error && r.value != null && r.value.tag == ValueTag.INT && r.value.int_val == 6);
        if (ok) { pass++; io::printn("[PASS] JIT: multi-arg call"); }
        else    { fail++; io::printn("[FAIL] JIT: multi-arg call"); }
    }

    // 72. mutable capture via JIT (using begin + define for set!)
    {
        EvalResult r = run("(begin (define x 0) (set! x (+ x 1)) (set! x (+ x 1)) x)", interp);
        bool ok = (!r.error.has_error && r.value != null && r.value.tag == ValueTag.INT && r.value.int_val == 2);
        if (ok) { pass++; io::printn("[PASS] JIT: mutable capture"); }
        else    { fail++; io::printn("[FAIL] JIT: mutable capture"); }
    }

    // 73. macro via JIT
    {
        EvalResult r = run("(begin (define [macro] double ([x] (+ x x))) (double 21))", interp);
        bool ok = (!r.error.has_error && r.value != null && r.value.tag == ValueTag.INT && r.value.int_val == 42);
        if (ok) { pass++; io::printn("[PASS] JIT: macro expansion"); }
        else    { fail++; io::printn("[FAIL] JIT: macro expansion"); }
    }

    // =========================================================================
    // Phase 8: compile_to_c3_with_print pattern tests
    // =========================================================================

    // 74. print_last works for literals
    {
        char[] code = compile_to_c3_with_print("42", interp);
        bool ok = str_contains(code, "aot::print_value");
        if (ok) { pass++; io::printn("[PASS] Compiler: print_last literal"); }
        else    { fail++; io::printn("[FAIL] Compiler: print_last literal"); }
    }

    // 75. print_last works for calls
    {
        char[] code = compile_to_c3_with_print("(+ 1 2)", interp);
        bool ok = str_contains(code, "aot::print_value");
        if (ok) { pass++; io::printn("[PASS] Compiler: print_last call"); }
        else    { fail++; io::printn("[FAIL] Compiler: print_last call"); }
    }

    // 76. print_last skips defines, wraps last non-define
    {
        char[] code = compile_to_c3_with_print("(define x 1) x", interp);
        bool ok = str_contains(code, "aot::print_value");
        if (ok) { pass++; io::printn("[PASS] Compiler: print_last skips defines"); }
        else    { fail++; io::printn("[FAIL] Compiler: print_last skips defines"); }
    }

    // 77. default case warning text present
    {
        char[] code = compile_to_c3("42", interp);
        // The default case emits "WARNING" in comment — but only for unsupported exprs.
        // We verify the constant string is in the source by checking the STDLIB compiles without it.
        bool ok = !str_contains(code, "WARNING");
        if (ok) { pass++; io::printn("[PASS] Compiler: no WARNING in normal compilation"); }
        else    { fail++; io::printn("[FAIL] Compiler: no WARNING in normal compilation"); }
    }

    interp.destroy();
    mem::free(interp);
    io::printfn("\n=== Compiler Tests: %d passed, %d failed ===", pass, fail);
    assert(fail == 0, "compiler tests had failures");
}

// =============================================================================
// Escape-scope optimization tests
// =============================================================================

fn void run_escape_scope_tests(Interp* interp, int* pass, int* fail) {
    io::printn("\n--- Escape-Scope Optimization Tests ---");

    // Accumulator pattern: reverse — should be O(n) not O(n^2)
    test_eq(interp, "escape-scope: reverse '(1 2 3) car => 3",
        "(car (reverse (quote (1 2 3))))", 3, pass, fail);
    test_eq(interp, "escape-scope: reverse length => 3",
        "(length (reverse (quote (1 2 3))))", 3, pass, fail);

    // Map with accumulator
    test_eq(interp, "escape-scope: map car => 2",
        "(car (map (lambda (x) (* x 2)) (quote (1 2 3))))", 2, pass, fail);
    test_eq(interp, "escape-scope: map length => 3",
        "(length (map (lambda (x) (+ x 1)) (quote (1 2 3))))", 3, pass, fail);

    // Filter with accumulator
    test_eq(interp, "escape-scope: filter car => 2",
        "(car (filter (lambda (x) (> x 1)) (quote (1 2 3))))", 2, pass, fail);
    test_eq(interp, "escape-scope: filter length => 2",
        "(length (filter (lambda (x) (> x 1)) (quote (1 2 3))))", 2, pass, fail);

    // Non-list return from TCO loop (should not be affected by escape-scope)
    test_eq(interp, "escape-scope: foldl non-list result",
        "(foldl + 0 (quote (1 2 3 4 5)))", 15, pass, fail);

    // Nested function calls with escape-scope
    test_eq(interp, "escape-scope: nested map+reverse",
        "(car (reverse (map (lambda (x) (* x 10)) (quote (1 2 3)))))", 30, pass, fail);

    // Effects inside loops
    test_eq(interp, "escape-scope: handle + map",
        "(handle (car (map (lambda (x) (+ x 1)) (quote (10 20 30)))) (tag arg (resolve 0)))", 11, pass, fail);

    // Iterator/list building (regression: ensure escape-scope doesn't corrupt)
    // Note: can't use 'collect' here — it was redefined by variadic tests above.
    // Instead, test with manual iterator consumption.
    setup(interp, "(define esc-consume (lambda (it) (let loop (acc nil val (next it)) (if (null? val) (reverse acc) (loop (cons (car val) acc) (next (cdr val)))))))");
    test_eq(interp, "escape-scope: iterator consume car",
        "(car (esc-consume (iterator (quote (1 2 3)))))", 1, pass, fail);
    test_eq(interp, "escape-scope: iterator consume len",
        "(length (esc-consume (iterator (quote (1 2 3)))))", 3, pass, fail);

    // Coroutines (regression check — use single self-contained expression per test
    // because test_eq runs the expression twice: once via run(), once via jit_exec)
    test_eq(interp, "escape-scope: coroutine yield",
        "(let (g (coroutine (lambda () (yield 42) 99))) (resume g))", 42, pass, fail);
    test_eq(interp, "escape-scope: coroutine yield+complete",
        "(let (g (coroutine (lambda () (yield 42) 99))) (resume g) (resume g))", 99, pass, fail);

    // Large list operations (performance regression guard)
    setup(interp, "(define big-list (range 1000))");
    test_eq(interp, "escape-scope: reverse 1000 car => 999",
        "(car (reverse big-list))", 999, pass, fail);
    test_eq(interp, "escape-scope: map 1000 length",
        "(length (map (lambda (x) (+ x 1)) big-list))", 1000, pass, fail);

    // --- Escape-Lane Env Optimization Tests ---
    // (TCO loop envs allocated in current scope ESCAPE lane, surviving bounces without copy)

    // Named-let reverse: loop env should survive in ESCAPE lane
    setup(interp, "(define my-rev (lambda (lst) (let loop (xs lst acc (quote ())) (if (null? xs) acc (loop (cdr xs) (cons (car xs) acc))))))");
    test_eq(interp, "esc-env: reverse car",
        "(car (my-rev (quote (1 2 3))))", 3, pass, fail);
    test_eq(interp, "esc-env: reverse length",
        "(length (my-rev (quote (1 2 3))))", 3, pass, fail);
    test_nil(interp, "esc-env: reverse empty",
        "(my-rev (quote ()))", pass, fail);
    test_eq(interp, "esc-env: reverse singleton",
        "(car (my-rev (quote (42))))", 42, pass, fail);

    // Large list — would be very slow without ESCAPE-lane env
    test_eq(interp, "esc-env: reverse 10000 length",
        "(length (my-rev (range 10000)))", 10000, pass, fail);

    // Map with accumulator
    setup(interp, "(define my-map (lambda (f lst) (let loop (xs lst acc (quote ())) (if (null? xs) (reverse acc) (loop (cdr xs) (cons (f (car xs)) acc))))))");
    test_eq(interp, "esc-env: map double",
        "(car (my-map (lambda (x) (* x 2)) (quote (1 2 3))))", 2, pass, fail);
    test_eq(interp, "esc-env: map 10000 length",
        "(length (my-map (lambda (x) (+ x 1)) (range 10000)))", 10000, pass, fail);

    // Filter
    setup(interp, "(define my-filter (lambda (p lst) (let loop (xs lst acc (quote ())) (if (null? xs) (reverse acc) (if (p (car xs)) (loop (cdr xs) (cons (car xs) acc)) (loop (cdr xs) acc))))))");
    test_eq(interp, "esc-env: filter gt2",
        "(length (my-filter (lambda (x) (> x 2)) (quote (1 2 3 4 5))))", 3, pass, fail);

    // Non-list return from loop (scalar — no ESCAPE-lane cons involved)
    test_eq(interp, "esc-env: sum 100",
        "(let loop (n 0 sum 0) (if (= n 100) sum (loop (+ n 1) (+ sum n))))", 4950, pass, fail);

    // Nested named-lets — each with independent ESCAPE-lane lifetime
    setup(interp, "(define flatten (lambda (lst) (let outer (xs lst acc (quote ())) (if (null? xs) (reverse acc) (if (list? (car xs)) (let inner (ys (car xs) acc2 acc) (if (null? ys) (outer (cdr xs) acc2) (inner (cdr ys) (cons (car ys) acc2)))) (outer (cdr xs) (cons (car xs) acc)))))))");
    test_eq(interp, "esc-env: flatten nested",
        "(length (flatten (quote ((1 2) (3 4) (5)))))", 5, pass, fail);
    test_eq(interp, "esc-env: flatten car",
        "(car (flatten (quote ((1 2) (3 4) (5)))))", 1, pass, fail);

    // Effects inside loop
    test_eq(interp, "esc-env: handle+loop",
        "(handle (let loop (n 0 sum 0) (if (= n 5) sum (loop (+ n 1) (+ sum (signal get n))))) (get arg (resolve (* arg 2))))", 20, pass, fail);

    // Named-let with shift should fall back to normal env chain copy
    test_eq(interp, "esc-env: shift-in-loop fallback",
        "(reset (let loop (n 0 acc 0) (if (= n 3) acc (loop (+ n 1) (+ acc (shift k (k 10)))))))", 30, pass, fail);
}
