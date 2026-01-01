package analysis

import (
	"testing"

	"purple_go/pkg/ast"
)

func TestInterproceduralOwnership(t *testing.T) {
	sa := NewSummaryAnalyzer()

	params := ast.List1(ast.NewSym("p"))
	body := ast.List2(ast.NewSym("car"), ast.NewSym("p"))

	summary := sa.AnalyzeFunction("consume-pair", params, body)
	if summary == nil {
		t.Fatal("summary not found")
	}

	if len(summary.Params) == 0 {
		t.Fatal("summary params missing")
	}

	if summary.Params[0].Ownership != OwnerBorrowed {
		t.Errorf("expected borrowed, got %v", summary.Params[0].Ownership)
	}
}
