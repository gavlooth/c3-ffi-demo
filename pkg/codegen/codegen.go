package codegen

import (
	"fmt"
	"io"
	"strings"

	"purple_go/pkg/analysis"
	"purple_go/pkg/ast"
)

// CodeGenerator generates C99 code from AST
type CodeGenerator struct {
	w                io.Writer
	registry         *TypeRegistry
	escapeCtx        *analysis.AnalysisContext
	shapeCtx         *analysis.ShapeContext
	rcOptCtx         *analysis.RCOptContext       // RC optimization context
	summaryCtx       *analysis.SummaryAnalyzer    // Interprocedural analysis
	concurrencyCtx   *analysis.ConcurrencyAnalyzer // Concurrency ownership
	reuseCtx         *analysis.ReuseAnalyzer      // Perceus reuse analysis
	arenaGen         *ArenaCodeGenerator
	tempCounter      int
	indentLevel      int
	useArenaFallback bool
	enableRCOpt      bool // Enable RC optimization (Lobster-style)
}

// NewCodeGenerator creates a new code generator
func NewCodeGenerator(w io.Writer) *CodeGenerator {
	registry := NewTypeRegistry()
	registry.InitDefaultTypes()
	return &CodeGenerator{
		w:                w,
		registry:         registry,
		escapeCtx:        analysis.NewAnalysisContext(),
		shapeCtx:         analysis.NewShapeContext(),
		rcOptCtx:         analysis.NewRCOptContext(),
		summaryCtx:       analysis.NewSummaryAnalyzer(),
		concurrencyCtx:   analysis.NewConcurrencyAnalyzer(),
		reuseCtx:         analysis.NewReuseAnalyzer(),
		arenaGen:         NewArenaCodeGenerator(),
		useArenaFallback: true, // Enable arena fallback for CYCLIC/UNKNOWN shapes
		enableRCOpt:      true, // Enable Lobster-style RC optimization
	}
}

// NewCodeGeneratorWithGlobalRegistry creates a code generator using the global type registry
// This allows access to user-defined types from deftype declarations
func NewCodeGeneratorWithGlobalRegistry(w io.Writer) *CodeGenerator {
	return &CodeGenerator{
		w:                w,
		registry:         GlobalRegistry(),
		escapeCtx:        analysis.NewAnalysisContext(),
		shapeCtx:         analysis.NewShapeContext(),
		rcOptCtx:         analysis.NewRCOptContext(),
		summaryCtx:       analysis.NewSummaryAnalyzer(),
		concurrencyCtx:   analysis.NewConcurrencyAnalyzer(),
		reuseCtx:         analysis.NewReuseAnalyzer(),
		arenaGen:         NewArenaCodeGenerator(),
		useArenaFallback: true,
		enableRCOpt:      true,
	}
}

// GetCycleStatusForType returns the cycle status for a user-defined type
// Uses TypeRegistry's analysis for cycle detection with weak edge breaking
func (g *CodeGenerator) GetCycleStatusForType(typeName string) CycleStatus {
	if g.registry == nil {
		return CycleStatusNone
	}
	return g.registry.GetCycleStatus(typeName)
}

// ShouldUseArenaForType returns true if arena allocation should be used for a type
// based on its cycle status (unbroken cycles require arena or SCC)
func (g *CodeGenerator) ShouldUseArenaForType(typeName string) bool {
	status := g.GetCycleStatusForType(typeName)
	return status == CycleStatusUnbroken
}

// SetRCOptimization enables or disables RC optimization
func (g *CodeGenerator) SetRCOptimization(enabled bool) {
	g.enableRCOpt = enabled
}

// SetArenaFallback enables or disables arena fallback for cyclic shapes
func (g *CodeGenerator) SetArenaFallback(enabled bool) {
	g.useArenaFallback = enabled
}

// AnalyzeFunction registers a function's summary for interprocedural analysis
func (g *CodeGenerator) AnalyzeFunction(name string, params *ast.Value, body *ast.Value) *analysis.FunctionSummary {
	if g.summaryCtx == nil {
		return nil
	}
	return g.summaryCtx.AnalyzeFunction(name, params, body)
}

// GetParamOwnership returns the ownership class for a function parameter at a call site
func (g *CodeGenerator) GetParamOwnership(funcName string, paramIdx int) analysis.OwnershipClass {
	if g.summaryCtx == nil || g.summaryCtx.Registry == nil {
		return analysis.OwnerBorrowed
	}
	return g.summaryCtx.Registry.GetParamOwnership(funcName, paramIdx)
}

// GetReturnOwnership returns the ownership class for a function's return value
func (g *CodeGenerator) GetReturnOwnership(funcName string) analysis.OwnershipClass {
	if g.summaryCtx == nil || g.summaryCtx.Registry == nil {
		return analysis.OwnerFresh
	}
	return g.summaryCtx.Registry.GetReturnOwnership(funcName)
}

// AnalyzeConcurrency performs concurrency analysis on an expression
func (g *CodeGenerator) AnalyzeConcurrency(expr *ast.Value) {
	if g.concurrencyCtx != nil {
		g.concurrencyCtx.Analyze(expr)
	}
}

// NeedsAtomicRC returns true if a variable needs atomic reference counting
func (g *CodeGenerator) NeedsAtomicRC(varName string) bool {
	if g.concurrencyCtx == nil {
		return false
	}
	return g.concurrencyCtx.Ctx.NeedsAtomicRC(varName)
}

// IsTransferred returns true if a variable's ownership has been transferred (e.g., via chan-send!)
func (g *CodeGenerator) IsTransferred(varName string) bool {
	if g.concurrencyCtx == nil {
		return false
	}
	return g.concurrencyCtx.Ctx.GetLocality(varName) == analysis.LocalityTransferred
}

// AnalyzeReuse performs reuse analysis on an expression
func (g *CodeGenerator) AnalyzeReuse(expr *ast.Value) {
	if g.reuseCtx != nil {
		g.reuseCtx.Analyze(expr)
	}
}

// TryReuse attempts to find a reuse candidate for an allocation
func (g *CodeGenerator) TryReuse(allocVar, allocType string, line int) *analysis.ReuseCandidate {
	if g.reuseCtx == nil {
		return nil
	}
	return g.reuseCtx.Ctx.TryReuse(allocVar, allocType, line)
}

// GetReuseFor returns the variable that can be reused for an allocation, if any
func (g *CodeGenerator) GetReuseFor(allocVar string) (string, bool) {
	if g.reuseCtx == nil {
		return "", false
	}
	return g.reuseCtx.Ctx.GetReuse(allocVar)
}

// AddPendingFree marks a variable as pending for free (available for reuse)
func (g *CodeGenerator) AddPendingFree(name, typeName string) {
	if g.reuseCtx != nil {
		g.reuseCtx.Ctx.AddPendingFree(name, typeName)
	}
}

// GenerateRCOperation generates the appropriate reference count operation
// Uses atomic operations for shared variables, regular operations otherwise
func (g *CodeGenerator) GenerateRCOperation(varName string, op string) string {
	if g.NeedsAtomicRC(varName) {
		switch op {
		case "inc":
			return fmt.Sprintf("atomic_inc_ref(%s)", varName)
		case "dec":
			return fmt.Sprintf("atomic_dec_ref(%s)", varName)
		}
	}
	switch op {
	case "inc":
		return fmt.Sprintf("inc_ref(%s)", varName)
	case "dec":
		return fmt.Sprintf("dec_ref(%s)", varName)
	}
	return ""
}

// GenerateAllocation generates an allocation, potentially reusing freed memory
func (g *CodeGenerator) GenerateAllocation(varName, allocType string, allocExpr string) string {
	if freeVar, ok := g.GetReuseFor(varName); ok {
		// Reuse available
		return fmt.Sprintf("reuse_as_%s(%s, %s)", allocType, freeVar, allocExpr)
	}
	return allocExpr
}

func (g *CodeGenerator) emit(format string, args ...interface{}) {
	fmt.Fprintf(g.w, format, args...)
}

func (g *CodeGenerator) indent() string {
	return strings.Repeat("    ", g.indentLevel)
}

func (g *CodeGenerator) newTemp() string {
	g.tempCounter++
	return fmt.Sprintf("_t%d", g.tempCounter)
}

// ValueToCExpr converts a Value to a C expression string
func (g *CodeGenerator) ValueToCExpr(v *ast.Value) string {
	if v == nil || ast.IsNil(v) {
		return "NULL"
	}
	switch v.Tag {
	case ast.TCode:
		return v.Str
	case ast.TInt:
		return fmt.Sprintf("mk_int(%d)", v.Int)
	case ast.TCell:
		carExpr := g.ValueToCExpr(v.Car)
		cdrExpr := g.ValueToCExpr(v.Cdr)
		return fmt.Sprintf("mk_pair(%s, %s)", carExpr, cdrExpr)
	default:
		return "NULL"
	}
}

// LiftValue converts a Value to a code Value
func (g *CodeGenerator) LiftValue(v *ast.Value) *ast.Value {
	if v == nil || ast.IsNil(v) {
		return ast.NewCode("NULL")
	}
	switch v.Tag {
	case ast.TCode:
		return v
	case ast.TInt:
		return ast.NewCode(fmt.Sprintf("mk_int(%d)", v.Int))
	case ast.TSym:
		return ast.NewCode(fmt.Sprintf("mk_sym(\"%s\")", v.Str))
	case ast.TCell:
		carCode := g.LiftValue(v.Car)
		cdrCode := g.LiftValue(v.Cdr)
		return ast.NewCode(fmt.Sprintf("mk_pair(%s, %s)", carCode.Str, cdrCode.Str))
	default:
		return ast.NewCode("NULL")
	}
}

// EmitCCall generates a C function call
func (g *CodeGenerator) EmitCCall(fn string, a, b *ast.Value) *ast.Value {
	aStr := g.ValueToCExpr(a)
	bStr := g.ValueToCExpr(b)
	return ast.NewCode(fmt.Sprintf("%s(%s, %s)", fn, aStr, bStr))
}

// GenerateLet generates code for a let expression with ASAP memory management
func (g *CodeGenerator) GenerateLet(bindings []struct {
	sym *ast.Value
	val *ast.Value
}, body *ast.Value) string {
	// Analyze the expression for escape and shape
	needsArena := false
	for _, bi := range bindings {
		g.escapeCtx.AddVar(bi.sym.Str)
		g.shapeCtx.AnalyzeShapes(bi.val)
		g.shapeCtx.AddShape(bi.sym.Str, g.shapeCtx.ResultShape)

		// RC Optimization: Track aliases and uniqueness
		if g.enableRCOpt {
			if ast.IsSym(bi.val) {
				// Value is a variable reference - creates an alias
				g.rcOptCtx.DefineAlias(bi.sym.Str, bi.val.Str)
			} else {
				// Fresh allocation - starts as unique
				g.rcOptCtx.DefineVar(bi.sym.Str)
			}
		}

		// Check if arena fallback is needed
		if g.useArenaFallback {
			shape := g.shapeCtx.ResultShape
			if shape == analysis.ShapeCyclic || shape == analysis.ShapeUnknown {
				needsArena = true
			}
		}
	}

	// Convert bindings for arena generator
	arenaBindings := make([]struct {
		sym *ast.Value
		val string
	}, len(bindings))

	for i, bi := range bindings {
		valStr := ""
		if ast.IsCode(bi.val) {
			valStr = bi.val.Str
		} else {
			valStr = g.ValueToCExpr(bi.val)
		}
		arenaBindings[i].sym = bi.sym
		arenaBindings[i].val = valStr
	}

	bodyStr := ""
	if ast.IsCode(body) {
		bodyStr = body.Str
	} else {
		bodyStr = g.ValueToCExpr(body)
	}

	// Use arena generator if needed
	if needsArena {
		return g.arenaGen.GenerateArenaLet(arenaBindings, bodyStr, true)
	}

	// Standard ASAP code generation
	var sb strings.Builder
	sb.WriteString("({\n")

	// Generate declarations
	for _, bi := range arenaBindings {
		sb.WriteString(fmt.Sprintf("    Obj* %s = %s;\n", bi.sym.Str, bi.val))
	}

	sb.WriteString(fmt.Sprintf("    Obj* _res = %s;\n", bodyStr))

	// Generate frees based on analysis
	for i := len(bindings) - 1; i >= 0; i-- {
		bi := bindings[i]
		usage := g.escapeCtx.FindVar(bi.sym.Str)
		shapeInfo := g.shapeCtx.FindShape(bi.sym.Str)

		isCaptured := usage != nil && usage.CapturedByLambda
		escapeClass := analysis.EscapeNone
		if usage != nil {
			escapeClass = usage.Escape
		}
		shape := analysis.ShapeUnknown
		if shapeInfo != nil {
			shape = shapeInfo.Shape
		}

		// RC Optimization: Get optimized free function
		var freeFn string
		var rcOptComment string
		if g.enableRCOpt {
			freeFn = g.rcOptCtx.GetFreeFunction(bi.sym.Str, shape)
			if freeFn == "" {
				// RC optimization eliminated this free
				rcOptComment = fmt.Sprintf("    /* %s: RC elided (alias of another var) */\n", bi.sym.Str)
			} else if freeFn == "free_unique" {
				rcOptComment = fmt.Sprintf("    %s(%s); /* RC opt: proven unique */\n", freeFn, bi.sym.Str)
			} else {
				rcOptComment = fmt.Sprintf("    %s(%s); /* ASAP Clean (shape: %s) */\n",
					freeFn, bi.sym.Str, analysis.ShapeString(shape))
			}
		} else {
			freeFn = analysis.ShapeFreeStrategy(shape)
			rcOptComment = fmt.Sprintf("    %s(%s); /* ASAP Clean (shape: %s) */\n",
				freeFn, bi.sym.Str, analysis.ShapeString(shape))
		}

		if isCaptured {
			sb.WriteString(fmt.Sprintf("    /* %s captured by closure - no free */\n", bi.sym.Str))
		} else if escapeClass == analysis.EscapeGlobal {
			sb.WriteString(fmt.Sprintf("    /* %s escapes to return - no free */\n", bi.sym.Str))
		} else {
			sb.WriteString(rcOptComment)
		}
	}

	sb.WriteString("    _res;\n})")

	return sb.String()
}

// GenerateProgram generates a complete C program
func (g *CodeGenerator) GenerateProgram(exprs []*ast.Value) {
	// Generate runtime
	runtime := NewRuntimeGenerator(g.w, g.registry)
	runtime.GenerateAll()

	// Generate main function
	g.emit("\nint main(void) {\n")
	g.emit("    Obj* result;\n")

	for _, expr := range exprs {
		if ast.IsCode(expr) {
			g.emit("    result = %s;\n", expr.Str)
		} else {
			g.emit("    result = %s;\n", g.ValueToCExpr(expr))
		}
		g.emit("    if (result && !result->is_pair) {\n")
		g.emit("        printf(\"Result: %%ld\\n\", result->i);\n")
		g.emit("    }\n")
		g.emit("    free_obj(result);\n")
	}

	g.emit("    flush_freelist();\n")
	g.emit("    return 0;\n")
	g.emit("}\n")
}

// GenerateProgramToString generates a complete C program as a string
func GenerateProgramToString(exprs []*ast.Value) string {
	var sb strings.Builder
	gen := NewCodeGenerator(&sb)
	gen.GenerateProgram(exprs)
	return sb.String()
}

// GenerateProgram is a convenience wrapper for a single expression.
func GenerateProgram(expr *ast.Value) string {
	return GenerateProgramToString([]*ast.Value{expr})
}

// FreePoint represents a point where a variable should be freed
type FreePoint struct {
	VarName       string
	Point         int
	IsConditional bool
	FreeFn        string
}

// GenerateFreePlacement generates optimal free placement based on analysis
func GenerateFreePlacement(expr *ast.Value, vars []string) []FreePoint {
	escapeCtx := analysis.NewAnalysisContext()
	shapeCtx := analysis.NewShapeContext()

	for _, v := range vars {
		escapeCtx.AddVar(v)
	}

	escapeCtx.AnalyzeExpr(expr)
	escapeCtx.AnalyzeEscape(expr, analysis.EscapeGlobal)
	shapeCtx.AnalyzeShapes(expr)

	var freePoints []FreePoint
	for _, v := range vars {
		usage := escapeCtx.FindVar(v)
		shapeInfo := shapeCtx.FindShape(v)

		if usage == nil {
			continue
		}

		if usage.CapturedByLambda {
			continue // Don't free captured variables
		}

		if usage.Escape == analysis.EscapeGlobal {
			continue // Don't free escaping variables
		}

		shape := analysis.ShapeUnknown
		if shapeInfo != nil {
			shape = shapeInfo.Shape
		}

		freePoints = append(freePoints, FreePoint{
			VarName:       v,
			Point:         usage.LastUseDepth,
			IsConditional: false,
			FreeFn:        analysis.ShapeFreeStrategy(shape),
		})
	}

	return freePoints
}
