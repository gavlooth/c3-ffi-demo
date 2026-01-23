# REVIEWED:SYNTAX Items - Syntax Review Tasks

This document tracks all `REVIEWED:SYNTAX` markers in the codebase. These markers indicate code that uses OmniLisp syntax patterns that may need review, updates, or standardization.

---

## Summary

Found **~180+ REVIEWED:SYNTAX markers** across the codebase, primarily in:
- Test files (tests/*.omni, tests/*.lisp)
- Example files (examples/*.omni)
- Library files (lib/*.omni)
- Documentation (docs/, README.md)

---

## Category 1: Test Helper Functions

These are boilerplate test helper functions that appear in almost every test file. They use consistent patterns but are duplicated across many files.

**Issue:** Code duplication - each test file defines its own `test-eq`, `test-bool`, `test-string`, etc.

**Mitigation:** Create a shared test utilities library.

### Affected Files:

| File | Line | Function | Pattern |
|------|------|----------|---------|
| `tests/test_string_trim_right.lisp` | 16, 29 | `test-bool`, `test-string` | `(define (test-X name expected actual) ...)` |
| `tests/test_string_chars.omni` | 17 | `test-eq` | Same pattern |
| `tests/test_trigonometric.omni` | 14, 33 | `test-name`, `test-approx` | Same pattern |
| `tests/test_string_append.omni` | 14, 27 | `test-name`, `test-eq` | Same pattern |
| `tests/test_string_replace.omni` | 16 | `test-string` | Same pattern |
| `tests/test_pipe_operator.lisp` | 15-18 | `test-eq` | Multiple markers |
| `tests/test_let_seq.lisp` | 16, 29, 42 | `test-num`, `test-bool`, `test-string` | Same pattern |
| `tests/test_string_center.omni` | 14 | `test-eq` | Same pattern |
| `tests/test_string_last_index_of.omni` | 17 | `test-int` | Same pattern |
| `tests/test_string_repeat.omni` | 16 | `test-string` | Same pattern |
| `tests/test_string_substr.omni` | 16 | `test-string` | Same pattern |
| `tests/test_json_parse.omni` | 22, 35, 48, 61, 74, 87, 100 | Multiple test helpers | Same pattern |
| `tests/test_sort_with.lisp` | 17, 30 | `test-equal`, `test-list` | Same pattern |
| `tests/test_numeric_predicates.lisp` | 20, 33 | `test-num`, `test-bool` | Same pattern |
| `tests/test_take_drop.lisp` | 21, 34, 47 | Multiple helpers | Same pattern |
| `tests/test_reverse.omni` | 14 | `test-eq` | Same pattern |
| `tests/test_string_titlecase.omni` | 16 | `test-name` | Same pattern |
| `tests/test_sort_by.omni` | 14 | `test-eq` | Same pattern |
| `tests/test_string_upcase.omni` | 17 | `test-string` | Same pattern |
| `tests/test_string_lowcase.omni` | 17 | `test-string` | Same pattern |
| `tests/test_piping_extended.lisp` | 21, 34, 47 | Multiple helpers | Same pattern |
| `tests/test_conditions.lisp` | 13, 25, 36 | Multiple helpers | Same pattern |
| `tests/test_math_lisp.lisp` | 12-16 | `test-float` | Multiple markers |
| `tests/test_iterators_extended.lisp` | 22, 35, 48 | Multiple helpers | Same pattern |
| `tests/test_partition.lisp` | 15, 28, 47 | Multiple helpers | Same pattern |
| `tests/test_string_trim_left.lisp` | 16, 29 | `test-bool`, `test-string` | Same pattern |
| `tests/test_string_index_of.omni` | 16 | `test-int` | Same pattern |
| `tests/test_interpose.lisp` | 21 | `test-array` | Same pattern |
| `tests/test_sort.omni` | 14 | `test-eq` | Same pattern |
| `tests/test_string_pad_left.omni` | 15 | `test-eq` | Same pattern |
| `tests/test_string_length.omni` | 14-15 | `test-num` | Multiple markers |
| `tests/test_json_file_io.omni` | 17, 30, 43 | Multiple helpers | Same pattern |
| `tests/test_string_replace_first.omni` | 16 | `test-string` | Same pattern |
| `tests/test_string_trim.lisp` | 16 | `test-string` | Same pattern |
| `tests/test_json_valid_p.omni` | 16 | `test-bool` | Same pattern |
| `tests/test_zip_unzip.lisp` | 21, 34, 47, 60 | Multiple helpers | Same pattern |
| `tests/test_bitwise_shift.lisp` | 17 | `test-num` | Same pattern |
| `tests/test_flatten.omni` | 14 | `test-eq` | Same pattern |
| `tests/test_gcd_lcm.omni` | 15, 28, 41 | Multiple helpers | Same pattern |
| `tests/test_string_reverse.omni` | 14 | `test-eq` | Same pattern |
| `tests/test_collections_utils.omni` | 22, 35 | `test-num`, `test-bool` | Same pattern |
| `tests/test_clamp.omni` | 15, 28 | `test-eq`, `test-approx` | Same pattern |
| `tests/test_dot_field_chain.lisp` | 19, 32, 45 | Multiple helpers | Same pattern |
| `tests/test_json_get.omni` | 20, 33 | `test-name`, `test-nothing` | Same pattern |
| `tests/test_string_compare.omni` | 17 | `test-compare` | Same pattern |

**Proposed Action:**
```lisp
;; Create lib/test-utils.omni
(define (test-eq name expected actual)
  (set! test-count (+ test-count 1))
  (if (equal? expected actual)
      (do (set! pass-count (+ pass-count 1))
          (print "PASS:" name))
      (do (set! fail-count (+ fail-count 1))
          (print "FAIL:" name)
          (print "  Expected:" expected)
          (print "  Got:" actual))))

;; ... other test helpers

;; Then in test files:
(import test-utils)
```

---

## Category 2: Bracket Parameter Syntax Functions

Functions using the OmniLisp-specific bracket syntax for parameters.

**Issue:** Syntax may need standardization or documentation updates.

### Affected Locations:

| File | Line | Code Pattern | Notes |
|------|------|--------------|-------|
| `tests/test_pipe_operator.lisp` | 35-36 | `(define (inc [x] (+ x 1))` | Bracket param syntax |
| `tests/test_pipe_operator.lisp` | 52 | `(define (square [x] (* x x))` | Same |
| `tests/test_pipe_operator.lisp` | 70 | `(define (double [x] (* x 2))` | Same |
| `tests/test_pipe_operator.lisp` | 103 | `(define (identity [x] x)` | Same |
| `tests/test_piping_extended.lisp` | 64-75 | Multiple functions | `inc`, `square`, `double`, `add`, `sub`, `mul` |
| `tests/test_piping_extended.lisp` | 127 | `(define (const [x] x))` | Same |
| `tests/test_piping_extended.lisp` | 142 | `(define (add-partial [x y] (+ x y))` | Same |
| `tests/test_piping_extended.lisp` | 159-169 | `sub-flipped`, `div-like`, `div-flipped` | Same |
| `tests/test_piping_extended.lisp` | 312 | `(define (sum-three [x y z] ...)` | Same |
| `tests/test_piping_extended.lisp` | 391 | `(define (identity [x] x))` | Same |
| `tests/test_piping_extended.lisp` | 436 | `(define (make-adder [n] ...)` | Same |
| `tests/test_iterators_extended.lisp` | 168, 203 | `my-gen`, `seq-gen` | Generator functions |
| `tests/bench_numeric.omni` | 5, 15, 25, 36 | Benchmark functions | Same |
| `tests/bench_specialization.omni` | 10-37 | Multiple benchmark functions | Same |
| `tests/bench_runtime.omni` | 5 | `(define (loop-n n func) ...)` | Same |

**Proposed Action:**
- Document bracket parameter syntax in SYNTAX.md
- Ensure parser handles both `(x y)` and `[x] [y]` consistently
- Add syntax validation tests

---

## Category 3: Iterator/Lambda Patterns

Lambda and iterator usage patterns that may need syntax review.

### Affected Locations:

| File | Line | Pattern | Notes |
|------|------|---------|-------|
| `tests/test_iterators.omni` | 11 | `(map (lambda [x] (* x 2)) ...)` | Lambda in map |
| `tests/test_iterators.omni` | 17 | `(filter (lambda [x] (= (% x 2) 0)) ...)` | Lambda in filter |
| `tests/test_iterators.omni` | 23 | `(collect-array (map (lambda [x] ...) ...))` | Nested |
| `tests/test_iterators.omni` | 30 | Complex pipeline | Multiple nested lambdas |
| `tests/test_iterator_primitives.omni` | 18, 25 | Iterator tests | Cons/list patterns |

**Proposed Action:**
- Verify lambda bracket syntax is properly documented
- Ensure consistent handling across parser

---

## Category 4: Type Annotation Syntax

Julia-style type annotations using braces.

### Affected Locations:

| File | Line | Pattern | Notes |
|------|------|---------|-------|
| `tests/test_julia_flex.omni` | 17-20 | `(define (add-ints x {Int} y {Int}) {Int} ...)` | Type annotations |
| `tests/unwired_part1.omni` | 1-7 | `(define (describe-val x {Int}) ...)` | Multiple dispatch types |
| `tests/bench_specialization.omni` | 13 | `(define (time-it [name {String}] [f {->Any}] {Any}) ...)` | Mixed syntax |
| `tests/bench_specialization.omni` | 24-37 | `{Int}`, `{Float}` annotations | Benchmark typed functions |

**Proposed Action:**
- Document type annotation syntax `{TypeName}`
- Verify parser handles `[param {Type}]` correctly
- Add type annotation validation

---

## Category 5: Macro Definitions

Macro syntax patterns.

### Affected Locations:

| File | Line | Pattern | Notes |
|------|------|---------|-------|
| `tests/test_macros.omni` | 8 | `[(when test body ...) ...]` | When macro |
| `tests/test_macros.omni` | 72 | Let1 macro usage | Inside let1 |

**Proposed Action:**
- Document macro syntax in MACRO_ARCHITECTURE.md
- Verify ellipsis handling `...`

---

## Category 6: Dot Accessor Syntax

Field access using dot notation.

### Affected Locations:

| File | Line | Pattern | Notes |
|------|------|---------|-------|
| `tests/test_piping_extended.lisp` | 199-249 | `(.length arr)`, `(.car pair)`, etc. | Field accessors |
| `tests/test_piping_extended.lisp` | 267-272 | `(.name dict)`, `(.age dict)` | Dict field access |
| `tests/test_piping_extended.lisp` | 327 | `(.0 nums)`, `(.2 nums)` | Numeric index access |
| `tests/test_piping_extended.lisp` | 377 | `(.inner outer)` | Nested access |
| `tests/test_piping_extended.lisp` | 417-422 | `(.name person)`, etc. | Property access |
| `tests/test_piping_extended.lisp` | 477 | `(.c missing-dict)` | Missing key access |

**Proposed Action:**
- Document dot accessor syntax fully
- Verify nil/nothing handling for missing keys

---

## Category 7: Control Flow / Special Forms

### Affected Locations:

| File | Line | Pattern | Notes |
|------|------|---------|-------|
| `tests/test_nested_fiber.lisp` | 10 | Fiber/resume patterns | Concurrency |
| `tests/test_phase15_narrowing.omni` | 7, 18 | Region allocation tests | Escape analysis |
| `tests/test_match_clauses.lisp` | 42 | Legacy match syntax | Backward compat |
| `tests/test_conditions.lisp` | 54, 274 | handler-case, restart-case | Condition system |

**Proposed Action:**
- Ensure control flow documentation is complete
- Verify legacy syntax backward compatibility

---

## Category 8: Import/Module Syntax

### Affected Locations:

| File | Line | Pattern | Notes |
|------|------|---------|-------|
| `examples/scicomp_demo.omni` | 11-18 | `(import SciComp.BLAS)` | Module imports |

**Proposed Action:**
- Document module/import syntax
- Verify hierarchical module names work

---

## Category 9: Library Definitions

### Affected Locations:

| File | Line | Pattern | Notes |
|------|------|---------|-------|
| `lib/immer.omni` | 49 | `(define (vector . args) ...)` | Variadic function |
| `lib/immer.omni` | 58-60 | `(define (nth v idx) ...)` | FFI wrapper |
| `lib/immer.omni` | 112 | `(define (hash-map . args) ...)` | Variadic |
| `lib/immer.omni` | 158 | `(define (hash-set . args) ...)` | Variadic |

**Proposed Action:**
- Document variadic function syntax `. args`
- Document FFI patterns

---

## Category 10: Documentation Examples

### Affected Locations:

| File | Line | Pattern | Notes |
|------|------|---------|-------|
| `README.md` | 57 | `(define {effect ask} ...)` | Effect definition |
| `docs/BRANCH_LEVEL_REGION_NARROWING.md` | 190 | `(defn process [data] ...)` | Doc example |
| `examples/demo.omni` | 8-46 | Multiple patterns | Demo file |

**Proposed Action:**
- Ensure README examples match current syntax
- Update docs if syntax has changed

---

## Priority Actions

### P0 - High Priority (Affects usability)
1. **Create shared test utilities library** - Eliminate duplication
2. **Document bracket parameter syntax** - `[param]` vs `(param)`
3. **Document type annotation syntax** - `{Type}`

### P1 - Medium Priority (Documentation)
4. **Update SYNTAX.md** with all syntactic forms
5. **Verify dot accessor documentation**
6. **Document variadic functions**

### P2 - Low Priority (Cleanup)
7. **Remove redundant markers** where syntax is now stable
8. **Standardize test file structure**
9. **Update example files to use modern syntax**

---

## Recommended Test Utilities Library

Create `lib/test-utils.omni`:

```lisp
;; Test framework utilities
;; Import this in test files instead of duplicating

(define test-count 0)
(define pass-count 0)
(define fail-count 0)

(define (test-eq name expected actual)
  (set! test-count (+ test-count 1))
  (if (equal? expected actual)
      (do (set! pass-count (+ pass-count 1))
          (print "PASS:" name))
      (do (set! fail-count (+ fail-count 1))
          (print "FAIL:" name)
          (print "  Expected:" expected)
          (print "  Got:" actual))))

(define (test-bool name expected actual)
  (test-eq name expected actual))

(define (test-num name expected actual)
  (test-eq name expected actual))

(define (test-string name expected actual)
  (test-eq name expected actual))

(define (test-approx name expected actual [tolerance 1e-10])
  (set! test-count (+ test-count 1))
  (if (< (abs (- expected actual)) tolerance)
      (do (set! pass-count (+ pass-count 1))
          (print "PASS:" name))
      (do (set! fail-count (+ fail-count 1))
          (print "FAIL:" name)
          (print "  Expected:" expected)
          (print "  Got:" actual)
          (print "  Diff:" (abs (- expected actual))))))

(define (test-nothing name value)
  (set! test-count (+ test-count 1))
  (if (nothing? value)
      (do (set! pass-count (+ pass-count 1))
          (print "PASS:" name))
      (do (set! fail-count (+ fail-count 1))
          (print "FAIL:" name "(expected nothing)"))))

(define (test-error name value)
  (set! test-count (+ test-count 1))
  (if (error? value)
      (do (set! pass-count (+ pass-count 1))
          (print "PASS:" name))
      (do (set! fail-count (+ fail-count 1))
          (print "FAIL:" name "(expected error)"))))

(define (print-summary)
  (print "")
  (print "==========================================")
  (print "Total:" test-count "| Pass:" pass-count "| Fail:" fail-count)
  (print "=========================================="))
```

---

## Files to Update After Creating Test Utils

After creating the test utilities library, update these files to import it:
- All files in `tests/` directory (~60 files)
- Remove duplicated test helper definitions
- Add `(import test-utils)` at top

This will reduce total codebase size by ~2000-3000 lines.
