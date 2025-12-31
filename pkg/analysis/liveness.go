package analysis

import "purple_go/pkg/ast"

// LivenessInfo tracks liveness information for a variable
type LivenessInfo struct {
	Name      string
	LastUse   int  // Program point of last use
	IsLive    bool // Currently live
	CanFreeAt int  // Earliest point we can free
}

// LivenessContext holds liveness analysis state
type LivenessContext struct {
	Vars          map[string]*LivenessInfo
	CurrentPoint  int
	InLoop        bool
	LoopDepth     int
}

// NewLivenessContext creates a new liveness analysis context
func NewLivenessContext() *LivenessContext {
	return &LivenessContext{
		Vars:         make(map[string]*LivenessInfo),
		CurrentPoint: 0,
	}
}

// AddVar adds a variable to track
func (ctx *LivenessContext) AddVar(name string) {
	ctx.Vars[name] = &LivenessInfo{
		Name:      name,
		LastUse:   -1,
		IsLive:    true,
		CanFreeAt: -1,
	}
}

// RecordUse records a variable use
func (ctx *LivenessContext) RecordUse(name string) {
	if v, ok := ctx.Vars[name]; ok {
		v.LastUse = ctx.CurrentPoint
		v.IsLive = true
		// If in a loop, we can't free until after the loop
		if ctx.InLoop {
			v.CanFreeAt = -1 // Will be set after loop analysis
		}
	}
}

// MarkDead marks a variable as dead (can be freed)
func (ctx *LivenessContext) MarkDead(name string) {
	if v, ok := ctx.Vars[name]; ok {
		v.IsLive = false
		if v.CanFreeAt == -1 {
			v.CanFreeAt = ctx.CurrentPoint
		}
	}
}

// AnalyzeLiveness performs liveness analysis on an expression
func (ctx *LivenessContext) AnalyzeLiveness(expr *ast.Value) {
	if expr == nil || ast.IsNil(expr) {
		return
	}

	ctx.CurrentPoint++

	switch expr.Tag {
	case ast.TSym:
		ctx.RecordUse(expr.Str)

	case ast.TCell:
		op := expr.Car
		args := expr.Cdr

		if ast.IsSym(op) {
			switch op.Str {
			case "quote":
				return

			case "lambda":
				// Lambda body is analyzed separately
				if !ast.IsNil(args) && ast.IsCell(args) {
					if !ast.IsNil(args.Cdr) && ast.IsCell(args.Cdr) {
						ctx.AnalyzeLiveness(args.Cdr.Car)
					}
				}

			case "let", "letrec":
				bindings := args.Car
				body := args.Cdr.Car
				for !ast.IsNil(bindings) && ast.IsCell(bindings) {
					bind := bindings.Car
					if ast.IsCell(bind) {
						sym := bind.Car
						valExpr := bind.Cdr.Car
						ctx.AnalyzeLiveness(valExpr)
						if ast.IsSym(sym) {
							ctx.AddVar(sym.Str)
						}
					}
					bindings = bindings.Cdr
				}
				ctx.AnalyzeLiveness(body)

			case "if":
				// For if, we need to consider both branches
				cond := args.Car
				thenBr := args.Cdr.Car
				var elseBr *ast.Value
				if !ast.IsNil(args.Cdr.Cdr) && ast.IsCell(args.Cdr.Cdr) {
					elseBr = args.Cdr.Cdr.Car
				}
				ctx.AnalyzeLiveness(cond)
				ctx.AnalyzeLiveness(thenBr)
				ctx.AnalyzeLiveness(elseBr)

			default:
				ctx.AnalyzeLiveness(op)
				ctx.analyzeListLiveness(args)
			}
		} else {
			ctx.AnalyzeLiveness(op)
			ctx.analyzeListLiveness(args)
		}
	}
}

func (ctx *LivenessContext) analyzeListLiveness(list *ast.Value) {
	for !ast.IsNil(list) && ast.IsCell(list) {
		ctx.AnalyzeLiveness(list.Car)
		list = list.Cdr
	}
}

// GetFreePoint returns the earliest point a variable can be freed
func (ctx *LivenessContext) GetFreePoint(name string) int {
	if v, ok := ctx.Vars[name]; ok {
		if v.CanFreeAt >= 0 {
			return v.CanFreeAt
		}
		return v.LastUse
	}
	return -1
}

// ComputeFreePlacements determines where to place free calls
func ComputeFreePlacements(expr *ast.Value, vars []string) map[string]int {
	ctx := NewLivenessContext()
	for _, v := range vars {
		ctx.AddVar(v)
	}
	ctx.AnalyzeLiveness(expr)

	placements := make(map[string]int)
	for _, v := range vars {
		placements[v] = ctx.GetFreePoint(v)
	}
	return placements
}
