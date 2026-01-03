package parser

import (
	"purple_go/pkg/ast"
	"testing"
)

func TestParseArray(t *testing.T) {
	tests := []struct {
		input    string
		expected string
	}{
		{"[1 2 3]", "[1 2 3]"},
		{"[]", "[]"},
		{"[1]", "[1]"},
		{"[[1 2] [3 4]]", "[[1 2] [3 4]]"},
	}

	for _, tt := range tests {
		p := NewPikaParser(tt.input)
		result, err := p.Parse()
		if err != nil {
			t.Errorf("Parse(%q) error: %v", tt.input, err)
			continue
		}
		if result.String() != tt.expected {
			t.Errorf("Parse(%q) = %q, want %q", tt.input, result.String(), tt.expected)
		}
	}
}

func TestParseTypeLit(t *testing.T) {
	tests := []struct {
		input    string
		expected string
	}{
		{"{Int}", "{Int}"},
		{"{Array Int}", "{Array Int}"},
		{"{Result Int Error}", "{Result Int Error}"},
	}

	for _, tt := range tests {
		p := NewPikaParser(tt.input)
		result, err := p.Parse()
		if err != nil {
			t.Errorf("Parse(%q) error: %v", tt.input, err)
			continue
		}
		if result.String() != tt.expected {
			t.Errorf("Parse(%q) = %q, want %q", tt.input, result.String(), tt.expected)
		}
	}
}

func TestParseDict(t *testing.T) {
	tests := []struct {
		input    string
		expected string
	}{
		{"#{}", "#{}"},
		{"#{:a 1}", "#{:a 1}"},
		{"#{:a 1 :b 2}", "#{:a 1 :b 2}"},
	}

	for _, tt := range tests {
		p := NewPikaParser(tt.input)
		result, err := p.Parse()
		if err != nil {
			t.Errorf("Parse(%q) error: %v", tt.input, err)
			continue
		}
		if result.String() != tt.expected {
			t.Errorf("Parse(%q) = %q, want %q", tt.input, result.String(), tt.expected)
		}
	}
}

func TestParseKeyword(t *testing.T) {
	tests := []struct {
		input    string
		expected string
	}{
		{":foo", ":foo"},
		{":bar-baz", ":bar-baz"},
		{":when", ":when"},
	}

	for _, tt := range tests {
		p := NewPikaParser(tt.input)
		result, err := p.Parse()
		if err != nil {
			t.Errorf("Parse(%q) error: %v", tt.input, err)
			continue
		}
		if !ast.IsKeyword(result) {
			t.Errorf("Parse(%q) should be keyword, got %v", tt.input, ast.TagName(result.Tag))
		}
		if result.String() != tt.expected {
			t.Errorf("Parse(%q) = %q, want %q", tt.input, result.String(), tt.expected)
		}
	}
}

func TestParseDotNotation(t *testing.T) {
	tests := []struct {
		input       string
		expectedStr string
	}{
		{"foo.bar", "(get foo (quote bar))"},
		{"foo.bar.baz", "(get (get foo (quote bar)) (quote baz))"},
	}

	for _, tt := range tests {
		p := NewPikaParser(tt.input)
		result, err := p.Parse()
		if err != nil {
			t.Errorf("Parse(%q) error: %v", tt.input, err)
			continue
		}
		if result.String() != tt.expectedStr {
			t.Errorf("Parse(%q) = %q, want %q", tt.input, result.String(), tt.expectedStr)
		}
	}
}

func TestParseFunctionalAccessor(t *testing.T) {
	p := NewPikaParser(".field")
	result, err := p.Parse()
	if err != nil {
		t.Errorf("Parse(.field) error: %v", err)
		return
	}

	// Should be (lambda (it) (get it (quote field)))
	if !ast.IsCell(result) || result.Car.Str != "lambda" {
		t.Errorf("Parse(.field) should be lambda, got %q", result.String())
	}
}

func TestParseHexNumber(t *testing.T) {
	tests := []struct {
		input    string
		expected int64
	}{
		{"0xFF", 255},
		{"0x10", 16},
		{"0xDEAD_BEEF", 0xDEADBEEF},
	}

	for _, tt := range tests {
		p := NewPikaParser(tt.input)
		result, err := p.Parse()
		if err != nil {
			t.Errorf("Parse(%q) error: %v", tt.input, err)
			continue
		}
		if !ast.IsInt(result) || result.Int != tt.expected {
			t.Errorf("Parse(%q) = %d, want %d", tt.input, result.Int, tt.expected)
		}
	}
}

func TestParseBinaryNumber(t *testing.T) {
	tests := []struct {
		input    string
		expected int64
	}{
		{"0b101", 5},
		{"0b1111_0000", 240},
	}

	for _, tt := range tests {
		p := NewPikaParser(tt.input)
		result, err := p.Parse()
		if err != nil {
			t.Errorf("Parse(%q) error: %v", tt.input, err)
			continue
		}
		if !ast.IsInt(result) || result.Int != tt.expected {
			t.Errorf("Parse(%q) = %d, want %d", tt.input, result.Int, tt.expected)
		}
	}
}

func TestParseMixedBrackets(t *testing.T) {
	input := "(define (add [x {Int}] [y {Int}]) {Int} (+ x y))"
	p := NewPikaParser(input)
	result, err := p.Parse()
	if err != nil {
		t.Errorf("Parse(%q) error: %v", input, err)
		return
	}

	// Should be a list starting with 'define'
	if !ast.IsCell(result) || result.Car.Str != "define" {
		t.Errorf("Parse(%q) should be (define ...), got %q", input, result.String())
	}
}

func TestParseMatchBranches(t *testing.T) {
	input := `(match x
		[0 :zero]
		[n :positive])`
	p := NewPikaParser(input)
	result, err := p.Parse()
	if err != nil {
		t.Errorf("Parse(%q) error: %v", input, err)
		return
	}

	// Should be a list starting with 'match'
	if !ast.IsCell(result) || result.Car.Str != "match" {
		t.Errorf("Parse should be (match ...), got %q", result.String())
	}

	// Second element should be x
	second := result.Cdr.Car
	if !ast.IsSym(second) || second.Str != "x" {
		t.Errorf("Second element should be x, got %q", second.String())
	}

	// Third element should be array [0 :zero]
	third := result.Cdr.Cdr.Car
	if !ast.IsArray(third) {
		t.Errorf("Third element should be array, got %q", third.String())
	}
}

func TestParseSyntaxQuote(t *testing.T) {
	input := "#'(if x y z)"
	p := NewPikaParser(input)
	result, err := p.Parse()
	if err != nil {
		t.Errorf("Parse(%q) error: %v", input, err)
		return
	}

	// Should be (syntax (if x y z))
	if !ast.IsCell(result) || result.Car.Str != "syntax" {
		t.Errorf("Parse(%q) should be (syntax ...), got %q", input, result.String())
	}
}

func TestParseNothing(t *testing.T) {
	p := NewPikaParser("nothing")
	result, err := p.Parse()
	if err != nil {
		t.Errorf("Parse(nothing) error: %v", err)
		return
	}
	if !ast.IsNothing(result) {
		t.Errorf("Parse(nothing) should be TNothing, got %v", ast.TagName(result.Tag))
	}
}

func TestParseUnderscoreInNumbers(t *testing.T) {
	tests := []struct {
		input    string
		expected int64
	}{
		{"1_000_000", 1000000},
		{"1_2_3", 123},
	}

	for _, tt := range tests {
		p := NewPikaParser(tt.input)
		result, err := p.Parse()
		if err != nil {
			t.Errorf("Parse(%q) error: %v", tt.input, err)
			continue
		}
		if !ast.IsInt(result) || result.Int != tt.expected {
			t.Errorf("Parse(%q) = %d, want %d", tt.input, result.Int, tt.expected)
		}
	}
}
