# Test Coverage Added - Iterator Primitives

## Date: 2025-01-12

## Summary
Added comprehensive test coverage for fundamental iterator primitives in OmniLisp that were previously untested.

## Target Component
- **File**: `/home/heefoo/code/OmniLisp/runtime/src/iterator.c`
- **Functions**: `prim_first`, `prim_rest`, `prim_iterate`, `prim_iter_next`, `prim_take`

## Test File Created
- **File**: `/home/heefoo/code/OmniLisp/tests/test_iterator_primitives.omni`
- **Type**: OmniLisp integration tests

## Functions Now Marked as TESTED

1. **`prim_first`** (line 29)
   - Tests: non-empty lists, single element lists, empty lists, pairs created via cons

2. **`prim_rest`** (line 51)
   - Tests: multi-element lists, single element lists, empty lists, pairs

3. **`prim_iterate`** (line 101)
   - Tests: increment iterator, multiplication iterator (stateful lazy sequences)

4. **`prim_iter_next`** (line 121)
   - Tests: advancing iterator and getting next value (via iterate)

5. **`prim_take`** (line 169)
   - Tests: taking n elements from lists, taking more than available, taking 0 elements, taking negative count, taking from iterators

## Test Coverage Details

### First Primitive Tests
- ✅ Non-empty list: `(first '(1 2 3 4 5))` → 1
- ✅ Single element list: `(first '(42))` → 42
- ✅ Empty list: `(first '())` → nil
- ✅ Pair: `(first (cons 100 '(200 300)))` → 100

### Rest Primitive Tests
- ✅ Multi-element list: `(rest '(1 2 3 4 5))` → '(2 3 4 5)
- ✅ Single element list: `(rest '(99))` → nil
- ✅ Empty list: `(rest '())` → nil
- ✅ Pair: `(rest (cons 10 '(20 30)))` → '(20 30)

### Take Primitive Tests
- ✅ Normal take: `(take 3 '(1 2 3 4 5 6 7 8 9 10))` → '(1 2 3)
- ✅ Take more than available: `(take 10 '(1 2 3))` → '(1 2 3)
- ✅ Take zero elements: `(take 0 '(1 2 3))` → nil
- ✅ Take negative count: `(take -5 '(1 2 3))` → nil
- ✅ Take from iterator: `(take 5 (iterate inc 0))` → '(0 1 2 3 4)

### Iterate Primitive Tests
- ✅ Increment sequence: `(iterate inc 0)` produces 0, 1, 2, 3, ...
- ✅ Multiplication sequence: `(iterate double 1)` produces 1, 2, 4, 8, ...
- ✅ Stateful iteration: Each call to `next` advances and returns current

### Integration Tests
- ✅ Iterate + Take: `take 5 (iterate inc 0)` → '(0 1 2 3 4)
- ✅ Nested structures: `(first (first '((1 2) (3 4))))` → 1

## Assertions Used
The test uses the built-in testing framework assertions:
- `assert-eq` - For equality comparisons
- `assert-nil` - For checking nil/nothing values

## Why This Coverage Was Needed
The existing `test_iterators.omni` tested higher-level operations (`range`, `next`, `map`, `filter`, `collect-list`, `collect-array`) but did NOT test:
- Basic sequence primitives (`first`, `rest`)
- Lazy iterator creation (`iterate`)
- Sequence slicing (`take`)

These are fundamental building blocks for functional programming in OmniLisp and deserved dedicated test coverage.

## Impact
- **Improved reliability**: Core sequence operations are now verified
- **Better documentation**: Tests serve as usage examples
- **Regression detection**: Future changes to iterator.c will be caught
- **Gap filling**: Addresses previously untested critical runtime primitives

## Files Modified/Created
1. ✅ `/home/heefoo/code/OmniLisp/runtime/src/iterator.c` - Added `// TESTED` markers to 5 functions
2. ✅ `/home/heefoo/code/OmniLisp/tests/test_iterator_primitives.omni` - Created new test file (135 lines)

## Test Execution
To run the tests:
```bash
cd /home/heefoo/code/OmniLisp
./runtime/tests/test_runner test_iterator_primitives.omni
```

Or from within OmniLisp REPL:
```lisp
(load "tests/test_iterator_primitives.omni")
```

---

# Test Coverage Added - JSON Get (json-get)

## Date: 2026-01-23

## Summary
Added comprehensive test coverage for the `json-get` primitive in OmniLisp, which enables nested access to JSON data structures using dot-separated paths.

## Target Component
- **File**: `/home/heefoo/code/OmniLisp/runtime/src/json.c`
- **Function**: `prim_json_get` (exposed as `json-get` in OmniLisp)

## Test File Created
- **File**: `/home/heefoo/code/OmniLisp/tests/test_json_get.omni`
- **Type**: OmniLisp integration tests
- **Lines**: 281

## Function Now Marked as TESTED

**`prim_json_get`** (line 697)
- Tests: basic dict access, nested dict access, array index access, mixed dict/array traversal, error cases, deep nesting, arrays of objects

## Test Coverage Details

### Basic Dict Access Tests
- ✅ Single-level dict key access: `(json-get #{:name "Alice" :age 30} "name")` → "Alice"
- ✅ Single-level dict key (age): `(json-get simple-dict "age")` → 30

### Nested Dict Access Tests
- ✅ Two-level nested dict: `(json-get nested-dict "user.name")` → "Bob"
- ✅ Three-level nested dict: `(json-get nested-dict "user.address.city")` → "New York"
- ✅ Three-level nested dict (zip): `(json-get nested-dict "user.address.zip")` → "10001"
- ✅ Sibling key access: `(json-get nested-dict "id")` → 123

### Array Index Access Tests
- ✅ Array first element: `(json-get data "items.0")` → 1
- ✅ Array middle element: `(json-get data "items.1")` → 2
- ✅ Array last element: `(json-get data "items.2")` → 3

### Mixed Dict/Array Access Tests
- ✅ Dict to array access: `(json-get data "nested.data.0")` → 10
- ✅ Dict to array access (middle): `(json-get data "nested.data.1")` → 20
- ✅ Dict to array access (last): `(json-get data "nested.data.2")` → 30

### Error Cases and Edge Cases
- ✅ Missing top-level key: Returns `nothing`
- ✅ Missing nested key: Returns `nothing`
- ✅ Missing deep nested key: Returns `nothing`
- ✅ Invalid array index (out of bounds): Returns `nothing`
- ✅ Invalid array index (negative): Returns `nothing`
- ✅ Array index on dict: Returns `nothing`
- ✅ Dict key on array element: Returns `nothing`
- ✅ Empty path: Returns original data
- ✅ Path component type mismatch: Returns `nothing`
- ✅ Non-numeric array index: Returns `nothing`

### Deep Nesting Tests
- ✅ Very deep nesting (5 levels): `(json-get deep-data "level1.level2.level3.level4.value")` → "deep"

### Array of Objects Tests
- ✅ Access object in array: `(json-get users "users.0.name")` → "Alice"
- ✅ Access object in array (index 1): `(json-get users "users.1.name")` → "Bob"
- ✅ Access object in array (last): `(json-get users "users.2.name")` → "Charlie"
- ✅ Access object id in array: `(json-get users "users.1.id")` → 2

## Assertions Used
The test uses a custom test framework:
- `test-name` - For equality comparisons (expected vs actual)
- `test-nothing` - For checking `nothing` return values

## Why This Coverage Was Needed
The `json-get` function is a critical API for JSON manipulation that provides:
- **Nested data access**: Allows traversing complex JSON structures with simple path syntax
- **Type-safe traversal**: Handles both dict and array navigation with proper error handling
- **Robust error handling**: Returns `nothing` for invalid paths instead of crashing
- **Common use case**: Essential for working with JSON APIs and configuration files

This function had NO prior test coverage despite being a public API that users would call directly.

## Impact
- **Improved reliability**: JSON path navigation is now thoroughly tested
- **Better documentation**: Tests demonstrate correct usage patterns
- **Regression detection**: Future changes to json.c will be caught
- **Gap filling**: Addresses previously untested JSON API function
- **Edge case coverage**: Handles invalid paths, missing keys, array bounds, and type mismatches

## Files Modified/Created
1. ✅ `/home/heefoo/code/OmniLisp/runtime/src/json.c` - Added `// TESTED` marker to `prim_json_get` (line 691)
2. ✅ `/home/heefoo/code/OmniLisp/tests/test_json_get.omni` - Created new test file (281 lines)

## Test Execution
To run the tests from within OmniLisp REPL:
```lisp
(load "tests/test_json_get.omni")
```

Expected output: 27 tests total, all passing with "ALL TESTS PASSED!" message.
