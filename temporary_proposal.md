
// PSEUDOCODE: "Scope tethering" lifted to *component / island / SCC* level
// Goal: ASAP proves most lifetimes; for the remaining uncertain cases,
//       we use a scoped runtime tether to guarantee "don't dismantle yet".
//       Symmetric RC is ONLY used internally to dismantle cycles once safe.
//
// Mental model:
//   - A "Component" = a set of heap objects that may contain internal cycles.
//   - External reachability is mediated through "handles" into the component.
//   - When the last handle is gone AND no active tethers exist => dismantle.
//   - ASAP inserts most handle drops and can often prove tethers are unnecessary;
//     but where it can't, it inserts scoped tethers around borrows/uses.

//////////////////////////////////////////////////////////////
// Runtime data structures
//////////////////////////////////////////////////////////////

struct Object {
  id: ObjId
  comp: ComponentId

  // Symmetric edges *inside the component*.
  // Each edge is bidirectional: endpoints both track it.
  edges: Set<EdgeToken>   // tokens identify the edge record shared by both ends
}

struct Component {
  id: ComponentId

  // True owning / external "handles" to this component.
  // (These are NOT symmetric edges; they are boundary refs.)
  handle_count: Int

  // Scope tethering counter (or 1-bit if you can guarantee nesting discipline).
  // Counter is safer if multiple locals can tether concurrently.
  tether_count: Int

  // Optional: list of member objects to start dismantle without global scan.
  // (Could be a linked list of allocations belonging to component.)
  members: List<Object*>

  // Optional: mark to avoid re-entrant dismantle scheduling.
  dismantle_scheduled: Bool
}

global components: Map<ComponentId, Component>

//////////////////////////////////////////////////////////////
// Boundary operations (external ownership)
//////////////////////////////////////////////////////////////

function acquire_handle(compId: ComponentId) -> Handle {
  c = components[compId]
  c.handle_count += 1
  return Handle{ compId }
}

function release_handle(h: Handle):
  c = components[h.compId]
  c.handle_count -= 1
  // If no more external handles, we might be able to dismantle immediately.
  maybe_schedule_dismantle(c)

function maybe_schedule_dismantle(c: Component):
  if c.handle_count == 0 and c.tether_count == 0 and not c.dismantle_scheduled:
    c.dismantle_scheduled = true
    // Could run now or enqueue; still synchronous here.
    dismantle_component(c.id)

//////////////////////////////////////////////////////////////
// Component-level scope tethering (Vale-style idea, but for components)
//////////////////////////////////////////////////////////////

// This token is created by the compiler (ASAP fallback path) around scopes
// where we have non-owning references into the component and can't statically
// prove the component stays alive for the whole scope.
struct TetherToken {
  compId: ComponentId
  prevScheduled: Bool  // optional; if you want to restore scheduling state
}

function tether_begin(compId: ComponentId) -> TetherToken:
  c = components[compId]

  // Begin tether: guarantees "component won't dismantle while token is live".
  c.tether_count += 1

  // Optional: capture prior scheduling state if you need exact restoration
  // semantics (Vale captures/restore a bit). Often you don't need this.
  token = TetherToken{ compId: compId, prevScheduled: c.dismantle_scheduled }
  return token

function tether_end(token: TetherToken):
  c = components[token.compId]
  c.tether_count -= 1

  // If this was the last tether, dismantle might now be permitted.
  maybe_schedule_dismantle(c)

//////////////////////////////////////////////////////////////
// Internal symmetric teardown (cycles handled here)
//////////////////////////////////////////////////////////////

function dismantle_component(compId: ComponentId):
  c = components[compId]

  // Safety: only dismantle when externally unreachable AND untethered.
  // (ASAP + tethering are responsible for making this true.)
  assert(c.handle_count == 0)
  assert(c.tether_count == 0)

  // Core idea:
  //   Repeatedly cancel internal symmetric edges.
  //   When an object has no remaining edges, it becomes freeable.
  //
  // Implementation detail:
  //   With symmetric edges, removing an edge is "agreed" by both endpoints:
  //   we delete the shared EdgeToken from both objects' edge sets.

  worklist = Queue<Object*>()

  // Seed: all members (or a subset if you maintain a better frontier).
  for obj in c.members:
    worklist.push(obj)

  while not worklist.empty():
    obj = worklist.pop()

    // Skip if already freed in earlier step.
    if obj is null: continue

    // Cancel all internal edges.
    for e in copy(obj.edges):
      (a, b) = endpoints(e)             // returns Object* a, Object* b
      a.edges.remove(e)
      b.edges.remove(e)
      destroy_edge_record(e)

      // If removing this edge makes the neighbor edge-less, it may become freeable.
      if b.comp == compId and b.edges.size == 0:
        worklist.push(b)
      if a.comp == compId and a.edges.size == 0:
        worklist.push(a)

    // If object has no edges left, it is isolated inside the component.
    if obj.edges.size == 0:
      free_object(obj)                  // reclaim memory
      remove_from_component_members(c, obj)

  // Finally, free component metadata itself.
  destroy_component_record(compId)

//////////////////////////////////////////////////////////////
// How the compiler (ASAP) uses this
//////////////////////////////////////////////////////////////

// Case A (ASAP succeeds fully):
//   It proves the component has no incoming refs after point P,
//   so it emits: release_handle(lastHandle) at P
//   and does NOT need tethering.

function compiled_code_path_ASAP_proved(handle: Handle):
  // ... use objects ...
  release_handle(handle)  // triggers dismantle if no internal tethers

// Case B (ASAP cannot prove non-escape / complex aliasing):
//   It wraps a region of code holding borrows with a tether.
//   This removes the need for "external_rc checks" during that scope,
//   because dismantle is deferred by construction.

function compiled_code_path_with_tether(handle: Handle, compId: ComponentId):
  // Borrow enters a scope where safety can't be fully proven statically.
  t = tether_begin(compId)
  // ... operate on non-owning refs into component ...
  tether_end(t)

  // Later, when ownership ends:
  release_handle(handle)
  // If this was last handle and no tethers remain, dismantle runs.

//////////////////////////////////////////////////////////////
// Notes / variations
//////////////////////////////////////////////////////////////

// 1) If you can enforce strict lexical nesting (no concurrent tethers),
//    tether_count can be a 1-bit flag with "save/restore" like Vale.
//    Counter is the simple robust choice.
//
// 2) If you don't want component member lists, you can seed the dismantle
//    from objects referenced by boundary handles (but here handle_count==0),
//    or keep a per-component allocation list (cheap and practical).
//
// 3) Concurrency:
//    handle_count/tether_count updates must be atomic, and dismantle scheduling
//    must be race-safe (CAS on dismantle_scheduled).
//
// 4) If some internal edges can point outside the component, forbid it or
//    model those as boundary handles instead; otherwise your "internal-only SRC"
//    assumption breaks.
