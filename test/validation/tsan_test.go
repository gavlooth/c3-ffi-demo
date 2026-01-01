package validation

import (
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"

	"purple_go/pkg/codegen"
	"purple_go/pkg/parser"
)

var ConcurrencyTestCases = []struct {
	Name string
	Code string
}{
	{"channel_transfer", `
        (let ((ch (make-chan 1))
              (x (cons 1 2)))
          (do
            (go (lambda () (chan-send! ch x)))
            (chan-recv! ch)))
    `},
	{"shared_read", `
        (let ((x (cons 1 2)))
          (do
            (go (lambda () (car x)))
            (go (lambda () (cdr x)))
            x))
    `},
	{"producer_consumer", `
        (let ((ch (make-chan 10)))
          (do
            (go (lambda ()
                  (do (chan-send! ch 1)
                      (chan-send! ch 2)
                      (chan-send! ch 3))))
            (+ (chan-recv! ch)
               (chan-recv! ch)
               (chan-recv! ch))))
    `},
}

func TestThreadSanitizer(t *testing.T) {
	if _, err := exec.LookPath("gcc"); err != nil {
		t.Skip("gcc not available")
	}

	for _, tc := range ConcurrencyTestCases {
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
				"-fsanitize=thread",
				"-g",
				"-o", binFile, cFile,
			)
			if out, err := compile.CombinedOutput(); err != nil {
				msg := string(out)
				if strings.Contains(msg, "fsanitize=thread") || strings.Contains(strings.ToLower(msg), "thread sanitizer") || strings.Contains(strings.ToLower(msg), "not supported") {
					t.Skipf("TSan not supported by compiler: %s", strings.TrimSpace(msg))
				}
				t.Fatalf("compile error: %v\n%s", err, out)
			}

			run := exec.Command(binFile)
			if out, err := run.CombinedOutput(); err != nil {
				t.Errorf("TSan detected race:\n%s", out)
			}
		})
	}
}
