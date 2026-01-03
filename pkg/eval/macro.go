// Package eval provides hygienic macro system for OmniLisp.
//
// OmniLisp macros use syntax objects to preserve lexical context
// and ensure hygiene. The syntax:
//
//   (define [macro name] (params...)
//     #'(template...))
//
// Where:
//   #'   = syntax-quote (preserves lexical context)
//   ~    = unquote (evaluate and insert)
//   ~@   = unquote-splicing (evaluate and splice list)
//   ~!   = unsyntax (escape to raw AST)
package eval

import (
	"fmt"
	"sync"

	"purple_go/pkg/ast"
)

// SyntaxObject wraps a datum with lexical context
type SyntaxObject struct {
	Datum    *ast.Value     // The underlying value
	Context  *MacroContext  // Lexical context
	Marks    []int          // Hygiene marks
	Source   *SourceLoc     // Source location (if available)
}

// MacroContext holds lexical context for macro expansion
type MacroContext struct {
	DefinitionEnv *ast.Value // Environment where macro was defined
	UseEnv        *ast.Value // Environment where macro is used
	Phase         int        // Expansion phase (0 = runtime, 1+ = compile-time)
}

// SourceLoc holds source location information
type SourceLoc struct {
	File   string
	Line   int
	Column int
}

// HygienicMacro represents a hygienic macro definition
type HygienicMacro struct {
	Name       string
	Params     []string
	Body       *ast.Value
	DefEnv     *ast.Value // Definition environment
	Marks      []int      // Marks applied at definition time
}

// Global hygienic macro table
var (
	hygienicMacros = make(map[string]*HygienicMacro)
	hygienicMutex  sync.RWMutex
	markCounter    int
)

// nextMark generates a unique mark for hygiene tracking
func nextMark() int {
	markCounter++
	return markCounter
}

// DefineHygienicMacro defines a new hygienic macro
func DefineHygienicMacro(name string, params []string, body, defEnv *ast.Value) *HygienicMacro {
	hygienicMutex.Lock()
	defer hygienicMutex.Unlock()

	macro := &HygienicMacro{
		Name:   name,
		Params: params,
		Body:   body,
		DefEnv: defEnv,
		Marks:  []int{nextMark()},
	}
	hygienicMacros[name] = macro
	return macro
}

// GetHygienicMacro retrieves a hygienic macro by name
func GetHygienicMacro(name string) *HygienicMacro {
	hygienicMutex.RLock()
	defer hygienicMutex.RUnlock()
	return hygienicMacros[name]
}

// ClearHygienicMacros clears all hygienic macros (for testing)
func ClearHygienicMacros() {
	hygienicMutex.Lock()
	defer hygienicMutex.Unlock()
	hygienicMacros = make(map[string]*HygienicMacro)
	markCounter = 0
}

// ExpandHygienicMacro expands a hygienic macro call
func ExpandHygienicMacro(macro *HygienicMacro, args []*ast.Value, useMenv *ast.Value) *ast.Value {
	// Create macro context
	ctx := &MacroContext{
		DefinitionEnv: macro.DefEnv,
		UseEnv:        useMenv.Env,
		Phase:         0,
	}

	// Bind parameters to arguments
	bindings := make(map[string]*ast.Value)
	for i, param := range macro.Params {
		if i < len(args) {
			bindings[param] = args[i]
		} else {
			bindings[param] = ast.Nil
		}
	}

	// Expand the body with hygiene
	expanded := expandSyntaxQuote(macro.Body, bindings, ctx, macro.Marks)

	// Apply use-site mark
	useMark := nextMark()
	expanded = applyMark(expanded, useMark)

	return expanded
}

// expandSyntaxQuote expands a syntax-quoted expression
func expandSyntaxQuote(expr *ast.Value, bindings map[string]*ast.Value, ctx *MacroContext, marks []int) *ast.Value {
	if expr == nil || ast.IsNil(expr) {
		return ast.Nil
	}

	// Check for syntax-quote marker (handled during parsing as special form)
	if ast.IsCell(expr) {
		head := expr.Car

		// #' or syntax-quote
		if ast.SymEqStr(head, "syntax-quote") || ast.SymEqStr(head, "#'") {
			return expandSyntaxQuote(expr.Cdr.Car, bindings, ctx, marks)
		}

		// ~ or unquote
		if ast.SymEqStr(head, "unquote") || ast.SymEqStr(head, "~") {
			// Evaluate unquoted expression
			inner := expr.Cdr.Car
			return substituteInTemplate(inner, bindings, ctx)
		}

		// ~@ or unquote-splicing
		if ast.SymEqStr(head, "unquote-splicing") || ast.SymEqStr(head, "~@") {
			// Return a marker for splicing (handled by list processing)
			inner := expr.Cdr.Car
			result := substituteInTemplate(inner, bindings, ctx)
			return ast.List2(ast.NewSym("__splice__"), result)
		}

		// ~! or unsyntax (raw AST escape)
		if ast.SymEqStr(head, "unsyntax") || ast.SymEqStr(head, "~!") {
			// Direct substitution without hygiene
			return expr.Cdr.Car
		}

		// Regular list - expand each element
		return expandSyntaxQuoteList(expr, bindings, ctx, marks)
	}

	// Symbol - check if it's a bound parameter
	if ast.IsSym(expr) {
		if val, ok := bindings[expr.Str]; ok {
			return val
		}
		// Apply hygiene marks to free variables
		return applyMarksToSymbol(expr, marks)
	}

	// Array - expand each element
	if ast.IsArray(expr) {
		elements := make([]*ast.Value, len(expr.ArrayData))
		for i, elem := range expr.ArrayData {
			elements[i] = expandSyntaxQuote(elem, bindings, ctx, marks)
		}
		return ast.NewArray(elements)
	}

	// Literals are returned as-is
	return expr
}

// expandSyntaxQuoteList expands a syntax-quoted list
func expandSyntaxQuoteList(list *ast.Value, bindings map[string]*ast.Value, ctx *MacroContext, marks []int) *ast.Value {
	if ast.IsNil(list) {
		return ast.Nil
	}
	if !ast.IsCell(list) {
		return expandSyntaxQuote(list, bindings, ctx, marks)
	}

	// Build result list, handling splicing
	var result []*ast.Value
	for !ast.IsNil(list) && ast.IsCell(list) {
		elem := list.Car
		expanded := expandSyntaxQuote(elem, bindings, ctx, marks)

		// Check for splice marker
		if ast.IsCell(expanded) && ast.SymEqStr(expanded.Car, "__splice__") {
			// Splice the list
			spliced := expanded.Cdr.Car
			if ast.IsCell(spliced) {
				for !ast.IsNil(spliced) && ast.IsCell(spliced) {
					result = append(result, spliced.Car)
					spliced = spliced.Cdr
				}
			} else if ast.IsArray(spliced) {
				result = append(result, spliced.ArrayData...)
			} else if !ast.IsNil(spliced) {
				result = append(result, spliced)
			}
		} else {
			result = append(result, expanded)
		}

		list = list.Cdr
	}

	// Convert slice back to list
	return sliceToList(result)
}

// substituteInTemplate substitutes bindings in an unquoted expression
func substituteInTemplate(expr *ast.Value, bindings map[string]*ast.Value, ctx *MacroContext) *ast.Value {
	if expr == nil || ast.IsNil(expr) {
		return ast.Nil
	}

	if ast.IsSym(expr) {
		if val, ok := bindings[expr.Str]; ok {
			return val
		}
		return expr
	}

	if ast.IsCell(expr) {
		car := substituteInTemplate(expr.Car, bindings, ctx)
		cdr := substituteInTemplate(expr.Cdr, bindings, ctx)
		return ast.NewCell(car, cdr)
	}

	if ast.IsArray(expr) {
		elements := make([]*ast.Value, len(expr.ArrayData))
		for i, elem := range expr.ArrayData {
			elements[i] = substituteInTemplate(elem, bindings, ctx)
		}
		return ast.NewArray(elements)
	}

	return expr
}

// applyMark applies a hygiene mark to an expression
func applyMark(expr *ast.Value, mark int) *ast.Value {
	// For now, marking is a no-op since we use gensym for unique names
	// A full implementation would track marks on syntax objects
	return expr
}

// applyMarksToSymbol creates a marked symbol for hygiene
func applyMarksToSymbol(sym *ast.Value, marks []int) *ast.Value {
	if len(marks) == 0 {
		return sym
	}
	// Generate a unique name based on marks
	// This is a simplified approach - a full implementation would
	// track marks in syntax objects
	return ast.NewSym(fmt.Sprintf("%s#%d", sym.Str, marks[len(marks)-1]))
}

// sliceToList converts a slice to a proper list
func sliceToList(items []*ast.Value) *ast.Value {
	if len(items) == 0 {
		return ast.Nil
	}
	result := ast.Nil
	for i := len(items) - 1; i >= 0; i-- {
		result = ast.NewCell(items[i], result)
	}
	return result
}

// evalDefineMacro handles (define [macro name] (params...) body)
// Defines a hygienic macro
func evalDefineMacro(macroSpec *ast.Value, paramsAndBody *ast.Value, menv *ast.Value) *ast.Value {
	if !ast.IsArray(macroSpec) || len(macroSpec.ArrayData) < 2 {
		return ast.NewError("define macro: invalid macro specification")
	}

	// macroSpec is [macro name]
	name := macroSpec.ArrayData[1]
	if !ast.IsSym(name) {
		return ast.NewError("define macro: name must be a symbol")
	}

	// Get parameter list
	params := paramsAndBody.Car
	body := paramsAndBody.Cdr.Car

	var paramNames []string
	for p := params; !ast.IsNil(p) && ast.IsCell(p); p = p.Cdr {
		if ast.IsSym(p.Car) {
			paramNames = append(paramNames, p.Car.Str)
		}
	}

	// Define the hygienic macro
	macro := DefineHygienicMacro(name.Str, paramNames, body, menv.Env)

	// Also register with the legacy macro system for compatibility
	DefineMacro(name.Str, paramNames, body, menv)

	// Create a primitive that expands the macro when called
	macroFn := ast.NewPrim(func(args *ast.Value, callMenv *ast.Value) *ast.Value {
		// Collect arguments (unevaluated)
		var argsList []*ast.Value
		a := args
		for !ast.IsNil(a) && ast.IsCell(a) {
			argsList = append(argsList, a.Car)
			a = a.Cdr
		}

		// Expand the macro
		expanded := ExpandHygienicMacro(macro, argsList, callMenv)

		// Evaluate the expansion
		return Eval(expanded, callMenv)
	})

	// Register in global environment
	GlobalDefine(name, macroFn)

	return name
}

// evalSyntaxQuote handles #'expr (syntax-quote)
// Creates a syntax object with lexical context
func evalSyntaxQuote(args *ast.Value, menv *ast.Value) *ast.Value {
	if ast.IsNil(args) {
		return ast.Nil
	}

	expr := args.Car

	// Create empty bindings (no pattern variables)
	bindings := make(map[string]*ast.Value)

	// Create context
	ctx := &MacroContext{
		DefinitionEnv: menv.Env,
		UseEnv:        menv.Env,
		Phase:         0,
	}

	// Expand with no marks
	return expandSyntaxQuote(expr, bindings, ctx, nil)
}

// AddMacroSpecialForms adds macro-related special forms to eval
// This should be called from init or the main Eval function
func AddMacroSpecialForms() {
	// These are handled directly in Eval:
	// - syntax-quote / #'
	// - unquote / ~
	// - unquote-splicing / ~@
	// - unsyntax / ~!
}
