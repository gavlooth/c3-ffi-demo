package eval

import (
	"testing"

	"purple_go/pkg/ast"
	"purple_go/pkg/parser"
)

func evalString(input string) *ast.Value {
	p := parser.New(input)
	expr, err := p.Parse()
	if err != nil {
		return nil
	}
	return Run(expr)
}

func TestArithmetic(t *testing.T) {
	tests := []struct {
		input    string
		expected int64
	}{
		{"(+ 1 2)", 3},
		{"(- 10 3)", 7},
		{"(* 4 5)", 20},
		{"(/ 20 4)", 5},
		{"(% 17 5)", 2},
		{"(+ 1 (+ 2 3))", 6},
		{"(* (+ 1 2) (- 5 2))", 9},
	}

	for _, tt := range tests {
		result := evalString(tt.input)
		if result == nil || !ast.IsInt(result) {
			t.Errorf("evalString(%q) = %v, want int", tt.input, result)
			continue
		}
		if result.Int != tt.expected {
			t.Errorf("evalString(%q) = %d, want %d", tt.input, result.Int, tt.expected)
		}
	}
}

func TestComparison(t *testing.T) {
	tests := []struct {
		input    string
		expected bool
	}{
		{"(= 1 1)", true},
		{"(= 1 2)", false},
		{"(< 1 2)", true},
		{"(< 2 1)", false},
		{"(> 2 1)", true},
		{"(> 1 2)", false},
		{"(<= 1 1)", true},
		{"(<= 1 2)", true},
		{"(>= 2 2)", true},
	}

	for _, tt := range tests {
		result := evalString(tt.input)
		if result == nil {
			t.Errorf("evalString(%q) = nil", tt.input)
			continue
		}
		isTrue := !ast.IsNil(result)
		if isTrue != tt.expected {
			t.Errorf("evalString(%q) = %v, want %v", tt.input, isTrue, tt.expected)
		}
	}
}

func TestLet(t *testing.T) {
	tests := []struct {
		input    string
		expected int64
	}{
		{"(let ((x 10)) x)", 10},
		{"(let ((x 5) (y 3)) (+ x y))", 8},
		{"(let ((x 10)) (let ((y 20)) (+ x y)))", 30},
		{"(let ((a 2) (b 3) (c 4)) (+ a (+ b c)))", 9},
	}

	for _, tt := range tests {
		result := evalString(tt.input)
		if result == nil || !ast.IsInt(result) {
			t.Errorf("evalString(%q) = %v, want int", tt.input, result)
			continue
		}
		if result.Int != tt.expected {
			t.Errorf("evalString(%q) = %d, want %d", tt.input, result.Int, tt.expected)
		}
	}
}

func TestIf(t *testing.T) {
	tests := []struct {
		input    string
		expected int64
	}{
		{"(if t 1 2)", 1},
		{"(if () 1 2)", 2},
		{"(if (= 1 1) 100 200)", 100},
		{"(if (= 1 2) 100 200)", 200},
	}

	for _, tt := range tests {
		result := evalString(tt.input)
		if result == nil || !ast.IsInt(result) {
			t.Errorf("evalString(%q) = %v, want int", tt.input, result)
			continue
		}
		if result.Int != tt.expected {
			t.Errorf("evalString(%q) = %d, want %d", tt.input, result.Int, tt.expected)
		}
	}
}

func TestLambda(t *testing.T) {
	tests := []struct {
		input    string
		expected int64
	}{
		{"(let ((f (lambda (x) (* x x)))) (f 5))", 25},
		{"(let ((add (lambda (a b) (+ a b)))) (add 3 4))", 7},
		{"(let ((f (lambda (x) (+ x 1)))) (f (f (f 0))))", 3},
	}

	for _, tt := range tests {
		result := evalString(tt.input)
		if result == nil || !ast.IsInt(result) {
			t.Errorf("evalString(%q) = %v, want int", tt.input, result)
			continue
		}
		if result.Int != tt.expected {
			t.Errorf("evalString(%q) = %d, want %d", tt.input, result.Int, tt.expected)
		}
	}
}

func TestLetrec(t *testing.T) {
	// Factorial
	input := "(letrec ((fact (lambda (n) (if (= n 0) 1 (* n (fact (- n 1))))))) (fact 5))"
	result := evalString(input)
	if result == nil || !ast.IsInt(result) {
		t.Errorf("factorial = %v, want int", result)
		return
	}
	if result.Int != 120 {
		t.Errorf("factorial(5) = %d, want 120", result.Int)
	}
}

func TestLift(t *testing.T) {
	result := evalString("(lift 42)")
	if result == nil || !ast.IsCode(result) {
		t.Errorf("lift(42) = %v, want code", result)
		return
	}
	if result.Str != "mk_int(42)" {
		t.Errorf("lift(42) = %q, want mk_int(42)", result.Str)
	}
}

func TestCons(t *testing.T) {
	result := evalString("(cons 1 (cons 2 ()))")
	if result == nil || !ast.IsCell(result) {
		t.Errorf("cons result = %v, want cell", result)
		return
	}
	expected := "(1 2)"
	if result.String() != expected {
		t.Errorf("cons result = %q, want %q", result.String(), expected)
	}
}

func TestQuote(t *testing.T) {
	result := evalString("'foo")
	if result == nil || !ast.IsSym(result) {
		t.Errorf("quote = %v, want symbol", result)
		return
	}
	if result.Str != "foo" {
		t.Errorf("quote = %q, want foo", result.Str)
	}
}
