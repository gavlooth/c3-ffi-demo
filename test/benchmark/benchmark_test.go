package benchmark

import (
	"testing"

	"purple_go/pkg/eval"
	"purple_go/pkg/jit"
	"purple_go/pkg/parser"
)

func BenchmarkListOperations(b *testing.B) {
	code := "(fold + 0 (map (lambda (x) (* x x)) (range 1000)))"
	expr, _ := parser.Parse(code)

	b.Run("interpreter", func(b *testing.B) {
		menv := eval.NewEnv()
		for i := 0; i < b.N; i++ {
			eval.Eval(expr, menv)
		}
	})

	b.Run("compiled", func(b *testing.B) {
		j := jit.Get()
		if !j.IsAvailable() {
			b.Skip("JIT not available (gcc not found)")
		}
		for i := 0; i < b.N; i++ {
			_, _ = jit.CompileAndRun(expr)
		}
	})
}

func BenchmarkRecursion(b *testing.B) {
	code := "(letrec ((fib (lambda (n) (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2))))))) (fib 20))"
	expr, _ := parser.Parse(code)

	b.Run("interpreter", func(b *testing.B) {
		menv := eval.NewEnv()
		for i := 0; i < b.N; i++ {
			eval.Eval(expr, menv)
		}
	})

	b.Run("compiled", func(b *testing.B) {
		j := jit.Get()
		if !j.IsAvailable() {
			b.Skip("JIT not available (gcc not found)")
		}
		for i := 0; i < b.N; i++ {
			_, _ = jit.CompileAndRun(expr)
		}
	})
}

func BenchmarkAllocation(b *testing.B) {
	code := `
        (letrec ((build (lambda (n)
                          (if (= n 0)
                              nil
                              (cons n (build (- n 1)))))))
          (build 10000))
    `
	expr, _ := parser.Parse(code)

	b.Run("compiled", func(b *testing.B) {
		j := jit.Get()
		if !j.IsAvailable() {
			b.Skip("JIT not available (gcc not found)")
		}
		for i := 0; i < b.N; i++ {
			_, _ = jit.CompileAndRun(expr)
		}
	})
}

func BenchmarkCycles(b *testing.B) {
	code := `
        (do
          (deftype Node (val int) (next Node) (prev Node :weak))
          (letrec ((build (lambda (n prev)
                            (if (= n 0)
                                nil
                                (let ((node (mk-Node n nil prev)))
                                  (do
                                    (if prev (set! (Node-next prev) node) nil)
                                    (build (- n 1) node)))))))
            (build 1000 nil)))
    `
	expr, _ := parser.Parse(code)

	b.Run("compiled", func(b *testing.B) {
		j := jit.Get()
		if !j.IsAvailable() {
			b.Skip("JIT not available (gcc not found)")
		}
		for i := 0; i < b.N; i++ {
			_, _ = jit.CompileAndRun(expr)
		}
	})
}

func BenchmarkChannels(b *testing.B) {
	code := `
        (let ((ch (make-chan 100)))
          (do
            (go (lambda ()
                  (letrec ((send (lambda (n)
                                   (if (= n 0)
                                       nil
                                       (do (chan-send! ch n)
                                           (send (- n 1)))))))
                    (send 1000))))
            (letrec ((recv (lambda (sum n)
                             (if (= n 0)
                                 sum
                                 (recv (+ sum (chan-recv! ch)) (- n 1))))))
              (recv 0 1000))))
    `
	expr, _ := parser.Parse(code)

	b.Run("compiled", func(b *testing.B) {
		j := jit.Get()
		if !j.IsAvailable() {
			b.Skip("JIT not available (gcc not found)")
		}
		for i := 0; i < b.N; i++ {
			_, _ = jit.CompileAndRun(expr)
		}
	})
}
