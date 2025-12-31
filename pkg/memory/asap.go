package memory

import (
	"fmt"
	"io"
	"strings"

	"purple_go/pkg/analysis"
	"purple_go/pkg/ast"
)

// ASAPGenerator generates ASAP (As Static As Possible) memory management code
type ASAPGenerator struct {
	w         io.Writer
	escapeCtx *analysis.AnalysisContext
	shapeCtx  *analysis.ShapeContext
}

// NewASAPGenerator creates a new ASAP generator
func NewASAPGenerator(w io.Writer) *ASAPGenerator {
	return &ASAPGenerator{
		w:         w,
		escapeCtx: analysis.NewAnalysisContext(),
		shapeCtx:  analysis.NewShapeContext(),
	}
}

func (g *ASAPGenerator) emit(format string, args ...interface{}) {
	fmt.Fprintf(g.w, format, args...)
}

// AnalyzeAndInjectFrees analyzes an expression and returns free injection points
func (g *ASAPGenerator) AnalyzeAndInjectFrees(expr *ast.Value, boundVars []string) []FreeInjection {
	// Add variables to tracking
	for _, v := range boundVars {
		g.escapeCtx.AddVar(v)
	}

	// Analyze expression
	g.escapeCtx.AnalyzeExpr(expr)
	g.escapeCtx.AnalyzeEscape(expr, analysis.EscapeGlobal)
	g.shapeCtx.AnalyzeShapes(expr)

	var injections []FreeInjection

	for _, varName := range boundVars {
		usage := g.escapeCtx.FindVar(varName)
		shapeInfo := g.shapeCtx.FindShape(varName)

		if usage == nil {
			continue
		}

		// Determine if we should free this variable
		shouldFree := true
		reason := ""

		if usage.CapturedByLambda {
			shouldFree = false
			reason = "captured by closure"
		} else if usage.Escape == analysis.EscapeGlobal {
			shouldFree = false
			reason = "escapes to return"
		} else if usage.UseCount == 0 {
			reason = "unused"
		}

		// Determine shape-based free strategy
		shape := analysis.ShapeUnknown
		if shapeInfo != nil {
			shape = shapeInfo.Shape
		}
		freeFn := analysis.ShapeFreeStrategy(shape)

		injections = append(injections, FreeInjection{
			VarName:    varName,
			ShouldFree: shouldFree,
			FreeFn:     freeFn,
			Shape:      shape,
			Reason:     reason,
			Point:      usage.LastUseDepth,
		})
	}

	return injections
}

// FreeInjection represents a point where a free should be injected
type FreeInjection struct {
	VarName    string
	ShouldFree bool
	FreeFn     string
	Shape      analysis.Shape
	Reason     string
	Point      int // Program point for the free
}

// GenerateCleanPhase generates the CLEAN phase code for a let binding
func (g *ASAPGenerator) GenerateCleanPhase(bindings []struct {
	sym  *ast.Value
	val  string
	code bool
}, bodyCode string) string {
	var sb strings.Builder

	// Collect variable names
	var varNames []string
	for _, b := range bindings {
		if ast.IsSym(b.sym) {
			varNames = append(varNames, b.sym.Str)
		}
	}

	sb.WriteString("({\n")

	// Generate declarations
	for _, b := range bindings {
		sb.WriteString(fmt.Sprintf("    Obj* %s = %s;\n", b.sym.Str, b.val))
	}

	// Generate body
	sb.WriteString(fmt.Sprintf("    Obj* _res = %s;\n", bodyCode))

	// Analyze and generate frees
	// For simplicity, we generate frees in reverse order
	for i := len(bindings) - 1; i >= 0; i-- {
		varName := bindings[i].sym.Str
		usage := g.escapeCtx.FindVar(varName)
		shapeInfo := g.shapeCtx.FindShape(varName)

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
			sb.WriteString(fmt.Sprintf("    /* %s captured by closure - ownership transferred */\n", varName))
		} else if escapeClass == analysis.EscapeGlobal {
			sb.WriteString(fmt.Sprintf("    /* %s escapes to return - no free */\n", varName))
		} else {
			sb.WriteString(fmt.Sprintf("    %s(%s); /* ASAP CLEAN (shape: %s) */\n",
				freeFn, varName, analysis.ShapeString(shape)))
		}
	}

	sb.WriteString("    _res;\n})")
	return sb.String()
}

// GenerateScannerCall generates a scanner call for debugging/verification
func GenerateScannerCall(typeName string, varName string) string {
	return fmt.Sprintf("scan_%s(%s); /* ASAP Mark */", typeName, varName)
}

// GenerateClearMarksCall generates a clear marks call
func GenerateClearMarksCall(typeName string, varName string) string {
	return fmt.Sprintf("clear_marks_%s(%s);", typeName, varName)
}
