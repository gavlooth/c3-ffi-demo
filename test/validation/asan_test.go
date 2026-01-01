package validation

import (
	"os"
	"os/exec"
	"path/filepath"
	"testing"

	"purple_go/pkg/codegen"
	"purple_go/pkg/parser"
)

func TestAddressSanitizer(t *testing.T) {
	if _, err := exec.LookPath("gcc"); err != nil {
		t.Skip("gcc not available")
	}

	for _, tc := range MemoryTestCases {
		t.Run(tc.Name, func(t *testing.T) {
			expr, err := parser.Parse(tc.Code)
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

			compile := exec.Command("gcc",
				"-std=c99", "-pthread",
				"-fsanitize=address",
				"-fno-omit-frame-pointer",
				"-g",
				"-o", binFile, cFile,
			)
			if out, err := compile.CombinedOutput(); err != nil {
				t.Fatalf("compile error: %v\n%s", err, out)
			}

			run := exec.Command(binFile)
			run.Env = append(os.Environ(), "ASAN_OPTIONS=detect_leaks=1")
			if out, err := run.CombinedOutput(); err != nil {
				t.Errorf("ASan detected issue:\n%s", out)
			}
		})
	}
}
