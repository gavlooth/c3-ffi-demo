package eval

import (
	"fmt"

	"purple_go/pkg/ast"
)

// Symbol constants
var (
	SymT       = ast.NewSym("t")
	SymQuote   = ast.NewSym("quote")
	SymIf      = ast.NewSym("if")
	SymLambda  = ast.NewSym("lambda")
	SymLet     = ast.NewSym("let")
	SymLetrec  = ast.NewSym("letrec")
	SymAnd     = ast.NewSym("and")
	SymOr      = ast.NewSym("or")
	SymLift    = ast.NewSym("lift")
	SymRun     = ast.NewSym("run")
	SymEM      = ast.NewSym("EM")
	SymScan    = ast.NewSym("scan")
	SymGetMeta = ast.NewSym("get-meta")
	SymSetMeta = ast.NewSym("set-meta!")
)

// getTwoArgs safely extracts two arguments from a list
func getTwoArgs(args *ast.Value) (*ast.Value, *ast.Value, bool) {
	if ast.IsNil(args) || !ast.IsCell(args) {
		return nil, nil, false
	}
	a := args.Car
	rest := args.Cdr
	if ast.IsNil(rest) || !ast.IsCell(rest) {
		return nil, nil, false
	}
	b := rest.Car
	return a, b, true
}

// getOneArg safely extracts one argument from a list
func getOneArg(args *ast.Value) *ast.Value {
	if ast.IsNil(args) || !ast.IsCell(args) {
		return nil
	}
	return args.Car
}

// emitCCall generates a C function call for code values
func emitCCall(fnName string, a, b *ast.Value) *ast.Value {
	aStr := valueToCode(a)
	if b == nil || ast.IsNil(b) {
		return ast.NewCode(fmt.Sprintf("%s(%s)", fnName, aStr))
	}
	bStr := valueToCode(b)
	return ast.NewCode(fmt.Sprintf("%s(%s, %s)", fnName, aStr, bStr))
}

// valueToCode converts a value to its C code representation
func valueToCode(v *ast.Value) string {
	if v == nil {
		return "NULL"
	}
	if ast.IsCode(v) {
		return v.Str
	}
	if ast.IsInt(v) {
		return fmt.Sprintf("mk_int(%d)", v.Int)
	}
	if ast.IsNil(v) {
		return "NULL"
	}
	return v.String()
}

// PrimAdd implements + primitive
func PrimAdd(args, menv *ast.Value) *ast.Value {
	a, b, ok := getTwoArgs(args)
	if !ok {
		return ast.Nil
	}
	if ast.IsCode(a) || ast.IsCode(b) {
		return emitCCall("add", a, b)
	}
	if !ast.IsInt(a) || !ast.IsInt(b) {
		return ast.Nil
	}
	return ast.NewInt(a.Int + b.Int)
}

// PrimSub implements - primitive
func PrimSub(args, menv *ast.Value) *ast.Value {
	a, b, ok := getTwoArgs(args)
	if !ok {
		return ast.Nil
	}
	if ast.IsCode(a) || ast.IsCode(b) {
		return emitCCall("sub", a, b)
	}
	if !ast.IsInt(a) || !ast.IsInt(b) {
		return ast.Nil
	}
	return ast.NewInt(a.Int - b.Int)
}

// PrimMul implements * primitive
func PrimMul(args, menv *ast.Value) *ast.Value {
	a, b, ok := getTwoArgs(args)
	if !ok {
		return ast.Nil
	}
	if ast.IsCode(a) || ast.IsCode(b) {
		return emitCCall("mul", a, b)
	}
	if !ast.IsInt(a) || !ast.IsInt(b) {
		return ast.Nil
	}
	return ast.NewInt(a.Int * b.Int)
}

// PrimDiv implements / primitive
func PrimDiv(args, menv *ast.Value) *ast.Value {
	a, b, ok := getTwoArgs(args)
	if !ok {
		return ast.Nil
	}
	if ast.IsCode(a) || ast.IsCode(b) {
		return emitCCall("div_op", a, b)
	}
	if !ast.IsInt(a) || !ast.IsInt(b) {
		return ast.Nil
	}
	if b.Int == 0 {
		return ast.NewInt(0)
	}
	return ast.NewInt(a.Int / b.Int)
}

// PrimMod implements % primitive
func PrimMod(args, menv *ast.Value) *ast.Value {
	a, b, ok := getTwoArgs(args)
	if !ok {
		return ast.Nil
	}
	if ast.IsCode(a) || ast.IsCode(b) {
		return emitCCall("mod_op", a, b)
	}
	if !ast.IsInt(a) || !ast.IsInt(b) {
		return ast.Nil
	}
	if b.Int == 0 {
		return ast.NewInt(0)
	}
	return ast.NewInt(a.Int % b.Int)
}

// PrimEq implements = primitive
func PrimEq(args, menv *ast.Value) *ast.Value {
	a, b, ok := getTwoArgs(args)
	if !ok {
		return ast.Nil
	}
	if ast.IsCode(a) || ast.IsCode(b) {
		return emitCCall("eq_op", a, b)
	}
	if ast.IsInt(a) && ast.IsInt(b) {
		if a.Int == b.Int {
			return SymT
		}
		return ast.Nil
	}
	if ast.IsSym(a) && ast.IsSym(b) {
		if ast.SymEq(a, b) {
			return SymT
		}
		return ast.Nil
	}
	if ast.IsNil(a) && ast.IsNil(b) {
		return SymT
	}
	return ast.Nil
}

// PrimLt implements < primitive
func PrimLt(args, menv *ast.Value) *ast.Value {
	a, b, ok := getTwoArgs(args)
	if !ok {
		return ast.Nil
	}
	if ast.IsCode(a) || ast.IsCode(b) {
		return emitCCall("lt_op", a, b)
	}
	if !ast.IsInt(a) || !ast.IsInt(b) {
		return ast.Nil
	}
	if a.Int < b.Int {
		return SymT
	}
	return ast.Nil
}

// PrimGt implements > primitive
func PrimGt(args, menv *ast.Value) *ast.Value {
	a, b, ok := getTwoArgs(args)
	if !ok {
		return ast.Nil
	}
	if ast.IsCode(a) || ast.IsCode(b) {
		return emitCCall("gt_op", a, b)
	}
	if !ast.IsInt(a) || !ast.IsInt(b) {
		return ast.Nil
	}
	if a.Int > b.Int {
		return SymT
	}
	return ast.Nil
}

// PrimLe implements <= primitive
func PrimLe(args, menv *ast.Value) *ast.Value {
	a, b, ok := getTwoArgs(args)
	if !ok {
		return ast.Nil
	}
	if ast.IsCode(a) || ast.IsCode(b) {
		return emitCCall("le_op", a, b)
	}
	if !ast.IsInt(a) || !ast.IsInt(b) {
		return ast.Nil
	}
	if a.Int <= b.Int {
		return SymT
	}
	return ast.Nil
}

// PrimGe implements >= primitive
func PrimGe(args, menv *ast.Value) *ast.Value {
	a, b, ok := getTwoArgs(args)
	if !ok {
		return ast.Nil
	}
	if ast.IsCode(a) || ast.IsCode(b) {
		return emitCCall("ge_op", a, b)
	}
	if !ast.IsInt(a) || !ast.IsInt(b) {
		return ast.Nil
	}
	if a.Int >= b.Int {
		return SymT
	}
	return ast.Nil
}

// PrimNot implements not primitive
func PrimNot(args, menv *ast.Value) *ast.Value {
	a := getOneArg(args)
	if a == nil {
		return SymT
	}
	if ast.IsCode(a) {
		return ast.NewCode(fmt.Sprintf("not_op(%s)", a.Str))
	}
	if ast.IsNil(a) {
		return SymT
	}
	return ast.Nil
}

// PrimCons implements cons primitive
func PrimCons(args, menv *ast.Value) *ast.Value {
	a, b, ok := getTwoArgs(args)
	if !ok {
		return ast.Nil
	}
	if ast.IsCode(a) || ast.IsCode(b) {
		return emitCCall("mk_pair", a, b)
	}
	return ast.NewCell(a, b)
}

// PrimCar implements car primitive
func PrimCar(args, menv *ast.Value) *ast.Value {
	a := getOneArg(args)
	if a == nil {
		return ast.Nil
	}
	if ast.IsCode(a) {
		return ast.NewCode(fmt.Sprintf("(%s)->a", a.Str))
	}
	if !ast.IsCell(a) {
		return ast.Nil
	}
	return a.Car
}

// PrimCdr implements cdr primitive
func PrimCdr(args, menv *ast.Value) *ast.Value {
	a := getOneArg(args)
	if a == nil {
		return ast.Nil
	}
	if ast.IsCode(a) {
		return ast.NewCode(fmt.Sprintf("(%s)->b", a.Str))
	}
	if !ast.IsCell(a) {
		return ast.Nil
	}
	return a.Cdr
}

// PrimFst is an alias for car
func PrimFst(args, menv *ast.Value) *ast.Value {
	return PrimCar(args, menv)
}

// PrimSnd is an alias for cdr
func PrimSnd(args, menv *ast.Value) *ast.Value {
	return PrimCdr(args, menv)
}

// PrimNull implements null? primitive
func PrimNull(args, menv *ast.Value) *ast.Value {
	a := getOneArg(args)
	if a == nil {
		return SymT
	}
	if ast.IsCode(a) {
		return ast.NewCode(fmt.Sprintf("is_nil(%s)", a.Str))
	}
	if ast.IsNil(a) {
		return SymT
	}
	return ast.Nil
}

// PrimList implements list primitive
func PrimList(args, menv *ast.Value) *ast.Value {
	return args
}

// PrimPrint implements print primitive
func PrimPrint(args, menv *ast.Value) *ast.Value {
	a := getOneArg(args)
	if a != nil {
		fmt.Println(a.String())
	}
	return ast.Nil
}

// DefaultEnv creates the default environment with primitives
func DefaultEnv() *ast.Value {
	env := ast.Nil
	// Arithmetic
	env = EnvExtend(env, ast.NewSym("+"), ast.NewPrim(PrimAdd))
	env = EnvExtend(env, ast.NewSym("-"), ast.NewPrim(PrimSub))
	env = EnvExtend(env, ast.NewSym("*"), ast.NewPrim(PrimMul))
	env = EnvExtend(env, ast.NewSym("/"), ast.NewPrim(PrimDiv))
	env = EnvExtend(env, ast.NewSym("%"), ast.NewPrim(PrimMod))
	// Comparison
	env = EnvExtend(env, ast.NewSym("="), ast.NewPrim(PrimEq))
	env = EnvExtend(env, ast.NewSym("<"), ast.NewPrim(PrimLt))
	env = EnvExtend(env, ast.NewSym(">"), ast.NewPrim(PrimGt))
	env = EnvExtend(env, ast.NewSym("<="), ast.NewPrim(PrimLe))
	env = EnvExtend(env, ast.NewSym(">="), ast.NewPrim(PrimGe))
	// Logical
	env = EnvExtend(env, ast.NewSym("not"), ast.NewPrim(PrimNot))
	// List operations
	env = EnvExtend(env, ast.NewSym("cons"), ast.NewPrim(PrimCons))
	env = EnvExtend(env, ast.NewSym("car"), ast.NewPrim(PrimCar))
	env = EnvExtend(env, ast.NewSym("cdr"), ast.NewPrim(PrimCdr))
	env = EnvExtend(env, ast.NewSym("fst"), ast.NewPrim(PrimFst))
	env = EnvExtend(env, ast.NewSym("snd"), ast.NewPrim(PrimSnd))
	env = EnvExtend(env, ast.NewSym("null?"), ast.NewPrim(PrimNull))
	env = EnvExtend(env, ast.NewSym("list"), ast.NewPrim(PrimList))
	// Utility
	env = EnvExtend(env, ast.NewSym("print"), ast.NewPrim(PrimPrint))
	// Constants
	env = EnvExtend(env, ast.NewSym("t"), SymT)
	env = EnvExtend(env, ast.NewSym("nil"), ast.Nil)
	return env
}
