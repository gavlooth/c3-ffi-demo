# OmniLisp Runtime Tests

This directory contains comprehensive tests for the OmniLisp runtime system.

## Test Organization

### C-Level Tests (`test_*.c`)
C-level tests verify the correctness of runtime primitives at the implementation level. These tests are particularly important for:
- Memory safety (reference counting, allocations)
- Type handling
- Edge cases (NULL inputs, empty collections)
- Performance characteristics

### OmniLisp Tests (`*.omni`)
OmniLisp tests verify the integration and usability of runtime features from the language level. These tests are particularly important for:
- API design
- Function composition
- Real-world usage patterns
- Documentation examples

## New Test Files (Recent Additions)

The following test files were recently created to improve coverage:

### 1. `test_iterators_enhanced.c` (C Tests)
**Coverage:** Iterator and sequence operations
- prim_first, prim_rest, prim_iterate, prim_iter_next
- prim_take, prim_collect, prim_range

**Run with:**
```bash
make test_iterators_enhanced
# Or integrate into main test suite
```

### 2. `test_piping_enhanced.c` (C Tests)
**Coverage:** Pipe operator and method chaining
- prim_pipe, prim_method_chain, prim_apply (enhanced)

**Run with:**
```bash
make test_piping_enhanced
# Or integrate into main test suite
```

### 3. `../../tests/test_iterators_piping.omni` (OmniLisp Tests)
**Coverage:** Iterator and piping integration tests
- Basic operations, take, range, collect
- Pipe operator chaining, function composition
- Edge cases

**Run with:**
```bash
../../bin/omni ../../tests/test_iterators_piping.omni
```

### 4. `../../tests/test_collections_utils.omni` (OmniLisp Tests)
**Coverage:** Collection utilities
- sort-by, sort-with, group-by, partition
- coll-take, coll-drop, take-while, drop-while
- flatten, flatten-deep, zip, unzip
- frequencies, interleave, interpose

**Run with:**
```bash
../../bin/omni ../../tests/test_collections_utils.omni
```

## Running Tests

### Run All C Tests
```bash
cd /home/heefoo/code/OmniLisp/runtime/tests
make
./test_runner
```

### Run Specific C Test
```bash
make test_<name>
./test_<name>
```

### Run OmniLisp Tests
```bash
cd /home/heefoo/code/OmniLisp
./bin/omni tests/<test_file>.omni
```

## Test Framework

### C Test Framework
Located in `test_framework.h`, provides:
- `TEST_SUITE(name)` - Define test suite
- `TEST_SECTION(name)` - Define test section
- `RUN_TEST(fn)` - Run a test function
- `ASSERT(cond)` - Assert condition
- `ASSERT_EQ(a, b)` - Assert equality
- `ASSERT_NOT_NULL(p)` - Assert non-NULL
- `PASS()` - Mark test as passed
- `FAIL(msg)` - Mark test as failed

Example:
```c
void test_example(void) {
    Obj* result = prim_function(arg);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_to_int(result), expected);
    PASS();
}

void run_example_tests(void) {
    TEST_SUITE("Example Tests");
    TEST_SECTION("Section Name");
    RUN_TEST(test_example);
}
```

### OmniLisp Test Framework
Each test file defines its own simple framework:
- `test-num(name, expected, actual)` - Test numeric equality
- `test-bool(name, expected, actual)` - Test boolean equality
- `test-eq(name, expected, actual)` - Test general equality

Example:
```omni
(define (test-num name expected actual)
  (set! test-count (+ test-count 1))
  (if (= expected actual)
      (print "PASS:" name)
      (print "FAIL:" name)))

(test-num "example" 42 (+ 20 22))
```

## Test Coverage Areas

### Fully Tested
- String manipulation (test_string_utils.c)
- JSON parsing/generation (test_json.c)
- I/O operations (test_io.c)
- Math and numerics (test_math_numerics.c)
- Basic iterators (test_iterators.c)

### Recently Enhanced
- Iterator operations (test_iterators_enhanced.c + .omni)
- Piping and composition (test_piping_enhanced.c + .omni)
- Collection utilities (test_collections_utils.omni)

### Needs Coverage
- Generator functions (make_generator, generator_next, generator_done)
- Unified iterator operations (iter_next_unified, take_unified)
- Effect system integration
- Profile and benchmarking primitives

### Recently Updated
- Regex operations (test_regex.c) - refactored to use Pika parser infrastructure, all tests pass

## Test Coverage Analysis

To check which functions are tested vs untested, run:
```bash
cd /home/heefoo/code/OmniLisp/runtime/scripts
./check_test_coverage.csh
```

## Adding New Tests

### C Test Pattern
1. Create `test_<feature>.c`
2. Include `test_framework.h`
3. Include the source file being tested (or appropriate headers)
4. Write test functions using the framework
5. Create a `run_<feature>_tests()` function
6. Update `Makefile` to compile new test

### OmniLisp Test Pattern
1. Create `tests/test_<feature>.omni`
2. Define test framework helper functions
3. Write tests for the feature
4. Include expected outputs
5. Document any prerequisites

## Best Practices

### C Tests
- Test NULL/invalid inputs
- Test edge cases (empty, single element, large inputs)
- Test memory management (use dec_ref for objects)
- Test type handling (different numeric types, collections)
- Keep tests focused and independent

### OmniLisp Tests
- Test common usage patterns
- Test function composition
- Test with real-world examples
- Keep tests readable and maintainable
- Document expected behavior clearly

## Continuous Integration

These tests should be integrated into CI pipeline:
- Run all C tests on each commit
- Run critical OmniLisp tests on each commit
- Generate coverage reports
- Fail build on test failures

## Test Results

After running tests, review the summary:
- **Total** - Number of tests run
- **Passed** - Number of successful tests
- **Failed** - Number of failed tests

Aim for:
- 100% pass rate on committed code
- Fast test execution (< 5 minutes for all tests)
- Clear error messages for failures

## Known Issues

### test_list_circular_length hangs (infinite loop)
The test `test_list_circular_length` in the list operations section causes the test suite to hang indefinitely. This test attempts to compute the length of a circular list, which results in an infinite loop. The test suite should be updated to either:
- Add a timeout/iteration limit to the circular list length test
- Skip this test until circular list detection is implemented
- Use a different approach to test circular list handling

**Workaround:** Run individual test suites or use a timeout wrapper when running the full test suite.

## Troubleshooting

### Common Issues

**Compilation Errors:**
- Ensure all headers are included
- Check function signatures match source
- Verify Makefile includes new test files

**Runtime Errors:**
- Check for memory leaks (missing dec_ref)
- Verify objects are properly initialized
- Ensure test environment is set up correctly

**Test Failures:**
- Check expected values are correct
- Verify function behavior matches specification
- Review changes to source code that might affect test

### Debugging
1. Run specific failing test with verbose output
2. Use debugger (gdb) to trace execution
3. Add temporary printf statements
4. Check memory usage with valgrind

## Contributing

When adding new features:
1. Write tests first (TDD approach)
2. Implement the feature
3. Ensure all tests pass
4. Add documentation
5. Update this README if relevant

## Further Reading

- [NEW_TESTS_SUMMARY.md](NEW_TESTS_SUMMARY.md) - Summary of recently added tests
- [COVERAGE_PLAN.md](COVERAGE_PLAN.md) - Test coverage planning
- [TEST_FIXES.md](TEST_FIXES.md) - Notes on test fixes
- [TEST_PLAN.md](TEST_PLAN.md) - Overall test strategy

## Contact

For questions about testing, refer to:
- Project documentation
- GitHub issues
- Development team
