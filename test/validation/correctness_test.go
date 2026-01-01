package validation

import (
	"testing"

	"purple_go/pkg/eval"
	"purple_go/pkg/jit"
	"purple_go/pkg/parser"
)

func TestCompiledMatchesInterpreter(t *testing.T) {
	j := jit.Get()
	if !j.IsAvailable() {
		t.Skip("JIT not available (gcc not found)")
	}

	for _, tc := range MemoryTestCases {
		t.Run(tc.Name, func(t *testing.T) {
			expr, err := parser.Parse(tc.Code)
			if err != nil {
				t.Fatalf("parse error: %v", err)
			}

			interpResult := eval.Eval(expr, eval.NewEnv())
			if interpResult == nil {
				t.Fatalf("interp error: nil result")
			}

			jitResult, err := jit.CompileAndRun(interpResult)
			if err != nil {
				t.Fatalf("jit error: %v", err)
			}

			if !eval.Equal(interpResult, jitResult) {
				t.Errorf("mismatch: interp=%v, compiled=%v",
					eval.Show(interpResult), eval.Show(jitResult))
			}
		})
	}
}
