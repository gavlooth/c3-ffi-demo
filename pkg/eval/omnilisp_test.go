package eval

import (
	"purple_go/pkg/ast"
	"purple_go/pkg/parser"
	"testing"
)

// Helper to parse and evaluate
func parseEval(t *testing.T, input string) *ast.Value {
	p := parser.NewPikaParser(input)
	expr, err := p.Parse()
	if err != nil {
		t.Fatalf("Parse error: %v", err)
	}
	env := DefaultEnv()
	menv := NewMenv(nil, env)
	return Eval(expr, menv)
}

func TestOmniLispArrayLiteral(t *testing.T) {
	result := parseEval(t, "[1 2 3]")
	if !ast.IsArray(result) {
		t.Errorf("Expected array, got %v", ast.TagName(result.Tag))
	}
	if len(result.ArrayData) != 3 {
		t.Errorf("Expected 3 elements, got %d", len(result.ArrayData))
	}
}

func TestOmniLispArrayOperations(t *testing.T) {
	// array-ref
	result := parseEval(t, "(array-ref [10 20 30] 1)")
	if !ast.IsInt(result) || result.Int != 20 {
		t.Errorf("array-ref expected 20, got %v", result.String())
	}

	// array-length
	result = parseEval(t, "(array-length [1 2 3 4 5])")
	if !ast.IsInt(result) || result.Int != 5 {
		t.Errorf("array-length expected 5, got %v", result.String())
	}
}

func TestOmniLispDictLiteral(t *testing.T) {
	result := parseEval(t, "#{:a 1 :b 2}")
	if !ast.IsDict(result) {
		t.Errorf("Expected dict, got %v", ast.TagName(result.Tag))
	}
	if len(result.DictKeys) != 2 {
		t.Errorf("Expected 2 entries, got %d", len(result.DictKeys))
	}
}

func TestOmniLispDictOperations(t *testing.T) {
	result := parseEval(t, "(dict-ref #{:a 42 :b 99} :a)")
	if !ast.IsInt(result) || result.Int != 42 {
		t.Errorf("dict-ref expected 42, got %v", result.String())
	}
}

func TestOmniLispTypeLiteral(t *testing.T) {
	result := parseEval(t, "{Int}")
	if !ast.IsTypeLit(result) {
		t.Errorf("Expected type literal, got %v", ast.TagName(result.Tag))
	}
	if result.TypeName != "Int" {
		t.Errorf("Expected type name 'Int', got '%s'", result.TypeName)
	}
}

func TestOmniLispParametricType(t *testing.T) {
	result := parseEval(t, "{Array Int}")
	if !ast.IsTypeLit(result) {
		t.Errorf("Expected type literal, got %v", ast.TagName(result.Tag))
	}
	if result.TypeName != "Array" {
		t.Errorf("Expected type name 'Array', got '%s'", result.TypeName)
	}
	if len(result.TypeParams) != 1 {
		t.Errorf("Expected 1 type param, got %d", len(result.TypeParams))
	}
}

func TestOmniLispKeyword(t *testing.T) {
	result := parseEval(t, ":foo")
	if !ast.IsKeyword(result) {
		t.Errorf("Expected keyword, got %v", ast.TagName(result.Tag))
	}
	if result.Str != "foo" {
		t.Errorf("Expected 'foo', got '%s'", result.Str)
	}
}

func TestOmniLispTuple(t *testing.T) {
	result := parseEval(t, "(tuple 1 2 3)")
	if !ast.IsTuple(result) {
		t.Errorf("Expected tuple, got %v", ast.TagName(result.Tag))
	}
	if len(result.TupleData) != 3 {
		t.Errorf("Expected 3 elements, got %d", len(result.TupleData))
	}
}

func TestOmniLispNothing(t *testing.T) {
	result := parseEval(t, "nothing")
	if !ast.IsNothing(result) {
		t.Errorf("Expected nothing, got %v", ast.TagName(result.Tag))
	}
}

func TestOmniLispGenericGet(t *testing.T) {
	// get from array
	result := parseEval(t, "(get [10 20 30] 1)")
	if !ast.IsInt(result) || result.Int != 20 {
		t.Errorf("get from array expected 20, got %v", result.String())
	}

	// get from dict
	result = parseEval(t, "(get #{:x 100} :x)")
	if !ast.IsInt(result) || result.Int != 100 {
		t.Errorf("get from dict expected 100, got %v", result.String())
	}
}

func TestOmniLispMixedSyntax(t *testing.T) {
	// Test mixing [] arrays in () forms
	result := parseEval(t, "(array-ref [1 2 3] 0)")
	if !ast.IsInt(result) || result.Int != 1 {
		t.Errorf("Expected 1, got %v", result.String())
	}
}
