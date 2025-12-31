package eval

import (
	"purple_go/pkg/ast"
)

// Pattern types for matching
const (
	PatWildcard    = iota // _
	PatVar                // x
	PatLit                // 42
	PatCons               // (cons a b) or (CON ...)
	PatNil                // nil or ()
	PatOr                 // (or pat1 pat2 ...)
	PatAs                 // (x @ pat)
	PatQuote              // 'literal
)

// Pattern represents a compiled pattern
type Pattern struct {
	Type    int
	Name    string      // for PatVar, PatAs
	Lit     *ast.Value  // for PatLit, PatQuote
	SubPats []*Pattern  // for PatCons, PatOr
	AsPat   *Pattern    // for PatAs (the inner pattern)
}

// MatchResult holds bindings from a successful match
type MatchResult struct {
	Success  bool
	Bindings map[string]*ast.Value
}

// CompilePattern compiles an AST pattern into a Pattern struct
func CompilePattern(pat *ast.Value) *Pattern {
	if pat == nil || ast.IsNil(pat) {
		return &Pattern{Type: PatNil}
	}

	// Wildcard: _
	if ast.IsSym(pat) && pat.Str == "_" {
		return &Pattern{Type: PatWildcard}
	}

	// Variable pattern: any symbol except special ones
	if ast.IsSym(pat) {
		if pat.Str == "nil" {
			return &Pattern{Type: PatNil}
		}
		return &Pattern{Type: PatVar, Name: pat.Str}
	}

	// Literal pattern: number
	if ast.IsInt(pat) {
		return &Pattern{Type: PatLit, Lit: pat}
	}

	// List pattern
	if ast.IsCell(pat) {
		head := pat.Car

		// Quote pattern: 'x
		if ast.SymEqStr(head, "quote") && !ast.IsNil(pat.Cdr) {
			return &Pattern{Type: PatQuote, Lit: pat.Cdr.Car}
		}

		// Or pattern: (or pat1 pat2 ...)
		if ast.SymEqStr(head, "or") {
			var subPats []*Pattern
			rest := pat.Cdr
			for !ast.IsNil(rest) && ast.IsCell(rest) {
				subPats = append(subPats, CompilePattern(rest.Car))
				rest = rest.Cdr
			}
			return &Pattern{Type: PatOr, SubPats: subPats}
		}

		// As pattern: (x @ pat) - we'll represent as (@ x pat)
		if ast.SymEqStr(head, "@") && !ast.IsNil(pat.Cdr) {
			name := pat.Cdr.Car
			subPat := pat.Cdr.Cdr.Car
			if ast.IsSym(name) {
				return &Pattern{
					Type:  PatAs,
					Name:  name.Str,
					AsPat: CompilePattern(subPat),
				}
			}
		}

		// Cons pattern: (cons a b) or constructor (TAG ...)
		if ast.SymEqStr(head, "cons") && !ast.IsNil(pat.Cdr) {
			carPat := CompilePattern(pat.Cdr.Car)
			cdrPat := &Pattern{Type: PatNil}
			if !ast.IsNil(pat.Cdr.Cdr) {
				cdrPat = CompilePattern(pat.Cdr.Cdr.Car)
			}
			return &Pattern{Type: PatCons, SubPats: []*Pattern{carPat, cdrPat}}
		}

		// List pattern: (list a b c ...)
		if ast.SymEqStr(head, "list") {
			return compileListPattern(pat.Cdr)
		}

		// Generic constructor pattern: (TAG arg1 arg2 ...)
		if ast.IsSym(head) {
			var subPats []*Pattern
			rest := pat.Cdr
			for !ast.IsNil(rest) && ast.IsCell(rest) {
				subPats = append(subPats, CompilePattern(rest.Car))
				rest = rest.Cdr
			}
			return &Pattern{Type: PatCons, Name: head.Str, SubPats: subPats}
		}
	}

	// Default: treat as literal
	return &Pattern{Type: PatLit, Lit: pat}
}

// compileListPattern converts (list a b c) to nested cons pattern
func compileListPattern(elements *ast.Value) *Pattern {
	if ast.IsNil(elements) {
		return &Pattern{Type: PatNil}
	}

	// Check for dotted list: (list a b . rest)
	if ast.IsCell(elements) {
		// Look for dot
		rest := elements
		for !ast.IsNil(rest) && ast.IsCell(rest) {
			if ast.IsSym(rest.Car) && rest.Car.Str == "." {
				// Dotted list - rest.Cdr.Car is the tail pattern
				if !ast.IsNil(rest.Cdr) {
					// Build cons chain up to dot
					return buildConsChainUntilDot(elements, CompilePattern(rest.Cdr.Car))
				}
			}
			rest = rest.Cdr
		}
	}

	// Regular list - build nested cons with nil at end
	if !ast.IsCell(elements) {
		return &Pattern{Type: PatNil}
	}

	headPat := CompilePattern(elements.Car)
	tailPat := compileListPattern(elements.Cdr)
	return &Pattern{Type: PatCons, SubPats: []*Pattern{headPat, tailPat}}
}

func buildConsChainUntilDot(elements *ast.Value, tailPat *Pattern) *Pattern {
	if ast.IsNil(elements) || !ast.IsCell(elements) {
		return tailPat
	}
	if ast.IsSym(elements.Car) && elements.Car.Str == "." {
		return tailPat
	}
	headPat := CompilePattern(elements.Car)
	restPat := buildConsChainUntilDot(elements.Cdr, tailPat)
	return &Pattern{Type: PatCons, SubPats: []*Pattern{headPat, restPat}}
}

// Match attempts to match a value against a pattern
func Match(pat *Pattern, val *ast.Value) *MatchResult {
	bindings := make(map[string]*ast.Value)
	if matchInto(pat, val, bindings) {
		return &MatchResult{Success: true, Bindings: bindings}
	}
	return &MatchResult{Success: false}
}

func matchInto(pat *Pattern, val *ast.Value, bindings map[string]*ast.Value) bool {
	switch pat.Type {
	case PatWildcard:
		return true

	case PatVar:
		bindings[pat.Name] = val
		return true

	case PatNil:
		return val == nil || ast.IsNil(val)

	case PatLit:
		if ast.IsInt(pat.Lit) && ast.IsInt(val) {
			return pat.Lit.Int == val.Int
		}
		if ast.IsSym(pat.Lit) && ast.IsSym(val) {
			return pat.Lit.Str == val.Str
		}
		return false

	case PatQuote:
		return valuesEqual(pat.Lit, val)

	case PatCons:
		if val == nil || ast.IsNil(val) || !ast.IsCell(val) {
			return false
		}
		if len(pat.SubPats) >= 1 {
			if !matchInto(pat.SubPats[0], val.Car, bindings) {
				return false
			}
		}
		if len(pat.SubPats) >= 2 {
			if !matchInto(pat.SubPats[1], val.Cdr, bindings) {
				return false
			}
		}
		return true

	case PatOr:
		for _, subPat := range pat.SubPats {
			subBindings := make(map[string]*ast.Value)
			if matchInto(subPat, val, subBindings) {
				for k, v := range subBindings {
					bindings[k] = v
				}
				return true
			}
		}
		return false

	case PatAs:
		bindings[pat.Name] = val
		return matchInto(pat.AsPat, val, bindings)
	}

	return false
}

// valuesEqual checks structural equality of two values
func valuesEqual(a, b *ast.Value) bool {
	if a == nil && b == nil {
		return true
	}
	if a == nil || b == nil {
		return false
	}
	if ast.IsNil(a) && ast.IsNil(b) {
		return true
	}
	if a.Tag != b.Tag {
		return false
	}

	switch a.Tag {
	case ast.TInt:
		return a.Int == b.Int
	case ast.TSym:
		return a.Str == b.Str
	case ast.TCell:
		return valuesEqual(a.Car, b.Car) && valuesEqual(a.Cdr, b.Cdr)
	case ast.TNil:
		return true
	default:
		return false
	}
}

// EvalMatch evaluates a match expression
// (match expr (pat1 body1) (pat2 body2) ...)
func EvalMatch(expr *ast.Value, menv *ast.Value) *ast.Value {
	args := expr.Cdr
	if ast.IsNil(args) {
		return ast.Nil
	}

	// Evaluate the scrutinee
	scrutinee := Eval(args.Car, menv)
	cases := args.Cdr

	// Try each case
	for !ast.IsNil(cases) && ast.IsCell(cases) {
		caseExpr := cases.Car
		if !ast.IsCell(caseExpr) {
			cases = cases.Cdr
			continue
		}

		patExpr := caseExpr.Car
		bodyExpr := caseExpr.Cdr.Car

		// Check for guard: (pat :when guard body)
		var guardExpr *ast.Value
		if !ast.IsNil(caseExpr.Cdr.Cdr) && ast.IsCell(caseExpr.Cdr.Cdr) {
			maybeWhen := caseExpr.Cdr.Car
			if ast.SymEqStr(maybeWhen, ":when") {
				guardExpr = caseExpr.Cdr.Cdr.Car
				bodyExpr = caseExpr.Cdr.Cdr.Cdr.Car
			}
		}

		pat := CompilePattern(patExpr)
		result := Match(pat, scrutinee)

		if result.Success {
			// Extend environment with bindings
			newEnv := menv.Env
			for name, val := range result.Bindings {
				newEnv = EnvExtend(newEnv, ast.NewSym(name), val)
			}

			bodyMenv := NewMenv(menv.Parent, newEnv)
			bodyMenv.HApp = menv.HApp
			bodyMenv.HLet = menv.HLet
			bodyMenv.HIf = menv.HIf
			bodyMenv.HLit = menv.HLit
			bodyMenv.HVar = menv.HVar

			// Check guard if present
			if guardExpr != nil {
				guardResult := Eval(guardExpr, bodyMenv)
				if ast.IsNil(guardResult) || (ast.IsInt(guardResult) && guardResult.Int == 0) {
					cases = cases.Cdr
					continue
				}
			}

			return Eval(bodyExpr, bodyMenv)
		}

		cases = cases.Cdr
	}

	// No match found
	return ast.Nil
}
