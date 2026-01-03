// Package eval provides type system support for OmniLisp.
//
// This implements Julia-style optional types with multiple dispatch.
// Types are organized in a hierarchy with abstract types at the top
// and concrete types as leaves.
package eval

import (
	"fmt"
	"sync"

	"purple_go/pkg/ast"
)

// TypeKind represents the kind of type
type TypeKind int

const (
	TypeKindAbstract   TypeKind = iota // Abstract type (can't be instantiated)
	TypeKindConcrete                   // Concrete type (can be instantiated)
	TypeKindParametric                 // Parametric type (e.g., Array{T})
	TypeKindUnion                      // Union type (e.g., Int | Float)
	TypeKindBuiltin                    // Built-in type (Int, Float, etc.)
)

// TypeDef represents a type definition in the hierarchy
type TypeDef struct {
	Name       string      // Type name
	Kind       TypeKind    // Type kind
	Parent     string      // Parent type name (empty for root)
	Fields     []FieldDef  // Fields (for concrete types)
	TypeParams []string    // Type parameters (for parametric types)
	UnionTypes []string    // Union members (for union types)
	Mutable    bool        // Whether instances are mutable
}

// FieldDef represents a field in a struct/type
type FieldDef struct {
	Name     string // Field name
	TypeName string // Field type
	Default  *ast.Value // Default value (optional)
}

// TypeRegistry holds all type definitions and the type hierarchy
type TypeRegistry struct {
	mu       sync.RWMutex
	types    map[string]*TypeDef
	children map[string][]string // parent -> children mapping
}

// Global type registry
var globalTypeRegistry = NewTypeRegistry()

// NewTypeRegistry creates a new type registry with built-in types
func NewTypeRegistry() *TypeRegistry {
	tr := &TypeRegistry{
		types:    make(map[string]*TypeDef),
		children: make(map[string][]string),
	}
	tr.initBuiltinTypes()
	return tr
}

// GlobalTypeRegistry returns the global type registry
func GlobalTypeRegistry() *TypeRegistry {
	return globalTypeRegistry
}

// initBuiltinTypes initializes the built-in type hierarchy
func (tr *TypeRegistry) initBuiltinTypes() {
	// Root type: Any (all types are subtypes of Any)
	tr.types["Any"] = &TypeDef{Name: "Any", Kind: TypeKindAbstract}

	// Number hierarchy
	tr.types["Number"] = &TypeDef{Name: "Number", Kind: TypeKindAbstract, Parent: "Any"}
	tr.types["Integer"] = &TypeDef{Name: "Integer", Kind: TypeKindAbstract, Parent: "Number"}
	tr.types["Real"] = &TypeDef{Name: "Real", Kind: TypeKindAbstract, Parent: "Number"}

	// Concrete number types
	tr.types["Int"] = &TypeDef{Name: "Int", Kind: TypeKindBuiltin, Parent: "Integer"}
	tr.types["Float"] = &TypeDef{Name: "Float", Kind: TypeKindBuiltin, Parent: "Real"}

	// Collection hierarchy
	tr.types["Collection"] = &TypeDef{Name: "Collection", Kind: TypeKindAbstract, Parent: "Any"}
	tr.types["Sequence"] = &TypeDef{Name: "Sequence", Kind: TypeKindAbstract, Parent: "Collection"}

	// Concrete collection types
	tr.types["List"] = &TypeDef{Name: "List", Kind: TypeKindBuiltin, Parent: "Sequence"}
	tr.types["Array"] = &TypeDef{Name: "Array", Kind: TypeKindBuiltin, Parent: "Sequence", Mutable: true}
	tr.types["Dict"] = &TypeDef{Name: "Dict", Kind: TypeKindBuiltin, Parent: "Collection", Mutable: true}
	tr.types["Tuple"] = &TypeDef{Name: "Tuple", Kind: TypeKindBuiltin, Parent: "Sequence"}

	// Other built-in types
	tr.types["Symbol"] = &TypeDef{Name: "Symbol", Kind: TypeKindBuiltin, Parent: "Any"}
	tr.types["Keyword"] = &TypeDef{Name: "Keyword", Kind: TypeKindBuiltin, Parent: "Any"}
	tr.types["String"] = &TypeDef{Name: "String", Kind: TypeKindBuiltin, Parent: "Sequence"}
	tr.types["Char"] = &TypeDef{Name: "Char", Kind: TypeKindBuiltin, Parent: "Any"}
	tr.types["Bool"] = &TypeDef{Name: "Bool", Kind: TypeKindBuiltin, Parent: "Any"}
	tr.types["Nothing"] = &TypeDef{Name: "Nothing", Kind: TypeKindBuiltin, Parent: "Any"}
	tr.types["Nil"] = &TypeDef{Name: "Nil", Kind: TypeKindBuiltin, Parent: "Any"}

	// Function types
	tr.types["Function"] = &TypeDef{Name: "Function", Kind: TypeKindAbstract, Parent: "Any"}
	tr.types["Lambda"] = &TypeDef{Name: "Lambda", Kind: TypeKindBuiltin, Parent: "Function"}
	tr.types["Primitive"] = &TypeDef{Name: "Primitive", Kind: TypeKindBuiltin, Parent: "Function"}
	tr.types["Continuation"] = &TypeDef{Name: "Continuation", Kind: TypeKindBuiltin, Parent: "Function"}

	// Concurrency types
	tr.types["Channel"] = &TypeDef{Name: "Channel", Kind: TypeKindBuiltin, Parent: "Any"}
	tr.types["Thread"] = &TypeDef{Name: "Thread", Kind: TypeKindBuiltin, Parent: "Any"}
	tr.types["Atom"] = &TypeDef{Name: "Atom", Kind: TypeKindBuiltin, Parent: "Any"}

	// Type types
	tr.types["Type"] = &TypeDef{Name: "Type", Kind: TypeKindBuiltin, Parent: "Any"}

	// Build children map
	tr.rebuildChildrenMap()
}

// rebuildChildrenMap rebuilds the parent->children mapping
func (tr *TypeRegistry) rebuildChildrenMap() {
	tr.children = make(map[string][]string)
	for name, td := range tr.types {
		if td.Parent != "" {
			tr.children[td.Parent] = append(tr.children[td.Parent], name)
		}
	}
}

// DefineType defines a new type
func (tr *TypeRegistry) DefineType(name string, kind TypeKind, parent string, fields []FieldDef) error {
	tr.mu.Lock()
	defer tr.mu.Unlock()

	// Validate parent exists (unless this is a root type)
	if parent != "" {
		if _, ok := tr.types[parent]; !ok {
			return fmt.Errorf("unknown parent type: %s", parent)
		}
	}

	// Check for duplicate
	if _, ok := tr.types[name]; ok {
		return fmt.Errorf("type already defined: %s", name)
	}

	tr.types[name] = &TypeDef{
		Name:   name,
		Kind:   kind,
		Parent: parent,
		Fields: fields,
	}

	// Update children map
	if parent != "" {
		tr.children[parent] = append(tr.children[parent], name)
	}

	return nil
}

// DefineAbstract defines an abstract type
func (tr *TypeRegistry) DefineAbstract(name, parent string) error {
	return tr.DefineType(name, TypeKindAbstract, parent, nil)
}

// DefineStruct defines a concrete struct type
func (tr *TypeRegistry) DefineStruct(name, parent string, fields []FieldDef, mutable bool) error {
	tr.mu.Lock()
	defer tr.mu.Unlock()

	if parent == "" {
		parent = "Any"
	}

	if _, ok := tr.types[parent]; !ok {
		return fmt.Errorf("unknown parent type: %s", parent)
	}

	tr.types[name] = &TypeDef{
		Name:    name,
		Kind:    TypeKindConcrete,
		Parent:  parent,
		Fields:  fields,
		Mutable: mutable,
	}

	tr.children[parent] = append(tr.children[parent], name)
	return nil
}

// GetType returns a type definition by name
func (tr *TypeRegistry) GetType(name string) *TypeDef {
	tr.mu.RLock()
	defer tr.mu.RUnlock()
	return tr.types[name]
}

// IsSubtype checks if child is a subtype of parent
func (tr *TypeRegistry) IsSubtype(child, parent string) bool {
	tr.mu.RLock()
	defer tr.mu.RUnlock()

	// Same type
	if child == parent {
		return true
	}

	// Any is the top type
	if parent == "Any" {
		return true
	}

	// Walk up the hierarchy
	current := child
	for {
		td, ok := tr.types[current]
		if !ok {
			return false
		}
		if td.Parent == "" {
			return false
		}
		if td.Parent == parent {
			return true
		}
		current = td.Parent
	}
}

// CommonAncestor finds the most specific common ancestor of two types
func (tr *TypeRegistry) CommonAncestor(t1, t2 string) string {
	tr.mu.RLock()
	defer tr.mu.RUnlock()

	// Get ancestors of t1
	ancestors := make(map[string]int) // type -> depth
	current := t1
	depth := 0
	for current != "" {
		ancestors[current] = depth
		td := tr.types[current]
		if td == nil {
			break
		}
		current = td.Parent
		depth++
	}

	// Walk up t2's hierarchy looking for first match
	current = t2
	for current != "" {
		if _, ok := ancestors[current]; ok {
			return current
		}
		td := tr.types[current]
		if td == nil {
			break
		}
		current = td.Parent
	}

	return "Any"
}

// TypeOf returns the type name of a value
func TypeOf(v *ast.Value) string {
	if v == nil || ast.IsNil(v) {
		return "Nil"
	}

	switch v.Tag {
	case ast.TInt:
		return "Int"
	case ast.TFloat:
		return "Float"
	case ast.TChar:
		return "Char"
	case ast.TSym:
		return "Symbol"
	case ast.TCell:
		return "List"
	case ast.TArray:
		return "Array"
	case ast.TDict:
		return "Dict"
	case ast.TTuple:
		return "Tuple"
	case ast.TKeyword:
		return "Keyword"
	case ast.TNothing:
		return "Nothing"
	case ast.TTypeLit:
		return "Type"
	case ast.TLambda, ast.TRecLambda:
		return "Lambda"
	case ast.TPrim:
		return "Primitive"
	case ast.TCont:
		return "Continuation"
	case ast.TChan, ast.TGreenChan:
		return "Channel"
	case ast.TThread:
		return "Thread"
	case ast.TAtom:
		return "Atom"
	case ast.TUserType:
		return v.TypeName
	case ast.TCode:
		return "Code"
	case ast.TError:
		return "Error"
	default:
		return "Any"
	}
}

// TypeMatches checks if a value matches a type (including supertypes)
func TypeMatches(v *ast.Value, typeName string) bool {
	valueType := TypeOf(v)
	return globalTypeRegistry.IsSubtype(valueType, typeName)
}

// TypeSignature represents a method signature for dispatch
type TypeSignature struct {
	ParamTypes []string // Types of each parameter
}

// NewTypeSignature creates a type signature from type names
func NewTypeSignature(types ...string) TypeSignature {
	return TypeSignature{ParamTypes: types}
}

// Key returns a string key for the signature (for map storage)
func (ts TypeSignature) Key() string {
	if len(ts.ParamTypes) == 0 {
		return "()"
	}
	key := ts.ParamTypes[0]
	for i := 1; i < len(ts.ParamTypes); i++ {
		key += "," + ts.ParamTypes[i]
	}
	return key
}

// Matches checks if values match this signature
func (ts TypeSignature) Matches(args []*ast.Value) bool {
	if len(args) != len(ts.ParamTypes) {
		return false
	}
	for i, arg := range args {
		if !TypeMatches(arg, ts.ParamTypes[i]) {
			return false
		}
	}
	return true
}

// Specificity returns a specificity score (higher = more specific)
// Used for sorting applicable methods
func (ts TypeSignature) Specificity() int {
	score := 0
	for _, t := range ts.ParamTypes {
		score += typeSpecificity(t)
	}
	return score
}

// typeSpecificity returns specificity score for a single type
// Concrete types are more specific than abstract types
func typeSpecificity(typeName string) int {
	td := globalTypeRegistry.GetType(typeName)
	if td == nil {
		return 0
	}

	// Count depth in hierarchy (deeper = more specific)
	depth := 0
	current := typeName
	for current != "" {
		depth++
		td := globalTypeRegistry.types[current]
		if td == nil {
			break
		}
		current = td.Parent
	}

	// Concrete types get bonus
	if td.Kind == TypeKindConcrete || td.Kind == TypeKindBuiltin {
		depth += 10
	}

	return depth
}

// CompareSpecificity compares two signatures for the same arguments
// Returns: -1 if ts1 is more specific, 1 if ts2 is more specific, 0 if equal
func (ts TypeSignature) CompareSpecificity(ts2 TypeSignature) int {
	if len(ts.ParamTypes) != len(ts2.ParamTypes) {
		return 0
	}

	ts1More := false
	ts2More := false

	for i := range ts.ParamTypes {
		t1 := ts.ParamTypes[i]
		t2 := ts2.ParamTypes[i]

		if t1 == t2 {
			continue
		}

		if globalTypeRegistry.IsSubtype(t1, t2) {
			ts1More = true
		} else if globalTypeRegistry.IsSubtype(t2, t1) {
			ts2More = true
		}
	}

	if ts1More && !ts2More {
		return -1
	}
	if ts2More && !ts1More {
		return 1
	}
	return 0
}

// ParseTypeAnnotation parses a type annotation value to type name
func ParseTypeAnnotation(v *ast.Value) string {
	if v == nil {
		return "Any"
	}

	if ast.IsTypeLit(v) {
		// Simple type: {Int}
		if len(v.TypeParams) == 0 {
			return v.TypeName
		}
		// Parametric type: {Array Int} - for now just use base type
		return v.TypeName
	}

	if ast.IsSym(v) {
		return v.Str
	}

	return "Any"
}

// ExtractParamTypes extracts type annotations from parameter list
// Returns parallel arrays of param names and their types
// For OmniLisp: [x {Int}] means x has type Int
func ExtractParamTypes(params *ast.Value) (names []string, types []string) {
	if ast.IsNil(params) {
		return nil, nil
	}

	for p := params; !ast.IsNil(p) && ast.IsCell(p); p = p.Cdr {
		param := p.Car

		// Simple symbol: x (no type annotation)
		if ast.IsSym(param) {
			names = append(names, param.Str)
			types = append(types, "Any")
			continue
		}

		// Array: [x {Int}] or [x {Int} default]
		if ast.IsArray(param) && len(param.ArrayData) >= 1 {
			if ast.IsSym(param.ArrayData[0]) {
				names = append(names, param.ArrayData[0].Str)
				if len(param.ArrayData) >= 2 && ast.IsTypeLit(param.ArrayData[1]) {
					types = append(types, ParseTypeAnnotation(param.ArrayData[1]))
				} else {
					types = append(types, "Any")
				}
				continue
			}
		}

		// Cell (legacy syntax): (x Int)
		if ast.IsCell(param) && ast.IsSym(param.Car) {
			names = append(names, param.Car.Str)
			if !ast.IsNil(param.Cdr) && ast.IsCell(param.Cdr) {
				typeVal := param.Cdr.Car
				if ast.IsSym(typeVal) {
					types = append(types, typeVal.Str)
				} else {
					types = append(types, ParseTypeAnnotation(typeVal))
				}
			} else {
				types = append(types, "Any")
			}
			continue
		}
	}

	return names, types
}
