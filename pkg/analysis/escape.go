package analysis

import "purple_go/pkg/ast"

// EscapeClass represents how a variable escapes its scope
type EscapeClass int

const (
	EscapeNone   EscapeClass = iota // Value stays local
	EscapeArg                       // Escapes via function argument
	EscapeGlobal                    // Escapes to return/closure
)

// VarUsage tracks variable usage information
type VarUsage struct {
	Name             string
	UseCount         int
	LastUseDepth     int
	Escape           EscapeClass
	CapturedByLambda bool
	Freed            bool
}

// AnalysisContext holds analysis state
type AnalysisContext struct {
	Vars         map[string]*VarUsage
	CurrentDepth int
	InLambda     bool
}

// NewAnalysisContext creates a new analysis context
func NewAnalysisContext() *AnalysisContext {
	return &AnalysisContext{
		Vars:         make(map[string]*VarUsage),
		CurrentDepth: 0,
		InLambda:     false,
	}
}

// FindVar looks up a variable
func (ctx *AnalysisContext) FindVar(name string) *VarUsage {
	return ctx.Vars[name]
}

// AddVar adds a new variable to track
func (ctx *AnalysisContext) AddVar(name string) {
	ctx.Vars[name] = &VarUsage{
		Name:         name,
		UseCount:     0,
		LastUseDepth: -1,
		Escape:       EscapeNone,
	}
}

// RecordUse records a variable usage
func (ctx *AnalysisContext) RecordUse(name string) {
	if v, ok := ctx.Vars[name]; ok {
		v.UseCount++
		v.LastUseDepth = ctx.CurrentDepth
		if ctx.InLambda {
			v.CapturedByLambda = true
		}
	}
}

// AnalyzeExpr analyzes an expression for variable usage
func (ctx *AnalysisContext) AnalyzeExpr(expr *ast.Value) {
	if expr == nil || ast.IsNil(expr) {
		return
	}

	ctx.CurrentDepth++
	defer func() { ctx.CurrentDepth-- }()

	switch expr.Tag {
	case ast.TSym:
		ctx.RecordUse(expr.Str)

	case ast.TCell:
		op := expr.Car
		args := expr.Cdr

		if ast.IsSym(op) {
			switch op.Str {
			case "quote":
				// Don't analyze quoted expressions
				return

			case "lambda":
				savedInLambda := ctx.InLambda
				ctx.InLambda = true
				if !ast.IsNil(args) && ast.IsCell(args) {
					if !ast.IsNil(args.Cdr) && ast.IsCell(args.Cdr) {
						ctx.AnalyzeExpr(args.Cdr.Car)
					}
				}
				ctx.InLambda = savedInLambda

			case "let", "letrec":
				bindings := args.Car
				body := args.Cdr.Car
				for !ast.IsNil(bindings) && ast.IsCell(bindings) {
					bind := bindings.Car
					if ast.IsCell(bind) && !ast.IsNil(bind.Cdr) {
						ctx.AnalyzeExpr(bind.Cdr.Car)
					}
					bindings = bindings.Cdr
				}
				ctx.AnalyzeExpr(body)

			case "if":
				ctx.analyzeList(args)

			default:
				ctx.AnalyzeExpr(op)
				ctx.analyzeList(args)
			}
		} else {
			ctx.AnalyzeExpr(op)
			ctx.analyzeList(args)
		}
	}
}

func (ctx *AnalysisContext) analyzeList(list *ast.Value) {
	for !ast.IsNil(list) && ast.IsCell(list) {
		ctx.AnalyzeExpr(list.Car)
		list = list.Cdr
	}
}

// AnalyzeEscape analyzes escape behavior of variables
func (ctx *AnalysisContext) AnalyzeEscape(expr *ast.Value, context EscapeClass) {
	if expr == nil || ast.IsNil(expr) {
		return
	}

	switch expr.Tag {
	case ast.TSym:
		if v, ok := ctx.Vars[expr.Str]; ok {
			if context > v.Escape {
				v.Escape = context
			}
		}

	case ast.TCell:
		op := expr.Car
		args := expr.Cdr

		if ast.IsSym(op) {
			switch op.Str {
			case "lambda":
				savedInLambda := ctx.InLambda
				ctx.InLambda = true
				if !ast.IsNil(args) && ast.IsCell(args) {
					if !ast.IsNil(args.Cdr) && ast.IsCell(args.Cdr) {
						ctx.AnalyzeEscape(args.Cdr.Car, EscapeGlobal)
					}
				}
				ctx.InLambda = savedInLambda

			case "let":
				bindings := args.Car
				body := args.Cdr.Car
				for !ast.IsNil(bindings) && ast.IsCell(bindings) {
					bind := bindings.Car
					if ast.IsCell(bind) && !ast.IsNil(bind.Cdr) {
						ctx.AnalyzeEscape(bind.Cdr.Car, EscapeNone)
					}
					bindings = bindings.Cdr
				}
				ctx.AnalyzeEscape(body, context)

			case "letrec":
				bindings := args.Car
				body := args.Cdr.Car
				// Mark all bound vars as potentially escaping
				b := bindings
				for !ast.IsNil(b) && ast.IsCell(b) {
					bind := b.Car
					if ast.IsCell(bind) && ast.IsSym(bind.Car) {
						if v, ok := ctx.Vars[bind.Car.Str]; ok {
							v.Escape = EscapeGlobal
						}
					}
					b = b.Cdr
				}
				// Analyze binding expressions
				for !ast.IsNil(bindings) && ast.IsCell(bindings) {
					bind := bindings.Car
					if ast.IsCell(bind) && !ast.IsNil(bind.Cdr) {
						ctx.AnalyzeEscape(bind.Cdr.Car, EscapeGlobal)
					}
					bindings = bindings.Cdr
				}
				ctx.AnalyzeEscape(body, context)

			case "set!":
				target := args.Car
				if ast.IsSym(target) {
					if v, ok := ctx.Vars[target.Str]; ok {
						v.Escape = EscapeGlobal
					}
				}
				if !ast.IsNil(args.Cdr) {
					ctx.AnalyzeEscape(args.Cdr.Car, EscapeGlobal)
				}

			case "cons":
				for !ast.IsNil(args) && ast.IsCell(args) {
					ctx.AnalyzeEscape(args.Car, EscapeArg)
					args = args.Cdr
				}

			default:
				for !ast.IsNil(args) && ast.IsCell(args) {
					ctx.AnalyzeEscape(args.Car, EscapeArg)
					args = args.Cdr
				}
			}
		} else {
			ctx.AnalyzeEscape(op, EscapeArg)
			for !ast.IsNil(args) && ast.IsCell(args) {
				ctx.AnalyzeEscape(args.Car, EscapeArg)
				args = args.Cdr
			}
		}
	}
}

// FindFreeVars finds free variables in an expression
func FindFreeVars(expr *ast.Value, bound map[string]bool) []string {
	var freeVars []string
	seen := make(map[string]bool)

	var find func(e *ast.Value, b map[string]bool)
	find = func(e *ast.Value, b map[string]bool) {
		if e == nil || ast.IsNil(e) {
			return
		}

		if ast.IsSym(e) {
			if !b[e.Str] && !seen[e.Str] {
				freeVars = append(freeVars, e.Str)
				seen[e.Str] = true
			}
			return
		}

		if ast.IsCell(e) {
			op := e.Car
			args := e.Cdr

			if ast.IsSym(op) {
				switch op.Str {
				case "quote":
					return

				case "lambda":
					params := args.Car
					body := args.Cdr.Car
					newBound := copyMap(b)
					for !ast.IsNil(params) && ast.IsCell(params) {
						if ast.IsSym(params.Car) {
							newBound[params.Car.Str] = true
						}
						params = params.Cdr
					}
					find(body, newBound)
					return

				case "let", "letrec":
					bindings := args.Car
					body := args.Cdr.Car
					newBound := copyMap(b)
					for !ast.IsNil(bindings) && ast.IsCell(bindings) {
						bind := bindings.Car
						sym := bind.Car
						valExpr := bind.Cdr.Car
						find(valExpr, b)
						if ast.IsSym(sym) {
							newBound[sym.Str] = true
						}
						bindings = bindings.Cdr
					}
					find(body, newBound)
					return
				}
			}

			find(op, b)
			for !ast.IsNil(args) && ast.IsCell(args) {
				find(args.Car, b)
				args = args.Cdr
			}
		}
	}

	find(expr, bound)
	return freeVars
}

func copyMap(m map[string]bool) map[string]bool {
	result := make(map[string]bool)
	for k, v := range m {
		result[k] = v
	}
	return result
}
