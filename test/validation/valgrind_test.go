package validation

import (
	"os"
	"os/exec"
	"path/filepath"
	"testing"

	"purple_go/pkg/codegen"
	"purple_go/pkg/parser"
)

func TestValgrindNoLeaks(t *testing.T) {
	if _, err := exec.LookPath("gcc"); err != nil {
		t.Skip("gcc not available")
	}
	if _, err := exec.LookPath("valgrind"); err != nil {
		t.Skip("valgrind not available")
	}

	tests := []struct {
		name string
		code string
	}{
		{"simple_int", "(+ 1 2)"},
		{"cons_cell", "(let ((x (cons 1 2))) (car x))"},
		{"nested_let", "(let ((x 1)) (let ((y 2)) (+ x y)))"},
		{"lambda", "((lambda (x) (+ x 1)) 5)"},
		{"list_ops", "(fold + 0 (list 1 2 3 4 5))"},
	}

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			expr, err := parser.Parse(tc.code)
			if err != nil {
				t.Fatalf("parse error: %v", err)
			}

			cCode := codegen.GenerateProgram(expr)

			tmpDir := t.TempDir()
			cFile := filepath.Join(tmpDir, "test.c")
			binFile := filepath.Join(tmpDir, "test")

			if err := os.WriteFile(cFile, []byte(cCode), 0644); err != nil {
				t.Fatalf("write error: %v", err)
			}

			compile := exec.Command("gcc", "-std=c99", "-pthread", "-o", binFile, cFile)
			if out, err := compile.CombinedOutput(); err != nil {
				t.Fatalf("compile error: %v\n%s", err, out)
			}

			valgrind := exec.Command("valgrind",
				"--leak-check=full",
				"--error-exitcode=1",
				"--errors-for-leak-kinds=all",
				binFile,
			)
			if out, err := valgrind.CombinedOutput(); err != nil {
				t.Errorf("valgrind found issues:\n%s", out)
			}
		})
	}
}
