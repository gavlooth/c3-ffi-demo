package validation

var MemoryTestCases = []struct {
	Name     string
	Code     string
	Expected string
}{
	// Basic allocations
	{"int_alloc", "(+ 1 2)", "3"},
	{"pair_alloc", "(car (cons 1 2))", "1"},
	{"nested_pairs", "(car (cdr (cons 1 (cons 2 3))))", "2"},

	// Let bindings
	{"let_simple", "(let ((x 1)) x)", "1"},
	{"let_nested", "(let ((x 1)) (let ((y 2)) (+ x y)))", "3"},
	{"let_shadow", "(let ((x 1)) (let ((x 2)) x))", "2"},

	// Functions
	{"lambda_simple", "((lambda (x) x) 42)", "42"},
	{"lambda_closure", "(let ((y 10)) ((lambda (x) (+ x y)) 5))", "15"},
	{"recursive", "(letrec ((f (lambda (n) (if (= n 0) 1 (* n (f (- n 1))))))) (f 5))", "120"},

	// Lists
	{"list_create", "(length (list 1 2 3))", "3"},
	{"list_map", "(car (map (lambda (x) (+ x 1)) (list 1 2 3)))", "2"},
	{"list_fold", "(fold + 0 (list 1 2 3 4 5))", "15"},

	// Boxes (mutable)
	{"box_simple", "(let ((b (box 1))) (unbox b))", "1"},
	{"box_set", "(let ((b (box 1))) (do (set-box! b 2) (unbox b)))", "2"},

	// Error handling
	{"try_no_error", "(try (+ 1 2) (lambda (e) 0))", "3"},
	{"try_with_error", "(try (error \"fail\") (lambda (e) 42))", "42"},

	// User-defined types
	{"deftype_simple", "(do (deftype Point (x int) (y int)) (Point-x (mk-Point 3 4)))", "3"},

	// Cycles with weak refs
	{"weak_cycle", `
        (do
          (deftype Node (val int) (next Node) (prev Node :weak))
          (let ((a (mk-Node 1 nil nil))
                (b (mk-Node 2 nil nil)))
            (do
              (set! (Node-next a) b)
              (set! (Node-prev b) a)
              (Node-val a))))
    `, "1"},
}
