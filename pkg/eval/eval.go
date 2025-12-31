package eval

import (
	"fmt"
	"sync"

	"purple_go/pkg/ast"
	"purple_go/pkg/codegen"
	"purple_go/pkg/jit"
)

// Macro System
// ============

// Macro represents a defined macro (transformer function)
type Macro struct {
	Name        string     // Macro name
	Params      []string   // Parameter names
	Body        *ast.Value // Transformer body (AST)
	Transformer *ast.Value // Compiled transformer (lambda)
}

// Global macro table
var (
	macroTable = make(map[string]*Macro)
	macroMutex sync.RWMutex
)

// DefineMacro stores a macro definition
func DefineMacro(name string, params []string, body *ast.Value, menv *ast.Value) *Macro {
	macroMutex.Lock()
	defer macroMutex.Unlock()

	// Build transformer: nest lambdas for each parameter
	// (defmacro name (a b) body) -> (lambda (a) (lambda (b) body))
	transformer := body
	for i := len(params) - 1; i >= 0; i-- {
		paramList := ast.List1(ast.NewSym(params[i]))
		transformer = ast.NewLambda(paramList, transformer, menv.Env)
	}

	macro := &Macro{
		Name:        name,
		Params:      params,
		Body:        body,
		Transformer: transformer,
	}
	macroTable[name] = macro
	return macro
}

// GetMacro retrieves a macro by name
func GetMacro(name string) *Macro {
	macroMutex.RLock()
	defer macroMutex.RUnlock()
	return macroTable[name]
}

// ClearMacros clears all macros (for testing)
func ClearMacros() {
	macroMutex.Lock()
	defer macroMutex.Unlock()
	macroTable = make(map[string]*Macro)
}

// ExpandMacro expands a macro call without evaluating the result
// Arguments are passed as unevaluated ASTs (not wrapped in quote)
func ExpandMacro(macro *Macro, args []*ast.Value, menv *ast.Value) *ast.Value {
	result := macro.Transformer

	for _, arg := range args {
		// Apply transformer to arg directly (not quoted)
		// The arg is already the unevaluated AST
		if ast.IsLambda(result) {
			// Extend environment with the parameter bound to the arg AST
			newEnv := EnvExtend(result.LamEnv, result.Params.Car, arg)
			// Create new lambda with remaining body, or evaluate body if no more params
			if ast.IsLambda(result.Body) {
				result = ast.NewLambda(result.Body.Params, result.Body.Body, newEnv)
			} else {
				// Last parameter - evaluate body in extended env
				bodyMenv := ast.NewMenv(newEnv, menv.Parent, menv.Level, menv.CopyHandlers())
				result = Eval(result.Body, bodyMenv)
			}
		}
	}

	return result
}

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
	case ast.TFloat:
		return ast.NewCode(fmt.Sprintf("mk_float(%g)", v.Float))
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
	if ast.IsFloat(v) {
		return fmt.Sprintf("mk_float(%g)", v.Float)
	}
	return "NULL"
}

// New creates a new evaluator with default code generator
func New() *Evaluator {
	return &Evaluator{
		CodeGen: &DefaultCodeGen{},
	}
}

// DefaultHandlers is the global default handler table
var DefaultHandlers [9]*ast.HandlerWrapper

// init initializes the default handlers
func init() {
	DefaultHandlers[ast.HIdxLit] = ast.WrapNativeHandler(defaultHLit)
	DefaultHandlers[ast.HIdxVar] = ast.WrapNativeHandler(defaultHVar)
	DefaultHandlers[ast.HIdxLam] = ast.WrapNativeHandler(defaultHLam)
	DefaultHandlers[ast.HIdxApp] = ast.WrapNativeHandler(defaultHApp)
	DefaultHandlers[ast.HIdxIf] = ast.WrapNativeHandler(defaultHIf)
	DefaultHandlers[ast.HIdxLft] = ast.WrapNativeHandler(defaultHLft)
	DefaultHandlers[ast.HIdxRun] = ast.WrapNativeHandler(defaultHRun)
	DefaultHandlers[ast.HIdxEM] = ast.WrapNativeHandler(defaultHEM)
	DefaultHandlers[ast.HIdxClam] = ast.WrapNativeHandler(defaultHClam)
}

// NewMenv creates a new meta-environment with default handlers
func NewMenv(parent, env *ast.Value) *ast.Value {
	level := 0
	if parent != nil && ast.IsMenv(parent) {
		level = parent.Level
	}
	var handlers [9]*ast.HandlerWrapper
	copy(handlers[:], DefaultHandlers[:])
	return ast.NewMenv(env, parent, level, handlers)
}

// NewMenvAtLevel creates a menv at a specific level
func NewMenvAtLevel(parent, env *ast.Value, level int) *ast.Value {
	var handlers [9]*ast.HandlerWrapper
	copy(handlers[:], DefaultHandlers[:])
	return ast.NewMenv(env, parent, level, handlers)
}

// EnsureParent ensures parent menv exists (lazy creation)
func EnsureParent(menv *ast.Value) *ast.Value {
	if menv == nil || !ast.IsMenv(menv) {
		return NewMenvAtLevel(ast.Nil, ast.Nil, 1)
	}
	if ast.IsNil(menv.Parent) || menv.Parent == nil {
		// Create parent at level + 1 with empty env and default handlers
		parent := NewMenvAtLevel(ast.Nil, ast.Nil, menv.Level+1)
		menv.Parent = parent
		return parent
	}
	return menv.Parent
}

// CallHandler calls a handler (native or closure)
func CallHandler(menv *ast.Value, idx int, arg *ast.Value) *ast.Value {
	if menv == nil || !ast.IsMenv(menv) {
		return ast.Nil
	}
	handler := menv.Handlers[idx]
	if handler == nil {
		handler = DefaultHandlers[idx]
	}
	if handler == nil {
		return ast.Nil
	}

	if handler.Native != nil {
		// Native handler: call directly
		return handler.Native(arg, menv)
	}

	if handler.Closure != nil && ast.IsLambda(handler.Closure) {
		// Closure handler: evaluate body with arg bound
		// IMPORTANT: Use DEFAULT handlers to prevent infinite recursion
		closure := handler.Closure
		newEnv := EnvExtend(closure.LamEnv, closure.Params.Car, arg)
		var defaultHandlersCopy [9]*ast.HandlerWrapper
		copy(defaultHandlersCopy[:], DefaultHandlers[:])
		bodyMenv := ast.NewMenv(newEnv, menv.Parent, menv.Level, defaultHandlersCopy)
		return Eval(closure.Body, bodyMenv)
	}

	return ast.Nil
}

// CallDefaultHandler calls the default handler by name
func CallDefaultHandler(menv *ast.Value, name string, arg *ast.Value) *ast.Value {
	idx, ok := ast.HandlerNames[name]
	if !ok {
		return ast.NewError(fmt.Sprintf("unknown handler: %s", name))
	}
	handler := DefaultHandlers[idx]
	if handler == nil || handler.Native == nil {
		return ast.NewError(fmt.Sprintf("no default handler for: %s", name))
	}
	return handler.Native(arg, menv)
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

func defaultHLam(exp, menv *ast.Value) *ast.Value {
	// exp is (lambda params body) or (lambda self (params) body)
	args := exp.Cdr
	params := args.Car
	body := args.Cdr.Car

	// Check for recursive lambda: (lambda self (params) body)
	// where self is a symbol and (params) is a list
	if ast.IsSym(params) && !ast.IsNil(args.Cdr) && ast.IsCell(args.Cdr) {
		selfName := params
		actualParams := args.Cdr.Car
		actualBody := args.Cdr.Cdr.Car
		return ast.NewRecLambda(selfName, actualParams, actualBody, menv.Env)
	}

	return ast.NewLambda(params, body, menv.Env)
}

func defaultHLft(exp, menv *ast.Value) *ast.Value {
	// exp is the value to lift
	return (&DefaultCodeGen{}).LiftValue(exp)
}

func defaultHRun(exp, menv *ast.Value) *ast.Value {
	// exp is the code to run
	if ast.IsCode(exp) {
		// Try to execute C code via JIT if available
		j := jit.Get()
		if j.IsAvailable() {
			return executeCodeWithJIT(exp.Str, j)
		}
		// JIT not available - return the code as-is
		return exp
	}
	// If it's an AST, evaluate it at base level
	baseMenv := NewMenvAtLevel(ast.Nil, menv.Env, 0)
	return Eval(exp, baseMenv)
}

// executeCodeWithJIT compiles and runs C code expression using JIT
func executeCodeWithJIT(codeExpr string, j *jit.JIT) *ast.Value {
	// Generate a complete C program with the Purple runtime
	runtime := generateMinimalRuntime()
	fullCode := jit.WrapCode(codeExpr, runtime)

	// Compile
	compiled, err := j.Compile(fullCode)
	if err != nil {
		return ast.NewError(fmt.Sprintf("JIT compile error: %v", err))
	}
	defer compiled.Close()

	// Run and get result
	result := compiled.Run()
	if !result.Success {
		return ast.NewError(fmt.Sprintf("JIT run error: %s", result.Error))
	}

	return ast.NewInt(result.IntValue)
}

// generateMinimalRuntime generates a minimal C runtime for JIT execution
func generateMinimalRuntime() string {
	return `
#include <stdlib.h>
#include <string.h>

typedef struct Obj {
    int is_pair;
    union {
        long i;
        struct { struct Obj* car; struct Obj* cdr; };
    };
} Obj;

static Obj* mk_int(long n) {
    Obj* o = (Obj*)malloc(sizeof(Obj));
    o->is_pair = 0;
    o->i = n;
    return o;
}

static Obj* mk_pair(Obj* car, Obj* cdr) {
    Obj* o = (Obj*)malloc(sizeof(Obj));
    o->is_pair = 1;
    o->car = car;
    o->cdr = cdr;
    return o;
}

static void free_obj(Obj* o) {
    if (o) free(o);
}
`
}

func defaultHEM(exp, menv *ast.Value) *ast.Value {
	// exp is the expression to evaluate at parent level
	parent := EnsureParent(menv)
	return Eval(exp, parent)
}

func defaultHClam(exp, menv *ast.Value) *ast.Value {
	// exp is (clambda params body) - compile lambda under current semantics
	args := exp.Cdr
	params := args.Car
	body := args.Cdr.Car

	// Create a lambda AST
	lamAST := ast.List3(ast.NewSym("lambda"), params, body)

	// Compile it (evaluate with lift semantics)
	compiled := evalCompile(lamAST, menv)

	if ast.IsCode(compiled) {
		// Evaluate the compiled code
		return Eval(compiled, menv)
	}
	return compiled
}

// evalCompile evaluates an expression in compile mode (lifting values to code)
func evalCompile(expr, menv *ast.Value) *ast.Value {
	// For now, just lift the result
	result := Eval(expr, menv)
	if !ast.IsCode(result) {
		return (&DefaultCodeGen{}).LiftValue(result)
	}
	return result
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

		// Create body menv preserving handlers from current menv
		bodyMenv := ast.NewMenv(newEnv, menv.Parent, menv.Level, menv.CopyHandlers())
		return Eval(body, bodyMenv)
	}

	if ast.IsRecLambda(fn) {
		selfName := fn.SelfName
		params := fn.Params
		body := fn.Body
		closureEnv := fn.LamEnv

		// Extend environment with self-reference
		newEnv := EnvExtend(closureEnv, selfName, fn)

		p := params
		a := args
		for !ast.IsNil(p) && !ast.IsNil(a) && ast.IsCell(p) && ast.IsCell(a) {
			newEnv = EnvExtend(newEnv, p.Car, a.Car)
			p = p.Cdr
			a = a.Cdr
		}

		// Create body menv preserving handlers from current menv
		bodyMenv := ast.NewMenv(newEnv, menv.Parent, menv.Level, menv.CopyHandlers())
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

	// Create body menv preserving handlers
	bodyMenv := ast.NewMenv(newEnv, menv.Parent, menv.Level, menv.CopyHandlers())
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

	// Create body menv preserving handlers
	bodyMenv := ast.NewMenv(newEnv, menv.Parent, menv.Level, menv.CopyHandlers())

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
		return CallHandler(menv, ast.HIdxLit, expr)

	case ast.TFloat:
		return CallHandler(menv, ast.HIdxLit, expr)

	case ast.TChar:
		return CallHandler(menv, ast.HIdxLit, expr)

	case ast.TCode:
		return expr

	case ast.TSym:
		return CallHandler(menv, ast.HIdxVar, expr)

	case ast.TCell:
		op := expr.Car
		args := expr.Cdr

		// Special forms
		if ast.SymEqStr(op, "quote") {
			return args.Car
		}

		if ast.SymEqStr(op, "lift") {
			v := Eval(args.Car, menv)
			return CallHandler(menv, ast.HIdxLft, v)
		}

		if ast.SymEqStr(op, "if") {
			return CallHandler(menv, ast.HIdxIf, expr)
		}

		if ast.SymEqStr(op, "let") {
			return defaultHLet(expr, menv)
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
			return CallHandler(menv, ast.HIdxLam, expr)
		}

		if ast.SymEqStr(op, "match") {
			return EvalMatch(expr, menv)
		}

		if ast.SymEqStr(op, "do") {
			return evalDo(args, menv)
		}

		if ast.SymEqStr(op, "error") {
			msg := Eval(args.Car, menv)
			msgStr := "error"
			if ast.IsSym(msg) {
				msgStr = msg.Str
			} else if ast.IsInt(msg) {
				msgStr = fmt.Sprintf("%d", msg.Int)
			}
			return ast.NewError(msgStr)
		}

		if ast.SymEqStr(op, "try") {
			// (try expr handler) - handler is a lambda that takes error
			tryExpr := args.Car
			handler := Eval(args.Cdr.Car, menv)
			result := Eval(tryExpr, menv)
			if ast.IsError(result) {
				// Apply handler to error message
				errVal := ast.NewSym(result.Str)
				return applyFn(handler, ast.List1(errVal), menv)
			}
			return result
		}

		if ast.SymEqStr(op, "assert") {
			cond := Eval(args.Car, menv)
			if ast.IsNil(cond) || (ast.IsInt(cond) && cond.Int == 0) {
				msg := "assertion failed"
				if !ast.IsNil(args.Cdr) {
					msgVal := Eval(args.Cdr.Car, menv)
					if ast.IsSym(msgVal) {
						msg = msgVal.Str
					}
				}
				return ast.NewError(msg)
			}
			return SymT
		}

		// EM - Escape to Meta-level
		if ast.SymEqStr(op, "EM") {
			e := args.Car
			return CallHandler(menv, ast.HIdxEM, e)
		}

		// run - Execute code at base level
		if ast.SymEqStr(op, "run") {
			codeVal := Eval(args.Car, menv)
			return CallHandler(menv, ast.HIdxRun, codeVal)
		}

		// clambda - Compile lambda under current semantics
		if ast.SymEqStr(op, "clambda") {
			return CallHandler(menv, ast.HIdxClam, expr)
		}

		// shift - Go up n levels and evaluate
		if ast.SymEqStr(op, "shift") {
			nVal := Eval(args.Car, menv)
			e := args.Cdr.Car
			n := int64(0)
			if ast.IsInt(nVal) {
				n = nVal.Int
			}
			return evalShift(int(n), e, menv)
		}

		// meta-level - Get current tower level
		if ast.SymEqStr(op, "meta-level") {
			return ast.NewInt(int64(menv.Level))
		}

		// get-meta - Get handler by name
		if ast.SymEqStr(op, "get-meta") {
			key := Eval(args.Car, menv)
			if !ast.IsSym(key) {
				return ast.NewError("get-meta: key must be a symbol")
			}
			idx, ok := ast.HandlerNames[key.Str]
			if !ok {
				return ast.NewError(fmt.Sprintf("get-meta: unknown handler: %s", key.Str))
			}
			handler := menv.GetHandler(idx)
			if handler == nil {
				return ast.Nil
			}
			// Return the closure if it's a closure handler, otherwise return a marker
			if handler.Closure != nil {
				return handler.Closure
			}
			return ast.NewSym(fmt.Sprintf("#<native-handler:%s>", key.Str))
		}

		// set-meta! - Install custom handler (returns new menv)
		if ast.SymEqStr(op, "set-meta!") {
			key := Eval(args.Car, menv)
			val := Eval(args.Cdr.Car, menv)
			if !ast.IsSym(key) {
				return ast.NewError("set-meta!: key must be a symbol")
			}
			idx, ok := ast.HandlerNames[key.Str]
			if !ok {
				return ast.NewError(fmt.Sprintf("set-meta!: unknown handler: %s", key.Str))
			}
			// val must be a lambda
			if !ast.IsLambda(val) && !ast.IsRecLambda(val) {
				return ast.NewError("set-meta!: handler must be a lambda")
			}
			wrapper := ast.WrapClosureHandler(val)
			return menv.SetHandler(idx, wrapper)
		}

		// with-menv - Evaluate body with custom meta-environment
		if ast.SymEqStr(op, "with-menv") {
			menvExpr := args.Car
			bodyExpr := args.Cdr.Car
			newMenv := Eval(menvExpr, menv)
			if !ast.IsMenv(newMenv) {
				return ast.NewError("with-menv: first argument must evaluate to menv")
			}
			return Eval(bodyExpr, newMenv)
		}

		// with-handlers - Scoped handler changes
		// (with-handlers ((name handler) ...) body)
		if ast.SymEqStr(op, "with-handlers") {
			handlerList := args.Car
			bodyExpr := args.Cdr.Car
			return evalWithHandlers(handlerList, bodyExpr, menv)
		}

		// default-handler - Delegate to default handler
		if ast.SymEqStr(op, "default-handler") {
			name := Eval(args.Car, menv)
			arg := Eval(args.Cdr.Car, menv)
			if !ast.IsSym(name) {
				return ast.NewError("default-handler: name must be a symbol")
			}
			return CallDefaultHandler(menv, name.Str, arg)
		}

		if ast.SymEqStr(op, "eval") {
			// (eval expr) - evaluate expression at runtime
			exprVal := Eval(args.Car, menv)
			return Eval(exprVal, menv)
		}

		if ast.SymEqStr(op, "quasiquote") {
			return evalQuasiquote(args.Car, menv)
		}

		if ast.SymEqStr(op, "sym-eq?") {
			a := Eval(args.Car, menv)
			b := Eval(args.Cdr.Car, menv)
			if ast.IsSym(a) && ast.IsSym(b) && a.Str == b.Str {
				return SymT
			}
			return ast.Nil
		}

		if ast.SymEqStr(op, "gensym") {
			// (gensym) or (gensym prefix)
			prefix := "g"
			if !ast.IsNil(args) {
				prefixVal := Eval(args.Car, menv)
				if ast.IsSym(prefixVal) {
					prefix = prefixVal.Str
				}
			}
			return ast.NewSym(fmt.Sprintf("%s%d", prefix, gensymCounter()))
		}

		if ast.SymEqStr(op, "trace") {
			// (trace expr) - evaluate and print result
			result := Eval(args.Car, menv)
			fmt.Printf("TRACE: %s\n", result.String())
			return result
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

		// FFI: (ffi "function_name" arg1 arg2 ...)
		// Generates C code that calls an external function
		if ast.SymEqStr(op, "ffi") {
			return evalFFI(args, menv)
		}

		// FFI declaration: (ffi-declare "ret_type" "func_name" "arg1_type" ...)
		// Registers an external function declaration
		if ast.SymEqStr(op, "ffi-declare") {
			return evalFFIDeclare(args, menv)
		}

		// Macro System
		// ============

		// defmacro - Define a macro
		// (defmacro name (params...) body scope)
		if ast.SymEqStr(op, "defmacro") {
			return evalDefmacro(args, menv)
		}

		// mcall - Call a macro
		// (mcall macro-name arg1 arg2 ...)
		if ast.SymEqStr(op, "mcall") {
			return evalMcall(args, menv)
		}

		// macroexpand - Expand macro without evaluating
		// (macroexpand (mcall macro-name arg1 arg2 ...))
		if ast.SymEqStr(op, "macroexpand") {
			return evalMacroexpand(args, menv)
		}

		// deftype - Define a type for back-edge analysis
		// (deftype TypeName (field1 Type1) (field2 Type2) ...)
		if ast.SymEqStr(op, "deftype") {
			return evalDeftype(args, menv)
		}

		// Regular application - use app handler
		return CallHandler(menv, ast.HIdxApp, expr)
	}

	return ast.Nil
}

// evalShift evaluates expression at n levels up the tower
func evalShift(n int, expr, menv *ast.Value) *ast.Value {
	if n <= 0 {
		return Eval(expr, menv)
	}
	// Go up n levels
	targetMenv := menv
	for i := 0; i < n; i++ {
		targetMenv = EnsureParent(targetMenv)
	}
	return Eval(expr, targetMenv)
}

// evalWithHandlers evaluates body with scoped handler changes
func evalWithHandlers(handlerList, body, menv *ast.Value) *ast.Value {
	// Start with current menv
	newMenv := menv

	// Process each handler binding
	for !ast.IsNil(handlerList) && ast.IsCell(handlerList) {
		binding := handlerList.Car
		if !ast.IsCell(binding) {
			return ast.NewError("with-handlers: invalid binding")
		}

		name := binding.Car
		handlerExpr := binding.Cdr.Car

		if !ast.IsSym(name) {
			return ast.NewError("with-handlers: handler name must be a symbol")
		}

		idx, ok := ast.HandlerNames[name.Str]
		if !ok {
			return ast.NewError(fmt.Sprintf("with-handlers: unknown handler: %s", name.Str))
		}

		// Evaluate handler expression
		handler := Eval(handlerExpr, menv)
		if !ast.IsLambda(handler) && !ast.IsRecLambda(handler) {
			return ast.NewError("with-handlers: handler must be a lambda")
		}

		wrapper := ast.WrapClosureHandler(handler)
		newMenv = newMenv.SetHandler(idx, wrapper)

		handlerList = handlerList.Cdr
	}

	// Evaluate body with new handlers
	return Eval(body, newMenv)
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

	// Create new menv preserving handlers
	recMenv := ast.NewMenv(newEnv, menv.Parent, menv.Level, menv.CopyHandlers())

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

// evalDo evaluates a sequence of expressions, returning the last
func evalDo(args, menv *ast.Value) *ast.Value {
	var result *ast.Value = ast.Nil
	rest := args
	for !ast.IsNil(rest) && ast.IsCell(rest) {
		result = Eval(rest.Car, menv)
		rest = rest.Cdr
	}
	return result
}

// applyFn applies a function to arguments
func applyFn(fn *ast.Value, args *ast.Value, menv *ast.Value) *ast.Value {
	if fn == nil {
		return ast.Nil
	}

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

		// Create body menv preserving handlers
		bodyMenv := ast.NewMenv(newEnv, menv.Parent, menv.Level, menv.CopyHandlers())
		return Eval(body, bodyMenv)
	}

	if ast.IsRecLambda(fn) {
		selfName := fn.SelfName
		params := fn.Params
		body := fn.Body
		closureEnv := fn.LamEnv

		// Extend environment with self-reference
		newEnv := EnvExtend(closureEnv, selfName, fn)

		p := params
		a := args
		for !ast.IsNil(p) && !ast.IsNil(a) && ast.IsCell(p) && ast.IsCell(a) {
			newEnv = EnvExtend(newEnv, p.Car, a.Car)
			p = p.Cdr
			a = a.Cdr
		}

		// Create body menv preserving handlers
		bodyMenv := ast.NewMenv(newEnv, menv.Parent, menv.Level, menv.CopyHandlers())
		return Eval(body, bodyMenv)
	}

	fmt.Printf("Error: Not a function in applyFn: %s\n", fn.String())
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

// gensymCount is the global counter for gensym
var gensymCount int64 = 0

// gensymCounter returns the next gensym number
func gensymCounter() int64 {
	gensymCount++
	return gensymCount
}

// evalQuasiquote handles quasiquote, unquote, and unquote-splicing
func evalQuasiquote(expr *ast.Value, menv *ast.Value) *ast.Value {
	return evalQQ(expr, menv, 0)
}

func evalQQ(expr *ast.Value, menv *ast.Value, depth int) *ast.Value {
	if expr == nil || ast.IsNil(expr) {
		return ast.Nil
	}

	// Non-list values are returned as-is
	if !ast.IsCell(expr) {
		return expr
	}

	head := expr.Car

	// Check for unquote: ,x or (unquote x)
	if ast.SymEqStr(head, "unquote") || ast.SymEqStr(head, ",") {
		if depth == 0 {
			// Evaluate the unquoted expression
			return Eval(expr.Cdr.Car, menv)
		}
		// Nested quasiquote - decrease depth
		inner := evalQQ(expr.Cdr.Car, menv, depth-1)
		return ast.List2(ast.NewSym("unquote"), inner)
	}

	// Check for unquote-splicing: ,@x or (unquote-splicing x)
	if ast.SymEqStr(head, "unquote-splicing") || ast.SymEqStr(head, ",@") {
		if depth == 0 {
			// This should be handled in list context
			return Eval(expr.Cdr.Car, menv)
		}
		inner := evalQQ(expr.Cdr.Car, menv, depth-1)
		return ast.List2(ast.NewSym("unquote-splicing"), inner)
	}

	// Check for nested quasiquote
	if ast.SymEqStr(head, "quasiquote") || ast.SymEqStr(head, "`") {
		inner := evalQQ(expr.Cdr.Car, menv, depth+1)
		return ast.List2(ast.NewSym("quasiquote"), inner)
	}

	// Regular list - process each element
	return evalQQList(expr, menv, depth)
}

func evalQQList(list *ast.Value, menv *ast.Value, depth int) *ast.Value {
	if ast.IsNil(list) {
		return ast.Nil
	}
	if !ast.IsCell(list) {
		return list
	}

	head := list.Car

	// Check for unquote-splicing at this level
	if ast.IsCell(head) {
		spliceHead := head.Car
		if ast.SymEqStr(spliceHead, "unquote-splicing") || ast.SymEqStr(spliceHead, ",@") {
			if depth == 0 {
				// Evaluate and splice
				spliced := Eval(head.Cdr.Car, menv)
				rest := evalQQList(list.Cdr, menv, depth)
				return appendLists(spliced, rest)
			}
		}
	}

	// Process head and tail
	newHead := evalQQ(head, menv, depth)
	newTail := evalQQList(list.Cdr, menv, depth)
	return ast.NewCell(newHead, newTail)
}

func appendLists(a, b *ast.Value) *ast.Value {
	if ast.IsNil(a) {
		return b
	}
	if !ast.IsCell(a) {
		return b
	}
	items := ast.ListToSlice(a)
	result := b
	for i := len(items) - 1; i >= 0; i-- {
		result = ast.NewCell(items[i], result)
	}
	return result
}

// FFI Support
// ============

// FFIDecl represents an external function declaration
type FFIDecl struct {
	Name       string
	ReturnType string
	ArgTypes   []string
}

// Global FFI registry
var ffiDeclarations = make(map[string]*FFIDecl)

// RegisterFFI registers an external function declaration
func RegisterFFI(name, retType string, argTypes []string) {
	ffiDeclarations[name] = &FFIDecl{
		Name:       name,
		ReturnType: retType,
		ArgTypes:   argTypes,
	}
}

// GetFFIDeclarations returns all registered FFI declarations
func GetFFIDeclarations() map[string]*FFIDecl {
	return ffiDeclarations
}

// ClearFFIDeclarations clears all FFI declarations (for testing)
func ClearFFIDeclarations() {
	ffiDeclarations = make(map[string]*FFIDecl)
}

// evalFFI handles (ffi "function_name" arg1 arg2 ...)
// In interpretation mode, it cannot actually call C functions
// In code generation mode, it generates C code to call the function
func evalFFI(args *ast.Value, menv *ast.Value) *ast.Value {
	if ast.IsNil(args) {
		return ast.NewError("ffi requires function name")
	}

	// Get function name
	fnNameVal := Eval(args.Car, menv)
	fnName := ""
	if ast.IsSym(fnNameVal) {
		fnName = fnNameVal.Str
	} else if ast.IsCell(fnNameVal) && ast.IsChar(fnNameVal.Car) {
		// It's a string (list of chars), convert to string
		var sb string
		for v := fnNameVal; !ast.IsNil(v) && ast.IsCell(v); v = v.Cdr {
			if ast.IsChar(v.Car) {
				sb += string(rune(v.Car.Int))
			}
		}
		fnName = sb
	} else {
		return ast.NewError("ffi function name must be a symbol or string")
	}

	// Evaluate arguments
	var evaledArgs []*ast.Value
	rest := args.Cdr
	for !ast.IsNil(rest) && ast.IsCell(rest) {
		arg := Eval(rest.Car, menv)
		evaledArgs = append(evaledArgs, arg)
		rest = rest.Cdr
	}

	// Check if any argument is Code (staging mode)
	anyCode := false
	for _, arg := range evaledArgs {
		if ast.IsCode(arg) {
			anyCode = true
			break
		}
	}

	// In staging/codegen mode, generate C code
	if anyCode {
		return generateFFICode(fnName, evaledArgs)
	}

	// In pure interpretation mode, we can only call certain built-in functions
	return evalFFIInterpret(fnName, evaledArgs)
}

// generateFFICode generates C code for an FFI call
func generateFFICode(fnName string, args []*ast.Value) *ast.Value {
	var argStrs []string
	for _, arg := range args {
		if ast.IsCode(arg) {
			argStrs = append(argStrs, arg.Str)
		} else if ast.IsInt(arg) {
			argStrs = append(argStrs, fmt.Sprintf("%d", arg.Int))
		} else if ast.IsFloat(arg) {
			argStrs = append(argStrs, fmt.Sprintf("%g", arg.Float))
		} else if ast.IsNil(arg) {
			argStrs = append(argStrs, "NULL")
		} else if ast.IsChar(arg) {
			argStrs = append(argStrs, fmt.Sprintf("'%c'", rune(arg.Int)))
		} else {
			argStrs = append(argStrs, "NULL")
		}
	}

	// Generate the C function call
	code := fmt.Sprintf("%s(%s)", fnName, joinStrings(argStrs, ", "))
	return ast.NewCode(code)
}

// evalFFIInterpret handles FFI calls in pure interpretation mode
// Only supports a limited set of built-in functions
func evalFFIInterpret(fnName string, args []*ast.Value) *ast.Value {
	switch fnName {
	case "puts":
		// (ffi "puts" str) - print string
		if len(args) > 0 {
			printValue(args[0])
			fmt.Println()
		}
		return ast.NewInt(0)

	case "putchar":
		// (ffi "putchar" ch) - print character
		if len(args) > 0 && ast.IsChar(args[0]) {
			fmt.Printf("%c", rune(args[0].Int))
		} else if len(args) > 0 && ast.IsInt(args[0]) {
			fmt.Printf("%c", rune(args[0].Int))
		}
		return ast.NewInt(0)

	case "getchar":
		// (ffi "getchar") - read character
		var ch byte
		fmt.Scanf("%c", &ch)
		return ast.NewChar(rune(ch))

	case "exit":
		// (ffi "exit" code) - exit with code (for interpretation, just return error)
		code := int64(0)
		if len(args) > 0 && ast.IsInt(args[0]) {
			code = args[0].Int
		}
		return ast.NewError(fmt.Sprintf("exit:%d", code))

	case "printf":
		// Simple printf support
		if len(args) > 0 {
			printValue(args[0])
			for _, arg := range args[1:] {
				fmt.Print(" ")
				printValue(arg)
			}
		}
		return ast.NewInt(0)

	default:
		return ast.NewError(fmt.Sprintf("ffi: unknown function in interpreter: %s", fnName))
	}
}

// printValue prints a value for FFI output
func printValue(v *ast.Value) {
	if v == nil || ast.IsNil(v) {
		return
	}
	if ast.IsInt(v) {
		fmt.Print(v.Int)
	} else if ast.IsChar(v) {
		fmt.Printf("%c", rune(v.Int))
	} else if ast.IsSym(v) {
		fmt.Print(v.Str)
	} else if ast.IsCell(v) {
		// Could be a string (list of chars)
		for ; !ast.IsNil(v) && ast.IsCell(v); v = v.Cdr {
			if ast.IsChar(v.Car) {
				fmt.Printf("%c", rune(v.Car.Int))
			}
		}
	}
}

// evalFFIDeclare handles (ffi-declare "ret_type" "func_name" "arg1_type" ...)
func evalFFIDeclare(args *ast.Value, menv *ast.Value) *ast.Value {
	if ast.IsNil(args) || ast.IsNil(args.Cdr) {
		return ast.NewError("ffi-declare requires return type and function name")
	}

	// Get return type
	retTypeVal := Eval(args.Car, menv)
	retType := valueToString(retTypeVal)

	// Get function name
	fnNameVal := Eval(args.Cdr.Car, menv)
	fnName := valueToString(fnNameVal)

	// Get argument types
	var argTypes []string
	rest := args.Cdr.Cdr
	for !ast.IsNil(rest) && ast.IsCell(rest) {
		argTypeVal := Eval(rest.Car, menv)
		argTypes = append(argTypes, valueToString(argTypeVal))
		rest = rest.Cdr
	}

	// Register the declaration
	RegisterFFI(fnName, retType, argTypes)

	// Return the function name as a symbol (can be used with ffi)
	return ast.NewSym(fnName)
}

// valueToString converts a value to a string for FFI declarations
func valueToString(v *ast.Value) string {
	if v == nil || ast.IsNil(v) {
		return ""
	}
	if ast.IsSym(v) {
		return v.Str
	}
	// Handle string (list of chars)
	if ast.IsCell(v) && ast.IsChar(v.Car) {
		var sb string
		for ; !ast.IsNil(v) && ast.IsCell(v); v = v.Cdr {
			if ast.IsChar(v.Car) {
				sb += string(rune(v.Car.Int))
			}
		}
		return sb
	}
	return v.String()
}

// joinStrings joins strings with a separator
func joinStrings(strs []string, sep string) string {
	if len(strs) == 0 {
		return ""
	}
	result := strs[0]
	for i := 1; i < len(strs); i++ {
		result += sep + strs[i]
	}
	return result
}

// GenerateFFIDeclarations generates C declarations for all registered FFI functions
func GenerateFFIDeclarations() string {
	var sb string
	sb += "/* FFI Declarations */\n"
	for _, decl := range ffiDeclarations {
		sb += fmt.Sprintf("extern %s %s(", decl.ReturnType, decl.Name)
		if len(decl.ArgTypes) == 0 {
			sb += "void"
		} else {
			for i, argType := range decl.ArgTypes {
				if i > 0 {
					sb += ", "
				}
				sb += argType
			}
		}
		sb += ");\n"
	}
	sb += "\n"
	return sb
}

// Macro System Implementation
// ===========================

// evalDefmacro handles (defmacro name (params...) body scope)
// Defines a macro transformer and evaluates scope with the macro bound
func evalDefmacro(args *ast.Value, menv *ast.Value) *ast.Value {
	if ast.IsNil(args) || ast.IsNil(args.Cdr) || ast.IsNil(args.Cdr.Cdr) {
		return ast.NewError("defmacro requires name, params, body, and scope")
	}

	// Get macro name
	nameVal := args.Car
	if !ast.IsSym(nameVal) {
		return ast.NewError("defmacro: macro name must be a symbol")
	}
	name := nameVal.Str

	// Get parameter list
	paramsVal := args.Cdr.Car
	var params []string
	if ast.IsCell(paramsVal) {
		p := paramsVal
		for !ast.IsNil(p) && ast.IsCell(p) {
			if ast.IsSym(p.Car) {
				params = append(params, p.Car.Str)
			}
			p = p.Cdr
		}
	} else if ast.IsSym(paramsVal) {
		// Single parameter as symbol
		params = []string{paramsVal.Str}
	}

	// Get body
	body := args.Cdr.Cdr.Car

	// Get scope (if provided)
	var scope *ast.Value
	if !ast.IsNil(args.Cdr.Cdr.Cdr) && ast.IsCell(args.Cdr.Cdr.Cdr) {
		scope = args.Cdr.Cdr.Cdr.Car
	}

	// Define the macro
	macro := DefineMacro(name, params, body, menv)

	// If no scope, return the macro transformer
	if scope == nil || ast.IsNil(scope) {
		return macro.Transformer
	}

	// Bind macro name to transformer in environment and evaluate scope
	// Create a lambda that wraps the macro expansion
	macroFn := ast.NewPrim(func(macroArgs *ast.Value, macroMenv *ast.Value) *ast.Value {
		// Collect arguments
		var argsList []*ast.Value
		a := macroArgs
		for !ast.IsNil(a) && ast.IsCell(a) {
			argsList = append(argsList, a.Car)
			a = a.Cdr
		}

		// Expand the macro
		expanded := ExpandMacro(macro, argsList, macroMenv)

		// Evaluate the expansion
		return Eval(expanded, macroMenv)
	})

	// Extend environment with macro bound
	newEnv := EnvExtend(menv.Env, nameVal, macroFn)
	scopeMenv := ast.NewMenv(newEnv, menv.Parent, menv.Level, menv.CopyHandlers())

	// Evaluate scope
	return Eval(scope, scopeMenv)
}

// evalMcall handles (mcall macro-name arg1 arg2 ...)
// Quotes each argument and applies the macro transformer, then evaluates result
func evalMcall(args *ast.Value, menv *ast.Value) *ast.Value {
	if ast.IsNil(args) {
		return ast.NewError("mcall requires macro name")
	}

	// Get macro name
	nameVal := args.Car
	var name string
	if ast.IsSym(nameVal) {
		name = nameVal.Str
	} else {
		// Try evaluating to get macro name
		evaledName := Eval(nameVal, menv)
		if ast.IsSym(evaledName) {
			name = evaledName.Str
		} else {
			return ast.NewError("mcall: first argument must be a macro name")
		}
	}

	// Look up macro
	macro := GetMacro(name)
	if macro == nil {
		return ast.NewError(fmt.Sprintf("mcall: undefined macro: %s", name))
	}

	// Collect unevaluated arguments (they will be quoted by ExpandMacro)
	var argsList []*ast.Value
	rest := args.Cdr
	for !ast.IsNil(rest) && ast.IsCell(rest) {
		argsList = append(argsList, rest.Car)
		rest = rest.Cdr
	}

	// Expand the macro
	expanded := ExpandMacro(macro, argsList, menv)

	// Evaluate the expanded form
	return Eval(expanded, menv)
}

// evalMacroexpand handles (macroexpand expr)
// Expands macro calls in expr without evaluating the result
func evalMacroexpand(args *ast.Value, menv *ast.Value) *ast.Value {
	if ast.IsNil(args) {
		return ast.NewError("macroexpand requires an expression")
	}

	expr := args.Car

	// If it's not a list, return as-is
	if !ast.IsCell(expr) {
		return expr
	}

	// Check if it's an mcall form
	op := expr.Car
	if ast.SymEqStr(op, "mcall") {
		mcallArgs := expr.Cdr
		if ast.IsNil(mcallArgs) {
			return expr
		}

		// Get macro name
		nameVal := mcallArgs.Car
		var name string
		if ast.IsSym(nameVal) {
			name = nameVal.Str
		} else {
			return expr // Can't expand if name isn't a symbol
		}

		// Look up macro
		macro := GetMacro(name)
		if macro == nil {
			return expr // Unknown macro, return as-is
		}

		// Collect arguments
		var argsList []*ast.Value
		rest := mcallArgs.Cdr
		for !ast.IsNil(rest) && ast.IsCell(rest) {
			argsList = append(argsList, rest.Car)
			rest = rest.Cdr
		}

		// Expand without evaluating
		return ExpandMacro(macro, argsList, menv)
	}

	// Check if the head is a macro name directly (alternative syntax)
	if ast.IsSym(op) {
		macro := GetMacro(op.Str)
		if macro != nil {
			// Collect arguments
			var argsList []*ast.Value
			rest := expr.Cdr
			for !ast.IsNil(rest) && ast.IsCell(rest) {
				argsList = append(argsList, rest.Car)
				rest = rest.Cdr
			}

			// Expand without evaluating
			return ExpandMacro(macro, argsList, menv)
		}
	}

	// Not a macro call, return as-is
	return expr
}

// Type System for Back-Edge Analysis
// ===================================

// evalDeftype handles (deftype TypeName (field1 Type1) (field2 Type2) ...)
// Registers the type with the global registry for back-edge analysis
func evalDeftype(args *ast.Value, menv *ast.Value) *ast.Value {
	if ast.IsNil(args) {
		return ast.NewError("deftype requires type name")
	}

	// Get type name
	typeNameVal := args.Car
	if !ast.IsSym(typeNameVal) {
		return ast.NewError("deftype: type name must be a symbol")
	}
	typeName := typeNameVal.Str

	// Parse fields: (field1 Type1) (field2 Type2) ...
	var fields []codegen.TypeField
	rest := args.Cdr
	for !ast.IsNil(rest) && ast.IsCell(rest) {
		fieldDef := rest.Car
		if !ast.IsCell(fieldDef) {
			return ast.NewError("deftype: field definition must be a list (name type)")
		}

		fieldNameVal := fieldDef.Car
		if !ast.IsSym(fieldNameVal) {
			return ast.NewError("deftype: field name must be a symbol")
		}
		fieldName := fieldNameVal.Str

		fieldTypeVal := fieldDef.Cdr.Car
		if !ast.IsSym(fieldTypeVal) {
			return ast.NewError("deftype: field type must be a symbol")
		}
		fieldType := fieldTypeVal.Str

		// Determine if field is scannable (pointer type)
		// Primitive types like int, float, bool are not scannable
		isScannable := !isPrimitiveType(fieldType)

		fields = append(fields, codegen.TypeField{
			Name:        fieldName,
			Type:        fieldType,
			IsScannable: isScannable,
			Strength:    codegen.FieldStrong, // Default to strong, back-edge analysis will update
		})

		rest = rest.Cdr
	}

	// Register type with global registry
	registry := codegen.GlobalRegistry()
	registry.RegisterType(typeName, fields)

	// Rebuild ownership graph and analyze back-edges
	registry.BuildOwnershipGraph()
	registry.AnalyzeBackEdges()

	// Return the type name as a symbol
	return typeNameVal
}

// isPrimitiveType returns true if the type is a primitive (non-pointer) type
func isPrimitiveType(typeName string) bool {
	switch typeName {
	case "int", "long", "short", "char", "byte",
		"float", "double",
		"bool", "boolean",
		"void",
		"i8", "i16", "i32", "i64",
		"u8", "u16", "u32", "u64",
		"f32", "f64":
		return true
	default:
		return false
	}
}
