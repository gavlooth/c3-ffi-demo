// Package eval provides module system for OmniLisp.
//
// Modules provide namespacing and controlled exports for code organization.
// Syntax:
//   (module ModuleName
//     (export name1 name2 ...)
//     (import [OtherModule :only (helper)])
//     body...)
//
//   (import ModuleName)
//   (import [ModuleName :as M])
//   (import [ModuleName :only (f1 f2)])
//   (import [ModuleName :refer :all])
package eval

import (
	"fmt"
	"sync"

	"purple_go/pkg/ast"
)

// Module represents a module with its bindings and exports
type Module struct {
	Name     string              // Module name
	Exports  map[string]bool     // Exported names (true = exported)
	Bindings map[string]*ast.Value // All bindings in module
	Imports  []*Import           // Imported modules
	Env      *ast.Value          // Module environment
}

// Import represents an import specification
type Import struct {
	ModuleName string   // Module being imported
	Alias      string   // :as alias (empty = no alias)
	Only       []string // :only names (nil = all)
	Except     []string // :except names (nil = none)
	Refer      []string // :refer names to bring into scope
	ReferAll   bool     // :refer :all
}

// ModuleRegistry holds all defined modules
type ModuleRegistry struct {
	mu      sync.RWMutex
	modules map[string]*Module
}

// Global module registry
var globalModuleRegistry = NewModuleRegistry()

// NewModuleRegistry creates a new module registry
func NewModuleRegistry() *ModuleRegistry {
	return &ModuleRegistry{
		modules: make(map[string]*Module),
	}
}

// GlobalModuleRegistry returns the global registry
func GlobalModuleRegistry() *ModuleRegistry {
	return globalModuleRegistry
}

// DefineModule creates or returns an existing module
func (mr *ModuleRegistry) DefineModule(name string) *Module {
	mr.mu.Lock()
	defer mr.mu.Unlock()

	if m, ok := mr.modules[name]; ok {
		return m
	}

	m := &Module{
		Name:     name,
		Exports:  make(map[string]bool),
		Bindings: make(map[string]*ast.Value),
		Env:      ast.Nil,
	}
	mr.modules[name] = m
	return m
}

// GetModule returns a module by name
func (mr *ModuleRegistry) GetModule(name string) *Module {
	mr.mu.RLock()
	defer mr.mu.RUnlock()
	return mr.modules[name]
}

// Export marks names as exported from the module
func (m *Module) Export(names ...string) {
	for _, name := range names {
		m.Exports[name] = true
	}
}

// Define adds a binding to the module
func (m *Module) Define(name string, value *ast.Value) {
	m.Bindings[name] = value
}

// Lookup looks up a name in the module
func (m *Module) Lookup(name string) *ast.Value {
	if v, ok := m.Bindings[name]; ok {
		return v
	}
	return nil
}

// LookupExported looks up an exported name
func (m *Module) LookupExported(name string) *ast.Value {
	if !m.Exports[name] {
		return nil
	}
	return m.Bindings[name]
}

// IsExported checks if a name is exported
func (m *Module) IsExported(name string) bool {
	return m.Exports[name]
}

// GetExportedNames returns all exported names
func (m *Module) GetExportedNames() []string {
	var names []string
	for name := range m.Exports {
		if m.Exports[name] {
			names = append(names, name)
		}
	}
	return names
}

// evalModule handles (module ModuleName body...)
// Creates a new module and evaluates the body in its context
func evalModule(args *ast.Value, menv *ast.Value) *ast.Value {
	if ast.IsNil(args) {
		return ast.NewError("module: requires module name")
	}

	// Get module name
	nameVal := args.Car
	if !ast.IsSym(nameVal) {
		return ast.NewError("module: module name must be a symbol")
	}
	moduleName := nameVal.Str

	// Create or get module
	mr := GlobalModuleRegistry()
	module := mr.DefineModule(moduleName)

	// Create module environment
	moduleEnv := ast.Nil
	moduleMenv := ast.NewMenv(moduleEnv, menv.Parent, menv.Level, menv.CopyHandlers())

	// Process module body
	body := args.Cdr
	for !ast.IsNil(body) && ast.IsCell(body) {
		form := body.Car

		// Check for special module forms
		if ast.IsCell(form) {
			op := form.Car

			// (export name1 name2 ...)
			if ast.SymEqStr(op, "export") {
				for exports := form.Cdr; !ast.IsNil(exports) && ast.IsCell(exports); exports = exports.Cdr {
					if ast.IsSym(exports.Car) {
						module.Export(exports.Car.Str)
					}
				}
				body = body.Cdr
				continue
			}

			// (import ...)
			if ast.SymEqStr(op, "import") {
				evalModuleImport(form.Cdr, module, moduleMenv)
				body = body.Cdr
				continue
			}
		}

		// Regular expression - evaluate and possibly bind
		result := Eval(form, moduleMenv)

		// If it was a define, capture the binding
		if ast.IsCell(form) && ast.SymEqStr(form.Car, "define") {
			first := form.Cdr.Car
			var name string
			if ast.IsSym(first) {
				name = first.Str
			} else if ast.IsCell(first) && ast.IsSym(first.Car) {
				name = first.Car.Str
			}
			if name != "" {
				module.Define(name, result)
				// Also add to module environment
				moduleMenv.Env = EnvExtend(moduleMenv.Env, ast.NewSym(name), result)
			}
		}

		body = body.Cdr
	}

	// Store the module environment
	module.Env = moduleMenv.Env

	return ast.NewSym(moduleName)
}

// evalModuleImport handles import within a module definition
func evalModuleImport(args *ast.Value, module *Module, menv *ast.Value) *ast.Value {
	imp := parseImport(args)
	if imp == nil {
		return ast.NewError("import: invalid import specification")
	}

	module.Imports = append(module.Imports, imp)

	// Perform the import
	return performImport(imp, menv)
}

// evalImport handles top-level (import ...) forms
func evalImport(args *ast.Value, menv *ast.Value) *ast.Value {
	imp := parseImport(args)
	if imp == nil {
		return ast.NewError("import: invalid import specification")
	}

	return performImport(imp, menv)
}

// parseImport parses an import specification
func parseImport(args *ast.Value) *Import {
	if ast.IsNil(args) {
		return nil
	}

	first := args.Car

	// Simple import: (import ModuleName)
	if ast.IsSym(first) {
		return &Import{
			ModuleName: first.Str,
			ReferAll:   true, // By default, import all exports
		}
	}

	// Complex import: (import [ModuleName :as M :only (f1 f2)])
	if ast.IsArray(first) && len(first.ArrayData) >= 1 {
		arr := first.ArrayData

		if !ast.IsSym(arr[0]) {
			return nil
		}

		imp := &Import{
			ModuleName: arr[0].Str,
		}

		// Parse modifiers
		i := 1
		for i < len(arr) {
			if ast.IsKeyword(arr[i]) || (ast.IsSym(arr[i]) && arr[i].Str[0] == ':') {
				keyword := arr[i].Str
				if ast.IsKeyword(arr[i]) {
					keyword = arr[i].Str
				} else {
					keyword = arr[i].Str[1:] // Remove leading :
				}

				switch keyword {
				case "as":
					if i+1 < len(arr) && ast.IsSym(arr[i+1]) {
						imp.Alias = arr[i+1].Str
						i += 2
						continue
					}

				case "only":
					if i+1 < len(arr) && ast.IsArray(arr[i+1]) {
						for _, v := range arr[i+1].ArrayData {
							if ast.IsSym(v) {
								imp.Only = append(imp.Only, v.Str)
							}
						}
						i += 2
						continue
					}

				case "except":
					if i+1 < len(arr) && ast.IsArray(arr[i+1]) {
						for _, v := range arr[i+1].ArrayData {
							if ast.IsSym(v) {
								imp.Except = append(imp.Except, v.Str)
							}
						}
						i += 2
						continue
					}

				case "refer":
					if i+1 < len(arr) {
						if ast.IsKeyword(arr[i+1]) && arr[i+1].Str == "all" {
							imp.ReferAll = true
						} else if ast.IsArray(arr[i+1]) {
							for _, v := range arr[i+1].ArrayData {
								if ast.IsSym(v) {
									imp.Refer = append(imp.Refer, v.Str)
								}
							}
						}
						i += 2
						continue
					}
				}
			}
			i++
		}

		return imp
	}

	// List form: (import (ModuleName :as M))
	if ast.IsCell(first) {
		if !ast.IsSym(first.Car) {
			return nil
		}

		imp := &Import{
			ModuleName: first.Car.Str,
		}

		// Parse modifiers from list
		rest := first.Cdr
		for !ast.IsNil(rest) && ast.IsCell(rest) {
			item := rest.Car

			if ast.IsSym(item) && len(item.Str) > 0 && item.Str[0] == ':' {
				keyword := item.Str[1:]
				rest = rest.Cdr
				if ast.IsNil(rest) {
					break
				}

				switch keyword {
				case "as":
					if ast.IsSym(rest.Car) {
						imp.Alias = rest.Car.Str
					}
				case "only":
					if ast.IsCell(rest.Car) {
						for v := rest.Car; !ast.IsNil(v) && ast.IsCell(v); v = v.Cdr {
							if ast.IsSym(v.Car) {
								imp.Only = append(imp.Only, v.Car.Str)
							}
						}
					}
				case "except":
					if ast.IsCell(rest.Car) {
						for v := rest.Car; !ast.IsNil(v) && ast.IsCell(v); v = v.Cdr {
							if ast.IsSym(v.Car) {
								imp.Except = append(imp.Except, v.Car.Str)
							}
						}
					}
				case "refer":
					if ast.IsSym(rest.Car) && rest.Car.Str == "all" {
						imp.ReferAll = true
					} else if ast.IsCell(rest.Car) {
						for v := rest.Car; !ast.IsNil(v) && ast.IsCell(v); v = v.Cdr {
							if ast.IsSym(v.Car) {
								imp.Refer = append(imp.Refer, v.Car.Str)
							}
						}
					}
				}
			}
			rest = rest.Cdr
		}

		return imp
	}

	return nil
}

// performImport performs the actual import into an environment
func performImport(imp *Import, menv *ast.Value) *ast.Value {
	mr := GlobalModuleRegistry()
	module := mr.GetModule(imp.ModuleName)
	if module == nil {
		return ast.NewError(fmt.Sprintf("import: unknown module: %s", imp.ModuleName))
	}

	// Get names to import
	var namesToImport []string

	if len(imp.Only) > 0 {
		// Only import specified names
		namesToImport = imp.Only
	} else {
		// Import all exported names
		namesToImport = module.GetExportedNames()
	}

	// Apply except filter
	if len(imp.Except) > 0 {
		exceptMap := make(map[string]bool)
		for _, name := range imp.Except {
			exceptMap[name] = true
		}
		var filtered []string
		for _, name := range namesToImport {
			if !exceptMap[name] {
				filtered = append(filtered, name)
			}
		}
		namesToImport = filtered
	}

	// Import the names
	for _, name := range namesToImport {
		value := module.LookupExported(name)
		if value == nil {
			continue // Skip non-exported or non-existent names
		}

		// Determine binding name
		bindName := name
		if imp.Alias != "" {
			bindName = imp.Alias + "/" + name
		}

		// Add to global environment
		GlobalDefine(ast.NewSym(bindName), value)

		// Also add unqualified if refer is set
		if imp.ReferAll || contains(imp.Refer, name) {
			GlobalDefine(ast.NewSym(name), value)
		}
	}

	return ast.NewSym(imp.ModuleName)
}

// contains checks if a slice contains a string
func contains(slice []string, s string) bool {
	for _, item := range slice {
		if item == s {
			return true
		}
	}
	return false
}

// evalRequire handles (require "path/to/module.ol")
// Loads and evaluates a module file
func evalRequire(args *ast.Value, menv *ast.Value) *ast.Value {
	// For now, just return an error - file loading needs OS integration
	return ast.NewError("require: file loading not yet implemented")
}

// ClearModules clears all modules (for testing)
func ClearModules() {
	globalModuleRegistry.mu.Lock()
	defer globalModuleRegistry.mu.Unlock()
	globalModuleRegistry.modules = make(map[string]*Module)
}

// QualifiedLookup looks up a qualified name like Module/name
func QualifiedLookup(name string) *ast.Value {
	// Find the / separator
	for i, c := range name {
		if c == '/' {
			moduleName := name[:i]
			memberName := name[i+1:]

			mr := GlobalModuleRegistry()
			module := mr.GetModule(moduleName)
			if module == nil {
				return nil
			}

			return module.LookupExported(memberName)
		}
	}
	return nil
}
