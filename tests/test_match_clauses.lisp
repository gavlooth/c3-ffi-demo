; Test match clause syntax - Issue 6 P3
; Tests both new array-based syntax and legacy alternating pairs

; Test 1: Basic array-based match clause [pattern result]
(define test1
  (match 42
    [42 "matched"]
    [_ "default"]))

; Test 2: Wildcard pattern
(define test2
  (match "hello"
    ["world" "nope"]
    [_ "caught by wildcard"]))

; Test 3: Multiple clauses
(define test3
  (match 3
    [1 "one"]
    [2 "two"]
    [3 "three"]
    [_ "other"]))

; Test 4: Guard with :when syntax
; [pattern :when guard result]
(define test4
  (let [n 15]
    (match n
      [n :when (> n 10) "greater than 10"]
      [_ "10 or less"])))

; Test 5: Guard with negative condition
(define test5
  (let [x 5]
    (match x
      [x :when (< x 0) "negative"]
      [x :when (= x 0) "zero"]
      [_ "positive"])))

; Test 6: Legacy syntax (alternating pattern result)
; This should still work for backward compatibility
; REVIEWED:SYNTAX
(define test6
  (match 100
    100 "hundred"
    _ "other"))

; Print results
(println "Test 1 (array clause): " test1)
(println "Test 2 (wildcard): " test2)
(println "Test 3 (multiple clauses): " test3)
(println "Test 4 (:when guard >10): " test4)
(println "Test 5 (:when guard positive): " test5)
(println "Test 6 (legacy syntax): " test6)
