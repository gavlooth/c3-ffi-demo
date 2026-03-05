# Appendix B: Stdlib Reference

**[Back to Index](../OMNI_REFERENCE.md)**

Functions and macros defined in `stdlib/stdlib.lisp`.

---

### Macros

| Name | Signature | Description |
|------|-----------|-------------|
| `when` | `(when test body...)` | If test, evaluate body |
| `unless` | `(unless test body...)` | If not test, evaluate body |
| `cond` | `(cond t1 b1 t2 b2 ...)` | Multi-branch conditional |
| `with-defaults` | `(with-defaults n1 v1 ... body)` | Default values for dict params |
| `stream-yield` | `(stream-yield val)` | Generator-style yield |

### Higher-Order Functions

| Name | Signature | Description |
|------|-----------|-------------|
| `map` | `(map f coll)` | Apply f to each element (dispatched: List, Array, Iterator) |
| `filter` | `(filter pred coll)` | Keep matching elements (dispatched) |
| `foldl` | `(foldl f acc lst)` | Left fold |
| `foldr` | `(foldr f init lst)` | Right fold |
| `reverse` | `(reverse lst)` | Reverse list |
| `append` | `(append a b)` | Concatenate lists |
| `flatten` | `(flatten lst)` | Flatten one level |
| `for-each` | `(for-each f lst)` | Apply for side effects |
| `any?` | `(any? pred lst)` | Any element matches? |
| `every?` | `(every? pred lst)` | All elements match? |
| `find` | `(find pred lst)` | First match or nil |
| `remove` | `(remove pred lst)` | Remove matching |
| `partition` | `(partition pred lst)` | Split by predicate: `(kept . rejected)` |

### Sequence Operations

| Name | Signature | Description |
|------|-----------|-------------|
| `nth` | `(nth n lst)` | Nth element (0-indexed) |
| `take` | `(take n lst)` | First N elements |
| `drop` | `(drop n lst)` | Skip first N elements |
| `zip` | `(zip a b)` | Zip two collections |
| `range` | `(range n)` | List 0 to n-1 |
| `foldr` | `(foldr f init coll)` | Right fold (finite collections) |

### Iterator Combinators

| Name | Signature | Description |
|------|-----------|-------------|
| `iterator` | `(iterator coll)` | Convert to lazy iterator (dispatched) |
| `range-from` | `(range-from n)` | Infinite: n, n+1, n+2... |
| `repeat` | `(repeat x)` | Infinite repetition |
| `cycle` | `(cycle coll)` | Infinite cycle |

Note: `map`, `filter`, `take`, `drop`, `zip`, and `foldl` are dispatched and
operate lazily when passed an `Iterator`.
Force an iterator with constructors: `(list it)` or `(array it)`.

### Utilities

| Name | Signature | Description |
|------|-----------|-------------|
| `id` | `(id x)` | Identity function |
| `compose` | `(compose f g)` | `(lambda (x) (f (g x)))` |
| `partial` | `(partial f args...)` | Partial application |
| `default` | `(default v fallback)` | Return v if non-nil, else fallback |

### Association Lists

| Name | Signature | Description |
|------|-----------|-------------|
| `assoc` | `(assoc key alist)` | Find pair with key |
| `assoc-ref` | `(assoc-ref key alist)` | Get value for key |

### Error Handling

| Name | Signature | Description |
|------|-----------|-------------|
| `try` | `(try thunk handler)` | Catch `raise` effects |
| `assert!` | `(assert! cond msg)` | Raise if condition fails |

### Lazy Evaluation

| Name | Signature | Description |
|------|-----------|-------------|
| `delay` | `(delay thunk)` | Create memoized lazy value |
| `force` | `(force p)` | Force evaluation |

### Generators

| Name | Signature | Description |
|------|-----------|-------------|
| `stream-take` | `(stream-take n gen)` | Take N from generator |

### Handler Composition

| Name | Signature | Description |
|------|-----------|-------------|
| `with-handlers` | `(with-handlers handlers thunk)` | Chain handlers |
| `with-trampoline` | `(with-trampoline thunk)` | Trampoline bouncer |

### I/O Wrappers

These redefine I/O to go through effects (fast path when no handler):

`print`, `println`, `display`, `newline`, `read-file`, `write-file`,
`file-exists?`, `read-lines`, `tcp-connect`, `tcp-read`, `tcp-write`,
`tcp-close`, `dns-resolve`, `async-sleep`, `tls-connect`, `tls-read`,
`tls-write`, `tls-close`, `http-get`, `offload`, `thread-spawn`,
`thread-join`, `thread-join-timeout`, `thread-cancel`

### Type Predicates (Stdlib-Defined)

`int?`, `double?`, `number?`, `string?`, `symbol?`, `boolean?`, `list?`,
`closure?`, `array?`, `dict?`

### Numeric Predicates (Stdlib-Defined)

`zero?`, `positive?`, `negative?`, `even?`, `odd?`
