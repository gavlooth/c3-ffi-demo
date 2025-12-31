package eval

import "purple_go/pkg/ast"

// EnvLookup looks up a symbol in an environment (association list)
func EnvLookup(env, sym *ast.Value) *ast.Value {
	for !ast.IsNil(env) && ast.IsCell(env) {
		pair := env.Car
		if ast.IsCell(pair) && ast.SymEq(pair.Car, sym) {
			return pair.Cdr
		}
		env = env.Cdr
	}
	return nil
}

// EnvExtend extends an environment with a new binding
func EnvExtend(env, sym, val *ast.Value) *ast.Value {
	return ast.NewCell(ast.NewCell(sym, val), env)
}

// EnvExtendMulti extends an environment with multiple bindings
func EnvExtendMulti(env *ast.Value, syms, vals []*ast.Value) *ast.Value {
	for i := range syms {
		if i < len(vals) {
			env = EnvExtend(env, syms[i], vals[i])
		}
	}
	return env
}

// BuildEnv creates an environment from a list of (name, value) pairs
func BuildEnv(pairs ...*ast.Value) *ast.Value {
	env := ast.Nil
	for i := 0; i < len(pairs)-1; i += 2 {
		env = EnvExtend(env, pairs[i], pairs[i+1])
	}
	return env
}
