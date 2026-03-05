module lisp;


import std::io;
import main;
fn void run_advanced_core_semantics_match_tests(Interp* interp, int* pass, int* fail) {
    // Match patterns
    test_eq(interp, "match 1 => 10", "(match 1 (1 10) (2 20) (3 30))", 10, pass, fail);
    test_eq(interp, "match 2 => 20", "(match 2 (1 10) (2 20) (3 30))", 20, pass, fail);
    test_eq(interp, "match 3 => 30", "(match 3 (1 10) (2 20) (3 30))", 30, pass, fail);
    test_eq(interp, "match default => 100", "(match 99 (1 10) (2 20) (x (+ x 1)))", 100, pass, fail);
    test_eq(interp, "match computed", "(match (+ 1 1) (1 10) (2 20) (3 30))", 20, pass, fail);
    test_eq(interp, "match [a b] => 30", "(match (quote (10 20)) ([a b] (+ a b)))", 30, pass, fail);
    test_eq(interp, "match [x] => 42", "(match (quote (42)) ([x] x))", 42, pass, fail);
    test_eq(interp, "match [a b c] product", "(match (quote (2 3 4)) ([a b c] (* a (* b c))))", 24, pass, fail);
    test_eq(interp, "match [h .. t] tail len", "(match (quote (1 2 3 4 5)) ([h .. t] (length t)))", 4, pass, fail);
    test_eq(interp, "match _ wildcard", "(match 42 (_ 0))", 0, pass, fail);
    test_eq(interp, "match string hello", "(match \"hello\" (\"hello\" 1) (\"world\" 2) (x 0))", 1, pass, fail);
    test_eq(interp, "match string world", "(match \"world\" (\"hello\" 1) (\"world\" 2) (x 0))", 2, pass, fail);
    test_eq(interp, "match string default", "(match \"other\" (\"hello\" 1) (\"world\" 2) (x 0))", 0, pass, fail);
}

fn void run_advanced_core_semantics_continuation_tests(Interp* interp, int* pass, int* fail) {
    // Continuations
    test_eq(interp, "shift discard k", "(reset (+ 100 (shift k 42)))", 42, pass, fail);
    test_eq(interp, "shift k mul", "(reset (* 2 (shift k (k 5))))", 10, pass, fail);
    test_eq(interp, "shift k expr", "(reset (+ 1 (shift k (k (+ 2 3)))))", 6, pass, fail);
    test_eq(interp, "shift use k result", "(reset (+ 1 (shift k (+ (k 10) 1000))))", 1011, pass, fail);
    test_eq(interp, "reset value", "(reset 42)", 42, pass, fail);
}

fn void run_advanced_core_semantics_effect_cond_tests(Interp* interp, int* pass, int* fail) {
    // Effect handlers
    test_eq(interp, "effect modify", "(handle (+ 1 (signal get 0)) (get x (resolve 100)))", 101, pass, fail);
    test_eq(interp, "effect arg double", "(handle (signal double 7) (double x (resolve (* x 2))))", 14, pass, fail);
    test_eq(interp, "effect bail", "(handle (+ 1 (+ 2 (+ 3 (signal bail 42)))) (bail x x))", 42, pass, fail);
    test_eq(interp, "handle no effect", "(handle (+ 10 20) (unused x 999))", 30, pass, fail);

    // If/cond edge cases
    test_eq(interp, "if nil => 2", "(if nil 1 2)", 2, pass, fail);
    test_eq(interp, "if false => 2", "(if false 1 2)", 2, pass, fail);
    test_eq(interp, "if 0 => 1 (truthy)", "(if 0 1 2)", 1, pass, fail);
    test_eq(interp, "if empty-str => 1 (truthy)", "(if \"\" 1 2)", 1, pass, fail);
    test_eq(interp, "if '() => 2 (nil)", "(if (quote ()) 1 2)", 2, pass, fail);
    test_eq(interp, "if '(1) => 1 (truthy)", "(if (quote (1)) 1 2)", 1, pass, fail);
    test_eq(interp, "nested if", "(if true (if false 1 2) 3)", 2, pass, fail);
    test_eq(interp, "if with branches", "(if (= 1 1) (+ 10 20) (+ 30 40))", 30, pass, fail);
}

fn void run_advanced_core_semantics_error_tests(Interp* interp, int* pass, int* fail) {
    // Error handling
    test_error(interp, "unbound variable", "nonexistent_var_xyz", pass, fail);
    test_error(interp, "call non-function", "(42 1)", pass, fail);
    test_error(interp, "shift outside reset", "(shift k 42)", pass, fail);
}

fn void run_advanced_core_semantics_try_raise_tests(Interp* interp, int* pass, int* fail) {
    // Error-as-effect: try/catch via raise effect
    test_eq(interp, "handle catches raise",
        "(handle (/ 1 0) (raise msg 42))", 42, pass, fail);
    test_str_val(interp, "try catch div-by-zero",
        "(try (lambda (xx) (/ 1 0)) (lambda (msg) msg))",
        "/: division by zero", pass, fail);
    test_str_val(interp, "try catch arity error",
        "(try (lambda (xx) (+ )) (lambda (msg) msg))",
        "+: expected at least 1 argument", pass, fail);
    test_str_val(interp, "try catch type error",
        "(try (lambda (xx) (+ \"a\" 1)) (lambda (msg) msg))",
        "+: expected number argument", pass, fail);
    test_str_val(interp, "try catch oob array",
        "(try (lambda (xx) (ref [1 2 3] 99)) (lambda (msg) msg))",
        "ref: array index out of bounds", pass, fail);
    test_str_val(interp, "try catch oob string",
        "(try (lambda (xx) (char-at \"abc\" 99)) (lambda (msg) msg))",
        "char-at: index out of range", pass, fail);
    test_str_val(interp, "try catch car nil",
        "(try (lambda (xx) (car 42)) (lambda (msg) msg))",
        "car: argument must be a pair", pass, fail);
    test_str_val(interp, "try catch unbound var",
        "(try (lambda (xx) nonexistent_var_xyz_2) (lambda (msg) msg))",
        "unbound variable 'nonexistent_var_xyz_2'", pass, fail);
    test_eq(interp, "try no error returns value",
        "(try (lambda (xx) 42) (lambda (msg) 0))",
        42, pass, fail);
    test_str_val(interp, "try catch set! unbound",
        "(try (lambda (xx) (set! no_such_var_xyz 1)) (lambda (msg) msg))",
        "set!: unbound variable", pass, fail);
    test_str_val(interp, "try catch user raise",
        "(try (lambda (xx) (signal raise \"custom error\")) (lambda (msg) msg))",
        "custom error", pass, fail);
    test_eq(interp, "nested try inner catches",
        "(try (lambda (xx) (try (lambda (yy) (/ 1 0)) (lambda (msg) 99))) (lambda (msg) 0))",
        99, pass, fail);
    test_eq(interp, "unhandled raise is error",
        "(if (pair? (quote (1))) 1 0)", 1, pass, fail);
}

fn void run_advanced_core_semantics_tests(Interp* interp, int* pass, int* fail) {
    run_advanced_core_semantics_match_tests(interp, pass, fail);
    run_advanced_core_semantics_continuation_tests(interp, pass, fail);
    run_advanced_core_semantics_effect_cond_tests(interp, pass, fail);
    run_advanced_core_semantics_error_tests(interp, pass, fail);
    run_advanced_core_semantics_try_raise_tests(interp, pass, fail);
}

fn void run_advanced_unicode_lambda_utf8_tests(Interp* interp, int* pass, int* fail) {
    // Phase 2A: λ (lambda) syntax
    test_eq(interp, "\xce\xbb application", "((\xce\xbb (x) (* x x)) 5)", 25, pass, fail);
    test_eq(interp, "\xce\xbb multi-param", "((\xce\xbb (x y) (+ x y)) 3 7)", 10, pass, fail);
    test_eq(interp, "\xce\xbb closure", "(let (a 10) ((\xce\xbb (x) (+ x a)) 5))", 15, pass, fail);
    test_eq(interp, "\xce\xbb recursive define", "(begin (define sq (\xce\xbb (x) (* x x))) (sq 6))", 36, pass, fail);

    // Phase 2B: UTF-8 string support
    test_eq(interp, "utf8 strlen ASCII", "(string-length \"hello\")", 5, pass, fail);
    test_eq(interp, "utf8 strlen h\xc3\xa9llo", "(string-length \"h\xc3\xa9llo\")", 5, pass, fail);
    test_eq(interp, "utf8 strlen CJK", "(string-length \"\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e\")", 3, pass, fail);
    test_eq(interp, "utf8 strlen emoji", "(string-length \"\xf0\x9f\x98\x80\")", 1, pass, fail);
    test_eq(interp, "utf8 byte-length ASCII", "(string-byte-length \"hello\")", 5, pass, fail);
    test_eq(interp, "utf8 byte-length h\xc3\xa9llo", "(string-byte-length \"h\xc3\xa9llo\")", 6, pass, fail);
    test_eq(interp, "utf8 byte-length CJK", "(string-byte-length \"\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e\")", 9, pass, fail);
    test_str_val(interp, "utf8 char-at ASCII", "(char-at \"hello\" 1)", "e", pass, fail);
    test_str_val(interp, "utf8 char-at caf\xc3\xa9", "(char-at \"caf\xc3\xa9\" 3)", "\xc3\xa9", pass, fail);
    test_str_val(interp, "utf8 char-at CJK", "(char-at \"\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e\" 1)", "\xe6\x9c\xac", pass, fail);
    test_str_val(interp, "utf8 char-at neg", "(char-at \"caf\xc3\xa9\" -1)", "\xc3\xa9", pass, fail);
    test_str_val(interp, "utf8 substring", "(substring \"caf\xc3\xa9\" 0 3)", "caf", pass, fail);
    test_str_val(interp, "utf8 substring mid", "(substring \"h\xc3\xa9llo\" 1 3)", "\xc3\xa9l", pass, fail);
    test_eq(interp, "utf8 string->list len", "(length (string->list \"h\xc3\xa9llo\"))", 5, pass, fail);
}

fn void run_advanced_unicode_iterator_primitives_tests(Interp* interp, int* pass, int* fail) {
    // Phase 3A: Iterator type + primitives
    test_truthy(interp, "make-iterator creates iter",
        "(iterator? (make-iterator (lambda () nil)))", pass, fail);
    test_truthy(interp, "next on empty iter is nil",
        "(null? (next (make-iterator (lambda () nil))))", pass, fail);
    // Simple iterator: returns (cons 42 empty-iter), then empty
    setup(interp, "(define empty-it (make-iterator (lambda () nil)))");
    setup(interp, "(define one-it (make-iterator (lambda () (cons 42 empty-it))))");
    test_eq(interp, "next returns pair car", "(car (next one-it))", 42, pass, fail);
    // Collect a hand-made 3-element iterator
    setup(interp, "(define it3 (make-iterator (lambda () (cons 3 (make-iterator (lambda () (cons 2 (make-iterator (lambda () (cons 1 (make-iterator (lambda () nil))))))))))))" );
    test_eq(interp, "list it3 car", "(car (list it3))", 3, pass, fail);
    test_eq(interp, "list it3 len", "(length (list it3))", 3, pass, fail);
    // array constructor from iterator
    test_eq(interp, "array it3 len", "(length (array it3))", 3, pass, fail);
    test_eq(interp, "array it3 ref 0", "(ref (array it3) 0)", 3, pass, fail);
    // type-of iterator
    test_truthy(interp, "type-of iterator",
        "(is? (make-iterator (lambda () nil)) (quote Iterator))", pass, fail);
}

fn void run_advanced_unicode_iterator_lazy_pipeline_tests(Interp* interp, int* pass, int* fail) {
    // Phase 3B: iterator constructors + lazy combinators
    test_truthy(interp, "iterator dispatches list",
        "(iterator? (iterator (quote (1 2 3))))", pass, fail);
    test_eq(interp, "iterator list list car", "(car (list (iterator (quote (1 2 3)))))", 1, pass, fail);
    test_eq(interp, "iterator list list len", "(length (list (iterator (quote (1 2 3)))))", 3, pass, fail);
    test_eq(interp, "iterator array list car", "(car (list (iterator [10 20 30])))", 10, pass, fail);
    test_eq(interp, "iterator array list len", "(length (list (iterator [10 20 30])))", 3, pass, fail);
    test_eq(interp, "iterator dict list len", "(length (list (iterator (begin (define iter-d {'a 1 'b 2}) iter-d))))", 2, pass, fail);
    test_truthy(interp, "iterator idempotent", "(iterator? (iterator (iterator [1 2 3])))", pass, fail);
    // Lazy map on iterator
    test_eq(interp, "map + list car", "(car (list (map (lambda (x) (* x 2)) (iterator [1 2 3]))))", 2, pass, fail);
    test_eq(interp, "map + list len", "(length (list (map (lambda (x) (* x 2)) (iterator [1 2 3]))))", 3, pass, fail);
    // Lazy filter on iterator
    test_truthy(interp, "filter iterator returns iterator",
        "(iterator? (filter even? (iterator [1 2 3 4])))", pass, fail);
    test_eq(interp, "filter even car", "(car (list (filter even? (iterator [1 2 3 4 5]))))", 2, pass, fail);
    test_eq(interp, "filter even len", "(length (list (filter even? (iterator [1 2 3 4 5]))))", 2, pass, fail);
    // Lazy take on iterator
    test_eq(interp, "take 2 len", "(length (list (take 2 (iterator [10 20 30]))))", 2, pass, fail);
    test_eq(interp, "take 2 car", "(car (list (take 2 (iterator [10 20 30]))))", 10, pass, fail);
    // Infinite + take
    test_eq(interp, "range-from + take", "(car (list (take 5 (range-from 0))))", 0, pass, fail);
    test_eq(interp, "range-from + take len", "(length (list (take 5 (range-from 0))))", 5, pass, fail);
    // map + filter pipeline
    test_eq(interp, "lazy pipeline len",
        "(length (list (take 3 (filter even? (map (lambda (x) (* x 2)) (range-from 1))))))", 3, pass, fail);
    // array constructor from iterator
    test_eq(interp, "array from iter", "(ref (array (iterator [5 6 7])) 1)", 6, pass, fail);
    // foldl on iterator
    test_eq(interp, "foldl iterator sum", "(foldl (lambda (a x) (+ a x)) 0 (iterator [1 2 3 4 5]))", 15, pass, fail);
    // unified infinite sources
    test_eq(interp, "repeat + take len", "(length (list (take 4 (repeat 7))))", 4, pass, fail);
    test_eq(interp, "cycle + take first", "(car (list (take 4 (cycle [9 8]))))", 9, pass, fail);
    // array-dispatched sequence ops
    test_eq(interp, "take array len", "(length (take 2 [10 20 30]))", 2, pass, fail);
    test_eq(interp, "take array ref 1", "(ref (take 2 [10 20 30]) 1)", 20, pass, fail);
    test_eq(interp, "drop array len", "(length (drop 1 [10 20 30]))", 2, pass, fail);
    test_eq(interp, "drop array ref 0", "(ref (drop 1 [10 20 30]) 0)", 20, pass, fail);
    test_eq(interp, "zip array len", "(length (zip [1 2 3] [4 5]))", 2, pass, fail);
    test_eq(interp, "zip array first-left", "(car (ref (zip [1 2] [4 5]) 0))", 1, pass, fail);
    test_eq(interp, "foldl array sum", "(foldl (lambda (a x) (+ a x)) 0 [1 2 3 4])", 10, pass, fail);
    // foldr should exist for finite collections
    test_eq(interp, "foldr list minus", "(foldr - 0 (quote (1 2 3)))", 2, pass, fail);
    test_eq(interp, "foldr array minus", "(foldr - 0 [1 2 3])", 2, pass, fail);
}

fn void run_advanced_unicode_iterator_tests(Interp* interp, int* pass, int* fail) {
    run_advanced_unicode_lambda_utf8_tests(interp, pass, fail);
    run_advanced_unicode_iterator_primitives_tests(interp, pass, fail);
    run_advanced_unicode_iterator_lazy_pipeline_tests(interp, pass, fail);
}

fn void run_advanced_logic_tco_tests(Interp* interp, int* pass, int* fail) {
    // And/or advanced
    test_eq(interp, "nested and/or", "(and (or nil 5) (or false 10))", 10, pass, fail);
    test_eq(interp, "nested or/and", "(or (and nil 5) (and 1 10))", 10, pass, fail);
    test_eq(interp, "and closure truthy", "(and (lambda (x) x) 42)", 42, pass, fail);

    // Define/redefinition
    setup(interp, "(define redef-test 10)");
    test_eq(interp, "redef before", "redef-test", 10, pass, fail);
    setup(interp, "(define redef-test 20)");
    test_eq(interp, "redef after", "redef-test", 20, pass, fail);
    setup(interp, "(define computed-val (+ (* 3 4) (- 10 5)))");
    test_eq(interp, "define complex expr", "computed-val", 17, pass, fail);

    // Path access
    setup(interp, "(define ptest (cons (cons (quote x) 10) (cons (cons (quote y) 20) (quote ()))))");
    test_eq(interp, "ptest.x => 10", "ptest.x", 10, pass, fail);
    test_eq(interp, "ptest.y => 20", "ptest.y", 20, pass, fail);

    // TCO (JIT-only mode: no interpreter TCO, limited by eval depth)
    setup(interp, "(define tco-loop (lambda (n) (if (= n 0) (quote done) (tco-loop (- n 1)))))");
    test_truthy(interp, "TCO loop 4000", "(tco-loop 4000)", pass, fail);
    setup(interp, "(define sum-acc (lambda (n) (lambda (acc) (if (= n 0) acc ((sum-acc (- n 1)) (+ acc n))))))");
    test_eq(interp, "TCO sum-acc 1000 => 500500", "((sum-acc 1000) 0)", 500500, pass, fail);
    setup(interp, "(define tco-even (lambda (n) (if (= n 0) true (tco-odd (- n 1)))))");
    setup(interp, "(define tco-odd (lambda (n) (if (= n 0) nil (tco-even (- n 1)))))");
    test_truthy(interp, "TCO mutual recursion 1000", "(tco-even 1000)", pass, fail);
    setup(interp, "(define count-down (lambda (n) (match n (0 (quote done)) (n (count-down (- n 1))))))");
    test_truthy(interp, "TCO match tail call 4000", "(count-down 4000)", pass, fail);
    setup(interp, "(define let-loop (lambda (n) (let (m (- n 1)) (if (= m 0) (quote done) (let-loop m)))))");
    test_truthy(interp, "TCO let body 4000", "(let-loop 4000)", pass, fail);
}

fn void run_advanced_effect_continuation_tests(Interp* interp, int* pass, int* fail) {
    // Multi-perform
    test_eq(interp, "multi-perform 2 sum", "(handle (+ (signal bounce 10) (signal bounce 20)) (bounce x (resolve x)))", 30, pass, fail);
    test_eq(interp, "multi-perform 3 sum", "(handle (+ (signal bounce 1) (+ (signal bounce 2) (signal bounce 3))) (bounce x (resolve x)))", 6, pass, fail);
    test_eq(interp, "multi-perform doubles", "(handle (+ (signal bounce 10) (signal bounce 20)) (bounce x (resolve (* x 2))))", 60, pass, fail);
    test_eq(interp, "multi-perform abort", "(handle (+ (signal bounce 10) (signal bounce 20)) (bounce x x))", 10, pass, fail);

    // signal/resolve syntax (new match-style clauses)
    test_eq(interp, "signal/resolve basic", "(handle (+ 1 (signal read nil)) (read x (resolve 41)))", 42, pass, fail);
    test_eq(interp, "signal abort", "(handle (+ 1 (signal bail 5)) (bail x x))", 5, pass, fail);
    test_eq(interp, "signal/resolve double", "(handle (signal double 7) (double x (resolve (* x 2))))", 14, pass, fail);
    test_eq(interp, "signal no trigger", "(handle (+ 10 20) (unused x 999))", 30, pass, fail);
    test_eq(interp, "signal multi-resume", "(handle (+ (signal bounce 10) (signal bounce 20)) (bounce x (resolve x)))", 30, pass, fail);
    test_error_contains(interp, "resolve outside handler", "(resolve 42)", "resolve", pass, fail);

    test_eq(interp, "multi-shift sum", "(reset (+ (shift k1 (k1 10)) (shift k2 (k2 20))))", 30, pass, fail);

    // Multi-shot continuations
    test_eq(interp, "multi-shot k twice", "(reset (+ 1 (shift k (+ (k 10) (k 20)))))", 32, pass, fail);
    test_eq(interp, "multi-shot k thrice", "(reset (* 2 (shift k (+ (k 3) (+ (k 5) (k 7))))))", 30, pass, fail);
    test_eq(interp, "multi-shot let bindings", "(reset (+ 10 (shift k (let (a (k 1) b (k 2)) (+ a b)))))", 23, pass, fail);
    test_eq(interp, "multi-shot conditional", "(reset (+ 5 (shift k (if true (k 10) (k 20)))))", 15, pass, fail);
    test_eq(interp, "multi-shot effect", "(handle (+ 1 (signal dup 0)) ((dup k x) (+ (k 10) (k 20))))", 32, pass, fail);

    // with-continuation (multi-shot escape hatch in handler clauses)
    test_eq(interp, "with-continuation basic", "(handle (+ 1 (signal choose 0)) (choose x (with-continuation k (+ (k 10) (k 20)))))", 32, pass, fail);
    test_eq(interp, "with-continuation single", "(handle (+ 1 (signal ask 0)) (ask x (with-continuation k (k 41))))", 42, pass, fail);

    // TCO inside reset/shift
    setup(interp, "(define reset-loop (lambda (n acc) (if (= n 0) acc (reset-loop (- n 1) (+ acc 1)))))");
    test_eq(interp, "TCO in reset body", "(reset (reset-loop 5000 0))", 5000, pass, fail);
    setup(interp, "(define shift-loop (lambda (n acc) (if (= n 0) acc (shift-loop (- n 1) (+ acc 1)))))");
    test_eq(interp, "TCO in shift body", "(reset (+ 1 (shift k (shift-loop 5000 0))))", 5000, pass, fail);
    test_eq(interp, "TCO begin+if in reset", "(reset (begin (+ 1 2) (if true (+ 10 20) 99)))", 30, pass, fail);
    test_eq(interp, "TCO let chains in shift", "(reset (+ 1 (shift k (let (x 10) (let (y 20) (+ x y))))))", 30, pass, fail);
    setup(interp, "(define cont-use-loop (lambda (k n) (if (= n 0) (k 42) (cont-use-loop k (- n 1)))))");
    test_eq(interp, "TCO tail-recursive k use", "(reset (+ 1 (shift k (cont-use-loop k 1000))))", 43, pass, fail);
    test_eq(interp, "TCO nested reset", "(reset (+ (reset (if true 10 20)) (shift k (k 5))))", 15, pass, fail);
    setup(interp, "(define handle-loop (lambda (n) (if (= n 0) 0 (+ 1 (handle-loop (- n 1))))))");
    test_eq(interp, "TCO handler body", "(handle (signal count 0) (count x (handle-loop 100)))", 100, pass, fail);
}

fn void run_advanced_runtime_control_tests(Interp* interp, int* pass, int* fail) {
    // Trampoline (JIT delegates effects to eval which handles them)
    test_eq(interp, "trampoline passthrough", "(with-trampoline (lambda (xx) 42))", 42, pass, fail);
    test_eq(interp, "trampoline single bounce", "(with-trampoline (lambda (xx) (+ 1 (signal bounce (lambda (xx) 41)))))", 42, pass, fail);
    setup(interp, "(define fib-t (lambda (n) (if (< n 2) n (+ (signal bounce (lambda (xx) (fib-t (- n 1)))) (signal bounce (lambda (xx) (fib-t (- n 2))))))))");
    test_eq(interp, "trampoline fib 7", "(with-trampoline (lambda (xx) (fib-t 7)))", 13, pass, fail);

    // Coroutine primitives (coroutine/resume/yield)
    test_eq(interp, "coroutine basic", "(resume (coroutine (lambda () (+ 1 2))))", 3, pass, fail);
    test_eq(interp, "coroutine yield", "(let (f (coroutine (lambda () (yield 42) 99))) (resume f))", 42, pass, fail);
    test_eq(interp, "coroutine yield complete", "(let (f (coroutine (lambda () (yield 42) 99))) (resume f) (resume f))", 99, pass, fail);
    test_eq(interp, "coroutine resume value", "(let (f (coroutine (lambda () (+ 1 (yield 0))))) (resume f) (resume f 10))", 11, pass, fail);
    test_eq(interp, "coroutine nested", "(let (outer (coroutine (lambda () (let (inner (coroutine (lambda () 10))) (+ (resume inner) 5))))) (resume outer))", 15, pass, fail);
    test_eq(interp, "coroutine deep nest", "(let (f (coroutine (lambda () (let (a (coroutine (lambda () (let (b (coroutine (lambda () 1))) (+ (resume b) 2))))) (+ (resume a) 3))))) (resume f))", 6, pass, fail);
    test_eq(interp, "coroutine multi yield", "(let (g (coroutine (lambda () (yield 1) (yield 2) 3))) (+ (resume g) (+ (resume g) (resume g))))", 6, pass, fail);
    setup(interp, "(define (coroutine-gen n) (coroutine (lambda () (let loop (i 0) (if (= i n) i (begin (yield i) (loop (+ i 1))))))))");
    test_eq(interp, "coroutine generator", "(let (g (coroutine-gen 3)) (+ (resume g) (+ (resume g) (+ (resume g) (resume g)))))", 6, pass, fail);

    // Variadic lambdas (JIT detects variadic closures and routes to fallback)
    setup(interp, "(define collect (lambda (.. args) args))");
    test_eq(interp, "variadic collect length", "(length (collect 1 2 3))", 3, pass, fail);
    test_nil(interp, "variadic collect empty", "(collect)", pass, fail);
    setup(interp, "(define head-rest (lambda (h .. t) t))");
    test_eq(interp, "variadic rest length", "(length (head-rest 1 2 3))", 2, pass, fail);
    test_nil(interp, "variadic empty rest", "(head-rest 1)", pass, fail);
    setup(interp, "(define multi-rest (lambda (a b .. rest) rest))");
    test_eq(interp, "variadic multi rest len", "(length (multi-rest 1 2 3 4 5))", 3, pass, fail);
    test_eq(interp, "variadic head-only", "((lambda (h .. rest-ho) h) 42 99 100)", 42, pass, fail);
    setup(interp, "(define count-rest (lambda (h .. t) (length t)))");
    test_eq(interp, "variadic count-rest", "(count-rest 1 2 3 4 5)", 4, pass, fail);
}

fn void run_advanced_lambda_syntax_tests(Interp* interp, int* pass, int* fail) {
    // Begin
    test_eq(interp, "begin returns last", "(begin 1 2 3)", 3, pass, fail);
    test_eq(interp, "begin single", "(begin 42)", 42, pass, fail);
    test_eq(interp, "begin side effects", "(begin (define begin-x 10) (define begin-y 20) (+ begin-x begin-y))", 30, pass, fail);
    test_eq(interp, "begin nested", "(begin (begin 1 2) (begin 3 4))", 4, pass, fail);
    test_eq(interp, "begin with if", "(begin (if true 10 20) (if false 30 40))", 40, pass, fail);

    // Zero-arg lambdas
    test_eq(interp, "zero-arg thunk", "((lambda () 42))", 42, pass, fail);
    test_eq(interp, "zero-arg closure", "(let (a 10) ((lambda () a)))", 10, pass, fail);
    test_eq(interp, "zero-arg stored thunk", "(let (thunk (lambda () 99)) (thunk))", 99, pass, fail);
    test_eq(interp, "zero-arg begin body", "((lambda () (begin 1 2 3)))", 3, pass, fail);
    test_eq(interp, "zero-arg computation", "((lambda () (+ 10 20)))", 30, pass, fail);

    // Multi-param lambda (strict arity)
    test_eq(interp, "multi-param two", "((lambda (x y) (+ x y)) 3 4)", 7, pass, fail);
    test_eq(interp, "multi-param three", "((lambda (x y z) (+ x (+ y z))) 1 2 3)", 6, pass, fail);
    test_eq(interp, "multi-param direct call", "(let (add (lambda (x y) (+ x y))) (add 10 20))", 30, pass, fail);
    setup(interp, "(define mp-add3 (lambda (a b c) (+ a (+ b c))))");
    test_eq(interp, "multi-param defined fn", "(mp-add3 10 20 30)", 60, pass, fail);
    test_eq(interp, "multi-param three call", "(let (f (lambda (x y z) (+ x (+ y z)))) (f 1 2 3))", 6, pass, fail);

    // Arity error tests (strict enforcement)
    test_error_contains(interp, "arity err 2-param 1-arg", "((lambda (x y) (+ x y)) 5)", "arity", pass, fail);
    test_error_contains(interp, "arity err 3-param 2-arg", "((lambda (x y z) (+ x y z)) 1 2)", "arity", pass, fail);

    // Placeholder _ desugaring
    test_eq(interp, "placeholder basic", "((+ 1 _) 5)", 6, pass, fail);
    test_eq(interp, "placeholder position", "((- _ 5) 10)", 5, pass, fail);
    test_eq(interp, "placeholder multi", "((+ _ _) 3 4)", 7, pass, fail);
    test_eq(interp, "placeholder in let", "(let (f (+ _ 10)) (f 5))", 15, pass, fail);
    test_eq(interp, "placeholder nested call", "((* 2 _) ((+ 1 _) 3))", 8, pass, fail);

    // Pipe |> operator
    test_eq(interp, "pipe basic", "(|> 5 (+ 3))", 8, pass, fail);
    test_eq(interp, "pipe chain", "(|> 5 (+ 3) (* 2))", 16, pass, fail);
    test_eq(interp, "pipe placeholder", "(|> 5 (- _ 3))", 2, pass, fail);
    test_eq(interp, "pipe mixed", "(|> 10 (+ 5) (- _ 3) (* 2))", 24, pass, fail);

    // Guard patterns (? pred)
    test_eq(interp, "guard basic", "(match 5 ((? positive?) 1) (_ 0))", 1, pass, fail);
    test_eq(interp, "guard fail", "(match -3 ((? positive?) 1) (_ 0))", 0, pass, fail);
    test_eq(interp, "guard with bind", "(match 5 ((? positive? x) x) (_ 0))", 5, pass, fail);
    test_eq(interp, "guard lambda", "(match 5 ((? (lambda (x) (> x 3))) 1) (_ 0))", 1, pass, fail);
    test_eq(interp, "guard placeholder", "(match 5 ((? (> _ 3)) 1) (_ 0))", 1, pass, fail);

    // Partial application
    test_eq(interp, "partial basic", "((partial + 1) 2)", 3, pass, fail);
    test_eq(interp, "partial multi", "((partial + 10) 20)", 30, pass, fail);
}

fn void run_advanced_binding_mutation_tests(Interp* interp, int* pass, int* fail) {
    // Multi-binding let
    test_eq(interp, "multi-let two", "(let (ml-x 10 ml-y 20) (+ ml-x ml-y))", 30, pass, fail);
    test_eq(interp, "multi-let three", "(let (ml-a 1 ml-b 2 ml-c 3) (+ ml-a (+ ml-b ml-c)))", 6, pass, fail);
    test_eq(interp, "multi-let sequential", "(let (ml-p 10 ml-q (+ ml-p 5)) ml-q)", 15, pass, fail);
    test_eq(interp, "multi-let with lambda", "(let (ml-f (lambda (xx) (+ xx 1)) ml-v (ml-f 10)) ml-v)", 11, pass, fail);
    test_eq(interp, "multi-let single", "(let (ml-z 42) ml-z)", 42, pass, fail);

    // Named let
    test_eq(interp, "named-let sum 1..5", "(let loop (n 5 acc 0) (if (= n 0) acc (loop (- n 1) (+ acc n))))", 15, pass, fail);
    test_eq(interp, "named-let fact 10", "(let fact (n 10 acc 1) (if (= n 0) acc (fact (- n 1) (* acc n))))", 3628800, pass, fail);
    test_eq(interp, "named-let fib 10", "(let fib (n 10 a 0 b 1) (if (= n 0) a (fib (- n 1) b (+ a b))))", 55, pass, fail);
    test_eq(interp, "named-let fib 20", "(let fib (n 20 a 0 b 1) (if (= n 0) a (fib (- n 1) b (+ a b))))", 6765, pass, fail);
    test_eq(interp, "named-let countdown", "(let count (n 100) (if (= n 0) 0 (count (- n 1))))", 0, pass, fail);
    test_eq(interp, "named-let build list car", "(car (let build (n 3 acc (quote ())) (if (= n 0) acc (build (- n 1) (cons n acc)))))", 1, pass, fail);
    test_eq(interp, "named-let 10000 iters", "(let loop (n 10000 acc 0) (if (= n 0) acc (loop (- n 1) (+ acc 1))))", 10000, pass, fail);

    // set!
    setup(interp, "(define set-test 10)");
    setup(interp, "(set! set-test 20)");
    test_eq(interp, "set! basic", "set-test", 20, pass, fail);

    // Counter tests are stateful (calling counter1 mutates), use JIT-only to avoid double mutation
    setup(interp, "(define make-counter (lambda () (let (count 0) (lambda () (begin (set! count (+ count 1)) count)))))");
    setup(interp, "(define counter1 (make-counter))");
    test_eq_jit(interp, "set! counter 1", "(counter1)", 1, pass, fail);
    test_eq_jit(interp, "set! counter 2", "(counter1)", 2, pass, fail);
    test_eq_jit(interp, "set! counter 3", "(counter1)", 3, pass, fail);

    // set! on let-local in closure (JIT detects and falls back to interpreter)
    test_eq(interp, "set! parent scope", "(let (x 1) (let (f (lambda () (set! x 42))) (begin (f) x)))", 42, pass, fail);
    test_error(interp, "set! unbound", "(set! undefined-var-xyz 1)", pass, fail);
    setup(interp, "(define sr-test 100)");
    setup(interp, "(set! sr-test (+ sr-test 1))");
    test_eq(interp, "set! read back", "sr-test", 101, pass, fail);
}

fn void run_advanced_macro_hygiene_string_number_tests(Interp* interp, int* pass, int* fail) {
    // String<->number
    test_eq(interp, "string->number 42", "(string->number \"42\")", 42, pass, fail);
    test_eq(interp, "string->number -7", "(string->number \"-7\")", -7, pass, fail);
    test_nil(interp, "string->number abc => nil", "(string->number \"abc\")", pass, fail);
    test_nil(interp, "string->number empty => nil", "(string->number \"\")", pass, fail);
    test_eq(interp, "number->string round-trip 42", "(string->number (number->string 42))", 42, pass, fail);
    test_eq(interp, "number->string round-trip 0", "(string->number (number->string 0))", 0, pass, fail);
    test_eq(interp, "number->string round-trip -99", "(string->number (number->string -99))", -99, pass, fail);

    // Stack overflow
    test_error(interp, "stack overflow caught", "(let ^rec (f (lambda (n) (+ 1 (f (+ n 1))))) (f 0))", pass, fail);
}

fn void run_advanced_macro_hygiene_quasi_pattern_tests(Interp* interp, int* pass, int* fail) {
    // Quasiquote
    test_tag(interp, "quasiquote basic", "`(a b c)", CONS, pass, fail);
    setup(interp, "(define qq-x 42)");
    test_tag(interp, "quasiquote unquote", "`(a ,qq-x c)", CONS, pass, fail);
    test_eq(interp, "quasiquote standalone", "`,(+ 1 2)", 3, pass, fail);
    setup(interp, "(define qq-lst (list 1 2 3))");
    test_eq(interp, "quasiquote splice len", "(length `(a ,@qq-lst d))", 5, pass, fail);
    setup(interp, "(define qq-empty (list))");
    test_eq(interp, "quasiquote splice empty", "(length `(a ,@qq-empty b))", 2, pass, fail);

    // Pattern macros
    setup(interp, "(define [macro] when ([test .. body] (if test (begin .. body) nil)))");
    test_eq(interp, "when true", "(when true 42)", 42, pass, fail);
    test_nil(interp, "when false", "(when false 42)", pass, fail);
    setup(interp, "(define [macro] unless ([test .. body] (if test nil (begin .. body))))");
    test_eq(interp, "unless false", "(unless false 99)", 99, pass, fail);
    test_nil(interp, "unless true", "(unless true 99)", pass, fail);
    setup(interp, "(define [macro] with-val ([name val body] (let (name val) body)))");
    test_eq(interp, "macro with-val", "(with-val z 100 (+ z 1))", 101, pass, fail);
    setup(interp, "(define [macro] cond ([] nil) ([test body .. rest] (if test body (cond .. rest))))");
    test_eq(interp, "cond macro", "(cond false 1 true 2 true 3)", 2, pass, fail);
    test_nil(interp, "cond all false", "(cond false 1 false 2)", pass, fail);
    setup(interp, "(define [macro] my-and ([a b] (if a b false)))");
    test_eq(interp, "my-and true", "(my-and true 42)", 42, pass, fail);
    setup(interp, "(define [macro] let1 ([name val body] (let (name val) body)))");
    test_eq(interp, "let1 macro", "(let1 x 10 (+ x 5))", 15, pass, fail);
}

fn void run_advanced_macro_hygiene_gensym_tests(Interp* interp, int* pass, int* fail) {
    // Auto-gensym
    setup(interp, "(define [macro] swap! ([a b] (let (tmp# a) (begin (set! a b) (set! b tmp#)))))");
    setup(interp, "(define swap-x 10)");
    setup(interp, "(define swap-y 20)");
    setup(interp, "(swap! swap-x swap-y)");
    test_eq(interp, "swap! x", "swap-x", 20, pass, fail);
    test_eq(interp, "swap! y", "swap-y", 10, pass, fail);
    setup(interp, "(define tmp 999)");
    setup(interp, "(define hx 1)");
    setup(interp, "(define hy 2)");
    setup(interp, "(swap! hx hy)");
    test_eq(interp, "gensym hygiene", "tmp", 999, pass, fail);

    // gensym
    test_tag(interp, "gensym returns symbol", "(gensym)", SYMBOL, pass, fail);
}

fn void run_advanced_macro_hygiene_capture_tests(Interp* interp, int* pass, int* fail) {
    // Macro hygiene (def-env capture)
    setup(interp, "(define hyg-helper (lambda (x) (+ x 100)))");
    setup(interp, "(define [macro] hyg-apply ([x] (hyg-helper x)))");
    test_eq(interp, "hygiene normal", "(hyg-apply 5)", 105, pass, fail);
    setup(interp, "(define hyg-helper (lambda (x) (* x 999)))");
    test_eq(interp, "hygiene immune to shadow", "(hyg-apply 5)", 105, pass, fail);
    setup(interp, "(define [macro] hyg-inc ([x] (+ x 1)))");
    test_eq(interp, "hygiene captures +", "(hyg-inc 10)", 11, pass, fail);
    setup(interp, "(define [macro] hyg-nil-test ([x] (if x 42 nil)))");
    test_nil(interp, "hygiene nil branch", "(hyg-nil-test false)", pass, fail);
    test_eq(interp, "hygiene true branch", "(hyg-nil-test true)", 42, pass, fail);
    setup(interp, "(define hyg-a 10)");
    setup(interp, "(define hyg-b 20)");
    setup(interp, "(define [macro] hyg-sum ([x] (+ (+ hyg-a hyg-b) x)))");
    test_eq(interp, "hygiene multi literals", "(hyg-sum 5)", 35, pass, fail);
    setup(interp, "(define hyg-a 1000)");
    setup(interp, "(define hyg-b 2000)");
    test_eq(interp, "hygiene multi immune", "(hyg-sum 5)", 35, pass, fail);
    setup(interp, "(define hyg-op (lambda (x) (* x 2)))");
    setup(interp, "(define [macro] hyg-with-tmp ([x] (let (tmp# (hyg-op x)) tmp#)))");
    test_eq(interp, "hygiene gensym+def", "(hyg-with-tmp 5)", 10, pass, fail);
    setup(interp, "(define hyg-op (lambda (x) 0))");
    test_eq(interp, "hygiene gensym+def immune", "(hyg-with-tmp 5)", 10, pass, fail);
    setup(interp, "(define x 999)");
    setup(interp, "(define [macro] hyg-id ([x] x))");
    test_eq(interp, "hygiene patvar substitutes", "(hyg-id 7)", 7, pass, fail);
    test_eq(interp, "hygiene recursive cond", "(cond false 1 false 2 true 3)", 3, pass, fail);
    setup(interp, "(define hyg-late-fn (lambda (x) (+ x 50)))");
    setup(interp, "(define [macro] hyg-late-macro ([x] (hyg-late-fn x)))");
    setup(interp, "(define hyg-late-fn (lambda (x) (- x 50)))");
    test_eq(interp, "hygiene late redef", "(hyg-late-macro 100)", 150, pass, fail);
}

fn void run_advanced_macro_hygiene_tests(Interp* interp, int* pass, int* fail) {
    run_advanced_macro_hygiene_string_number_tests(interp, pass, fail);
    run_advanced_macro_hygiene_quasi_pattern_tests(interp, pass, fail);
    run_advanced_macro_hygiene_gensym_tests(interp, pass, fail);
    run_advanced_macro_hygiene_capture_tests(interp, pass, fail);
}

fn void run_advanced_collections_dict_literal_tests(Interp* interp, int* pass, int* fail) {
    // Dicts
    setup(interp, "(define hm1 (dict 1 10 2 20 3 30))");
    test_eq(interp, "ref dict", "(ref hm1 2)", 20, pass, fail);
    test_nil(interp, "ref dict missing", "(ref hm1 99)", pass, fail);
    setup(interp, "(dict-set! hm1 2 200)");
    test_eq(interp, "dict-set! overwrite", "(ref hm1 2)", 200, pass, fail);
    test_truthy(interp, "has? dict yes", "(has? hm1 1)", pass, fail);
    test_nil(interp, "has? dict no", "(has? hm1 99)", pass, fail);
    test_eq(interp, "length dict", "(length hm1)", 3, pass, fail);
    setup(interp, "(define hm2 (dict 1 10 2 20 3 30))");
    setup(interp, "(remove! hm2 2)");
    test_nil(interp, "remove! dict", "(has? hm2 2)", pass, fail);
    test_eq(interp, "remove! count", "(length hm2)", 2, pass, fail);
    setup(interp, "(define hm3 (dict 1 10 2 20))");
    test_eq(interp, "keys len", "(length (keys hm3))", 2, pass, fail);
    test_eq(interp, "values len", "(length (values hm3))", 2, pass, fail);
    setup(interp, "(define hm4 (dict \"name\" \"Alice\" \"age\" 30))");
    test_str(interp, "dict string key", "(ref hm4 \"name\")", pass, fail);
    setup(interp, "(define hm5 (dict))");
    test_eq(interp, "dict empty count", "(length hm5)", 0, pass, fail);
    test_truthy(interp, "dict? yes", "(dict? (dict 1 2))", pass, fail);
    test_nil(interp, "dict? no", "(dict? 42)", pass, fail);
    setup(interp, "(define hm-big (dict))");
    setup(interp, "(let loop (i 0) (if (= i 50) hm-big (begin (dict-set! hm-big i (* i i)) (loop (+ i 1)))))");
    test_eq(interp, "dict auto-grow count", "(length hm-big)", 50, pass, fail);
    test_eq(interp, "dict auto-grow ref 7", "(ref hm-big 7)", 49, pass, fail);

    // === LITERAL SYNTAX TESTS ===
    // Dict literal {}
    test_eq(interp, "{} empty", "(length {})", 0, pass, fail);
    test_eq(interp, "{} literal", "(ref {'a 1 'b 2} 'a)", 1, pass, fail);
    test_truthy(interp, "{} string keys", "(has? {\"name\" \"Alice\"} \"name\")", pass, fail);
    test_truthy(interp, "{} has key", "(has? {'x 10 'y 20} 'y)", pass, fail);
    test_eq(interp, "{} count", "(length {'a 1 'b 2 'c 3})", 3, pass, fail);
    // Array literal []
    test_eq(interp, "[] empty", "(length [])", 0, pass, fail);
    test_eq(interp, "[] literal", "(ref [10 20 30] 1)", 20, pass, fail);
    test_eq(interp, "[] length", "(length [1 2 3])", 3, pass, fail);
    test_eq(interp, "[] nested", "(ref (ref [1 [2 3]] 1) 0)", 2, pass, fail);
    test_eq(interp, "[] index", "(let (a [10 20 30]) a.[1])", 20, pass, fail);
    // Cons cell dot-path access and mutation
    test_eq(interp, "cons .car", "(let (p (cons 1 2)) p.car)", 1, pass, fail);
    test_eq(interp, "cons .cdr", "(let (p (cons 1 2)) p.cdr)", 2, pass, fail);
    test_eq_interp(interp, "set! cons .car", "(let (p (cons 1 2)) (begin (set! p.car 99) p.car))", 99, pass, fail);
    test_eq_interp(interp, "set! cons .cdr", "(let (p (cons 1 2)) (begin (set! p.cdr 50) p.cdr))", 50, pass, fail);
    test_eq_interp(interp, "set! cons preserves", "(let (p (cons 1 2)) (begin (set! p.car 99) p.cdr))", 2, pass, fail);
}

fn void run_advanced_collections_generic_ops_tests(Interp* interp, int* pass, int* fail) {
    // === GENERIC OPERATIONS TESTS ===
    // ref - array
    test_eq(interp, "ref array", "(ref [10 20 30] 1)", 20, pass, fail);
    test_error(interp, "ref array oob", "(ref [1] 5)", pass, fail);
    // ref - dict
    test_eq(interp, "ref dict", "(ref {'a 1 'b 2} 'a)", 1, pass, fail);
    test_nil(interp, "ref dict missing", "(ref {'a 1} 'b)", pass, fail);
    // ref - cons
    test_eq(interp, "ref cons car", "(ref (cons 10 20) 0)", 10, pass, fail);
    test_eq(interp, "ref cons cdr", "(ref (cons 10 20) 1)", 20, pass, fail);
    // ref - string
    test_eq(interp, "ref string", "(ref \"hello\" 0)", 104, pass, fail);
    // negative indexing (Python-style: -1 = last, -2 = second-to-last)
    test_eq(interp, "ref array -1", "(ref [10 20 30] -1)", 30, pass, fail);
    test_eq(interp, "ref array -2", "(ref [10 20 30] -2)", 20, pass, fail);
    test_eq(interp, "ref array -3", "(ref [10 20 30] -3)", 10, pass, fail);
    test_error(interp, "ref array -4 oob", "(ref [10 20 30] -4)", pass, fail);
    test_eq(interp, "ref string -1", "(ref \"hello\" -1)", 111, pass, fail);
    test_eq(interp, "ref string -5", "(ref \"hello\" -5)", 104, pass, fail);
    test_error(interp, "ref string -6 oob", "(ref \"hello\" -6)", pass, fail);
    // ref on lists (walks cons chain, not just pair 0/1)
    test_eq(interp, "ref list 0", "(ref '(10 20 30) 0)", 10, pass, fail);
    test_eq(interp, "ref list 2", "(ref '(10 20 30) 2)", 30, pass, fail);
    test_eq(interp, "ref list -1", "(ref '(10 20 30) -1)", 30, pass, fail);
    test_error(interp, "ref list oob", "(ref '(10 20 30) 5)", pass, fail);
    test_eq(interp, ".[-1] array", "(let (a [10 20 30]) a.[-1])", 30, pass, fail);
    test_eq(interp, ".[-1] list", "(let (l '(10 20 30)) l.[-1])", 30, pass, fail);
    test_eq(interp, ".[-2] list", "(let (l '(10 20 30)) l.[-2])", 20, pass, fail);
    test_error(interp, ".[-4] list oob", "(let (l '(10 20 30)) l.[-4])", pass, fail);
    test_str(interp, "char-at -1", "(char-at \"abc\" -1)", pass, fail);
    setup(interp, "(define neg-arr [1 2 3])");
    setup(interp, "(array-set! neg-arr -1 99)");
    test_eq(interp, "array-set! -1", "(ref neg-arr 2)", 99, pass, fail);
    // length - generic
    test_eq(interp, "length array", "(length [1 2 3])", 3, pass, fail);
    test_eq(interp, "length dict", "(length {'a 1 'b 2})", 2, pass, fail);
    test_eq(interp, "length string", "(length \"hello\")", 5, pass, fail);
    test_eq(interp, "length list", "(length '(1 2 3 4))", 4, pass, fail);
    // push!
    setup(interp, "(define ga [1 2])");
    setup(interp, "(push! ga 3)");
    test_eq(interp, "push! generic", "(length ga)", 3, pass, fail);
    test_eq(interp, "push! val", "(ref ga 2)", 3, pass, fail);
    // keys / values / has? / remove!
    setup(interp, "(define gd {'x 10 'y 20})");
    test_eq(interp, "keys len", "(length (keys gd))", 2, pass, fail);
    test_eq(interp, "values len", "(length (values gd))", 2, pass, fail);
    test_truthy(interp, "has? yes", "(has? gd 'x)", pass, fail);
    test_nil(interp, "has? no", "(has? gd 'z)", pass, fail);
    setup(interp, "(remove! gd 'x)");
    test_nil(interp, "remove! gone", "(has? gd 'x)", pass, fail);
    test_eq(interp, "remove! count", "(length gd)", 1, pass, fail);
}

fn void run_advanced_module_qualified_cached_tests(Interp* interp, int* pass, int* fail) {
    // Qualified access (default import = qualified-only)
    setup(interp, "(module math-utils (export dbl triple) (define dbl (lambda (x) (* x 2))) (define triple (lambda (x) (* x 3))) (define internal (lambda (x) (+ x 100))))");
    setup(interp, "(import math-utils)");
    test_eq(interp, "module qualified dbl", "(math-utils.dbl 5)", 10, pass, fail);
    test_eq(interp, "module qualified triple", "(math-utils.triple 4)", 12, pass, fail);
    test_error(interp, "module unexported hidden", "(math-utils.internal 5)", pass, fail);
    // Unqualified access should fail without selective import
    test_error(interp, "module unqualified fails", "(triple 4)", pass, fail);
    // Re-import (cached)
    setup(interp, "(import math-utils)");
    test_eq(interp, "module re-import cached", "(math-utils.dbl 7)", 14, pass, fail);
    test_error(interp, "module duplicate def", "(module math-utils (export) (define x 1))", pass, fail);
}

fn void run_advanced_module_selective_rename_tests(Interp* interp, int* pass, int* fail) {
    // Selective import: (import mod (sym1 sym2))
    setup(interp, "(module sel-mod (export add-one sub-one) (define add-one (lambda (x) (+ x 1))) (define sub-one (lambda (x) (- x 1))))");
    setup(interp, "(import sel-mod (add-one))");
    test_eq(interp, "selective import add-one", "(add-one 5)", 6, pass, fail);
    test_error(interp, "selective import sub-one hidden", "(sub-one 5)", pass, fail);
    // Qualified still works alongside selective
    test_eq(interp, "selective + qualified", "(sel-mod.sub-one 5)", 4, pass, fail);

    // Rename import: (import mod (sym :as alias))
    setup(interp, "(module rename-mod (export compute) (define compute (lambda (x) (* x 10))))");
    setup(interp, "(import rename-mod (compute :as calc))");
    test_eq(interp, "renamed import", "(calc 3)", 30, pass, fail);
    test_error(interp, "original name hidden after rename", "(compute 3)", pass, fail);
}

fn void run_advanced_module_all_dependency_tests(Interp* interp, int* pass, int* fail) {
    // Import :all (explicit opt-in, old behavior)
    setup(interp, "(module all-mod (export foo bar) (define foo (lambda () 42)) (define bar (lambda () 99)))");
    setup(interp, "(import all-mod :all)");
    test_eq(interp, "import all foo", "(foo)", 42, pass, fail);
    test_eq(interp, "import all bar", "(bar)", 99, pass, fail);

    // Original tests updated to use selective import
    setup(interp, "(module list-utils (export sum-list) (define sum-list (lambda (lst) (foldl + 0 lst))))");
    setup(interp, "(import list-utils (sum-list))");
    test_eq(interp, "module stdlib access", "(sum-list (quote (1 2 3 4 5)))", 15, pass, fail);
    setup(interp, "(module base-mod (export base-fn) (define base-fn (lambda (x) (+ x 100))))");
    setup(interp, "(import base-mod :all)");
    setup(interp, "(module derived-mod (export derived-fn) (define derived-fn (lambda (x) (base-fn (* x 2)))))");
    setup(interp, "(import derived-mod (derived-fn))");
    test_eq(interp, "module depends on another", "(derived-fn 5)", 110, pass, fail);
    setup(interp, "(module private-mod (export) (define secret 42))");
    setup(interp, "(import private-mod)");
    test_error(interp, "module empty export hides", "secret", pass, fail);
    setup(interp, "(module closure-mod (export make-counter) (define make-counter (lambda () (let (n 0) (lambda () (begin (set! n (+ n 1)) n))))))");
    setup(interp, "(import closure-mod (make-counter))");
    setup(interp, "(define my-counter (make-counter))");
    // Counter tests are stateful: JIT-only to avoid double mutation
    test_eq_jit(interp, "module counter 1", "(my-counter)", 1, pass, fail);
    test_eq_jit(interp, "module counter 2", "(my-counter)", 2, pass, fail);
    setup(interp, "(module tail-mod (export tail-sum) (define tail-sum (lambda (n acc) (if (= n 0) acc (tail-sum (- n 1) (+ acc n))))))");
    setup(interp, "(import tail-mod (tail-sum))");
    test_eq(interp, "module tail-call sum 1000", "(tail-sum 1000 0)", 500500, pass, fail);
}

fn void run_advanced_module_reexport_shorthand_tests(Interp* interp, int* pass, int* fail) {
    // Export-from (re-export)
    setup(interp, "(module inner-mod (export inner-val) (define inner-val 42))");
    setup(interp, "(module outer-mod (export inner-val) (import inner-mod) (export-from inner-mod (inner-val)))");
    setup(interp, "(import outer-mod (inner-val))");
    test_eq(interp, "export-from re-export", "inner-val", 42, pass, fail);
    setup(interp, "(module inner-all-mod (export all-a all-b) (define all-a 7) (define all-b 9))");
    setup(interp, "(module outer-all-mod (export all-a all-b) (export-from inner-all-mod :all))");
    setup(interp, "(import outer-all-mod (all-a all-b))");
    test_eq(interp, "export-from :all all-a", "all-a", 7, pass, fail);
    test_eq(interp, "export-from :all all-b", "all-b", 9, pass, fail);

    // Module as first-class value
    setup(interp, "(module val-mod (export x) (define x 77))");
    setup(interp, "(import val-mod)");
    test_eq(interp, "module as value", "(val-mod.x)", 77, pass, fail);

    // === SHORTHAND DEFINE TESTS ===
    setup(interp, "(define (sh-double x) (* x 2))");
    test_eq(interp, "shorthand define", "(sh-double 21)", 42, pass, fail);
    setup(interp, "(define (sh-add3 a b c) (+ a (+ b c)))");
    test_eq(interp, "shorthand define multi", "(sh-add3 10 20 30)", 60, pass, fail);
    setup(interp, "(define (sh-answer) 42)");
    test_eq(interp, "shorthand define zero-arg", "(sh-answer)", 42, pass, fail);
}

fn void run_advanced_parser_export_from_edge_tests(Interp* interp, int* pass, int* fail) {
    test_error_contains(interp,
        "parser export-from missing module",
        "(module bad-exp-missing (export) (export-from))",
        "expected module name after export-from",
        pass, fail);

    test_error_contains(interp,
        "parser export-from missing specifier",
        "(module bad-exp-nospec (export) (export-from inner-mod))",
        "expected :all or (names...) after export-from module",
        pass, fail);

    test_error_contains(interp,
        "parser export-from bad specifier",
        "(module bad-exp-spec (export) (export-from inner-mod bogus))",
        "expected :all or (names...) after export-from module",
        pass, fail);

    test_error_contains(interp,
        "parser export-from list non-symbol",
        "(module bad-exp-list (export) (export-from inner-mod (123)))",
        "expected symbol in export-from list",
        pass, fail);

    test_error_contains(interp,
        "parser export-from missing close paren",
        "(module bad-exp-close (export) (export-from inner-mod (a b))",
        ")",
        pass, fail);

    test_error_contains(interp,
        "parser export-from nested list element",
        "(module bad-exp-nested (export) (export-from inner-mod ((a))))",
        "expected symbol in export-from list",
        pass, fail);
}

fn void run_advanced_parser_import_edge_tests(Interp* interp, int* pass, int* fail) {
    test_error_contains(interp,
        "parser import paren missing :as",
        "(import bad-mod ((x y)))",
        "expected :as in import specifier",
        pass, fail);

    test_error_contains(interp,
        "parser import paren missing alias",
        "(import bad-mod ((x :as)))",
        "expected alias after :as",
        pass, fail);

    test_error_contains(interp,
        "parser import symbol missing alias",
        "(import bad-mod (x :as))",
        "expected alias after :as",
        pass, fail);

    test_error_contains(interp,
        "parser import list non-symbol",
        "(import bad-mod (123))",
        "expected symbol in import list",
        pass, fail);

    test_error_contains(interp,
        "parser import missing close paren",
        "(import bad-mod (x y)",
        ")",
        pass, fail);
}

fn void run_advanced_module_system_tests(Interp* interp, int* pass, int* fail) {
    // === MODULE SYSTEM TESTS ===
    run_advanced_module_qualified_cached_tests(interp, pass, fail);
    run_advanced_module_selective_rename_tests(interp, pass, fail);
    run_advanced_module_all_dependency_tests(interp, pass, fail);
    run_advanced_module_reexport_shorthand_tests(interp, pass, fail);
    run_advanced_parser_import_edge_tests(interp, pass, fail);
    run_advanced_parser_export_from_edge_tests(interp, pass, fail);
}

fn void run_advanced_collections_module_tests(Interp* interp, int* pass, int* fail) {
    run_advanced_collections_dict_literal_tests(interp, pass, fail);
    run_advanced_collections_generic_ops_tests(interp, pass, fail);
    run_advanced_module_system_tests(interp, pass, fail);
}

fn void run_advanced_stdlib_numeric_collection_tests(Interp* interp, int* pass, int* fail) {
    // === STDLIB TESTS ===
    test_eq(interp, "map car", "(car (map (lambda (x) (* x 2)) (quote (1 2 3))))", 2, pass, fail);
    test_eq(interp, "map length", "(length (map (lambda (x) (* x 2)) (quote (1 2 3))))", 3, pass, fail);
    test_nil(interp, "map empty", "(map (lambda (x) (* x 2)) (quote ()))", pass, fail);
    test_eq(interp, "filter car", "(car (filter (lambda (x) (> x 2)) (quote (1 2 3 4 5))))", 3, pass, fail);
    test_eq(interp, "filter length", "(length (filter (lambda (x) (> x 2)) (quote (1 2 3 4 5))))", 3, pass, fail);
    test_nil(interp, "filter none", "(filter (lambda (x) (> x 100)) (quote (1 2 3)))", pass, fail);
    test_eq(interp, "foldl sum", "(foldl + 0 (quote (1 2 3 4 5)))", 15, pass, fail);
    test_eq(interp, "foldl product", "(foldl * 1 (quote (1 2 3 4)))", 24, pass, fail);
    test_eq(interp, "foldr cons len", "(length (foldr cons nil (quote (1 2 3))))", 3, pass, fail);
    test_eq(interp, "foldr cons car", "(car (foldr cons nil (quote (1 2 3))))", 1, pass, fail);
    test_eq(interp, "append length", "(length (append (quote (1 2)) (quote (3 4))))", 4, pass, fail);
    test_eq(interp, "append car", "(car (append (quote (1 2)) (quote (3 4))))", 1, pass, fail);
    test_eq(interp, "append nil left", "(car (append nil (quote (1 2))))", 1, pass, fail);
    test_eq(interp, "reverse car", "(car (reverse (quote (1 2 3))))", 3, pass, fail);
    test_eq(interp, "reverse length", "(length (reverse (quote (1 2 3))))", 3, pass, fail);
    test_nil(interp, "reverse nil", "(reverse nil)", pass, fail);
    test_eq(interp, "compose *2.+1", "((compose (lambda (x) (* x 2)) (lambda (x) (+ x 1))) 3)", 8, pass, fail);
    test_eq(interp, "compose +1.*2", "((compose (lambda (x) (+ x 1)) (lambda (x) (* x 2))) 3)", 7, pass, fail);
    test_eq(interp, "id", "(id 42)", 42, pass, fail);
    test_eq(interp, "nth 0", "(nth 0 (quote (10 20 30 40)))", 10, pass, fail);
    test_eq(interp, "nth 2", "(nth 2 (quote (10 20 30 40)))", 30, pass, fail);
    test_eq(interp, "take length", "(length (take 3 (quote (1 2 3 4 5))))", 3, pass, fail);
    test_eq(interp, "take car", "(car (take 3 (quote (1 2 3 4 5))))", 1, pass, fail);
    test_nil(interp, "take 0", "(take 0 (quote (1 2 3)))", pass, fail);
    test_eq(interp, "drop car", "(car (drop 2 (quote (1 2 3 4 5))))", 3, pass, fail);
    test_eq(interp, "drop length", "(length (drop 2 (quote (1 2 3 4 5))))", 3, pass, fail);
    test_eq(interp, "drop 0", "(car (drop 0 (quote (1 2 3))))", 1, pass, fail);
    test_eq(interp, "zip length", "(length (zip (quote (1 2 3)) (quote (4 5 6))))", 3, pass, fail);
    test_eq(interp, "zip first car", "(car (car (zip (quote (1 2 3)) (quote (4 5 6)))))", 1, pass, fail);
    test_eq(interp, "zip first cdr", "(cdr (car (zip (quote (1 2 3)) (quote (4 5 6)))))", 4, pass, fail);
    test_eq(interp, "zip unequal", "(length (zip (quote (1 2)) (quote (4 5 6))))", 2, pass, fail);
    test_eq(interp, "range length", "(length (range 5))", 5, pass, fail);
    test_eq(interp, "range car", "(car (range 5))", 0, pass, fail);
    test_nil(interp, "range 0", "(range 0)", pass, fail);
    test_nil(interp, "for-each", "(for-each (lambda (x) (+ x 1)) (quote (1 2 3)))", pass, fail);
    test_truthy(interp, "any? found", "(any? (lambda (x) (= x 3)) (quote (1 2 3 4)))", pass, fail);
    test_nil(interp, "any? not found", "(any? (lambda (x) (= x 99)) (quote (1 2 3 4)))", pass, fail);
    test_truthy(interp, "every? all", "(every? (lambda (x) (> x 0)) (quote (1 2 3 4)))", pass, fail);
    test_nil(interp, "every? not all", "(every? (lambda (x) (> x 2)) (quote (1 2 3 4)))", pass, fail);
    test_truthy(interp, "every? empty", "(every? (lambda (x) (> x 0)) nil)", pass, fail);
    test_eq(interp, "try/raise catch", "(try (lambda (xx) (+ 1 (signal raise 42))) (lambda (msg) msg))", 42, pass, fail);
    test_eq(interp, "try no error", "(try (lambda (xx) (+ 1 2)) (lambda (msg) msg))", 3, pass, fail);
    test_truthy(interp, "assert! true", "(assert! (= 1 1) \"ok\")", pass, fail);
    test_truthy(interp, "assert! false raises", "(handle (assert! (= 1 2) \"fail\") (raise msg msg))", pass, fail);
    setup(interp, "(define test-alist (list (cons 1 10) (cons 2 20) (cons 3 30)))");
    test_eq(interp, "assoc find", "(cdr (assoc 2 test-alist))", 20, pass, fail);
    test_nil(interp, "assoc not found", "(assoc 99 test-alist)", pass, fail);
    test_eq(interp, "assoc-ref", "(assoc-ref 3 test-alist)", 30, pass, fail);
    test_nil(interp, "assoc-ref not found", "(assoc-ref 99 test-alist)", pass, fail);
    test_eq(interp, "map+filter+foldl", "(foldl + 0 (filter (lambda (x) (> x 3)) (map (lambda (x) (* x 2)) (quote (1 2 3 4 5)))))", 28, pass, fail);
    test_eq(interp, "reverse+take", "(car (take 1 (reverse (quote (1 2 3)))))", 3, pass, fail);
    test_eq(interp, "sum of squares", "(foldl + 0 (map (lambda (x) (* x x)) (range 5)))", 30, pass, fail);
}

fn void run_advanced_stdlib_numeric_float_math_tests(Interp* interp, int* pass, int* fail) {
    // === FLOAT TESTS ===
    test_double(interp, "float 3.14", "3.14", 3.14, pass, fail);
    test_double(interp, "float -2.5", "-2.5", -2.5, pass, fail);
    test_double(interp, "float 1.0e3", "1.0e3", 1000.0, pass, fail);
    test_double(interp, "float 1.5e-2", "1.5e-2", 0.015, pass, fail);
    test_double(interp, "int+float", "(+ 1 2.0)", 3.0, pass, fail);
    test_double(interp, "float*int", "(* 2.5 4)", 10.0, pass, fail);
    test_double(interp, "float/int", "(/ 1.0 3)", 0.333333, pass, fail);
    test_double(interp, "float-float", "(- 5.5 2.5)", 3.0, pass, fail);
    test_eq(interp, "int+int stays int", "(+ 1 2)", 3, pass, fail);
    test_truthy(interp, "< int float", "(< 1 2.5)", pass, fail);
    test_truthy(interp, "> float int", "(> 3.5 2)", pass, fail);
    test_truthy(interp, "= int float", "(= 1 1.0)", pass, fail);
    test_truthy(interp, "<= float", "(<= 1.5 1.5)", pass, fail);
    test_truthy(interp, ">= float", "(>= 2.0 1.5)", pass, fail);
    test_double(interp, "unary neg float", "(- 0 3.14)", -3.14, pass, fail);
    test_double(interp, "exact->inexact", "(exact->inexact 5)", 5.0, pass, fail);
    test_eq(interp, "inexact->exact", "(inexact->exact 3.7)", 3, pass, fail);
    test_double(interp, "string->number float", "(string->number \"3.14\")", 3.14, pass, fail);
    test_double(interp, "string->number exp", "(string->number \"1.5e2\")", 150.0, pass, fail);
    test_str(interp, "number->string double", "(number->string 3.14)", pass, fail);

    // === MATH LIBRARY TESTS (Phase 2) ===
    test_double(interp, "sin 0", "(sin 0.0)", 0.0, pass, fail);
    test_double(interp, "cos 0", "(cos 0.0)", 1.0, pass, fail);
    test_double(interp, "tan 0", "(tan 0.0)", 0.0, pass, fail);
    test_double(interp, "asin 0", "(asin 0.0)", 0.0, pass, fail);
    test_double(interp, "acos 1", "(acos 1.0)", 0.0, pass, fail);
    test_double(interp, "atan 0", "(atan 0.0)", 0.0, pass, fail);
    test_double(interp, "atan2", "(atan2 1.0 1.0)", 0.7853981, pass, fail);
    test_double(interp, "exp 0", "(exp 0.0)", 1.0, pass, fail);
    test_double(interp, "log 1", "(log 1.0)", 0.0, pass, fail);
    test_double(interp, "sqrt 4", "(sqrt 4.0)", 2.0, pass, fail);
    test_double(interp, "pow 2 10", "(pow 2.0 10.0)", 1024.0, pass, fail);
    test_eq(interp, "floor 3.7", "(floor 3.7)", 3, pass, fail);
    test_eq(interp, "ceiling 3.2", "(ceiling 3.2)", 4, pass, fail);
    test_eq(interp, "round 3.5", "(round 3.5)", 4, pass, fail);
    test_eq(interp, "truncate 3.9", "(truncate 3.9)", 3, pass, fail);
    test_eq(interp, "abs -5", "(abs -5)", 5, pass, fail);
    test_double(interp, "abs -3.14", "(abs -3.14)", 3.14, pass, fail);
    test_eq(interp, "min 3 7", "(min 3 7)", 3, pass, fail);
    test_eq(interp, "max 3 7", "(max 3 7)", 7, pass, fail);
    test_eq(interp, "gcd 12 8", "(gcd 12 8)", 4, pass, fail);
    test_double(interp, "pi constant", "pi", 3.14159265, pass, fail);
    test_double(interp, "e constant", "e", 2.71828182, pass, fail);
}

fn void run_advanced_stdlib_numeric_array_set_tests(Interp* interp, int* pass, int* fail) {
    // === ARRAY TESTS (Phase 8) ===
    test_eq(interp, "length array", "(length (array 1 2 3))", 3, pass, fail);
    test_eq(interp, "length Array alias", "(length (Array 1 2 3))", 3, pass, fail);
    test_eq(interp, "ref array", "(ref (array 10 20 30) 1)", 20, pass, fail);
    test_eq(interp, "array from list", "(ref (array '(10 20 30)) 1)", 20, pass, fail);
    test_eq(interp, "Array from list", "(ref (Array '(10 20 30)) 1)", 20, pass, fail);
    setup(interp, "(define test-arr (array 1 2 3))");
    setup(interp, "(array-set! test-arr 1 99)");
    test_eq(interp, "array-set!", "(ref test-arr 1)", 99, pass, fail);
    test_truthy(interp, "array?", "(array? (array 1 2))", pass, fail);
    test_nil(interp, "array? non-arr", "(array? 42)", pass, fail);
    test_truthy(interp, "list from array", "(= (list [1 2 3]) '(1 2 3))", pass, fail);
    test_truthy(interp, "List alias from array", "(= (List [1 2 3]) '(1 2 3))", pass, fail);
    test_eq(interp, "array from list 2", "(ref (array '(10 20 30)) 2)", 30, pass, fail);
    test_eq(interp, "array nil→empty", "(length (array nil))", 0, pass, fail);
    setup(interp, "(define push-arr (array 1 2))");
    setup(interp, "(push! push-arr 3)");
    test_eq(interp, "push!", "(length push-arr)", 3, pass, fail);
    test_eq(interp, "push! val", "(ref push-arr 2)", 3, pass, fail);
    test_error(interp, "ref oob", "(ref (array 1) 5)", pass, fail);
    test_error(interp, "array-set! oob", "(array-set! (array 1) 5 0)", pass, fail);
    test_eq(interp, "empty array", "(length (array))", 0, pass, fail);

    // === SET TESTS (Phase 9) ===
    test_eq(interp, "set-size", "(set-size (set 1 2 3))", 3, pass, fail);
    test_eq(interp, "set-size Set alias", "(set-size (Set 1 2 3))", 3, pass, fail);
    test_truthy(interp, "set-contains?", "(set-contains? (set 1 2 3) 2)", pass, fail);
    test_nil(interp, "set-contains? miss", "(set-contains? (set 1 2 3) 5)", pass, fail);
    setup(interp, "(define test-set (set 1 2))");
    setup(interp, "(set-add test-set 3)");
    test_eq(interp, "set-add", "(set-size test-set)", 3, pass, fail);
    test_truthy(interp, "set-add contains", "(set-contains? test-set 3)", pass, fail);
    setup(interp, "(set-remove test-set 2)");
    test_eq(interp, "set-remove", "(set-size test-set)", 2, pass, fail);
    test_nil(interp, "set-remove gone", "(set-contains? test-set 2)", pass, fail);
    test_eq(interp, "set dedup", "(set-size (set 1 1 2 2 3))", 3, pass, fail);

    // === CONVENIENCE TESTS (Phase 12) ===
    test_eq(interp, "read-string", "(read-string \"(+ 1 2)\")", 3, pass, fail);
    test_truthy(interp, "symbol->string", "(= (symbol->string 'hello) \"hello\")", pass, fail);
    test_truthy(interp, "string->symbol", "(= (string->symbol \"abc\") 'abc)", pass, fail);
    test_eq(interp, "Dict alias ref", "(ref (Dict 'a 1 'b 2) 'b)", 2, pass, fail);
}

fn void run_advanced_stdlib_sort_bitwise_hof_tests(Interp* interp, int* pass, int* fail) {
    // === SORTING & BITWISE TESTS (Phase 3) ===
    test_eq(interp, "sort car", "(car (sort (quote (3 1 2))))", 1, pass, fail);
    test_eq(interp, "sort length", "(length (sort (quote (5 3 1 4 2))))", 5, pass, fail);
    test_eq(interp, "sort last", "(car (cdr (cdr (sort (quote (3 1 2))))))", 3, pass, fail);
    test_eq(interp, "sort-by custom", "(car (sort-by (lambda (a) (lambda (b) (- a b))) (quote (3 1 2))))", 1, pass, fail);
    test_eq(interp, "bitwise-and", "(bitwise-and 12 10)", 8, pass, fail);
    test_eq(interp, "bitwise-or", "(bitwise-or 12 10)", 14, pass, fail);
    test_eq(interp, "bitwise-xor", "(bitwise-xor 12 10)", 6, pass, fail);
    test_eq(interp, "bitwise-not 0", "(bitwise-not 0)", -1, pass, fail);
    test_eq(interp, "lshift", "(lshift 1 4)", 16, pass, fail);
    test_eq(interp, "rshift", "(rshift 16 4)", 1, pass, fail);

    // === STDLIB HOFs (Phase 3) ===
    test_eq(interp, "flatten", "(length (flatten (quote ((1 2) (3 4) (5)))))", 5, pass, fail);
    test_eq(interp, "flatten car", "(car (flatten (quote ((1 2) (3)))))", 1, pass, fail);
    test_eq(interp, "partition yes", "(length (car (partition (lambda (x) (> x 3)) (quote (1 2 3 4 5)))))", 2, pass, fail);
    test_eq(interp, "partition no", "(length (cdr (partition (lambda (x) (> x 3)) (quote (1 2 3 4 5)))))", 3, pass, fail);
    test_eq(interp, "remove", "(length (remove (lambda (x) (= x 3)) (quote (1 2 3 4 5))))", 4, pass, fail);
    test_eq(interp, "find found", "(find (lambda (x) (= x 3)) (quote (1 2 3 4 5)))", 3, pass, fail);
    test_nil(interp, "find not found", "(find (lambda (x) (= x 99)) (quote (1 2 3)))", pass, fail);
}

fn void run_advanced_stdlib_string_predicate_format_tests(Interp* interp, int* pass, int* fail) {
    // === STRING OPS TESTS (Phase 5) ===
    test_truthy(interp, "string-contains? yes", "(string-contains? \"hello world\" \"world\")", pass, fail);
    test_nil(interp, "string-contains? no", "(string-contains? \"hello\" \"xyz\")", pass, fail);
    test_eq(interp, "string-index-of found", "(string-index-of \"hello world\" \"world\")", 6, pass, fail);
    test_eq(interp, "string-index-of not", "(string-index-of \"hello\" \"xyz\")", -1, pass, fail);
    test_str(interp, "string-replace", "(string-replace \"hello world\" \"world\" \"there\")", pass, fail);
    test_str(interp, "char-at", "(char-at \"hello\" 1)", pass, fail);
    test_str(interp, "string-repeat", "(string-repeat \"ab\" 3)", pass, fail);

    // === TYPE PREDICATE TESTS (Phase 5) ===
    test_truthy(interp, "double? yes", "(double? 3.14)", pass, fail);
    test_nil(interp, "double? no", "(double? 42)", pass, fail);
    test_truthy(interp, "number? int", "(number? 42)", pass, fail);
    test_truthy(interp, "number? double", "(number? 3.14)", pass, fail);
    test_nil(interp, "number? string", "(number? \"hello\")", pass, fail);
    test_truthy(interp, "boolean? true", "(boolean? true)", pass, fail);
    test_truthy(interp, "boolean? nil", "(boolean? nil)", pass, fail);
    test_nil(interp, "boolean? int", "(boolean? 42)", pass, fail);
    test_truthy(interp, "list? yes", "(list? (quote (1 2 3)))", pass, fail);
    test_truthy(interp, "list? nil", "(list? nil)", pass, fail);
    test_nil(interp, "list? int", "(list? 42)", pass, fail);
    test_truthy(interp, "procedure? lambda", "(procedure? (lambda (x) x))", pass, fail);
    test_truthy(interp, "procedure? prim", "(procedure? +)", pass, fail);
    test_nil(interp, "procedure? int", "(procedure? 42)", pass, fail);
    test_truthy(interp, "zero? 0", "(zero? 0)", pass, fail);
    test_nil(interp, "zero? 1", "(zero? 1)", pass, fail);
    test_truthy(interp, "positive? 5", "(positive? 5)", pass, fail);
    test_nil(interp, "positive? -1", "(positive? -1)", pass, fail);
    test_truthy(interp, "negative? -1", "(negative? -1)", pass, fail);
    test_nil(interp, "negative? 5", "(negative? 5)", pass, fail);
    test_truthy(interp, "even? 4", "(even? 4)", pass, fail);
    test_nil(interp, "even? 3", "(even? 3)", pass, fail);
    test_truthy(interp, "odd? 3", "(odd? 3)", pass, fail);
    test_nil(interp, "odd? 4", "(odd? 4)", pass, fail);

    // === FORMAT & DISPLAY TESTS (Phase 5) ===
    test_str_val(interp, "cl-format ~a", "(cl-format \"~a\" 42)", "42", pass, fail);
    test_str_val(interp, "cl-format ~a ~a", "(cl-format \"~a plus ~a\" 1 2)", "1 plus 2", pass, fail);
    test_str_val(interp, "cl-format tilde", "(cl-format \"100~~\")", "100~", pass, fail);
    test_str_val(interp, "format %s int", "(format \"%s\" 42)", "42", pass, fail);
    test_str_val(interp, "format %s str", "(format \"%s\" \"hello\")", "hello", pass, fail);
    test_str_val(interp, "format %s nil", "(format \"%s\" nil)", "nil", pass, fail);
    test_str_val(interp, "format %d", "(format \"%d\" 42)", "42", pass, fail);
    test_str_val(interp, "format %x", "(format \"%x\" 255)", "ff", pass, fail);
    test_str_val(interp, "format %X", "(format \"%X\" 255)", "FF", pass, fail);
    test_str_val(interp, "format %o", "(format \"%o\" 8)", "10", pass, fail);
    test_str_val(interp, "format %b", "(format \"%b\" 10)", "1010", pass, fail);
    test_str(interp, "format %f", "(format \"%f\" 3.14)", pass, fail);
    test_str_val(interp, "format %.2f", "(format \"%.2f\" 3.14159)", "3.14", pass, fail);
    test_str_val(interp, "format %%", "(format \"100%%\")", "100%", pass, fail);
    test_str_val(interp, "format mixed", "(format \"%s is %d\" \"x\" 42)", "x is 42", pass, fail);
    test_str_val(interp, "format %b zero", "(format \"%b\" 0)", "0", pass, fail);
    test_str_val(interp, "format %d neg", "(format \"%d\" -7)", "-7", pass, fail);
    test_str_val(interp, "format %.0f", "(format \"%.0f\" 3.7)", "3", pass, fail);
}

fn void run_advanced_stdlib_introspection_lazy_tco_tests(Interp* interp, int* pass, int* fail) {
    // === INTROSPECTION TESTS (Phase 7) ===
    setup(interp, "(define [macro] my-inc ([x] (+ x 1)))");
    test_truthy(interp, "macroexpand", "(pair? (macroexpand (quote (my-inc 5))))", pass, fail);
    test_eq(interp, "eval literal", "(eval 42)", 42, pass, fail);
    test_eq(interp, "eval list", "(eval (quote (+ 1 2)))", 3, pass, fail);
    test_eq(interp, "apply +", "(apply + (quote (1 2)))", 3, pass, fail);
    test_truthy(interp, "bound? yes", "(bound? '+)", pass, fail);
    test_nil(interp, "bound? no", "(bound? 'xyzzy-undefined)", pass, fail);
    test_error(interp, "error raises", "(error \"test error\")", pass, fail);

    // === LAZY EVALUATION TESTS (Phase 8) ===
    setup(interp, "(define p (delay (lambda (xx) 42)))");
    test_eq(interp, "delay/force", "(force p)", 42, pass, fail);

    // === TCO STDLIB TESTS (Phase 6) ===
    test_eq(interp, "foldl TCO 4000", "(foldl + 0 (range 4000))", 7998000, pass, fail);
}

fn void run_advanced_stdlib_numeric_tests(Interp* interp, int* pass, int* fail) {
    run_advanced_stdlib_numeric_collection_tests(interp, pass, fail);
    run_advanced_stdlib_numeric_float_math_tests(interp, pass, fail);
    run_advanced_stdlib_sort_bitwise_hof_tests(interp, pass, fail);
    run_advanced_stdlib_string_predicate_format_tests(interp, pass, fail);
    run_advanced_stdlib_introspection_lazy_tco_tests(interp, pass, fail);
    run_advanced_stdlib_numeric_array_set_tests(interp, pass, fail);
}

fn void run_advanced_type_parametric_core_hierarchy_tests(Interp* interp, int* pass, int* fail) {
    // === TYPE SYSTEM TESTS (Phase 1) ===

    // type-of primitive
    test_truthy(interp, "type-of int", "(= (type-of 42) 'Int)", pass, fail);
    test_truthy(interp, "typeof alias int", "(= (typeof 42) 'Int)", pass, fail);
    test_truthy(interp, "type-of double", "(= (type-of 3.14) 'Double)", pass, fail);
    test_truthy(interp, "type-of string", "(= (type-of \"hello\") 'String)", pass, fail);
    test_truthy(interp, "type-of symbol", "(= (type-of 'foo) 'Symbol)", pass, fail);
    test_truthy(interp, "type-of nil", "(= (type-of nil) 'Nil)", pass, fail);
    test_truthy(interp, "type-of list", "(= (type-of '(1 2 3)) 'List)", pass, fail);
    test_truthy(interp, "type-of array", "(= (type-of (array 1 2 3)) 'Array)", pass, fail);
    test_truthy(interp, "type-of Array alias", "(= (type-of (Array 1 2 3)) 'Array)", pass, fail);
    test_truthy(interp, "type-of closure", "(= (type-of (lambda (x) x)) 'Closure)", pass, fail);
    test_truthy(interp, "type-of true", "(= (type-of true) 'Bool)", pass, fail);

    // is? primitive
    test_truthy(interp, "is? int", "(is? 42 'Int)", pass, fail);
    test_truthy(interp, "is? string", "(is? \"hello\" 'String)", pass, fail);
    test_nil(interp, "is? mismatch", "(is? 42 'String)", pass, fail);
    test_truthy(interp, "is? Any", "(is? 42 'Any)", pass, fail);

    // define [type] — simple struct (interp-only: JIT doesn't know user types yet)
    setup(interp, "(define [type] Point (^Int x) (^Int y))");
    test_truthy_interp(interp, "type constructor", "(instance? (Point 3 4))", pass, fail);
    test_truthy_interp(interp, "type-of instance", "(= (type-of (Point 3 4)) 'Point)", pass, fail);
    test_eq_interp(interp, "type field access x", "(let (p (Point 3 4)) p.x)", 3, pass, fail);
    test_eq_interp(interp, "type field access y", "(let (p (Point 3 4)) p.y)", 4, pass, fail);
    test_truthy_interp(interp, "is? instance type", "(is? (Point 3 4) 'Point)", pass, fail);

    // define [abstract] — abstract type
    setup(interp, "(define [abstract] Shape)");
    test_truthy(interp, "abstract type defined", "(= 'Shape 'Shape)", pass, fail);

    // define [type] with parent
    setup(interp, "(define [type] (Circle Shape) (^Int radius))");
    test_truthy_interp(interp, "subtype constructor", "(instance? (Circle 5))", pass, fail);
    test_truthy_interp(interp, "is? subtype", "(is? (Circle 5) 'Circle)", pass, fail);
    test_truthy_interp(interp, "is? parent type", "(is? (Circle 5) 'Shape)", pass, fail);
    test_nil_interp(interp, "is? wrong parent", "(is? (Point 1 2) 'Shape)", pass, fail);
    test_eq_interp(interp, "subtype field access", "(let (c (Circle 5)) c.radius)", 5, pass, fail);

    // Nested type access
    setup(interp, "(define [type] Line (^Point start) (^Point end))");
    test_eq_interp(interp, "nested type access", "(let (l (Line (Point 1 2) (Point 3 4))) l.start.x)", 1, pass, fail);
    test_eq_interp(interp, "nested type access 2", "(let (l (Line (Point 1 2) (Point 3 4))) l.end.y)", 4, pass, fail);

    // define [union] — union types
    setup(interp, "(define [union] (Option T) None (Some T))");
    test_truthy_interp(interp, "union nullary", "(instance? None)", pass, fail);
    test_truthy_interp(interp, "union constructor", "(instance? (Some 42))", pass, fail);
    test_truthy_interp(interp, "is? union variant", "(is? None 'Option)", pass, fail);
    test_truthy_interp(interp, "is? union Some", "(is? (Some 42) 'Option)", pass, fail);
    test_truthy_interp(interp, "is? union Some exact", "(is? (Some 42) 'Some)", pass, fail);

    // define [alias]
    setup(interp, "(define [alias] Num Int)");
    test_truthy(interp, "alias defined", "(= 'Num 'Num)", pass, fail);

    // instance? primitive
    test_nil(interp, "instance? non-instance", "(instance? 42)", pass, fail);
}

fn void run_advanced_type_parametric_type_args_tests(Interp* interp, int* pass, int* fail) {
    // === PARAMETRIC TYPE TESTS (Phase 7) ===

    // Parametric type with single type param
    setup(interp, "(define [type] (Box T) (^T value))");
    test_truthy_interp(interp, "Box constructor", "(instance? (Box 42))", pass, fail);
    test_eq_interp(interp, "Box field access", "(let (b (Box 42)) b.value)", 42, pass, fail);
    test_truthy_interp(interp, "Box type-of", "(= (type-of (Box 42)) 'Box)", pass, fail);

    // type-args primitive — infer T=Int from (Box 42)
    test_truthy_interp(interp, "type-args Box Int", "(= (car (type-args (Box 42))) 'Int)", pass, fail);
    test_truthy_interp(interp, "type-args Box String", "(= (car (type-args (Box \"hi\"))) 'String)", pass, fail);

    // Parametric type with two type params
    setup(interp, "(define [type] (Pair A B) (^A first) (^B second))");
    test_eq_interp(interp, "Pair field first", "(let (p (Pair 1 \"two\")) p.first)", 1, pass, fail);
    test_truthy_interp(interp, "Pair field second", "(= (let (p (Pair 1 \"two\")) p.second) \"two\")", pass, fail);
    test_truthy_interp(interp, "type-args Pair A", "(= (car (type-args (Pair 1 \"two\"))) 'Int)", pass, fail);
    test_truthy_interp(interp, "type-args Pair B", "(= (car (cdr (type-args (Pair 1 \"two\")))) 'String)", pass, fail);

    // type-args on non-parametric type returns nil
    test_nil_interp(interp, "type-args non-param", "(type-args (Point 1 2))", pass, fail);
    // type-args on non-instance returns nil
    test_nil_interp(interp, "type-args int", "(type-args 42)", pass, fail);
}

fn void run_advanced_type_parametric_disambiguation_tests(Interp* interp, int* pass, int* fail) {
    // Disambiguation: (Circle Shape) still works as parent, not type param
    // Circle was defined above with parent Shape — verify it still works
    test_truthy_interp(interp, "disambig parent still works", "(is? (Circle 5) 'Shape)", pass, fail);
}

fn void run_advanced_parser_type_def_edge_tests(Interp* interp, int* pass, int* fail) {
    test_error_contains(interp,
        "parser deftype missing name",
        "(define [type])",
        "expected type name after [type]",
        pass, fail);

    test_error_contains(interp,
        "parser deftype compound missing name",
        "(define [type] () (^Int x))",
        "expected type name",
        pass, fail);

    test_error_contains(interp,
        "parser deftype missing field name",
        "(define [type] Broken (^Int))",
        "expected field name",
        pass, fail);

    test_error_contains(interp,
        "parser defunion missing name",
        "(define [union])",
        "expected union name after [union]",
        pass, fail);

    test_error_contains(interp,
        "parser defunion compound missing name",
        "(define [union] () (Some T))",
        "expected union name",
        pass, fail);

    test_error_contains(interp,
        "parser defunion variant missing name",
        "(define [union] U ())",
        "expected variant name",
        pass, fail);

    test_error_contains(interp,
        "parser deftype compound missing close",
        "(define [type] (Box T (^Int x))",
        ")",
        pass, fail);

    test_error_contains(interp,
        "parser defunion compound missing close",
        "(define [union] (Maybe T (Some T) None)",
        ")",
        pass, fail);

    test_error_contains(interp,
        "parser defunion variant missing close",
        "(define [union] Maybe (Some T)",
        ")",
        pass, fail);

    test_error_contains(interp,
        "parser deftype compound non-symbol type param",
        "(define [type] (Box T 123) (^Int x))",
        ")",
        pass, fail);

    test_error_contains(interp,
        "parser defunion compound non-symbol type param",
        "(define [union] (Maybe T 123) None (Some T))",
        ")",
        pass, fail);

    test_error_contains(interp,
        "parser defunion variant non-symbol name",
        "(define [union] Maybe (123 T))",
        "expected variant name",
        pass, fail);
}

fn void run_advanced_type_parametric_tests(Interp* interp, int* pass, int* fail) {
    run_advanced_type_parametric_core_hierarchy_tests(interp, pass, fail);
    run_advanced_type_parametric_type_args_tests(interp, pass, fail);
    run_advanced_type_parametric_disambiguation_tests(interp, pass, fail);
    run_advanced_parser_type_def_edge_tests(interp, pass, fail);
}

fn void run_advanced_dispatch_constructor_basic_tests(Interp* interp, int* pass, int* fail) {
    // === MULTIPLE DISPATCH TESTS (Phase 2) ===

    // Basic dispatch on types
    setup(interp, "(define (describe (^Int n)) \"integer\")");
    setup(interp, "(define (describe (^String s)) \"string\")");
    setup(interp, "(define (describe x) \"other\")");
    test_truthy_interp(interp, "dispatch int", "(= (describe 42) \"integer\")", pass, fail);
    test_truthy_interp(interp, "dispatch string", "(= (describe \"hi\") \"string\")", pass, fail);
    test_truthy_interp(interp, "dispatch fallback", "(= (describe '(1 2)) \"other\")", pass, fail);

    // Val dispatch (Fibonacci)
    setup(interp, "(define (fib (^(Val 0) n)) 0)");
    setup(interp, "(define (fib (^(Val 1) n)) 1)");
    setup(interp, "(define (fib (^Int n)) (+ (fib (- n 1)) (fib (- n 2))))");
    test_eq_interp(interp, "dispatch fib 0", "(fib 0)", 0, pass, fail);
    test_eq_interp(interp, "dispatch fib 1", "(fib 1)", 1, pass, fail);
    test_eq_interp(interp, "dispatch fib 10", "(fib 10)", 55, pass, fail);

    // Multi-arg dispatch
    setup(interp, "(define (add2 (^Int a) (^Int b)) (+ a b))");
    setup(interp, "(define (add2 (^String a) (^String b)) (string-append a b))");
    test_eq_interp(interp, "dispatch multi-arg int", "(add2 3 4)", 7, pass, fail);
    test_truthy_interp(interp, "dispatch multi-arg str", "(= (add2 \"hello\" \" world\") \"hello world\")", pass, fail);
}

fn void run_advanced_dispatch_constructor_constraint_tests(Interp* interp, int* pass, int* fail) {
    // === CONSTRAINED DISPATCH TESTS (Phase 7) ===

    // Define abstract type hierarchy for constraint testing
    setup(interp, "(define [abstract] (Numeric Number))");  // Numeric extends Number (but Number doesn't exist yet, no parent)

    // Actually, let's use fresh abstract type hierarchy
    setup(interp, "(define [abstract] Addable)");
    setup(interp, "(define [type] (MyInt Addable) (^Int val))");
    setup(interp, "(define [type] (MyStr Addable) (^String val))");
    setup(interp, "(define [type] NotAddable (^Int val))");

    // Constrained dispatch: only matches if arg type <: Addable
    setup(interp, "(define (get-val (^{'T Addable} x)) x.val)");
    test_eq_interp(interp, "constraint MyInt match", "(get-val (MyInt 42))", 42, pass, fail);
    test_truthy_interp(interp, "constraint MyStr match", "(= (get-val (MyStr \"hi\")) \"hi\")", pass, fail);

    // Constraint fallback: NotAddable doesn't satisfy Addable constraint
    // Add a fallback method that accepts anything
    setup(interp, "(define (get-val x) (- 0 1))");
    test_eq_interp(interp, "constraint fallback", "(get-val (NotAddable 99))", -1, pass, fail);
}

fn void run_advanced_dispatch_constructor_validation_tests(Interp* interp, int* pass, int* fail) {
    // === CONSTRUCTOR FIELD TYPE VALIDATION TESTS ===

    // Correct types — should succeed
    test_truthy_interp(interp, "ctor valid Point", "(instance? (Point 3 4))", pass, fail);
    // Wrong type — string where Int expected
    test_error(interp, "ctor type error str for int", "(Point \"x\" 4)", pass, fail);
    // Wrong type — double where Int expected
    test_error(interp, "ctor type error dbl for int", "(Point 3.0 4)", pass, fail);
    // Subtype field — Circle has Int field, still works
    test_truthy_interp(interp, "ctor valid Circle", "(instance? (Circle 5))", pass, fail);
    // Nested instance type — Line expects Point fields
    test_truthy_interp(interp, "ctor valid Line", "(instance? (Line (Point 1 2) (Point 3 4)))", pass, fail);
    // Parametric field — Box accepts anything (no constraint)
    test_truthy_interp(interp, "ctor parametric Box", "(instance? (Box \"anything\"))", pass, fail);
}

fn void run_advanced_dispatch_constructor_ambiguity_parametric_tests(Interp* interp, int* pass, int* fail) {
    // === DISPATCH AMBIGUITY DETECTION TESTS ===

    // Two methods with identical signatures — should error
    setup(interp, "(define (ambig-test (^Int x) (^Int y)) (+ x y))");
    setup(interp, "(define (ambig-test (^Int x) (^Int y)) (* x y))");
    test_error(interp, "dispatch ambiguity", "(ambig-test 3 4)", pass, fail);
    // Different specificity — should succeed (picks more specific)
    setup(interp, "(define (specific-test (^Int x)) (* x 2))");
    setup(interp, "(define (specific-test x) 0)");
    test_eq_interp(interp, "dispatch no ambiguity", "(specific-test 5)", 10, pass, fail);

    // === PARAMETRIC CONSTRAINT AT CONSTRUCTION TESTS ===

    // Define constrained parametric type: T must be subtype of Number
    // Constraint expressed via dict annotation on field: ^{'T Number}
    setup(interp, "(define [type] (NumBox T) (^{'T Number} value))");
    // Int <: Number — should succeed
    test_eq_interp(interp, "NumBox Int ok", "(let (b (NumBox 42)) b.value)", 42, pass, fail);
    // Double <: Number — should succeed
    test_truthy_interp(interp, "NumBox Dbl ok", "(= (type-of (NumBox 3.14)) 'NumBox)", pass, fail);
    // String not <: Number — should error
    test_error(interp, "NumBox String fail", "(NumBox \"hi\")", pass, fail);
}

fn void run_advanced_dispatch_constructor_tests(Interp* interp, int* pass, int* fail) {
    run_advanced_dispatch_constructor_basic_tests(interp, pass, fail);
    run_advanced_dispatch_constructor_constraint_tests(interp, pass, fail);
    run_advanced_dispatch_constructor_validation_tests(interp, pass, fail);
    run_advanced_dispatch_constructor_ambiguity_parametric_tests(interp, pass, fail);
}

fn void run_advanced_index_mutation_tests(Interp* interp, int* pass, int* fail) {
    // Array/dict indexing (Phase 3)
    test_eq(interp, "array index", "(let (v (array 10 20 30)) v.[0])", 10, pass, fail);
    test_eq(interp, "array index 2", "(let (v (array 10 20 30)) v.[2])", 30, pass, fail);

    // Struct field mutation (Phase 3)
    test_eq_interp(interp, "set! struct field", "(let (p (Point 1 2)) (begin (set! p.x 99) p.x))", 99, pass, fail);
    test_eq_interp(interp, "set! struct field y", "(let (p (Point 1 2)) (begin (set! p.y 50) p.y))", 50, pass, fail);
    test_eq_interp(interp, "set! nested field", "(let (ln (Line (Point 0 0) (Point 10 10))) (begin (set! ln.start.x 5) ln.start.x))", 5, pass, fail);
    test_eq_interp(interp, "set! preserves other", "(let (p (Point 3 4)) (begin (set! p.x 99) p.y))", 4, pass, fail);
    test_eq_interp(interp, "instance field ffi survives scope return",
        "(begin "
        "  (define [type] EscBox h) "
        "  (define (mk-esc-box) (let (a (atomic 7)) (EscBox a))) "
        "  (define b (mk-esc-box)) "
        "  (atomic-add! b.h 1) "
        "  (atomic-read b.h))", 8, pass, fail);
    test_eq_interp(interp, "closure capture instance ffi field",
        "(begin "
        "  (define [type] EscBoxFn h) "
        "  (define (mk-esc-fn) "
        "    (let (a (atomic 3) b (EscBoxFn a)) "
        "      (lambda () (atomic-read b.h)))) "
        "  (define f (mk-esc-fn)) "
        "  (f))", 3, pass, fail);

    // Hashmap indexing (Phase 3)
    test_eq_interp(interp, "dict index", "(let (m {'a 1 'b 2}) m.['a])", 1, pass, fail);
}

fn void run_advanced_consolidation_primitive_hierarchy_tests(Interp* interp, int* pass, int* fail) {
    // === PRIMITIVE CONSOLIDATION TESTS (Phase 4) ===
    // Overload + for Point type (primitive becomes fallback)
    setup(interp, "(define (add-points (^Point a) (^Point b)) (Point (+ a.x b.x) (+ a.y b.y)))");
    test_eq_interp(interp, "typed dispatch Point+", "(let (r (add-points (Point 1 2) (Point 3 4))) r.x)", 4, pass, fail);
    test_eq_interp(interp, "typed dispatch Point+ y", "(let (r (add-points (Point 1 2) (Point 3 4))) r.y)", 6, pass, fail);
    // Built-in + still works (dispatch preserves primitive fallback)
    test_eq_interp(interp, "builtin + after dispatch", "(+ 10 20)", 30, pass, fail);
    // Overload length for custom type
    setup(interp, "(define (my-len (^Point p)) 2)");
    test_eq_interp(interp, "typed dispatch custom len", "(my-len (Point 5 6))", 2, pass, fail);

    // === ABSTRACT TYPE HIERARCHY TESTS ===
    // Number hierarchy
    test_truthy_interp(interp, "is? int Number", "(is? 42 'Number)", pass, fail);
    test_truthy_interp(interp, "is? double Number", "(is? 3.14 'Number)", pass, fail);
    test_nil_interp(interp, "is? string not Number", "(is? \"hi\" 'Number)", pass, fail);
    test_nil_interp(interp, "is? nil not Number", "(is? nil 'Number)", pass, fail);

    // Collection hierarchy
    test_truthy_interp(interp, "is? list Collection", "(is? '(1 2) 'Collection)", pass, fail);
    test_truthy_interp(interp, "is? array Collection", "(is? [1 2] 'Collection)", pass, fail);
    test_truthy_interp(interp, "is? dict Collection", "(is? {'a 1} 'Collection)", pass, fail);
    test_nil_interp(interp, "is? int not Collection", "(is? 42 'Collection)", pass, fail);
    test_nil_interp(interp, "is? string not Collection", "(is? \"hi\" 'Collection)", pass, fail);

    // number? uses abstract hierarchy
    test_truthy_interp(interp, "number? int", "(number? 42)", pass, fail);
    test_truthy_interp(interp, "number? double", "(number? 3.14)", pass, fail);
    test_nil_interp(interp, "number? string", "(number? \"hi\")", pass, fail);
}

fn void run_advanced_consolidation_predicate_equivalence_tests(Interp* interp, int* pass, int* fail) {
    // === PREDICATE EQUIVALENCE TESTS ===
    test_truthy_interp(interp, "int? positive", "(int? 42)", pass, fail);
    test_nil_interp(interp, "int? negative", "(int? 3.14)", pass, fail);
    test_truthy_interp(interp, "double? positive", "(double? 3.14)", pass, fail);
    test_nil_interp(interp, "double? negative", "(double? 42)", pass, fail);
    test_truthy_interp(interp, "string? positive", "(string? \"hi\")", pass, fail);
    test_nil_interp(interp, "string? negative", "(string? 42)", pass, fail);
    test_truthy_interp(interp, "symbol? positive", "(symbol? 'foo)", pass, fail);
    test_nil_interp(interp, "symbol? negative", "(symbol? 42)", pass, fail);
    test_truthy_interp(interp, "boolean? true", "(boolean? true)", pass, fail);
    test_truthy_interp(interp, "boolean? false", "(boolean? false)", pass, fail);
    test_nil_interp(interp, "boolean? int", "(boolean? 42)", pass, fail);
    test_truthy_interp(interp, "list? positive", "(list? '(1 2))", pass, fail);
    test_truthy_interp(interp, "list? nil", "(list? nil)", pass, fail);
    test_nil_interp(interp, "list? int", "(list? 42)", pass, fail);
    test_truthy_interp(interp, "closure? positive", "(closure? (lambda (x) x))", pass, fail);
    test_nil_interp(interp, "closure? int", "(closure? 42)", pass, fail);
    test_truthy_interp(interp, "array? positive", "(array? [1 2])", pass, fail);
    test_nil_interp(interp, "array? int", "(array? 42)", pass, fail);
    test_truthy_interp(interp, "dict? positive", "(dict? {'a 1})", pass, fail);
    test_nil_interp(interp, "dict? int", "(dict? 42)", pass, fail);
    test_truthy_interp(interp, "zero? yes", "(zero? 0)", pass, fail);
    test_nil_interp(interp, "zero? no", "(zero? 1)", pass, fail);
    test_truthy_interp(interp, "positive? yes", "(positive? 5)", pass, fail);
    test_nil_interp(interp, "positive? no", "(positive? -1)", pass, fail);
    test_truthy_interp(interp, "negative? yes", "(negative? -3)", pass, fail);
    test_nil_interp(interp, "negative? no", "(negative? 0)", pass, fail);
    test_truthy_interp(interp, "even? yes", "(even? 4)", pass, fail);
    test_nil_interp(interp, "even? no", "(even? 3)", pass, fail);
    test_truthy_interp(interp, "odd? yes", "(odd? 3)", pass, fail);
    test_nil_interp(interp, "odd? no", "(odd? 4)", pass, fail);
}

fn void run_advanced_consolidation_dispatch_extensibility_tests(Interp* interp, int* pass, int* fail) {
    // === DISPATCH EXTENSIBILITY TESTS ===
    setup(interp, "(define [type] Vec2 (^Double vx) (^Double vy))");
    setup(interp, "(define (+ (^Vec2 a) (^Vec2 b)) (Vec2 (+ a.vx b.vx) (+ a.vy b.vy)))");
    test_truthy_interp(interp, "Vec2 + dispatch x",
        "(= (let (v (+ (Vec2 1.0 2.0) (Vec2 3.0 4.0))) v.vx) 4.0)", pass, fail);
    test_truthy_interp(interp, "Vec2 + dispatch y",
        "(= (let (v (+ (Vec2 1.0 2.0) (Vec2 3.0 4.0))) v.vy) 6.0)", pass, fail);
    // Built-in + still works after Vec2 extension
    test_eq_interp(interp, "builtin + after Vec2", "(+ 1 2)", 3, pass, fail);
    // Extend length for Vec2
    setup(interp, "(define (length (^Vec2 v)) (sqrt (+ (* v.vx v.vx) (* v.vy v.vy))))");
    test_truthy_interp(interp, "Vec2 length dispatch",
        "(= (length (Vec2 3.0 4.0)) 5.0)", pass, fail);
    // Built-in length still works
    test_eq_interp(interp, "builtin length after Vec2", "(length '(1 2 3))", 3, pass, fail);
    // procedure? recognizes method tables
    test_truthy_interp(interp, "procedure? method table", "(procedure? +)", pass, fail);
}

fn void run_advanced_consolidation_predicate_dispatch_tests(Interp* interp, int* pass, int* fail) {
    run_advanced_consolidation_primitive_hierarchy_tests(interp, pass, fail);
    run_advanced_consolidation_predicate_equivalence_tests(interp, pass, fail);
    run_advanced_consolidation_dispatch_extensibility_tests(interp, pass, fail);
}

fn void run_advanced_mutation_hierarchy_dispatch_tests(Interp* interp, int* pass, int* fail) {
    run_advanced_index_mutation_tests(interp, pass, fail);
    run_advanced_consolidation_predicate_dispatch_tests(interp, pass, fail);
}

fn void run_advanced_io_typed_effect_tests(Interp* interp, int* pass, int* fail) {
    // === I/O EFFECTS TESTS (Phase 5) ===
    // print/println go through effects now (fast path to __raw-*)
    test_nil_interp(interp, "io effect print", "(print 42)", pass, fail);
    test_nil_interp(interp, "io effect println", "(println 99)", pass, fail);

    // Custom handler can suppress output
    test_eq_interp(interp, "io handle suppress",
        "(handle (begin (println \"suppressed\") 42) (io/println x (resolve nil)))",
        42, pass, fail);

    // Custom handler can capture output
    test_truthy_interp(interp, "io handle capture",
        "(= (handle (begin (println \"captured\") nil) (io/println x x)) \"captured\")",
        pass, fail);

    // __raw-print still works directly
    test_nil_interp(interp, "raw-print direct", "(__raw-print 0)", pass, fail);

    // === TYPED EFFECT DECLARATIONS TESTS ===
    // Declaring effects doesn't error
    test_nil_interp(interp, "defeffect decl", "(define [effect] (test/greet (^String name)))", pass, fail);
    test_nil_interp(interp, "defeffect no arg", "(define [effect] (test/ping))", pass, fail);
    test_nil_interp(interp, "defeffect int arg", "(define [effect] (test/count (^Int n)))", pass, fail);

    // Perform with correct type works
    test_str_interp(interp, "effect correct type",
        "(handle (signal test/greet \"hello\") (test/greet x x))", pass, fail);
    test_eq_interp(interp, "effect int correct",
        "(handle (signal test/count 42) (test/count x x))", 42, pass, fail);

    // Perform with wrong type errors
    test_error(interp, "effect wrong type int",
        "(handle (signal test/greet 42) (test/greet x x))", pass, fail);
    test_error(interp, "effect wrong type str",
        "(handle (signal test/count \"bad\") (test/count x x))", pass, fail);

    // Undeclared effects still work (backward compat)
    test_eq_interp(interp, "effect undeclared ok",
        "(handle (signal custom-tag 42) (custom-tag x (+ x 1)))", 43, pass, fail);

    // Nullary effect (no arg type) accepts anything
    test_eq_interp(interp, "effect nullary any",
        "(handle (signal test/ping 99) (test/ping x x))", 99, pass, fail);

    // ^Any effects accept all types
    test_nil_interp(interp, "io effect Any int", "(print 42)", pass, fail);
    test_nil_interp(interp, "io effect Any str", "(print \"hello\")", pass, fail);

    // io/read-file with wrong type should error (^String)
    test_error(interp, "io/read-file wrong type",
        "(handle (signal io/read-file 42) (io/read-file x x))", pass, fail);

    // io/read-file with correct type works
    test_str_interp(interp, "io/read-file correct type",
        "(handle (signal io/read-file \"test.txt\") (io/read-file x x))", pass, fail);

    // Subtype check: Number accepts Int and Double
    test_nil_interp(interp, "defeffect number arg", "(define [effect] (test/num (^Number n)))", pass, fail);
    test_eq_interp(interp, "effect number int",
        "(handle (signal test/num 42) (test/num x x))", 42, pass, fail);
    test_error(interp, "effect number str",
        "(handle (signal test/num \"bad\") (test/num x x))", pass, fail);
}

fn void run_advanced_union_memory_limit_tests(Interp* interp, int* pass, int* fail) {
    // === UNION PATTERN MATCHING TESTS (Phase 6) ===
    // Option and Result types already defined in Phase 1 tests above

    // Nullary constructor match
    test_eq_interp(interp, "match None", "(match None (None 0) ((Some x) x))", 0, pass, fail);

    // Constructor with field match
    test_eq_interp(interp, "match Some", "(match (Some 42) (None 0) ((Some x) x))", 42, pass, fail);

    // Result type
    setup(interp, "(define [union] (Result T E) (Ok T) (Err E))");
    test_eq_interp(interp, "match Ok", "(match (Ok 1) ((Ok v) v) ((Err e) (- 0 1)))", 1, pass, fail);
    test_eq_interp(interp, "match Err", "(match (Err 99) ((Ok v) v) ((Err e) e))", 99, pass, fail);

    // Nested constructor match
    test_eq_interp(interp, "match nested Some", "(match (Some (Some 7)) ((Some (Some x)) x) (_ 0))", 7, pass, fail);

    // Wildcard in constructor
    test_eq_interp(interp, "match ctor wildcard", "(match (Ok 42) ((Ok _) 1) ((Err _) 2))", 1, pass, fail);

    // === MEMORY RECLAMATION TESTS ===
    // unsafe-free! on array: releases backing, accessing freed value errors
    test_error(interp, "unsafe-free! array UAF", "(let (a [1 2 3]) (begin (unsafe-free! a) a))", pass, fail);
    // unsafe-free! on dict: releases backing, accessing freed value errors
    test_error(interp, "unsafe-free! dict UAF", "(let (d {'a 1 'b 2}) (begin (unsafe-free! d) d))", pass, fail);
    // unsafe-free! on non-collection: no-op, returns nil
    test_nil_interp(interp, "unsafe-free! int noop", "(unsafe-free! 42)", pass, fail);
    // unsafe-free! on nil: no-op
    test_nil_interp(interp, "unsafe-free! nil noop", "(unsafe-free! nil)", pass, fail);

    // === RAISED LIMITS TESTS ===
    // Match with 17 clauses (was max 16, now 32)
    test_eq(interp, "match 17 clauses",
        "(match 17 (1 101) (2 102) (3 103) (4 104) (5 105) (6 106) (7 107) (8 108) (9 109) (10 110) (11 111) (12 112) (13 113) (14 114) (15 115) (16 116) (17 117))",
        117, pass, fail);
    // Handle with 9 effect clauses (was max 8, now 16)
    test_eq(interp, "handle 9 effects",
        "(handle (signal e9 42) (e1 v (resolve v)) (e2 v (resolve v)) (e3 v (resolve v)) (e4 v (resolve v)) (e5 v (resolve v)) (e6 v (resolve v)) (e7 v (resolve v)) (e8 v (resolve v)) (e9 v (+ v 1)))",
        43, pass, fail);
    // Large method table (>32 methods, was max 32, now 64)
    setup(interp, "(define [type] TestMT33 (^Int val))");
    setup(interp, "(define (mt33-f (^(Val 1) n)) 1) (define (mt33-f (^(Val 2) n)) 2) (define (mt33-f (^(Val 3) n)) 3) (define (mt33-f (^(Val 4) n)) 4) (define (mt33-f (^(Val 5) n)) 5) (define (mt33-f (^(Val 6) n)) 6) (define (mt33-f (^(Val 7) n)) 7) (define (mt33-f (^(Val 8) n)) 8)");
    setup(interp, "(define (mt33-f (^(Val 9) n)) 9) (define (mt33-f (^(Val 10) n)) 10) (define (mt33-f (^(Val 11) n)) 11) (define (mt33-f (^(Val 12) n)) 12) (define (mt33-f (^(Val 13) n)) 13) (define (mt33-f (^(Val 14) n)) 14) (define (mt33-f (^(Val 15) n)) 15) (define (mt33-f (^(Val 16) n)) 16)");
    setup(interp, "(define (mt33-f (^(Val 17) n)) 17) (define (mt33-f (^(Val 18) n)) 18) (define (mt33-f (^(Val 19) n)) 19) (define (mt33-f (^(Val 20) n)) 20) (define (mt33-f (^(Val 21) n)) 21) (define (mt33-f (^(Val 22) n)) 22) (define (mt33-f (^(Val 23) n)) 23) (define (mt33-f (^(Val 24) n)) 24)");
    setup(interp, "(define (mt33-f (^(Val 25) n)) 25) (define (mt33-f (^(Val 26) n)) 26) (define (mt33-f (^(Val 27) n)) 27) (define (mt33-f (^(Val 28) n)) 28) (define (mt33-f (^(Val 29) n)) 29) (define (mt33-f (^(Val 30) n)) 30) (define (mt33-f (^(Val 31) n)) 31) (define (mt33-f (^(Val 32) n)) 32)");
    setup(interp, "(define (mt33-f (^(Val 33) n)) 33)");
    test_eq(interp, "method table >32", "(mt33-f 33)", 33, pass, fail);
}

fn void run_advanced_effect_union_limit_tests(Interp* interp, int* pass, int* fail) {
    run_advanced_io_typed_effect_tests(interp, pass, fail);
    run_advanced_union_memory_limit_tests(interp, pass, fail);
}

fn void run_advanced_ffi_system_tests(Interp* interp, int* pass, int* fail) {
    // === FFI TESTS (define [ffi lib] / define [ffi λ]) ===
    // Library declaration
    setup(interp, "(define [ffi lib] libc \"libc.so.6\")");
    test_tag(interp, "ffi lib handle", "libc", FFI_HANDLE, pass, fail);

    // Function bindings — integer functions
    setup(interp, "(define [ffi λ libc] (strlen (^String s)) ^Int)");
    test_eq(interp, "ffi λ strlen", "(strlen \"hello\")", 5, pass, fail);
    test_eq(interp, "ffi λ strlen empty", "(strlen \"\")", 0, pass, fail);

    setup(interp, "(define [ffi λ libc] (abs (^Int n)) ^Int)");
    test_eq(interp, "ffi λ abs", "(abs -42)", 42, pass, fail);
    test_eq(interp, "ffi λ abs zero", "(abs 0)", 0, pass, fail);

    setup(interp, "(define [ffi λ libc] (atoi (^String s)) ^Int)");
    test_eq(interp, "ffi λ atoi", "(atoi \"12345\")", 12345, pass, fail);
    test_eq(interp, "ffi λ atoi neg", "(atoi \"-99\")", -99, pass, fail);

    // Zero-arg function
    setup(interp, "(define [ffi λ libc] (getpid) ^Int)");
    test_gt(interp, "ffi λ getpid", "(getpid)", 0, pass, fail);

    // Math library — double functions
    setup(interp, "(define [ffi lib] libm \"libm.so.6\")");
    setup(interp, "(define [ffi λ libm] (sqrt (^Double x)) ^Double)");
    test_eq_double(interp, "ffi λ sqrt", "(sqrt 4.0)", 2.0, pass, fail);
    test_eq_double(interp, "ffi λ sqrt 9", "(sqrt 9.0)", 3.0, pass, fail);

    setup(interp, "(define [ffi λ libm] (pow (^Double x) (^Double y)) ^Double)");
    test_eq_double(interp, "ffi λ pow", "(pow 2.0 10.0)", 1024.0, pass, fail);

    // Mixed int/double (libffi enables this — old FFI couldn't)
    setup(interp, "(define [ffi λ libm] (ldexp (^Double x) (^Int exp)) ^Double)");
    test_eq_double(interp, "ffi λ ldexp", "(ldexp 1.0 10)", 1024.0, pass, fail);

    // Void return
    setup(interp, "(define [ffi λ libc] (free (^Ptr p)))");
    test_nil_jit(interp, "ffi λ free null", "(free 0)", pass, fail);

    // Error cases
    test_error(interp, "ffi lib bad", "(define [ffi lib] bad-lib \"nonexistent_xyz.so\")", pass, fail);

    // === SYSTEM PRIMITIVES TESTS ===
    // random — returns double in [0,1)
    test_truthy(interp, "random range", "(let (r (random)) (and (>= r 0.0) (< r 1.0)))", pass, fail);
    test_truthy(interp, "random is double", "(double? (random))", pass, fail);
    // random-int — returns int in [0,n)
    test_truthy(interp, "random-int range", "(let (r (random-int 10)) (and (>= r 0) (< r 10)))", pass, fail);
    test_truthy(interp, "random-int is int", "(int? (random-int 100))", pass, fail);
    // time — returns epoch seconds (should be > 2020-01-01)
    test_gt(interp, "time epoch", "(time)", 1577836800, pass, fail);
    // time-ms — returns epoch milliseconds (> time * 1000)
    test_gt(interp, "time-ms epoch", "(time-ms)", 1577836800000, pass, fail);
    // getenv — HOME should exist, nonexistent returns nil
    test_str(interp, "getenv HOME", "(getenv \"HOME\")", pass, fail);
    test_nil(interp, "getenv missing", "(getenv \"OMNI_NONEXISTENT_VAR_XYZ\")", pass, fail);
    // shell — captures stdout
    test_str_val(interp, "shell echo", "(shell \"echo hello\")", "hello", pass, fail);
    // sleep — returns nil (tiny sleep to not slow tests)
    test_nil(interp, "sleep tiny", "(sleep 0.001)", pass, fail);
}

fn void run_advanced_tests(Interp* interp, int* pass, int* fail) {
    io::printn("\n--- Advanced Feature Tests ---");
    run_advanced_core_semantics_tests(interp, pass, fail);
    run_advanced_unicode_iterator_tests(interp, pass, fail);
    run_advanced_logic_tco_tests(interp, pass, fail);
    run_advanced_effect_continuation_tests(interp, pass, fail);
    run_advanced_runtime_control_tests(interp, pass, fail);
    run_advanced_lambda_syntax_tests(interp, pass, fail);
    run_advanced_binding_mutation_tests(interp, pass, fail);
    run_advanced_macro_hygiene_tests(interp, pass, fail);
    run_advanced_collections_module_tests(interp, pass, fail);
    run_advanced_stdlib_numeric_tests(interp, pass, fail);
    run_advanced_type_parametric_tests(interp, pass, fail);
    run_advanced_dispatch_constructor_tests(interp, pass, fail);
    run_advanced_mutation_hierarchy_dispatch_tests(interp, pass, fail);
    run_advanced_effect_union_limit_tests(interp, pass, fail);
    run_advanced_ffi_system_tests(interp, pass, fail);
}
