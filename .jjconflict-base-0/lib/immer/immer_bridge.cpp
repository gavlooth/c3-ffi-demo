/*
 * immer_bridge.cpp - C wrapper for Immer persistent data structures
 *
 * Immer is a header-only C++14 library providing persistent immutable
 * data structures with structural sharing.
 *
 * This wrapper exposes a C interface for OmniLisp's FFI.
 */

#include "immer_bridge.h"

#include <immer/flex_vector.hpp>
#include <immer/map.hpp>
#include <immer/set.hpp>
#include <functional>

/*
 * We use void* as the element type for maximum flexibility.
 * OmniLisp objects are passed as opaque pointers.
 *
 * We use flex_vector instead of vector because it supports both
 * take and drop operations efficiently.
 *
 * For maps and sets, we need comparison. We use pointer comparison
 * which works for interned symbols/keywords. For value equality,
 * a custom comparator would be needed.
 */

using IVector = immer::flex_vector<void*>;
using IMap = immer::map<void*, void*>;
using ISet = immer::set<void*>;

extern "C" {

/* ========== Vector ========== */

void* immer_vector_empty(void) {
    return new IVector();
}

void* immer_vector_push(void* vec, void* elem) {
    auto* v = static_cast<IVector*>(vec);
    return new IVector(v->push_back(elem));
}

void* immer_vector_set(void* vec, int idx, void* elem) {
    auto* v = static_cast<IVector*>(vec);
    if (idx < 0 || static_cast<size_t>(idx) >= v->size()) {
        return vec;  // Return unchanged on out-of-bounds
    }
    return new IVector(v->set(idx, elem));
}

void* immer_vector_get(void* vec, int idx) {
    auto* v = static_cast<IVector*>(vec);
    if (idx < 0 || static_cast<size_t>(idx) >= v->size()) {
        return nullptr;
    }
    return (*v)[idx];
}

int immer_vector_size(void* vec) {
    auto* v = static_cast<IVector*>(vec);
    return static_cast<int>(v->size());
}

void* immer_vector_pop(void* vec) {
    auto* v = static_cast<IVector*>(vec);
    if (v->size() == 0) {
        return vec;  // Return unchanged on empty
    }
    return new IVector(v->take(v->size() - 1));
}

void* immer_vector_take(void* vec, int n) {
    auto* v = static_cast<IVector*>(vec);
    if (n <= 0) {
        return new IVector();
    }
    size_t take_n = std::min(static_cast<size_t>(n), v->size());
    return new IVector(v->take(take_n));
}

void* immer_vector_drop(void* vec, int n) {
    auto* v = static_cast<IVector*>(vec);
    if (n <= 0) {
        return new IVector(*v);  // Copy
    }
    if (static_cast<size_t>(n) >= v->size()) {
        return new IVector();
    }
    return new IVector(v->drop(n));
}

void immer_vector_free(void* vec) {
    delete static_cast<IVector*>(vec);
}

void immer_vector_foreach(void* vec, immer_iter_fn fn, void* ctx) {
    auto* v = static_cast<IVector*>(vec);
    for (auto& elem : *v) {
        if (fn(elem, ctx) != 0) {
            break;
        }
    }
}

/* ========== Map ========== */

void* immer_map_empty(void) {
    return new IMap();
}

void* immer_map_assoc(void* m, void* key, void* val) {
    auto* map = static_cast<IMap*>(m);
    return new IMap(map->set(key, val));
}

void* immer_map_get(void* m, void* key, void* not_found) {
    auto* map = static_cast<IMap*>(m);
    auto* found = map->find(key);
    return found ? *found : not_found;
}

void* immer_map_dissoc(void* m, void* key) {
    auto* map = static_cast<IMap*>(m);
    return new IMap(map->erase(key));
}

int immer_map_contains(void* m, void* key) {
    auto* map = static_cast<IMap*>(m);
    return map->count(key) > 0 ? 1 : 0;
}

int immer_map_count(void* m) {
    auto* map = static_cast<IMap*>(m);
    return static_cast<int>(map->size());
}

void immer_map_free(void* m) {
    delete static_cast<IMap*>(m);
}

void immer_map_foreach(void* m, immer_iter_kv_fn fn, void* ctx) {
    auto* map = static_cast<IMap*>(m);
    for (auto& [key, val] : *map) {
        if (fn(key, val, ctx) != 0) {
            break;
        }
    }
}

/* ========== Set ========== */

void* immer_set_empty(void) {
    return new ISet();
}

void* immer_set_conj(void* s, void* elem) {
    auto* set = static_cast<ISet*>(s);
    return new ISet(set->insert(elem));
}

void* immer_set_disj(void* s, void* elem) {
    auto* set = static_cast<ISet*>(s);
    return new ISet(set->erase(elem));
}

int immer_set_contains(void* s, void* elem) {
    auto* set = static_cast<ISet*>(s);
    return set->count(elem) > 0 ? 1 : 0;
}

int immer_set_count(void* s) {
    auto* set = static_cast<ISet*>(s);
    return static_cast<int>(set->size());
}

void immer_set_free(void* s) {
    delete static_cast<ISet*>(s);
}

void immer_set_foreach(void* s, immer_iter_fn fn, void* ctx) {
    auto* set = static_cast<ISet*>(s);
    for (auto& elem : *set) {
        if (fn(elem, ctx) != 0) {
            break;
        }
    }
}

} /* extern "C" */
