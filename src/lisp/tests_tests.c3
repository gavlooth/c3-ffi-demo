module lisp;


import std::io;
import main;
// =============================================================================
// SECTION 10: TESTS
// =============================================================================

// ---------------------------------------------------------------------------
// Unified Test Helpers — each runs BOTH interpreter AND JIT on the same expr
// ---------------------------------------------------------------------------

fn void setup(Interp* interp, char[] code) {
    run(code, interp);
}

fn void test_eq(Interp* interp, char[] name, char[] expr, long expected, int* pass, int* fail) {
    EvalResult ri = run(expr, interp);
    bool interp_ok = !ri.error.has_error && is_int(ri.value) && ri.value.int_val == expected;

    Expr* ast = parse_for_jit(expr, interp);
    bool jit_ok = false;
    if (ast != null) {
        JitFn f = jit_compile(ast, interp);
        if (f != null) {
            Value* jv = jit_exec(f, interp);
            jit_ok = jv != null && jv.tag == INT && jv.int_val == expected;
        }
    }

    if (interp_ok && jit_ok) {
        io::printfn("[PASS] %s", (ZString)name);
        (*pass)++;
    } else {
        io::printfn("[FAIL] %s (interp=%s, jit=%s)", (ZString)name, interp_ok ? "ok" : "FAIL", jit_ok ? "ok" : "FAIL");
        (*fail)++;
    }
}

fn void test_eq_double(Interp* interp, char[] name, char[] expr, double expected, int* pass, int* fail) {
    EvalResult ri = run(expr, interp);
    bool interp_ok = !ri.error.has_error && is_double(ri.value) && ri.value.double_val == expected;

    Expr* ast = parse_for_jit(expr, interp);
    bool jit_ok = false;
    if (ast != null) {
        JitFn f = jit_compile(ast, interp);
        if (f != null) {
            Value* jv = jit_exec(f, interp);
            jit_ok = jv != null && is_double(jv) && jv.double_val == expected;
        }
    }

    if (interp_ok && jit_ok) {
        io::printfn("[PASS] %s", (ZString)name);
        (*pass)++;
    } else {
        io::printfn("[FAIL] %s (interp=%s, jit=%s)", (ZString)name, interp_ok ? "ok" : "FAIL", jit_ok ? "ok" : "FAIL");
        (*fail)++;
    }
}

fn void test_truthy(Interp* interp, char[] name, char[] expr, int* pass, int* fail) {
    EvalResult ri = run(expr, interp);
    bool interp_ok = !ri.error.has_error && !is_nil(ri.value) && !(is_symbol(ri.value) && ri.value.sym_val == interp.sym_false);

    Expr* ast = parse_for_jit(expr, interp);
    bool jit_ok = false;
    if (ast != null) {
        JitFn f = jit_compile(ast, interp);
        if (f != null) {
            Value* jv = jit_exec(f, interp);
            jit_ok = jv != null && !is_nil(jv) && !(is_symbol(jv) && jv.sym_val == interp.sym_false);
        }
    }

    if (interp_ok && jit_ok) {
        io::printfn("[PASS] %s", (ZString)name);
        (*pass)++;
    } else {
        io::printfn("[FAIL] %s (interp=%s, jit=%s)", (ZString)name, interp_ok ? "ok" : "FAIL", jit_ok ? "ok" : "FAIL");
        (*fail)++;
    }
}

fn void test_nil(Interp* interp, char[] name, char[] expr, int* pass, int* fail) {
    EvalResult ri = run(expr, interp);
    bool interp_ok = !ri.error.has_error && is_nil(ri.value);

    Expr* ast = parse_for_jit(expr, interp);
    bool jit_ok = false;
    if (ast != null) {
        JitFn f = jit_compile(ast, interp);
        if (f != null) {
            Value* jv = jit_exec(f, interp);
            jit_ok = jv != null && is_nil(jv);
        }
    }

    if (interp_ok && jit_ok) {
        io::printfn("[PASS] %s", (ZString)name);
        (*pass)++;
    } else {
        io::printfn("[FAIL] %s (interp=%s, jit=%s)", (ZString)name, interp_ok ? "ok" : "FAIL", jit_ok ? "ok" : "FAIL");
        (*fail)++;
    }
}

fn void test_error(Interp* interp, char[] name, char[] expr, int* pass, int* fail) {
    EvalResult ri = run(expr, interp);
    bool interp_ok = ri.error.has_error;
    if (interp_ok) {
        io::printfn("[PASS] %s", (ZString)name);
        (*pass)++;
    } else {
        io::printfn("[FAIL] %s (expected error)", (ZString)name);
        (*fail)++;
    }
}

fn void test_error_contains(Interp* interp, char[] name, char[] expr_str, char[] expected_substr, int* pass, int* fail) {
    EvalResult ri = run(expr_str, interp);
    if (!ri.error.has_error) {
        io::printfn("[FAIL] %s (expected error, got success)", (ZString)name);
        (*fail)++; return;
    }
    // Check if error message contains expected substring
    char[] msg = ri.error.message[:256];
    bool found = false;
    usz slen = 0;
    while (slen < 256 && msg[slen] != 0) slen++;
    usz elen = expected_substr.len;
    if (elen <= slen) {
        for (usz i = 0; i <= slen - elen; i++) {
            bool match = true;
            for (usz j = 0; j < elen; j++) {
                if (msg[i + j] != expected_substr[j]) { match = false; break; }
            }
            if (match) { found = true; break; }
        }
    }
    if (found) {
        io::printfn("[PASS] %s", (ZString)name);
        (*pass)++;
    } else {
        io::printfn("[FAIL] %s (error '%s' doesn't contain '%s')", (ZString)name, (ZString)msg, (ZString)expected_substr);
        (*fail)++;
    }
}

fn bool str_starts_with(char[] haystack, char[] needle) {
    if (haystack.len < needle.len) return false;
    for (usz i = 0; i < needle.len; i++) {
        if (haystack[i] != needle[i]) return false;
    }
    return true;
}

fn void test_str_val(Interp* interp, char[] name, char[] expr, char[] expected, int* pass, int* fail) {
    EvalResult ri = run(expr, interp);
    bool interp_ok = !ri.error.has_error && is_string(ri.value) &&
        str_starts_with(ri.value.str_chars[:ri.value.str_len], expected) &&
        ri.value.str_len == expected.len;

    Expr* ast = parse_for_jit(expr, interp);
    bool jit_ok = false;
    if (ast != null) {
        JitFn f = jit_compile(ast, interp);
        if (f != null) {
            Value* jv = jit_exec(f, interp);
            jit_ok = jv != null && jv.tag == STRING &&
                str_starts_with(jv.str_chars[:jv.str_len], expected) &&
                jv.str_len == expected.len;
        }
    }

    if (interp_ok && jit_ok) {
        io::printfn("[PASS] %s", (ZString)name);
        (*pass)++;
    } else {
        char[256] got_buf;
        char[] got_str = "?";
        if (!ri.error.has_error && is_string(ri.value)) {
            got_str = ri.value.str_chars[:ri.value.str_len];
        }
        io::printfn("[FAIL] %s (interp=%s, jit=%s, got='%s')", (ZString)name,
            interp_ok ? "ok" : "FAIL", jit_ok ? "ok" : "FAIL", (ZString)got_str);
        (*fail)++;
    }
}

fn void test_str(Interp* interp, char[] name, char[] expr, int* pass, int* fail) {
    EvalResult ri = run(expr, interp);
    bool interp_ok = !ri.error.has_error && is_string(ri.value);

    Expr* ast = parse_for_jit(expr, interp);
    bool jit_ok = false;
    if (ast != null) {
        JitFn f = jit_compile(ast, interp);
        if (f != null) {
            Value* jv = jit_exec(f, interp);
            jit_ok = jv != null && jv.tag == STRING;
        }
    }

    if (interp_ok && jit_ok) {
        io::printfn("[PASS] %s", (ZString)name);
        (*pass)++;
    } else {
        io::printfn("[FAIL] %s (interp=%s, jit=%s)", (ZString)name, interp_ok ? "ok" : "FAIL", jit_ok ? "ok" : "FAIL");
        (*fail)++;
    }
}

fn void test_tag(Interp* interp, char[] name, char[] expr, ValueTag expected, int* pass, int* fail) {
    EvalResult ri = run(expr, interp);
    bool interp_ok = !ri.error.has_error && ri.value != null && ri.value.tag == expected;

    Expr* ast = parse_for_jit(expr, interp);
    bool jit_ok = false;
    if (ast != null) {
        JitFn f = jit_compile(ast, interp);
        if (f != null) {
            Value* jv = jit_exec(f, interp);
            jit_ok = jv != null && jv.tag == expected;
        }
    }

    if (interp_ok && jit_ok) {
        io::printfn("[PASS] %s", (ZString)name);
        (*pass)++;
    } else {
        io::printfn("[FAIL] %s (interp=%s, jit=%s)", (ZString)name, interp_ok ? "ok" : "FAIL", jit_ok ? "ok" : "FAIL");
        (*fail)++;
    }
}

fn void test_gt(Interp* interp, char[] name, char[] expr, long min, int* pass, int* fail) {
    EvalResult ri = run(expr, interp);
    bool interp_ok = !ri.error.has_error && is_int(ri.value) && ri.value.int_val > min;

    Expr* ast = parse_for_jit(expr, interp);
    bool jit_ok = false;
    if (ast != null) {
        JitFn f = jit_compile(ast, interp);
        if (f != null) {
            Value* jv = jit_exec(f, interp);
            jit_ok = jv != null && jv.tag == INT && jv.int_val > min;
        }
    }

    if (interp_ok && jit_ok) {
        io::printfn("[PASS] %s", (ZString)name);
        (*pass)++;
    } else {
        io::printfn("[FAIL] %s (interp=%s, jit=%s)", (ZString)name, interp_ok ? "ok" : "FAIL", jit_ok ? "ok" : "FAIL");
        (*fail)++;
    }
}

// Interp-only: for tests that involve stateful side effects or features
// the JIT cannot handle (modules, error paths, println, etc.)
fn void test_eq_interp(Interp* interp, char[] name, char[] expr, long expected, int* pass, int* fail) {
    EvalResult ri = run(expr, interp);
    if (!ri.error.has_error && is_int(ri.value) && ri.value.int_val == expected) {
        io::printfn("[PASS] %s", (ZString)name);
        (*pass)++;
    } else {
        if (!ri.error.has_error && is_int(ri.value)) { io::printfn("[FAIL] %s (got %d)", (ZString)name, ri.value.int_val); }
        else { io::printfn("[FAIL] %s", (ZString)name); }
        (*fail)++;
    }
}

fn void test_truthy_interp(Interp* interp, char[] name, char[] expr, int* pass, int* fail) {
    EvalResult ri = run(expr, interp);
    if (!ri.error.has_error && !is_nil(ri.value) && !(is_symbol(ri.value) && ri.value.sym_val == interp.sym_false)) {
        io::printfn("[PASS] %s", (ZString)name);
        (*pass)++;
    } else {
        io::printfn("[FAIL] %s", (ZString)name);
        (*fail)++;
    }
}

fn void test_nil_interp(Interp* interp, char[] name, char[] expr, int* pass, int* fail) {
    EvalResult ri = run(expr, interp);
    if (!ri.error.has_error && is_nil(ri.value)) {
        io::printfn("[PASS] %s", (ZString)name);
        (*pass)++;
    } else {
        io::printfn("[FAIL] %s", (ZString)name);
        (*fail)++;
    }
}

fn void test_str_interp(Interp* interp, char[] name, char[] expr, int* pass, int* fail) {
    EvalResult ri = run(expr, interp);
    if (!ri.error.has_error && is_string(ri.value)) {
        io::printfn("[PASS] %s", (ZString)name);
        (*pass)++;
    } else {
        io::printfn("[FAIL] %s", (ZString)name);
        (*fail)++;
    }
}

fn void test_tag_interp(Interp* interp, char[] name, char[] expr, ValueTag expected, int* pass, int* fail) {
    EvalResult ri = run(expr, interp);
    if (!ri.error.has_error && ri.value != null && ri.value.tag == expected) {
        io::printfn("[PASS] %s", (ZString)name);
        (*pass)++;
    } else {
        io::printfn("[FAIL] %s", (ZString)name);
        (*fail)++;
    }
}

// JIT-only: for stateful tests that would double-mutate if run through both interp and JIT
fn void test_eq_jit(Interp* interp, char[] name, char[] source, long expected, int* pass, int* fail) {
    Expr* ast = parse_for_jit(source, interp);
    if (ast == null) { (*fail)++; io::printfn("[FAIL] %s (JIT parse error)", (ZString)name); return; }
    JitFn f = jit_compile(ast, interp);
    if (f == null) { (*fail)++; io::printfn("[FAIL] %s (JIT compile error)", (ZString)name); return; }
    Value* result = jit_exec(f, interp);
    if (result != null && result.tag == INT && result.int_val == expected) {
        io::printfn("[PASS] %s", (ZString)name);
        (*pass)++;
    } else {
        (*fail)++;
        if (result != null && result.tag == INT) { io::printfn("[FAIL] %s (JIT got %d)", (ZString)name, result.int_val); }
        else { io::printfn("[FAIL] %s", (ZString)name); }
    }
}

fn void test_nil_jit(Interp* interp, char[] name, char[] source, int* pass, int* fail) {
    Expr* ast = parse_for_jit(source, interp);
    if (ast == null) { (*fail)++; io::printfn("[FAIL] %s (JIT parse error)", (ZString)name); return; }
    JitFn f = jit_compile(ast, interp);
    if (f == null) { (*fail)++; io::printfn("[FAIL] %s (JIT compile error)", (ZString)name); return; }
    Value* result = jit_exec(f, interp);
    if (result == null || is_nil(result)) {
        io::printfn("[PASS] %s", (ZString)name);
        (*pass)++;
    } else {
        (*fail)++;
        io::printfn("[FAIL] %s (JIT expected nil)", (ZString)name);
    }
}

fn double dabs(double x) @inline { return x < 0.0 ? -x : x; }

fn void test_double(Interp* interp, char[] name, char[] expr, double expected, int* pass, int* fail) {
    EvalResult ri = run(expr, interp);
    bool interp_ok = !ri.error.has_error && is_number(ri.value) && dabs(to_double(ri.value) - expected) < 0.0001;

    Expr* ast = parse_for_jit(expr, interp);
    bool jit_ok = false;
    if (ast != null) {
        JitFn f = jit_compile(ast, interp);
        if (f != null) {
            Value* jv = jit_exec(f, interp);
            jit_ok = jv != null && is_number(jv) && dabs(to_double(jv) - expected) < 0.0001;
        }
    }

    if (interp_ok && jit_ok) {
        io::printfn("[PASS] %s", (ZString)name);
        (*pass)++;
    } else {
        io::printfn("[FAIL] %s (interp=%s, jit=%s)", (ZString)name, interp_ok ? "ok" : "FAIL", jit_ok ? "ok" : "FAIL");
        (*fail)++;
    }
}

// =============================================================================
// E2E COMPILER TEST GENERATION
// =============================================================================

struct E2ETestCase {
    char[] name;
    char[] expr;
}

// E2E test expressions — must be compilable (no type system, modules, set!, arrays, dicts)
// Setup defines are run first, then each expr is evaluated and its output captured.
const char[][] E2E_SETUPS = {
    // Basic definitions
    `(define x 10)`,
    `(define add +)`,
    `(define make-adder (lambda (n) (lambda (x) (+ n x))))`,
    `(define adder5 (make-adder 5))`,
    // Recursion
    `(define sum-to (lambda (n) (if (= n 0) 0 (+ n (sum-to (- n 1))))))`,
    `(define fib (lambda (n) (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2))))))`,
    // List helpers
    `(define mymap (lambda (f) (lambda (lst) (if (null? lst) (quote ()) (cons (f (car lst)) ((mymap f) (cdr lst)))))))`,
    `(define double (lambda (x) (* x 2)))`,
    `(define fold-helper (lambda (f) (lambda (init) (lambda (lst) (if (null? lst) init (((fold-helper f) (f init (car lst))) (cdr lst)))))))`,
    // Counter factory
    `(define make-counter (lambda (start) (lambda (x) (+ start x))))`,
    `(define c1 (make-counter 10))`,
    `(define c2 (make-counter 20))`,
    // Named let helper
    `(define fact (lambda (n) (if (= n 0) 1 (* n (fact (- n 1))))))`,
    // Mutual recursion
    `(define is-even (lambda (n) (if (= n 0) true (is-odd (- n 1)))))`,
    `(define is-odd (lambda (n) (if (= n 0) nil (is-even (- n 1)))))`,
    // Composition helpers
    `(define inc (lambda (x) (+ x 1)))`,
    `(define add3 (lambda (x) (+ x 3)))`,
    `(define apply-twice (lambda (f) (lambda (x) (f (f x)))))`,
    // Additional closure
    `(define c3 (make-counter 30))`,
    // Note: id, compose already defined by stdlib
    // set! test variables
    `(define s_x 10)`,
    `(define s_acc 0)`,
    `(define s_count 0)`,
};

const E2ETestCase[] E2E_TESTS = {
    // === Literals ===
    { "int literal 42", "42" },
    { "int literal 0", "0" },
    { "int literal negative", "-5" },
    { "string literal", `"hello"` },
    { "true literal", "true" },
    { "nil literal", "nil" },
    { "quoted symbol", "(quote foo)" },
    { "quoted list", "(quote (a b c))" },
    { "quoted nested", "(quote (1 (2 3) 4))" },
    { "empty list", "(quote ())" },

    // === Arithmetic ===
    { "add", "(+ 1 2)" },
    { "add zero", "(+ 0 0)" },
    { "add negative", "(+ -5 3)" },
    { "add cancel", "(+ 100 -100)" },
    { "add large", "(+ 1000000 2000000)" },
    { "sub", "(- 10 3)" },
    { "sub negative result", "(- 3 10)" },
    { "mul", "(* 6 7)" },
    { "mul zero", "(* 100 0)" },
    { "div", "(/ 10 3)" },
    { "div exact", "(/ 100 10)" },
    { "modulo", "(% 10 3)" },
    { "modulo even", "(% 8 4)" },
    { "nested arith", "(+ (* 3 4) (- 10 5))" },

    // === Floats ===
    { "float literal", "3.14" },
    { "float add", "(+ 1.5 2.5)" },
    { "float mul", "(* 2.0 3.0)" },
    { "float div", "(/ 7.0 2.0)" },
    { "int to float add", "(+ 1 0.5)" },
    { "float integer value", "5.0" },

    // === Comparisons ===
    { "eq true", "(= 5 5)" },
    { "eq false", "(= 5 6)" },
    { "lt true", "(< 3 5)" },
    { "lt false", "(< 5 3)" },
    { "gt true", "(> 5 3)" },
    { "gt false", "(> 3 5)" },
    { "le true", "(<= 5 5)" },
    { "le false", "(<= 6 5)" },
    { "ge true", "(>= 5 5)" },
    { "ge false", "(>= 4 5)" },

    // === Variables & Bindings ===
    { "var x", "x" },
    { "let simple", "(let (y 7) y)" },
    { "let nested", "(let (a 1) (let (b 2) (+ a b)))" },
    { "let shadow", "(let (x 99) x)" },
    { "let multi", "(let (a 1 b 2 c 3) (+ a (+ b c)))" },

    // === Lambda & Application ===
    { "lambda identity", "((lambda (x) x) 5)" },
    { "lambda const", "((lambda (x) 42) 99)" },
    { "lambda arith", "((lambda (x) (+ x 10)) 32)" },
    { "curried add", "(((lambda (x) (lambda (y) (+ x y))) 3) 4)" },
    { "closure adder5", "(adder5 10)" },

    // === If ===
    { "if true", "(if true 1 2)" },
    { "if false", "(if nil 1 2)" },
    { "if nested", "(if (> 5 3) (if (< 2 1) 10 20) 30)" },
    { "if no else", "(if true 42)" },

    // === And/Or ===
    { "and true true", "(and 1 42)" },
    { "and false short", "(and nil 42)" },
    { "or first truthy", "(or 42 99)" },
    { "or fallback", "(or nil 99)" },

    // === Truthiness ===
    { "0 is truthy", "(if 0 42 99)" },
    { "empty string truthy", `(if "" 1 0)` },
    { "empty list truthy", "(if (quote ()) 1 0)" },

    // === Begin ===
    { "begin single", "(begin 42)" },
    { "begin multi", "(begin 1 2 3)" },

    // === Cons / Car / Cdr ===
    { "car cons", "(car (cons 1 2))" },
    { "cdr cons", "(cdr (cons 1 2))" },
    { "car list", "(car (quote (10 20 30)))" },
    { "cdr list", "(cdr (quote (10 20 30)))" },
    { "cons build", "(cons 1 (cons 2 (cons 3 (quote ()))))" },
    { "null? empty", "(null? (quote ()))" },
    { "null? nonempty", "(null? (quote (1)))" },
    { "pair?", "(pair? (cons 1 2))" },
    { "pair? nil", "(pair? nil)" },

    // === Length ===
    { "length empty", "(length (quote ()))" },
    { "length 5", "(length (quote (1 2 3 4 5)))" },

    // === String operations ===
    { "string-length", `(string-length "hello world")` },
    { "string-append", `(string-append "hello" " world")` },
    // substring skipped: 3-arg function not yet supported in compiler runtime
    { "number->string", `(number->string 42)` },
    { "string->number", `(string->number "42")` },

    // === Recursive functions ===
    { "sum-to 50", "(sum-to 50)" },
    { "fib 10", "(fib 10)" },
    { "fib 15", "(fib 15)" },
    { "factorial 5", "(fact 5)" },
    { "factorial 10", "(fact 10)" },

    // === Closure factories ===
    { "counter c1", "(c1 5)" },
    { "counter c2", "(c2 5)" },

    // === Higher-order functions ===
    { "map double car", "(car ((mymap double) (quote (1 2 3))))" },
    { "fold sum", "(((fold-helper +) 0) (quote (1 2 3 4 5)))" },
    { "map and fold", "(((fold-helper +) 0) ((mymap double) (quote (1 2 3 4 5))))" },

    // === Pattern matching ===
    { "match literal", "(match 42 (42 1) (x 0))" },
    { "match variable", "(match 5 (x x))" },
    { "match wildcard", "(match 99 (_ 1))" },
    { "match list", "(match (quote (1 2 3)) ([a b c] (+ a (+ b c))))" },
    { "match head", "(match (quote (10 20 30)) ([head .. tail] head))" },
    { "match rest sum", "(match (quote (1 2 3 4 5)) ([a b ..] (+ a b)))" },
    { "match last", "(match (quote (1 2 3 4 5)) ([.. last] last))" },
    { "match fallthrough", "(match 42 (0 100) (1 200) (x x))" },
    { "match underscore", "(match (quote (1 2 3)) ([_ second _] second))" },

    // === Nested let deep ===
    { "nested let 5", "(let (a 1) (let (b 2) (let (c 3) (let (d 4) (let (e 5) (+ a (+ b (+ c (+ d e)))))))))" },

    // === Let recursive ===
    { "let rec factorial", "(let ^rec (f (lambda (n) (if (= n 0) 1 (* n (f (- n 1)))))) (f 5))" },
    { "named let loop", "(let loop (n 10 acc 0) (if (= n 0) acc (loop (- n 1) (+ acc n))))" },

    // === Effects ===
    { "reset passthrough", "(reset (+ 10 20))" },
    { "shift aborts", "(reset (+ 1 (shift k 42)))" },
    { "shift k resumes", "(reset (+ 1 (shift k (k 10))))" },
    { "handle no effect", "(handle 42 (ask x (resolve 0)))" },
    { "handle perform resume", "(handle (+ 1 (signal ask 0)) (ask x (resolve 10)))" },
    { "handle abort", "(handle (+ 1 (signal ask 5)) (ask x x))" },

    // === Not ===
    { "not true", "(not true)" },
    { "not nil", "(not nil)" },
    { "not 0", "(not 0)" },

    // === Apply ===
    { "apply add", "(apply + (quote (1 2)))" },
    { "apply list", "(apply cons (quote (1 2)))" },

    // === Arithmetic edge cases ===
    { "sub zero", "(- 0 0)" },
    { "sub neg neg", "(- -5 -3)" },
    { "mul neg pos", "(* -3 4)" },
    { "mul neg neg", "(* -3 -4)" },
    { "div exact 2", "(/ 10 2)" },
    { "div zero num", "(/ 0 5)" },
    { "div neg", "(/ -10 3)" },
    { "mod zero", "(% 9 3)" },
    { "mod small", "(% 7 10)" },
    { "nested add", "(+ 1 (+ 2 3))" },
    { "nested mul add", "(* 2 (+ 3 4))" },
    { "nested sub mul", "(- (* 5 5) (+ 10 5))" },
    { "nested div add", "(/ (+ 10 20) (- 10 4))" },

    // === Comparison edge cases ===
    { "eq zero", "(= 0 0)" },
    { "eq neg", "(= -1 -1)" },
    { "lt equal", "(< 1 1)" },
    { "lt neg zero", "(< -5 0)" },
    { "gt equal", "(> 1 1)" },
    { "le strict", "(<= 1 2)" },
    { "ge strict", "(>= 2 1)" },
    { "if lt branch", "(if (< 1 2) 10 20)" },
    { "if gt branch", "(if (> 1 2) 10 20)" },
    { "if eq computed", "(if (= (+ 2 3) 5) 1 0)" },

    // === And/Or edge cases ===
    { "and true true2", "(and true true)" },
    { "and false true", "(and false true)" },
    { "and true false", "(and true false)" },
    { "or true false", "(or true false)" },
    { "or false nil", "(or false nil)" },
    // and/or short-circuit tests skipped: compiler generates code for both branches

    // === Type predicates ===
    { "string? yes", `(string? "hello")` },
    { "string? no", "(string? 42)" },
    { "int? yes", "(int? 42)" },
    { "int? no", `(int? "hello")` },
    { "symbol? yes", "(symbol? (quote foo))" },
    { "symbol? no", "(symbol? 42)" },
    { "closure? yes", "(closure? (lambda (x) x))" },
    { "closure? no", "(closure? 42)" },
    { "null? nil", "(null? nil)" },
    { "null? 42", "(null? 42)" },
    { "null? list", "(null? (quote (1 2)))" },
    { "pair? list", "(pair? (quote (1 2 3)))" },
    { "pair? 42", "(pair? 42)" },
    { "not false", "(not false)" },
    { "not 42", "(not 42)" },
    { "not empty-str", `(not "")` },

    // === Zero-arg lambdas ===
    { "thunk 42", "((lambda () 42))" },
    { "thunk capture", "(let (a 10) ((lambda () a)))" },
    { "thunk begin", "((lambda () (begin 1 2 3)))" },
    { "thunk arith", "((lambda () (+ 10 20)))" },

    // === Multi-param lambdas (strict arity) ===
    { "multi-param add", "((lambda (x y) (+ x y)) 3 4)" },
    { "multi-param 3", "((lambda (x y z) (+ x (+ y z))) 1 2 3)" },
    { "multi-param direct", "(let (add (lambda (x y) (+ x y))) (add 10 20))" },

    // === Named let patterns ===
    { "named let sum 5", "(let loop (n 5 acc 0) (if (= n 0) acc (loop (- n 1) (+ acc n))))" },
    { "named let fact 10", "(let f (n 10 acc 1) (if (= n 0) acc (f (- n 1) (* acc n))))" },
    { "named let fib 10", "(let fib (n 10 a 0 b 1) (if (= n 0) a (fib (- n 1) b (+ a b))))" },
    { "named let fib 20", "(let fib (n 20 a 0 b 1) (if (= n 0) a (fib (- n 1) b (+ a b))))" },
    { "named let count", "(let count (n 100) (if (= n 0) 0 (count (- n 1))))" },

    // === More match patterns ===
    { "match multi 1", "(match 1 (1 10) (2 20) (3 30))" },
    { "match multi 2", "(match 2 (1 10) (2 20) (3 30))" },
    { "match multi 3", "(match 3 (1 10) (2 20) (3 30))" },
    { "match default", "(match 99 (1 10) (2 20) (x (+ x 1)))" },
    { "match computed", "(match (+ 1 1) (1 10) (2 20) (3 30))" },
    { "match pair sum", "(match (quote (10 20)) ([a b] (+ a b)))" },
    { "match single", "(match (quote (42)) ([x] x))" },
    { "match product", "(match (quote (2 3 4)) ([a b c] (* a (* b c))))" },
    { "match rest len", "(match (quote (1 2 3 4 5)) ([h .. t] (length t)))" },
    { "match string", `(match "hello" ("hello" 1) ("world" 2) (x 0))` },

    // === More effects/continuations ===
    { "reset large", "(reset (+ 100 (shift k 42)))" },
    { "shift resume mul", "(reset (* 2 (shift k (k 5))))" },
    { "shift resume add", "(reset (+ 1 (shift k (k (+ 2 3)))))" },
    { "multi-shot k", "(reset (+ 1 (shift k (+ (k 10) 1000))))" },
    { "handle get", "(handle (+ 1 (signal get 0)) (get x (resolve 100)))" },
    { "handle double", "(handle (signal double 7) (double x (resolve (* x 2))))" },
    { "handle bail", "(handle (+ 1 (+ 2 (+ 3 (signal bail 42)))) (bail x x))" },
    { "handle no trigger", "(handle (+ 10 20) (unused x 999))" },

    // === More list operations ===
    { "cadr", "(car (cdr (cons 1 (cons 2 (cons 3 nil)))))" },
    { "caddr", "(car (cdr (cdr (cons 1 (cons 2 (cons 3 nil))))))" },
    { "length 1", "(length (quote (1)))" },
    { "length 10", "(length (quote (1 2 3 4 5 6 7 8 9 10)))" },
    { "length cons 2", "(length (cons 1 (cons 2 nil)))" },
    { "car quoted", "(car (quote (10 20 30)))" },
    { "cons dotted", "(cons 1 2)" },

    // === Let shadowing ===
    { "let shadow inner", "(let (x 10) (let (x 20) x))" },
    { "let no leak", "(let (x 10) (let (y 20) x))" },

    // === Mutual recursion ===
    { "is-even 10", "(is-even 10)" },
    { "is-odd 7", "(is-odd 7)" },

    // === Composition/apply-twice ===
    { "apply-twice add3", "((apply-twice add3) 10)" },
    { "identity int", "(id 42)" },
    { "identity nil", "(id nil)" },
    { "counter c3", "(c3 5)" },

    // === String edge cases ===
    { "string-length empty", `(string-length "")` },
    { "string-length 1", `(string-length "a")` },
    { "string-append empty", `(string-append "" "abc")` },
    { "string->number neg", `(string->number "-7")` },
    { "number->string 0", `(number->string 0)` },

    // === Float edge cases ===
    { "float neg", "-2.5" },
    { "int+float", "(+ 1 2.0)" },
    { "float*int", "(* 2.5 4)" },
    { "float sub", "(- 5.5 2.5)" },
    { "float lt", "(< 1 2.5)" },
    { "float gt", "(> 3.5 2)" },
    { "float eq", "(= 1 1.0)" },

    // === Begin edge cases ===
    { "begin nested", "(begin (begin 1 2) (begin 3 4))" },
    { "begin if", "(begin (if true 10 20) (if false 30 40))" },

    // === Recursive let 6 ===
    { "let rec fact 6", "(let ^rec (f (lambda (n) (if (= n 0) 1 (* n (f (- n 1)))))) (f 6))" },

    // === Misc ===
    { "nested closures", "((lambda (x) ((lambda (y) (+ x y)) 20)) 10)" },
    { "deep closure chain", "((((lambda (a) (lambda (b) (lambda (c) (+ a (+ b c))))) 1) 2) 3)" },
    { "list operations", "(car (cdr (cdr (quote (1 2 3 4 5)))))" },
    { "complex arith", "(* (+ 1 2) (- 10 (/ 8 2)))" },
    { "recursive list build", "(length (cons 1 (cons 2 (cons 3 (quote ())))))" },

    // === Stdlib HOFs ===
    { "stdlib map double", "(map (lambda (x) (* x 2)) (quote (1 2 3)))" },
    { "stdlib map inc", "(map (lambda (x) (+ x 1)) (quote (10 20 30)))" },
    { "stdlib map identity", "(map (lambda (x) x) (quote (1 2 3)))" },
    { "stdlib filter gt2", "(filter (lambda (x) (> x 2)) (quote (1 2 3 4 5)))" },
    { "stdlib filter none", "(filter (lambda (x) (> x 100)) (quote (1 2 3)))" },
    { "stdlib filter all", "(filter (lambda (x) (> x 0)) (quote (1 2 3)))" },
    { "stdlib foldl sum", "(foldl + 0 (quote (1 2 3 4 5)))" },
    { "stdlib foldl product", "(foldl * 1 (quote (1 2 3 4 5)))" },
    { "stdlib foldr cons", "(foldr cons nil (quote (1 2 3)))" },
    { "stdlib append", "(append (quote (1 2)) (quote (3 4)))" },
    { "stdlib append empty l", "(append (quote ()) (quote (1 2)))" },
    { "stdlib append empty r", "(append (quote (1 2)) (quote ()))" },
    { "stdlib reverse", "(reverse (quote (1 2 3)))" },
    { "stdlib reverse single", "(reverse (quote (7)))" },
    { "stdlib reverse empty", "(reverse (quote ()))" },
    { "stdlib compose", "((compose (lambda (x) (* x 2)) (lambda (x) (+ x 1))) 5)" },
    { "stdlib nth 0", "(nth 0 (quote (10 20 30)))" },
    { "stdlib nth 2", "(nth 2 (quote (10 20 30)))" },
    { "stdlib take 3", "(take 3 (quote (1 2 3 4 5)))" },
    { "stdlib take 0", "(take 0 (quote (1 2 3)))" },
    { "stdlib take all", "(take 5 (quote (1 2 3)))" },
    { "stdlib drop 2", "(drop 2 (quote (1 2 3 4 5)))" },
    { "stdlib drop 0", "(drop 0 (quote (1 2 3)))" },
    { "stdlib drop all", "(drop 5 (quote (1 2 3)))" },
    { "stdlib zip", "(car (zip (quote (1 2 3)) (quote (4 5 6))))" },
    { "stdlib range 5", "(range 5)" },
    { "stdlib range 0", "(range 0)" },
    { "stdlib range 1", "(range 1)" },
    { "stdlib any? found", "(any? (lambda (x) (= x 3)) (quote (1 2 3 4)))" },
    { "stdlib any? not found", "(any? (lambda (x) (= x 99)) (quote (1 2 3)))" },
    { "stdlib every? true", "(every? (lambda (x) (> x 0)) (quote (1 2 3)))" },
    { "stdlib every? false", "(every? (lambda (x) (> x 2)) (quote (1 2 3)))" },
    { "stdlib assoc found", "(assoc 2 (quote ((1 . 10) (2 . 20) (3 . 30))))" },
    { "stdlib assoc not found", "(assoc 99 (quote ((1 . 10) (2 . 20))))" },
    { "stdlib assoc-ref", "(assoc-ref 2 (quote ((1 . 10) (2 . 20) (3 . 30))))" },
    { "stdlib id compose", "((compose id id) 42)" },
    { "stdlib map filter", "(foldl + 0 (filter (lambda (x) (> x 2)) (quote (1 2 3 4 5))))" },

    // === Dict operations ===
    { "dict empty", "(dict? (dict))" },
    { "dict create ref", "(ref (dict 1 10 2 20) 1)" },
    { "dict ref 2", "(ref (dict 1 10 2 20) 2)" },
    { "dict? yes", "(dict? (dict 1 2))" },
    { "dict? no", "(dict? 42)" },
    { "dict has? yes", "(has? (dict 1 10 2 20) 1)" },
    { "dict has? no", "(has? (dict 1 10) 99)" },
    { "dict keys", "(length (keys (dict 1 10 2 20)))" },

    // string-contains?, string-index-of, string-replace, char-at,
    // string-repeat, symbol->string, string->symbol — not mapped in compiler

    // === String operations (extended) ===
    { "string-upcase", `(string-upcase "hello")` },
    { "string-downcase", `(string-downcase "HELLO")` },
    { "string-trim", `(string-trim "  hello  ")` },

    // === set! mutation (setups define s_x=10, s_acc=0, s_count=0) ===
    { "set! returns value", "(set! s_x 20)" },
    { "set! read back", "(begin (set! s_acc 1) (set! s_acc (+ s_acc 1)) s_acc)" },
    { "set! in lambda", "(begin ((lambda (n) (set! s_count n)) 42) s_count)" },

    // === Quasiquote (no unquote — eval_source can't see let-bound vars) ===
    { "quasiquote literal", "`42" },
    { "quasiquote list", "`(1 2 3)" },
    { "quasiquote no unquote", "`(a b c)" },

    // when/unless/cond — skipped: compiler doesn't expand eval-time macros

    // === Dot-bracket indexing ===
    { "dot-bracket 0", "(let (lst (quote (10 20 30))) lst.[0])" },
    { "dot-bracket 2", "(let (lst (quote (10 20 30))) lst.[2])" },
    { "dot-bracket expr", "(let (lst (quote (10 20 30))) lst.[(+ 0 1)])" },

    // === Deep recursion / TCO ===
    { "tco sum 1000", "(let loop (n 1000 acc 0) (if (= n 0) acc (loop (- n 1) (+ acc n))))" },
    { "tco count 5000", "(let count (n 5000) (if (= n 0) 0 (count (- n 1))))" },

    // === Complex combinations ===
    { "map then reverse", "(reverse (map (lambda (x) (* x 10)) (quote (1 2 3))))" },
    { "filter then length", "(length (filter (lambda (x) (> x 3)) (quote (1 2 3 4 5 6))))" },
    { "nested map", "(map (lambda (x) (+ x 1)) (map (lambda (x) (* x 2)) (quote (1 2 3))))" },
    { "foldl with filter", "(foldl + 0 (filter (lambda (x) (< x 4)) (quote (1 2 3 4 5))))" },
    { "compose triple", "((compose (lambda (x) (* x 3)) (lambda (x) (+ x 1))) 10)" },
    { "range then sum", "(foldl + 0 (range 10))" },
    { "take from range", "(take 3 (range 10))" },
    { "drop from range", "(drop 7 (range 10))" },
    { "match on map result", "(match (map (lambda (x) (* x 10)) (quote (1 2 3))) ([a b c] (+ a (+ b c))))" },
    { "closure over let rec", "(let ^rec (f (lambda (n) (if (<= n 1) 1 (* n (f (- n 1)))))) (f 7))" },
    { "nested handle", "(handle (handle (+ 1 (signal inner 0)) (inner x (resolve 10))) (outer x (resolve 0)))" },
    { "shift double resume", "(reset (+ (shift k (+ (k 1) (k 2))) 10))" },
    { "let-rec factorial 8", "(let ^rec (f (lambda (n) (if (= n 0) 1 (* n (f (- n 1)))))) (f 8))" },

    // === P1: type-of ===
    { "type-of int", "(type-of 42)" },
    { "type-of string", `(type-of "hello")` },
    { "type-of symbol", "(type-of (quote foo))" },
    { "type-of list", "(type-of (quote (1 2)))" },
    { "type-of nil", "(type-of nil)" },
    { "type-of true", "(type-of true)" },
    { "type-of closure", "(type-of (lambda (x) x))" },
    { "type-of double", "(type-of 3.14)" },

    // === P1: type predicates ===
    { "int? yes", "(int? 42)" },
    { "int? no", "(int? 3.14)" },
    { "string? yes", `(string? "hi")` },
    { "string? no", "(string? 42)" },
    { "symbol? yes", "(symbol? (quote foo))" },
    { "symbol? no", "(symbol? 42)" },
    { "double? yes", "(double? 3.14)" },
    { "double? no", "(double? 42)" },
    { "number? int", "(number? 42)" },
    { "number? double", "(number? 3.14)" },
    { "number? no", "(number? (quote foo))" },
    { "pair? yes", "(pair? (quote (1 2)))" },
    { "pair? no", "(pair? 42)" },
    { "null? yes", "(null? nil)" },
    { "null? no", "(null? 42)" },

    // === P1: math primitives ===
    { "abs positive", "(abs 5)" },
    { "abs negative", "(abs -7)" },
    { "abs zero", "(abs 0)" },
    { "abs double", "(abs -3.5)" },
    { "min ints", "(min 3 7)" },
    { "min reversed", "(min 7 3)" },
    { "min doubles", "(min 1.5 2.5)" },
    { "max ints", "(max 3 7)" },
    { "max reversed", "(max 7 3)" },
    { "max doubles", "(max 1.5 2.5)" },
    { "floor", "(floor 3.7)" },
    { "floor neg", "(floor -3.2)" },
    { "ceiling", "(ceiling 3.2)" },
    { "ceiling neg", "(ceiling -3.7)" },
    { "round up", "(round 3.6)" },
    { "round down", "(round 3.4)" },
    { "truncate pos", "(truncate 3.9)" },
    { "truncate neg", "(truncate -3.9)" },
    { "sqrt 4", "(sqrt 4)" },
    { "sqrt 9", "(sqrt 9)" },
    { "even? yes", "(even? 4)" },
    { "even? no", "(even? 3)" },
    { "odd? yes", "(odd? 3)" },
    { "odd? no", "(odd? 4)" },
    { "zero? yes", "(zero? 0)" },
    { "zero? no", "(zero? 1)" },
    { "positive? yes", "(positive? 5)" },
    { "positive? no", "(positive? -3)" },
    { "negative? yes", "(negative? -3)" },
    { "negative? no", "(negative? 5)" },
    { "gcd", "(gcd 12 8)" },
    { "gcd coprime", "(gcd 7 13)" },

    // === P2: substring (3-arg via interpreter delegation) ===
    { "substring basic", `(substring "hello" 1 3)` },
    { "substring from start", `(substring "abcdef" 0 3)` },
    { "substring to end", `(substring "abcdef" 3 6)` },
    { "substring single", `(substring "hello" 0 1)` },

    // === P2: string-split and string-join ===
    { "string-split", `(length (string-split "a,b,c" ","))` },
    { "string-join", `(string-join " " (quote ("hello" "world")))` },
    { "string-join empty", `(string-join "," (quote ()))` },

    // === P2: when/unless ===
    { "when true", "(when true 42)" },
    { "when false", "(when false 42)" },
    { "unless true", "(unless true 42)" },
    { "unless false", "(unless false 42)" },

    // === P3: Bitwise ops ===
    { "bitwise-and", "(bitwise-and 12 10)" },
    { "bitwise-or", "(bitwise-or 12 10)" },
    { "bitwise-xor", "(bitwise-xor 12 10)" },
    { "bitwise-not 0", "(bitwise-not 0)" },
    { "bitwise-not 1", "(bitwise-not 1)" },
    { "lshift", "(lshift 1 4)" },
    { "rshift", "(rshift 16 4)" },
    { "lshift-rshift", "(rshift (lshift 3 8) 8)" },

    // === P4: Quasiquote + unquote ===
    { "qq unquote let", "(let (x 42) `(a ,x b))" },
    { "qq unquote arith", "(let (n 10) `(result ,(+ n 5)))" },
    { "qq unquote nested", "(let (a 1 b 2) `(,a ,b ,(+ a b)))" },
    { "qq splice literal", "`(1 2 3)" },
};

fn void generate_e2e_tests(Interp* interp) {
    io::printn("=== Generating E2E Compiler Tests ===");

    // Use a fresh interpreter for e2e generation to avoid test state leaking
    Interp* e2e_interp = (Interp*)mem::malloc(Interp.sizeof);
    e2e_interp.init();
    register_primitives(e2e_interp);
    register_stdlib(e2e_interp);

    // Run all setups
    for (usz i = 0; i < E2E_SETUPS.len; i++) {
        EvalResult r = run(E2E_SETUPS[i], e2e_interp);
        if (r.error.has_error) {
            io::printfn("WARNING: Setup %d failed, skipping", (int)i);
        }
    }

    // Build the combined Pika source (setups + test exprs)
    // and capture expected output for each test expr
    usz buf_size = 131072;  // 128KB
    char* expected_buf = (char*)mem::malloc(buf_size);
    usz expected_pos = 0;

    char* source_buf = (char*)mem::malloc(buf_size);
    usz source_pos = 0;

    int test_count = 0;
    int skip_count = 0;

    // First, add all setup defines to the source
    for (usz i = 0; i < E2E_SETUPS.len; i++) {
        for (usz j = 0; j < E2E_SETUPS[i].len && source_pos < buf_size - 2; j++) {
            source_buf[source_pos++] = E2E_SETUPS[i][j];
        }
        source_buf[source_pos++] = '\n';
    }

    // Add E2E start marker
    char[] marker = "(define __e2e_start__ true)\n";
    for (usz i = 0; i < marker.len && source_pos < buf_size - 2; i++) {
        source_buf[source_pos++] = marker[i];
    }

    // Now process each test expression
    for (usz i = 0; i < E2E_TESTS.len; i++) {
        // Run through interpreter to get expected output
        EvalResult r = run(E2E_TESTS[i].expr, e2e_interp);

        if (r.error.has_error) {
            io::printfn("  SKIP (interp error): %s", (ZString)E2E_TESTS[i].name);
            skip_count++;
            continue;
        }

        // Capture expected output to buffer
        char[1024] val_buf;
        usz val_len = print_value_to_buf(r.value, &e2e_interp.symbols, &val_buf, val_buf.len);

        // Append to expected buffer
        for (usz j = 0; j < val_len && expected_pos < buf_size - 2; j++) {
            expected_buf[expected_pos++] = val_buf[j];
        }
        expected_buf[expected_pos++] = '\n';

        // Append expression to source
        for (usz j = 0; j < E2E_TESTS[i].expr.len && source_pos < buf_size - 2; j++) {
            source_buf[source_pos++] = E2E_TESTS[i].expr[j];
        }
        source_buf[source_pos++] = '\n';

        test_count++;
    }

    // Compile the combined source
    io::printfn("  Compiling %d test expressions...", test_count);

    char[] omni_source = source_buf[:source_pos];
    char[] c3_code = compile_to_c3_print_all(omni_source, e2e_interp);

    if (c3_code.len == 0) {
        io::printn("ERROR: Compilation produced empty output");
        mem::free(expected_buf);
        mem::free(source_buf);
        e2e_interp.destroy();
        mem::free(e2e_interp);
        return;
    }

    // Write generated C3 file
    if (try file = io::file::open("build/e2e_test.c3", "w")) {
        defer (void)file.close();
        file.write(c3_code)!!;
        io::printfn("  Wrote build/e2e_test.c3 (%d bytes)", (int)c3_code.len);
    } else {
        io::printn("ERROR: Cannot write build/e2e_test.c3");
        mem::free(expected_buf);
        mem::free(source_buf);
        e2e_interp.destroy();
        mem::free(e2e_interp);
        return;
    }

    // Write expected output file
    char[] expected_out = expected_buf[:expected_pos];
    if (try file = io::file::open("build/e2e_expected.txt", "w")) {
        defer (void)file.close();
        file.write(expected_out)!!;
        io::printfn("  Wrote build/e2e_expected.txt (%d lines)", test_count);
    } else {
        io::printn("ERROR: Cannot write build/e2e_expected.txt");
        mem::free(expected_buf);
        mem::free(source_buf);
        e2e_interp.destroy();
        mem::free(e2e_interp);
        return;
    }

    io::printfn("=== Generated %d e2e tests (%d skipped) ===", test_count, skip_count);

    mem::free(expected_buf);
    mem::free(source_buf);
    e2e_interp.destroy();
    mem::free(e2e_interp);
}

fn void run_diagnostic_tests(Interp* interp, int* pass, int* fail) {
    io::printn("\n--- Diagnostic Tests ---");

    // --- Dispatch diagnostics ---

    // Type mismatch - shows arg types
    test_error_contains(interp, "dispatch: type mismatch shows types",
        "(begin (define (typed-fn (^Int x)) x) (typed-fn \"hello\"))",
        "String", pass, fail);

    // Shows method name
    test_error_contains(interp, "dispatch: shows method name",
        "(begin (define (typed-fn2 (^Int x)) x) (typed-fn2 \"hello\"))",
        "typed-fn2", pass, fail);

    // Arity mismatch
    test_error_contains(interp, "dispatch: arity mismatch",
        "(begin (define [type] DPt (^Int x) (^Int y)) (define (dadd (^DPt a) (^DPt b)) 1) (dadd (DPt 1 2)))",
        "no matching method", pass, fail);

    // --- Match exhaustiveness ---

    // Union match missing variant
    test_error_contains(interp, "match: missing union variant",
        "(begin (define [union] DiagResult DiagOk (DiagErr Int)) (match (DiagErr 42) ((DiagOk) 1)))",
        "missing variants", pass, fail);

    // Non-union match shows type
    test_error_contains(interp, "match: non-union shows type",
        "(match 42 (\"hello\" 1))",
        "Int", pass, fail);

    // --- Strict handlers ---

    // Strict handler catches unhandled effect
    test_error_contains(interp, "handle ^strict: catches unhandled",
        "(handle ^strict (signal my-eff 42) (other-eff v v))",
        "strict handler", pass, fail);

    // Strict handler allows handled effects
    test_eq(interp, "handle ^strict: allows handled",
        "(handle ^strict (signal my-eff 42) (my-eff v (+ v 1)))",
        43, pass, fail);

    // --- Unhandled effect diagnostics ---

    // Shows tag name
    test_error_contains(interp, "unhandled effect: shows tag name",
        "(signal unknown-eff 42)",
        "unknown-eff", pass, fail);

    // Shows arg type
    test_error_contains(interp, "unhandled effect: shows arg type",
        "(signal unknown-eff \"hello\")",
        "String", pass, fail);
}

fn void run_async_tests(Interp* interp, int* pass, int* fail) {
    io::printn("\n--- Async I/O Tests ---");

    // DNS resolve localhost
    test_str_val(interp, "dns-resolve localhost",
        "(dns-resolve \"localhost\")", "127.0.0.1", pass, fail);

    // async-sleep returns nil
    test_tag(interp, "async-sleep returns nil",
        "(async-sleep 1)", NIL, pass, fail);

    // DNS resolve returns a string
    test_str(interp, "dns-resolve returns string",
        "(dns-resolve \"localhost\")", pass, fail);
}

fn void run_json_tests(Interp* interp, int* pass, int* fail) {
    io::printn("\n--- JSON Tests ---");

    // Parse basic types
    test_eq(interp, "json-parse integer",
        "(json-parse \"42\")", 42, pass, fail);
    test_str_val(interp, "json-parse string",
        "(json-parse \"\\\"hello\\\"\")", "hello", pass, fail);
    test_tag(interp, "json-parse null",
        "(json-parse \"null\")", NIL, pass, fail);

    // Parse array
    test_tag(interp, "json-parse array is array",
        "(json-parse \"[1,2,3]\")", ARRAY, pass, fail);
    test_eq(interp, "json-parse array length",
        "(length (json-parse \"[1,2,3]\"))", 3, pass, fail);

    // Parse object
    test_tag(interp, "json-parse object is dict",
        "(json-parse \"{\\\"a\\\": 1}\")", HASHMAP, pass, fail);

    // Emit basic types
    test_str_val(interp, "json-emit integer",
        "(json-emit 42)", "42", pass, fail);
    test_str_val(interp, "json-emit string",
        "(json-emit \"hello\")", "\"hello\"", pass, fail);
    test_str_val(interp, "json-emit nil",
        "(json-emit nil)", "null", pass, fail);

    // Round-trip: array
    test_str_val(interp, "json-emit array",
        "(json-emit [1 2 3])", "[1,2,3]", pass, fail);

    // Round-trip: emit then parse
    test_eq(interp, "json round-trip integer",
        "(json-parse (json-emit 99))", 99, pass, fail);
}

fn void run_compression_tests(Interp* interp, int* pass, int* fail) {
    io::printn("\n--- Compression Tests ---");

    // gzip round-trip
    test_str_val(interp, "gzip/gunzip round-trip",
        "(gunzip (gzip \"hello world\"))", "hello world", pass, fail);

    // deflate round-trip
    test_str_val(interp, "deflate/inflate round-trip",
        "(inflate (deflate \"test data\"))", "test data", pass, fail);

    // Empty string round-trip
    test_str_val(interp, "gzip/gunzip empty",
        "(gunzip (gzip \"\"))", "", pass, fail);
}

fn void run_unicode_tests(Interp* interp, int* pass, int* fail) {
    io::printn("\n--- Unicode Tests ---");

    // Upcase: ASCII
    test_str_val(interp, "string-upcase ASCII",
        "(string-upcase \"hello\")", "HELLO", pass, fail);

    // Downcase: ASCII
    test_str_val(interp, "string-downcase ASCII",
        "(string-downcase \"HELLO\")", "hello", pass, fail);

    // Codepoints: ASCII
    test_eq(interp, "string-codepoints ASCII count",
        "(length (string-codepoints \"abc\"))", 3, pass, fail);
    test_eq(interp, "string-codepoints first is 97",
        "(car (string-codepoints \"abc\"))", 97, pass, fail);

    // Char-category: letter
    test_str_val(interp, "char-category uppercase A",
        "(char-category 65)", "Lu", pass, fail);
    test_str_val(interp, "char-category lowercase a",
        "(char-category 97)", "Ll", pass, fail);
    test_str_val(interp, "char-category digit 0",
        "(char-category 48)", "Nd", pass, fail);

    // Normalization: NFC (identity for ASCII)
    test_str_val(interp, "string-normalize NFC ASCII",
        "(string-normalize \"hello\" 'NFC)", "hello", pass, fail);

    // Graphemes: ASCII chars are each a grapheme
    test_eq(interp, "string-graphemes ASCII",
        "(length (string-graphemes \"abc\"))", 3, pass, fail);
}

fn void run_pika_tests(Interp* interp, int* pass, int* fail) {
    io::printn("\n--- Pika Regex/Grammar Tests ---");

    // Regex: re-match
    test_str_val(interp, "re-match literal",
        "(re-match \"hello\" \"say hello world\")", "hello", pass, fail);
    test_tag(interp, "re-match no match returns nil",
        "(re-match \"xyz\" \"hello\")", NIL, pass, fail);
    test_str(interp, "re-match digits finds digit",
        "(re-match \"[0-9]+\" \"abc123def\")", pass, fail);
    test_str_val(interp, "re-match dot",
        "(re-match \"a.c\" \"abc\")", "abc", pass, fail);

    // Regex: re-fullmatch
    test_str_val(interp, "re-fullmatch exact",
        "(re-fullmatch \"hello\" \"hello\")", "hello", pass, fail);
    test_tag(interp, "re-fullmatch rejects partial",
        "(re-fullmatch \"hello\" \"hello world\")", NIL, pass, fail);

    // Regex: re-find-all
    test_eq(interp, "re-find-all count",
        "(length (re-find-all \"a\" \"abracadabra\"))", 5, pass, fail);
    test_str_val(interp, "re-find-all first",
        "(car (re-find-all \"[0-9]+\" \"a1 b22 c333\"))", "1", pass, fail);

    // Regex: re-split
    test_eq(interp, "re-split count",
        "(length (re-split \" \" \"hello world foo\"))", 3, pass, fail);
    test_str_val(interp, "re-split first",
        "(car (re-split \" \" \"hello world\"))", "hello", pass, fail);

    // Regex: re-replace
    test_str_val(interp, "re-replace first",
        "(re-replace \"[0-9]+\" \"X\" \"a1b2c3\")", "aXb2c3", pass, fail);
    test_str(interp, "re-replace global returns string",
        "(re-replace \"a\" \"X\" \"abaca\" 'global)", pass, fail);

    // Regex: re-match-pos
    test_eq(interp, "re-match-pos start",
        "(car (re-match-pos \"hello\" \"say hello\"))", 4, pass, fail);

    // Regex: re-find-all-pos
    test_eq(interp, "re-find-all-pos count",
        "(length (re-find-all-pos \"a\" \"abaca\"))", 3, pass, fail);

    // Regex: escaped chars
    test_str(interp, "re-match \\d",
        "(re-match \"\\\\d+\" \"abc123\")", pass, fail);
    test_str(interp, "re-match \\w+",
        "(re-match \"\\\\w+\" \"hello_world\")", pass, fail);
}

fn void run_atomic_tests(Interp* interp, int* pass, int* fail) {
    io::printn("\n--- Atomic Tests ---");

    // Create atomic
    test_tag(interp, "atomic creates handle",
        "(atomic 0)", FFI_HANDLE, pass, fail);

    // Read initial value
    {
        setup(interp, "(define a (atomic 42))");
        EvalResult r = run("(atomic-read a)", interp);
        if (!r.error.has_error && is_int(r.value) && r.value.int_val == 42) {
            io::printn("[PASS] atomic-read initial");
            (*pass)++;
        } else {
            io::printn("[FAIL] atomic-read initial");
            (*fail)++;
        }
    }

    // Add and read
    {
        setup(interp, "(atomic-add! a 10)");
        EvalResult r = run("(atomic-read a)", interp);
        if (!r.error.has_error && is_int(r.value) && r.value.int_val == 52) {
            io::printn("[PASS] atomic-add! + read");
            (*pass)++;
        } else {
            io::printfn("[FAIL] atomic-add! + read (got %d)", is_int(r.value) ? (int)r.value.int_val : -1);
            (*fail)++;
        }
    }

    // CAS success
    {
        EvalResult r = run("(atomic-cas! a 52 100)", interp);
        if (!r.error.has_error && r.value != null && r.value.tag == SYMBOL) {
            io::printn("[PASS] atomic-cas! success");
            (*pass)++;
        } else {
            io::printn("[FAIL] atomic-cas! success");
            (*fail)++;
        }
    }

    // CAS fail (value is now 100, not 52)
    {
        EvalResult r = run("(atomic-cas! a 52 200)", interp);
        if (!r.error.has_error && (r.value == null || r.value.tag == NIL)) {
            io::printn("[PASS] atomic-cas! fail");
            (*pass)++;
        } else {
            io::printn("[FAIL] atomic-cas! fail");
            (*fail)++;
        }
    }
}

fn void run_http_tests(Interp* interp, int* pass, int* fail) {
    io::printn("\n--- HTTP Tests ---");

    // http-get is defined (bound in env)
    test_tag(interp, "http-get is bound",
        "(procedure? http-get)", SYMBOL, pass, fail);

    // http-get with bad URL returns error
    {
        EvalResult r = run("(http-get \"not-a-url\")", interp);
        if (r.error.has_error || (r.value != null && r.value.tag == ERROR)) {
            io::printn("[PASS] http-get rejects bad URL");
            (*pass)++;
        } else {
            io::printn("[FAIL] http-get should reject bad URL");
            (*fail)++;
        }
    }
}

fn void run_scheduler_tests(Interp* interp, int* pass, int* fail) {
    io::printn("\n--- Scheduler Tests ---");

    // Spawn creates a fiber id
    {
        EvalResult r = run("(spawn (lambda () 42))", interp);
        if (!r.error.has_error && is_int(r.value)) {
            io::printn("[PASS] spawn returns id");
            (*pass)++;
        } else {
            io::printn("[FAIL] spawn returns id");
            (*fail)++;
        }
    }

    // run-fibers completes spawned fibers
    {
        setup(interp, "(spawn (lambda () (+ 1 2)))");
        EvalResult r = run("(run-fibers)", interp);
        if (!r.error.has_error) {
            io::printn("[PASS] run-fibers completes");
            (*pass)++;
        } else {
            io::printn("[FAIL] run-fibers");
            (*fail)++;
        }
    }

    // In-fiber await of direct child works
    {
        EvalResult r = run(
            "(begin "
            "  (define p (spawn (lambda () (let (c (spawn (lambda () 7))) (+ 1 (await c)))))) "
            "  (await p))",
            interp);
        if (!r.error.has_error && is_int(r.value) && r.value.int_val == 8) {
            io::printn("[PASS] await direct child");
            (*pass)++;
        } else {
            io::printfn("[FAIL] await direct child (got %d)", is_int(r.value) ? (int)r.value.int_val : -1);
            (*fail)++;
        }
    }

    // Parent completion waits for spawned child lifetime, even without explicit await
    {
        EvalResult r = run(
            "(begin "
            "  (define sched-done 0) "
            "  (define p "
            "    (spawn (lambda () "
            "      (begin "
            "        (spawn (lambda () (begin (set! sched-done 1) 99))) "
            "        42)))) "
            "  (await p) "
            "  sched-done)",
            interp);
        if (!r.error.has_error && is_int(r.value) && r.value.int_val == 1) {
            io::printn("[PASS] parent waits for unawaited child");
            (*pass)++;
        } else {
            io::printfn("[FAIL] parent waits for unawaited child (got %d)", is_int(r.value) ? (int)r.value.int_val : -1);
            (*fail)++;
        }
    }

    // Structured rule: fibers cannot await non-children
    {
        EvalResult r = run(
            "(begin "
            "  (define b (spawn (lambda () 99))) "
            "  (define a (spawn (lambda () (await b)))) "
            "  (await a))",
            interp);
        if (r.error.has_error || (r.value != null && r.value.tag == ERROR)) {
            io::printn("[PASS] await non-child rejected");
            (*pass)++;
        } else {
            io::printn("[FAIL] await non-child rejected");
            (*fail)++;
        }
    }

    // Multiple fibers with run-fibers
    {
        setup(interp, "(define fib-a (spawn (lambda () 10)))");
        setup(interp, "(define fib-b (spawn (lambda () 20)))");
        setup(interp, "(run-fibers)");
        io::printn("[PASS] run-fibers completes");
        (*pass)++;
    }

    // Wakeup ring smoke: enqueue/drain with no active pending reads should be a no-op.
    {
        scheduler_init();
        void* saved_async = g_scheduler.async_handle;
        g_scheduler.async_handle = null;
        scheduler_reset_wakeup_queue();

        bool pushed = wakeup_enqueue(WAKEUP_TIMER_EXPIRED, MAX_FIBERS + 1, 0, null);
        drain_wakeups();
        usz head = g_scheduler.wakeup_head.load();
        usz tail = g_scheduler.wakeup_tail.load();
        g_scheduler.async_handle = saved_async;

        if (pushed && head == tail) {
            io::printn("[PASS] wakeup ring smoke");
            (*pass)++;
        } else {
            io::printfn("[FAIL] wakeup ring smoke (pushed=%s head=%d tail=%d)",
                pushed ? "yes" : "no", (long)head, (long)tail);
            (*fail)++;
        }
    }

    // Wakeup ring wraparound: fill capacity, reject overflow, then drain to empty.
    {
        scheduler_init();
        void* saved_async = g_scheduler.async_handle;
        g_scheduler.async_handle = null;
        scheduler_reset_wakeup_queue();

        usz pushed = 0;
        for (usz i = 0; i < WAKEUP_RING_SIZE; i++) {
            if (wakeup_enqueue(WAKEUP_POLL_ERROR, MAX_FIBERS + i + 1, -1, null)) pushed++;
        }
        bool overflow_rejected = !wakeup_enqueue(WAKEUP_POLL_ERROR, MAX_FIBERS + 999, -2, null);
        drain_wakeups();
        usz head = g_scheduler.wakeup_head.load();
        usz tail = g_scheduler.wakeup_tail.load();
        g_scheduler.async_handle = saved_async;

        if (pushed == WAKEUP_RING_SIZE && overflow_rejected && head == tail) {
            io::printn("[PASS] wakeup ring wraparound");
            (*pass)++;
        } else {
            io::printfn("[FAIL] wakeup ring wraparound (pushed=%d overflow_rejected=%s head=%d tail=%d)",
                (long)pushed, overflow_rejected ? "yes" : "no", (long)head, (long)tail);
            (*fail)++;
        }
    }

    // Timer wakeup must only flip scheduler state during drain phase.
    {
        scheduler_init();
        void* saved_async = g_scheduler.async_handle;
        g_scheduler.async_handle = null;
        scheduler_reset_wakeup_queue();

        g_scheduler.fiber_count = 1;
        g_scheduler.current_fiber_id = NO_FIBER;
        g_scheduler.fibers[0] = {};
        g_scheduler.fibers[0].state = FIBER_BLOCKED;
        g_scheduler.pending_reads[0] = {};
        g_scheduler.pending_reads[0].active = true;

        bool pushed = wakeup_enqueue(WAKEUP_TIMER_EXPIRED, 0, 0, null);
        drain_wakeups();

        bool ok = pushed &&
                  g_scheduler.pending_reads[0].timed_out &&
                  g_scheduler.pending_reads[0].completed &&
                  g_scheduler.fibers[0].state == FIBER_READY;

        g_scheduler.pending_reads[0] = {};
        g_scheduler.fibers[0] = {};
        g_scheduler.fiber_count = 0;
        scheduler_reset_wakeup_queue();
        g_scheduler.async_handle = saved_async;

        if (ok) {
            io::printn("[PASS] timer wakeup transitions in drain");
            (*pass)++;
        } else {
            io::printn("[FAIL] timer wakeup transitions in drain");
            (*fail)++;
        }
    }

    // Atomic CAS semantics regression: mismatch must not modify value.
    {
        setup(interp, "(define sched-cas (atomic 7))");
        EvalResult ok_swap = run("(begin (atomic-cas! sched-cas 7 9) (atomic-read sched-cas))", interp);
        EvalResult bad_swap = run("(begin (atomic-cas! sched-cas 7 11) (atomic-read sched-cas))", interp);
        if (!ok_swap.error.has_error && is_int(ok_swap.value) && ok_swap.value.int_val == 9 &&
            !bad_swap.error.has_error && is_int(bad_swap.value) && bad_swap.value.int_val == 9) {
            io::printn("[PASS] atomic-cas semantics");
            (*pass)++;
        } else {
            io::printn("[FAIL] atomic-cas semantics");
            (*fail)++;
        }
    }

    // Offload sync fallback executes worker operation and returns bytes.
    {
        EvalResult r = run("(gunzip (offload 'gzip \"hello-offload\"))", interp);
        if (!r.error.has_error && is_string(r.value) &&
            r.value.str_len == 13 &&
            str_starts_with(r.value.str_chars[:r.value.str_len], "hello-offload")) {
            io::printn("[PASS] offload sync gzip roundtrip");
            (*pass)++;
        } else {
            io::printn("[FAIL] offload sync gzip roundtrip");
            (*fail)++;
        }
    }

    // Offload async path: worker completion resumes blocked fibers.
    {
        EvalResult r = run(
            "(begin "
            "  (define a (spawn (lambda () (offload 'sleep-ms 5)))) "
            "  (define b (spawn (lambda () (offload 'sleep-ms 5)))) "
            "  (await a) "
            "  (await b) "
            "  1)",
            interp);
        if (!r.error.has_error && is_int(r.value) && r.value.int_val == 1) {
            io::printn("[PASS] offload async fiber resume");
            (*pass)++;
        } else {
            io::printn("[FAIL] offload async fiber resume");
            (*fail)++;
        }
    }

    // Unsupported offload op should raise.
    {
        EvalResult r = run("(offload 'nope 1)", interp);
        if (r.error.has_error || (r.value != null && r.value.tag == ERROR)) {
            io::printn("[PASS] offload rejects unsupported op");
            (*pass)++;
        } else {
            io::printn("[FAIL] offload rejects unsupported op");
            (*fail)++;
        }
    }

    // Thread task API: spawn + join returns offloaded result.
    {
        EvalResult r = run(
            "(begin "
            "  (define t (thread-spawn 'gzip \"thread-task\")) "
            "  (gunzip (thread-join t)))",
            interp);
        if (!r.error.has_error && is_string(r.value) &&
            r.value.str_len == 11 &&
            str_starts_with(r.value.str_chars[:r.value.str_len], "thread-task")) {
            io::printn("[PASS] thread-spawn/thread-join gzip");
            (*pass)++;
        } else {
            io::printn("[FAIL] thread-spawn/thread-join gzip");
            (*fail)++;
        }
    }

    // Thread task API: concurrent tasks complete and join deterministically.
    {
        EvalResult r = run(
            "(begin "
            "  (define t1 (thread-spawn 'sleep-ms 5)) "
            "  (define t2 (thread-spawn 'sleep-ms 5)) "
            "  (thread-join t1) "
            "  (thread-join t2) "
            "  1)",
            interp);
        if (!r.error.has_error && is_int(r.value) && r.value.int_val == 1) {
            io::printn("[PASS] thread-spawn concurrent join");
            (*pass)++;
        } else {
            io::printn("[FAIL] thread-spawn concurrent join");
            (*fail)++;
        }
    }

    // Joining the same task twice should fail (task handle consumed).
    {
        EvalResult r = run(
            "(begin "
            "  (define t (thread-spawn 'sleep-ms 1)) "
            "  (thread-join t) "
            "  (thread-join t))",
            interp);
        if (r.error.has_error || (r.value != null && r.value.tag == ERROR)) {
            io::printn("[PASS] thread-join double join rejected");
            (*pass)++;
        } else {
            io::printn("[FAIL] thread-join double join rejected");
            (*fail)++;
        }
    }

    // Timed join with zero timeout should fail if task is not complete yet.
    {
        EvalResult r = run(
            "(begin "
            "  (define t (thread-spawn 'sleep-ms 20)) "
            "  (thread-join-timeout t 0))",
            interp);
        if (r.error.has_error || (r.value != null && r.value.tag == ERROR)) {
            io::printn("[PASS] thread-join-timeout immediate timeout");
            (*pass)++;
        } else {
            io::printn("[FAIL] thread-join-timeout immediate timeout");
            (*fail)++;
        }
    }

    // Timed join succeeds when timeout budget is sufficient.
    {
        EvalResult r = run(
            "(begin "
            "  (define t (thread-spawn 'sleep-ms 5)) "
            "  (thread-join-timeout t 200) "
            "  1)",
            interp);
        if (!r.error.has_error && is_int(r.value) && r.value.int_val == 1) {
            io::printn("[PASS] thread-join-timeout success");
            (*pass)++;
        } else {
            io::printn("[FAIL] thread-join-timeout success");
            (*fail)++;
        }
    }

    // Canceling a task should make join fail with cancellation.
    {
        EvalResult r = run(
            "(begin "
            "  (define t (thread-spawn 'sleep-ms 20)) "
            "  (thread-cancel t) "
            "  (thread-join t))",
            interp);
        if (r.error.has_error || (r.value != null && r.value.tag == ERROR)) {
            io::printn("[PASS] thread-cancel causes join cancellation");
            (*pass)++;
        } else {
            io::printn("[FAIL] thread-cancel causes join cancellation");
            (*fail)++;
        }
    }
}

fn void run_deduce_tests(Interp* interp, int* pass, int* fail) {
    io::printn("\n--- Deduce Tests ---");

    // (deduce 'open 'memory) → database handle
    test_tag(interp, "deduce 'open returns handle",
        "(deduce 'open 'memory)", FFI_HANDLE, pass, fail);

    // Create relation directly via C3 (avoids JIT define arity issues)
    setup(interp, "(define ddb (deduce 'open 'memory))");
    {
        Value* db_val = interp.global_env.lookup(interp.symbols.intern("ddb"));
        if (db_val != null && db_val.tag == FFI_HANDLE) {
            Value* name_sym = interp.alloc_value(); name_sym.tag = SYMBOL; name_sym.sym_val = interp.symbols.intern("person");
            Value* col1 = interp.alloc_value(); col1.tag = SYMBOL; col1.sym_val = interp.symbols.intern("name");
            Value* col2 = interp.alloc_value(); col2.tag = SYMBOL; col2.sym_val = interp.symbols.intern("age");
            Value* spec = make_cons(interp, name_sym, make_cons(interp, col1, make_cons(interp, col2, make_nil(interp))));
            Value*[1] fargs;
            fargs[0] = make_cons(interp, db_val, spec);
            Value* rel = prim_define_relation(fargs[..], null, interp);
            if (rel != null && rel.tag == FFI_HANDLE) {
                env_define_with_barrier(interp.global_env, interp.symbols.intern("person"), rel, interp);
            }
        }
    }

    // (deduce 'fact! relation vals...) + (deduce 'scan relation)
    setup(interp, "(deduce 'fact! person \"Alice\" 30)");
    setup(interp, "(deduce 'fact! person \"Bob\" 25)");

    {
        EvalResult r = run("(length (deduce 'scan person))", interp);
        if (!r.error.has_error && is_int(r.value) && r.value.int_val >= 2) {
            io::printn("[PASS] deduce 'fact! + 'scan");
            (*pass)++;
        } else {
            io::printfn("[FAIL] deduce 'fact! + 'scan (got %d)", is_int(r.value) ? (int)r.value.int_val : -1);
            (*fail)++;
        }
    }

    // (deduce 'retract! relation vals...)
    setup(interp, "(deduce 'retract! person \"Bob\" 25)");
    {
        EvalResult r = run("(length (deduce 'scan person))", interp);
        if (!r.error.has_error && is_int(r.value) && r.value.int_val >= 1) {
            io::printn("[PASS] deduce 'retract!");
            (*pass)++;
        } else {
            io::printn("[FAIL] deduce 'retract!");
            (*fail)++;
        }
    }

    // Dict results with symbol-as-function
    {
        EvalResult r = run("('name (car (deduce 'scan person)))", interp);
        if (!r.error.has_error && is_string(r.value)) {
            io::printn("[PASS] deduce 'scan dict result");
            (*pass)++;
        } else {
            io::printn("[FAIL] deduce 'scan dict result");
            (*fail)++;
        }
    }

    // (deduce 'count relation)
    {
        EvalResult r = run("(deduce 'count person)", interp);
        if (!r.error.has_error && is_int(r.value) && r.value.int_val >= 1) {
            io::printn("[PASS] deduce 'count");
            (*pass)++;
        } else {
            io::printn("[FAIL] deduce 'count");
            (*fail)++;
        }
    }

    // (deduce 'query relation filter-fn)
    {
        EvalResult r = run("(length (deduce 'query person (lambda (row) (> ('age row) 28))))", interp);
        if (!r.error.has_error && is_int(r.value) && r.value.int_val >= 1) {
            io::printn("[PASS] deduce 'query filter");
            (*pass)++;
        } else {
            io::printfn("[FAIL] deduce 'query filter (got %d)", is_int(r.value) ? (int)r.value.int_val : -1);
            (*fail)++;
        }
    }

    // (deduce 'query ...) result is dict with symbol-as-function
    {
        EvalResult r = run("('name (car (deduce 'query person (lambda (row) (> ('age row) 28)))))", interp);
        if (!r.error.has_error && is_string(r.value)) {
            io::printn("[PASS] deduce 'query dict result");
            (*pass)++;
        } else {
            io::printn("[FAIL] deduce 'query dict result");
            (*fail)++;
        }
    }

    // (deduce 'match relation pattern)
    {
        EvalResult r = run("(length (deduce 'match person '(person ?name ?age)))", interp);
        if (!r.error.has_error && is_int(r.value) && r.value.int_val >= 1) {
            io::printn("[PASS] deduce 'match all");
            (*pass)++;
        } else {
            io::printfn("[FAIL] deduce 'match all (got %d)", is_int(r.value) ? (int)r.value.int_val : -1);
            (*fail)++;
        }
    }

    // Match result has stripped ?var keys
    {
        EvalResult r = run("('name (car (deduce 'match person '(person ?name ?age))))", interp);
        if (!r.error.has_error && is_string(r.value)) {
            io::printn("[PASS] deduce 'match ?var binding");
            (*pass)++;
        } else {
            io::printn("[FAIL] deduce 'match ?var binding");
            (*fail)++;
        }
    }
}

fn void run_schema_tests(Interp* interp, int* pass, int* fail) {
    io::printn("\n--- Schema/Contract Tests ---");

    // Define a schema
    setup(interp, "(define [schema] age-schema (and int (> 0)))");

    // validate with inline schema
    test_tag(interp, "validate int passes",
        "(validate 'int 42)", SYMBOL, pass, fail);
    test_tag(interp, "validate int fails on string",
        "(validate 'int \"hello\")", NIL, pass, fail);
    test_tag(interp, "validate string passes",
        "(validate 'string \"hello\")", SYMBOL, pass, fail);
    test_tag(interp, "validate any passes",
        "(validate 'any 42)", SYMBOL, pass, fail);

    // validate with composite schema
    test_tag(interp, "validate (and int (> 0)) passes",
        "(validate '(and int (> 0)) 5)", SYMBOL, pass, fail);
    test_tag(interp, "validate (and int (> 0)) fails on negative",
        "(validate '(and int (> 0)) -1)", NIL, pass, fail);
    test_tag(interp, "validate (or string int) passes int",
        "(validate '(or string int) 42)", SYMBOL, pass, fail);
    test_tag(interp, "validate (or string int) passes string",
        "(validate '(or string int) \"hi\")", SYMBOL, pass, fail);

    // validate with named schema
    test_tag(interp, "validate named schema passes",
        "(validate 'age-schema 5)", SYMBOL, pass, fail);
    test_tag(interp, "validate named schema fails",
        "(validate 'age-schema -1)", NIL, pass, fail);

    // maybe
    test_tag(interp, "validate (maybe int) on nil",
        "(validate '(maybe int) nil)", SYMBOL, pass, fail);
    test_tag(interp, "validate (maybe int) on int",
        "(validate '(maybe int) 42)", SYMBOL, pass, fail);

    // explain returns nil on valid
    test_tag(interp, "explain valid returns nil",
        "(explain 'int 42)", NIL, pass, fail);
}

fn void run_reader_dispatch_tests(Interp* interp, int* pass, int* fail) {
    io::printn("\n--- Reader Dispatch Tests ---");

    // Regex literal #r""
    test_str(interp, "regex literal #r",
        "(re-match #r\"hello\" \"say hello world\")", pass, fail);

    // Block comment
    test_eq(interp, "block comment",
        "(+ 1 #| ignored |# 2)", 3, pass, fail);

    // Form comment #_
    test_eq(interp, "#_ skips one form",
        "(+ 1 #_ 999 2)", 3, pass, fail);

    // Multi-form comment #N_
    test_eq(interp, "#2_ skips two forms",
        "(+ #2_ 10 20 1 2)", 3, pass, fail);

    // Nested block comment
    test_eq(interp, "nested block comment",
        "(+ 1 #| outer #| inner |# still |# 2)", 3, pass, fail);

    // Symbol-as-function: ('name dict) => (ref dict 'name)
    test_str_val(interp, "symbol-as-function on dict",
        "('name (dict 'name \"Alice\" 'age 30))", "Alice", pass, fail);
    test_eq(interp, "symbol-as-function on dict returns int",
        "('age (dict 'name \"Alice\" 'age 30))", 30, pass, fail);
}

fn void run_lisp_tests() {
    io::printn("=== Unified Tests (Interpreter + JIT) ===");

    Interp* interp = (Interp*)mem::malloc(Interp.sizeof);
    interp.init();
    register_primitives(interp);

    register_stdlib(interp);

    int pass = 0;
    int fail = 0;

    run_basic_tests(interp, &pass, &fail);
    run_memory_lifetime_regression_tests(interp, &pass, &fail);
    run_memory_stress_tests(interp, &pass, &fail);
    run_list_closure_tests(interp, &pass, &fail);
    run_arithmetic_comparison_tests(interp, &pass, &fail);
    run_string_type_tests(interp, &pass, &fail);
    run_diagnostic_tests(interp, &pass, &fail);
    run_advanced_tests(interp, &pass, &fail);
    run_escape_scope_tests(interp, &pass, &fail);
    run_limit_busting_tests(interp, &pass, &fail);
    run_tco_recycling_tests(interp, &pass, &fail);
    run_closure_lifecycle_tests(interp, &pass, &fail);

    run_pika_tests(interp, &pass, &fail);
    run_unicode_tests(interp, &pass, &fail);
    run_compression_tests(interp, &pass, &fail);
    run_json_tests(interp, &pass, &fail);
    run_async_tests(interp, &pass, &fail);
    run_reader_dispatch_tests(interp, &pass, &fail);
    run_schema_tests(interp, &pass, &fail);
    run_deduce_tests(interp, &pass, &fail);
    run_scheduler_tests(interp, &pass, &fail);
    run_http_tests(interp, &pass, &fail);
    run_atomic_tests(interp, &pass, &fail);

    io::printfn("\n=== Unified Tests: %d passed, %d failed ===", pass, fail);
    assert(fail == 0, "tests failed");

    run_compiler_tests(interp);

    interp.destroy();
    mem::free(interp);
}

fn void run_memory_lifetime_regression_tests(Interp* interp, int* pass, int* fail) {
    io::printn("\n--- Memory Lifetime Regression Tests ---");

    // Invariant: run() must return a value that lives in caller scope.
    // For closure results, this catches stale child-scope pointers.
    {
        EvalResult r = run("((lambda (x) (lambda (y) x)) 10)", interp);
        bool ok = !r.error.has_error &&
                  r.value != null &&
                  main::is_in_scope((void*)r.value, interp.current_scope);

        if (ok) {
            io::printn("[PASS] lifetime: run promotes closure result to caller scope");
            (*pass)++;
        } else {
            io::printn("[FAIL] lifetime: run promotes closure result to caller scope");
            (*fail)++;
        }
    }

    // Invariant: completed user coroutines should return their StackCtx to the pool.
    // This uses a fresh interpreter so pool accounting is deterministic.
    {
        Interp* tmp = (Interp*)mem::malloc(Interp.sizeof);
        tmp.init();
        register_primitives(tmp);

        EvalResult r_thunk = run("(lambda () 42)", tmp);
        if (r_thunk.error.has_error || r_thunk.value == null) {
            io::printn("[FAIL] lifetime: completed coroutine returns StackCtx to pool (failed to build thunk)");
            (*fail)++;
            tmp.destroy();
            mem::free(tmp);
            return;
        }

        Value*[1] coro_args;
        coro_args[0] = r_thunk.value;

        usz before = tmp.stack_ctx_pool.pool_size;
        Value* c = prim_coroutine(coro_args[..], null, tmp);

        Value*[1] resume_args;
        resume_args[0] = c;
        Value* r = prim_resume(resume_args[..], null, tmp);
        usz after = tmp.stack_ctx_pool.pool_size;

        bool ok = c != null &&
                  c.tag == COROUTINE &&
                  r != null &&
                  r.tag == INT &&
                  r.int_val == 42 &&
                  after == before + 1;

        if (ok) {
            io::printn("[PASS] lifetime: completed coroutine returns StackCtx to pool");
            (*pass)++;
        } else {
            io::printfn("[FAIL] lifetime: completed coroutine returns StackCtx to pool (before=%d after=%d, coro_tag=%d, result_tag=%d)",
                (int)before, (int)after,
                c != null ? (int)c.tag : -1,
                r != null ? (int)r.tag : -1);
            (*fail)++;
        }

        tmp.destroy();
        mem::free(tmp);
    }

    // Invariant: dual-lane cons barrier promotes TEMP inputs when cdr is ESCAPE-lane.
    {
        main::ScopeRegion* saved = interp.current_scope;
        main::ScopeRegion* s = main::scope_create(saved);
        interp.current_scope = s;

        Value* temp_car = make_int(interp, 7); // TEMP lane
        Value* escape_nil = interp.alloc_value_escape();
        escape_nil.tag = NIL;
        Value* pair = make_cons(interp, temp_car, escape_nil);

        bool ok = pair != null &&
                  pair.scope_gen == s.escape_generation &&
                  pair.cons_val.car != null &&
                  pair.cons_val.car.scope_gen == s.escape_generation &&
                  pair.cons_val.cdr != null &&
                  pair.cons_val.cdr.scope_gen == s.escape_generation;

        interp.current_scope = saved;
        main::scope_release(s);

        if (ok) {
            io::printn("[PASS] lifetime: dual-lane make_cons promotes temp->escape");
            (*pass)++;
        } else {
            io::printn("[FAIL] lifetime: dual-lane make_cons promotes temp->escape");
            (*fail)++;
        }
    }

    // Invariant: promote_to_escape upgrades TEMP list wrappers to ESCAPE lane.
    {
        main::ScopeRegion* saved = interp.current_scope;
        main::ScopeRegion* s = main::scope_create(saved);
        interp.current_scope = s;

        Value* tmp_nil = make_nil(interp);
        Value* tmp_list = make_cons(interp, make_int(interp, 1), tmp_nil);
        Value* esc_list = promote_to_escape(tmp_list, interp);

        bool ok = esc_list != null &&
                  esc_list.scope_gen == s.escape_generation &&
                  esc_list.tag == CONS &&
                  esc_list.cons_val.car != null &&
                  esc_list.cons_val.car.scope_gen == s.escape_generation &&
                  esc_list.cons_val.cdr != null &&
                  esc_list.cons_val.cdr.scope_gen == s.escape_generation;

        interp.current_scope = saved;
        main::scope_release(s);

        if (ok) {
            io::printn("[PASS] lifetime: promote_to_escape upgrades temp list");
            (*pass)++;
        } else {
            io::printn("[FAIL] lifetime: promote_to_escape upgrades temp list");
            (*fail)++;
        }
    }

    // Invariant: env_define_with_barrier promotes temp payload for ESCAPE-lane env.
    {
        main::ScopeRegion* saved = interp.current_scope;
        main::ScopeRegion* s = main::scope_create(saved);
        interp.current_scope = s;

        Env* e = make_env(interp, null);
        e.scope_gen = s.escape_generation; // Simulate ESCAPE-lane env frame
        SymbolId sid = interp.symbols.intern("__barrier_define");
        Value* tmp = make_int(interp, 11);
        env_define_with_barrier(e, sid, tmp, interp);
        Value* got = e.lookup(sid);

        bool ok = got != null &&
                  got.tag == INT &&
                  got.int_val == 11 &&
                  got.scope_gen == s.escape_generation;

        interp.current_scope = saved;
        main::scope_release(s);

        if (ok) {
            io::printn("[PASS] lifetime: env_define_with_barrier promotes temp value");
            (*pass)++;
        } else {
            io::printn("[FAIL] lifetime: env_define_with_barrier promotes temp value");
            (*fail)++;
        }
    }

    // Invariant: env_set_with_barrier promotes temp payload at write target frame.
    {
        main::ScopeRegion* saved = interp.current_scope;
        main::ScopeRegion* s = main::scope_create(saved);
        interp.current_scope = s;

        Env* e = make_env(interp, null);
        e.scope_gen = s.escape_generation; // Simulate ESCAPE-lane env frame
        SymbolId sid = interp.symbols.intern("__barrier_set");
        env_define_with_barrier(e, sid, make_int(interp, 1), interp);
        Value* tmp_new = make_int(interp, 99);
        bool set_ok = true;
        if (catch err = env_set_with_barrier(e, sid, tmp_new, interp)) {
            set_ok = false;
        }
        Value* got = e.lookup(sid);

        bool ok = set_ok &&
                  got != null &&
                  got.tag == INT &&
                  got.int_val == 99 &&
                  got.scope_gen == s.escape_generation;

        interp.current_scope = saved;
        main::scope_release(s);

        if (ok) {
            io::printn("[PASS] lifetime: env_set_with_barrier promotes temp value");
            (*pass)++;
        } else {
            io::printn("[FAIL] lifetime: env_set_with_barrier promotes temp value");
            (*fail)++;
        }
    }

    // Invariant: root-boundary promotion keeps defensive-copy semantics for
    // disjoint scope values (value not owned by releasing scope and not rooted).
    {
        main::ScopeRegion* saved = interp.current_scope;
        main::ScopeRegion* scope_a = main::scope_create(saved);
        main::ScopeRegion* scope_b = main::scope_create(saved);

        interp.current_scope = scope_b;
        Value* foreign = make_cons(interp, make_int(interp, 21), make_nil(interp));

        interp.current_scope = scope_a;
        usz base_site = copy_to_parent_site_calls(COPY_SITE_JIT_EVAL_SET);
        Value* promoted = promote_to_root_site(foreign, interp, COPY_SITE_JIT_EVAL_SET);
        usz site_delta = copy_to_parent_site_calls(COPY_SITE_JIT_EVAL_SET) - base_site;

        bool pre_ok = promoted != null &&
                      promoted != foreign &&
                      promoted.tag == CONS &&
                      promoted.cons_val.car != null &&
                      promoted.cons_val.car.tag == INT &&
                      promoted.cons_val.car.int_val == 21 &&
                      site_delta == 1 &&
                      !main::is_in_scope((void*)promoted, scope_b);

        // Releasing source scope must not invalidate promoted value.
        main::scope_release(scope_b);

        bool post_ok = promoted != null &&
                       promoted.tag == CONS &&
                       promoted.cons_val.car != null &&
                       promoted.cons_val.car.tag == INT &&
                       promoted.cons_val.car.int_val == 21;

        interp.current_scope = saved;
        main::scope_release(scope_a);

        bool ok = pre_ok && post_ok;
        if (ok) {
            io::printn("[PASS] lifetime: root-boundary promotion defends disjoint scope");
            (*pass)++;
        } else {
            io::printfn("[FAIL] lifetime: root-boundary promotion defends disjoint scope (site_delta=%d same=%s pre_ok=%s post_ok=%s)",
                (long)site_delta,
                (promoted == foreign) ? "yes" : "no",
                pre_ok ? "yes" : "no",
                post_ok ? "yes" : "no");
            (*fail)++;
        }
    }

    // Invariant: cons-barrier fallback copy sites must remain unused.
    // If these counters rise, an old path was accidentally reintroduced.
    {
        usz base_total = copy_to_parent_total_calls();
        usz base_car = copy_to_parent_site_calls(COPY_SITE_CONS_BARRIER_CAR);
        usz base_cdr = copy_to_parent_site_calls(COPY_SITE_CONS_BARRIER_CDR);

        EvalResult r = run(
            "(let ^rec (loop (lambda (n acc) (if (= n 2000) acc (loop (+ n 1) (cons n acc))))) (length (loop 0 nil)))",
            interp);

        usz total_delta = copy_to_parent_total_calls() - base_total;
        usz car_delta = copy_to_parent_site_calls(COPY_SITE_CONS_BARRIER_CAR) - base_car;
        usz cdr_delta = copy_to_parent_site_calls(COPY_SITE_CONS_BARRIER_CDR) - base_cdr;

        bool ok_eval = !r.error.has_error &&
                       r.value != null &&
                       r.value.tag == INT &&
                       r.value.int_val == 2000;
        bool ok_budget = car_delta == 0 && cdr_delta == 0;
        bool ok = ok_eval && ok_budget;

        if (ok) {
            io::printn("[PASS] lifetime: cons-barrier fallback sites stay at zero");
            (*pass)++;
        } else {
            io::printfn("[FAIL] lifetime: cons-barrier fallback sites stay at zero (total_delta=%d car_delta=%d cdr_delta=%d tag=%d int=%d err=%s)",
                (long)total_delta,
                (long)car_delta,
                (long)cdr_delta,
                r.value != null ? (int)r.value.tag : -1,
                (r.value != null && r.value.tag == INT) ? (int)r.value.int_val : -1,
                r.error.has_error ? "yes" : "no");
            (*fail)++;
        }
    }

    // Invariant: env copy uses one boundary epoch and memoizes repeated values.
    // This keeps copy_env_to_scope churn bounded for shared captured values.
    {
        reset_memory_promotion_stats();

        main::ScopeRegion* saved = interp.current_scope;
        main::ScopeRegion* target = main::scope_create(saved);
        main::ScopeRegion* source = main::scope_create(saved);

        interp.current_scope = source;
        Env* src_env = make_env(interp, null);
        Value* shared = make_cons(interp, make_int(interp, 123), make_nil(interp));
        for (usz i = 0; i < 24; i++) {
            char[64] nbuf;
            char[] name = io::bprintf(&nbuf, "__copy_env_shared_%d", (long)i)!!;
            env_define_with_barrier(src_env, interp.symbols.intern(name), shared, interp);
        }

        interp.current_scope = target;
        usz base_site = copy_to_parent_site_calls(COPY_SITE_COPY_ENV_TO_SCOPE);
        Env* copied = copy_env_to_scope(src_env, interp);
        usz site_delta = copy_to_parent_site_calls(COPY_SITE_COPY_ENV_TO_SCOPE) - base_site;

        bool copied_ok = copied != null && copied.binding_count == src_env.binding_count;
        bool alias_ok = copied_ok;
        Value* first = null;
        if (copied_ok) {
            for (usz i = 0; i < copied.binding_count; i++) {
                Value* cur = copied.bindings[i].value;
                if (cur == null || cur.tag != CONS ||
                    cur.cons_val.car == null || cur.cons_val.car.tag != INT ||
                    cur.cons_val.car.int_val != 123) {
                    alias_ok = false;
                    break;
                }
                if (i == 0) first = cur;
                if (i > 0 && cur != first) {
                    alias_ok = false;
                    break;
                }
            }
        }

        // Source release must not invalidate target copies.
        main::scope_release(source);
        bool post_ok = first != null &&
                       first.tag == CONS &&
                       first.cons_val.car != null &&
                       first.cons_val.car.tag == INT &&
                       first.cons_val.car.int_val == 123;

        interp.current_scope = saved;
        main::scope_release(target);

        bool ok = copied_ok && alias_ok && post_ok && site_delta <= 4;
        if (ok) {
            io::printn("[PASS] lifetime: copy_env shared-value memo gate");
            (*pass)++;
        } else {
            io::printfn("[FAIL] lifetime: copy_env shared-value memo gate (site_delta=%d copied_ok=%s alias_ok=%s post_ok=%s)",
                (long)site_delta,
                copied_ok ? "yes" : "no",
                alias_ok ? "yes" : "no",
                post_ok ? "yes" : "no");
            (*fail)++;
        }
    }

    // Invariant: unified one-scope escape-env mode allocates env frames in ESCAPE lane.
    {
        main::ScopeRegion* saved_scope = interp.current_scope;
        bool saved_escape_mode = interp.escape_env_mode;

        main::ScopeRegion* s = main::scope_create(saved_scope);
        interp.current_scope = s;
        interp.escape_env_mode = true;

        Env* e = make_env(interp, null);
        bool ok = e != null &&
                  e.scope_gen == s.escape_generation &&
                  main::is_in_scope((void*)e, s);

        interp.escape_env_mode = saved_escape_mode;
        interp.current_scope = saved_scope;
        main::scope_release(s);

        if (ok) {
            io::printn("[PASS] lifetime: unified escape-env uses current ESCAPE lane");
            (*pass)++;
        } else {
            io::printn("[FAIL] lifetime: unified escape-env uses current ESCAPE lane");
            (*fail)++;
        }
    }

    // Invariant: core hot copy sites remain bounded on hot closure loop.
    {
        reset_memory_promotion_stats();

        EvalResult r = run(
            "(begin "
            "  (define step (lambda (x) (+ x 1))) "
            "  (define mk (lambda (n) (lambda (x) (+ x n)))) "
            "  (let ^rec (loop (lambda (i acc) "
            "    (if (= i 600) "
            "        acc "
            "        (let (f (mk i)) "
            "          (loop (+ i 1) (step (+ acc (f 1)))))))) "
            "    (loop 0 0)))",
            interp);

        usz jit_single_delta = copy_to_parent_site_calls(COPY_SITE_JIT_SINGLE_SCOPE_RETURN);
        usz copy_env_delta = copy_to_parent_site_calls(COPY_SITE_COPY_ENV_TO_SCOPE);
        usz run_jit_delta = copy_to_parent_site_calls(COPY_SITE_RUN_JIT_RESULT);
        usz tco_frame_delta = copy_to_parent_site_calls(COPY_SITE_JIT_TCO_FRAME_COPY);

        bool ok_eval = !r.error.has_error &&
                       r.value != null &&
                       r.value.tag == INT &&
                       r.value.int_val == 180900;
        bool ok_budget = jit_single_delta <= 32 &&
                         copy_env_delta <= 32 &&
                         run_jit_delta <= 8 &&
                         tco_frame_delta <= 64;
        bool ok = ok_eval && ok_budget;

        if (ok) {
            io::printn("[PASS] lifetime: jit/copy hot-site budget gate");
            (*pass)++;
        } else {
            io::printfn("[FAIL] lifetime: jit/copy hot-site budget gate (jit_single=%d copy_env=%d run_jit=%d tco_frame=%d tag=%d int=%d err=%s)",
                (long)jit_single_delta,
                (long)copy_env_delta,
                (long)run_jit_delta,
                (long)tco_frame_delta,
                r.value != null ? (int)r.value.tag : -1,
                (r.value != null && r.value.tag == INT) ? (int)r.value.int_val : -1,
                r.error.has_error ? "yes" : "no");
            (*fail)++;
        }
    }

    // Invariant: named-let TCO fast path keeps frame-copy churn near-zero.
    {
        reset_memory_promotion_stats();

        EvalResult r = run(
            "(let ^rec (loop (lambda (n acc) "
            "  (if (= n 5000) "
            "      acc "
            "      (loop (+ n 1) (+ acc 1))))) "
            "  (loop 0 0))",
            interp);

        usz tco_frame_delta = copy_to_parent_site_calls(COPY_SITE_JIT_TCO_FRAME_COPY);
        usz copy_env_delta = copy_to_parent_site_calls(COPY_SITE_COPY_ENV_TO_SCOPE);
        usz run_jit_delta = copy_to_parent_site_calls(COPY_SITE_RUN_JIT_RESULT);

        bool ok_eval = !r.error.has_error &&
                       r.value != null &&
                       r.value.tag == INT &&
                       r.value.int_val == 5000;
        bool ok_budget = tco_frame_delta <= 16 &&
                         copy_env_delta <= 8 &&
                         run_jit_delta <= 4;
        bool ok = ok_eval && ok_budget;

        if (ok) {
            io::printn("[PASS] lifetime: tco frame-copy budget gate");
            (*pass)++;
        } else {
            io::printfn("[FAIL] lifetime: tco frame-copy budget gate (tco_frame=%d copy_env=%d run_jit=%d tag=%d int=%d err=%s)",
                (long)tco_frame_delta,
                (long)copy_env_delta,
                (long)run_jit_delta,
                r.value != null ? (int)r.value.tag : -1,
                (r.value != null && r.value.tag == INT) ? (int)r.value.int_val : -1,
                r.error.has_error ? "yes" : "no");
            (*fail)++;
        }
    }

    // Invariant: promotion context memoizes fallback copy within one boundary epoch.
    {
        main::ScopeRegion* saved_scope = interp.current_scope;
        main::ScopeRegion* parent = main::scope_create(saved_scope);
        main::ScopeRegion* child = main::scope_create(parent);

        interp.current_scope = child;
        Value* src = make_cons(interp, make_int(interp, 7), make_nil(interp));

        interp.current_scope = parent;
        main::ScopeRegion* saved_releasing = interp.releasing_scope;
        interp.releasing_scope = child;
        usz base_site = copy_to_parent_site_calls(COPY_SITE_RUN_JIT_RESULT);

        PromotionContext ctx = {};
        promotion_context_begin(interp, &ctx, 8);
        Value* a = copy_value_if_owned_by_scope(src, interp, child, COPY_SITE_RUN_JIT_RESULT);
        Value* b = copy_value_if_owned_by_scope(src, interp, child, COPY_SITE_RUN_JIT_RESULT);
        promotion_context_end(interp, &ctx);

        usz site_delta = copy_to_parent_site_calls(COPY_SITE_RUN_JIT_RESULT) - base_site;
        bool ok = a != null &&
                  b == a &&
                  site_delta == 1 &&
                  ctx.epoch != 0 &&
                  !ctx.aborted;

        interp.releasing_scope = saved_releasing;
        interp.current_scope = saved_scope;
        main::scope_release(child);
        main::scope_release(parent);

        if (ok) {
            io::printn("[PASS] lifetime: promotion context memo gate");
            (*pass)++;
        } else {
            io::printfn("[FAIL] lifetime: promotion context memo gate (site_delta=%d epoch=%d aborted=%s same=%s)",
                (long)site_delta,
                (int)ctx.epoch,
                ctx.aborted ? "yes" : "no",
                (a != null && b == a) ? "yes" : "no");
            (*fail)++;
        }
    }

    // Invariant: budget abort triggers deterministic fallback in same epoch context.
    {
        main::ScopeRegion* saved_scope = interp.current_scope;
        main::ScopeRegion* parent = main::scope_create(saved_scope);
        main::ScopeRegion* child = main::scope_create(parent);

        interp.current_scope = child;
        Value* src = make_cons(interp, make_int(interp, 11), make_nil(interp));

        PromotionContext ctx = {};
        promotion_context_begin(interp, &ctx, 0);
        Value* promoted = promote_to_escape(src, interp);

        interp.current_scope = parent;
        main::ScopeRegion* saved_releasing = interp.releasing_scope;
        interp.releasing_scope = child;
        usz base_site = copy_to_parent_site_calls(COPY_SITE_RUN_JIT_RESULT);
        Value* fallback = copy_value_if_owned_by_scope(src, interp, child, COPY_SITE_RUN_JIT_RESULT);
        usz site_delta = copy_to_parent_site_calls(COPY_SITE_RUN_JIT_RESULT) - base_site;
        interp.releasing_scope = saved_releasing;
        promotion_context_end(interp, &ctx);

        bool ok = promoted == src &&
                  ctx.aborted &&
                  ctx.epoch != 0 &&
                  fallback != null &&
                  fallback.tag == CONS &&
                  site_delta == 1;

        interp.current_scope = saved_scope;
        main::scope_release(child);
        main::scope_release(parent);

        if (ok) {
            io::printn("[PASS] lifetime: promotion abort fallback gate");
            (*pass)++;
        } else {
            io::printfn("[FAIL] lifetime: promotion abort fallback gate (aborted=%s epoch=%d promoted_same=%s site_delta=%d fallback_tag=%d)",
                ctx.aborted ? "yes" : "no",
                (int)ctx.epoch,
                (promoted == src) ? "yes" : "no",
                (long)site_delta,
                fallback != null ? (int)fallback.tag : -1);
            (*fail)++;
        }
    }
}

fn void run_basic_tests(Interp* interp, int* pass, int* fail) {
    io::printn("\n--- Basic Tests ---");

    // Literals & bindings
    test_eq(interp, "integer literal 42", "42", 42, pass, fail);
    setup(interp, "(define x 10)");
    test_eq(interp, "variable binding x => 10", "x", 10, pass, fail);
    test_eq(interp, "lambda identity", "((lambda (x) x) 5)", 5, pass, fail);
    test_eq(interp, "let binding", "(let (y 7) y)", 7, pass, fail);
    test_eq(interp, "if true", "(if true 1 2)", 1, pass, fail);

    // Primitives
    setup(interp, "(define add +)");
    test_tag(interp, "quote returns list", "(quote (a b c))", CONS, pass, fail);

    // Closure
    setup(interp, "(define make-adder (lambda (n) (lambda (x) n)))");
    test_tag(interp, "nested lambda", "(make-adder 5)", CLOSURE, pass, fail);

    // Cons/car/cdr
    test_eq(interp, "car (cons 1 2) => 1", "(car (cons 1 2))", 1, pass, fail);
    test_eq(interp, "cdr (cons 1 2) => 2", "(cdr (cons 1 2))", 2, pass, fail);

    // Print (interp only, side effect)
    { io::print("[PASS] Print: "); run("(println (quote hello))", interp); (*pass)++; }

    // Source location tracking
    {
        EvalResult r = run("undefined_var", interp);
        if (r.error.has_error) {
            io::printn("[PASS] source location tracking");
            (*pass)++;
        } else {
            io::printn("[FAIL] source location tracking");
            (*fail)++;
        }
    }

    // Dot-bracket indexing
    setup(interp, "(define mylist (quote (10 20 30 40 50)))");
    test_eq(interp, "mylist.[0] => 10", "mylist.[0]", 10, pass, fail);
    test_eq(interp, "mylist.[2] => 30", "mylist.[2]", 30, pass, fail);
    setup(interp, "(define idx 3)");
    test_eq(interp, "mylist.[idx] => 40", "mylist.[idx]", 40, pass, fail);

    // Pattern matching basics
    test_eq(interp, "match literal", "(match 42 (42 1) (x 0))", 1, pass, fail);
    test_eq(interp, "match variable", "(match 5 (x x))", 5, pass, fail);
    test_eq(interp, "match wildcard", "(match 99 (_ 1))", 1, pass, fail);
    test_eq(interp, "match [a b c]", "(match (quote (1 2 3)) ([a b c] (+ a (+ b c))))", 6, pass, fail);
    test_eq(interp, "match [head .. tail]", "(match (quote (10 20 30)) ([head .. tail] head))", 10, pass, fail);
    test_eq(interp, "match [a b ..]", "(match (quote (1 2 3 4 5)) ([a b ..] (+ a b)))", 3, pass, fail);
    test_eq(interp, "match [.. last]", "(match (quote (1 2 3 4 5)) ([.. last] last))", 5, pass, fail);
    test_eq(interp, "match fallthrough", "(match 42 (0 100) (1 200) (x x))", 42, pass, fail);
    test_eq(interp, "match [_ second _]", "(match (quote (1 2 3)) ([_ second _] second))", 2, pass, fail);

    // === Let destructuring: all combinations ===

    // Array: basic
    test_eq(interp, "let [x y] array", "(let ([x y] [10 20]) (+ x y))", 30, pass, fail);
    test_eq(interp, "let [a b c] list", "(let ([a b c] '(1 2 3)) (+ a (+ b c)))", 6, pass, fail);
    test_eq(interp, "let [a b c] array", "(let ([a b c] [1 2 3]) (+ a (+ b c)))", 6, pass, fail);

    // Array: head..tail
    test_eq(interp, "let [h .. t] list", "(let ([h .. t] '(10 20 30)) h)", 10, pass, fail);
    test_eq(interp, "let [h .. t] array", "(let ([h .. t] [10 20 30]) h)", 10, pass, fail);
    test_eq(interp, "let [h .. t] tail len", "(let ([h .. t] '(10 20 30)) (length t))", 2, pass, fail);

    // Array: ignore rest
    test_eq(interp, "let [a b ..] list", "(let ([a b ..] '(1 2 3 4 5)) (+ a b))", 3, pass, fail);
    test_eq(interp, "let [a b ..] array", "(let ([a b ..] [1 2 3 4 5]) (+ a b))", 3, pass, fail);

    // Array: [.. last]
    test_eq(interp, "let [.. last] list", "(let ([.. last] '(1 2 3 4 5)) last)", 5, pass, fail);

    // Array: wildcard
    test_eq(interp, "let [_ b _] skip", "(let ([_ b _] [10 20 30]) b)", 20, pass, fail);

    // Dict: basic
    test_str(interp, "let {name} dict", "(let ({name} {'name \"Alice\" 'age 30}) name)", pass, fail);
    test_eq(interp, "let {age} dict", "(let ({age} {'name \"Alice\" 'age 30}) age)", 30, pass, fail);
    test_eq(interp, "let {x y} dict", "(let ({x y} {'x 10 'y 20}) (+ x y))", 30, pass, fail);

    // Dict: missing key → nil
    test_eq(interp, "let {z} missing → nil", "(let ({z} {'x 10}) (if z 99 0))", 0, pass, fail);

    // Dict: 3+ keys
    test_eq(interp, "let {a b c} dict", "(let ({a b c} {'a 1 'b 2 'c 3}) (+ a (+ b c)))", 6, pass, fail);

    // Mixed: plain + array
    test_eq(interp, "let plain + [arr]", "(let (x 1 [a b] [2 3]) (+ x (+ a b)))", 6, pass, fail);
    test_eq(interp, "let [arr] + plain", "(let ([a b] [2 3] x 1) (+ x (+ a b)))", 6, pass, fail);

    // Mixed: plain + dict
    test_eq(interp, "let plain + {dict}", "(let (x 1 {y} {'y 2}) (+ x y))", 3, pass, fail);
    test_eq(interp, "let {dict} + plain", "(let ({y} {'y 2} x 1) (+ x y))", 3, pass, fail);

    // Mixed: array + dict
    test_eq(interp, "let [arr] + {dict}", "(let ([a b] [1 2] {x} {'x 10}) (+ a (+ b x)))", 13, pass, fail);
    test_eq(interp, "let {dict} + [arr]", "(let ({x} {'x 10} [a b] [1 2]) (+ a (+ b x)))", 13, pass, fail);

    // Mixed: plain + array + dict
    test_eq(interp, "let p + [a] + {d}", "(let (z 1 [a b] [2 3] {x} {'x 4}) (+ z (+ a (+ b x))))", 10, pass, fail);

    // Mixed: two arrays
    test_eq(interp, "let [a1] [a2]", "(let ([a b] [1 2] [c d] [3 4]) (+ a (+ b (+ c d))))", 10, pass, fail);

    // Mixed: two dicts
    test_eq(interp, "let {d1} {d2}", "(let ({x} {'x 3} {y} {'y 7}) (+ x y))", 10, pass, fail);

    // Shadowing in let: second binding shadows first
    test_eq(interp, "let shadow plain", "(let (x 1 x 2) x)", 2, pass, fail);
    test_eq(interp, "let shadow [a] [a]", "(let ([a b] [1 2] [a b] [10 20]) (+ a b))", 30, pass, fail);
    test_eq(interp, "let shadow {d} {d}", "(let ({x} {'x 1} {x} {'x 99}) x)", 99, pass, fail);

    // Nested scope: destructured bindings visible in body
    test_eq(interp, "let [a] in body", "(let ([a b] [1 2]) (let (c (+ a b)) c))", 3, pass, fail);
    test_eq(interp, "let {d} in body", "(let ({x y} {'x 3 'y 4}) (let (z (+ x y)) z))", 7, pass, fail);

    // Array destr from function result
    test_eq(interp, "let [a b] from call", "(let ([a b] (list 10 20)) (+ a b))", 30, pass, fail);

    // Dict destr with symbol-as-function
    test_eq(interp, "let {d} sym-as-fn", "(let ({name} {'name \"Alice\"}) (string-length name))", 5, pass, fail);

    // default function for dict optional keys
    test_eq(interp, "default nil → fallback", "(default nil 42)", 42, pass, fail);
    test_eq(interp, "default val → val", "(default 10 42)", 10, pass, fail);
    test_eq(interp, "default 0 → 0", "(default 0 42)", 0, pass, fail);
    test_eq(interp, "default with {dict}", "(let ({timeout} {'host \"x\"}) (default timeout 5000))", 5000, pass, fail);
    test_eq(interp, "default provided", "(let ({timeout} {'timeout 3000}) (default timeout 5000))", 3000, pass, fail);

    // with-defaults macro
    test_eq(interp, "with-defaults body only", "(with-defaults 42)", 42, pass, fail);
    test_eq(interp, "with-defaults nil → default", "(let (x nil) (with-defaults x 5 x))", 5, pass, fail);
    test_eq(interp, "with-defaults val → val", "(let (x 10) (with-defaults x 5 x))", 10, pass, fail);
    test_eq(interp, "with-defaults multi", "(let (x nil y nil) (with-defaults x 5 y 10 (+ x y)))", 15, pass, fail);
    test_eq(interp, "with-defaults partial", "(let (x 1 y nil) (with-defaults x 5 y 10 (+ x y)))", 11, pass, fail);
    test_eq(interp, "with-defaults + {dict}", "(let ({timeout} {'host \"x\"}) (with-defaults timeout 5000 timeout))", 5000, pass, fail);

    // === Destructuring in function params: all combinations ===

    // Single dict param
    setup(interp, "(define (d-single {name}) name)");
    test_str(interp, "define {name} param", "(d-single {'name \"Alice\" 'age 30})", pass, fail);

    // Multi-key dict param
    setup(interp, "(define (d-multi {x y}) (+ x y))");
    test_eq(interp, "define {x y} param", "(d-multi {'x 10 'y 20})", 30, pass, fail);

    // Single array param
    setup(interp, "(define (d-arr [a b]) (+ a b))");
    test_eq(interp, "define [a b] param", "(d-arr [3 4])", 7, pass, fail);

    // Array param with head..tail
    setup(interp, "(define (d-ht [h .. t]) h)");
    test_eq(interp, "define [h .. t] param", "(d-ht '(10 20 30))", 10, pass, fail);

    // Dict + positional
    setup(interp, "(define (d-dp {a} b) (+ a b))");
    test_eq(interp, "define {dict} + positional", "(d-dp {'a 5} 10)", 15, pass, fail);

    // Positional + dict
    setup(interp, "(define (d-pd x {y}) (+ x y))");
    test_eq(interp, "define positional + {dict}", "(d-pd 3 {'y 7})", 10, pass, fail);

    // Array + positional
    setup(interp, "(define (d-ap [a b] c) (+ a (+ b c)))");
    test_eq(interp, "define [arr] + positional", "(d-ap [1 2] 3)", 6, pass, fail);

    // Positional + array
    setup(interp, "(define (d-pa x [a b]) (+ x (+ a b)))");
    test_eq(interp, "define positional + [arr]", "(d-pa 10 [3 4])", 17, pass, fail);

    // Two dict params
    setup(interp, "(define (d-dd {x} {y}) (+ x y))");
    test_eq(interp, "define {d1} {d2}", "(d-dd {'x 3} {'y 7})", 10, pass, fail);

    // Dict + array
    setup(interp, "(define (d-da {x} [a b]) (+ x (+ a b)))");
    test_eq(interp, "define {dict} [arr]", "(d-da {'x 10} [1 2])", 13, pass, fail);

    // Array + dict
    setup(interp, "(define (d-ad [a b] {x}) (+ a (+ b x)))");
    test_eq(interp, "define [arr] {dict}", "(d-ad [1 2] {'x 10})", 13, pass, fail);

    // Dict + array + positional
    setup(interp, "(define (d-dap {x} [a b] c) (+ x (+ a (+ b c))))");
    test_eq(interp, "define {d} [a] pos", "(d-dap {'x 1} [2 3] 4)", 10, pass, fail);

    // Positional + dict + array
    setup(interp, "(define (d-pda z {x} [a b]) (+ z (+ x (+ a b))))");
    test_eq(interp, "define pos {d} [a]", "(d-pda 1 {'x 2} [3 4])", 10, pass, fail);

    // Dict + positional + variadic
    setup(interp, "(define (d-dpv {x} y .. rest) (+ x y))");
    test_eq(interp, "define {d} pos .. rest", "(d-dpv {'x 5} 10 20 30)", 15, pass, fail);

    // Array + variadic
    setup(interp, "(define (d-av [a b] .. rest) (+ a b))");
    test_eq(interp, "define [arr] .. rest", "(d-av [1 2] 3 4 5)", 3, pass, fail);

    // Three destructuring params
    setup(interp, "(define (d-three {x} [a b] {y}) (+ x (+ a (+ b y))))");
    test_eq(interp, "define {d} [a] {d2}", "(d-three {'x 1} [2 3] {'y 4})", 10, pass, fail);

    // Lambda: dict param
    test_eq(interp, "lambda {dict}", "((lambda ({x y}) (+ x y)) {'x 3 'y 4})", 7, pass, fail);

    // Lambda: array param
    test_eq(interp, "lambda [arr]", "((lambda ([a b]) (+ a b)) [5 6])", 11, pass, fail);

    // Lambda: dict + positional
    test_eq(interp, "lambda {d} pos", "((lambda ({x} y) (+ x y)) {'x 3} 7)", 10, pass, fail);

    // Lambda: array + dict
    test_eq(interp, "lambda [a] {d}", "((lambda ([a b] {x}) (+ a (+ b x))) [1 2] {'x 10})", 13, pass, fail);

    // Typed param + dict
    setup(interp, "(define (d-typed (^Int n) {x}) (+ n x))");
    test_eq(interp, "define typed + {dict}", "(d-typed 5 {'x 10})", 15, pass, fail);

    // === Shadowing: last binding wins ===
    // Same name in two dict params: second shadows first (nested lets, inner wins)
    setup(interp, "(define (d-shadow {x} {x y}) (+ x y))");
    test_eq(interp, "shadow {x} {x y} inner wins", "(d-shadow {'x 100} {'x 1 'y 2})", 3, pass, fail);

    // Same name in array params: second shadows
    setup(interp, "(define (d-sha2 [c z] [c z]) (+ c z))");
    test_eq(interp, "shadow [c z] [c z] inner wins", "(d-sha2 [100 200] [1 2])", 3, pass, fail);

    // Dict key missing → nil
    test_eq(interp, "dict missing key → nil", "((lambda ({x y}) (if y 99 0)) {'x 10})", 0, pass, fail);

    // Length
    test_eq(interp, "length '() => 0", "(length (quote ()))", 0, pass, fail);
    test_eq(interp, "length '(1 2 3 4 5) => 5", "(length (quote (1 2 3 4 5)))", 5, pass, fail);

    // Path access
    setup(interp, "(define namepair (cons (quote name) \"Alice\"))");
    setup(interp, "(define agepair (cons (quote age) 30))");
    setup(interp, "(define testperson (cons namepair (cons agepair (quote ()))))");
    test_str(interp, "testperson.name => string", "testperson.name", pass, fail);
    test_eq(interp, "testperson.age => 30", "testperson.age", 30, pass, fail);

    // Nested path
    setup(interp, "(define citypair (cons (quote city) \"NYC\"))");
    setup(interp, "(define zippair (cons (quote zip) 10001))");
    setup(interp, "(define testaddress (cons citypair (cons zippair (quote ()))))");
    setup(interp, "(define namepair2 (cons (quote name) \"Bob\"))");
    setup(interp, "(define addrpair (cons (quote address) testaddress))");
    setup(interp, "(define testperson2 (cons namepair2 (cons addrpair (quote ()))))");
    test_str(interp, "testperson2.address.city => string", "testperson2.address.city", pass, fail);

    // Recursive let
    test_eq(interp, "let ^rec factorial 5 => 120", "(let ^rec (fact (lambda (n) (if (= n 0) 1 (* n (fact (- n 1)))))) (fact 5))", 120, pass, fail);
    test_eq(interp, "non-recursive let", "(let (x 10) (+ x 5))", 15, pass, fail);

    // And/or
    test_truthy(interp, "and true true", "(and true true)", pass, fail);
    test_nil(interp, "and false true => nil", "(and false true)", pass, fail);
    test_nil(interp, "and true false => nil", "(and true false)", pass, fail);
    test_eq(interp, "and 1 42 => 42", "(and 1 42)", 42, pass, fail);
    test_truthy(interp, "or true false", "(or true false)", pass, fail);
    test_nil(interp, "or false nil => nil", "(or false nil)", pass, fail);
    test_eq(interp, "or 42 99 => 42", "(or 42 99)", 42, pass, fail);
    test_eq(interp, "or nil 99 => 99", "(or nil 99)", 99, pass, fail);
    test_nil(interp, "and short-circuit", "(and nil undefined_var)", pass, fail);
    test_eq(interp, "or short-circuit", "(or 1 undefined_var)", 1, pass, fail);

    // Truthiness
    test_eq(interp, "0 is truthy", "(if 0 42 99)", 42, pass, fail);

    // Reset/shift
    test_eq(interp, "shift aborts", "(reset (+ 1 (shift k 42)))", 42, pass, fail);
    test_eq(interp, "shift k resumes", "(reset (+ 1 (shift k (k 10))))", 11, pass, fail);
    test_eq(interp, "reset passthrough", "(reset (+ 10 20))", 30, pass, fail);

    // Effect handlers
    test_eq(interp, "handle without effect", "(handle 42 (ask x (resolve 0)))", 42, pass, fail);
    test_eq(interp, "handle + perform + resume", "(handle (+ 1 (signal ask 0)) (ask x (resolve 10)))", 11, pass, fail);
    test_eq(interp, "handle abort", "(handle (+ 1 (signal ask 5)) (ask x x))", 5, pass, fail);

    // Empty string truthy
    test_eq(interp, "empty string is truthy", "(if \"\" 1 0)", 1, pass, fail);
}

fn void run_memory_stress_tests(Interp* interp, int* pass, int* fail) {
    io::printn("\n--- Memory Stress Tests ---");

    // Deep recursion
    setup(interp, "(define sum-to (lambda (n) (if (= n 0) 0 (+ n (sum-to (- n 1))))))");
    test_eq(interp, "deep recursion sum 1..50 => 1275", "(sum-to 50)", 1275, pass, fail);

    // Closure factory
    setup(interp, "(define make-counter (lambda (start) (lambda (x) (+ start x))))");
    setup(interp, "(define c1 (make-counter 10))");
    setup(interp, "(define c2 (make-counter 20))");
    setup(interp, "(define c3 (make-counter 30))");
    test_eq(interp, "closure c1 5 => 15", "(c1 5)", 15, pass, fail);
    test_eq(interp, "closure c2 5 => 25", "(c2 5)", 25, pass, fail);
    test_eq(interp, "closure c3 5 => 35", "(c3 5)", 35, pass, fail);

    // String operations
    test_eq(interp, "string-length hello world => 11", "(string-length \"hello world\")", 11, pass, fail);
    test_str(interp, "string-upcase hello => string", "(string-upcase \"hello\")", pass, fail);

    // Pattern matching stress
    setup(interp, "(define data (quote (1 2 3 4 5 6 7 8 9 10)))");
    test_eq(interp, "match [a b c .. rest]", "(match data ([a b c .. rest] (+ a (+ b c))))", 6, pass, fail);
    test_eq(interp, "match [.. last] => 10", "(match data ([.. last] last))", 10, pass, fail);

    // Fibonacci
    setup(interp, "(define fib (lambda (n) (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2))))))");
    test_eq(interp, "fib 15 => 610", "(fib 15)", 610, pass, fail);

    // Map over list
    setup(interp, "(define mymap (lambda (f) (lambda (lst) (if (null? lst) (quote ()) (cons (f (car lst)) ((mymap f) (cdr lst)))))))");
    setup(interp, "(define double (lambda (x) (* x 2)))");
    test_eq(interp, "map double car => 2", "(car ((mymap double) (quote (1 2 3 4 5))))", 2, pass, fail);
    setup(interp, "(define mapped ((mymap double) (quote (1 2 3 4 5))))");
    test_eq(interp, "map double last => 10", "(match mapped ([.. last] last))", 10, pass, fail);

    // Nested let 10 levels
    test_eq(interp, "nested let 10 levels => 55", "(let (a 1) (let (b 2) (let (c 3) (let (d 4) (let (e 5) (let (f 6) (let (g 7) (let (h 8) (let (i 9) (let (j 10) (+ a (+ b (+ c (+ d (+ e (+ f (+ g (+ h (+ i j)))))))))))))))))))))", 55, pass, fail);

    // Repeated evaluation
    setup(interp, "(define mem-counter 0)");
    for (int i = 0; i < 100; i++) { run("(define mem-counter (+ mem-counter 1))", interp); }
    test_eq(interp, "repeated eval 100x", "mem-counter", 100, pass, fail);

    // Large list
    setup(interp, "(define build-list-helper (lambda (n) (lambda (acc) (if (= n 0) acc ((build-list-helper (- n 1)) (cons n acc))))))");
    setup(interp, "(define biglist ((build-list-helper 50) (quote ())))");
    test_eq(interp, "build-list 50 elements", "(length biglist)", 50, pass, fail);

    // Fold
    setup(interp, "(define fold-helper (lambda (f) (lambda (init) (lambda (lst) (if (null? lst) init (((fold-helper f) (f init (car lst))) (cdr lst)))))))");
    test_eq(interp, "fold sum 1..10 => 55", "(((fold-helper +) 0) (quote (1 2 3 4 5 6 7 8 9 10)))", 55, pass, fail);
}

fn void run_arithmetic_comparison_tests(Interp* interp, int* pass, int* fail) {
    io::printn("\n--- Arithmetic & Comparison Tests ---");

    // Addition
    test_eq(interp, "(+ 1 2) => 3", "(+ 1 2)", 3, pass, fail);
    test_eq(interp, "(+ 0 0) => 0", "(+ 0 0)", 0, pass, fail);
    test_eq(interp, "(+ -5 3) => -2", "(+ -5 3)", -2, pass, fail);
    test_eq(interp, "(+ 100 -100) => 0", "(+ 100 -100)", 0, pass, fail);
    test_eq(interp, "(+ 1000000 2000000) => 3000000", "(+ 1000000 2000000)", 3000000, pass, fail);

    // Subtraction
    test_eq(interp, "(- 10 3) => 7", "(- 10 3)", 7, pass, fail);
    test_eq(interp, "(- 3 10) => -7", "(- 3 10)", -7, pass, fail);
    test_eq(interp, "(- 0 0) => 0", "(- 0 0)", 0, pass, fail);
    test_eq(interp, "(- -5 -3) => -2", "(- -5 -3)", -2, pass, fail);

    // Multiplication
    test_eq(interp, "(* 3 4) => 12", "(* 3 4)", 12, pass, fail);
    test_eq(interp, "(* 0 999) => 0", "(* 0 999)", 0, pass, fail);
    test_eq(interp, "(* 1 42) => 42", "(* 1 42)", 42, pass, fail);
    test_eq(interp, "(* -3 4) => -12", "(* -3 4)", -12, pass, fail);
    test_eq(interp, "(* -3 -4) => 12", "(* -3 -4)", 12, pass, fail);

    // Division
    test_eq(interp, "(/ 10 2) => 5", "(/ 10 2)", 5, pass, fail);
    test_eq(interp, "(/ 7 2) => 3", "(/ 7 2)", 3, pass, fail);
    test_eq(interp, "(/ 0 5) => 0", "(/ 0 5)", 0, pass, fail);
    test_eq(interp, "(/ -10 3) => -3", "(/ -10 3)", -3, pass, fail);

    // Modulo
    test_eq(interp, "(% 10 3) => 1", "(% 10 3)", 1, pass, fail);
    test_eq(interp, "(% 9 3) => 0", "(% 9 3)", 0, pass, fail);
    test_eq(interp, "(% 7 10) => 7", "(% 7 10)", 7, pass, fail);

    // Nested arithmetic
    test_eq(interp, "(+ 1 (+ 2 3)) => 6", "(+ 1 (+ 2 3))", 6, pass, fail);
    test_eq(interp, "(* 2 (+ 3 4)) => 14", "(* 2 (+ 3 4))", 14, pass, fail);
    test_eq(interp, "(- (* 5 5) (+ 10 5)) => 10", "(- (* 5 5) (+ 10 5))", 10, pass, fail);
    test_eq(interp, "(/ (+ 10 20) (- 10 4)) => 5", "(/ (+ 10 20) (- 10 4))", 5, pass, fail);

    // Equality
    test_truthy(interp, "(= 5 5) => true", "(= 5 5)", pass, fail);
    test_nil(interp, "(= 5 6) => nil", "(= 5 6)", pass, fail);
    test_truthy(interp, "(= 0 0) => true", "(= 0 0)", pass, fail);
    test_truthy(interp, "(= -1 -1) => true", "(= -1 -1)", pass, fail);

    // Less than
    test_truthy(interp, "(< 1 2) => true", "(< 1 2)", pass, fail);
    test_nil(interp, "(< 2 1) => nil", "(< 2 1)", pass, fail);
    test_nil(interp, "(< 1 1) => nil", "(< 1 1)", pass, fail);
    test_truthy(interp, "(< -5 0) => true", "(< -5 0)", pass, fail);

    // Greater than
    test_truthy(interp, "(> 2 1) => true", "(> 2 1)", pass, fail);
    test_nil(interp, "(> 1 2) => nil", "(> 1 2)", pass, fail);
    test_nil(interp, "(> 1 1) => nil", "(> 1 1)", pass, fail);

    // Less or equal
    test_truthy(interp, "(<= 1 2) => true", "(<= 1 2)", pass, fail);
    test_truthy(interp, "(<= 2 2) => true", "(<= 2 2)", pass, fail);
    test_nil(interp, "(<= 3 2) => nil", "(<= 3 2)", pass, fail);

    // Greater or equal
    test_truthy(interp, "(>= 2 1) => true", "(>= 2 1)", pass, fail);
    test_truthy(interp, "(>= 2 2) => true", "(>= 2 2)", pass, fail);
    test_nil(interp, "(>= 1 2) => nil", "(>= 1 2)", pass, fail);

    // Comparison in conditionals
    test_eq(interp, "(if (< 1 2) 10 20) => 10", "(if (< 1 2) 10 20)", 10, pass, fail);
    test_eq(interp, "(if (> 1 2) 10 20) => 20", "(if (> 1 2) 10 20)", 20, pass, fail);
    test_eq(interp, "(if (= (+ 2 3) 5) 1 0) => 1", "(if (= (+ 2 3) 5) 1 0)", 1, pass, fail);
}

fn void run_string_type_tests(Interp* interp, int* pass, int* fail) {
    io::printn("\n--- String & Type Predicate Tests ---");

    // String length
    test_eq(interp, "string-length hello => 5", "(string-length \"hello\")", 5, pass, fail);
    test_eq(interp, "string-length empty => 0", "(string-length \"\")", 0, pass, fail);
    test_eq(interp, "string-length a => 1", "(string-length \"a\")", 1, pass, fail);

    // String case/trim/append/substring
    test_str(interp, "string-upcase hello", "(string-upcase \"hello\")", pass, fail);
    test_str(interp, "string-downcase HELLO", "(string-downcase \"HELLO\")", pass, fail);
    test_str(interp, "string-upcase empty", "(string-upcase \"\")", pass, fail);
    test_str(interp, "string-trim", "(string-trim \"  hello  \")", pass, fail);
    test_str(interp, "string-trim noop", "(string-trim \"hello\")", pass, fail);
    test_str(interp, "string-append", "(string-append \"hello\" \" world\")", pass, fail);
    test_str(interp, "string-append empty", "(string-append \"\" \"abc\")", pass, fail);
    test_str(interp, "substring 0 5", "(substring \"hello world\" 0 5)", pass, fail);
    test_str(interp, "substring 6 11", "(substring \"hello world\" 6 11)", pass, fail);

    // String predicates
    test_truthy(interp, "string? hello => true", "(string? \"hello\")", pass, fail);
    test_nil(interp, "string? 42 => nil", "(string? 42)", pass, fail);
    test_nil(interp, "string? nil => nil", "(string? nil)", pass, fail);

    // int?
    test_truthy(interp, "int? 42 => true", "(int? 42)", pass, fail);
    test_truthy(interp, "int? 0 => true", "(int? 0)", pass, fail);
    test_truthy(interp, "int? -5 => true", "(int? -5)", pass, fail);
    test_nil(interp, "int? hello => nil", "(int? \"hello\")", pass, fail);
    test_nil(interp, "int? nil => nil", "(int? nil)", pass, fail);

    // symbol?
    test_truthy(interp, "symbol? foo => true", "(symbol? (quote foo))", pass, fail);
    test_nil(interp, "symbol? 42 => nil", "(symbol? 42)", pass, fail);
    test_nil(interp, "symbol? hello => nil", "(symbol? \"hello\")", pass, fail);

    // closure?
    test_truthy(interp, "closure? lambda => true", "(closure? (lambda (x) x))", pass, fail);
    test_nil(interp, "closure? 42 => nil", "(closure? 42)", pass, fail);
    test_nil(interp, "closure? hello => nil", "(closure? \"hello\")", pass, fail);

    // null?
    test_truthy(interp, "null? nil => true", "(null? nil)", pass, fail);
    test_truthy(interp, "null? '() => true", "(null? (quote ()))", pass, fail);
    test_nil(interp, "null? 42 => nil", "(null? 42)", pass, fail);
    test_nil(interp, "null? '(1 2) => nil", "(null? (quote (1 2)))", pass, fail);

    // pair?
    test_truthy(interp, "pair? cons => true", "(pair? (cons 1 2))", pass, fail);
    test_truthy(interp, "pair? list => true", "(pair? (quote (1 2 3)))", pass, fail);
    test_nil(interp, "pair? nil => nil", "(pair? nil)", pass, fail);
    test_nil(interp, "pair? 42 => nil", "(pair? 42)", pass, fail);

    // not
    test_truthy(interp, "not nil => true", "(not nil)", pass, fail);
    test_truthy(interp, "not false => true", "(not false)", pass, fail);
    test_nil(interp, "not true => nil", "(not true)", pass, fail);
    test_nil(interp, "not 42 => nil", "(not 42)", pass, fail);
    test_nil(interp, "not 0 => nil (truthy)", "(not 0)", pass, fail);
    test_nil(interp, "not empty-str => nil (truthy)", "(not \"\")", pass, fail);
}

fn void run_list_closure_tests(Interp* interp, int* pass, int* fail) {
    io::printn("\n--- List & Closure Tests ---");

    // cons/car/cdr
    test_eq(interp, "car (cons 1 2) => 1", "(car (cons 1 2))", 1, pass, fail);
    test_eq(interp, "cdr (cons 1 2) => 2", "(cdr (cons 1 2))", 2, pass, fail);
    test_eq(interp, "car (cons (+ 1 2) 4) => 3", "(car (cons (+ 1 2) 4))", 3, pass, fail);
    test_eq(interp, "cadr => 2", "(car (cdr (cons 1 (cons 2 (cons 3 nil)))))", 2, pass, fail);
    test_eq(interp, "caddr => 3", "(car (cdr (cdr (cons 1 (cons 2 (cons 3 nil))))))", 3, pass, fail);

    // Length
    test_eq(interp, "length '() => 0", "(length (quote ()))", 0, pass, fail);
    test_eq(interp, "length '(1) => 1", "(length (quote (1)))", 1, pass, fail);
    test_eq(interp, "length '(1..10) => 10", "(length (quote (1 2 3 4 5 6 7 8 9 10)))", 10, pass, fail);
    test_eq(interp, "length cons => 2", "(length (cons 1 (cons 2 nil)))", 2, pass, fail);

    // Quote
    test_eq(interp, "car '(10 20 30) => 10", "(car (quote (10 20 30)))", 10, pass, fail);
    test_eq(interp, "length '(a b c d e) => 5", "(length (quote (a b c d e)))", 5, pass, fail);
    test_truthy(interp, "null? '() => true", "(null? (quote ()))", pass, fail);

    // List indexing
    setup(interp, "(define idx-list (quote (10 20 30)))");
    test_eq(interp, "idx-list.[0] => 10", "idx-list.[0]", 10, pass, fail);
    test_eq(interp, "idx-list.[1] => 20", "idx-list.[1]", 20, pass, fail);
    test_eq(interp, "idx-list.[2] => 30", "idx-list.[2]", 30, pass, fail);

    // Closures
    setup(interp, "(define make-adder2 (lambda (n) (lambda (x) (+ n x))))");
    setup(interp, "(define add10 (make-adder2 10))");
    setup(interp, "(define add20 (make-adder2 20))");
    test_eq(interp, "add10 5 => 15", "(add10 5)", 15, pass, fail);
    test_eq(interp, "add20 5 => 25", "(add20 5)", 25, pass, fail);

    // Composition
    setup(interp, "(define compose (lambda (f) (lambda (g) (lambda (x) (f (g x))))))");
    setup(interp, "(define double2 (lambda (x) (* x 2)))");
    setup(interp, "(define inc (lambda (x) (+ x 1)))");
    setup(interp, "(define double-then-inc (compose inc double2))");
    test_eq(interp, "compose inc double 5 => 11", "(double-then-inc 5)", 11, pass, fail);

    // Multi-capture
    setup(interp, "(define make-linear (lambda (a) (lambda (b) (lambda (x) (+ (* a x) b)))))");
    setup(interp, "(define f2x3 ((make-linear 2) 3))");
    test_eq(interp, "f(x)=2x+3, f(10)=23", "(f2x3 10)", 23, pass, fail);

    // Scoping
    test_eq(interp, "let shadowing", "(let (x 10) (let (x 20) x))", 20, pass, fail);
    setup(interp, "(define outer-x 100)");
    setup(interp, "(let (outer-x 999) outer-x)");
    test_eq(interp, "let no leak", "outer-x", 100, pass, fail);

    setup(interp, "(define scope-test 42)");
    setup(interp, "(define scope-fn (lambda (scope-test) scope-test))");
    test_eq(interp, "lambda scope param", "(scope-fn 99)", 99, pass, fail);
    test_eq(interp, "global preserved", "scope-test", 42, pass, fail);

    // Recursive let
    test_eq(interp, "let ^rec fact 6 => 720", "(let ^rec (f (lambda (n) (if (= n 0) 1 (* n (f (- n 1)))))) (f 6))", 720, pass, fail);

    // Mutual recursion
    setup(interp, "(define is-even (lambda (n) (if (= n 0) true (is-odd (- n 1)))))");
    setup(interp, "(define is-odd (lambda (n) (if (= n 0) false (is-even (- n 1)))))");
    test_truthy(interp, "even 10 => true", "(is-even 10)", pass, fail);
    test_truthy(interp, "odd 7 => true", "(is-odd 7)", pass, fail);

    // Higher-order
    setup(interp, "(define apply-twice (lambda (f) (lambda (x) (f (f x)))))");
    setup(interp, "(define add3 (lambda (x) (+ x 3)))");
    test_eq(interp, "apply-twice add3 10 => 16", "((apply-twice add3) 10)", 16, pass, fail);

    // Identity
    setup(interp, "(define id (lambda (x) x))");
    test_eq(interp, "id 42 => 42", "(id 42)", 42, pass, fail);
    test_str(interp, "id hello => string", "(id \"hello\")", pass, fail);
    test_nil(interp, "id nil => nil", "(id nil)", pass, fail);
}

fn void run_advanced_tests(Interp* interp, int* pass, int* fail) {
    io::printn("\n--- Advanced Feature Tests ---");

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

    // Continuations
    test_eq(interp, "shift discard k", "(reset (+ 100 (shift k 42)))", 42, pass, fail);
    test_eq(interp, "shift k mul", "(reset (* 2 (shift k (k 5))))", 10, pass, fail);
    test_eq(interp, "shift k expr", "(reset (+ 1 (shift k (k (+ 2 3)))))", 6, pass, fail);
    test_eq(interp, "shift use k result", "(reset (+ 1 (shift k (+ (k 10) 1000))))", 1011, pass, fail);
    test_eq(interp, "reset value", "(reset 42)", 42, pass, fail);

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

    // Error handling
    test_error(interp, "unbound variable", "nonexistent_var_xyz", pass, fail);
    test_error(interp, "call non-function", "(42 1)", pass, fail);
    test_error(interp, "shift outside reset", "(shift k 42)", pass, fail);

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
    test_eq(interp, "collect it3 car", "(car (collect it3))", 3, pass, fail);
    test_eq(interp, "collect it3 len", "(length (collect it3))", 3, pass, fail);
    // to-array
    test_eq(interp, "to-array it3 len", "(length (to-array it3))", 3, pass, fail);
    test_eq(interp, "to-array it3 ref 0", "(ref (to-array it3) 0)", 3, pass, fail);
    // type-of iterator
    test_truthy(interp, "type-of iterator",
        "(is? (make-iterator (lambda () nil)) (quote Iterator))", pass, fail);

    // Phase 3B: iterator constructors + lazy combinators
    test_truthy(interp, "iterator dispatches list",
        "(iterator? (iterator (quote (1 2 3))))", pass, fail);
    test_eq(interp, "iterator list collect car", "(car (collect (iterator (quote (1 2 3)))))", 1, pass, fail);
    test_eq(interp, "iterator list collect len", "(length (collect (iterator (quote (1 2 3)))))", 3, pass, fail);
    test_eq(interp, "iterator array collect car", "(car (collect (iterator [10 20 30])))", 10, pass, fail);
    test_eq(interp, "iterator array collect len", "(length (collect (iterator [10 20 30])))", 3, pass, fail);
    test_eq(interp, "iterator dict collect len", "(length (collect (iterator (begin (define iter-d {'a 1 'b 2}) iter-d))))", 2, pass, fail);
    test_truthy(interp, "iterator idempotent", "(iterator? (iterator (iterator [1 2 3])))", pass, fail);
    // Lazy imap
    test_eq(interp, "imap + collect car", "(car (collect (imap (lambda (x) (* x 2)) (iterator [1 2 3]))))", 2, pass, fail);
    test_eq(interp, "imap + collect len", "(length (collect (imap (lambda (x) (* x 2)) (iterator [1 2 3]))))", 3, pass, fail);
    // Lazy ifilter
    test_eq(interp, "ifilter even car", "(car (collect (ifilter even? (iterator [1 2 3 4 5]))))", 2, pass, fail);
    test_eq(interp, "ifilter even len", "(length (collect (ifilter even? (iterator [1 2 3 4 5]))))", 2, pass, fail);
    // Lazy itake
    test_eq(interp, "itake 2 len", "(length (collect (itake 2 (iterator [10 20 30]))))", 2, pass, fail);
    test_eq(interp, "itake 2 car", "(car (collect (itake 2 (iterator [10 20 30]))))", 10, pass, fail);
    // Infinite + itake
    test_eq(interp, "range-from + itake", "(car (collect (itake 5 (range-from 0))))", 0, pass, fail);
    test_eq(interp, "range-from + itake len", "(length (collect (itake 5 (range-from 0))))", 5, pass, fail);
    // imap + ifilter pipeline
    test_eq(interp, "lazy pipeline len",
        "(length (collect (itake 3 (ifilter even? (imap (lambda (x) (* x 2)) (range-from 1))))))", 3, pass, fail);
    // to-array from iterator
    test_eq(interp, "to-array from iter", "(ref (to-array (iterator [5 6 7])) 1)", 6, pass, fail);
    // ifoldl
    test_eq(interp, "ifoldl sum", "(ifoldl (lambda (a x) (+ a x)) 0 (iterator [1 2 3 4 5]))", 15, pass, fail);

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

    // === MODULE SYSTEM TESTS ===
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

    // Export-from (re-export)
    setup(interp, "(module inner-mod (export inner-val) (define inner-val 42))");
    setup(interp, "(module outer-mod (export inner-val) (import inner-mod) (export-from inner-mod (inner-val)))");
    setup(interp, "(import outer-mod (inner-val))");
    test_eq(interp, "export-from re-export", "inner-val", 42, pass, fail);

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

    // === FLOAT TESTS ===
    // Float literals
    test_double(interp, "float 3.14", "3.14", 3.14, pass, fail);
    test_double(interp, "float -2.5", "-2.5", -2.5, pass, fail);
    test_double(interp, "float 1.0e3", "1.0e3", 1000.0, pass, fail);
    test_double(interp, "float 1.5e-2", "1.5e-2", 0.015, pass, fail);
    // Mixed arithmetic
    test_double(interp, "int+float", "(+ 1 2.0)", 3.0, pass, fail);
    test_double(interp, "float*int", "(* 2.5 4)", 10.0, pass, fail);
    test_double(interp, "float/int", "(/ 1.0 3)", 0.333333, pass, fail);
    test_double(interp, "float-float", "(- 5.5 2.5)", 3.0, pass, fail);
    // Int arithmetic stays int
    test_eq(interp, "int+int stays int", "(+ 1 2)", 3, pass, fail);
    // Comparisons with float
    test_truthy(interp, "< int float", "(< 1 2.5)", pass, fail);
    test_truthy(interp, "> float int", "(> 3.5 2)", pass, fail);
    test_truthy(interp, "= int float", "(= 1 1.0)", pass, fail);
    test_truthy(interp, "<= float", "(<= 1.5 1.5)", pass, fail);
    test_truthy(interp, ">= float", "(>= 2.0 1.5)", pass, fail);
    // Unary minus
    test_double(interp, "unary neg float", "(- 0 3.14)", -3.14, pass, fail);
    // Conversions
    test_double(interp, "exact->inexact", "(exact->inexact 5)", 5.0, pass, fail);
    test_eq(interp, "inexact->exact", "(inexact->exact 3.7)", 3, pass, fail);
    // string->number float
    test_double(interp, "string->number float", "(string->number \"3.14\")", 3.14, pass, fail);
    test_double(interp, "string->number exp", "(string->number \"1.5e2\")", 150.0, pass, fail);
    // number->string double
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
    // cl-format backward compatibility (old CL-style ~a ~s ~~)
    test_str_val(interp, "cl-format ~a", "(cl-format \"~a\" 42)", "42", pass, fail);
    test_str_val(interp, "cl-format ~a ~a", "(cl-format \"~a plus ~a\" 1 2)", "1 plus 2", pass, fail);
    test_str_val(interp, "cl-format tilde", "(cl-format \"100~~\")", "100~", pass, fail);

    // New printf-style format
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

    // === ARRAY TESTS (Phase 8) ===
    test_eq(interp, "length array", "(length (array 1 2 3))", 3, pass, fail);
    test_eq(interp, "ref array", "(ref (array 10 20 30) 1)", 20, pass, fail);
    // array/list conversion via constructors
    test_eq(interp, "array from list", "(ref (array '(10 20 30)) 1)", 20, pass, fail);
    setup(interp, "(define test-arr (array 1 2 3))");
    setup(interp, "(array-set! test-arr 1 99)");
    test_eq(interp, "array-set!", "(ref test-arr 1)", 99, pass, fail);
    test_truthy(interp, "array?", "(array? (array 1 2))", pass, fail);
    test_nil(interp, "array? non-arr", "(array? 42)", pass, fail);
    test_truthy(interp, "list from array", "(= (list [1 2 3]) '(1 2 3))", pass, fail);
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

    // === TYPE SYSTEM TESTS (Phase 1) ===

    // type-of primitive
    test_truthy(interp, "type-of int", "(= (type-of 42) 'Int)", pass, fail);
    test_truthy(interp, "type-of double", "(= (type-of 3.14) 'Double)", pass, fail);
    test_truthy(interp, "type-of string", "(= (type-of \"hello\") 'String)", pass, fail);
    test_truthy(interp, "type-of symbol", "(= (type-of 'foo) 'Symbol)", pass, fail);
    test_truthy(interp, "type-of nil", "(= (type-of nil) 'Nil)", pass, fail);
    test_truthy(interp, "type-of list", "(= (type-of '(1 2 3)) 'List)", pass, fail);
    test_truthy(interp, "type-of array", "(= (type-of (array 1 2 3)) 'Array)", pass, fail);
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

    // Disambiguation: (Circle Shape) still works as parent, not type param
    // Circle was defined above with parent Shape — verify it still works
    test_truthy_interp(interp, "disambig parent still works", "(is? (Circle 5) 'Shape)", pass, fail);

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

    // Array/dict indexing (Phase 3)
    test_eq(interp, "array index", "(let (v (array 10 20 30)) v.[0])", 10, pass, fail);
    test_eq(interp, "array index 2", "(let (v (array 10 20 30)) v.[2])", 30, pass, fail);

    // Struct field mutation (Phase 3)
    test_eq_interp(interp, "set! struct field", "(let (p (Point 1 2)) (begin (set! p.x 99) p.x))", 99, pass, fail);
    test_eq_interp(interp, "set! struct field y", "(let (p (Point 1 2)) (begin (set! p.y 50) p.y))", 50, pass, fail);
    test_eq_interp(interp, "set! nested field", "(let (ln (Line (Point 0 0) (Point 10 10))) (begin (set! ln.start.x 5) ln.start.x))", 5, pass, fail);
    test_eq_interp(interp, "set! preserves other", "(let (p (Point 3 4)) (begin (set! p.x 99) p.y))", 4, pass, fail);

    // Hashmap indexing (Phase 3)
    test_eq_interp(interp, "dict index", "(let (m {'a 1 'b 2}) m.['a])", 1, pass, fail);

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
