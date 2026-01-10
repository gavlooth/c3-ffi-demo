# Build and Test Guide

This document defines the canonical commands for building and testing OmniLisp. All agents and developers should use these commands to ensure consistency.

## Canonical Commands

### Runtime

The runtime is located in `runtime/`.

*   **Run Unit Tests:**
    ```bash
    make -C runtime/tests test
    ```
    This compiles and runs the main test suite (`runtime/tests/test_main.c`).

*   **Run with AddressSanitizer (ASAN):**
    ```bash
    make -C runtime/tests asan
    ```
    Use this to detect memory errors (leaks, UAF, etc.).

*   **Run Benchmarks:**
    ```bash
    make -C runtime/tests bench
    ```

*   **Clean:**
    ```bash
    make -C runtime/tests clean
    ```

### Compiler (C Toolchain)

The C compiler toolchain is located in `csrc/`.

*   **Run Unit Tests:**
    ```bash
    make -C csrc/tests test
    ```
    This compiles and runs the compiler test suite.

*   **Build Compiler:**
    ```bash
    make -C csrc all
    ```

*   **Clean:**
    ```bash
    make -C csrc clean
    ```

### Root Level

*   **Run All Tests (Runtime + Compiler):**
    ```bash
    make test
    ```

## Warning Policy

*   **Policy:** Warnings are allowed but must be tracked as specific cleanup tasks.
*   **Goal:** The codebase should eventually be warning-clean under `-Wall -Wextra`.
*   **CI:** CI may treat warnings as errors in specific "strict" steps, but local development allows warnings to prevent blocking progress on experimental features.
*   **Action:** If you introduce new warnings, please fix them immediately or file a TODO item if they cannot be fixed trivially.

## Build Artifacts

Build artifacts (binaries, object files) should **never** be committed to the repository.
The build system is configured to ignore these files. If you see tracked binaries, please remove them.
