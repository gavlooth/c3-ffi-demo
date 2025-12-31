package parser

import (
	"testing"

	"purple_go/pkg/ast"
)

func TestParseInt(t *testing.T) {
	tests := []struct {
		input    string
		expected int64
	}{
		{"42", 42},
		{"-10", -10},
		{"0", 0},
		{"12345", 12345},
	}

	for _, tt := range tests {
		v, err := ParseString(tt.input)
		if err != nil {
			t.Errorf("ParseString(%q) error: %v", tt.input, err)
			continue
		}
		if !ast.IsInt(v) {
			t.Errorf("ParseString(%q) = %v, want int", tt.input, v)
			continue
		}
		if v.Int != tt.expected {
			t.Errorf("ParseString(%q) = %d, want %d", tt.input, v.Int, tt.expected)
		}
	}
}

func TestParseSym(t *testing.T) {
	tests := []struct {
		input    string
		expected string
	}{
		{"foo", "foo"},
		{"+", "+"},
		{"hello-world", "hello-world"},
		{"x1", "x1"},
	}

	for _, tt := range tests {
		v, err := ParseString(tt.input)
		if err != nil {
			t.Errorf("ParseString(%q) error: %v", tt.input, err)
			continue
		}
		if !ast.IsSym(v) {
			t.Errorf("ParseString(%q) = %v, want symbol", tt.input, v)
			continue
		}
		if v.Str != tt.expected {
			t.Errorf("ParseString(%q) = %q, want %q", tt.input, v.Str, tt.expected)
		}
	}
}

func TestParseList(t *testing.T) {
	tests := []struct {
		input    string
		expected string
	}{
		{"()", "()"},
		{"(+ 1 2)", "(+ 1 2)"},
		{"(foo bar baz)", "(foo bar baz)"},
		{"(a (b c) d)", "(a (b c) d)"},
	}

	for _, tt := range tests {
		v, err := ParseString(tt.input)
		if err != nil {
			t.Errorf("ParseString(%q) error: %v", tt.input, err)
			continue
		}
		result := v.String()
		if result != tt.expected {
			t.Errorf("ParseString(%q).String() = %q, want %q", tt.input, result, tt.expected)
		}
	}
}

func TestParseQuote(t *testing.T) {
	v, err := ParseString("'foo")
	if err != nil {
		t.Fatalf("ParseString error: %v", err)
	}
	expected := "(quote foo)"
	if v.String() != expected {
		t.Errorf("ParseString('foo) = %q, want %q", v.String(), expected)
	}
}

func TestParseAll(t *testing.T) {
	input := "(+ 1 2) (* 3 4)"
	exprs, err := ParseAllString(input)
	if err != nil {
		t.Fatalf("ParseAllString error: %v", err)
	}
	if len(exprs) != 2 {
		t.Errorf("ParseAllString returned %d exprs, want 2", len(exprs))
	}
}

func TestParseComments(t *testing.T) {
	input := "; this is a comment\n(+ 1 2)"
	v, err := ParseString(input)
	if err != nil {
		t.Fatalf("ParseString error: %v", err)
	}
	if v.String() != "(+ 1 2)" {
		t.Errorf("ParseString with comment = %q, want (+ 1 2)", v.String())
	}
}
