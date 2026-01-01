// Package jit provides Just-In-Time compilation for Purple generated C code.
//
// It uses GCC to compile generated C code to a shared library, then
// executes it via dlopen/dlsym or runs it as a subprocess.
package jit

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"sync"

	"purple_go/pkg/ast"
	"purple_go/pkg/codegen"
)

// JIT represents a Just-In-Time compiler
type JIT struct {
	mu      sync.Mutex
	tempDir string
	counter int
}

// Result holds the result of JIT execution
type Result struct {
	IntValue int64
	Success  bool
	Error    string
}

// Global JIT instance
var globalJIT *JIT
var jitOnce sync.Once

// Get returns the global JIT instance
func Get() *JIT {
	jitOnce.Do(func() {
		dir, err := os.MkdirTemp("", "purple_jit_")
		if err == nil {
			globalJIT = &JIT{tempDir: dir}
		} else {
			globalJIT = &JIT{}
		}
	})
	return globalJIT
}

// IsAvailable returns true if JIT compilation is available
func (j *JIT) IsAvailable() bool {
	// Check if gcc is available
	_, err := exec.LookPath("gcc")
	return err == nil && j.tempDir != ""
}

// CompiledCode represents compiled JIT code
type CompiledCode struct {
	exePath string
}

// Compile compiles C code and returns an executable
func (j *JIT) Compile(code string) (*CompiledCode, error) {
	j.mu.Lock()
	defer j.mu.Unlock()

	if j.tempDir == "" {
		return nil, fmt.Errorf("no temp directory")
	}

	j.counter++
	baseName := fmt.Sprintf("purple_jit_%d", j.counter)
	srcPath := filepath.Join(j.tempDir, baseName+".c")
	exePath := filepath.Join(j.tempDir, baseName)

	// Write source file
	if err := os.WriteFile(srcPath, []byte(code), 0644); err != nil {
		return nil, fmt.Errorf("failed to write source: %v", err)
	}

	// Compile with gcc - create executable that prints result
	cmd := exec.Command("gcc", "-std=c99", "-pthread", "-O2", "-o", exePath, srcPath)
	output, err := cmd.CombinedOutput()
	if err != nil {
		return nil, fmt.Errorf("GCC compilation failed: %v\n%s", err, output)
	}

	return &CompiledCode{exePath: exePath}, nil
}

// Run executes the compiled code and returns the result
func (cc *CompiledCode) Run() Result {
	if cc.exePath == "" {
		return Result{Success: false, Error: "no executable"}
	}

	cmd := exec.Command(cc.exePath)
	output, err := cmd.Output()
	if err != nil {
		return Result{Success: false, Error: err.Error()}
	}

	// Parse the output as an integer
	outStr := strings.TrimSpace(string(output))
	result, err := strconv.ParseInt(outStr, 10, 64)
	if err != nil {
		return Result{Success: false, Error: fmt.Sprintf("failed to parse result: %s", outStr)}
	}

	return Result{
		IntValue: result,
		Success:  true,
	}
}

// Close releases resources associated with compiled code
func (cc *CompiledCode) Close() {
	if cc.exePath != "" {
		os.Remove(cc.exePath)
		os.Remove(cc.exePath + ".c")
	}
}

// Cleanup removes the temp directory
func (j *JIT) Cleanup() {
	if j.tempDir != "" {
		os.RemoveAll(j.tempDir)
	}
}

// CompileAndRun compiles a Purple value (or code) and executes it via JIT.
// For non-code values, this emits a constant program that returns the value.
func CompileAndRun(value *ast.Value) (*ast.Value, error) {
	j := Get()
	if !j.IsAvailable() {
		return nil, fmt.Errorf("JIT not available (gcc not found)")
	}

	var program string
	if ast.IsCode(value) {
		program = codegen.GenerateProgramToString([]*ast.Value{value})
	} else {
		// Generate a constant program for the evaluated value.
		program = codegen.GenerateProgramToString([]*ast.Value{value})
	}

	compiled, err := j.Compile(program)
	if err != nil {
		return nil, err
	}
	defer compiled.Close()

	// Run and parse output for codegen-style output.
	out, err := runProgram(compiled.exePath)
	if err != nil {
		return nil, err
	}
	if val, ok := parseIntFromOutput(out); ok {
		return ast.NewInt(val), nil
	}

	// No integer output; return nil to indicate non-int result.
	return ast.Nil, nil
}

func runProgram(path string) ([]byte, error) {
	cmd := exec.Command(path)
	out, err := cmd.Output()
	if err != nil {
		return nil, err
	}
	return out, nil
}

func parseIntFromOutput(out []byte) (int64, bool) {
	fields := strings.Fields(string(out))
	for i := len(fields) - 1; i >= 0; i-- {
		if v, err := strconv.ParseInt(fields[i], 10, 64); err == nil {
			return v, true
		}
	}
	return 0, false
}

// WrapCode wraps Purple-generated code in a compilable C program
func WrapCode(purpleCode, runtime string) string {
	return fmt.Sprintf(`#include <stdio.h>
%s

int main(void) {
    Obj* result = %s;
    if (result && !result->is_pair) {
        printf("%%ld\n", result->i);
    } else {
        printf("0\n");
    }
    return 0;
}
`, runtime, purpleCode)
}

// WrapCodeWithMain wraps code that already has computation, just needs to return
func WrapCodeWithMain(computation, runtime string) string {
	return fmt.Sprintf(`#include <stdio.h>
%s

int main(void) {
    long result = (long)(%s);
    printf("%%ld\n", result);
    return 0;
}
`, runtime, computation)
}
