# LibTorch Integration Plan

## Overview

LibTorch is PyTorch's C++ API. Integrating it with Purple requires bridging our Lisp-like language to C++ tensor operations while respecting ASAP memory management.

## Current Status

| Component | Status |
|-----------|--------|
| FFI basic calls | Done |
| Float support | Done |
| External declarations | Done |
| C99 code generation | Done |
| Tensor type | Not started |
| LibTorch C wrapper | Not started |

## Architecture Options

### Option 1: C Wrapper Layer (Recommended)

Create a thin C wrapper around libtorch's C++ API:

```
Purple Code  -->  Generated C99  -->  C Wrapper  -->  libtorch C++ API
```

**Advantages:**
- Purple keeps generating C99 (no C++ complexity)
- ASAP memory management can be preserved
- Clear ownership boundaries

**Implementation:**

```c
// purple_torch.h - C wrapper for libtorch
typedef struct PTensor PTensor;  // Opaque pointer to at::Tensor

PTensor* pt_zeros(int* dims, int ndims);
PTensor* pt_ones(int* dims, int ndims);
PTensor* pt_randn(int* dims, int ndims);
PTensor* pt_from_data(float* data, int* dims, int ndims);

// Operations
PTensor* pt_add(PTensor* a, PTensor* b);
PTensor* pt_mul(PTensor* a, PTensor* b);
PTensor* pt_matmul(PTensor* a, PTensor* b);
PTensor* pt_relu(PTensor* a);
PTensor* pt_softmax(PTensor* a, int dim);

// Memory
void pt_free(PTensor* t);
PTensor* pt_clone(PTensor* t);

// Data access
float* pt_data(PTensor* t);
int pt_ndims(PTensor* t);
int pt_dim(PTensor* t, int i);
```

### Option 2: Generate C++ Directly

Modify codegen to emit C++ instead of C99.

**Disadvantages:**
- Major refactoring needed
- C++ memory semantics conflict with ASAP
- More complex build system

### Option 3: Use LibTorch's C API (ATen)

PyTorch has some C bindings but they're limited and not well documented.

## Recommended Implementation Plan

### Phase 1: Tensor Type in Purple

Add a new `TTensor` type to represent tensor references:

```go
// In ast/value.go
const (
    // ...existing tags...
    TTensor  // Tensor reference (opaque pointer to C tensor)
)

type Value struct {
    // ...existing fields...
    TensorPtr uintptr  // For TTensor - opaque pointer
    TensorDims []int   // Shape information
}
```

### Phase 2: C Wrapper Library

Create `purple_torch.cpp`:

```cpp
// purple_torch.cpp
#include <torch/torch.h>

extern "C" {
    typedef at::Tensor* PTensor;

    PTensor pt_zeros(int* dims, int ndims) {
        std::vector<int64_t> shape(dims, dims + ndims);
        return new at::Tensor(torch::zeros(shape));
    }

    PTensor pt_add(PTensor a, PTensor b) {
        return new at::Tensor(*a + *b);
    }

    void pt_free(PTensor t) {
        delete t;
    }
}
```

Build with:
```bash
g++ -shared -fPIC -o libpurple_torch.so purple_torch.cpp \
    $(python3 -c "import torch; print(torch.utils.cmake_prefix_path)")/Torch/TorchConfig.cmake
```

### Phase 3: Purple Tensor Primitives

Add tensor operations to Purple:

```scheme
; Create tensors
(tensor-zeros 2 3)        ; 2x3 zeros
(tensor-ones 3 4 5)       ; 3x4x5 ones
(tensor-randn 10 10)      ; 10x10 random normal

; Operations
(tensor-add a b)
(tensor-mul a b)
(tensor-matmul a b)
(tensor-relu x)

; Neural network layers (higher-level)
(linear-layer 784 128 x)  ; Linear transformation
(conv2d-layer 3 64 3 x)   ; Convolution
```

### Phase 4: Memory Management Integration

**Challenge:** LibTorch has its own reference counting. We need to bridge this with ASAP.

**Solution:** Treat tensors as "external resources" with explicit ownership:

```scheme
; Tensors are reference-counted by libtorch
; Purple's ASAP generates pt_free() calls at appropriate points

(let ((x (tensor-randn 10 10)))
  (let ((y (tensor-relu x)))
    ; When y goes out of scope, pt_free(y) is called
    (tensor-sum y)))
; pt_free(x) called here
```

Generated C code:
```c
Obj* main_fn() {
    PTensor* x = pt_randn((int[]){10, 10}, 2);
    PTensor* y = pt_relu(x);
    Obj* result = pt_sum(y);
    pt_free(y);  // ASAP insertion
    pt_free(x);  // ASAP insertion
    return result;
}
```

### Phase 5: Autograd Support

LibTorch supports automatic differentiation. We can expose this:

```scheme
(with-grad
  (let ((x (tensor-randn 10 10 :requires-grad t)))
    (let ((loss (tensor-sum (tensor-mul x x))))
      (backward loss)
      (tensor-grad x))))
```

## Implementation Timeline

| Phase | Description | Complexity |
|-------|-------------|------------|
| 1 | TTensor type in AST | Low |
| 2 | C wrapper skeleton | Medium |
| 3 | Basic tensor primitives | Medium |
| 4 | ASAP integration | High |
| 5 | Autograd | High |
| 6 | Neural network layers | Medium |

## Build System Integration

The generated C code will need to link against the wrapper:

```bash
# Compile Purple program
./purple -c program.purple > program.c

# Compile with libtorch
gcc -o program program.c -L. -lpurple_torch \
    -Wl,-rpath,$(python3 -c "import torch; print(torch.utils.cmake_prefix_path + '/lib')")
```

## Example: Training a Simple Network

End goal - Purple code like this should work:

```scheme
; Define model
(define model
  (sequential
    (linear 784 128)
    (relu)
    (linear 128 10)
    (softmax -1)))

; Training loop
(define (train-step x y)
  (let ((pred (forward model x)))
    (let ((loss (cross-entropy pred y)))
      (backward loss)
      (optimizer-step)
      loss)))

; Run training
(for-each train-step training-data)
```

## Challenges & Mitigations

### 1. C++ Name Mangling
- **Problem:** LibTorch is C++, uses mangled names
- **Solution:** C wrapper with `extern "C"`

### 2. Memory Model Mismatch
- **Problem:** LibTorch uses shared_ptr, ASAP is compile-time
- **Solution:** Treat tensors as owned resources, generate free calls

### 3. GPU Memory
- **Problem:** CUDA tensors have different lifecycle
- **Solution:** Explicit device management in wrapper

### 4. Large Tensor Allocations
- **Problem:** Tensors don't fit ASAP's stack analysis
- **Solution:** Always heap-allocate tensors, track in escape analysis

### 5. Gradients
- **Problem:** Autograd creates internal computation graphs
- **Solution:** Treat as opaque, only expose backward() and grad()

## Next Steps

1. Implement TTensor type in AST
2. Write minimal C wrapper (zeros, add, free)
3. Add tensor primitives to evaluator
4. Test with simple program
5. Integrate with ASAP analysis
6. Add more operations incrementally

## References

- [LibTorch C++ API](https://pytorch.org/cppdocs/)
- [LibTorch Installation](https://pytorch.org/get-started/locally/)
- [PyTorch C++ Examples](https://github.com/pytorch/examples/tree/main/cpp)
