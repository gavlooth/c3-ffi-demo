package codegen

import (
	"strings"
	"sync"
)

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

// BackEdgeHints contains field name patterns that are likely back-edges
var BackEdgeHints = []string{
	"parent", "owner", "container", // Points to ancestor/owner
	"prev", "previous", "back",     // Reverse direction in sequences
	"up", "outer",                  // Hierarchical back-references
}

// isBackEdgeHint returns true if the field name matches a back-edge naming pattern
func isBackEdgeHint(fieldName string) bool {
	lower := strings.ToLower(fieldName)
	for _, hint := range BackEdgeHints {
		if strings.Contains(lower, hint) {
			return true
		}
	}
	return false
}

// AnalyzeBackEdges detects and marks back edges in the ownership graph
// Uses three strategies:
// 1. Naming heuristics (prev, parent, back, etc.)
// 2. Second-pointer detection (if type already has a strong pointer to same type)
// 3. DFS cycle detection (fallback)
func (r *TypeRegistry) AnalyzeBackEdges() {
	// Phase 1: Apply naming heuristics
	r.applyNamingHeuristics()

	// Phase 2: Detect second pointers to same type
	r.detectSecondPointers()

	// Phase 3: DFS-based cycle detection for remaining edges
	r.detectCyclesWithDFS()
}

// applyNamingHeuristics marks fields with back-edge naming patterns as weak
func (r *TypeRegistry) applyNamingHeuristics() {
	for _, e := range r.OwnershipGraph {
		if isBackEdgeHint(e.FieldName) {
			e.IsBackEdge = true
			r.markFieldWeak(e.FromType, e.FieldName)
		}
	}
}

// detectSecondPointers marks the second pointer to the same type as weak
// Example: Node has (next Node) and (prev Node) - prev becomes weak
func (r *TypeRegistry) detectSecondPointers() {
	for _, t := range r.Types {
		// Track first strong pointer to each type
		firstPointer := make(map[string]string) // targetType -> fieldName

		for i := range t.Fields {
			f := &t.Fields[i]
			if !f.IsScannable || f.Strength == FieldWeak {
				continue
			}

			if first, exists := firstPointer[f.Type]; exists {
				// This is a second pointer to the same type
				// Mark it as weak (the first one stays strong)
				f.Strength = FieldWeak
				// Update the ownership edge too
				for _, e := range r.OwnershipGraph {
					if e.FromType == t.Name && e.FieldName == f.Name {
						e.IsBackEdge = true
						break
					}
				}
				_ = first // Used for reference if needed
			} else {
				firstPointer[f.Type] = f.Name
			}
		}
	}
}

// detectCyclesWithDFS uses DFS to find remaining cycles not caught by heuristics
// Only marks edges as back-edges if the cycle isn't already broken by other weak edges
func (r *TypeRegistry) detectCyclesWithDFS() {
	visited := make(map[string]int) // 0=white, 1=gray, 2=black
	var path []string

	// Check if there's already a back-edge that breaks the cycle involving 'from' and 'to'
	// For type-level analysis, if any edge from type X to type Y is a back-edge,
	// then the cycle X -> Y -> ... -> X is already broken
	hasBackEdgeInCycle := func(from, to string) bool {
		// For self-loops (from == to), check if any edge from 'from' to 'to' is already a back-edge
		if from == to {
			for _, e := range r.OwnershipGraph {
				if e.FromType == from && e.ToType == to && e.IsBackEdge {
					return true
				}
			}
			return false
		}

		// For longer cycles, check if any edge on the path is already a back-edge
		// Path is: to -> ... -> from -> to (we're trying to add from -> to)
		// We need to check edges on: to -> path[idx+1] -> ... -> from
		for i, p := range path {
			if p == to {
				// Check edges from to to next, etc. up to from
				for j := i; j < len(path)-1; j++ {
					curr := path[j]
					next := path[j+1]
					for _, e := range r.OwnershipGraph {
						if e.FromType == curr && e.ToType == next && e.IsBackEdge {
							return true
						}
					}
				}
				// Also check from last path element to 'from'
				if len(path) > 0 {
					last := path[len(path)-1]
					for _, e := range r.OwnershipGraph {
						if e.FromType == last && e.ToType == from && e.IsBackEdge {
							return true
						}
					}
				}
				break
			}
		}
		return false
	}

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
			if e.FromType == typeName && !e.IsBackEdge {
				// Check if target is in current path (potential back edge)
				isCycle := false
				for _, p := range path {
					if p == e.ToType {
						isCycle = true
						break
					}
				}

				if isCycle {
					// Only mark as back-edge if the cycle isn't already broken
					if !hasBackEdgeInCycle(typeName, e.ToType) {
						e.IsBackEdge = true
						r.markFieldWeak(e.FromType, e.FieldName)
					}
				} else {
					dfs(e.ToType)
				}
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

// HasCycleBrokenByWeakEdges checks if cycles involving this type are broken by auto-weak edges
// If true, dec_ref is safe to use instead of deferred_release or arena
func (r *TypeRegistry) HasCycleBrokenByWeakEdges(typeName string) bool {
	// Check if there are any back-edges (cycles) involving this type
	hasCycle := false
	hasBackEdge := false

	for _, e := range r.OwnershipGraph {
		// Check if this type participates in a cycle
		if e.FromType == typeName || e.ToType == typeName {
			// Check if this specific edge is a cycle back to the type
			if e.FromType == typeName && r.canReach(e.ToType, typeName) {
				hasCycle = true
				if e.IsBackEdge {
					hasBackEdge = true
				}
			}
		}
	}

	// If there's a cycle but we have a back-edge breaking it, dec_ref is safe
	return !hasCycle || hasBackEdge
}

// canReach checks if there's a path from 'from' to 'to' via strong edges only
func (r *TypeRegistry) canReach(from, to string) bool {
	if from == to {
		return true
	}

	visited := make(map[string]bool)
	var dfs func(current string) bool
	dfs = func(current string) bool {
		if current == to {
			return true
		}
		if visited[current] {
			return false
		}
		visited[current] = true

		for _, e := range r.OwnershipGraph {
			if e.FromType == current && !e.IsBackEdge {
				if dfs(e.ToType) {
					return true
				}
			}
		}
		return false
	}

	return dfs(from)
}

// CycleStatus represents the status of cycles for a type
type CycleStatus int

const (
	CycleStatusNone      CycleStatus = iota // No cycles - pure ASAP/dec_ref
	CycleStatusBroken                       // Has cycles but broken by weak edges - dec_ref safe
	CycleStatusUnbroken                     // Has unbroken cycles - need arena/SCC
)

// GetCycleStatus returns the cycle status for a type
func (r *TypeRegistry) GetCycleStatus(typeName string) CycleStatus {
	t := r.Types[typeName]
	if t == nil {
		return CycleStatusNone
	}

	// Check if type is recursive
	if !t.IsRecursive {
		// Check if it participates in mutual recursion
		hasCycle := false
		for _, e := range r.OwnershipGraph {
			if e.FromType == typeName && r.canReach(e.ToType, typeName) {
				hasCycle = true
				break
			}
		}
		if !hasCycle {
			return CycleStatusNone
		}
	}

	// Type has cycles - check if they're broken
	if r.HasCycleBrokenByWeakEdges(typeName) {
		return CycleStatusBroken
	}
	return CycleStatusUnbroken
}

// IsFieldWeak returns true if the specified field is weak
// Implements analysis.FieldStrengthLookup interface
func (r *TypeRegistry) IsFieldWeak(typeName, fieldName string) bool {
	t := r.Types[typeName]
	if t == nil {
		return false
	}
	for _, f := range t.Fields {
		if f.Name == fieldName {
			return f.Strength == FieldWeak
		}
	}
	return false
}

// IsFieldStrong returns true if the specified field is strong
func (r *TypeRegistry) IsFieldStrong(typeName, fieldName string) bool {
	t := r.Types[typeName]
	if t == nil {
		return false
	}
	for _, f := range t.Fields {
		if f.Name == fieldName {
			return f.Strength == FieldStrong
		}
	}
	return false
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
