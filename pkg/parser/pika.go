package parser

import (
	"fmt"
	"purple_go/pkg/ast"
	"strconv"
	"strings"
	"unicode"
)

// PikaResult represents a parse result
type PikaResult struct {
	Success bool       // Whether parsing succeeded
	Value   *ast.Value // The parsed AST (as first-class data)
	Pos     int        // Position after match
	Err     string     // Error message if failed
}

// Failed creates a failed result
func Failed(pos int, msg string) PikaResult {
	return PikaResult{Success: false, Pos: pos, Err: msg}
}

// Succeeded creates a successful result
func Succeeded(value *ast.Value, pos int) PikaResult {
	return PikaResult{Success: true, Value: value, Pos: pos}
}

// MemoKey for memoization
type MemoKey struct {
	Rule string
	Pos  int
}

// PikaParser implements a Pika-style parser with OmniLisp bracket semantics
// Key properties:
// - Right-to-left parsing (handles left recursion)
// - O(n) with memoization
// - AST nodes are first-class Lisp data
// - Supports () [] {} #{} brackets
type PikaParser struct {
	Input   []rune
	Memo    map[MemoKey]PikaResult
	RuleMap map[string]func(int) PikaResult
}

// NewPikaParser creates a new Pika parser
func NewPikaParser(input string) *PikaParser {
	p := &PikaParser{
		Input: []rune(input),
		Memo:  make(map[MemoKey]PikaResult),
	}
	p.initRules()
	return p
}

// initRules initializes the grammar rules
func (p *PikaParser) initRules() {
	p.RuleMap = map[string]func(int) PikaResult{
		"expr":       p.parseExpr,
		"atom":       p.parseAtom,
		"list":       p.parseList,
		"array":      p.parseArray,
		"typelit":    p.parseTypeLit,
		"dict":       p.parseDict,
		"quote":      p.parseQuote,
		"quasiquote": p.parseQuasiquote,
		"unquote":    p.parseUnquote,
		"number":     p.parseNumber,
		"symbol":     p.parseSymbol,
		"keyword":    p.parseKeyword,
		"string":     p.parseString,
	}
}

// Parse parses the input from position 0
func (p *PikaParser) Parse() (*ast.Value, error) {
	p.skipWhitespace(0)
	result := p.parseProgram(0)
	if !result.Success {
		return nil, fmt.Errorf("parse error at position %d: %s", result.Pos, result.Err)
	}
	return result.Value, nil
}

// parseProgram parses multiple expressions
func (p *PikaParser) parseProgram(pos int) PikaResult {
	var exprs []*ast.Value
	pos = p.skipWhitespace(pos)

	for pos < len(p.Input) {
		result := p.memoized("expr", pos)
		if !result.Success {
			if len(exprs) == 0 {
				return result
			}
			break
		}
		exprs = append(exprs, result.Value)
		pos = p.skipWhitespace(result.Pos)
	}

	if len(exprs) == 0 {
		return Failed(pos, "expected expression")
	}
	if len(exprs) == 1 {
		return Succeeded(exprs[0], pos)
	}

	// Multiple expressions become (begin ...)
	result := ast.NewSym("begin")
	for _, e := range exprs {
		result = ast.NewCell(result, ast.NewCell(e, ast.Nil))
	}
	return Succeeded(result, pos)
}

// memoized applies memoization to a rule
func (p *PikaParser) memoized(rule string, pos int) PikaResult {
	key := MemoKey{Rule: rule, Pos: pos}
	if result, ok := p.Memo[key]; ok {
		return result
	}

	fn, ok := p.RuleMap[rule]
	if !ok {
		return Failed(pos, fmt.Sprintf("unknown rule: %s", rule))
	}

	result := fn(pos)
	p.Memo[key] = result
	return result
}

// parseExpr parses an expression (main entry point per expression)
func (p *PikaParser) parseExpr(pos int) PikaResult {
	pos = p.skipWhitespace(pos)

	if pos >= len(p.Input) {
		return Failed(pos, "unexpected end of input")
	}

	ch := p.Input[pos]

	switch ch {
	case '(':
		return p.memoized("list", pos)
	case '[':
		return p.memoized("array", pos)
	case '{':
		return p.memoized("typelit", pos)
	case '#':
		return p.parseSpecial(pos)
	case '\'':
		return p.memoized("quote", pos)
	case '`':
		return p.memoized("quasiquote", pos)
	case ',':
		return p.memoized("unquote", pos)
	case '"':
		return p.memoized("string", pos)
	case ':':
		return p.memoized("keyword", pos)
	case '.':
		// Check if it's a functional accessor .field
		if pos+1 < len(p.Input) && p.isSymbolStart(p.Input[pos+1]) {
			return p.parseFunctionalAccessor(pos)
		}
		return p.memoized("atom", pos)
	default:
		return p.memoized("atom", pos)
	}
}

// parseAtom parses an atom (number or symbol)
func (p *PikaParser) parseAtom(pos int) PikaResult {
	pos = p.skipWhitespace(pos)

	if pos >= len(p.Input) {
		return Failed(pos, "unexpected end of input")
	}

	// Try number first
	if p.isDigitStart(pos) {
		return p.memoized("number", pos)
	}

	// Otherwise symbol (which may have dot access)
	return p.parseSymbolWithDotAccess(pos)
}

// parseNumber parses an integer or float
func (p *PikaParser) parseNumber(pos int) PikaResult {
	pos = p.skipWhitespace(pos)
	start := pos

	// Optional sign
	if pos < len(p.Input) && (p.Input[pos] == '-' || p.Input[pos] == '+') {
		pos++
	}

	if pos >= len(p.Input) || !unicode.IsDigit(p.Input[pos]) {
		return Failed(start, "expected number")
	}

	// Check for hex/binary
	if pos+1 < len(p.Input) && p.Input[pos] == '0' {
		if p.Input[pos+1] == 'x' || p.Input[pos+1] == 'X' {
			return p.parseHexNumber(start)
		}
		if p.Input[pos+1] == 'b' || p.Input[pos+1] == 'B' {
			return p.parseBinaryNumber(start)
		}
	}

	// Integer part (skip underscores)
	for pos < len(p.Input) && (unicode.IsDigit(p.Input[pos]) || p.Input[pos] == '_') {
		pos++
	}

	// Check for float
	isFloat := false
	if pos < len(p.Input) && p.Input[pos] == '.' {
		// Make sure it's not a dot accessor
		if pos+1 < len(p.Input) && unicode.IsDigit(p.Input[pos+1]) {
			isFloat = true
			pos++
			for pos < len(p.Input) && (unicode.IsDigit(p.Input[pos]) || p.Input[pos] == '_') {
				pos++
			}
		}
	}

	// Exponent
	if pos < len(p.Input) && (p.Input[pos] == 'e' || p.Input[pos] == 'E') {
		isFloat = true
		pos++
		if pos < len(p.Input) && (p.Input[pos] == '+' || p.Input[pos] == '-') {
			pos++
		}
		for pos < len(p.Input) && unicode.IsDigit(p.Input[pos]) {
			pos++
		}
	}

	// Remove underscores for parsing
	numStr := strings.ReplaceAll(string(p.Input[start:pos]), "_", "")

	if isFloat {
		val, err := strconv.ParseFloat(numStr, 64)
		if err != nil {
			return Failed(start, "invalid float")
		}
		return Succeeded(ast.NewFloat(val), pos)
	}

	val, err := strconv.ParseInt(numStr, 10, 64)
	if err != nil {
		return Failed(start, "invalid integer")
	}
	return Succeeded(ast.NewInt(val), pos)
}

// parseHexNumber parses a hexadecimal number 0xDEADBEEF
func (p *PikaParser) parseHexNumber(start int) PikaResult {
	pos := start
	if p.Input[pos] == '-' || p.Input[pos] == '+' {
		pos++
	}
	pos += 2 // Skip 0x

	hexStart := pos
	for pos < len(p.Input) {
		ch := p.Input[pos]
		if unicode.IsDigit(ch) || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F') || ch == '_' {
			pos++
		} else {
			break
		}
	}

	hexStr := strings.ReplaceAll(string(p.Input[hexStart:pos]), "_", "")
	val, err := strconv.ParseInt(hexStr, 16, 64)
	if err != nil {
		return Failed(start, "invalid hex number")
	}

	if start < len(p.Input) && p.Input[start] == '-' {
		val = -val
	}
	return Succeeded(ast.NewInt(val), pos)
}

// parseBinaryNumber parses a binary number 0b101010
func (p *PikaParser) parseBinaryNumber(start int) PikaResult {
	pos := start
	if p.Input[pos] == '-' || p.Input[pos] == '+' {
		pos++
	}
	pos += 2 // Skip 0b

	binStart := pos
	for pos < len(p.Input) && (p.Input[pos] == '0' || p.Input[pos] == '1' || p.Input[pos] == '_') {
		pos++
	}

	binStr := strings.ReplaceAll(string(p.Input[binStart:pos]), "_", "")
	val, err := strconv.ParseInt(binStr, 2, 64)
	if err != nil {
		return Failed(start, "invalid binary number")
	}

	if start < len(p.Input) && p.Input[start] == '-' {
		val = -val
	}
	return Succeeded(ast.NewInt(val), pos)
}

// parseSymbol parses a symbol
func (p *PikaParser) parseSymbol(pos int) PikaResult {
	pos = p.skipWhitespace(pos)
	start := pos

	if pos >= len(p.Input) {
		return Failed(pos, "expected symbol")
	}

	ch := p.Input[pos]
	if !p.isSymbolStart(ch) {
		return Failed(pos, "expected symbol")
	}

	pos++
	for pos < len(p.Input) && p.isSymbolChar(p.Input[pos]) {
		pos++
	}

	name := string(p.Input[start:pos])

	// Special symbols
	switch name {
	case "nil":
		return Succeeded(ast.Nil, pos)
	case "nothing":
		return Succeeded(ast.Nothing, pos)
	case "#t", "true":
		return Succeeded(ast.NewSym("#t"), pos)
	case "#f", "false":
		return Succeeded(ast.NewSym("#f"), pos)
	}

	return Succeeded(ast.NewSym(name), pos)
}

// parseSymbolWithDotAccess parses a symbol with optional dot access
// e.g., foo, foo.bar, foo.bar.baz, foo.(0), foo.[1 2 3]
func (p *PikaParser) parseSymbolWithDotAccess(pos int) PikaResult {
	result := p.memoized("symbol", pos)
	if !result.Success {
		return result
	}

	current := result.Value
	pos = result.Pos

	// Check for dot access chains
	for pos < len(p.Input) && p.Input[pos] == '.' {
		pos++ // Skip '.'

		if pos >= len(p.Input) {
			break
		}

		ch := p.Input[pos]

		if ch == '(' {
			// Dynamic access: obj.(expr)
			pos++ // Skip '('
			exprResult := p.memoized("expr", pos)
			if !exprResult.Success {
				return exprResult
			}
			pos = p.skipWhitespace(exprResult.Pos)
			if pos >= len(p.Input) || p.Input[pos] != ')' {
				return Failed(pos, "expected ')' in dynamic access")
			}
			pos++ // Skip ')'

			// (get current expr)
			current = ast.NewCell(ast.NewSym("get"),
				ast.NewCell(current,
					ast.NewCell(exprResult.Value, ast.Nil)))

		} else if ch == '[' {
			// Multi-access: obj.[indices]
			arrayResult := p.memoized("array", pos)
			if !arrayResult.Success {
				return arrayResult
			}
			pos = arrayResult.Pos

			// (multi-get current indices)
			current = ast.NewCell(ast.NewSym("multi-get"),
				ast.NewCell(current,
					ast.NewCell(arrayResult.Value, ast.Nil)))

		} else if p.isSymbolStart(ch) {
			// Field access: obj.field
			symResult := p.memoized("symbol", pos)
			if !symResult.Success {
				return symResult
			}
			pos = symResult.Pos

			// (get current 'field)
			quotedField := ast.NewCell(ast.NewSym("quote"),
				ast.NewCell(symResult.Value, ast.Nil))
			current = ast.NewCell(ast.NewSym("get"),
				ast.NewCell(current,
					ast.NewCell(quotedField, ast.Nil)))
		} else {
			// Not a valid dot access, backtrack
			pos--
			break
		}
	}

	return Succeeded(current, pos)
}

// parseFunctionalAccessor parses .field -> (lambda (it) (get it 'field))
func (p *PikaParser) parseFunctionalAccessor(pos int) PikaResult {
	pos++ // Skip initial '.'

	// Parse the accessor chain
	var accessors []string
	for pos < len(p.Input) {
		if !p.isSymbolStart(p.Input[pos]) {
			break
		}

		start := pos
		for pos < len(p.Input) && p.isSymbolChar(p.Input[pos]) {
			pos++
		}
		accessors = append(accessors, string(p.Input[start:pos]))

		// Check for more dots
		if pos < len(p.Input) && p.Input[pos] == '.' {
			pos++
		} else {
			break
		}
	}

	if len(accessors) == 0 {
		return Failed(pos, "expected field name after '.'")
	}

	// Build: (lambda (it) (get (get it 'field1) 'field2) ...)
	itSym := ast.NewSym("it")
	body := itSym
	for _, field := range accessors {
		quotedField := ast.NewCell(ast.NewSym("quote"),
			ast.NewCell(ast.NewSym(field), ast.Nil))
		body = ast.NewCell(ast.NewSym("get"),
			ast.NewCell(body,
				ast.NewCell(quotedField, ast.Nil)))
	}

	lambda := ast.NewCell(ast.NewSym("lambda"),
		ast.NewCell(ast.NewCell(itSym, ast.Nil),
			ast.NewCell(body, ast.Nil)))

	return Succeeded(lambda, pos)
}

// parseKeyword parses a keyword :symbol
func (p *PikaParser) parseKeyword(pos int) PikaResult {
	if pos >= len(p.Input) || p.Input[pos] != ':' {
		return Failed(pos, "expected keyword")
	}
	pos++ // Skip ':'

	start := pos
	for pos < len(p.Input) && p.isSymbolChar(p.Input[pos]) {
		pos++
	}

	if pos == start {
		return Failed(pos, "expected keyword name after ':'")
	}

	name := string(p.Input[start:pos])
	return Succeeded(ast.NewKeyword(name), pos)
}

// parseString parses a string literal with interpolation
func (p *PikaParser) parseString(pos int) PikaResult {
	if pos >= len(p.Input) || p.Input[pos] != '"' {
		return Failed(pos, "expected string")
	}
	pos++

	var parts []*ast.Value
	var currentChars []rune

	flushChars := func() {
		if len(currentChars) > 0 {
			// Build string as list of chars
			result := ast.Nil
			for i := len(currentChars) - 1; i >= 0; i-- {
				result = ast.NewCell(ast.NewChar(currentChars[i]), result)
			}
			result = ast.NewCell(ast.NewSym("string"), result)
			parts = append(parts, result)
			currentChars = nil
		}
	}

	for pos < len(p.Input) && p.Input[pos] != '"' {
		ch := p.Input[pos]

		if ch == '\\' && pos+1 < len(p.Input) {
			// Escape sequence
			pos++
			switch p.Input[pos] {
			case 'n':
				currentChars = append(currentChars, '\n')
			case 't':
				currentChars = append(currentChars, '\t')
			case 'r':
				currentChars = append(currentChars, '\r')
			case '\\':
				currentChars = append(currentChars, '\\')
			case '"':
				currentChars = append(currentChars, '"')
			case '$':
				currentChars = append(currentChars, '$')
			default:
				currentChars = append(currentChars, p.Input[pos])
			}
			pos++
		} else if ch == '$' {
			// String interpolation
			if pos+1 < len(p.Input) {
				nextCh := p.Input[pos+1]
				if nextCh == '(' {
					// $(expr)
					flushChars()
					pos += 2 // Skip '$('
					exprResult := p.memoized("expr", pos)
					if !exprResult.Success {
						return exprResult
					}
					pos = p.skipWhitespace(exprResult.Pos)
					if pos >= len(p.Input) || p.Input[pos] != ')' {
						return Failed(pos, "expected ')' in string interpolation")
					}
					pos++ // Skip ')'
					parts = append(parts, exprResult.Value)
				} else if p.isSymbolStart(nextCh) {
					// $name
					flushChars()
					pos++ // Skip '$'
					start := pos
					for pos < len(p.Input) && p.isSymbolChar(p.Input[pos]) {
						pos++
					}
					name := string(p.Input[start:pos])
					parts = append(parts, ast.NewSym(name))
				} else {
					currentChars = append(currentChars, ch)
					pos++
				}
			} else {
				currentChars = append(currentChars, ch)
				pos++
			}
		} else {
			currentChars = append(currentChars, ch)
			pos++
		}
	}

	if pos >= len(p.Input) {
		return Failed(pos, "unterminated string")
	}
	pos++ // Skip closing quote

	flushChars()

	// If no interpolation, return simple string
	if len(parts) == 1 {
		return Succeeded(parts[0], pos)
	}

	// Build (string-concat part1 part2 ...)
	if len(parts) == 0 {
		// Empty string
		return Succeeded(ast.NewCell(ast.NewSym("string"), ast.Nil), pos)
	}

	result := ast.Nil
	for i := len(parts) - 1; i >= 0; i-- {
		result = ast.NewCell(parts[i], result)
	}
	result = ast.NewCell(ast.NewSym("string-concat"), result)
	return Succeeded(result, pos)
}

// parseList parses a list (s-expression) with ()
func (p *PikaParser) parseList(pos int) PikaResult {
	if pos >= len(p.Input) || p.Input[pos] != '(' {
		return Failed(pos, "expected '('")
	}
	pos++
	pos = p.skipWhitespace(pos)

	var elements []*ast.Value

	for pos < len(p.Input) && p.Input[pos] != ')' {
		result := p.memoized("expr", pos)
		if !result.Success {
			return result
		}
		elements = append(elements, result.Value)
		pos = p.skipWhitespace(result.Pos)

		// Check for dotted pair
		if pos+1 < len(p.Input) && p.Input[pos] == '.' &&
			(p.Input[pos+1] == ' ' || p.Input[pos+1] == '\t' || p.Input[pos+1] == '\n') {
			pos++
			pos = p.skipWhitespace(pos)
			cdrResult := p.memoized("expr", pos)
			if !cdrResult.Success {
				return cdrResult
			}
			pos = p.skipWhitespace(cdrResult.Pos)

			if pos >= len(p.Input) || p.Input[pos] != ')' {
				return Failed(pos, "expected ')' after dotted pair")
			}
			pos++

			// Build improper list
			result := cdrResult.Value
			for i := len(elements) - 1; i >= 0; i-- {
				result = ast.NewCell(elements[i], result)
			}
			return Succeeded(result, pos)
		}
	}

	if pos >= len(p.Input) {
		return Failed(pos, "expected ')'")
	}
	pos++ // Skip ')'

	// Build proper list
	result := ast.Nil
	for i := len(elements) - 1; i >= 0; i-- {
		result = ast.NewCell(elements[i], result)
	}

	return Succeeded(result, pos)
}

// parseArray parses an array literal with []
func (p *PikaParser) parseArray(pos int) PikaResult {
	if pos >= len(p.Input) || p.Input[pos] != '[' {
		return Failed(pos, "expected '['")
	}
	pos++
	pos = p.skipWhitespace(pos)

	var elements []*ast.Value

	for pos < len(p.Input) && p.Input[pos] != ']' {
		result := p.memoized("expr", pos)
		if !result.Success {
			return result
		}
		elements = append(elements, result.Value)
		pos = p.skipWhitespace(result.Pos)
	}

	if pos >= len(p.Input) {
		return Failed(pos, "expected ']'")
	}
	pos++ // Skip ']'

	return Succeeded(ast.NewArray(elements), pos)
}

// parseTypeLit parses a type literal with {}
func (p *PikaParser) parseTypeLit(pos int) PikaResult {
	if pos >= len(p.Input) || p.Input[pos] != '{' {
		return Failed(pos, "expected '{'")
	}
	pos++
	pos = p.skipWhitespace(pos)

	// Get type name (first element must be a symbol)
	if pos >= len(p.Input) || !p.isSymbolStart(p.Input[pos]) {
		return Failed(pos, "expected type name in {}")
	}

	nameResult := p.memoized("symbol", pos)
	if !nameResult.Success {
		return nameResult
	}
	typeName := nameResult.Value.Str
	pos = p.skipWhitespace(nameResult.Pos)

	// Get type parameters
	var params []*ast.Value
	for pos < len(p.Input) && p.Input[pos] != '}' {
		result := p.memoized("expr", pos)
		if !result.Success {
			return result
		}
		params = append(params, result.Value)
		pos = p.skipWhitespace(result.Pos)
	}

	if pos >= len(p.Input) {
		return Failed(pos, "expected '}'")
	}
	pos++ // Skip '}'

	return Succeeded(ast.NewTypeLit(typeName, params), pos)
}

// parseDict parses a dictionary literal with #{}
func (p *PikaParser) parseDict(pos int) PikaResult {
	// Already verified we're at #{
	pos += 2 // Skip '#{'
	pos = p.skipWhitespace(pos)

	var keys, values []*ast.Value

	for pos < len(p.Input) && p.Input[pos] != '}' {
		// Parse key
		keyResult := p.memoized("expr", pos)
		if !keyResult.Success {
			return keyResult
		}
		keys = append(keys, keyResult.Value)
		pos = p.skipWhitespace(keyResult.Pos)

		// Parse value
		if pos >= len(p.Input) || p.Input[pos] == '}' {
			return Failed(pos, "expected value after key in dict")
		}
		valResult := p.memoized("expr", pos)
		if !valResult.Success {
			return valResult
		}
		values = append(values, valResult.Value)
		pos = p.skipWhitespace(valResult.Pos)
	}

	if pos >= len(p.Input) {
		return Failed(pos, "expected '}'")
	}
	pos++ // Skip '}'

	return Succeeded(ast.NewDict(keys, values), pos)
}

// parseQuote parses a quoted expression
func (p *PikaParser) parseQuote(pos int) PikaResult {
	if pos >= len(p.Input) || p.Input[pos] != '\'' {
		return Failed(pos, "expected quote")
	}
	pos++

	result := p.memoized("expr", pos)
	if !result.Success {
		return result
	}

	// (quote <expr>)
	quoted := ast.NewCell(ast.NewSym("quote"), ast.NewCell(result.Value, ast.Nil))
	return Succeeded(quoted, result.Pos)
}

// parseQuasiquote parses a quasiquoted expression
func (p *PikaParser) parseQuasiquote(pos int) PikaResult {
	if pos >= len(p.Input) || p.Input[pos] != '`' {
		return Failed(pos, "expected quasiquote")
	}
	pos++

	result := p.memoized("expr", pos)
	if !result.Success {
		return result
	}

	// (quasiquote <expr>)
	quoted := ast.NewCell(ast.NewSym("quasiquote"), ast.NewCell(result.Value, ast.Nil))
	return Succeeded(quoted, result.Pos)
}

// parseUnquote parses an unquote or unquote-splicing
func (p *PikaParser) parseUnquote(pos int) PikaResult {
	if pos >= len(p.Input) || p.Input[pos] != ',' {
		return Failed(pos, "expected unquote")
	}
	pos++

	splice := false
	if pos < len(p.Input) && p.Input[pos] == '@' {
		splice = true
		pos++
	}

	result := p.memoized("expr", pos)
	if !result.Success {
		return result
	}

	sym := "unquote"
	if splice {
		sym = "unquote-splicing"
	}

	// (unquote <expr>) or (unquote-splicing <expr>)
	quoted := ast.NewCell(ast.NewSym(sym), ast.NewCell(result.Value, ast.Nil))
	return Succeeded(quoted, result.Pos)
}

// parseSpecial parses special syntax like #t, #f, #\char, #{dict}
func (p *PikaParser) parseSpecial(pos int) PikaResult {
	if pos >= len(p.Input) || p.Input[pos] != '#' {
		return Failed(pos, "expected #")
	}
	pos++

	if pos >= len(p.Input) {
		return Failed(pos, "unexpected end after #")
	}

	switch p.Input[pos] {
	case 't':
		pos++
		return Succeeded(ast.NewSym("#t"), pos)
	case 'f':
		pos++
		return Succeeded(ast.NewSym("#f"), pos)
	case '{':
		// Dictionary literal #{}
		return p.parseDict(pos - 1) // Back up to include #
	case '\\':
		// Character literal
		pos++
		if pos >= len(p.Input) {
			return Failed(pos, "expected character after #\\")
		}
		ch := p.Input[pos]
		pos++

		// Check for named characters
		if p.isSymbolChar(ch) {
			start := pos - 1
			for pos < len(p.Input) && p.isSymbolChar(p.Input[pos]) {
				pos++
			}
			name := string(p.Input[start:pos])
			switch name {
			case "space":
				ch = ' '
			case "newline":
				ch = '\n'
			case "tab":
				ch = '\t'
			case "return":
				ch = '\r'
			default:
				if len(name) == 1 {
					ch = rune(name[0])
				}
			}
		}

		return Succeeded(ast.NewChar(ch), pos)

	case '(':
		// Vector literal (as list with vec tag)
		listResult := p.memoized("list", pos)
		if !listResult.Success {
			return listResult
		}
		// (vec ...)
		vec := ast.NewCell(ast.NewSym("vec"), listResult.Value)
		return Succeeded(vec, listResult.Pos)

	case '\'':
		// Syntax quote #'
		pos++
		result := p.memoized("expr", pos)
		if !result.Success {
			return result
		}
		// (syntax <expr>)
		syntax := ast.NewCell(ast.NewSym("syntax"), ast.NewCell(result.Value, ast.Nil))
		return Succeeded(syntax, result.Pos)

	default:
		return Failed(pos, fmt.Sprintf("unknown special syntax #%c", p.Input[pos]))
	}
}

// Helper methods

func (p *PikaParser) skipWhitespace(pos int) int {
	for pos < len(p.Input) {
		ch := p.Input[pos]
		if ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' {
			pos++
		} else if ch == ';' {
			// Skip comment
			for pos < len(p.Input) && p.Input[pos] != '\n' {
				pos++
			}
		} else {
			break
		}
	}
	return pos
}

func (p *PikaParser) isDigitStart(pos int) bool {
	if pos >= len(p.Input) {
		return false
	}
	ch := p.Input[pos]
	if unicode.IsDigit(ch) {
		return true
	}
	if (ch == '-' || ch == '+') && pos+1 < len(p.Input) && unicode.IsDigit(p.Input[pos+1]) {
		return true
	}
	return false
}

func (p *PikaParser) isSymbolStart(ch rune) bool {
	if unicode.IsLetter(ch) {
		return true
	}
	// Extended symbol characters (but not : which is keyword prefix)
	switch ch {
	case '!', '$', '%', '&', '*', '+', '-', '/', '<', '=', '>', '?', '@', '^', '_', '~':
		return true
	}
	return false
}

func (p *PikaParser) isSymbolChar(ch rune) bool {
	return p.isSymbolStart(ch) || unicode.IsDigit(ch) || ch == '!' || ch == '?'
}

// Left Recursion Support
// Pika handles left recursion by parsing right-to-left

// PikaLeftRecursive extends PikaParser with left recursion support
type PikaLeftRecursive struct {
	*PikaParser
	LeftRecursive map[string]bool
	Growing       map[MemoKey]bool
}

// NewPikaLeftRecursive creates a parser with left recursion support
func NewPikaLeftRecursive(input string) *PikaLeftRecursive {
	return &PikaLeftRecursive{
		PikaParser:    NewPikaParser(input),
		LeftRecursive: make(map[string]bool),
		Growing:       make(map[MemoKey]bool),
	}
}

// RegisterLeftRecursive marks a rule as left-recursive
func (p *PikaLeftRecursive) RegisterLeftRecursive(rule string) {
	p.LeftRecursive[rule] = true
}

// memoizedLR applies memoization with left-recursion handling
func (p *PikaLeftRecursive) memoizedLR(rule string, pos int) PikaResult {
	key := MemoKey{Rule: rule, Pos: pos}

	// Check memo
	if result, ok := p.Memo[key]; ok {
		return result
	}

	// For left-recursive rules, use growing approach
	if p.LeftRecursive[rule] {
		return p.growLR(rule, pos, key)
	}

	// Non-left-recursive: normal memoization
	fn, ok := p.RuleMap[rule]
	if !ok {
		return Failed(pos, fmt.Sprintf("unknown rule: %s", rule))
	}

	result := fn(pos)
	p.Memo[key] = result
	return result
}

// growLR implements the growing approach for left recursion
func (p *PikaLeftRecursive) growLR(rule string, pos int, key MemoKey) PikaResult {
	if p.Growing[key] {
		// In the middle of growing, return failure to break recursion
		return Failed(pos, "left recursion base case")
	}

	// Seed with failure
	p.Memo[key] = Failed(pos, "left recursion seed")
	p.Growing[key] = true

	fn := p.RuleMap[rule]
	for {
		result := fn(pos)
		prev := p.Memo[key]

		// Stop growing if we didn't make progress
		if !result.Success || result.Pos <= prev.Pos {
			break
		}

		// Update memo with better result
		p.Memo[key] = result
	}

	p.Growing[key] = false
	return p.Memo[key]
}

// AST as First-Class Data
// The AST nodes produced by Pika ARE Lisp values that can be:
// - Quoted and manipulated
// - Pattern matched
// - Transformed by macros
// - Evaluated at multiple meta-levels (tower of interpreters)

// ASTNode represents an AST node as a Lisp value
// This is the homoiconic representation
type ASTNode = *ast.Value

// CreateASTNode creates an AST node from components
// (node-type children...)
func CreateASTNode(nodeType string, children ...*ast.Value) *ast.Value {
	// Build children list from the end
	result := ast.Nil
	for i := len(children) - 1; i >= 0; i-- {
		result = ast.NewCell(children[i], result)
	}
	// Prepend the node type
	return ast.NewCell(ast.NewSym(nodeType), result)
}

// GetNodeType extracts the type from an AST node
func GetNodeType(node *ast.Value) string {
	if ast.IsCell(node) && ast.IsSym(node.Car) {
		return node.Car.Str
	}
	return ""
}

// GetNodeChildren extracts children from an AST node
func GetNodeChildren(node *ast.Value) []*ast.Value {
	if !ast.IsCell(node) {
		return nil
	}

	var children []*ast.Value
	for n := node.Cdr; !ast.IsNil(n) && ast.IsCell(n); n = n.Cdr {
		children = append(children, n.Car)
	}
	return children
}
