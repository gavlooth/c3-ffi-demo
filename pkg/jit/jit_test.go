package jit

import (
	"testing"
)

func TestJITAvailability(t *testing.T) {
	j := Get()
	// Just test that we can check availability without crashing
	available := j.IsAvailable()
	t.Logf("JIT available: %v", available)
}

func TestJITCompileAndRun(t *testing.T) {
	j := Get()
	if !j.IsAvailable() {
		t.Skip("JIT not available (gcc not found)")
	}

	// Simple C program that returns 42
	code := `
#include <stdio.h>

int main(void) {
    printf("42\n");
    return 0;
}
`

	compiled, err := j.Compile(code)
	if err != nil {
		t.Fatalf("Failed to compile: %v", err)
	}
	defer compiled.Close()

	result := compiled.Run()
	if !result.Success {
		t.Fatalf("Failed to run: %s", result.Error)
	}

	if result.IntValue != 42 {
		t.Errorf("Expected 42, got %d", result.IntValue)
	}
}

func TestJITArithmetic(t *testing.T) {
	j := Get()
	if !j.IsAvailable() {
		t.Skip("JIT not available (gcc not found)")
	}

	// Test arithmetic computation
	code := `
#include <stdio.h>

int main(void) {
    int result = (3 + 4) * 5;
    printf("%d\n", result);
    return 0;
}
`

	compiled, err := j.Compile(code)
	if err != nil {
		t.Fatalf("Failed to compile: %v", err)
	}
	defer compiled.Close()

	result := compiled.Run()
	if !result.Success {
		t.Fatalf("Failed to run: %s", result.Error)
	}

	expected := int64((3 + 4) * 5)
	if result.IntValue != expected {
		t.Errorf("Expected %d, got %d", expected, result.IntValue)
	}
}

func TestWrapCode(t *testing.T) {
	runtime := `
typedef struct Obj {
    int is_pair;
    long i;
} Obj;

static Obj* mk_int(long n) {
    static Obj obj;
    obj.is_pair = 0;
    obj.i = n;
    return &obj;
}
`
	code := WrapCode("mk_int(100)", runtime)

	j := Get()
	if !j.IsAvailable() {
		t.Skip("JIT not available (gcc not found)")
	}

	compiled, err := j.Compile(code)
	if err != nil {
		t.Fatalf("Failed to compile wrapped code: %v", err)
	}
	defer compiled.Close()

	result := compiled.Run()
	if !result.Success {
		t.Fatalf("Failed to run: %s", result.Error)
	}

	if result.IntValue != 100 {
		t.Errorf("Expected 100, got %d", result.IntValue)
	}
}

func TestJITCompileError(t *testing.T) {
	j := Get()
	if !j.IsAvailable() {
		t.Skip("JIT not available (gcc not found)")
	}

	// Invalid C code should fail compilation
	code := `
this is not valid C code!!!
`

	_, err := j.Compile(code)
	if err == nil {
		t.Error("Expected compile error for invalid code")
	}
}

func TestJITMultipleCompilations(t *testing.T) {
	j := Get()
	if !j.IsAvailable() {
		t.Skip("JIT not available (gcc not found)")
	}

	// Test that multiple compilations work correctly
	for i := 0; i < 5; i++ {
		code := `
#include <stdio.h>
int main(void) {
    printf("%d\n", ` + string(rune('0'+i)) + `);
    return 0;
}
`
		compiled, err := j.Compile(code)
		if err != nil {
			t.Fatalf("Failed to compile iteration %d: %v", i, err)
		}

		result := compiled.Run()
		compiled.Close()

		if !result.Success {
			t.Fatalf("Failed to run iteration %d: %s", i, result.Error)
		}

		if result.IntValue != int64(i) {
			t.Errorf("Iteration %d: expected %d, got %d", i, i, result.IntValue)
		}
	}
}
