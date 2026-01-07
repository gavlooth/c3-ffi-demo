# Changelog

All notable changes to OmniLisp are documented in this file.

## [Unreleased]

## [0.9.0] - 2026-01-07

### Added - Advanced Region Optimizations
- **Iterative Transmigration** (`runtime/src/memory/transmigrate.c`)
  - Worklist-based deep copy to prevent stack overflow.
  - O(1) cycle detection using `RegionBitmap`.
- **Thread-Local Tether Cache** (`runtime/src/memory/region_core.c`)
  - TLS-based tethering to elide atomic operations on hot paths.
- **Region Splicing** (`runtime/src/memory/region_core.c`)
  - O(1) block-level data movement between regions.
- **Metadata-Driven Traversal**
  - `TraceFn` and `TypeInfo` for extensible transmigration logic.

### Documentation
- **Unified Syntax Specification** (`SYNTAX.md`)
  - Merged all syntax, bracket calculus, and Pika DSL docs.
- **Memory Model Diagram** (`ARCHITECTURE.md`)
  - Added ASCII diagram for RC-G architecture.

## [0.8.0] - 2026-01-04

### Added - Region Infrastructure (C Runtime)

- **IRegion Interface** (`runtime/src/memory/region_core.c`)
  - Pluggable region backends via unified internal API.
- **Weak Reference Control Blocks**
  - O(1) weak reference invalidation with ABA protection.
- **External Handle Indexing** (`runtime/src/memory/handle.c`)
  - Stable integer handles for FFI and determinism.

## [0.5.0] - 2025-12-31

### Added
- **Region-Based Reference Counting** (`runtime/src/memory/region_core.c`)
  - Core RCB structure with `external_rc` and `tether_count`.
- **As Static As Possible (ASAP) Analysis** (`csrc/analysis/`)
  - Compile-time lifetime inference and free placement.
- **Pika Parser Implementation** (`csrc/parser/`)
  - Bottom-up dynamic programming parser in C.

## [0.1.0] - 2025-12-31

### Added
- **C99 Runtime Foundation**
  - Basic `Value` structure and immediate values.
  - Arena-based bump pointer allocator.
- **Compiler Frontend (C implementation)**
  - Initial S-expression parser and code generator.