# OmniLisp Event-Driven Pipeline Pattern

Using **algebraic effects** + **fibers** + **multiple dispatch** to implement event-driven architectures (like Clojure core.async pipelines).

---

## Core Pattern

```lisp
;; =============================================================
;; STEP 1: Define Action Types
;; =============================================================
;; Each action is a struct. The type IS the dispatch key.

(define {struct ActionA}
  [field1 {Type1}]
  [field2 {Type2}])

(define {struct ActionB}
  [field1 {Type1}])

(define {struct ActionC}
  [data {Any}])

(define {struct Finished}
  [result {Any}])

;; =============================================================
;; STEP 2: Define the Dispatch Effect
;; =============================================================
;; This replaces channel send/receive

(define ^:one-shot {effect dispatch})

;; =============================================================
;; STEP 3: Define Handlers via Multiple Dispatch
;; =============================================================
;; Each action type gets its own handler method

;; Default (fallback)
(define evolve [action {Any}] [ctx {Dict}]
  (log ctx (format "Unhandled: %s" (type-of action))))

;; ActionA -> may dispatch ActionB
(define evolve [action {ActionA}] [ctx {Dict}]
  (let [result (do-work-a action.field1 action.field2)]
    (perform dispatch (ActionB result))))

;; ActionB -> may dispatch ActionC
(define evolve [action {ActionB}] [ctx {Dict}]
  (let [result (do-work-b action.field1)]
    (perform dispatch (ActionC result))))

;; ActionC -> terminal, dispatches Finished
(define evolve [action {ActionC}] [ctx {Dict}]
  (let [final (do-work-c action.data)]
    (perform dispatch (Finished final))))

;; Finished -> log and done
(define evolve [action {Finished}] [ctx {Dict}]
  (log ctx (format "Done: %s" action.result)))

;; =============================================================
;; STEP 4: Error Handling via Type Hierarchy
;; =============================================================
;; Group actions that share error behavior under abstract parents

(define {abstract Rollbackable})
(define ^:parent {Rollbackable} {struct ActionB} [])
(define ^:parent {Rollbackable} {struct ActionC} [])

;; Default error handler
(define handle-error [action {Any}] [err {Any}] [ctx {Dict}]
  (log-error ctx (format "Error in %s: %s" (type-of action) err)))

;; Rollbackable actions trigger cleanup
(define handle-error [action {Rollbackable}] [err {Any}] [ctx {Dict}]
  (log-error ctx (format "Rollback triggered by %s" (type-of action)))
  (perform dispatch (Rollback action)))

;; =============================================================
;; STEP 5: The Pipeline Runner
;; =============================================================

(define run-pipeline [ctx initial-action]
  (with-fibers
    (handle
      ;; Start with initial action
      (evolve initial-action ctx)

      ;; dispatch effect: spawn fiber for each new action
      (dispatch [action resume]
        (spawn (fiber (fn []
          (handle
            (evolve action ctx)
            ;; Nested dispatch keeps spawning
            (dispatch [a r]
              (spawn (fiber (fn [] (evolve a ctx))))
              (r nothing))
            ;; Catch errors within this action
            (EFFECT_FAIL [e r]
              (handle-error action e ctx)
              (r nothing))))))
        (resume nothing))

      ;; Top-level error catch
      (EFFECT_FAIL [err resume]
        (handle-error initial-action err ctx)
        (resume nothing)))

    ;; Run scheduler until all fibers complete
    (run-fibers)))
```

---

## Alternative: Sequential Processing (No Concurrency)

```lisp
(define run-pipeline-sequential [ctx initial-action]
  (let [queue (atom (list initial-action))]

    (define process-next []
      (match (deref queue)
        (list) nothing  ;; empty, done
        [action .. rest]
          (do
            (set! queue rest)
            (handle
              (evolve-seq action ctx queue)
              (EFFECT_FAIL [e r]
                (handle-error action e ctx)
                (r nothing)))
            (process-next))))

    (process-next)))

;; Handlers enqueue instead of perform
(define evolve-seq [action {ActionA}] [ctx {Dict}] [queue {Atom}]
  (let [result (do-work-a action.field1 action.field2)]
    (swap! queue (fn [q] (append q (list (ActionB result)))))))
```

---

## Alternative: Bounded Concurrency (Worker Pool)

```lisp
(define run-pipeline-pooled [ctx initial-action num-workers]
  (with-fibers
    (let [queue (atom (list initial-action))
          done (atom false)]

      ;; Worker fiber
      (define worker []
        (loop []
          (let [q (deref queue)]
            (match q
              (list)
                (if (deref done)
                    nothing
                    (do (yield) (recur)))
              [action .. rest]
                (do
                  (set! queue rest)
                  (handle
                    (evolve action ctx)
                    (dispatch [a r]
                      (swap! queue (fn [q] (append q (list a))))
                      (r nothing))
                    (EFFECT_FAIL [e r]
                      (handle-error action e ctx)
                      (r nothing)))
                  (recur))))))

      ;; Spawn worker pool
      (let [workers (collect-list
                      (map (fn [_] (spawn (fiber worker)))
                           (range num-workers)))]
        ;; Wait for queue to drain
        (loop []
          (when (or (not (empty? (deref queue)))
                    (any? fiber-running? workers))
            (yield)
            (recur)))
        (set! done true)
        (for-each [w workers] (join w))))))
```

---

## Periodic Scheduling

```lisp
(define run-periodic [ctx interval-ms make-action]
  (with-fibers
    (let [running (atom true)]
      (spawn (fiber (fn []
        (loop []
          (when (deref running)
            (run-pipeline ctx (make-action))
            (sleep interval-ms)
            (recur))))))
      ;; Return control handle
      #{:stop (fn [] (set! running false))})))

;; Usage
(let [handle (run-periodic config 3600000
               (fn [] (FetchEmails (minus-hours (now) 24) (now))))]
  ;; Later...
  ((get handle 'stop)))
```

---

## Pattern Summary

```
┌─────────────────────────────────────────────────────────────┐
│                     EFFECT HANDLER                          │
│  (handle body (dispatch [action resume] ...))               │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│   ┌──────────┐    perform     ┌──────────┐    perform      │
│   │ ActionA  │ ─────────────► │ ActionB  │ ──────────────► │
│   └──────────┘    dispatch    └──────────┘    dispatch     │
│        │                           │                        │
│        ▼                           ▼                        │
│   ┌──────────┐               ┌──────────┐                  │
│   │ evolve   │               │ evolve   │                  │
│   │ ActionA  │               │ ActionB  │                  │
│   └──────────┘               └──────────┘                  │
│   (multiple dispatch)        (multiple dispatch)           │
│                                                             │
├─────────────────────────────────────────────────────────────┤
│                     FIBER SCHEDULER                         │
│  (with-fibers ... (run-fibers))                            │
│  - spawn creates lightweight task                           │
│  - yield parks fiber                                        │
│  - join waits for completion                                │
└─────────────────────────────────────────────────────────────┘
```

---

## Key Equivalences

| Clojure core.async | OmniLisp |
|--------------------|----------|
| `(chan n)` | Not needed; fibers + effects |
| `(>! ch val)` / `(put! ch val)` | `(perform dispatch val)` |
| `(<! ch)` | Handler clause: `(dispatch [val resume] ...)` |
| `(go ...)` | `(spawn (fiber (fn [] ...)))` |
| `(thread ...)` | `(spawn (fiber (fn [] ...)))` |
| `(<!! ch)` (blocking) | `(join fiber)` |
| `(alts! [ch1 ch2])` | Multiple effects in handler |
| `(timeout ms)` | `(sleep ms)` inside fiber |
| `(close! ch)` | Set atom flag, fibers check and exit |
| `defmulti`/`defmethod` | Multiple `define` with type dispatch |
| `derive` hierarchy | `^:parent` type inheritance |

---

## Complete Example: Email Processing Pipeline

Based on a Clojure core.async flow pattern:

```lisp
;; =============================================================
;; Action Types
;; =============================================================

(define {struct FetchEmails}
  [start-date {Date}]
  [end-date {Date}])

(define {struct PersistEmail}
  [message-id {String}]
  [date {Date}]
  [content {String}])

(define {struct ExtractOrderData}
  [email-id {String}]
  [content {String}]
  [date {Date}])

(define {struct PersistOrderDetails}
  [email-id {String}]
  [date {Date}]
  [order-details {Dict}])

(define {struct ArchiveProcessedEmail}
  [email-id {String}]
  [is-order? {Bool}]
  [received-date {Date}])

(define {struct RollbackPersistence}
  [email-id {String}])

(define {struct Finished}
  [data {Any}])

;; =============================================================
;; Dispatch Effect
;; =============================================================

(define ^:one-shot {effect dispatch})

;; =============================================================
;; Type Hierarchy for Error Handling
;; =============================================================

(define {abstract RollbackTrigger})
(define ^:parent {RollbackTrigger} {struct ExtractOrderData} [])
(define ^:parent {RollbackTrigger} {struct PersistOrderDetails} [])

;; =============================================================
;; Action Handlers
;; =============================================================

(define evolve [action {Any}] [ctx {Dict}]
  (log-info ctx (format "Completed: %s" (type-of action))))

(define evolve [action {FetchEmails}] [ctx {Dict}]
  (let [store (get ctx 'store)
        folder (get ctx 'folder)
        fld (ensure-email-folder store folder)
        msgs (get-mails-from-folder fld action.start-date action.end-date)]
    (for-each [msg msgs]
      (spawn (fiber (fn []
        (when-let [content (extract-full-email-content ctx msg)]
          (perform dispatch (PersistEmail
            (get content 'message-id)
            (get content 'date)
            (get content 'content))))))))
    (close-folder fld)))

(define evolve [action {PersistEmail}] [ctx {Dict}]
  (let [p-adapter (get ctx 'p-adapter)]
    (create-email! p-adapter action.message-id action.content action)
    (if (string-empty? action.content)
        (perform dispatch (Finished #{:email-id action.message-id}))
        (perform dispatch (ExtractOrderData
          action.message-id
          action.content
          action.date)))))

(define evolve [action {ExtractOrderData}] [ctx {Dict}]
  (let [assistant (get ctx 'assistant)
        response (process-message assistant action.content)]
    (if (string-empty? response)
        (perform dispatch (Finished action))
        (perform dispatch (PersistOrderDetails
          action.email-id
          (parse-date action.date)
          (extract-json-string-body response))))))

(define evolve [action {PersistOrderDetails}] [ctx {Dict}]
  (set-email-order-details! (get ctx 'p-adapter)
                            action.email-id
                            action.date
                            action.order-details)
  (perform dispatch (ArchiveProcessedEmail
    action.email-id
    (get action.order-details 'order)
    action.date)))

(define evolve [action {ArchiveProcessedEmail}] [ctx {Dict}]
  (let [store (get ctx 'store)
        src (get-folder store (get ctx 'folder))
        dest (ensure-order-folder store
               (get ctx 'processed-emails-folder)
               action.received-date
               action.is-order?)]
    (open-folder src 'read-write)
    (open-folder dest 'read-write)
    (move-messages! action.email-id src dest)
    (close-folder src)
    (close-folder dest)
    (perform dispatch (Finished action))))

(define evolve [action {RollbackPersistence}] [ctx {Dict}]
  (delete-email! (get ctx 'p-adapter) action.email-id)
  (perform dispatch (Finished
    (format "Rollback for %s succeeded" action.email-id))))

(define evolve [action {Finished}] [ctx {Dict}]
  (log-info ctx (format "Finished: %s" action.data)))

;; =============================================================
;; Error Handlers
;; =============================================================

(define handle-error [action {Any}] [err {Any}] [ctx {Dict}]
  (log-error ctx (format "Error in [%s]: %s" (type-of action) err)))

(define handle-error [action {RollbackTrigger}] [err {Any}] [ctx {Dict}]
  (log-error ctx (format "Rollback triggered by %s" (type-of action)))
  (perform dispatch (RollbackPersistence action.email-id)))

;; =============================================================
;; Pipeline Runner
;; =============================================================

(define run-pipeline [ctx initial-action]
  (with-fibers
    (handle
      (evolve initial-action ctx)

      (dispatch [action resume]
        (spawn (fiber (fn []
          (handle
            (evolve action ctx)
            (dispatch [a r]
              (spawn (fiber (fn [] (evolve a ctx))))
              (r nothing))
            (EFFECT_FAIL [e r]
              (handle-error action e ctx)
              (r nothing))))))
        (resume nothing))

      (EFFECT_FAIL [err resume]
        (handle-error initial-action err ctx)
        (resume nothing)))

    (run-fibers)))

;; =============================================================
;; Lifecycle
;; =============================================================

(define interval-dispatch [hours]
  (FetchEmails (minus-hours (now) hours) (now)))

(define init-flow [config]
  (when (get config 'session)
    (let [hours (get config 'email-retrieval-window-hours)
          state (atom #{:running true})]
      (with-fibers
        (spawn (fiber (fn []
          (loop []
            (when (get (deref state) 'running)
              (run-pipeline config (interval-dispatch hours))
              (sleep (* hours 3600 1000))
              (recur)))))))
      state)))

(define halt-flow [state]
  (swap! state (fn [s] (assoc s 'running false))))
```
