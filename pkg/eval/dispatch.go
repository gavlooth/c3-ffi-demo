// Package eval provides multiple dispatch for OmniLisp.
//
// Multiple dispatch selects the most specific method based on the
// runtime types of ALL arguments, not just the first one.
// This is Julia's approach to polymorphism.
package eval

import (
	"fmt"
	"sort"
	"sync"

	"purple_go/pkg/ast"
)

// Method represents a single method implementation
type Method struct {
	Signature TypeSignature // Parameter types
	ParamNames []string     // Parameter names
	Body      *ast.Value    // Method body
	Env       *ast.Value    // Closure environment
}

// GenericFunction represents a function with multiple methods
type GenericFunction struct {
	Name    string
	Methods []*Method
	mu      sync.RWMutex
}

// GenericRegistry holds all generic functions
type GenericRegistry struct {
	mu       sync.RWMutex
	generics map[string]*GenericFunction
}

// Global generic function registry
var globalGenericRegistry = NewGenericRegistry()

// NewGenericRegistry creates a new generic function registry
func NewGenericRegistry() *GenericRegistry {
	return &GenericRegistry{
		generics: make(map[string]*GenericFunction),
	}
}

// GlobalGenericRegistry returns the global registry
func GlobalGenericRegistry() *GenericRegistry {
	return globalGenericRegistry
}

// DefineGeneric creates or gets a generic function
func (gr *GenericRegistry) DefineGeneric(name string) *GenericFunction {
	gr.mu.Lock()
	defer gr.mu.Unlock()

	if gf, ok := gr.generics[name]; ok {
		return gf
	}

	gf := &GenericFunction{
		Name:    name,
		Methods: nil,
	}
	gr.generics[name] = gf
	return gf
}

// GetGeneric returns a generic function by name
func (gr *GenericRegistry) GetGeneric(name string) *GenericFunction {
	gr.mu.RLock()
	defer gr.mu.RUnlock()
	return gr.generics[name]
}

// IsGeneric checks if a name refers to a generic function
func (gr *GenericRegistry) IsGeneric(name string) bool {
	gr.mu.RLock()
	defer gr.mu.RUnlock()
	_, ok := gr.generics[name]
	return ok
}

// AddMethod adds a method to a generic function
func (gf *GenericFunction) AddMethod(sig TypeSignature, paramNames []string, body, env *ast.Value) {
	gf.mu.Lock()
	defer gf.mu.Unlock()

	// Check if method with same signature exists (override)
	sigKey := sig.Key()
	for i, m := range gf.Methods {
		if m.Signature.Key() == sigKey {
			// Replace existing method
			gf.Methods[i] = &Method{
				Signature:  sig,
				ParamNames: paramNames,
				Body:       body,
				Env:        env,
			}
			return
		}
	}

	// Add new method
	gf.Methods = append(gf.Methods, &Method{
		Signature:  sig,
		ParamNames: paramNames,
		Body:       body,
		Env:        env,
	})
}

// Dispatch finds and calls the most specific applicable method
func (gf *GenericFunction) Dispatch(args []*ast.Value, menv *ast.Value) *ast.Value {
	gf.mu.RLock()
	defer gf.mu.RUnlock()

	// Find applicable methods
	applicable := gf.findApplicable(args)

	if len(applicable) == 0 {
		return ast.NewError(fmt.Sprintf("no applicable method for %s with types %v",
			gf.Name, argsToTypes(args)))
	}

	// Sort by specificity (most specific first)
	sort.Slice(applicable, func(i, j int) bool {
		cmp := applicable[i].Signature.CompareSpecificity(applicable[j].Signature)
		if cmp != 0 {
			return cmp < 0
		}
		// Fall back to total specificity score
		return applicable[i].Signature.Specificity() > applicable[j].Signature.Specificity()
	})

	// Check for ambiguity (top two have equal specificity)
	if len(applicable) > 1 {
		cmp := applicable[0].Signature.CompareSpecificity(applicable[1].Signature)
		if cmp == 0 && applicable[0].Signature.Specificity() == applicable[1].Signature.Specificity() {
			return ast.NewError(fmt.Sprintf("ambiguous method call for %s with types %v",
				gf.Name, argsToTypes(args)))
		}
	}

	// Call the most specific method
	return gf.callMethod(applicable[0], args, menv)
}

// findApplicable returns all methods that match the given arguments
func (gf *GenericFunction) findApplicable(args []*ast.Value) []*Method {
	var result []*Method
	for _, m := range gf.Methods {
		if m.Signature.Matches(args) {
			result = append(result, m)
		}
	}
	return result
}

// callMethod invokes a method with the given arguments
func (gf *GenericFunction) callMethod(m *Method, args []*ast.Value, menv *ast.Value) *ast.Value {
	// Extend environment with parameter bindings
	newEnv := m.Env
	for i, name := range m.ParamNames {
		if i < len(args) {
			newEnv = EnvExtend(newEnv, ast.NewSym(name), args[i])
		}
	}

	// Create body menv
	bodyMenv := ast.NewMenv(newEnv, menv.Parent, menv.Level, menv.CopyHandlers())

	// Evaluate body
	return Eval(m.Body, bodyMenv)
}

// argsToTypes returns the type names of a list of arguments
func argsToTypes(args []*ast.Value) []string {
	types := make([]string, len(args))
	for i, arg := range args {
		types[i] = TypeOf(arg)
	}
	return types
}

// MethodCount returns the number of methods
func (gf *GenericFunction) MethodCount() int {
	gf.mu.RLock()
	defer gf.mu.RUnlock()
	return len(gf.Methods)
}

// DefineMethod is a helper to define a method on a generic function
func DefineMethod(name string, paramTypes []string, paramNames []string, body, env *ast.Value) {
	gr := GlobalGenericRegistry()
	gf := gr.DefineGeneric(name)
	sig := NewTypeSignature(paramTypes...)
	gf.AddMethod(sig, paramNames, body, env)
}

// DispatchCall attempts to dispatch a function call through the generic system
// Returns nil if the function is not a generic, allowing fallback to normal call
func DispatchCall(name string, args []*ast.Value, menv *ast.Value) *ast.Value {
	gr := GlobalGenericRegistry()
	gf := gr.GetGeneric(name)
	if gf == nil {
		return nil // Not a generic, use normal dispatch
	}
	return gf.Dispatch(args, menv)
}

// GenericValue wraps a generic function as an ast.Value (for first-class functions)
type GenericValue struct {
	GF *GenericFunction
}

// NewGenericPrim creates a primitive that dispatches to a generic function
func NewGenericPrim(name string) *ast.Value {
	return ast.NewPrim(func(args *ast.Value, menv *ast.Value) *ast.Value {
		// Convert args list to slice
		var argSlice []*ast.Value
		for a := args; !ast.IsNil(a) && ast.IsCell(a); a = a.Cdr {
			argSlice = append(argSlice, a.Car)
		}

		// Dispatch
		result := DispatchCall(name, argSlice, menv)
		if result == nil {
			return ast.NewError(fmt.Sprintf("no generic function named: %s", name))
		}
		return result
	})
}

// evalDefineMethod handles (define [method name Type1 Type2 ...] (params) body)
// This is the OmniLisp syntax for defining methods on generic functions
func evalDefineMethod(methodSpec *ast.Value, paramsAndBody *ast.Value, menv *ast.Value) *ast.Value {
	if !ast.IsArray(methodSpec) || len(methodSpec.ArrayData) < 2 {
		return ast.NewError("define method: invalid method specification")
	}

	// methodSpec is [method name Type1 Type2 ...]
	name := methodSpec.ArrayData[1]
	if !ast.IsSym(name) {
		return ast.NewError("define method: name must be a symbol")
	}

	// Extract type constraints from method spec
	var paramTypes []string
	for i := 2; i < len(methodSpec.ArrayData); i++ {
		typeVal := methodSpec.ArrayData[i]
		paramTypes = append(paramTypes, ParseTypeAnnotation(typeVal))
	}

	// Get parameter names from params
	params := paramsAndBody.Car
	body := paramsAndBody.Cdr.Car

	var paramNames []string
	for p := params; !ast.IsNil(p) && ast.IsCell(p); p = p.Cdr {
		param := p.Car
		if ast.IsSym(param) {
			paramNames = append(paramNames, param.Str)
		} else if ast.IsArray(param) && len(param.ArrayData) >= 1 {
			if ast.IsSym(param.ArrayData[0]) {
				paramNames = append(paramNames, param.ArrayData[0].Str)
			}
		}
	}

	// If no types specified in method spec, extract from params
	if len(paramTypes) == 0 {
		_, paramTypes = ExtractParamTypes(params)
	}

	// Ensure we have types for all params
	for len(paramTypes) < len(paramNames) {
		paramTypes = append(paramTypes, "Any")
	}

	// Define the method
	DefineMethod(name.Str, paramTypes, paramNames, body, menv.Env)

	// Also register a global binding that dispatches to the generic
	// (only if not already defined)
	if GlobalLookup(name) == nil {
		GlobalDefine(name, NewGenericPrim(name.Str))
	}

	return name
}

// EvalDefineWithDispatch handles define forms that may create generic functions
// This is called from evalDefine for typed function definitions
func EvalDefineWithDispatch(first *ast.Value, rest *ast.Value, menv *ast.Value) *ast.Value {
	// Check if this is a typed function definition
	// (define (name [x {Int}] [y {Int}]) body)

	if !ast.IsCell(first) {
		return nil // Not a function definition
	}

	name := first.Car
	if !ast.IsSym(name) {
		return nil
	}

	params := first.Cdr
	body := rest.Car

	// Extract parameter names and types
	paramNames, paramTypes := ExtractParamTypes(params)

	// Check if any parameter has a non-Any type
	hasTypes := false
	for _, t := range paramTypes {
		if t != "Any" {
			hasTypes = true
			break
		}
	}

	if !hasTypes {
		return nil // No type annotations, use regular define
	}

	// This is a typed function - create/extend generic function
	DefineMethod(name.Str, paramTypes, paramNames, body, menv.Env)

	// Register global binding if not already
	if GlobalLookup(name) == nil {
		GlobalDefine(name, NewGenericPrim(name.Str))
	}

	return name
}

// ClearGenerics clears all generic functions (for testing)
func ClearGenerics() {
	globalGenericRegistry.mu.Lock()
	defer globalGenericRegistry.mu.Unlock()
	globalGenericRegistry.generics = make(map[string]*GenericFunction)
}
