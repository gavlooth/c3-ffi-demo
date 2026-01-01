package eval

import "purple_go/pkg/ast"

// NewEnv returns a fresh meta-environment with the default bindings.
func NewEnv() *ast.Value {
	env := DefaultEnv()
	return NewMenv(ast.Nil, env)
}

// Equal compares two values for structural equality.
func Equal(a, b *ast.Value) bool {
	return valuesEqual(a, b)
}

// Show renders a value as a string for debugging.
func Show(v *ast.Value) string {
	if v == nil {
		return "nil"
	}
	return v.String()
}
