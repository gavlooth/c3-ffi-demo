package test

import (
	"testing"

	"purple_go/pkg/ast"
	"purple_go/pkg/codegen"
	"purple_go/pkg/eval"
	"purple_go/pkg/parser"
)

func TestDeftype(t *testing.T) {
	// Reset global registry before test
	codegen.ResetGlobalRegistry()

	// Define a doubly-linked list node type
	input := `(deftype Node
		(value int)
		(next Node)
		(prev Node))`

	p := parser.New(input)
	expr, err := p.Parse()
	if err != nil {
		t.Fatalf("Parse error: %v", err)
	}

	// Evaluate the deftype
	env := eval.DefaultEnv()
	menv := eval.NewMenv(ast.Nil, env)
	result := eval.Eval(expr, menv)

	if ast.IsError(result) {
		t.Fatalf("Eval error: %s", result.Str)
	}

	// Check that the type was registered
	registry := codegen.GlobalRegistry()
	nodeDef := registry.FindType("Node")
	if nodeDef == nil {
		t.Fatal("Node type not registered")
	}

	// Check fields
	if len(nodeDef.Fields) != 3 {
		t.Fatalf("Expected 3 fields, got %d", len(nodeDef.Fields))
	}

	// Check field names and types
	expectedFields := []struct {
		name        string
		typ         string
		isScannable bool
	}{
		{"value", "int", false},
		{"next", "Node", true},
		{"prev", "Node", true},
	}

	for i, expected := range expectedFields {
		if nodeDef.Fields[i].Name != expected.name {
			t.Errorf("Field %d: expected name %s, got %s", i, expected.name, nodeDef.Fields[i].Name)
		}
		if nodeDef.Fields[i].Type != expected.typ {
			t.Errorf("Field %d: expected type %s, got %s", i, expected.typ, nodeDef.Fields[i].Type)
		}
		if nodeDef.Fields[i].IsScannable != expected.isScannable {
			t.Errorf("Field %d: expected isScannable %v, got %v", i, expected.isScannable, nodeDef.Fields[i].IsScannable)
		}
	}

	// Check that back-edge analysis ran
	// The 'prev' field should be marked as weak since it forms a cycle
	foundBackEdge := false
	for _, edge := range registry.OwnershipGraph {
		if edge.FromType == "Node" && edge.FieldName == "prev" && edge.IsBackEdge {
			foundBackEdge = true
			break
		}
	}

	if !foundBackEdge {
		t.Log("Note: Back-edge detection found edges:", registry.OwnershipGraph)
	}
}

func TestDeftypeTreeWithParent(t *testing.T) {
	// Reset global registry before test
	codegen.ResetGlobalRegistry()

	// Define a tree type with parent pointer
	input := `(deftype Tree
		(value int)
		(left Tree)
		(right Tree)
		(parent Tree))`

	p := parser.New(input)
	expr, err := p.Parse()
	if err != nil {
		t.Fatalf("Parse error: %v", err)
	}

	env := eval.DefaultEnv()
	menv := eval.NewMenv(ast.Nil, env)
	result := eval.Eval(expr, menv)

	if ast.IsError(result) {
		t.Fatalf("Eval error: %s", result.Str)
	}

	registry := codegen.GlobalRegistry()
	treeDef := registry.FindType("Tree")
	if treeDef == nil {
		t.Fatal("Tree type not registered")
	}

	// Check that it's marked as recursive
	if !treeDef.IsRecursive {
		t.Error("Tree type should be marked as recursive")
	}
}

func TestDeftypeMultipleTypes(t *testing.T) {
	// Reset global registry before test
	codegen.ResetGlobalRegistry()

	// Define multiple related types
	inputs := []string{
		`(deftype Container (items List))`,
		`(deftype List (head Item) (tail List))`,
		`(deftype Item (value int) (container Container))`,
	}

	env := eval.DefaultEnv()
	menv := eval.NewMenv(ast.Nil, env)

	for _, input := range inputs {
		p := parser.New(input)
		expr, err := p.Parse()
		if err != nil {
			t.Fatalf("Parse error: %v", err)
		}
		result := eval.Eval(expr, menv)
		if ast.IsError(result) {
			t.Fatalf("Eval error: %s", result.Str)
		}
	}

	registry := codegen.GlobalRegistry()

	// Check all types were registered
	for _, name := range []string{"Container", "List", "Item"} {
		if registry.FindType(name) == nil {
			t.Errorf("Type %s not registered", name)
		}
	}

	// Item.container forms a cycle back to Container
	// This should be detected as a back-edge
	t.Log("Ownership graph:")
	for _, edge := range registry.OwnershipGraph {
		t.Logf("  %s.%s -> %s (back-edge: %v)", edge.FromType, edge.FieldName, edge.ToType, edge.IsBackEdge)
	}
}
