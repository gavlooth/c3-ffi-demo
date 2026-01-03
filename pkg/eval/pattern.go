package eval

import (
	"purple_go/pkg/ast"
)

// Pattern types for matching
const (
	PatWildcard    = iota // _
	PatVar                // x
	PatLit                // 42, :keyword, #t, #f
	PatCons               // (cons a b) or (CON ...)
	PatNil                // nil or ()
	PatOr                 // (or pat1 pat2 ...)
	PatAnd                // (and pat1 pat2 ...)
	PatNot                // (not pat)
	PatAs                 // (as pat x) or (x @ pat)
	PatQuote              // 'literal
	PatArray              // [p1 p2 ...] or [a .. rest]
	PatDict               // #{:key pat}
	PatTuple              // (tuple p1 p2)
	PatSatisfies          // (? pred)
	PatConstructor        // (TypeName p1 p2)
)

// Pattern represents a compiled pattern
type Pattern struct {
	Type       int
	Name       string      // for PatVar, PatAs, PatConstructor
	Lit        *ast.Value  // for PatLit, PatQuote
	SubPats    []*Pattern  // for PatCons, PatOr, PatAnd, PatArray, PatTuple
	AsPat      *Pattern    // for PatAs (the inner pattern)
	RestIdx    int         // for PatArray with rest pattern (-1 if none)
	DictKeys   []*ast.Value // for PatDict
	DictPats   []*Pattern   // for PatDict
	Predicate  *ast.Value   // for PatSatisfies
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

	// Special symbols
	if ast.IsSym(pat) {
		switch pat.Str {
		case "nil":
			return &Pattern{Type: PatNil}
		case "nothing":
			return &Pattern{Type: PatLit, Lit: ast.Nothing}
		case "#t", "true":
			return &Pattern{Type: PatLit, Lit: ast.NewSym("#t")}
		case "#f", "false":
			return &Pattern{Type: PatLit, Lit: ast.NewSym("#f")}
		default:
			// Variable pattern
			return &Pattern{Type: PatVar, Name: pat.Str}
		}
	}

	// Literal integer
	if ast.IsInt(pat) {
		return &Pattern{Type: PatLit, Lit: pat}
	}

	// Literal float
	if ast.IsFloat(pat) {
		return &Pattern{Type: PatLit, Lit: pat}
	}

	// Literal char
	if ast.IsChar(pat) {
		return &Pattern{Type: PatLit, Lit: pat}
	}

	// Keyword literal
	if ast.IsKeyword(pat) {
		return &Pattern{Type: PatLit, Lit: pat}
	}

	// Array pattern [p1 p2 ...] or [a .. rest]
	if ast.IsArray(pat) {
		return compileArrayPattern(pat)
	}

	// Dict pattern #{:key pat}
	if ast.IsDict(pat) {
		return compileDictPattern(pat)
	}

	// Tuple pattern
	if ast.IsTuple(pat) {
		return compileTuplePattern(pat)
	}

	// List patterns (special forms)
	if ast.IsCell(pat) {
		return compileListPatternForm(pat)
	}

	// Default: treat as literal
	return &Pattern{Type: PatLit, Lit: pat}
}

// compileArrayPattern compiles [p1 p2 ...] or [a .. rest]
func compileArrayPattern(pat *ast.Value) *Pattern {
	elems := pat.ArrayData
	restIdx := -1

	// Find rest pattern (..)
	for i, elem := range elems {
		if ast.IsSym(elem) && (elem.Str == ".." || elem.Str == "...") {
			restIdx = i
			break
		}
	}

	var subPats []*Pattern
	for i, elem := range elems {
		if i == restIdx {
			continue // Skip the .. marker
		}
		subPats = append(subPats, CompilePattern(elem))
	}

	return &Pattern{
		Type:    PatArray,
		SubPats: subPats,
		RestIdx: restIdx,
	}
}

// compileDictPattern compiles #{:key pat ...}
func compileDictPattern(pat *ast.Value) *Pattern {
	var keys []*ast.Value
	var pats []*Pattern

	for i := range pat.DictKeys {
		keys = append(keys, pat.DictKeys[i])
		pats = append(pats, CompilePattern(pat.DictValues[i]))
	}

	return &Pattern{
		Type:     PatDict,
		DictKeys: keys,
		DictPats: pats,
	}
}

// compileTuplePattern compiles a tuple pattern
func compileTuplePattern(pat *ast.Value) *Pattern {
	var subPats []*Pattern
	for _, elem := range pat.TupleData {
		subPats = append(subPats, CompilePattern(elem))
	}
	return &Pattern{Type: PatTuple, SubPats: subPats}
}

// compileListPatternForm compiles list-form patterns like (cons ...), (or ...), etc.
func compileListPatternForm(pat *ast.Value) *Pattern {
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

	// And pattern: (and pat1 pat2 ...)
	if ast.SymEqStr(head, "and") {
		var subPats []*Pattern
		rest := pat.Cdr
		for !ast.IsNil(rest) && ast.IsCell(rest) {
			subPats = append(subPats, CompilePattern(rest.Car))
			rest = rest.Cdr
		}
		return &Pattern{Type: PatAnd, SubPats: subPats}
	}

	// Not pattern: (not pat)
	if ast.SymEqStr(head, "not") && !ast.IsNil(pat.Cdr) {
		return &Pattern{Type: PatNot, AsPat: CompilePattern(pat.Cdr.Car)}
	}

	// Satisfies pattern: (? pred) or (satisfies pred)
	if (ast.SymEqStr(head, "?") || ast.SymEqStr(head, "satisfies")) && !ast.IsNil(pat.Cdr) {
		return &Pattern{Type: PatSatisfies, Predicate: pat.Cdr.Car}
	}

	// As pattern: (as pat x)
	if ast.SymEqStr(head, "as") && !ast.IsNil(pat.Cdr) && !ast.IsNil(pat.Cdr.Cdr) {
		innerPat := CompilePattern(pat.Cdr.Car)
		name := pat.Cdr.Cdr.Car
		if ast.IsSym(name) {
			return &Pattern{Type: PatAs, Name: name.Str, AsPat: innerPat}
		}
	}

	// Legacy @ pattern: (@ x pat) or (x @ pat)
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

	// Cons pattern: (cons a b)
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

	// Tuple pattern: (tuple a b c)
	if ast.SymEqStr(head, "tuple") {
		var subPats []*Pattern
		rest := pat.Cdr
		for !ast.IsNil(rest) && ast.IsCell(rest) {
			subPats = append(subPats, CompilePattern(rest.Car))
			rest = rest.Cdr
		}
		return &Pattern{Type: PatTuple, SubPats: subPats}
	}

	// Generic constructor pattern: (TypeName arg1 arg2 ...)
	if ast.IsSym(head) {
		var subPats []*Pattern
		rest := pat.Cdr
		for !ast.IsNil(rest) && ast.IsCell(rest) {
			subPats = append(subPats, CompilePattern(rest.Car))
			rest = rest.Cdr
		}
		return &Pattern{Type: PatConstructor, Name: head.Str, SubPats: subPats}
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
		rest := elements
		for !ast.IsNil(rest) && ast.IsCell(rest) {
			if ast.IsSym(rest.Car) && rest.Car.Str == "." {
				if !ast.IsNil(rest.Cdr) {
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
	if matchInto(pat, val, bindings, nil) {
		return &MatchResult{Success: true, Bindings: bindings}
	}
	return &MatchResult{Success: false}
}

// MatchWithMenv matches with access to environment (for predicates)
func MatchWithMenv(pat *Pattern, val *ast.Value, menv *ast.Value) *MatchResult {
	bindings := make(map[string]*ast.Value)
	if matchInto(pat, val, bindings, menv) {
		return &MatchResult{Success: true, Bindings: bindings}
	}
	return &MatchResult{Success: false}
}

func matchInto(pat *Pattern, val *ast.Value, bindings map[string]*ast.Value, menv *ast.Value) bool {
	switch pat.Type {
	case PatWildcard:
		return true

	case PatVar:
		bindings[pat.Name] = val
		return true

	case PatNil:
		return val == nil || ast.IsNil(val)

	case PatLit:
		return matchLiteral(pat.Lit, val)

	case PatQuote:
		return valuesEqual(pat.Lit, val)

	case PatCons:
		if val == nil || ast.IsNil(val) || !ast.IsCell(val) {
			return false
		}
		if len(pat.SubPats) >= 1 {
			if !matchInto(pat.SubPats[0], val.Car, bindings, menv) {
				return false
			}
		}
		if len(pat.SubPats) >= 2 {
			if !matchInto(pat.SubPats[1], val.Cdr, bindings, menv) {
				return false
			}
		}
		return true

	case PatOr:
		for _, subPat := range pat.SubPats {
			subBindings := make(map[string]*ast.Value)
			if matchInto(subPat, val, subBindings, menv) {
				for k, v := range subBindings {
					bindings[k] = v
				}
				return true
			}
		}
		return false

	case PatAnd:
		for _, subPat := range pat.SubPats {
			if !matchInto(subPat, val, bindings, menv) {
				return false
			}
		}
		return true

	case PatNot:
		subBindings := make(map[string]*ast.Value)
		if matchInto(pat.AsPat, val, subBindings, menv) {
			return false
		}
		return true

	case PatAs:
		bindings[pat.Name] = val
		return matchInto(pat.AsPat, val, bindings, menv)

	case PatArray:
		return matchArrayPattern(pat, val, bindings, menv)

	case PatDict:
		return matchDictPattern(pat, val, bindings, menv)

	case PatTuple:
		return matchTuplePattern(pat, val, bindings, menv)

	case PatSatisfies:
		return matchSatisfies(pat, val, menv)

	case PatConstructor:
		return matchConstructor(pat, val, bindings, menv)
	}

	return false
}

func matchLiteral(lit, val *ast.Value) bool {
	if lit == nil || val == nil {
		return lit == val
	}
	if lit.Tag != val.Tag {
		return false
	}
	switch lit.Tag {
	case ast.TInt:
		return lit.Int == val.Int
	case ast.TFloat:
		return lit.Float == val.Float
	case ast.TChar:
		return lit.Int == val.Int
	case ast.TSym:
		return lit.Str == val.Str
	case ast.TKeyword:
		return lit.Str == val.Str
	case ast.TNothing:
		return true
	case ast.TNil:
		return true
	default:
		return false
	}
}

func matchArrayPattern(pat *Pattern, val *ast.Value, bindings map[string]*ast.Value, menv *ast.Value) bool {
	if !ast.IsArray(val) {
		return false
	}

	valElems := val.ArrayData

	if pat.RestIdx < 0 {
		// No rest pattern - exact length match
		if len(pat.SubPats) != len(valElems) {
			return false
		}
		for i, subPat := range pat.SubPats {
			if !matchInto(subPat, valElems[i], bindings, menv) {
				return false
			}
		}
		return true
	}

	// Rest pattern present
	beforeCount := pat.RestIdx
	afterCount := len(pat.SubPats) - pat.RestIdx

	// Minimum length check
	if len(valElems) < beforeCount+afterCount {
		return false
	}

	// Match before rest
	for i := 0; i < beforeCount; i++ {
		if !matchInto(pat.SubPats[i], valElems[i], bindings, menv) {
			return false
		}
	}

	// Match after rest
	for i := 0; i < afterCount; i++ {
		patIdx := pat.RestIdx + i
		valIdx := len(valElems) - afterCount + i
		if !matchInto(pat.SubPats[patIdx], valElems[valIdx], bindings, menv) {
			return false
		}
	}

	// Bind rest elements if there's a variable for it
	// The element right after .. should be a variable to capture rest
	if pat.RestIdx < len(pat.SubPats) {
		restPat := pat.SubPats[pat.RestIdx]
		if restPat.Type == PatVar {
			restStart := beforeCount
			restEnd := len(valElems) - afterCount
			restElems := make([]*ast.Value, restEnd-restStart)
			copy(restElems, valElems[restStart:restEnd])
			bindings[restPat.Name] = ast.NewArray(restElems)
		}
	}

	return true
}

func matchDictPattern(pat *Pattern, val *ast.Value, bindings map[string]*ast.Value, menv *ast.Value) bool {
	if !ast.IsDict(val) {
		return false
	}

	// Each key in pattern must exist and match
	for i, patKey := range pat.DictKeys {
		found := false
		for j, valKey := range val.DictKeys {
			if ast.ValuesEqual(patKey, valKey) {
				if !matchInto(pat.DictPats[i], val.DictValues[j], bindings, menv) {
					return false
				}
				found = true
				break
			}
		}
		if !found {
			return false
		}
	}

	return true
}

func matchTuplePattern(pat *Pattern, val *ast.Value, bindings map[string]*ast.Value, menv *ast.Value) bool {
	if !ast.IsTuple(val) {
		return false
	}

	if len(pat.SubPats) != len(val.TupleData) {
		return false
	}

	for i, subPat := range pat.SubPats {
		if !matchInto(subPat, val.TupleData[i], bindings, menv) {
			return false
		}
	}

	return true
}

func matchSatisfies(pat *Pattern, val *ast.Value, menv *ast.Value) bool {
	if menv == nil || pat.Predicate == nil {
		return false
	}

	pred := Eval(pat.Predicate, menv)
	result := applyFn(pred, ast.NewCell(val, ast.Nil), menv)

	// Check if result is truthy
	if ast.IsNil(result) {
		return false
	}
	if ast.IsSym(result) && (result.Str == "#f" || result.Str == "false") {
		return false
	}
	if ast.IsNothing(result) {
		return false
	}
	return true
}

func matchConstructor(pat *Pattern, val *ast.Value, bindings map[string]*ast.Value, menv *ast.Value) bool {
	// Match against user-defined type
	if ast.IsUserType(val) && val.UserTypeName == pat.Name {
		// Match field patterns
		if len(pat.SubPats) != len(val.UserTypeFieldOrder) {
			return false
		}
		for i, fieldName := range val.UserTypeFieldOrder {
			fieldVal := val.UserTypeFields[fieldName]
			if !matchInto(pat.SubPats[i], fieldVal, bindings, menv) {
				return false
			}
		}
		return true
	}

	// Also match cons cells tagged with constructor name
	if ast.IsCell(val) && ast.IsSym(val.Car) && val.Car.Str == pat.Name {
		// Match constructor arguments
		rest := val.Cdr
		for i, subPat := range pat.SubPats {
			if ast.IsNil(rest) || !ast.IsCell(rest) {
				return false
			}
			if !matchInto(subPat, rest.Car, bindings, menv) {
				return false
			}
			rest = rest.Cdr
			_ = i
		}
		return ast.IsNil(rest)
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
	case ast.TFloat:
		return a.Float == b.Float
	case ast.TChar:
		return a.Int == b.Int
	case ast.TSym:
		return a.Str == b.Str
	case ast.TKeyword:
		return a.Str == b.Str
	case ast.TCell:
		return valuesEqual(a.Car, b.Car) && valuesEqual(a.Cdr, b.Cdr)
	case ast.TNil:
		return true
	case ast.TNothing:
		return true
	case ast.TArray:
		if len(a.ArrayData) != len(b.ArrayData) {
			return false
		}
		for i := range a.ArrayData {
			if !valuesEqual(a.ArrayData[i], b.ArrayData[i]) {
				return false
			}
		}
		return true
	case ast.TDict:
		if len(a.DictKeys) != len(b.DictKeys) {
			return false
		}
		for i := range a.DictKeys {
			if !valuesEqual(a.DictKeys[i], b.DictKeys[i]) {
				return false
			}
			if !valuesEqual(a.DictValues[i], b.DictValues[i]) {
				return false
			}
		}
		return true
	case ast.TTuple:
		if len(a.TupleData) != len(b.TupleData) {
			return false
		}
		for i := range a.TupleData {
			if !valuesEqual(a.TupleData[i], b.TupleData[i]) {
				return false
			}
		}
		return true
	default:
		return a == b
	}
}

// EvalMatch evaluates a match expression
// OmniLisp syntax: (match expr [pat1 result1] [pat2 :when guard result2] ...)
// Legacy syntax: (match expr (pat1 body1) (pat2 body2) ...)
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

		var patExpr, bodyExpr, guardExpr *ast.Value

		// Check for OmniLisp array syntax [pattern result] or [pattern :when guard result]
		if ast.IsArray(caseExpr) && len(caseExpr.ArrayData) >= 2 {
			patExpr = caseExpr.ArrayData[0]

			// Check for else
			if ast.IsSym(patExpr) && patExpr.Str == "else" {
				return Eval(caseExpr.ArrayData[1], menv)
			}

			// Check for :when guard
			if len(caseExpr.ArrayData) >= 4 {
				maybeWhen := caseExpr.ArrayData[1]
				if ast.IsKeyword(maybeWhen) && maybeWhen.Str == "when" {
					guardExpr = caseExpr.ArrayData[2]
					bodyExpr = caseExpr.ArrayData[3]
				} else {
					bodyExpr = caseExpr.ArrayData[1]
				}
			} else {
				bodyExpr = caseExpr.ArrayData[1]
			}
		} else if ast.IsCell(caseExpr) {
			// Legacy syntax (pattern body) or (pattern :when guard body)
			patExpr = caseExpr.Car
			rest := caseExpr.Cdr

			if !ast.IsNil(rest) && ast.IsCell(rest) {
				maybeWhen := rest.Car
				if ast.SymEqStr(maybeWhen, ":when") && !ast.IsNil(rest.Cdr) {
					guardExpr = rest.Cdr.Car
					if !ast.IsNil(rest.Cdr.Cdr) {
						bodyExpr = rest.Cdr.Cdr.Car
					}
				} else {
					bodyExpr = rest.Car
				}
			}
		} else {
			cases = cases.Cdr
			continue
		}

		if patExpr == nil || bodyExpr == nil {
			cases = cases.Cdr
			continue
		}

		pat := CompilePattern(patExpr)
		result := MatchWithMenv(pat, scrutinee, menv)

		if result.Success {
			// Extend environment with bindings
			newEnv := menv.Env
			for name, val := range result.Bindings {
				newEnv = EnvExtend(newEnv, ast.NewSym(name), val)
			}

			// Create body menv preserving handlers
			bodyMenv := ast.NewMenv(newEnv, menv.Parent, menv.Level, menv.CopyHandlers())

			// Check guard if present
			if guardExpr != nil {
				guardResult := Eval(guardExpr, bodyMenv)
				if !isTruthy(guardResult) {
					cases = cases.Cdr
					continue
				}
			}

			return Eval(bodyExpr, bodyMenv)
		}

		cases = cases.Cdr
	}

	// No match found
	return ast.NewError("match: no matching pattern")
}

// isTruthy checks if a value is truthy
func isTruthy(v *ast.Value) bool {
	if ast.IsNil(v) {
		return false
	}
	if ast.IsSym(v) && (v.Str == "#f" || v.Str == "false") {
		return false
	}
	if ast.IsNothing(v) {
		return false
	}
	return true
}
