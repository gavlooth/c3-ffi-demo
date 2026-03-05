# Appendix A: Primitive Reference

**[Back to Index](../OMNI_REFERENCE.md)**

---

### Dispatched Primitives (31)

These support user extension via method tables.

| Name | Arity | Category |
|------|-------|----------|
| `+` | 2 | Arithmetic |
| `-` | 2 | Arithmetic |
| `*` | 2 | Arithmetic |
| `/` | 2 | Arithmetic |
| `%` | 2 | Arithmetic |
| `=` | 2 | Comparison |
| `<` | 2 | Comparison |
| `>` | 2 | Comparison |
| `<=` | 2 | Comparison |
| `>=` | 2 | Comparison |
| `length` | 1 | Collection |
| `ref` | 2 | Collection |
| `push!` | 2 | Collection |
| `keys` | 1 | Collection |
| `values` | 1 | Collection |
| `has?` | 2 | Collection |
| `remove!` | 2 | Collection |
| `sort` | 1 | Sorting |
| `sort-by` | 2 | Sorting |
| `string-append` | variadic | String |
| `string-contains?` | 2 | String |
| `string-upcase` | 1 | String |
| `string-downcase` | 1 | String |
| `abs` | 1 | Math |
| `floor` | 1 | Math |
| `ceiling` | 1 | Math |
| `round` | 1 | Math |
| `truncate` | 1 | Math |
| `sqrt` | 1 | Math |
| `min` | 2 | Math |
| `max` | 2 | Math |

### Regular Primitives

**List Operations:**

| Name | Arity | Description |
|------|-------|-------------|
| `cons` | 2 | Construct pair |
| `car` | 1 | First element |
| `cdr` | 1 | Rest |
| `null?` | 1 | Is nil? |
| `pair?` | 1 | Is cons? |
| `list` / `List` | variadic | Create list / convert collection->list (array/iterator) |

**Boolean:**

| Name | Arity | Description |
|------|-------|-------------|
| `not` | 1 | Logical negation |

**I/O (raw, used by effect fast path):**

| Name | Arity | Description |
|------|-------|-------------|
| `__raw-print` | 1 | Raw print |
| `__raw-println` | 1 | Raw println |
| `__raw-newline` | 0 | Raw newline |
| `__raw-display` | 1 | Raw display |
| `__raw-read-file` | 1 | Raw file read |
| `__raw-write-file` | 2 | Raw file write |
| `__raw-file-exists?` | 1 | Raw file check |
| `__raw-read-lines` | 1 | Raw line reader |

**String:**

| Name | Arity | Description |
|------|-------|-------------|
| `string-join` | 2 | Join list with separator |
| `substring` | 3 | Extract substring |
| `string-split` | 2 | Split by delimiter |
| `string-length` | 1 | Length |
| `string-byte-length` | 1 | Byte length |
| `string->list` | 1 | To list of chars |
| `list->string` | 1 | List to string |
| `string-trim` | 1 | Trim whitespace |
| `string-index-of` | 2 | Find index |
| `string-replace` | 3 | Replace occurrences |
| `char-at` | 2 | Char at index |
| `string-repeat` | 2 | Repeat N times |

**Type & Introspection:**

| Name | Arity | Description |
|------|-------|-------------|
| `type-of` / `typeof` | 1 | Type name as symbol |
| `is?` | 2 | Type/subtype check |
| `instance?` | 1 | Is type instance? |
| `continuation?` | 1 | Is continuation? |
| `procedure?` | 1 | Is callable? |
| `bound?` | 1 | Is name defined? |
| `type-args` | 1 | Parametric type args |

**Conversion:**

| Name | Arity | Description |
|------|-------|-------------|
| `string->number` | 1 | Parse number |
| `number->string` | 1 | Number to string |
| `exact->inexact` | 1 | Int to double |
| `inexact->exact` | 1 | Double to int |
| `string->symbol` | 1 | String to symbol |
| `symbol->string` | 1 | Symbol to string |

**Math:**

| Name | Arity | Description |
|------|-------|-------------|
| `sin` | 1 | Sine |
| `cos` | 1 | Cosine |
| `tan` | 1 | Tangent |
| `asin` | 1 | Arc sine |
| `acos` | 1 | Arc cosine |
| `atan` | 1 | Arc tangent |
| `atan2` | 2 | Two-arg arctangent |
| `exp` | 1 | Exponential |
| `log` | 1 | Natural log |
| `log10` | 1 | Log base 10 |
| `pow` | 2 | Power |
| `gcd` | 2 | Greatest common divisor |
| `lcm` | 2 | Least common multiple |

**Bitwise:**

| Name | Arity | Description |
|------|-------|-------------|
| `bitwise-and` | 2 | AND |
| `bitwise-or` | 2 | OR |
| `bitwise-xor` | 2 | XOR |
| `bitwise-not` | 1 | NOT |
| `lshift` | 2 | Left shift |
| `rshift` | 2 | Right shift |

**Collections:**

| Name | Arity | Description |
|------|-------|-------------|
| `array` / `Array` | variadic | Create array / convert collection->array (list/iterator) |
| `array-set!` | 3 | Set element at index |
| `dict` / `Dict` | variadic | Create dict |
| `dict-set!` | 3 | Set key-value |
| `set` / `Set` | variadic | Create set |
| `set-add` | 2 | Add to set |
| `set-remove` | 2 | Remove from set |
| `set-contains?` | 2 | Set membership |
| `set-size` | 1 | Set cardinality |

**Coroutines:**

| Name | Arity | Description |
|------|-------|-------------|
| `coroutine` | 1 | Create coroutine |
| `resume` | 1 | Resume coroutine |
| `yield` | 1 | Yield value |
| `coroutine?` | 1 | Type check |

**Error:**

| Name | Arity | Description |
|------|-------|-------------|
| `error` | 1 | Create error value |
| `error-message` | 1 | Extract message |

**Iterator:**

| Name | Arity | Description |
|------|-------|-------------|
| `make-iterator` | 1 | Create from thunk |
| `next` | 1 | Get next (value . rest) pair |
| `collect` | 1 | Compatibility helper: collect iterator into list |
| `to-array` | 1 | Compatibility helper: collect iterator into array |

Preferred forcing style uses collection constructors:
`(list it)` and `(array it)`.

**Misc:**

| Name | Arity | Description |
|------|-------|-------------|
| `gensym` | 0 | Generate unique symbol |
| `format` | variadic | Printf-style formatting |
| `cl-format` | variadic | CL-style formatting |
| `read-string` | 1 | Parse string to Lisp |
| `eval` | 1 | Evaluate expression |
| `apply` | 2 | Apply function to arg list |
| `macroexpand` | 1 | Expand macro |
| `load` | 1 | Load and evaluate file |
| `unsafe-free!` | 1 | Free heap backing |

**Concurrency:**

| Name | Arity | Description |
|------|-------|-------------|
| `atomic` | 1 | Create atomic integer |
| `atomic-add!` | 2 | Atomic fetch-and-add |
| `atomic-read` | 1 | Atomic load |
| `atomic-cas!` | 3 | Compare-and-swap |
| `run-fibers` | variadic | Run thunks as fibers |
| `fiber-cancel` | 1 | Cancel a fiber |

**Regex (Pika):**

| Name | Arity | Description |
|------|-------|-------------|
| `re-match` | 2 | First match |
| `re-fullmatch` | 2 | Full string match |
| `re-find-all` | 2 | All matches |
| `re-split` | 2 | Split by pattern |
| `re-replace` | 3-4 | Replace matches |
| `re-match-pos` | 2 | Match with positions |
| `re-find-all-pos` | 2 | All matches with positions |

**PEG Grammar (Pika):**

| Name | Arity | Description |
|------|-------|-------------|
| `pika/grammar` | variadic | Define grammar |
| `pika/parse` | 2 | Parse input |
| `pika/fold` | 3 | Fold over parse tree |
| `pika/grammar-rules` | 1 | List rule names |
| `pika/match-span` | 3 | Match specific rule |
| `pika/parse-lisp` | 1 | Parse Lisp syntax |

**Unicode:**

| Name | Arity | Description |
|------|-------|-------------|
| `string-normalize` | 2 | Unicode normalize |
| `string-graphemes` | 1 | Split into graphemes |
| `string-codepoints` | 1 | List of codepoints |
| `char-category` | 1 | Unicode category |

**JSON:**

| Name | Arity | Description |
|------|-------|-------------|
| `json-parse` | 1 | Parse JSON string |
| `json-emit` | 1 | Emit compact JSON |
| `json-emit-pretty` | 1 | Emit pretty JSON |

**Compression:**

| Name | Arity | Description |
|------|-------|-------------|
| `gzip` | 1 | Gzip compress |
| `gunzip` | 1 | Gzip decompress |
| `deflate` | 1 | Deflate compress |
| `inflate` | 1 | Deflate decompress |

**Networking:**

| Name | Arity | Description |
|------|-------|-------------|
| `__raw-tcp-connect` | 1 | Raw TCP connect |
| `__raw-tcp-read` | 1 | Raw TCP read |
| `__raw-tcp-write` | 1 | Raw TCP write |
| `__raw-tcp-close` | 1 | Raw TCP close |
| `__raw-dns-resolve` | 1 | Raw DNS resolve |
| `__raw-async-sleep` | 1 | Raw sleep |
| `__raw-tls-connect` | 1 | Raw TLS connect |
| `__raw-tls-read` | 1 | Raw TLS read |
| `__raw-tls-write` | 1 | Raw TLS write |
| `__raw-tls-close` | 1 | Raw TLS close |

**Deduce (Database):**

| Name | Arity | Description |
|------|-------|-------------|
| `deduce` | variadic | Unified database interface |
