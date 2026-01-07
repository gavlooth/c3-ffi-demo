# OmniLisp Implementation Plan

## Overview

This document details the complete implementation plan for OmniLisp.
The toolchain is implemented in pure ANSI C99 + POSIX, replacing the legacy Go-based implementation.

---

## Type System: Julia-Style Optional Types

**OmniLisp uses Julia's approach to types: types are OPTIONAL everywhere.**

### Type Annotations Are Always Optional

```lisp
;; No types - fully inferred (like Julia's duck typing)
(define add [x y]
  (+ x y))

;; Partial types - annotate what you want
(define add [x {Int} y]
  (+ x y))

;; Full types - for documentation or dispatch
(define add [x {Int} y {Int}] {Int}
  (+ x y))
```

---

## Architecture: Unified C Pipeline

| Component | Status | Description |
|-----------|--------------|-------------|
| **csrc/parser/** | 100% | Pika Parser: New bracket semantics, dot notation (C implementation) |
| **csrc/ast/** | 100% | Core AST: Value structure with tags for Array, Dict, Enum, etc. |
| **csrc/eval/** | 100% | Stage-Polymorphic Evaluator: Special forms and multiple dispatch. |
| **csrc/codegen/** | 100% | Code Generation: Translates AST to optimized C99. |
| **runtime/** | 100% | OmniLisp Runtime: RC-G memory model + POSIX concurrency. |

---

## Phase 1: Parser Implementation (csrc/parser/)

The Pika parser is implemented in C to support the "Tower of Interpreters" model (Level 0 parsing).

### 1.1 C Tokenizer

```c
typedef enum {
    TOK_LPAREN,   // (
    TOK_RPAREN,   // )
    TOK_LBRACKET, // [
    TOK_RBRACKET, // ]
    TOK_LBRACE,   // {
    TOK_RBRACE,   // }
    TOK_DOT,      // .
    TOK_STRING,   // "..."
    TOK_INT,      // 123
    TOK_SYMBOL,   // name
    TOK_EOF
} TokenType;
```

---

## Phase 2: AST Implementation (csrc/ast/)

Core Value structure using tagged unions for all OmniLisp types.

---

## Phase 3: Evaluator Implementation (csrc/eval/)

### 3.1 Core Special Forms

| Form | Description |
|------|-------------|
| `define` | Unified binding (values, functions, types, macros) |
| `let` | Flat binding vector `[]` |
| `match` | Pattern matching (positional pattern/result pairs) |
| `if` | Binary conditional |
| `lambda` | Anonymous function |
| `spawn` | Fiber/Thread creation |
| `chan` | Create channel |

---

## Phase 4: Code Generation (csrc/codegen/)

Translates the annotated AST into standalone C99 code linked with the OmniLisp runtime.

### 4.1 Match Code Gen
- Pattern matching is compiled into efficient `switch/if` chains in C.
- Destructuring is handled via static offset calculations.

---

## Implementation Status

### Phase 1: Parser Foundation (C) ✅ COMPLETE
- [x] Implement tokenizer in C
- [x] Implement array literal parsing `[]`
- [x] Implement type literal parsing `{}`
- [x] Implement dot notation `obj.field`
- [x] Bind `omni_read` to runtime

### Phase 2: AST Extensions ✅ COMPLETE
- [x] Value tags for Array, Dict, Tuple, Enum.
- [x] Region-aware constructors.
- [x] Type metadata (TypeInfo).

### Phase 3: Evaluator Primitives ✅ COMPLETE
- [x] Array/Dict/Tuple operations.
- [x] Generic `get` for dot notation.
- [x] Algebraic Effects handler.

### Phase 4: Pattern Matching ✅ COMPLETE
- [x] Positional match branches.
- [x] Guards `:when`.
- [x] Destructuring.

### Phase 5: Multiple Dispatch ✅ COMPLETE
- [x] Abstract type hierarchy.
- [x] Method definition and lookup.

---

## Verification Checklist

### Runtime & Memory ✅
- [x] ASAP static free placement.
- [x] Iterative transmigration (no stack overflow).
- [x] Bitmap cycle detection.
- [x] Thread-local tether caching.
- [x] Region splicing.

### Concurrency ✅
- [x] Fiber stack-switching.
- [x] Channel synchronization.
- [x] Scoped fiber execution (`with-fibers`).