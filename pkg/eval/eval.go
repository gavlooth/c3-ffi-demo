package eval

import (
	"fmt"

	"purple_go/pkg/ast"
)

// Evaluator is the stage-polymorphic evaluator
type Evaluator struct {
	// CodeGen is set when we need code generation support
	CodeGen CodeGenerator
}

// CodeGenerator interface for code generation during evaluation
type CodeGenerator interface {
	LiftValue(v *ast.Value) *ast.Value
	ValueToCExpr(v *ast.Value) string
}

// DefaultCodeGen provides basic code generation
type DefaultCodeGen struct{}

func (d *DefaultCodeGen) LiftValue(v *ast.Value) *ast.Value {
	if v == nil || ast.IsNil(v) {
		return ast.NewCode("NULL")
	}
	switch v.Tag {
	case ast.TInt:
		return ast.NewCode(fmt.Sprintf("mk_int(%d)", v.Int))
	case ast.TSym:
		return ast.NewCode(fmt.Sprintf("mk_sym(\"%s\")", v.Str))
	case ast.TCode:
		return v
	case ast.TCell:
		carCode := d.LiftValue(v.Car)
		cdrCode := d.LiftValue(v.Cdr)
		return ast.NewCode(fmt.Sprintf("mk_pair(%s, %s)", carCode.Str, cdrCode.Str))
	default:
		return ast.NewCode("NULL")
	}
}

func (d *DefaultCodeGen) ValueToCExpr(v *ast.Value) string {
	if v == nil || ast.IsNil(v) {
		return "NULL"
	}
	if ast.IsCode(v) {
		return v.Str
	}
	if ast.IsInt(v) {
		return fmt.Sprintf("mk_int(%d)", v.Int)
	}
	return "NULL"
}

// New creates a new evaluator with default code generator
func New() *Evaluator {
	return &Evaluator{
		CodeGen: &DefaultCodeGen{},
	}
}

// NewMenv creates a new meta-environment
func NewMenv(parent, env *ast.Value) *ast.Value {
	return ast.NewMenv(env, parent,
		defaultHApp,
		defaultHLet,
		defaultHIf,
		defaultHLit,
		defaultHVar,
	)
}

// Default handlers
func defaultHLit(exp, menv *ast.Value) *ast.Value {
	return exp
}

func defaultHVar(exp, menv *ast.Value) *ast.Value {
	v := EnvLookup(menv.Env, exp)
	if v == nil {
		fmt.Printf("Error: Unbound variable: %s\n", exp.Str)
		return ast.Nil
	}
	return v
}

func defaultHApp(exp, menv *ast.Value) *ast.Value {
	fExpr := exp.Car
	argsExpr := exp.Cdr

	fn := Eval(fExpr, menv)
	if fn == nil {
		return ast.Nil
	}

	args := evalList(argsExpr, menv)

	if ast.IsPrim(fn) {
		return fn.Prim(args, menv)
	}

	if ast.IsLambda(fn) {
		params := fn.Params
		body := fn.Body
		closureEnv := fn.LamEnv

		newEnv := closureEnv
		p := params
		a := args
		for !ast.IsNil(p) && !ast.IsNil(a) && ast.IsCell(p) && ast.IsCell(a) {
			newEnv = EnvExtend(newEnv, p.Car, a.Car)
			p = p.Cdr
			a = a.Cdr
		}

		bodyMenv := NewMenv(menv.Parent, newEnv)
		bodyMenv.HApp = menv.HApp
		bodyMenv.HLet = menv.HLet
		bodyMenv.HIf = menv.HIf
		bodyMenv.HLit = menv.HLit
		bodyMenv.HVar = menv.HVar

		return Eval(body, bodyMenv)
	}

	fmt.Printf("Error: Not a function: %s\n", fn.String())
	return ast.Nil
}

func defaultHLet(exp, menv *ast.Value) *ast.Value {
	args := exp.Cdr
	bindings := args.Car
	body := args.Cdr.Car

	anyCode := false
	newEnv := menv.Env

	// Collect bindings
	var bindList []bindInfo

	b := bindings
	for !ast.IsNil(b) && ast.IsCell(b) {
		bind := b.Car
		sym := bind.Car
		valExpr := bind.Cdr.Car
		val := Eval(valExpr, menv)
		if val == nil {
			val = ast.Nil
		}
		if ast.IsCode(val) {
			anyCode = true
		}
		bindList = append(bindList, bindInfo{sym: sym, val: val})
		b = b.Cdr
	}

	if anyCode {
		// Code generation path - generate C code block
		return generateLetCode(bindList, body, menv)
	}

	// Interpretation path
	for _, bi := range bindList {
		newEnv = EnvExtend(newEnv, bi.sym, bi.val)
	}

	bodyMenv := NewMenv(menv.Parent, newEnv)
	bodyMenv.HApp = menv.HApp
	bodyMenv.HLet = menv.HLet
	bodyMenv.HIf = menv.HIf
	bodyMenv.HLit = menv.HLit
	bodyMenv.HVar = menv.HVar

	return Eval(body, bodyMenv)
}

func generateLetCode(bindings []bindInfo, body *ast.Value, menv *ast.Value) *ast.Value {
	// Simple code generation for let bindings
	var decls, frees string
	newEnv := menv.Env

	cg := &DefaultCodeGen{}

	for _, bi := range bindings {
		valStr := ""
		if ast.IsCode(bi.val) {
			valStr = bi.val.Str
		} else {
			valStr = cg.ValueToCExpr(bi.val)
		}
		decls += fmt.Sprintf("  Obj* %s = %s;\n", bi.sym.Str, valStr)
		frees = fmt.Sprintf("  free_obj(%s);\n", bi.sym.Str) + frees

		ref := ast.NewCode(bi.sym.Str)
		newEnv = EnvExtend(newEnv, bi.sym, ref)
	}

	bodyMenv := NewMenv(menv.Parent, newEnv)
	bodyMenv.HApp = menv.HApp
	bodyMenv.HLet = menv.HLet
	bodyMenv.HIf = menv.HIf
	bodyMenv.HLit = menv.HLit
	bodyMenv.HVar = menv.HVar

	res := Eval(body, bodyMenv)
	resStr := ""
	if ast.IsCode(res) {
		resStr = res.Str
	} else {
		resStr = cg.ValueToCExpr(res)
	}

	code := fmt.Sprintf("({\n%s  Obj* _res = %s;\n%s  _res;\n})", decls, resStr, frees)
	return ast.NewCode(code)
}

func defaultHIf(exp, menv *ast.Value) *ast.Value {
	args := exp.Cdr
	condExpr := args.Car
	thenExpr := args.Cdr.Car
	var elseExpr *ast.Value
	if !ast.IsNil(args.Cdr.Cdr) && ast.IsCell(args.Cdr.Cdr) {
		elseExpr = args.Cdr.Cdr.Car
	} else {
		elseExpr = ast.Nil
	}

	c := Eval(condExpr, menv)

	if ast.IsCode(c) {
		t := Eval(thenExpr, menv)
		e := Eval(elseExpr, menv)

		tStr := ""
		if ast.IsCode(t) {
			tStr = t.Str
		} else {
			tStr = (&DefaultCodeGen{}).ValueToCExpr(t)
		}

		eStr := ""
		if ast.IsCode(e) {
			eStr = e.Str
		} else {
			eStr = (&DefaultCodeGen{}).ValueToCExpr(e)
		}

		return ast.NewCode(fmt.Sprintf("((%s)->i ? (%s) : (%s))", c.Str, tStr, eStr))
	}

	if !ast.IsNil(c) {
		return Eval(thenExpr, menv)
	}
	return Eval(elseExpr, menv)
}

// bindInfo holds a binding for let expressions
type bindInfo struct {
	sym *ast.Value
	val *ast.Value
}

// evalList evaluates a list of expressions
func evalList(list, menv *ast.Value) *ast.Value {
	if ast.IsNil(list) {
		return ast.Nil
	}
	h := Eval(list.Car, menv)
	t := evalList(list.Cdr, menv)
	return ast.NewCell(h, t)
}

// Eval is the main evaluation function
func Eval(expr, menv *ast.Value) *ast.Value {
	if ast.IsNil(expr) {
		return ast.Nil
	}
	if menv == nil {
		return ast.Nil
	}

	switch expr.Tag {
	case ast.TInt:
		return menv.HLit(expr, menv)

	case ast.TCode:
		return expr

	case ast.TSym:
		return menv.HVar(expr, menv)

	case ast.TCell:
		op := expr.Car
		args := expr.Cdr

		// Special forms
		if ast.SymEqStr(op, "quote") {
			return args.Car
		}

		if ast.SymEqStr(op, "lift") {
			v := Eval(args.Car, menv)
			return (&DefaultCodeGen{}).LiftValue(v)
		}

		if ast.SymEqStr(op, "if") {
			return menv.HIf(expr, menv)
		}

		if ast.SymEqStr(op, "let") {
			return menv.HLet(expr, menv)
		}

		if ast.SymEqStr(op, "letrec") {
			return evalLetrec(expr, menv)
		}

		if ast.SymEqStr(op, "and") {
			return evalAnd(args, menv)
		}

		if ast.SymEqStr(op, "or") {
			return evalOr(args, menv)
		}

		if ast.SymEqStr(op, "lambda") {
			params := args.Car
			body := args.Cdr.Car
			return ast.NewLambda(params, body, menv.Env)
		}

		if ast.SymEqStr(op, "EM") {
			// Escape to meta-level
			e := args.Car
			parent := menv.Parent
			if ast.IsNil(parent) {
				parent = NewMenv(ast.Nil, ast.Nil)
				menv.Parent = parent
			}
			return Eval(e, parent)
		}

		if ast.SymEqStr(op, "scan") {
			typeSym := Eval(args.Car, menv)
			val := Eval(args.Cdr.Car, menv)
			if !ast.IsSym(typeSym) || val == nil {
				return ast.Nil
			}
			valStr := ""
			if ast.IsCode(val) {
				valStr = val.Str
			} else {
				valStr = val.String()
			}
			return ast.NewCode(fmt.Sprintf("scan_%s(%s); // ASAP Mark", typeSym.Str, valStr))
		}

		// Regular application
		return menv.HApp(expr, menv)
	}

	return ast.Nil
}

func evalLetrec(exp, menv *ast.Value) *ast.Value {
	args := exp.Cdr
	bindings := args.Car
	body := args.Cdr.Car

	// Placeholder for uninitialized bindings
	uninit := ast.NewPrim(nil)

	// First pass: extend env with placeholders
	newEnv := menv.Env
	b := bindings
	for !ast.IsNil(b) && ast.IsCell(b) {
		bind := b.Car
		sym := bind.Car
		newEnv = EnvExtend(newEnv, sym, uninit)
		b = b.Cdr
	}

	// Create new menv
	recMenv := NewMenv(menv.Parent, newEnv)
	recMenv.HApp = menv.HApp
	recMenv.HLet = menv.HLet
	recMenv.HIf = menv.HIf
	recMenv.HLit = menv.HLit
	recMenv.HVar = menv.HVar

	// Second pass: evaluate and update
	b = bindings
	for !ast.IsNil(b) && ast.IsCell(b) {
		bind := b.Car
		sym := bind.Car
		valExpr := bind.Cdr.Car
		val := Eval(valExpr, recMenv)

		// Update placeholder
		e := newEnv
		for !ast.IsNil(e) && ast.IsCell(e) {
			pair := e.Car
			if ast.SymEq(pair.Car, sym) {
				pair.Cdr = val
				break
			}
			e = e.Cdr
		}
		b = b.Cdr
	}

	return Eval(body, recMenv)
}

func evalAnd(args, menv *ast.Value) *ast.Value {
	result := SymT
	rest := args
	for !ast.IsNil(rest) && ast.IsCell(rest) {
		result = Eval(rest.Car, menv)
		if ast.IsCode(result) {
			// Code level - generate && chain
			remaining := rest.Cdr
			for !ast.IsNil(remaining) && ast.IsCell(remaining) {
				next := Eval(remaining.Car, menv)
				nextStr := ""
				if ast.IsCode(next) {
					nextStr = next.Str
				} else {
					nextStr = next.String()
				}
				result = ast.NewCode(fmt.Sprintf("(%s && %s)", result.Str, nextStr))
				remaining = remaining.Cdr
			}
			return result
		}
		if ast.IsNil(result) {
			return ast.Nil
		}
		rest = rest.Cdr
	}
	return result
}

func evalOr(args, menv *ast.Value) *ast.Value {
	rest := args
	for !ast.IsNil(rest) && ast.IsCell(rest) {
		result := Eval(rest.Car, menv)
		if ast.IsCode(result) {
			// Code level - generate || chain
			remaining := rest.Cdr
			for !ast.IsNil(remaining) && ast.IsCell(remaining) {
				next := Eval(remaining.Car, menv)
				nextStr := ""
				if ast.IsCode(next) {
					nextStr = next.Str
				} else {
					nextStr = next.String()
				}
				result = ast.NewCode(fmt.Sprintf("(%s || %s)", result.Str, nextStr))
				remaining = remaining.Cdr
			}
			return result
		}
		if !ast.IsNil(result) {
			return result
		}
		rest = rest.Cdr
	}
	return ast.Nil
}

// EvalString evaluates a string expression
func EvalString(input string, env *ast.Value) (*ast.Value, error) {
	// Import parser inline to avoid cycle
	// This is a simple integration point
	return nil, fmt.Errorf("use parser.ParseString and Eval separately")
}

// Run evaluates an expression with a default environment
func Run(expr *ast.Value) *ast.Value {
	env := DefaultEnv()
	menv := NewMenv(ast.Nil, env)
	return Eval(expr, menv)
}
