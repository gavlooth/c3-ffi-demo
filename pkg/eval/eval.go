// Package eval provides a Go-based interpreter for Purple expressions.
//
// DEPRECATED: This package is for development and testing only.
// Production execution should use the native compiler (pkg/compiler)
// which generates C code executed via JIT or AOT compilation.
//
// The Go-based interpreter violates the architectural principle:
// "Go is compiler-only - Go must NEVER be in runtime/interpretation hot paths."
//
// Use cases for this package:
//   - Testing compiler output against reference implementation
//   - Debugging AST evaluation
//   - Development iteration (faster compile times)
//
// For production, use:
//   - main.go (default mode) - compiles to C and executes
//   - pkg/compiler - generates C code
//   - pkg/jit - JIT compilation with caching
package eval

import (
	"fmt"
	"strings"
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
	// OmniLisp: Handle dot notation for field access (obj.field)
	if strings.Contains(exp.Str, ".") {
		parts := strings.SplitN(exp.Str, ".", 2)
		if len(parts) == 2 {
			objName := parts[0]
			fieldName := parts[1]

			// Look up the object
			objSym := ast.NewSym(objName)
			obj := EnvLookup(menv.Env, objSym)
			if obj == nil {
				obj = GlobalLookup(objSym)
			}
			if obj == nil {
				// Could be Type.Variant (enum qualified access)
				enumDef := GlobalStructRegistry().GetEnum(objName)
				if enumDef != nil {
					for _, v := range enumDef.Variants {
						if v.Name == fieldName {
							return GlobalLookup(ast.NewSym(fieldName))
						}
					}
				}
				fmt.Printf("Error: Unbound variable: %s\n", objName)
				return ast.Nil
			}

			// Get field from object
			return GetField(obj, fieldName)
		}
	}

	// First check local environment
	v := EnvLookup(menv.Env, exp)
	if v != nil {
		return v
	}
	// Fall back to global environment
	v = GlobalLookup(exp)
	if v != nil {
		return v
	}
	fmt.Printf("Error: Unbound variable: %s\n", exp.Str)
	return ast.Nil
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

	// Handle continuation application
	if ast.IsCont(fn) {
		// Get the first argument
		arg := ast.Nil
		if !ast.IsNil(args) && ast.IsCell(args) {
			arg = args.Car
		}
		// Call the continuation function (will panic to escape)
		return fn.ContFn(arg)
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

// LetMode represents the binding mode for let
type LetMode int

const (
	LetParallel   LetMode = iota // Default: parallel binding
	LetSequential                // :seq - sequential binding
	LetRecursive                 // :rec - recursive binding
)

// LetModifiers holds parsed let modifiers
type LetModifiers struct {
	Mode     LetMode
	Name     *ast.Value // Named let (loop name)
	Bindings *ast.Value // Binding form
	Body     *ast.Value // Body expressions
}

// parseLetModifiers parses OmniLisp let modifiers
// Supports:
//   (let [x 1 y 2] body)              - parallel, array bindings
//   (let :seq [x 1 y 2] body)         - sequential
//   (let :rec [x ... y ...] body)     - recursive
//   (let loop [i 0] body)             - named let
//   (let :seq loop [i 0] body)        - combined
func parseLetModifiers(args *ast.Value) *LetModifiers {
	mods := &LetModifiers{
		Mode: LetParallel,
	}

	rest := args

	// Check for modifier keyword
	if !ast.IsNil(rest) && ast.IsCell(rest) {
		first := rest.Car
		if ast.IsKeyword(first) || (ast.IsSym(first) && len(first.Str) > 0 && first.Str[0] == ':') {
			keyword := first.Str
			if ast.IsKeyword(first) {
				keyword = first.Str
			} else if first.Str[0] == ':' {
				keyword = first.Str[1:]
			}

			switch keyword {
			case "seq":
				mods.Mode = LetSequential
				rest = rest.Cdr
			case "rec":
				mods.Mode = LetRecursive
				rest = rest.Cdr
			}
		}
	}

	// Check for named let (loop name)
	if !ast.IsNil(rest) && ast.IsCell(rest) {
		first := rest.Car
		if ast.IsSym(first) && !ast.IsKeyword(first) {
			// This is a loop name if followed by array/list bindings
			if !ast.IsNil(rest.Cdr) && ast.IsCell(rest.Cdr) {
				second := rest.Cdr.Car
				if ast.IsArray(second) || ast.IsCell(second) {
					mods.Name = first
					rest = rest.Cdr
				}
			}
		}
	}

	// Get bindings and body
	if !ast.IsNil(rest) && ast.IsCell(rest) {
		mods.Bindings = rest.Car
		mods.Body = rest.Cdr.Car
	}

	return mods
}

// parseBindings parses bindings from either list or array form
// List form: ((x 1) (y 2))
// Array form: [x 1 y 2]
func parseBindings(bindings *ast.Value) []bindInfo {
	var result []bindInfo

	// Array form: [x 1 y 2]
	if ast.IsArray(bindings) {
		arr := bindings.ArrayData
		for i := 0; i+1 < len(arr); i += 2 {
			if ast.IsSym(arr[i]) {
				result = append(result, bindInfo{
					sym: arr[i],
					val: arr[i+1], // Note: value is unevaluated here
				})
			} else if ast.IsArray(arr[i]) && len(arr[i].ArrayData) >= 1 {
				// Typed binding: [[x {Int}] 1]
				if ast.IsSym(arr[i].ArrayData[0]) {
					result = append(result, bindInfo{
						sym: arr[i].ArrayData[0],
						val: arr[i+1],
					})
				}
			}
		}
		return result
	}

	// List form: ((x 1) (y 2))
	for b := bindings; !ast.IsNil(b) && ast.IsCell(b); b = b.Cdr {
		bind := b.Car
		if ast.IsCell(bind) {
			sym := bind.Car
			valExpr := bind.Cdr.Car
			result = append(result, bindInfo{sym: sym, val: valExpr})
		}
	}

	return result
}

func defaultHLet(exp, menv *ast.Value) *ast.Value {
	args := exp.Cdr

	// Parse OmniLisp modifiers
	mods := parseLetModifiers(args)

	// Parse bindings
	bindList := parseBindings(mods.Bindings)

	// Handle named let (loop form)
	if mods.Name != nil {
		return evalNamedLet(mods.Name, bindList, mods.Body, mods.Mode, menv)
	}

	// Handle different binding modes
	switch mods.Mode {
	case LetSequential:
		return evalLetSequential(bindList, mods.Body, menv)
	case LetRecursive:
		return evalLetRecursive(bindList, mods.Body, menv)
	default:
		return evalLetParallel(bindList, mods.Body, menv)
	}
}

// evalLetParallel evaluates bindings in parallel (all see same environment)
func evalLetParallel(bindList []bindInfo, body *ast.Value, menv *ast.Value) *ast.Value {
	anyCode := false
	newEnv := menv.Env

	// Evaluate all values in the original environment
	evaluated := make([]bindInfo, len(bindList))
	for i, bi := range bindList {
		val := Eval(bi.val, menv)
		if val == nil {
			val = ast.Nil
		}
		if ast.IsCode(val) {
			anyCode = true
		}
		evaluated[i] = bindInfo{sym: bi.sym, val: val}
	}

	if anyCode {
		return generateLetCode(evaluated, body, menv)
	}

	// Extend environment with all bindings
	for _, bi := range evaluated {
		newEnv = EnvExtend(newEnv, bi.sym, bi.val)
	}

	bodyMenv := ast.NewMenv(newEnv, menv.Parent, menv.Level, menv.CopyHandlers())
	return Eval(body, bodyMenv)
}

// evalLetSequential evaluates bindings sequentially (each sees previous bindings)
func evalLetSequential(bindList []bindInfo, body *ast.Value, menv *ast.Value) *ast.Value {
	newEnv := menv.Env
	currentMenv := menv

	for _, bi := range bindList {
		val := Eval(bi.val, currentMenv)
		if val == nil {
			val = ast.Nil
		}
		newEnv = EnvExtend(newEnv, bi.sym, val)
		currentMenv = ast.NewMenv(newEnv, menv.Parent, menv.Level, menv.CopyHandlers())
	}

	return Eval(body, currentMenv)
}

// evalLetRecursive evaluates bindings recursively (all see all bindings, even unevaluated)
func evalLetRecursive(bindList []bindInfo, body *ast.Value, menv *ast.Value) *ast.Value {
	// Create placeholders
	uninit := ast.NewPrim(nil)
	newEnv := menv.Env
	for _, bi := range bindList {
		newEnv = EnvExtend(newEnv, bi.sym, uninit)
	}

	recMenv := ast.NewMenv(newEnv, menv.Parent, menv.Level, menv.CopyHandlers())

	// Evaluate and update
	for _, bi := range bindList {
		val := Eval(bi.val, recMenv)
		if val == nil {
			val = ast.Nil
		}
		// Update the binding in place
		for e := newEnv; !ast.IsNil(e) && ast.IsCell(e); e = e.Cdr {
			pair := e.Car
			if ast.SymEq(pair.Car, bi.sym) {
				pair.Cdr = val
				break
			}
		}
	}

	return Eval(body, recMenv)
}

// evalNamedLet evaluates a named let (loop form)
// (let loop [i 0] body) -> loop is a function that can be called recursively
func evalNamedLet(name *ast.Value, bindList []bindInfo, body *ast.Value, mode LetMode, menv *ast.Value) *ast.Value {
	// Build parameter list and initial values
	var params []*ast.Value
	var initVals []*ast.Value

	for _, bi := range bindList {
		params = append(params, bi.sym)
		initVals = append(initVals, bi.val)
	}

	// Create a recursive lambda for the loop
	paramList := ast.Nil
	for i := len(params) - 1; i >= 0; i-- {
		paramList = ast.NewCell(params[i], paramList)
	}

	// Create the recursive lambda
	loopLam := ast.NewRecLambda(name, paramList, body, menv.Env)

	// Extend environment with the loop function
	newEnv := EnvExtend(menv.Env, name, loopLam)
	loopLam.LamEnv = newEnv // Update closure to include self

	// Evaluate initial values based on mode
	var evaledVals []*ast.Value
	switch mode {
	case LetSequential:
		currentEnv := newEnv
		for _, valExpr := range initVals {
			currentMenv := ast.NewMenv(currentEnv, menv.Parent, menv.Level, menv.CopyHandlers())
			val := Eval(valExpr, currentMenv)
			evaledVals = append(evaledVals, val)
		}
	default:
		initMenv := ast.NewMenv(newEnv, menv.Parent, menv.Level, menv.CopyHandlers())
		for _, valExpr := range initVals {
			val := Eval(valExpr, initMenv)
			evaledVals = append(evaledVals, val)
		}
	}

	// Create environment with initial bindings
	bodyEnv := newEnv
	for i, param := range params {
		if i < len(evaledVals) {
			bodyEnv = EnvExtend(bodyEnv, param, evaledVals[i])
		}
	}

	bodyMenv := ast.NewMenv(bodyEnv, menv.Parent, menv.Level, menv.CopyHandlers())
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

	// OmniLisp self-evaluating types
	case ast.TArray:
		// Evaluate each element in the array
		elements := make([]*ast.Value, len(expr.ArrayData))
		for i, elem := range expr.ArrayData {
			elements[i] = Eval(elem, menv)
		}
		return ast.NewArray(elements)

	case ast.TDict:
		// Evaluate both keys and values
		keys := make([]*ast.Value, len(expr.DictKeys))
		values := make([]*ast.Value, len(expr.DictValues))
		for i := range expr.DictKeys {
			keys[i] = Eval(expr.DictKeys[i], menv)
			values[i] = Eval(expr.DictValues[i], menv)
		}
		return ast.NewDict(keys, values)

	case ast.TTuple:
		// Evaluate each element in the tuple
		elements := make([]*ast.Value, len(expr.TupleData))
		for i, elem := range expr.TupleData {
			elements[i] = Eval(elem, menv)
		}
		return ast.NewTuple(elements)

	case ast.TTypeLit:
		// Type literals are self-evaluating
		// But we may need to evaluate type parameters
		params := make([]*ast.Value, len(expr.TypeParams))
		for i, param := range expr.TypeParams {
			params[i] = Eval(param, menv)
		}
		return ast.NewTypeLit(expr.TypeName, params)

	case ast.TKeyword:
		// Keywords are self-evaluating
		return expr

	case ast.TNothing:
		// nothing is self-evaluating
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

		// OmniLisp: Arrow lambda shorthand (-> x body) or (-> (x y) body)
		if ast.SymEqStr(op, "->") {
			return evalArrowLambda(args, menv)
		}

		if ast.SymEqStr(op, "match") {
			return EvalMatch(expr, menv)
		}

		if ast.SymEqStr(op, "do") {
			return evalDo(args, menv)
		}

		// OmniLisp: begin - sequence expressions, returns last (or nothing if empty)
		if ast.SymEqStr(op, "begin") {
			return evalBegin(args, menv)
		}

		// OmniLisp: send - short form for chan-send!
		if ast.SymEqStr(op, "send") {
			return PrimChanSend(args, menv)
		}

		// OmniLisp: recv - short form for chan-recv!
		if ast.SymEqStr(op, "recv") {
			ch := Eval(args.Car, menv)
			return ChanRecvBlocking(ch)
		}

		// OmniLisp: for - collect results with nested (Cartesian) semantics
		if ast.SymEqStr(op, "for") {
			return evalFor(args, menv, true)
		}

		// OmniLisp: foreach - side effects with nested semantics
		if ast.SymEqStr(op, "foreach") {
			return evalFor(args, menv, false)
		}

		// set! - mutate existing binding (handles dot notation too)
		if ast.SymEqStr(op, "set!") {
			return evalSetBang(args, menv)
		}

		// OmniLisp: set-field! - mutate struct field
		if ast.SymEqStr(op, "set-field!") {
			return evalSetField(args, menv)
		}

		// define - create top-level definition
		if ast.SymEqStr(op, "define") {
			return evalDefine(args, menv)
		}

		// call/cc - call with current continuation
		if ast.SymEqStr(op, "call/cc") || ast.SymEqStr(op, "call-with-current-continuation") {
			return evalCallCC(args, menv)
		}

		// prompt - establish a delimiter for control (Felleisen's naming)
		if ast.SymEqStr(op, "prompt") {
			return evalPrompt(args, menv)
		}

		// control - capture delimited continuation (Felleisen's naming)
		if ast.SymEqStr(op, "control") {
			return evalControl(args, menv)
		}

		// go - spawn a green thread
		if ast.SymEqStr(op, "go") {
			return evalGo(args, menv)
		}

		// select - wait on multiple channels
		if ast.SymEqStr(op, "select") {
			return evalSelect(args, menv)
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

		// OmniLisp: syntax-quote / #' - hygienic quasiquote
		if ast.SymEqStr(op, "syntax-quote") || ast.SymEqStr(op, "#'") {
			return evalSyntaxQuote(args, menv)
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

		// OmniLisp Module System
		// ======================

		// module - Define a module
		// (module ModuleName (export f1 f2) body...)
		if ast.SymEqStr(op, "module") {
			return evalModule(args, menv)
		}

		// import - Import from a module
		// (import ModuleName) or (import [ModuleName :as M])
		if ast.SymEqStr(op, "import") {
			return evalImport(args, menv)
		}

		// require - Load a module file
		// (require "path/to/module.ol")
		if ast.SymEqStr(op, "require") {
			return evalRequire(args, menv)
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

// evalArrowLambda implements arrow lambda shorthand
// (-> x body) - single parameter
// (-> (x y) body) - multiple parameters
func evalArrowLambda(args, menv *ast.Value) *ast.Value {
	if ast.IsNil(args) || ast.IsNil(args.Cdr) {
		return ast.NewError("->: requires params and body")
	}

	paramsExpr := args.Car
	body := args.Cdr.Car

	var params *ast.Value

	// Single symbol: (-> x body) means (lambda (x) body)
	if ast.IsSym(paramsExpr) {
		params = ast.List1(paramsExpr)
	} else if ast.IsCell(paramsExpr) {
		// List of params: (-> (x y) body)
		params = paramsExpr
	} else if ast.IsArray(paramsExpr) {
		// Array of params: (-> [x y] body) - convert to list
		params = ast.Nil
		for i := len(paramsExpr.ArrayData) - 1; i >= 0; i-- {
			params = ast.NewCell(paramsExpr.ArrayData[i], params)
		}
	} else {
		return ast.NewError("->: params must be symbol, list, or array")
	}

	return ast.NewLambda(params, body, menv.Env)
}

// forBinding represents a binding in a for expression
type forBinding struct {
	name *ast.Value // Variable name
	seq  *ast.Value // Sequence to iterate over
}

// evalFor implements for/foreach with nested (Cartesian product) semantics
// (for [x xs y ys] body) - collect results
// (foreach [x xs y ys] body) - side effects only
func evalFor(args, menv *ast.Value, collect bool) *ast.Value {
	if ast.IsNil(args) || ast.IsNil(args.Cdr) {
		if collect {
			return ast.NewError("for: requires bindings and body")
		}
		return ast.NewError("foreach: requires bindings and body")
	}

	bindingsExpr := args.Car
	body := args.Cdr.Car

	// Parse bindings: [x xs y ys ...]
	var bindings []forBinding

	if ast.IsArray(bindingsExpr) {
		arr := bindingsExpr.ArrayData
		for i := 0; i+1 < len(arr); i += 2 {
			name := arr[i]
			seqExpr := arr[i+1]
			if !ast.IsSym(name) {
				return ast.NewError("for: binding name must be a symbol")
			}
			seq := Eval(seqExpr, menv)
			bindings = append(bindings, forBinding{name: name, seq: seq})
		}
	} else if ast.IsCell(bindingsExpr) {
		// List form: ((x xs) (y ys))
		for b := bindingsExpr; !ast.IsNil(b) && ast.IsCell(b); b = b.Cdr {
			binding := b.Car
			if !ast.IsCell(binding) {
				return ast.NewError("for: binding must be a pair")
			}
			name := binding.Car
			seqExpr := binding.Cdr.Car
			if !ast.IsSym(name) {
				return ast.NewError("for: binding name must be a symbol")
			}
			seq := Eval(seqExpr, menv)
			bindings = append(bindings, forBinding{name: name, seq: seq})
		}
	} else {
		return ast.NewError("for: bindings must be array or list")
	}

	if len(bindings) == 0 {
		if collect {
			return ast.NewArray(nil)
		}
		return ast.Nothing
	}

	// Nested iteration (Cartesian product)
	var results []*ast.Value
	evalForNested(bindings, 0, menv, body, collect, &results)

	if collect {
		return ast.NewArray(results)
	}
	return ast.Nothing
}

// evalForNested recursively evaluates nested for bindings
func evalForNested(bindings []forBinding, idx int, menv *ast.Value, body *ast.Value, collect bool, results *[]*ast.Value) {
	if idx >= len(bindings) {
		// All bindings bound, evaluate body
		result := Eval(body, menv)
		if collect {
			*results = append(*results, result)
		}
		return
	}

	binding := bindings[idx]
	seq := binding.seq

	// Iterate over the sequence
	if ast.IsArray(seq) {
		for _, elem := range seq.ArrayData {
			newEnv := EnvExtend(menv.Env, binding.name, elem)
			newMenv := ast.NewMenv(newEnv, menv.Parent, menv.Level, menv.CopyHandlers())
			evalForNested(bindings, idx+1, newMenv, body, collect, results)
		}
	} else if ast.IsCell(seq) {
		for s := seq; !ast.IsNil(s) && ast.IsCell(s); s = s.Cdr {
			newEnv := EnvExtend(menv.Env, binding.name, s.Car)
			newMenv := ast.NewMenv(newEnv, menv.Parent, menv.Level, menv.CopyHandlers())
			evalForNested(bindings, idx+1, newMenv, body, collect, results)
		}
	}
	// Other sequence types could be added here (e.g., ranges)
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

// evalBegin implements OmniLisp begin
// (begin) -> nothing
// (begin e1 e2 ...) -> evaluates all, returns last
// Does NOT create a new scope
func evalBegin(args, menv *ast.Value) *ast.Value {
	if ast.IsNil(args) {
		return ast.Nothing
	}
	var result *ast.Value = ast.Nothing
	rest := args
	for !ast.IsNil(rest) && ast.IsCell(rest) {
		result = Eval(rest.Car, menv)
		rest = rest.Cdr
	}
	return result
}

// contEscape is used to escape from call/cc
type contEscape struct {
	value *ast.Value
	tag   int // Unique tag for matching continuations
}

// shiftEscape is used for shift/reset delimited continuations
type shiftEscape struct {
	proc      *ast.Value // The procedure to call with the continuation
	resetTag  int        // Tag of the enclosing reset
	delimited bool       // True if this is from shift (vs call/cc)
}

var contTagCounter int = 0

func nextContTag() int {
	contTagCounter++
	return contTagCounter
}

// evalCallCC implements call/cc (call with current continuation)
// (call/cc (lambda (k) body)) - k is the continuation that escapes to caller
func evalCallCC(args, menv *ast.Value) *ast.Value {
	if ast.IsNil(args) {
		return ast.NewError("call/cc: requires a procedure")
	}

	// Evaluate the procedure
	proc := Eval(args.Car, menv)
	if proc == nil {
		return ast.NewError("call/cc: procedure evaluated to nil")
	}

	// Create a unique tag for this continuation
	tag := nextContTag()

	// Create a continuation that captures the escape
	contFn := func(val *ast.Value) *ast.Value {
		// Panic with the value to escape
		panic(contEscape{value: val, tag: tag})
	}

	// Create the continuation value
	cont := ast.NewCont(contFn, menv)
	cont.Int = int64(tag) // Store tag in Int field for matching

	// Call the procedure with the continuation, catching any escape
	defer func() {
		if r := recover(); r != nil {
			if escape, ok := r.(contEscape); ok && escape.tag == tag {
				// This is our escape, return the value
				// But we can't return from defer, so we re-panic to be caught below
				panic(escape)
			}
			// Not our escape, re-panic
			panic(r)
		}
	}()

	// Apply the procedure to the continuation
	// This is wrapped in a function to handle the defer properly
	return callWithContinuation(proc, cont, menv, tag)
}

// callWithContinuation calls proc with cont and handles continuation escapes
func callWithContinuation(proc, cont *ast.Value, menv *ast.Value, tag int) (result *ast.Value) {
	defer func() {
		if r := recover(); r != nil {
			if escape, ok := r.(contEscape); ok && escape.tag == tag {
				result = escape.value
				return
			}
			panic(r) // Re-panic if not our escape
		}
	}()

	return applyFn(proc, ast.List1(cont), menv)
}

// resetTagStack tracks the current reset boundaries
var resetTagStack []int

// pushResetTag pushes a tag onto the reset stack
func pushResetTag(tag int) {
	resetTagStack = append(resetTagStack, tag)
}

// popResetTag pops a tag from the reset stack
func popResetTag() {
	if len(resetTagStack) > 0 {
		resetTagStack = resetTagStack[:len(resetTagStack)-1]
	}
}

// currentResetTag returns the current reset tag or -1 if none
func getCurrentResetTag() int {
	if len(resetTagStack) == 0 {
		return -1
	}
	return resetTagStack[len(resetTagStack)-1]
}

// evalPrompt implements (prompt body) - establishes a delimiter (Felleisen's naming)
func evalPrompt(args, menv *ast.Value) *ast.Value {
	if ast.IsNil(args) {
		return ast.Nil
	}

	tag := nextContTag()
	body := args.Car

	return evalWithPrompt(body, menv, tag)
}

// evalWithPrompt evaluates body with a prompt boundary
func evalWithPrompt(body, menv *ast.Value, tag int) (result *ast.Value) {
	pushResetTag(tag)
	defer popResetTag()

	defer func() {
		if r := recover(); r != nil {
			if escape, ok := r.(shiftEscape); ok && escape.resetTag == tag {
				// We caught a control from within our prompt
				result = escape.proc
				return
			}
			panic(r) // Re-panic if not our escape
		}
	}()

	return Eval(body, menv)
}

// evalControl implements (control k body) - captures delimited continuation (Felleisen's naming)
// k is bound to the continuation up to the enclosing prompt
func evalControl(args, menv *ast.Value) *ast.Value {
	if ast.IsNil(args) || ast.IsNil(args.Cdr) {
		return ast.NewError("control: requires variable and body")
	}

	kSym := args.Car
	if !ast.IsSym(kSym) {
		return ast.NewError("control: first argument must be a symbol")
	}

	body := args.Cdr.Car

	// Get the enclosing prompt tag
	resetTag := getCurrentResetTag()
	if resetTag < 0 {
		return ast.NewError("control: no enclosing prompt")
	}

	// Create the continuation that, when called, returns to the enclosing reset
	contFn := func(val *ast.Value) *ast.Value {
		// This continuation, when invoked, just returns the value
		// In a full implementation, this would re-install the context
		return val
	}

	cont := ast.NewCont(contFn, menv)

	// Bind k to the continuation and evaluate body
	newEnv := EnvExtend(menv.Env, kSym, cont)
	bodyMenv := ast.NewMenv(newEnv, menv.Parent, menv.Level, menv.CopyHandlers())

	// Evaluate the body
	result := Eval(body, bodyMenv)

	// Panic to escape to the enclosing reset with the result
	panic(shiftEscape{proc: result, resetTag: resetTag, delimited: true})
}

// evalGo implements (go expr) - spawns a green thread (fire-and-forget)
func evalGo(args, menv *ast.Value) *ast.Value {
	if ast.IsNil(args) {
		return ast.NewError("go: requires an expression")
	}

	expr := args.Car

	// Use goroutines for true concurrency (fire-and-forget)
	go func() {
		// Create a new menv for the goroutine
		goMenv := ast.NewMenv(menv.Env, menv.Parent, menv.Level, menv.CopyHandlers())
		Eval(expr, goMenv)
	}()

	// Return nil (unit) - go is fire-and-forget like Go's goroutines
	return ast.Nil
}

// evalSelect implements (select clauses...)
// Each clause is (chan-expr => handler-body) or (default => body)
func evalSelect(args, menv *ast.Value) *ast.Value {
	if ast.IsNil(args) {
		return ast.Nil
	}

	// Collect channel operations
	type selectCase struct {
		ch      *ast.Value
		isSend  bool
		sendVal *ast.Value
		recvVar *ast.Value // Variable to bind received value (for recv cases)
		body    *ast.Value
	}

	var cases []selectCase
	var defaultBody *ast.Value

	// Parse clauses
	rest := args
	for !ast.IsNil(rest) && ast.IsCell(rest) {
		clause := rest.Car
		if !ast.IsCell(clause) {
			rest = rest.Cdr
			continue
		}

		first := clause.Car

		// Check for default clause
		if ast.SymEqStr(first, "default") {
			if !ast.IsNil(clause.Cdr) && ast.IsCell(clause.Cdr) {
				defaultBody = clause.Cdr.Car
			}
			rest = rest.Cdr
			continue
		}

		// Parse channel operation
		// (recv ch var => body) or (recv ch => body) or (send ch val => body)
		if ast.SymEqStr(first, "recv") {
			if ast.IsCell(clause.Cdr) {
				chExpr := clause.Cdr.Car
				ch := Eval(chExpr, menv)
				// Check for optional variable: (recv ch var => body) or (recv ch => body)
				afterCh := clause.Cdr.Cdr
				var recvVar *ast.Value
				var arrowRest *ast.Value
				if ast.IsCell(afterCh) {
					if ast.SymEqStr(afterCh.Car, "=>") {
						// No variable: (recv ch => body)
						arrowRest = afterCh
					} else if ast.IsSym(afterCh.Car) {
						// Has variable: (recv ch var => body)
						recvVar = afterCh.Car
						arrowRest = afterCh.Cdr
					}
				}
				if ast.IsCell(arrowRest) && ast.SymEqStr(arrowRest.Car, "=>") {
					body := arrowRest.Cdr.Car
					cases = append(cases, selectCase{ch: ch, isSend: false, recvVar: recvVar, body: body})
				}
			}
		} else if ast.SymEqStr(first, "send") {
			if ast.IsCell(clause.Cdr) && ast.IsCell(clause.Cdr.Cdr) {
				chExpr := clause.Cdr.Car
				valExpr := clause.Cdr.Cdr.Car
				ch := Eval(chExpr, menv)
				val := Eval(valExpr, menv)
				// Find body after =>
				arrowRest := clause.Cdr.Cdr.Cdr
				if ast.IsCell(arrowRest) && ast.SymEqStr(arrowRest.Car, "=>") {
					body := arrowRest.Cdr.Car
					cases = append(cases, selectCase{ch: ch, isSend: true, sendVal: val, body: body})
				}
			}
		}

		rest = rest.Cdr
	}

	// Try each case without blocking
	for _, c := range cases {
		if !ast.IsChan(c.ch) {
			continue
		}

		if c.isSend {
			if ChanSend(c.ch, c.sendVal) {
				return Eval(c.body, menv)
			}
		} else {
			val, ok := ChanRecv(c.ch)
			if ok {
				// Bind received value to variable if specified
				bodyEnv := menv
				if c.recvVar != nil {
					bodyEnv = ast.NewMenv(
						EnvExtend(menv.Env, c.recvVar, val),
						menv.Parent,
						menv.Level,
						menv.CopyHandlers(),
					)
				}
				return Eval(c.body, bodyEnv)
			}
		}
	}

	// If no case ready and we have default, execute it
	if defaultBody != nil {
		return Eval(defaultBody, menv)
	}

	// No default, need to block - for now just return nil
	// Full implementation would park the goroutine
	return ast.Nil
}

// evalSetBang handles (set! var value)
// Mutates an existing variable binding in the current environment
func evalSetBang(args, menv *ast.Value) *ast.Value {
	if ast.IsNil(args) || ast.IsNil(args.Cdr) {
		return ast.NewError("set!: requires variable and value")
	}

	varSym := args.Car
	if !ast.IsSym(varSym) {
		return ast.NewError("set!: first argument must be a symbol")
	}

	// Evaluate the value
	val := Eval(args.Cdr.Car, menv)

	// OmniLisp: Handle dot notation for field mutation (set! obj.field value)
	if strings.Contains(varSym.Str, ".") {
		parts := strings.SplitN(varSym.Str, ".", 2)
		if len(parts) == 2 {
			objName := parts[0]
			fieldName := parts[1]

			// Look up the object
			objSym := ast.NewSym(objName)
			obj := EnvLookup(menv.Env, objSym)
			if obj == nil {
				obj = GlobalLookup(objSym)
			}
			if obj == nil {
				return ast.NewError(fmt.Sprintf("set!: unbound variable: %s", objName))
			}

			// Set the field
			if err := SetField(obj, fieldName, val); err != nil {
				return ast.NewError(fmt.Sprintf("set!: %s", err.Error()))
			}
			return val
		}
	}

	// Try to set in the current environment
	if EnvSet(menv.Env, varSym, val) {
		return val
	}

	// Try to set in global environment
	globalMutex.Lock()
	found := EnvSet(globalEnv, varSym, val)
	globalMutex.Unlock()
	if found {
		return val
	}

	// Variable not found - error
	return ast.NewError(fmt.Sprintf("set!: unbound variable: %s", varSym.Str))
}

// evalSetField implements (set-field! obj field value)
func evalSetField(args, menv *ast.Value) *ast.Value {
	if ast.IsNil(args) || ast.IsNil(args.Cdr) || ast.IsNil(args.Cdr.Cdr) {
		return ast.NewError("set-field!: requires object, field, and value")
	}

	obj := Eval(args.Car, menv)
	fieldSym := args.Cdr.Car
	val := Eval(args.Cdr.Cdr.Car, menv)

	var fieldName string
	if ast.IsSym(fieldSym) {
		fieldName = fieldSym.Str
	} else if ast.IsKeyword(fieldSym) {
		fieldName = fieldSym.Str
	} else {
		return ast.NewError("set-field!: field must be a symbol or keyword")
	}

	if err := SetField(obj, fieldName, val); err != nil {
		return ast.NewError(fmt.Sprintf("set-field!: %s", err.Error()))
	}
	return val
}

// evalDefine handles (define name value) or (define (name args...) body)
// Creates a binding in the global environment
// Also handles OmniLisp extensions:
//   - (define [method name Type...] (params) body) - method definition
//   - (define (name [x {Int}] [y {Int}]) body) - typed function (multiple dispatch)
func evalDefine(args, menv *ast.Value) *ast.Value {
	if ast.IsNil(args) {
		return ast.NewError("define: requires at least name")
	}

	first := args.Car

	// OmniLisp: Check for struct/enum/mutable type definition {struct ...} or {enum ...}
	if ast.IsTypeLit(first) {
		typeName := first.TypeName
		if typeName == "struct" || typeName == "mutable" {
			return evalDefineStruct(first, args.Cdr, menv)
		}
		if typeName == "enum" {
			return evalDefineEnum(first, args.Cdr, menv)
		}
	}

	// OmniLisp: Check for method definition [method name Type...]
	if ast.IsArray(first) && len(first.ArrayData) >= 2 {
		if ast.IsSym(first.ArrayData[0]) && first.ArrayData[0].Str == "method" {
			return evalDefineMethod(first, args.Cdr, menv)
		}
		// OmniLisp: Check for macro definition [macro name]
		if ast.IsSym(first.ArrayData[0]) && first.ArrayData[0].Str == "macro" {
			return evalDefineMacro(first, args.Cdr, menv)
		}
	}

	// Case 1: (define (name args...) body) - function shorthand
	if ast.IsCell(first) {
		name := first.Car
		if !ast.IsSym(name) {
			return ast.NewError("define: function name must be a symbol")
		}

		// OmniLisp: Check if any parameters have type annotations
		// If so, use multiple dispatch system
		if result := EvalDefineWithDispatch(first, args.Cdr, menv); result != nil {
			// Generate summary for interprocedural analysis
			if cg := codegen.GlobalCodeGenerator(); cg != nil {
				cg.AnalyzeFunction(name.Str, first.Cdr, args.Cdr.Car)
			}
			return result
		}

		params := first.Cdr
		body := args.Cdr.Car

		// Create a lambda
		lam := ast.NewLambda(params, body, menv.Env)
		GlobalDefine(name, lam)

		// Generate summary for interprocedural analysis
		if cg := codegen.GlobalCodeGenerator(); cg != nil {
			cg.AnalyzeFunction(name.Str, params, body)
		}
		return name
	}

	// Case 2: (define name value) - simple definition
	if !ast.IsSym(first) {
		return ast.NewError("define: first argument must be a symbol or (name args...)")
	}

	if ast.IsNil(args.Cdr) {
		return ast.NewError("define: requires a value")
	}

	val := Eval(args.Cdr.Car, menv)
	GlobalDefine(first, val)
	return first
}

// applyFn applies a function to arguments
func applyFn(fn *ast.Value, args *ast.Value, menv *ast.Value) *ast.Value {
	if fn == nil {
		return ast.Nil
	}

	if ast.IsPrim(fn) {
		return fn.Prim(args, menv)
	}

	// Handle continuation application
	if ast.IsCont(fn) {
		arg := ast.Nil
		if !ast.IsNil(args) && ast.IsCell(args) {
			arg = args.Car
		}
		return fn.ContFn(arg)
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

	// OmniLisp: Handle arrays - preserve structure
	if ast.IsArray(expr) {
		var items []*ast.Value
		for _, elem := range expr.ArrayData {
			items = append(items, evalQQ(elem, menv, depth))
		}
		return ast.NewArray(items)
	}

	// OmniLisp: Handle dicts - preserve structure
	if ast.IsDict(expr) {
		keys := make([]*ast.Value, len(expr.DictKeys))
		vals := make([]*ast.Value, len(expr.DictValues))
		for i := range expr.DictKeys {
			keys[i] = evalQQ(expr.DictKeys[i], menv, depth)
			vals[i] = evalQQ(expr.DictValues[i], menv, depth)
		}
		return ast.NewDict(keys, vals)
	}

	// OmniLisp: Handle tuples - preserve structure
	if ast.IsTuple(expr) {
		var items []*ast.Value
		for _, elem := range expr.TupleData {
			items = append(items, evalQQ(elem, menv, depth))
		}
		return ast.NewTuple(items)
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
// Creates constructor (mk-TypeName), accessors (TypeName-field), and predicate (TypeName?)
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
	var fieldNames []string // Keep field names in order for constructor
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

		// Check for :weak annotation
		strength := codegen.FieldStrong
		if !ast.IsNil(fieldDef.Cdr.Cdr) {
			annotation := fieldDef.Cdr.Cdr.Car
			if ast.IsSym(annotation) && annotation.Str == ":weak" {
				strength = codegen.FieldWeak
			}
		}

		// Determine if field is scannable (pointer type)
		isScannable := !isPrimitiveType(fieldType)

		fields = append(fields, codegen.TypeField{
			Name:        fieldName,
			Type:        fieldType,
			IsScannable: isScannable,
			Strength:    strength,
		})
		fieldNames = append(fieldNames, fieldName)

		rest = rest.Cdr
	}

	// Register type with global registry
	registry := codegen.GlobalRegistry()
	registry.RegisterType(typeName, fields)

	// Rebuild ownership graph and analyze back-edges
	registry.BuildOwnershipGraph()
	registry.AnalyzeBackEdges()

	// Create and register constructor: mk-TypeName
	constructorName := "mk-" + typeName
	constructor := createTypeConstructor(typeName, fieldNames)
	GlobalDefine(ast.NewSym(constructorName), constructor)

	// Create and register field accessors: TypeName-fieldName
	for _, fieldName := range fieldNames {
		accessorName := typeName + "-" + fieldName
		accessor := createFieldAccessor(typeName, fieldName)
		GlobalDefine(ast.NewSym(accessorName), accessor)

		// Also create setter: set-TypeName-fieldName!
		setterName := "set-" + typeName + "-" + fieldName + "!"
		setter := createFieldSetter(typeName, fieldName)
		GlobalDefine(ast.NewSym(setterName), setter)
	}

	// Create and register type predicate: TypeName?
	predicateName := typeName + "?"
	predicate := createTypePredicate(typeName)
	GlobalDefine(ast.NewSym(predicateName), predicate)

	// Return the type name as a symbol
	return typeNameVal
}

// createTypeConstructor creates a primitive that constructs instances of the type
func createTypeConstructor(typeName string, fieldNames []string) *ast.Value {
	// Capture typeName and fieldNames in closure
	tn := typeName
	fns := make([]string, len(fieldNames))
	copy(fns, fieldNames)

	return ast.NewPrim(func(args *ast.Value, menv *ast.Value) *ast.Value {
		// Collect field values from args
		fields := make(map[string]*ast.Value)
		argList := args
		for i, fn := range fns {
			if ast.IsNil(argList) {
				return ast.NewError(fmt.Sprintf("mk-%s: expected %d arguments, got %d", tn, len(fns), i))
			}
			fields[fn] = argList.Car
			argList = argList.Cdr
		}
		return ast.NewUserType(tn, fields, fns)
	})
}

// createFieldAccessor creates a primitive that accesses a field of the type
func createFieldAccessor(typeName, fieldName string) *ast.Value {
	tn := typeName
	fn := fieldName

	return ast.NewPrim(func(args *ast.Value, menv *ast.Value) *ast.Value {
		if ast.IsNil(args) {
			return ast.NewError(fmt.Sprintf("%s-%s: requires 1 argument", tn, fn))
		}
		v := args.Car
		if !ast.IsUserTypeOf(v, tn) {
			return ast.NewError(fmt.Sprintf("%s-%s: argument must be a %s", tn, fn, tn))
		}
		result := ast.UserTypeGetField(v, fn)
		if result == nil {
			return ast.Nil
		}
		return result
	})
}

// createFieldSetter creates a primitive that sets a field of the type
func createFieldSetter(typeName, fieldName string) *ast.Value {
	tn := typeName
	fn := fieldName

	return ast.NewPrim(func(args *ast.Value, menv *ast.Value) *ast.Value {
		if ast.IsNil(args) || ast.IsNil(args.Cdr) {
			return ast.NewError(fmt.Sprintf("set-%s-%s!: requires 2 arguments", tn, fn))
		}
		v := args.Car
		newVal := args.Cdr.Car
		if !ast.IsUserTypeOf(v, tn) {
			return ast.NewError(fmt.Sprintf("set-%s-%s!: first argument must be a %s", tn, fn, tn))
		}
		ast.UserTypeSetField(v, fn, newVal)
		return newVal
	})
}

// createTypePredicate creates a primitive that checks if a value is of the type
func createTypePredicate(typeName string) *ast.Value {
	tn := typeName

	return ast.NewPrim(func(args *ast.Value, menv *ast.Value) *ast.Value {
		if ast.IsNil(args) {
			return ast.NewError(fmt.Sprintf("%s?: requires 1 argument", tn))
		}
		v := args.Car
		if ast.IsUserTypeOf(v, tn) {
			return ast.NewSym("t") // true
		}
		return ast.Nil // false
	})
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
