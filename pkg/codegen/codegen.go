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
	w            io.Writer
	registry     *TypeRegistry
	escapeCtx    *analysis.AnalysisContext
	shapeCtx     *analysis.ShapeContext
	tempCounter  int
	indentLevel  int
}

// NewCodeGenerator creates a new code generator
func NewCodeGenerator(w io.Writer) *CodeGenerator {
	registry := NewTypeRegistry()
	registry.InitDefaultTypes()
	return &CodeGenerator{
		w:         w,
		registry:  registry,
		escapeCtx: analysis.NewAnalysisContext(),
		shapeCtx:  analysis.NewShapeContext(),
	}
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
	var sb strings.Builder

	// Analyze the expression for escape and shape
	for _, bi := range bindings {
		g.escapeCtx.AddVar(bi.sym.Str)
		g.shapeCtx.AnalyzeShapes(bi.val)
		g.shapeCtx.AddShape(bi.sym.Str, g.shapeCtx.ResultShape)
	}

	sb.WriteString("({\n")

	// Generate declarations
	for _, bi := range bindings {
		valStr := ""
		if ast.IsCode(bi.val) {
			valStr = bi.val.Str
		} else {
			valStr = g.ValueToCExpr(bi.val)
		}
		sb.WriteString(fmt.Sprintf("    Obj* %s = %s;\n", bi.sym.Str, valStr))
	}

	// Generate body
	bodyStr := ""
	if ast.IsCode(body) {
		bodyStr = body.Str
	} else {
		bodyStr = g.ValueToCExpr(body)
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

		freeFn := analysis.ShapeFreeStrategy(shape)

		if isCaptured {
			sb.WriteString(fmt.Sprintf("    /* %s captured by closure - no free */\n", bi.sym.Str))
		} else if escapeClass == analysis.EscapeGlobal {
			sb.WriteString(fmt.Sprintf("    /* %s escapes to return - no free */\n", bi.sym.Str))
		} else {
			sb.WriteString(fmt.Sprintf("    %s(%s); /* ASAP Clean (shape: %s) */\n",
				freeFn, bi.sym.Str, analysis.ShapeString(shape)))
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

	for _, expr := range exprs {
		if ast.IsCode(expr) {
			g.emit("    Obj* result = %s;\n", expr.Str)
		} else {
			g.emit("    Obj* result = %s;\n", g.ValueToCExpr(expr))
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
