# Scientific Computing Module Plan

Integration of BLAS and LibTorch with OmniLisp for high-performance scientific computation.

---

## Executive Summary

**Strategy**: Use C libraries directly where possible, write thin C wrappers only when necessary.

| Library | API Type | Strategy | Rationale |
|---------|----------|----------|-----------|
| OpenBLAS | Pure C (CBLAS) | Direct FFI | No wrappers needed |
| LibTorch | C++ with C API | C wrapper layer | C++ classes need wrapping |

**Key Design Principles**:
1. Zero-copy tensor operations via region-based memory
2. Ownership annotations prevent memory errors
3. Lazy computation graphs for optimization
4. ASAP-compatible memory management (no GC)

---

## Part 1: BLAS Integration (OpenBLAS/CBLAS)

### 1.1 Library Selection

**OpenBLAS** is the recommended BLAS implementation:
- Pure C interface (CBLAS)
- No C++ dependencies
- Optimized for modern CPUs (AVX2, AVX-512)
- Thread-safe with OpenMP support
- BSD license

Installation:
```bash
# Debian/Ubuntu
apt install libopenblas-dev

# Arch
pacman -S openblas

# macOS
brew install openblas
```

### 1.2 Core Types

```lisp
;; ===========================================
;; Module: SciComp.BLAS
;; ===========================================

(module SciComp.BLAS
  (import {ffi :linux "libopenblas.so"
               :darwin "libopenblas.dylib"
          :as blas})

  ;; -----------------------------------------
  ;; Core C Types
  ;; -----------------------------------------

  ;; CBLAS layout enum
  (define CblasRowMajor 101)
  (define CblasColMajor 102)

  ;; CBLAS transpose enum
  (define CblasNoTrans 111)
  (define CblasTrans 112)
  (define CblasConjTrans 113)

  ;; CBLAS uplo (upper/lower triangular)
  (define CblasUpper 121)
  (define CblasLower 122)

  ;; CBLAS diag (unit diagonal)
  (define CblasNonUnit 131)
  (define CblasUnit 132)

  ;; CBLAS side (left/right multiply)
  (define CblasLeft 141)
  (define CblasRight 142)

  ;; -----------------------------------------
  ;; Vector Type (1D Dense Array)
  ;; -----------------------------------------

  ;; OmniLisp wrapper for contiguous float array
  (define {struct :ffi Vector}
    [data {CPtr CDouble}]     ;; Pointer to data
    [size {CSize}]            ;; Number of elements
    [stride {CInt}]           ;; Stride (1 for contiguous)
    [owner {CBool}])          ;; True if we own the data

  ;; -----------------------------------------
  ;; Matrix Type (2D Dense Array)
  ;; -----------------------------------------

  (define {struct :ffi Matrix}
    [data {CPtr CDouble}]     ;; Pointer to data (row-major)
    [rows {CSize}]            ;; Number of rows
    [cols {CSize}]            ;; Number of columns
    [ld {CSize}]              ;; Leading dimension (>= cols for row-major)
    [layout {CInt}]           ;; CblasRowMajor or CblasColMajor
    [owner {CBool}])          ;; True if we own the data
```

### 1.3 BLAS Level 1 (Vector Operations)

```lisp
  ;; -----------------------------------------
  ;; BLAS Level 1: Vector-Vector Operations
  ;; -----------------------------------------

  ;; y := a*x + y (DAXPY - Double-precision A*X Plus Y)
  (define {extern blas/cblas_daxpy}
    [n {CInt}]                          ;; Number of elements
    [alpha {CDouble}]                   ;; Scalar multiplier
    [^:borrowed x {CPtr CDouble}]       ;; Input vector x
    [incx {CInt}]                       ;; Stride for x
    [^:borrowed y {CPtr CDouble}]       ;; Input/output vector y (modified!)
    [incy {CInt}]                       ;; Stride for y
    -> {Nothing})

  ;; dot := x' * y (DDOT - Double-precision DOT product)
  (define {extern blas/cblas_ddot}
    [n {CInt}]
    [^:borrowed x {CPtr CDouble}]
    [incx {CInt}]
    [^:borrowed y {CPtr CDouble}]
    [incy {CInt}]
    -> {CDouble})

  ;; norm := ||x||_2 (DNRM2 - Double-precision NRM2)
  (define {extern blas/cblas_dnrm2}
    [n {CInt}]
    [^:borrowed x {CPtr CDouble}]
    [incx {CInt}]
    -> {CDouble})

  ;; sum := sum(|x_i|) (DASUM - Double-precision Absolute SUM)
  (define {extern blas/cblas_dasum}
    [n {CInt}]
    [^:borrowed x {CPtr CDouble}]
    [incx {CInt}]
    -> {CDouble})

  ;; idx := argmax(|x_i|) (IDAMAX - Index of Double Absolute MAX)
  (define {extern blas/cblas_idamax}
    [n {CInt}]
    [^:borrowed x {CPtr CDouble}]
    [incx {CInt}]
    -> {CSize})

  ;; x := a*x (DSCAL - Double-precision SCALe)
  (define {extern blas/cblas_dscal}
    [n {CInt}]
    [alpha {CDouble}]
    [^:borrowed x {CPtr CDouble}]       ;; Modified in place!
    [incx {CInt}]
    -> {Nothing})

  ;; y := x (DCOPY - Double-precision COPY)
  (define {extern blas/cblas_dcopy}
    [n {CInt}]
    [^:borrowed x {CPtr CDouble}]
    [incx {CInt}]
    [^:borrowed y {CPtr CDouble}]       ;; Output
    [incy {CInt}]
    -> {Nothing})

  ;; swap(x, y) (DSWAP - Double-precision SWAP)
  (define {extern blas/cblas_dswap}
    [n {CInt}]
    [^:borrowed x {CPtr CDouble}]       ;; Modified
    [incx {CInt}]
    [^:borrowed y {CPtr CDouble}]       ;; Modified
    [incy {CInt}]
    -> {Nothing})
```

### 1.4 BLAS Level 2 (Matrix-Vector Operations)

```lisp
  ;; -----------------------------------------
  ;; BLAS Level 2: Matrix-Vector Operations
  ;; -----------------------------------------

  ;; y := alpha*A*x + beta*y (DGEMV - Double GEneral Matrix-Vector)
  (define {extern blas/cblas_dgemv}
    [layout {CInt}]                     ;; CblasRowMajor/CblasColMajor
    [trans {CInt}]                      ;; CblasNoTrans/CblasTrans
    [m {CInt}]                          ;; Rows of A
    [n {CInt}]                          ;; Cols of A
    [alpha {CDouble}]                   ;; Scalar alpha
    [^:borrowed A {CPtr CDouble}]       ;; Matrix A
    [lda {CInt}]                        ;; Leading dimension of A
    [^:borrowed x {CPtr CDouble}]       ;; Input vector x
    [incx {CInt}]                       ;; Stride for x
    [beta {CDouble}]                    ;; Scalar beta
    [^:borrowed y {CPtr CDouble}]       ;; Input/output vector y
    [incy {CInt}]                       ;; Stride for y
    -> {Nothing})

  ;; A := alpha*x*y' + A (DGER - Double GEneral Rank-1 update)
  (define {extern blas/cblas_dger}
    [layout {CInt}]
    [m {CInt}]
    [n {CInt}]
    [alpha {CDouble}]
    [^:borrowed x {CPtr CDouble}]
    [incx {CInt}]
    [^:borrowed y {CPtr CDouble}]
    [incy {CInt}]
    [^:borrowed A {CPtr CDouble}]       ;; Modified
    [lda {CInt}]
    -> {Nothing})

  ;; y := alpha*A*x + beta*y for symmetric A (DSYMV)
  (define {extern blas/cblas_dsymv}
    [layout {CInt}]
    [uplo {CInt}]                       ;; CblasUpper/CblasLower
    [n {CInt}]
    [alpha {CDouble}]
    [^:borrowed A {CPtr CDouble}]
    [lda {CInt}]
    [^:borrowed x {CPtr CDouble}]
    [incx {CInt}]
    [beta {CDouble}]
    [^:borrowed y {CPtr CDouble}]
    [incy {CInt}]
    -> {Nothing})

  ;; Triangular matrix-vector solve (DTRSV)
  (define {extern blas/cblas_dtrsv}
    [layout {CInt}]
    [uplo {CInt}]
    [trans {CInt}]
    [diag {CInt}]                       ;; CblasUnit/CblasNonUnit
    [n {CInt}]
    [^:borrowed A {CPtr CDouble}]
    [lda {CInt}]
    [^:borrowed x {CPtr CDouble}]       ;; Input/output (solves A*x = b)
    [incx {CInt}]
    -> {Nothing})
```

### 1.5 BLAS Level 3 (Matrix-Matrix Operations)

```lisp
  ;; -----------------------------------------
  ;; BLAS Level 3: Matrix-Matrix Operations
  ;; -----------------------------------------

  ;; C := alpha*A*B + beta*C (DGEMM - Double GEneral Matrix-Matrix)
  (define {extern blas/cblas_dgemm}
    [layout {CInt}]                     ;; CblasRowMajor/CblasColMajor
    [transA {CInt}]                     ;; CblasNoTrans/CblasTrans for A
    [transB {CInt}]                     ;; CblasNoTrans/CblasTrans for B
    [m {CInt}]                          ;; Rows of C (rows of op(A))
    [n {CInt}]                          ;; Cols of C (cols of op(B))
    [k {CInt}]                          ;; Cols of op(A) = rows of op(B)
    [alpha {CDouble}]                   ;; Scalar alpha
    [^:borrowed A {CPtr CDouble}]       ;; Matrix A
    [lda {CInt}]                        ;; Leading dim of A
    [^:borrowed B {CPtr CDouble}]       ;; Matrix B
    [ldb {CInt}]                        ;; Leading dim of B
    [beta {CDouble}]                    ;; Scalar beta
    [^:borrowed C {CPtr CDouble}]       ;; Input/output matrix C
    [ldc {CInt}]                        ;; Leading dim of C
    -> {Nothing})

  ;; C := alpha*A*A' + beta*C for symmetric result (DSYRK)
  (define {extern blas/cblas_dsyrk}
    [layout {CInt}]
    [uplo {CInt}]
    [trans {CInt}]
    [n {CInt}]
    [k {CInt}]
    [alpha {CDouble}]
    [^:borrowed A {CPtr CDouble}]
    [lda {CInt}]
    [beta {CDouble}]
    [^:borrowed C {CPtr CDouble}]
    [ldc {CInt}]
    -> {Nothing})

  ;; Triangular matrix multiply (DTRMM)
  (define {extern blas/cblas_dtrmm}
    [layout {CInt}]
    [side {CInt}]                       ;; CblasLeft/CblasRight
    [uplo {CInt}]
    [trans {CInt}]
    [diag {CInt}]
    [m {CInt}]
    [n {CInt}]
    [alpha {CDouble}]
    [^:borrowed A {CPtr CDouble}]
    [lda {CInt}]
    [^:borrowed B {CPtr CDouble}]       ;; Modified: B := alpha*op(A)*B
    [ldb {CInt}]
    -> {Nothing})

  ;; Triangular solve (DTRSM)
  (define {extern blas/cblas_dtrsm}
    [layout {CInt}]
    [side {CInt}]
    [uplo {CInt}]
    [trans {CInt}]
    [diag {CInt}]
    [m {CInt}]
    [n {CInt}]
    [alpha {CDouble}]
    [^:borrowed A {CPtr CDouble}]
    [lda {CInt}]
    [^:borrowed B {CPtr CDouble}]       ;; Solves: op(A)*X = alpha*B
    [ldb {CInt}]
    -> {Nothing})
```

### 1.6 High-Level OmniLisp API

```lisp
  ;; -----------------------------------------
  ;; High-Level OmniLisp Vector Operations
  ;; -----------------------------------------

  ;; Create vector with owned memory
  (define (vec-new n)
    (let [data (ffi/alloc-array {CDouble} n)]
      (Vector data n 1 true)))

  ;; Create vector from list
  (define (vec-from-list xs)
    (let [n (length xs)
          data (ffi/alloc-array {CDouble} n)]
      (let loop [(i 0) (xs xs)]
        (when (not (null? xs))
          (ffi/array-set! data i (car xs))
          (loop (+ i 1) (cdr xs))))
      (Vector data n 1 true)))

  ;; Create zero vector
  (define (vec-zeros n)
    (let [v (vec-new n)]
      (ffi/memset (Vector-data v) 0 (* n (sizeof CDouble)))
      v))

  ;; Create ones vector
  (define (vec-ones n)
    (let [v (vec-new n)]
      (let loop [(i 0)]
        (when (< i n)
          (ffi/array-set! (Vector-data v) i 1.0)
          (loop (+ i 1))))
      v))

  ;; Free vector
  (define (vec-free! v)
    (when (Vector-owner v)
      (ffi/free (Vector-data v))))

  ;; with-vec scoped allocation
  (define-syntax with-vec
    (syntax-rules ()
      [(_ [name expr] body ...)
       (let [name expr]
         (try
           (begin body ...)
           (finally (vec-free! name))))]))

  ;; Vector operations
  (define (vec-dot x y)
    (assert (= (Vector-size x) (Vector-size y)))
    (cblas_ddot (Vector-size x)
                (Vector-data x) (Vector-stride x)
                (Vector-data y) (Vector-stride y)))

  (define (vec-norm x)
    (cblas_dnrm2 (Vector-size x)
                 (Vector-data x) (Vector-stride x)))

  (define (vec-scale! alpha x)
    (cblas_dscal (Vector-size x)
                 alpha
                 (Vector-data x) (Vector-stride x)))

  (define (vec-axpy! alpha x y)
    (assert (= (Vector-size x) (Vector-size y)))
    (cblas_daxpy (Vector-size x)
                 alpha
                 (Vector-data x) (Vector-stride x)
                 (Vector-data y) (Vector-stride y)))

  ;; Functional (allocating) versions
  (define (vec-add x y)
    (let [result (vec-copy x)]
      (vec-axpy! 1.0 y result)
      result))

  (define (vec-scale alpha x)
    (let [result (vec-copy x)]
      (vec-scale! alpha result)
      result))

  (define (vec-copy x)
    (let [result (vec-new (Vector-size x))]
      (cblas_dcopy (Vector-size x)
                   (Vector-data x) (Vector-stride x)
                   (Vector-data result) 1)
      result))

  ;; -----------------------------------------
  ;; High-Level OmniLisp Matrix Operations
  ;; -----------------------------------------

  ;; Create matrix with owned memory
  (define (mat-new rows cols)
    (let [data (ffi/alloc-array {CDouble} (* rows cols))]
      (Matrix data rows cols cols CblasRowMajor true)))

  ;; Create zero matrix
  (define (mat-zeros rows cols)
    (let [m (mat-new rows cols)]
      (ffi/memset (Matrix-data m) 0 (* rows cols (sizeof CDouble)))
      m))

  ;; Create identity matrix
  (define (mat-eye n)
    (let [m (mat-zeros n n)]
      (let loop [(i 0)]
        (when (< i n)
          (mat-set! m i i 1.0)
          (loop (+ i 1))))
      m))

  ;; Element access
  (define (mat-get m i j)
    (ffi/array-get (Matrix-data m)
                   (+ (* i (Matrix-ld m)) j)))

  (define (mat-set! m i j val)
    (ffi/array-set! (Matrix-data m)
                    (+ (* i (Matrix-ld m)) j)
                    val))

  ;; Free matrix
  (define (mat-free! m)
    (when (Matrix-owner m)
      (ffi/free (Matrix-data m))))

  ;; with-mat scoped allocation
  (define-syntax with-mat
    (syntax-rules ()
      [(_ [name expr] body ...)
       (let [name expr]
         (try
           (begin body ...)
           (finally (mat-free! name))))]))

  ;; Matrix-vector multiply: y := alpha*A*x + beta*y
  (define (mat-vec-mul! alpha A x beta y)
    (cblas_dgemv (Matrix-layout A)
                 CblasNoTrans
                 (Matrix-rows A) (Matrix-cols A)
                 alpha
                 (Matrix-data A) (Matrix-ld A)
                 (Vector-data x) (Vector-stride x)
                 beta
                 (Vector-data y) (Vector-stride y)))

  ;; Functional version
  (define (mat-vec-mul A x)
    (let [y (vec-zeros (Matrix-rows A))]
      (mat-vec-mul! 1.0 A x 0.0 y)
      y))

  ;; Matrix-matrix multiply: C := alpha*A*B + beta*C
  (define (mat-mul! alpha A B beta C)
    (cblas_dgemm (Matrix-layout A)
                 CblasNoTrans CblasNoTrans
                 (Matrix-rows A) (Matrix-cols B) (Matrix-cols A)
                 alpha
                 (Matrix-data A) (Matrix-ld A)
                 (Matrix-data B) (Matrix-ld B)
                 beta
                 (Matrix-data C) (Matrix-ld C)))

  ;; Functional version
  (define (mat-mul A B)
    (let [C (mat-zeros (Matrix-rows A) (Matrix-cols B))]
      (mat-mul! 1.0 A B 0.0 C)
      C))

  ;; Transpose (creates new matrix)
  (define (mat-transpose A)
    (let [B (mat-new (Matrix-cols A) (Matrix-rows A))]
      (let loop [(i 0)]
        (when (< i (Matrix-rows A))
          (let loop-j [(j 0)]
            (when (< j (Matrix-cols A))
              (mat-set! B j i (mat-get A i j))
              (loop-j (+ j 1))))
          (loop (+ i 1))))
      B))

  (export
    ;; Types
    Vector Matrix
    ;; Constants
    CblasRowMajor CblasColMajor
    CblasNoTrans CblasTrans CblasConjTrans
    CblasUpper CblasLower
    ;; Vector operations
    vec-new vec-zeros vec-ones vec-from-list vec-free!
    vec-dot vec-norm vec-scale! vec-axpy! vec-add vec-scale vec-copy
    with-vec
    ;; Matrix operations
    mat-new mat-zeros mat-eye mat-get mat-set! mat-free!
    mat-vec-mul mat-vec-mul! mat-mul mat-mul! mat-transpose
    with-mat))
```

### 1.7 Region-Based Matrix Allocation

For temporary computations, use regions:

```lisp
;; Efficient temporary allocation with arenas
(define (mat-chain-multiply matrices)
  "Multiply a chain of matrices using optimal temporary storage"
  (with-ffi-arena [arena (* 1024 1024 10)]  ;; 10MB arena
    (let loop [(result (car matrices))
               (rest (cdr matrices))]
      (if (null? rest)
          (mat-copy result)  ;; Copy result out of arena
          (let [temp (mat-mul-arena arena result (car rest))]
            (loop temp (cdr rest)))))))

(define (mat-mul-arena arena A B)
  "Matrix multiply with arena allocation"
  (let [C-data (ffi/arena-alloc arena
                 (* (Matrix-rows A) (Matrix-cols B) (sizeof CDouble)))]
    (let [C (Matrix C-data (Matrix-rows A) (Matrix-cols B)
                    (Matrix-cols B) CblasRowMajor false)]
      (mat-mul! 1.0 A B 0.0 C)
      C)))
```

---

## Part 2: LibTorch Integration

### 2.1 LibTorch Overview

LibTorch is PyTorch's C++ frontend. Key challenges:
- Primary API is C++ (ATen)
- C API exists but is limited
- Tensor operations manage their own memory (reference counting)

**Strategy**: Write thin C wrappers around LibTorch C++ API.

### 2.2 C Wrapper Layer

Create `libtorch_c_api.h` and `libtorch_c_api.cpp`:

```c
/* libtorch_c_api.h - C wrapper for LibTorch */
#ifndef LIBTORCH_C_API_H
#define LIBTORCH_C_API_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle types */
typedef struct TorchTensor* TensorHandle;
typedef struct TorchDevice* DeviceHandle;
typedef struct TorchModule* ModuleHandle;
typedef struct TorchOptimizer* OptimizerHandle;

/* Scalar types */
typedef enum {
    TORCH_FLOAT32 = 0,
    TORCH_FLOAT64 = 1,
    TORCH_INT32 = 2,
    TORCH_INT64 = 3,
    TORCH_BOOL = 4,
    TORCH_COMPLEX64 = 5,
    TORCH_COMPLEX128 = 6
} TorchDType;

/* Device types */
typedef enum {
    TORCH_CPU = 0,
    TORCH_CUDA = 1,
    TORCH_MPS = 2    /* Apple Metal */
} TorchDeviceType;

/* Error handling */
typedef struct {
    int code;
    char message[256];
} TorchError;

/* Get last error (thread-local) */
TorchError* torch_get_last_error(void);
void torch_clear_error(void);

/*
 * ===========================================
 * Tensor Creation
 * ===========================================
 */

/* Create empty tensor */
TensorHandle torch_tensor_empty(
    const int64_t* shape,
    size_t ndim,
    TorchDType dtype,
    TorchDeviceType device
);

/* Create tensor from data (copies data) */
TensorHandle torch_tensor_from_data(
    const void* data,
    const int64_t* shape,
    size_t ndim,
    TorchDType dtype
);

/* Create special tensors */
TensorHandle torch_tensor_zeros(const int64_t* shape, size_t ndim, TorchDType dtype);
TensorHandle torch_tensor_ones(const int64_t* shape, size_t ndim, TorchDType dtype);
TensorHandle torch_tensor_eye(int64_t n, TorchDType dtype);
TensorHandle torch_tensor_rand(const int64_t* shape, size_t ndim);
TensorHandle torch_tensor_randn(const int64_t* shape, size_t ndim);
TensorHandle torch_tensor_arange(double start, double end, double step, TorchDType dtype);
TensorHandle torch_tensor_linspace(double start, double end, int64_t steps);

/* Clone tensor (deep copy) */
TensorHandle torch_tensor_clone(TensorHandle t);

/* Free tensor */
void torch_tensor_free(TensorHandle t);

/*
 * ===========================================
 * Tensor Properties
 * ===========================================
 */

/* Get number of dimensions */
size_t torch_tensor_ndim(TensorHandle t);

/* Get shape (fills shape array, returns ndim) */
size_t torch_tensor_shape(TensorHandle t, int64_t* shape_out, size_t max_dims);

/* Get total number of elements */
int64_t torch_tensor_numel(TensorHandle t);

/* Get dtype */
TorchDType torch_tensor_dtype(TensorHandle t);

/* Get device */
TorchDeviceType torch_tensor_device(TensorHandle t);

/* Check if tensor requires grad */
int torch_tensor_requires_grad(TensorHandle t);

/* Get data pointer (careful: lifetime tied to tensor!) */
void* torch_tensor_data_ptr(TensorHandle t);

/* Copy data out (safe) */
int torch_tensor_copy_data(TensorHandle t, void* out, size_t out_bytes);

/*
 * ===========================================
 * Tensor Operations (Return New Tensors)
 * ===========================================
 */

/* Arithmetic */
TensorHandle torch_add(TensorHandle a, TensorHandle b);
TensorHandle torch_sub(TensorHandle a, TensorHandle b);
TensorHandle torch_mul(TensorHandle a, TensorHandle b);
TensorHandle torch_div(TensorHandle a, TensorHandle b);
TensorHandle torch_matmul(TensorHandle a, TensorHandle b);
TensorHandle torch_mm(TensorHandle a, TensorHandle b);  /* 2D matrix multiply */
TensorHandle torch_bmm(TensorHandle a, TensorHandle b); /* Batched matrix multiply */

/* Scalar operations */
TensorHandle torch_add_scalar(TensorHandle t, double scalar);
TensorHandle torch_mul_scalar(TensorHandle t, double scalar);
TensorHandle torch_pow_scalar(TensorHandle t, double exp);

/* Reduction */
TensorHandle torch_sum(TensorHandle t);
TensorHandle torch_sum_dim(TensorHandle t, int64_t dim, int keepdim);
TensorHandle torch_mean(TensorHandle t);
TensorHandle torch_mean_dim(TensorHandle t, int64_t dim, int keepdim);
TensorHandle torch_max(TensorHandle t);
TensorHandle torch_min(TensorHandle t);
TensorHandle torch_argmax(TensorHandle t);
TensorHandle torch_argmin(TensorHandle t);

/* Unary */
TensorHandle torch_neg(TensorHandle t);
TensorHandle torch_abs(TensorHandle t);
TensorHandle torch_sqrt(TensorHandle t);
TensorHandle torch_exp(TensorHandle t);
TensorHandle torch_log(TensorHandle t);
TensorHandle torch_sin(TensorHandle t);
TensorHandle torch_cos(TensorHandle t);
TensorHandle torch_tanh(TensorHandle t);
TensorHandle torch_sigmoid(TensorHandle t);
TensorHandle torch_relu(TensorHandle t);
TensorHandle torch_softmax(TensorHandle t, int64_t dim);

/* Shape manipulation */
TensorHandle torch_reshape(TensorHandle t, const int64_t* shape, size_t ndim);
TensorHandle torch_transpose(TensorHandle t, int64_t dim0, int64_t dim1);
TensorHandle torch_permute(TensorHandle t, const int64_t* dims, size_t ndim);
TensorHandle torch_squeeze(TensorHandle t);
TensorHandle torch_unsqueeze(TensorHandle t, int64_t dim);
TensorHandle torch_flatten(TensorHandle t);
TensorHandle torch_cat(TensorHandle* tensors, size_t count, int64_t dim);
TensorHandle torch_stack(TensorHandle* tensors, size_t count, int64_t dim);

/* Indexing */
TensorHandle torch_index(TensorHandle t, const int64_t* indices, size_t count);
TensorHandle torch_slice(TensorHandle t, int64_t dim, int64_t start, int64_t end);

/* Linear algebra */
TensorHandle torch_inverse(TensorHandle t);
TensorHandle torch_det(TensorHandle t);
TensorHandle torch_trace(TensorHandle t);
TensorHandle torch_norm(TensorHandle t);
TensorHandle torch_svd_u(TensorHandle t);
TensorHandle torch_svd_s(TensorHandle t);
TensorHandle torch_svd_v(TensorHandle t);

/* Type/device conversion */
TensorHandle torch_to_dtype(TensorHandle t, TorchDType dtype);
TensorHandle torch_to_device(TensorHandle t, TorchDeviceType device);
TensorHandle torch_contiguous(TensorHandle t);

/*
 * ===========================================
 * In-Place Operations (Modify Tensor)
 * ===========================================
 */

void torch_add_(TensorHandle a, TensorHandle b);
void torch_sub_(TensorHandle a, TensorHandle b);
void torch_mul_(TensorHandle a, TensorHandle b);
void torch_div_(TensorHandle a, TensorHandle b);
void torch_fill_(TensorHandle t, double value);
void torch_zero_(TensorHandle t);

/*
 * ===========================================
 * Autograd
 * ===========================================
 */

/* Enable/disable gradient tracking */
void torch_set_requires_grad(TensorHandle t, int requires);

/* Backward pass */
void torch_backward(TensorHandle loss);

/* Get gradient */
TensorHandle torch_grad(TensorHandle t);

/* Zero gradients */
void torch_zero_grad(TensorHandle t);

/* Detach from graph */
TensorHandle torch_detach(TensorHandle t);

/* No-grad context */
void torch_no_grad_begin(void);
void torch_no_grad_end(void);

/*
 * ===========================================
 * Neural Network Modules
 * ===========================================
 */

/* Linear layer */
ModuleHandle torch_nn_linear(int64_t in_features, int64_t out_features);
TensorHandle torch_nn_forward(ModuleHandle m, TensorHandle input);
void torch_nn_free(ModuleHandle m);

/* Get parameters */
size_t torch_nn_num_parameters(ModuleHandle m);
TensorHandle torch_nn_parameter(ModuleHandle m, size_t idx);

/* Save/load */
int torch_save_tensor(TensorHandle t, const char* path);
TensorHandle torch_load_tensor(const char* path);
int torch_save_module(ModuleHandle m, const char* path);
ModuleHandle torch_load_module(const char* path);

/*
 * ===========================================
 * Optimizers
 * ===========================================
 */

OptimizerHandle torch_optim_sgd(TensorHandle* params, size_t count, double lr);
OptimizerHandle torch_optim_adam(TensorHandle* params, size_t count, double lr);
void torch_optim_step(OptimizerHandle opt);
void torch_optim_zero_grad(OptimizerHandle opt);
void torch_optim_free(OptimizerHandle opt);

/*
 * ===========================================
 * Device Management
 * ===========================================
 */

int torch_cuda_is_available(void);
int torch_cuda_device_count(void);
void torch_cuda_synchronize(void);

#ifdef __cplusplus
}
#endif

#endif /* LIBTORCH_C_API_H */
```

### 2.3 C++ Implementation (Excerpt)

```cpp
/* libtorch_c_api.cpp - Implementation */
#include "libtorch_c_api.h"
#include <torch/torch.h>
#include <ATen/ATen.h>
#include <mutex>

/* Thread-local error handling */
static thread_local TorchError g_last_error = {0, ""};

extern "C" {

TorchError* torch_get_last_error(void) {
    return &g_last_error;
}

void torch_clear_error(void) {
    g_last_error.code = 0;
    g_last_error.message[0] = '\0';
}

static void set_error(int code, const char* msg) {
    g_last_error.code = code;
    strncpy(g_last_error.message, msg, sizeof(g_last_error.message) - 1);
}

/* Internal: Convert dtype */
static at::ScalarType to_scalar_type(TorchDType dtype) {
    switch (dtype) {
        case TORCH_FLOAT32: return at::kFloat;
        case TORCH_FLOAT64: return at::kDouble;
        case TORCH_INT32: return at::kInt;
        case TORCH_INT64: return at::kLong;
        case TORCH_BOOL: return at::kBool;
        default: return at::kFloat;
    }
}

/* Internal: Convert device */
static at::Device to_device(TorchDeviceType dev) {
    switch (dev) {
        case TORCH_CPU: return at::kCPU;
        case TORCH_CUDA: return at::kCUDA;
        case TORCH_MPS: return at::kMPS;
        default: return at::kCPU;
    }
}

/*
 * Tensor Creation
 */

TensorHandle torch_tensor_empty(
    const int64_t* shape,
    size_t ndim,
    TorchDType dtype,
    TorchDeviceType device
) {
    try {
        std::vector<int64_t> dims(shape, shape + ndim);
        auto options = at::TensorOptions()
            .dtype(to_scalar_type(dtype))
            .device(to_device(device));
        at::Tensor* t = new at::Tensor(at::empty(dims, options));
        return reinterpret_cast<TensorHandle>(t);
    } catch (const std::exception& e) {
        set_error(1, e.what());
        return nullptr;
    }
}

TensorHandle torch_tensor_from_data(
    const void* data,
    const int64_t* shape,
    size_t ndim,
    TorchDType dtype
) {
    try {
        std::vector<int64_t> dims(shape, shape + ndim);
        auto options = at::TensorOptions().dtype(to_scalar_type(dtype));

        /* Calculate total size */
        int64_t numel = 1;
        for (size_t i = 0; i < ndim; i++) numel *= dims[i];

        /* Create tensor and copy data */
        at::Tensor* t = new at::Tensor(at::empty(dims, options));
        size_t elem_size = t->element_size();
        memcpy(t->data_ptr(), data, numel * elem_size);

        return reinterpret_cast<TensorHandle>(t);
    } catch (const std::exception& e) {
        set_error(1, e.what());
        return nullptr;
    }
}

TensorHandle torch_tensor_zeros(const int64_t* shape, size_t ndim, TorchDType dtype) {
    try {
        std::vector<int64_t> dims(shape, shape + ndim);
        at::Tensor* t = new at::Tensor(at::zeros(dims, to_scalar_type(dtype)));
        return reinterpret_cast<TensorHandle>(t);
    } catch (const std::exception& e) {
        set_error(1, e.what());
        return nullptr;
    }
}

void torch_tensor_free(TensorHandle t) {
    if (t) {
        delete reinterpret_cast<at::Tensor*>(t);
    }
}

/*
 * Operations
 */

TensorHandle torch_add(TensorHandle a, TensorHandle b) {
    try {
        at::Tensor* ta = reinterpret_cast<at::Tensor*>(a);
        at::Tensor* tb = reinterpret_cast<at::Tensor*>(b);
        at::Tensor* result = new at::Tensor(at::add(*ta, *tb));
        return reinterpret_cast<TensorHandle>(result);
    } catch (const std::exception& e) {
        set_error(1, e.what());
        return nullptr;
    }
}

TensorHandle torch_matmul(TensorHandle a, TensorHandle b) {
    try {
        at::Tensor* ta = reinterpret_cast<at::Tensor*>(a);
        at::Tensor* tb = reinterpret_cast<at::Tensor*>(b);
        at::Tensor* result = new at::Tensor(at::matmul(*ta, *tb));
        return reinterpret_cast<TensorHandle>(result);
    } catch (const std::exception& e) {
        set_error(1, e.what());
        return nullptr;
    }
}

void torch_backward(TensorHandle loss) {
    try {
        at::Tensor* t = reinterpret_cast<at::Tensor*>(loss);
        t->backward();
    } catch (const std::exception& e) {
        set_error(1, e.what());
    }
}

int torch_cuda_is_available(void) {
    return at::cuda::is_available() ? 1 : 0;
}

} /* extern "C" */
```

### 2.4 OmniLisp LibTorch Bindings

```lisp
;; ===========================================
;; Module: SciComp.Torch
;; ===========================================

(module SciComp.Torch
  (import {ffi :linux "libomnitorch.so"
               :darwin "libomnitorch.dylib"
          :as torch})

  ;; -----------------------------------------
  ;; Opaque Types with Destructors
  ;; -----------------------------------------

  (define {opaque Tensor :destructor torch/torch_tensor_free})
  (define {opaque Module :destructor torch/torch_nn_free})
  (define {opaque Optimizer :destructor torch/torch_optim_free})

  ;; -----------------------------------------
  ;; DType Constants
  ;; -----------------------------------------

  (define Float32 0)
  (define Float64 1)
  (define Int32 2)
  (define Int64 3)
  (define Bool 4)

  ;; -----------------------------------------
  ;; Device Constants
  ;; -----------------------------------------

  (define CPU 0)
  (define CUDA 1)
  (define MPS 2)

  ;; -----------------------------------------
  ;; Tensor Creation FFI
  ;; -----------------------------------------

  (define {extern torch/torch_tensor_empty :null-on-error}
    [shape {CPtr CInt64}]
    [ndim {CSize}]
    [dtype {CInt}]
    [device {CInt}]
    -> {Option {Handle Tensor}})

  (define {extern torch/torch_tensor_from_data :null-on-error}
    [data {CPtr}]
    [shape {CPtr CInt64}]
    [ndim {CSize}]
    [dtype {CInt}]
    -> {Option {Handle Tensor}})

  (define {extern torch/torch_tensor_zeros :null-on-error}
    [shape {CPtr CInt64}]
    [ndim {CSize}]
    [dtype {CInt}]
    -> {Option {Handle Tensor}})

  (define {extern torch/torch_tensor_ones :null-on-error}
    [shape {CPtr CInt64}]
    [ndim {CSize}]
    [dtype {CInt}]
    -> {Option {Handle Tensor}})

  (define {extern torch/torch_tensor_rand :null-on-error}
    [shape {CPtr CInt64}]
    [ndim {CSize}]
    -> {Option {Handle Tensor}})

  (define {extern torch/torch_tensor_randn :null-on-error}
    [shape {CPtr CInt64}]
    [ndim {CSize}]
    -> {Option {Handle Tensor}})

  (define {extern torch/torch_tensor_clone :null-on-error}
    [t {Handle Tensor}]
    -> {Option {Handle Tensor}})

  (define {extern torch/torch_tensor_free}
    [^:consumed t {Handle Tensor}]
    -> {Nothing})

  ;; -----------------------------------------
  ;; Tensor Properties FFI
  ;; -----------------------------------------

  (define {extern torch/torch_tensor_ndim}
    [t {Handle Tensor}]
    -> {CSize})

  (define {extern torch/torch_tensor_numel}
    [t {Handle Tensor}]
    -> {CInt64})

  (define {extern torch/torch_tensor_dtype}
    [t {Handle Tensor}]
    -> {CInt})

  (define {extern torch/torch_tensor_shape}
    [t {Handle Tensor}]
    [shape_out {CPtr CInt64}]
    [max_dims {CSize}]
    -> {CSize})

  (define {extern torch/torch_tensor_copy_data}
    [t {Handle Tensor}]
    [out {CPtr}]
    [out_bytes {CSize}]
    -> {CInt})

  ;; -----------------------------------------
  ;; Tensor Operations FFI
  ;; -----------------------------------------

  (define {extern torch/torch_add :null-on-error}
    [a {Handle Tensor}]
    [b {Handle Tensor}]
    -> {Option {Handle Tensor}})

  (define {extern torch/torch_sub :null-on-error}
    [a {Handle Tensor}]
    [b {Handle Tensor}]
    -> {Option {Handle Tensor}})

  (define {extern torch/torch_mul :null-on-error}
    [a {Handle Tensor}]
    [b {Handle Tensor}]
    -> {Option {Handle Tensor}})

  (define {extern torch/torch_div :null-on-error}
    [a {Handle Tensor}]
    [b {Handle Tensor}]
    -> {Option {Handle Tensor}})

  (define {extern torch/torch_matmul :null-on-error}
    [a {Handle Tensor}]
    [b {Handle Tensor}]
    -> {Option {Handle Tensor}})

  (define {extern torch/torch_mm :null-on-error}
    [a {Handle Tensor}]
    [b {Handle Tensor}]
    -> {Option {Handle Tensor}})

  (define {extern torch/torch_sum :null-on-error}
    [t {Handle Tensor}]
    -> {Option {Handle Tensor}})

  (define {extern torch/torch_mean :null-on-error}
    [t {Handle Tensor}]
    -> {Option {Handle Tensor}})

  (define {extern torch/torch_relu :null-on-error}
    [t {Handle Tensor}]
    -> {Option {Handle Tensor}})

  (define {extern torch/torch_sigmoid :null-on-error}
    [t {Handle Tensor}]
    -> {Option {Handle Tensor}})

  (define {extern torch/torch_softmax :null-on-error}
    [t {Handle Tensor}]
    [dim {CInt64}]
    -> {Option {Handle Tensor}})

  (define {extern torch/torch_reshape :null-on-error}
    [t {Handle Tensor}]
    [shape {CPtr CInt64}]
    [ndim {CSize}]
    -> {Option {Handle Tensor}})

  (define {extern torch/torch_transpose :null-on-error}
    [t {Handle Tensor}]
    [dim0 {CInt64}]
    [dim1 {CInt64}]
    -> {Option {Handle Tensor}})

  ;; -----------------------------------------
  ;; Autograd FFI
  ;; -----------------------------------------

  (define {extern torch/torch_set_requires_grad}
    [t {Handle Tensor}]
    [requires {CInt}]
    -> {Nothing})

  (define {extern torch/torch_backward}
    [loss {Handle Tensor}]
    -> {Nothing})

  (define {extern torch/torch_grad :null-on-error}
    [t {Handle Tensor}]
    -> {Option {Handle Tensor}})

  (define {extern torch/torch_detach :null-on-error}
    [t {Handle Tensor}]
    -> {Option {Handle Tensor}})

  (define {extern torch/torch_no_grad_begin}
    -> {Nothing})

  (define {extern torch/torch_no_grad_end}
    -> {Nothing})

  ;; -----------------------------------------
  ;; Neural Network FFI
  ;; -----------------------------------------

  (define {extern torch/torch_nn_linear :null-on-error}
    [in_features {CInt64}]
    [out_features {CInt64}]
    -> {Option {Handle Module}})

  (define {extern torch/torch_nn_forward :null-on-error}
    [m {Handle Module}]
    [input {Handle Tensor}]
    -> {Option {Handle Tensor}})

  (define {extern torch/torch_nn_num_parameters}
    [m {Handle Module}]
    -> {CSize})

  (define {extern torch/torch_nn_parameter :null-on-error}
    [m {Handle Module}]
    [idx {CSize}]
    -> {Option {Handle Tensor}})

  ;; -----------------------------------------
  ;; Optimizer FFI
  ;; -----------------------------------------

  (define {extern torch/torch_optim_sgd :null-on-error}
    [params {CPtr {Handle Tensor}}]
    [count {CSize}]
    [lr {CDouble}]
    -> {Option {Handle Optimizer}})

  (define {extern torch/torch_optim_adam :null-on-error}
    [params {CPtr {Handle Tensor}}]
    [count {CSize}]
    [lr {CDouble}]
    -> {Option {Handle Optimizer}})

  (define {extern torch/torch_optim_step}
    [opt {Handle Optimizer}]
    -> {Nothing})

  (define {extern torch/torch_optim_zero_grad}
    [opt {Handle Optimizer}]
    -> {Nothing})

  ;; -----------------------------------------
  ;; Device Management FFI
  ;; -----------------------------------------

  (define {extern torch/torch_cuda_is_available}
    -> {CInt})

  (define {extern torch/torch_cuda_device_count}
    -> {CInt})

  (define {extern torch/torch_to_device :null-on-error}
    [t {Handle Tensor}]
    [device {CInt}]
    -> {Option {Handle Tensor}})

  ;; -----------------------------------------
  ;; High-Level OmniLisp API
  ;; -----------------------------------------

  ;; Shape helper: convert list to C array
  (define (shape-to-c shape)
    (let [n (length shape)
          arr (ffi/alloc-array {CInt64} n)]
      (let loop [(i 0) (xs shape)]
        (when (not (null? xs))
          (ffi/array-set! arr i (car xs))
          (loop (+ i 1) (cdr xs))))
      arr))

  ;; Create tensor from nested lists
  (define (tensor data :dtype Float32 :device CPU)
    "Create tensor from nested list, inferring shape"
    (let* [(flat (flatten-nested data))
           (shape (infer-shape data))
           (n (length flat))
           (c-data (ffi/alloc-array {CDouble} n))
           (c-shape (shape-to-c shape))]
      ;; Fill data array
      (let loop [(i 0) (xs flat)]
        (when (not (null? xs))
          (ffi/array-set! c-data i (car xs))
          (loop (+ i 1) (cdr xs))))
      ;; Create tensor
      (let [result (torch_tensor_from_data c-data c-shape (length shape) dtype)]
        (ffi/free c-data)
        (ffi/free c-shape)
        (match result
          [(Some t) t]
          [None (error "Failed to create tensor")]))))

  ;; Create zero tensor
  (define (zeros shape :dtype Float32)
    (let [c-shape (shape-to-c shape)]
      (let [result (torch_tensor_zeros c-shape (length shape) dtype)]
        (ffi/free c-shape)
        (match result
          [(Some t) t]
          [None (error "Failed to create zeros")]))))

  ;; Create ones tensor
  (define (ones shape :dtype Float32)
    (let [c-shape (shape-to-c shape)]
      (let [result (torch_tensor_ones c-shape (length shape) dtype)]
        (ffi/free c-shape)
        (match result
          [(Some t) t]
          [None (error "Failed to create ones")]))))

  ;; Create random tensor
  (define (rand shape)
    (let [c-shape (shape-to-c shape)]
      (let [result (torch_tensor_rand c-shape (length shape))]
        (ffi/free c-shape)
        (match result
          [(Some t) t]
          [None (error "Failed to create rand")]))))

  ;; Create random normal tensor
  (define (randn shape)
    (let [c-shape (shape-to-c shape)]
      (let [result (torch_tensor_randn c-shape (length shape))]
        (ffi/free c-shape)
        (match result
          [(Some t) t]
          [None (error "Failed to create randn")]))))

  ;; Get tensor shape as list
  (define (tensor-shape t)
    (let* [(ndim (torch_tensor_ndim t))
           (arr (ffi/alloc-array {CInt64} ndim))
           (_ (torch_tensor_shape t arr ndim))]
      (let loop [(i 0) (result '())]
        (if (>= i ndim)
            (reverse result)
            (loop (+ i 1)
                  (cons (ffi/array-get arr i) result))))))

  ;; Tensor operations (functional API)
  (define (t+ a b)
    (match (torch_add a b)
      [(Some t) t]
      [None (error "torch_add failed")]))

  (define (t- a b)
    (match (torch_sub a b)
      [(Some t) t]
      [None (error "torch_sub failed")]))

  (define (t* a b)
    (match (torch_mul a b)
      [(Some t) t]
      [None (error "torch_mul failed")]))

  (define (t/ a b)
    (match (torch_div a b)
      [(Some t) t]
      [None (error "torch_div failed")]))

  (define (t@ a b)
    "Matrix multiplication"
    (match (torch_matmul a b)
      [(Some t) t]
      [None (error "torch_matmul failed")]))

  (define (t-sum t)
    (match (torch_sum t)
      [(Some r) r]
      [None (error "torch_sum failed")]))

  (define (t-mean t)
    (match (torch_mean t)
      [(Some r) r]
      [None (error "torch_mean failed")]))

  (define (t-relu t)
    (match (torch_relu t)
      [(Some r) r]
      [None (error "torch_relu failed")]))

  (define (t-sigmoid t)
    (match (torch_sigmoid t)
      [(Some r) r]
      [None (error "torch_sigmoid failed")]))

  (define (t-softmax t dim)
    (match (torch_softmax t dim)
      [(Some r) r]
      [None (error "torch_softmax failed")]))

  (define (t-reshape t shape)
    (let [c-shape (shape-to-c shape)]
      (let [result (torch_reshape t c-shape (length shape))]
        (ffi/free c-shape)
        (match result
          [(Some r) r]
          [None (error "torch_reshape failed")]))))

  (define (t-transpose t dim0 dim1)
    (match (torch_transpose t dim0 dim1)
      [(Some r) r]
      [None (error "torch_transpose failed")]))

  ;; Autograd helpers
  (define (requires-grad! t)
    (torch_set_requires_grad t 1)
    t)

  (define (backward! loss)
    (torch_backward loss))

  (define (grad t)
    (match (torch_grad t)
      [(Some g) g]
      [None (error "No gradient available")]))

  (define (detach t)
    (match (torch_detach t)
      [(Some d) d]
      [None (error "torch_detach failed")]))

  ;; No-grad context
  (define-syntax with-no-grad
    (syntax-rules ()
      [(_ body ...)
       (begin
         (torch_no_grad_begin)
         (let [result (begin body ...)]
           (torch_no_grad_end)
           result))]))

  ;; Device helpers
  (define (cuda-available?)
    (= (torch_cuda_is_available) 1))

  (define (to-cuda t)
    (match (torch_to_device t CUDA)
      [(Some r) r]
      [None (error "Failed to move to CUDA")]))

  (define (to-cpu t)
    (match (torch_to_device t CPU)
      [(Some r) r]
      [None (error "Failed to move to CPU")]))

  ;; Neural network helpers
  (define (linear in-features out-features)
    (match (torch_nn_linear in-features out-features)
      [(Some m) m]
      [None (error "Failed to create Linear layer")]))

  (define (forward module input)
    (match (torch_nn_forward module input)
      [(Some out) out]
      [None (error "Forward pass failed")]))

  (define (parameters module)
    (let* [(n (torch_nn_num_parameters module))
           (params '())]
      (let loop [(i 0) (ps '())]
        (if (>= i n)
            (reverse ps)
            (match (torch_nn_parameter module i)
              [(Some p) (loop (+ i 1) (cons p ps))]
              [None (error "Failed to get parameter")])))))

  ;; Optimizer helpers
  (define (sgd params :lr 0.01)
    (with-ffi [param-arr (ffi/alloc-array {Handle Tensor} (length params))]
      (let loop [(i 0) (ps params)]
        (when (not (null? ps))
          (ffi/array-set! param-arr i (car ps))
          (loop (+ i 1) (cdr ps))))
      (match (torch_optim_sgd param-arr (length params) lr)
        [(Some opt) opt]
        [None (error "Failed to create SGD optimizer")])))

  (define (adam params :lr 0.001)
    (with-ffi [param-arr (ffi/alloc-array {Handle Tensor} (length params))]
      (let loop [(i 0) (ps params)]
        (when (not (null? ps))
          (ffi/array-set! param-arr i (car ps))
          (loop (+ i 1) (cdr ps))))
      (match (torch_optim_adam param-arr (length params) lr)
        [(Some opt) opt]
        [None (error "Failed to create Adam optimizer")])))

  (define (step! optimizer)
    (torch_optim_step optimizer))

  (define (zero-grad! optimizer)
    (torch_optim_zero_grad optimizer))

  ;; Export public API
  (export
    ;; Types
    Tensor Module Optimizer
    ;; DTypes
    Float32 Float64 Int32 Int64 Bool
    ;; Devices
    CPU CUDA MPS
    ;; Creation
    tensor zeros ones rand randn tensor-shape
    ;; Operations
    t+ t- t* t/ t@ t-sum t-mean t-relu t-sigmoid t-softmax
    t-reshape t-transpose
    ;; Autograd
    requires-grad! backward! grad detach with-no-grad
    ;; Device
    cuda-available? to-cuda to-cpu
    ;; NN
    linear forward parameters
    ;; Optimizers
    sgd adam step! zero-grad!))
```

---

## Part 3: Build System Integration

### 3.1 Makefile for C Wrapper

```makefile
# Makefile for libomnitorch C wrapper

LIBTORCH_PATH ?= /usr/local/lib/libtorch
CXX = g++
CXXFLAGS = -std=c++17 -fPIC -O2 -Wall
LDFLAGS = -shared

INCLUDES = -I$(LIBTORCH_PATH)/include \
           -I$(LIBTORCH_PATH)/include/torch/csrc/api/include

LIBS = -L$(LIBTORCH_PATH)/lib \
       -ltorch -ltorch_cpu -lc10 \
       -Wl,-rpath,$(LIBTORCH_PATH)/lib

# Add CUDA if available
ifdef CUDA
LIBS += -ltorch_cuda -lc10_cuda
CXXFLAGS += -DWITH_CUDA
endif

TARGET = libomnitorch.so
SOURCES = libtorch_c_api.cpp
OBJECTS = $(SOURCES:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

install: $(TARGET)
	cp $(TARGET) /usr/local/lib/
	cp libtorch_c_api.h /usr/local/include/

clean:
	rm -f $(OBJECTS) $(TARGET)

.PHONY: all install clean
```

### 3.2 Package Configuration

```lisp
;; scicomp.omni - Package manifest

(package scicomp
  :version "0.1.0"
  :description "Scientific Computing for OmniLisp"
  :dependencies []
  :native-deps
    [{:name "openblas"
      :linux "libopenblas.so"
      :darwin "libopenblas.dylib"
      :check "pkg-config --exists openblas"}
     {:name "libtorch"
      :linux "libomnitorch.so"
      :darwin "libomnitorch.dylib"
      :build "./build_libtorch_wrapper.sh"}]
  :modules
    ["SciComp.BLAS"
     "SciComp.Torch"
     "SciComp.Linalg"])
```

---

## Part 4: Example Programs

### 4.1 BLAS Example: Matrix Operations

```lisp
(import SciComp.BLAS)

;; Solve linear system Ax = b using LU decomposition
(define (solve-linear A b)
  (with-mat [A-copy (mat-copy A)]
    (with-vec [x (vec-copy b)]
      ;; Use LAPACK for actual solve (DGESV)
      ;; For now, simple Gaussian elimination
      (gaussian-eliminate! A-copy x)
      (back-substitute A-copy x)
      x)))

;; Example: Least squares regression
(define (least-squares X y)
  "Solve X'X * beta = X'y for beta"
  (let* [(Xt (mat-transpose X))
         (XtX (mat-mul Xt X))
         (Xty (mat-vec-mul Xt y))]
    (solve-linear XtX Xty)))

;; Usage
(define X (mat-from-lists
  '((1.0 1.0)
    (1.0 2.0)
    (1.0 3.0)
    (1.0 4.0))))

(define y (vec-from-list '(2.1 3.9 6.1 7.9)))

(let [beta (least-squares X y)]
  (print "Intercept: $(vec-get beta 0)")
  (print "Slope: $(vec-get beta 1)"))
```

### 4.2 LibTorch Example: Neural Network

```lisp
(import SciComp.Torch)

;; Simple MLP for XOR problem
(define (train-xor)
  ;; Training data
  (let* [(X (tensor '((0.0 0.0)
                      (0.0 1.0)
                      (1.0 0.0)
                      (1.0 1.0))))
         (y (tensor '((0.0) (1.0) (1.0) (0.0))))

         ;; Simple 2-layer network
         (W1 (requires-grad! (randn '(2 4))))
         (b1 (requires-grad! (zeros '(4))))
         (W2 (requires-grad! (randn '(4 1))))
         (b2 (requires-grad! (zeros '(1))))

         ;; Optimizer
         (opt (sgd (list W1 b1 W2 b2) :lr 0.1))]

    ;; Training loop
    (let loop [(epoch 0)]
      (when (< epoch 1000)
        ;; Forward pass
        (let* [(h1 (t+ (t@ X W1) b1))
               (a1 (t-relu h1))
               (h2 (t+ (t@ a1 W2) b2))
               (pred (t-sigmoid h2))

               ;; Loss: binary cross-entropy
               (loss (t-mean (t- (t* (t- 0.0 y) (t-log pred))
                                 (t* (t- 1.0 y) (t-log (t- 1.0 pred))))))]

          ;; Backward pass
          (zero-grad! opt)
          (backward! loss)
          (step! opt)

          ;; Log every 100 epochs
          (when (= (mod epoch 100) 0)
            (print "Epoch $(epoch): loss = $(tensor-item loss)")))

        (loop (+ epoch 1))))

    ;; Return trained weights
    (list W1 b1 W2 b2)))
```

### 4.3 Combined Example: GPU-Accelerated Linear Algebra

```lisp
(import SciComp.BLAS)
(import SciComp.Torch)

;; Use LibTorch for GPU, fall back to BLAS for CPU
(define (batched-matmul A B :device CPU)
  "Batched matrix multiplication with device selection"
  (if (and (= device CUDA) (cuda-available?))
      ;; GPU path: use LibTorch
      (let* [(A-t (to-cuda (tensor A)))
             (B-t (to-cuda (tensor B)))
             (C-t (t@ A-t B-t))]
        (tensor-to-list (to-cpu C-t)))
      ;; CPU path: use BLAS
      (with-mat [A-m (mat-from-lists A)]
        (with-mat [B-m (mat-from-lists B)]
          (let [C-m (mat-mul A-m B-m)]
            (mat-to-lists C-m))))))
```

---

## Part 5: Implementation Phases

### Phase 1: BLAS Core (No wrappers needed)

| Task | Effort | Description |
|------|--------|-------------|
| Define FFI declarations for CBLAS | Low | Direct binding, no C code |
| Implement Vector/Matrix structs | Low | FFI struct definitions |
| Level 1 operations (dot, norm, axpy) | Low | Direct calls |
| Level 2 operations (gemv) | Low | Direct calls |
| Level 3 operations (gemm) | Low | Direct calls |
| High-level OmniLisp wrappers | Medium | Functional API |
| Tests | Medium | Property-based testing |

**Deliverable**: `SciComp.BLAS` module with full BLAS coverage

### Phase 2: LibTorch C Wrapper

| Task | Effort | Description |
|------|--------|-------------|
| Write `libtorch_c_api.h` | Medium | C header with all declarations |
| Implement tensor creation | Medium | C++ → C bridge |
| Implement tensor operations | Medium | Basic arithmetic, reductions |
| Implement autograd | Medium | backward, grad |
| Implement NN modules | High | Linear, Conv2d, etc. |
| Implement optimizers | Medium | SGD, Adam |
| Build system (CMake/Make) | Low | Cross-platform build |
| Tests | High | C++ and OmniLisp tests |

**Deliverable**: `libomnitorch.so` shared library

### Phase 3: OmniLisp Torch Bindings

| Task | Effort | Description |
|------|--------|-------------|
| FFI declarations | Medium | All C API functions |
| High-level tensor API | Medium | Python-like interface |
| Autograd wrappers | Medium | `with-no-grad`, etc. |
| NN module builders | High | Layer composition |
| Training utilities | Medium | Training loops |
| GPU memory management | High | CUDA integration |
| Documentation | Medium | Examples and guides |

**Deliverable**: `SciComp.Torch` module

### Phase 4: Integration & Optimization

| Task | Effort | Description |
|------|--------|-------------|
| Region-based tensor allocation | High | Zero-copy for temporaries |
| Lazy computation graphs | High | Fusion optimization |
| ASAP integration | Medium | Proper ownership tracking |
| Benchmarking | Medium | Performance validation |
| Memory profiling | Medium | Leak detection |

---

## Part 6: Memory Management Strategy

### 6.1 BLAS Memory Model

```
┌─────────────────────────────────────────────────────────────┐
│                    OmniLisp Scope                           │
│                                                             │
│  (with-mat [A (mat-new 100 100)]                           │
│    (with-vec [x (vec-new 100)]                             │
│      (with-vec [y (vec-zeros 100)]                         │
│        (mat-vec-mul! 1.0 A x 0.0 y)                        │
│        (vec-norm y))))                                      │
│                                                             │
│  ASAP Behavior:                                            │
│  - mat-new allocates via ffi/alloc-array                   │
│  - BLAS operations borrow pointers                         │
│  - with-* macros inject free_obj at scope exit             │
│  - No GC involvement                                        │
└─────────────────────────────────────────────────────────────┘
```

**Key Insight**: BLAS operates on borrowed pointers. Ownership stays in OmniLisp.

### 6.2 LibTorch Memory Model

```
┌─────────────────────────────────────────────────────────────┐
│                    LibTorch Internal RC                     │
│                                                             │
│  TensorHandle ──────────► at::Tensor (C++ object)          │
│  (uint64_t)              - Internal reference count         │
│                          - Storage shared across views      │
│                                                             │
│  OmniLisp handle tracks the C++ object lifetime:           │
│  - torch_tensor_free decrements LibTorch's RC              │
│  - When RC=0, LibTorch frees storage                       │
│  - OmniLisp's ASAP calls torch_tensor_free at scope exit   │
└─────────────────────────────────────────────────────────────┘
```

**Key Insight**: LibTorch manages its own memory. OmniLisp controls handle lifetime.

### 6.3 Ownership Annotations in Practice

```lisp
;; Returns fresh tensor (caller owns)
(define {extern torch/torch_add :null-on-error}
  [^:borrowed a {Handle Tensor}]   ;; Borrowed: don't free after call
  [^:borrowed b {Handle Tensor}]   ;; Borrowed: don't free after call
  -> {^:owned Option {Handle Tensor}})  ;; Owned: caller must free

;; In-place operation (modifies first argument)
(define {extern torch/torch_add_}
  [a {Handle Tensor}]              ;; Modified in place
  [^:borrowed b {Handle Tensor}]
  -> {Nothing})
```

### 6.4 Arena Allocation for Computation Graphs

```lisp
;; Efficient temporary allocation during forward pass
(define (forward-with-arena model inputs)
  (with-ffi-arena [arena (* 1024 1024 100)]  ;; 100MB arena
    (let* [(layer1-out (layer1-forward model inputs))
           (layer2-out (layer2-forward model layer1-out))
           ;; ... intermediate tensors allocated in arena
           (final-out (output-layer model layerN-out))]
      ;; Copy result out before arena destruction
      (tensor-clone final-out))))
;; Arena freed here, all intermediates gone
```

---

## Summary

| Component | Strategy | Wrapper Needed | Key Files |
|-----------|----------|----------------|-----------|
| OpenBLAS | Direct FFI | No | `SciComp/BLAS.omni` |
| LibTorch | C wrapper | Yes | `libtorch_c_api.{h,cpp}`, `SciComp/Torch.omni` |

**Total new C code**: ~800 lines (`libtorch_c_api.cpp`)
**Total OmniLisp code**: ~1500 lines (BLAS + Torch modules)

The BLAS integration is straightforward (no wrappers), while LibTorch requires a thin C++ → C bridge due to its C++ API. Both integrate cleanly with ASAP memory management through explicit ownership annotations.
