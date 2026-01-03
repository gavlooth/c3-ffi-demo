// Package eval provides struct and enum definitions for OmniLisp.
//
// Struct syntax:
//   (define {struct Point} [x {Float}] [y {Float}])
//   (define {struct Circle :extends Shape} [center {Point}] [radius {Float}])
//   (define {struct [Pair T]} [first {T}] [second {T}])
//   (define {struct [Pair T] :extends Collection} [first {T}] [second {T}])
//
// Field modifiers:
//   [field {Type}]              - immutable field
//   [field :mutable {Type}]     - mutable field
//   [field {Type} default]      - field with default value
//
// Mutable struct sugar:
//   (define {mutable Player} ...) - all fields mutable
//
// Construction:
//   (Point 1.0 2.0)                    - positional
//   (Point 1.0 & :y 2.0)               - mixed positional + named
//   (Point & :x 1.0 :y 2.0)            - all named
package eval

import (
	"fmt"
	"sync"

	"purple_go/pkg/ast"
)

// StructField represents a field in a struct definition
type StructField struct {
	Name     string     // Field name
	Type     string     // Type name (or type param like "T")
	Mutable  bool       // Whether field is mutable
	Default  *ast.Value // Default value (nil if required)
}

// StructDef represents a struct type definition
type StructDef struct {
	Name       string         // Struct name
	Parent     string         // Parent type (default "Any")
	TypeParams []string       // Type parameters (e.g., ["T", "K"])
	Fields     []StructField  // Field definitions
	AllMutable bool           // True if defined with {mutable ...}
}

// EnumVariant represents a variant in an enum definition
type EnumVariant struct {
	Name   string        // Variant name (e.g., "Some", "None")
	Fields []StructField // Fields for this variant (empty for simple variants)
}

// EnumDef represents an enum type definition
type EnumDef struct {
	Name       string        // Enum name
	TypeParams []string      // Type parameters
	Variants   []EnumVariant // Variant definitions
}

// StructInstance represents an instance of a struct
type StructInstance struct {
	TypeName string                 // Struct type name
	Fields   map[string]*ast.Value  // Field values
	Mutable  map[string]bool        // Which fields are mutable
}

// StructRegistry holds all struct and enum definitions
type StructRegistry struct {
	mu      sync.RWMutex
	structs map[string]*StructDef
	enums   map[string]*EnumDef
	// Variant to enum mapping for smart qualification
	variantToEnum map[string][]string
}

// Global struct registry
var globalStructRegistry = NewStructRegistry()

// NewStructRegistry creates a new struct registry
func NewStructRegistry() *StructRegistry {
	return &StructRegistry{
		structs:       make(map[string]*StructDef),
		enums:         make(map[string]*EnumDef),
		variantToEnum: make(map[string][]string),
	}
}

// GlobalStructRegistry returns the global registry
func GlobalStructRegistry() *StructRegistry {
	return globalStructRegistry
}

// DefineStruct registers a struct definition
func (sr *StructRegistry) DefineStruct(def *StructDef) {
	sr.mu.Lock()
	defer sr.mu.Unlock()
	sr.structs[def.Name] = def
}

// GetStruct returns a struct definition by name
func (sr *StructRegistry) GetStruct(name string) *StructDef {
	sr.mu.RLock()
	defer sr.mu.RUnlock()
	return sr.structs[name]
}

// DefineEnum registers an enum definition
func (sr *StructRegistry) DefineEnum(def *EnumDef) {
	sr.mu.Lock()
	defer sr.mu.Unlock()
	sr.enums[def.Name] = def

	// Register variant -> enum mappings
	for _, v := range def.Variants {
		sr.variantToEnum[v.Name] = append(sr.variantToEnum[v.Name], def.Name)
	}
}

// GetEnum returns an enum definition by name
func (sr *StructRegistry) GetEnum(name string) *EnumDef {
	sr.mu.RLock()
	defer sr.mu.RUnlock()
	return sr.enums[name]
}

// LookupVariant finds which enum(s) define a variant
func (sr *StructRegistry) LookupVariant(name string) []string {
	sr.mu.RLock()
	defer sr.mu.RUnlock()
	return sr.variantToEnum[name]
}

// IsVariantAmbiguous returns true if variant name is defined in multiple enums
func (sr *StructRegistry) IsVariantAmbiguous(name string) bool {
	enums := sr.LookupVariant(name)
	return len(enums) > 1
}

// ClearStructs clears all definitions (for testing)
func ClearStructs() {
	globalStructRegistry.mu.Lock()
	defer globalStructRegistry.mu.Unlock()
	globalStructRegistry.structs = make(map[string]*StructDef)
	globalStructRegistry.enums = make(map[string]*EnumDef)
	globalStructRegistry.variantToEnum = make(map[string][]string)
}

// evalDefineStruct handles (define {struct ...} fields...)
func evalDefineStruct(typeForm *ast.Value, fields *ast.Value, menv *ast.Value) *ast.Value {
	def := &StructDef{
		Parent: "Any",
	}

	// Parse the type form: {struct Name} or {struct [Name T] :extends Parent}
	if !ast.IsTypeLit(typeForm) {
		return ast.NewError("define struct: invalid type form")
	}

	// Check for mutable sugar
	if typeForm.TypeName == "mutable" {
		def.AllMutable = true
		// First type param should be the actual struct name
		if len(typeForm.TypeParams) < 1 {
			return ast.NewError("define mutable: requires struct name")
		}
		nameVal := typeForm.TypeParams[0]
		if ast.IsSym(nameVal) {
			def.Name = nameVal.Str
		} else if ast.IsTypeLit(nameVal) {
			def.Name = nameVal.TypeName
		} else {
			return ast.NewError("define mutable: invalid struct name")
		}
	} else if typeForm.TypeName == "struct" {
		// Parse struct header
		if len(typeForm.TypeParams) < 1 {
			return ast.NewError("define struct: requires struct name")
		}

		firstParam := typeForm.TypeParams[0]

		// Check if it's [Name T ...] (parametric) or just Name
		if ast.IsArray(firstParam) && len(firstParam.ArrayData) >= 1 {
			// Parametric: {struct [Pair T K]}
			if ast.IsSym(firstParam.ArrayData[0]) {
				def.Name = firstParam.ArrayData[0].Str
			}
			for i := 1; i < len(firstParam.ArrayData); i++ {
				if ast.IsSym(firstParam.ArrayData[i]) {
					def.TypeParams = append(def.TypeParams, firstParam.ArrayData[i].Str)
				}
			}
		} else if ast.IsSym(firstParam) {
			// Simple: {struct Point}
			def.Name = firstParam.Str
		} else if ast.IsTypeLit(firstParam) {
			def.Name = firstParam.TypeName
		} else {
			return ast.NewError("define struct: invalid struct name")
		}

		// Check for :extends
		for i := 1; i < len(typeForm.TypeParams); i++ {
			param := typeForm.TypeParams[i]
			if ast.IsKeyword(param) && param.Str == "extends" {
				if i+1 < len(typeForm.TypeParams) {
					parentVal := typeForm.TypeParams[i+1]
					if ast.IsSym(parentVal) {
						def.Parent = parentVal.Str
					} else if ast.IsTypeLit(parentVal) {
						def.Parent = parentVal.TypeName
					}
					i++ // Skip the parent value
				}
			}
		}
	} else {
		return ast.NewError("define struct: expected {struct ...} or {mutable ...}")
	}

	if def.Name == "" {
		return ast.NewError("define struct: missing struct name")
	}

	// Parse fields
	for f := fields; !ast.IsNil(f) && ast.IsCell(f); f = f.Cdr {
		field := f.Car
		sf, err := parseStructField(field, def.AllMutable)
		if err != nil {
			return ast.NewError(fmt.Sprintf("define struct %s: %s", def.Name, err.Error()))
		}
		def.Fields = append(def.Fields, sf)
	}

	// Register the struct
	GlobalStructRegistry().DefineStruct(def)

	// Register type in type registry
	GlobalTypeRegistry().DefineStruct(def.Name, def.Parent, nil, def.AllMutable)

	// Create constructor function
	constructorFn := makeStructConstructor(def)
	GlobalDefine(ast.NewSym(def.Name), constructorFn)

	return ast.NewSym(def.Name)
}

// parseStructField parses a field definition [name {Type}] or [name :mutable {Type}]
func parseStructField(field *ast.Value, allMutable bool) (StructField, error) {
	sf := StructField{
		Mutable: allMutable,
	}

	if !ast.IsArray(field) || len(field.ArrayData) < 1 {
		return sf, fmt.Errorf("invalid field definition")
	}

	arr := field.ArrayData
	idx := 0

	// First element is field name
	if !ast.IsSym(arr[idx]) {
		return sf, fmt.Errorf("field name must be a symbol")
	}
	sf.Name = arr[idx].Str
	idx++

	// Check for :mutable modifier
	if idx < len(arr) {
		if ast.IsKeyword(arr[idx]) && arr[idx].Str == "mutable" {
			sf.Mutable = true
			idx++
		}
	}

	// Next should be type
	if idx < len(arr) {
		if ast.IsTypeLit(arr[idx]) {
			sf.Type = arr[idx].TypeName
			idx++
		} else if ast.IsSym(arr[idx]) {
			// Could be a type parameter like T
			sf.Type = arr[idx].Str
			idx++
		}
	}

	// Optional default value
	if idx < len(arr) {
		sf.Default = arr[idx]
	}

	return sf, nil
}

// makeStructConstructor creates a constructor function for a struct
func makeStructConstructor(def *StructDef) *ast.Value {
	return ast.NewPrim(func(args *ast.Value, menv *ast.Value) *ast.Value {
		return constructStruct(def, args, menv)
	})
}

// constructStruct creates a struct instance from arguments
func constructStruct(def *StructDef, args *ast.Value, menv *ast.Value) *ast.Value {
	fieldValues := make(map[string]*ast.Value)
	fieldMutable := make(map[string]bool)

	// Initialize with defaults
	for _, f := range def.Fields {
		fieldMutable[f.Name] = f.Mutable || def.AllMutable
		if f.Default != nil {
			fieldValues[f.Name] = Eval(f.Default, menv)
		}
	}

	// Process arguments
	positionalIdx := 0
	inNamedSection := false

	for a := args; !ast.IsNil(a) && ast.IsCell(a); a = a.Cdr {
		arg := a.Car

		// Check for & separator
		if ast.IsSym(arg) && arg.Str == "&" {
			inNamedSection = true
			continue
		}

		if inNamedSection {
			// Named argument: :field value
			if ast.IsKeyword(arg) {
				fieldName := arg.Str
				a = a.Cdr
				if ast.IsNil(a) {
					return ast.NewError(fmt.Sprintf("%s: missing value for :%s", def.Name, fieldName))
				}
				fieldValues[fieldName] = a.Car
			} else {
				return ast.NewError(fmt.Sprintf("%s: expected keyword in named section", def.Name))
			}
		} else {
			// Positional argument
			if positionalIdx < len(def.Fields) {
				fieldValues[def.Fields[positionalIdx].Name] = arg
				positionalIdx++
			} else {
				return ast.NewError(fmt.Sprintf("%s: too many positional arguments", def.Name))
			}
		}
	}

	// Check all required fields are set
	for _, f := range def.Fields {
		if _, ok := fieldValues[f.Name]; !ok {
			return ast.NewError(fmt.Sprintf("%s: missing required field '%s'", def.Name, f.Name))
		}
	}

	// Create the struct instance as a user type
	fieldOrder := make([]string, len(def.Fields))
	for i, f := range def.Fields {
		fieldOrder[i] = f.Name
	}
	instance := ast.NewUserType(def.Name, fieldValues, fieldOrder)
	return instance
}

// evalDefineEnum handles (define {enum ...} variants...)
func evalDefineEnum(typeForm *ast.Value, variants *ast.Value, menv *ast.Value) *ast.Value {
	def := &EnumDef{}

	if !ast.IsTypeLit(typeForm) || typeForm.TypeName != "enum" {
		return ast.NewError("define enum: expected {enum ...}")
	}

	// Parse enum header: {enum Name} or {enum [Option T]}
	if len(typeForm.TypeParams) < 1 {
		return ast.NewError("define enum: requires enum name")
	}

	firstParam := typeForm.TypeParams[0]

	if ast.IsArray(firstParam) && len(firstParam.ArrayData) >= 1 {
		// Parametric: {enum [Option T]}
		if ast.IsSym(firstParam.ArrayData[0]) {
			def.Name = firstParam.ArrayData[0].Str
		}
		for i := 1; i < len(firstParam.ArrayData); i++ {
			if ast.IsSym(firstParam.ArrayData[i]) {
				def.TypeParams = append(def.TypeParams, firstParam.ArrayData[i].Str)
			}
		}
	} else if ast.IsSym(firstParam) {
		def.Name = firstParam.Str
	} else {
		return ast.NewError("define enum: invalid enum name")
	}

	// Parse variants
	for v := variants; !ast.IsNil(v) && ast.IsCell(v); v = v.Cdr {
		variant := v.Car
		ev, err := parseEnumVariant(variant)
		if err != nil {
			return ast.NewError(fmt.Sprintf("define enum %s: %s", def.Name, err.Error()))
		}
		def.Variants = append(def.Variants, ev)
	}

	// Register the enum
	GlobalStructRegistry().DefineEnum(def)

	// Create constructors for each variant
	for _, variant := range def.Variants {
		makeEnumVariantConstructor(def, variant)
	}

	return ast.NewSym(def.Name)
}

// parseEnumVariant parses a variant: Name or (Name [field {Type}] ...)
func parseEnumVariant(v *ast.Value) (EnumVariant, error) {
	ev := EnumVariant{}

	// Simple variant: Name
	if ast.IsSym(v) {
		ev.Name = v.Str
		return ev, nil
	}

	// Variant with fields: (Name [field {Type}] ...)
	if ast.IsCell(v) {
		if !ast.IsSym(v.Car) {
			return ev, fmt.Errorf("variant name must be a symbol")
		}
		ev.Name = v.Car.Str

		// Parse fields
		for f := v.Cdr; !ast.IsNil(f) && ast.IsCell(f); f = f.Cdr {
			field := f.Car
			sf, err := parseStructField(field, false)
			if err != nil {
				return ev, err
			}
			ev.Fields = append(ev.Fields, sf)
		}
	}

	return ev, nil
}

// makeEnumVariantConstructor creates a constructor for an enum variant
func makeEnumVariantConstructor(def *EnumDef, variant EnumVariant) {
	if len(variant.Fields) == 0 {
		// Simple variant - just a symbol-like value with special marker
		fields := map[string]*ast.Value{
			"__variant__": ast.NewSym(variant.Name),
		}
		val := ast.NewUserType(def.Name, fields, []string{"__variant__"})
		GlobalDefine(ast.NewSym(variant.Name), val)
	} else {
		// Variant with fields - constructor function
		fn := ast.NewPrim(func(args *ast.Value, menv *ast.Value) *ast.Value {
			fieldValues := make(map[string]*ast.Value)
			fieldOrder := make([]string, 0, len(variant.Fields)+1)

			// Add variant tag
			fieldValues["__variant__"] = ast.NewSym(variant.Name)
			fieldOrder = append(fieldOrder, "__variant__")

			// Collect field values from args
			idx := 0
			for a := args; !ast.IsNil(a) && ast.IsCell(a) && idx < len(variant.Fields); a = a.Cdr {
				f := variant.Fields[idx]
				fieldValues[f.Name] = a.Car
				fieldOrder = append(fieldOrder, f.Name)
				idx++
			}

			if idx < len(variant.Fields) {
				return ast.NewError(fmt.Sprintf("%s: missing field '%s'",
					variant.Name, variant.Fields[idx].Name))
			}

			return ast.NewUserType(def.Name, fieldValues, fieldOrder)
		})
		GlobalDefine(ast.NewSym(variant.Name), fn)
	}
}

// GetField retrieves a field value from a struct instance
func GetField(instance *ast.Value, fieldName string) *ast.Value {
	if instance == nil {
		return ast.Nil
	}

	// Check if it's a user type with field map
	if instance.Tag == ast.TUserType && instance.UserTypeFields != nil {
		if val, ok := instance.UserTypeFields[fieldName]; ok {
			return val
		}
	}

	return ast.Nil
}

// SetField sets a field value on a mutable struct instance
func SetField(instance *ast.Value, fieldName string, value *ast.Value) error {
	if instance == nil {
		return fmt.Errorf("cannot set field on nil")
	}

	// Check if it's a user type with field map
	if instance.Tag == ast.TUserType && instance.UserTypeFields != nil {
		// Find the struct definition to check mutability
		def := GlobalStructRegistry().GetStruct(instance.UserTypeName)
		if def != nil {
			for _, f := range def.Fields {
				if f.Name == fieldName {
					if !f.Mutable && !def.AllMutable {
						return fmt.Errorf("field '%s' is immutable", fieldName)
					}
					break
				}
			}
		}

		// Check if field exists
		if _, ok := instance.UserTypeFields[fieldName]; !ok {
			return fmt.Errorf("field '%s' not found", fieldName)
		}

		// Set the field value
		instance.UserTypeFields[fieldName] = value
		return nil
	}

	return fmt.Errorf("not a struct instance")
}
