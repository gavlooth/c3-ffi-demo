package codegen

import "sync"

// FieldStrength represents the strength of a reference field
type FieldStrength int

const (
	FieldUntraced FieldStrength = iota
	FieldStrong
	FieldWeak
)

// TypeField represents a field in a type definition
type TypeField struct {
	Name        string
	Type        string
	IsScannable bool
	Strength    FieldStrength
}

// TypeDef represents a type definition in the registry
type TypeDef struct {
	Name        string
	Fields      []TypeField
	IsRecursive bool
}

// OwnershipEdge represents an edge in the ownership graph
type OwnershipEdge struct {
	FromType   string
	FieldName  string
	ToType     string
	IsBackEdge bool
}

// TypeRegistry manages type definitions
type TypeRegistry struct {
	Types          map[string]*TypeDef
	OwnershipGraph []*OwnershipEdge
}

// Global type registry for cross-package access
var (
	globalRegistry     *TypeRegistry
	globalRegistryOnce sync.Once
	globalRegistryMu   sync.RWMutex
)

// GlobalRegistry returns the singleton global type registry
func GlobalRegistry() *TypeRegistry {
	globalRegistryOnce.Do(func() {
		globalRegistry = NewTypeRegistry()
		globalRegistry.InitDefaultTypes()
	})
	return globalRegistry
}

// ResetGlobalRegistry resets the global registry (for testing)
func ResetGlobalRegistry() {
	globalRegistryMu.Lock()
	defer globalRegistryMu.Unlock()
	globalRegistry = NewTypeRegistry()
	globalRegistry.InitDefaultTypes()
}

// NewTypeRegistry creates a new type registry
func NewTypeRegistry() *TypeRegistry {
	return &TypeRegistry{
		Types: make(map[string]*TypeDef),
	}
}

// RegisterType adds a type to the registry
func (r *TypeRegistry) RegisterType(name string, fields []TypeField) {
	t := &TypeDef{
		Name:   name,
		Fields: make([]TypeField, len(fields)),
	}
	copy(t.Fields, fields)

	// Set strengths and check for recursion
	for i := range t.Fields {
		if t.Fields[i].IsScannable {
			t.Fields[i].Strength = FieldStrong
			if t.Fields[i].Type == name {
				t.IsRecursive = true
			}
		} else {
			t.Fields[i].Strength = FieldUntraced
		}
	}

	r.Types[name] = t
}

// FindType looks up a type by name
func (r *TypeRegistry) FindType(name string) *TypeDef {
	return r.Types[name]
}

// BuildOwnershipGraph builds the ownership graph from registered types
func (r *TypeRegistry) BuildOwnershipGraph() {
	r.OwnershipGraph = nil
	for _, t := range r.Types {
		for _, f := range t.Fields {
			if f.IsScannable {
				r.OwnershipGraph = append(r.OwnershipGraph, &OwnershipEdge{
					FromType:  t.Name,
					FieldName: f.Name,
					ToType:    f.Type,
				})
			}
		}
	}
}

// AnalyzeBackEdges detects and marks back edges in the ownership graph
func (r *TypeRegistry) AnalyzeBackEdges() {
	visited := make(map[string]int) // 0=white, 1=gray, 2=black
	var path []string

	var dfs func(typeName string)
	dfs = func(typeName string) {
		if visited[typeName] == 2 {
			return
		}
		if visited[typeName] == 1 {
			return // Already in current path
		}

		visited[typeName] = 1
		path = append(path, typeName)

		for _, e := range r.OwnershipGraph {
			if e.FromType == typeName {
				// Check if target is in current path (back edge)
				for _, p := range path {
					if p == e.ToType {
						e.IsBackEdge = true
						r.markFieldWeak(e.FromType, e.FieldName)
						break
					}
				}
				dfs(e.ToType)
			}
		}

		path = path[:len(path)-1]
		visited[typeName] = 2
	}

	for name := range r.Types {
		if visited[name] == 0 {
			dfs(name)
		}
	}
}

func (r *TypeRegistry) markFieldWeak(typeName, fieldName string) {
	if t := r.Types[typeName]; t != nil {
		for i := range t.Fields {
			if t.Fields[i].Name == fieldName {
				t.Fields[i].Strength = FieldWeak
				return
			}
		}
	}
}

// InitDefaultTypes initializes the default type registry with common types
func (r *TypeRegistry) InitDefaultTypes() {
	// Pair type
	r.RegisterType("Pair", []TypeField{
		{Name: "a", Type: "Obj", IsScannable: true},
		{Name: "b", Type: "Obj", IsScannable: true},
	})

	// List type
	r.RegisterType("List", []TypeField{
		{Name: "a", Type: "List", IsScannable: true},
		{Name: "b", Type: "List", IsScannable: true},
	})

	// Tree type
	r.RegisterType("Tree", []TypeField{
		{Name: "left", Type: "Tree", IsScannable: true},
		{Name: "right", Type: "Tree", IsScannable: true},
		{Name: "value", Type: "int", IsScannable: false},
	})

	r.BuildOwnershipGraph()
	r.AnalyzeBackEdges()
}
