module lisp;

import std::core::mem;
import std::io;
import main;

// ============================================================
// Deduce — Relational Datalog for Omni
//
// LMDB-backed named relations, Datalog rules, bottom-up query.
// ============================================================

// ============================================================
// LMDB extern declarations (no wrapper)
// ============================================================

alias MdbEnv = void;
alias MdbTxn = void;
alias MdbCursor = void;
alias MdbDbi = uint;

struct MdbVal {
    usz   mv_size;
    void* mv_data;
}

extern fn int mdb_env_create(MdbEnv** env) @extern("mdb_env_create");
extern fn int mdb_env_open(MdbEnv* env, char* path, uint flags, uint mode) @extern("mdb_env_open");
extern fn int mdb_env_set_mapsize(MdbEnv* env, usz size) @extern("mdb_env_set_mapsize");
extern fn int mdb_env_set_maxdbs(MdbEnv* env, uint dbs) @extern("mdb_env_set_maxdbs");
extern fn void mdb_env_close(MdbEnv* env) @extern("mdb_env_close");
extern fn int mdb_txn_begin(MdbEnv* env, MdbTxn* parent, uint flags, MdbTxn** txn) @extern("mdb_txn_begin");
extern fn int mdb_txn_commit(MdbTxn* txn) @extern("mdb_txn_commit");
extern fn void mdb_txn_abort(MdbTxn* txn) @extern("mdb_txn_abort");
extern fn int mdb_dbi_open(MdbTxn* txn, char* name, uint flags, MdbDbi* dbi) @extern("mdb_dbi_open");
extern fn int mdb_put(MdbTxn* txn, MdbDbi dbi, MdbVal* key, MdbVal* data, uint flags) @extern("mdb_put");
extern fn int mdb_get(MdbTxn* txn, MdbDbi dbi, MdbVal* key, MdbVal* data) @extern("mdb_get");
extern fn int mdb_del(MdbTxn* txn, MdbDbi dbi, MdbVal* key, MdbVal* data) @extern("mdb_del");
extern fn int mdb_cursor_open(MdbTxn* txn, MdbDbi dbi, MdbCursor** cursor) @extern("mdb_cursor_open");
extern fn int mdb_cursor_get(MdbCursor* cursor, MdbVal* key, MdbVal* data, int op) @extern("mdb_cursor_get");
extern fn void mdb_cursor_close(MdbCursor* cursor) @extern("mdb_cursor_close");

const uint MDB_CREATE = 0x40000;
const uint MDB_NOSUBDIR = 0x4000;
const uint MDB_RDONLY = 0x20000;
const int MDB_SUCCESS = 0;
const int MDB_NOTFOUND = -30798;
const int MDB_FIRST = 0;
const int MDB_NEXT = 8;
const int MDB_SET_RANGE = 17;

// ============================================================
// Deduce Database Handle
// ============================================================

struct DeduceDb {
    MdbEnv* env;
    bool    open;
}

// ============================================================
// Relation — named tuple schema + LMDB backing
// ============================================================

const usz MAX_RELATION_COLS = 16;

struct Relation {
    SymbolId     name;
    SymbolId[MAX_RELATION_COLS] columns;
    usz          col_count;
    MdbDbi       dbi;           // LMDB database handle for this relation
    DeduceDb*    db;            // parent database
    SymbolId     key_col;       // primary key column (0 = none)
    SymbolId     index_col;     // indexed column (0 = none)
}

// ============================================================
// Tuple encoding: Values → bytes for LMDB storage
// ============================================================

// Encode a tuple of Values into a byte buffer. Format: [tag byte][data]...
// Tags: 0=nil, 1=int(8 bytes LE), 2=double(8 bytes), 3=string(2 byte len + data), 4=symbol(2 byte id)
fn usz? encode_tuple(Value*[] vals, char* buf, usz buf_size) {
    usz pos = 0;
    for (usz i = 0; i < vals.len; i++) {
        Value* v = vals[i];
        if (v == null || v.tag == NIL) {
            if (pos >= buf_size) return WRITE_FAILED~;
            buf[pos++] = 0;
        } else if (v.tag == INT) {
            if (pos + 9 > buf_size) return WRITE_FAILED~;
            buf[pos++] = 1;
            long val = v.int_val;
            for (usz j = 0; j < 8; j++) {
                buf[pos++] = (char)(val & 0xFF);
                val >>= 8;
            }
        } else if (v.tag == DOUBLE) {
            if (pos + 9 > buf_size) return WRITE_FAILED~;
            buf[pos++] = 2;
            double dval = v.double_val;
            char* dp = (char*)&dval;
            for (usz j = 0; j < 8; j++) buf[pos++] = dp[j];
        } else if (v.tag == STRING) {
            usz slen = v.str_len;
            if (slen > 0xFFFF) slen = 0xFFFF;
            if (pos + 3 + slen > buf_size) return WRITE_FAILED~;
            buf[pos++] = 3;
            buf[pos++] = (char)(slen & 0xFF);
            buf[pos++] = (char)((slen >> 8) & 0xFF);
            for (usz j = 0; j < slen; j++) buf[pos++] = v.str_chars[j];
        } else if (v.tag == SYMBOL) {
            if (pos + 3 > buf_size) return WRITE_FAILED~;
            buf[pos++] = 4;
            uint sid = (uint)v.sym_val;
            buf[pos++] = (char)(sid & 0xFF);
            buf[pos++] = (char)((sid >> 8) & 0xFF);
        } else {
            if (pos >= buf_size) return WRITE_FAILED~;
            buf[pos++] = 0;  // unsupported → nil
        }
    }
    return pos;
}

// Decode a tuple from bytes back to Values
fn usz? decode_tuple(char* buf, usz buf_size, Value*[] out, Interp* interp) {
    usz pos = 0;
    usz count = 0;
    while (pos < buf_size && count < out.len) {
        char tag = buf[pos++];
        if (tag == 0) {
            out[count++] = make_nil(interp);
        } else if (tag == 1) {
            if (pos + 8 > buf_size) return READ_FAILED~;
            long val = 0;
            for (usz j = 0; j < 8; j++) val |= ((long)(uint)(char)buf[pos + j]) << (j * 8);
            pos += 8;
            out[count++] = make_int(interp, val);
        } else if (tag == 2) {
            if (pos + 8 > buf_size) return READ_FAILED~;
            double dval;
            char* dp = (char*)&dval;
            for (usz j = 0; j < 8; j++) dp[j] = buf[pos + j];
            pos += 8;
            out[count++] = make_double(interp, dval);
        } else if (tag == 3) {
            if (pos + 2 > buf_size) return READ_FAILED~;
            usz slen = (usz)(uint)(char)buf[pos] | ((usz)(uint)(char)buf[pos + 1] << 8);
            pos += 2;
            if (pos + slen > buf_size) return READ_FAILED~;
            out[count++] = make_string(interp, buf[pos..pos + slen - 1]);
            pos += slen;
        } else if (tag == 4) {
            if (pos + 2 > buf_size) return READ_FAILED~;
            uint sid = (uint)(char)buf[pos] | ((uint)(char)buf[pos + 1] << 8);
            pos += 2;
            Value* v = interp.alloc_value();
            v.tag = SYMBOL;
            v.sym_val = (SymbolId)sid;
            out[count++] = v;
        } else {
            return READ_FAILED~;  // unknown tag = malformed data
        }
    }
    return count;
}

// ============================================================
// (deduce-open path) → database handle
// (deduce-open 'memory) → ephemeral database
// ============================================================

fn Value* prim_deduce_open(Value*[] args, Env* env, Interp* interp) {
    // fault: lisp::ARITY_MISMATCH
    if (args.len < 1) return raise_error(interp, "deduce-open: expected path or 'memory");

    DeduceDb* db = (DeduceDb*)mem::malloc(DeduceDb.sizeof);
    // fault: lisp::WRITE_FAILED
    if (db == null) return raise_error(interp, "deduce-open: out of memory");

    int rc = mdb_env_create(&db.env);
    if (rc != MDB_SUCCESS) {
        mem::free(db);
        // fault: lisp::WRITE_FAILED
        return raise_error(interp, "deduce-open: failed to create LMDB env");
    }

    mdb_env_set_mapsize(db.env, 1073741824);  // 1GB default
    mdb_env_set_maxdbs(db.env, 64);

    // Determine path
    char[256] path_buf;  // buffer: C-interop (keep) — null-terminated for mdb_env_open
    uint flags = 0;

    if (args[0].tag == SYMBOL) {
        // 'memory → use unique temp path with NOSUBDIR
        // Generate unique name using address of db pointer
        char[32] suffix;  // buffer: C-interop (keep) — hex-formatted, copied into path_buf
        usz slen = io::bprintf(&suffix, "%x", (uint)(usz)db)!!.len;
        char[] base = "/tmp/deduce-";
        for (usz i = 0; i < base.len; i++) path_buf[i] = base[i];
        for (usz i = 0; i < slen; i++) path_buf[base.len + i] = suffix[i];
        path_buf[base.len + slen] = 0;
        flags = MDB_NOSUBDIR;
    } else if (is_string(args[0])) {
        char[] path = args[0].str_chars[:args[0].str_len];
        usz plen = path.len < 255 ? path.len : 255;
        for (usz i = 0; i < plen; i++) path_buf[i] = path[i];
        path_buf[plen] = 0;
        flags = MDB_NOSUBDIR;
    } else {
        mem::free(db);
        // fault: lisp::TYPE_MISMATCH
        return raise_error(interp, "deduce-open: expected path string or 'memory");
    }

    rc = mdb_env_open(db.env, &path_buf, flags, 0664);
    if (rc != MDB_SUCCESS) {
        mdb_env_close(db.env);
        mem::free(db);
        // fault: lisp::WRITE_FAILED
        return raise_error(interp, "deduce-open: failed to open database");
    }

    db.open = true;

    // Wrap database pointer as scoped FFI handle
    Value* v = make_ffi_handle_ex(interp, (void*)db, "deduce-db", null, true, false);
    if (v == null) {
        mdb_env_close(db.env);
        mem::free(db);
        return raise_error(interp, "deduce-open: out of memory");
    }
    return v;
}

// ============================================================
// (fact! relation val1 val2 ...) → nil
// ============================================================

fn Value* prim_assert_fact(Value*[] args, Env* env, Interp* interp) {
    // fault: lisp::ARITY_MISMATCH
    if (args.len < 1) return raise_error(interp, "fact!: expected (fact! relation val...)");

    // First arg should be a Relation value (FFI_HANDLE wrapping Relation*)
    Value* rel_val = args[0];
    if (rel_val == null || rel_val.tag != FFI_HANDLE) {
        // fault: lisp::TYPE_MISMATCH
        return raise_error(interp, "fact!: first argument must be a relation");
    }
    if (rel_val.ffi_val == null) return raise_error(interp, "fact!: relation handle is closed");
    Relation* rel = (Relation*)rel_val.ffi_val.lib_handle;
    // fault: lisp::TYPE_MISMATCH
    if (rel.db == null || !rel.db.open) return raise_error(interp, "fact!: database not open");

    usz val_count = args.len - 1;
    if (val_count != rel.col_count) {
        // fault: lisp::ARITY_MISMATCH
        return raise_error(interp, "fact!: wrong number of values for relation");
    }

    // Encode tuple
    char[4096] key_buf;  // buffer: C-interop (keep) — binary tuple encoding for mdb_put
    usz? key_len = encode_tuple(args[1..], key_buf[..], 4096);
    if (catch err = key_len) {
        return raise_error(interp, "fact!: tuple too large for buffer");
    }

    MdbVal key;
    key.mv_size = key_len;
    key.mv_data = &key_buf;

    MdbVal data;
    char empty = 0;
    data.mv_size = 0;
    data.mv_data = &empty;

    // Write to LMDB
    MdbTxn* txn = null;
    int rc = mdb_txn_begin(rel.db.env, null, 0, &txn);
    // fault: lisp::WRITE_FAILED
    if (rc != MDB_SUCCESS) return raise_error(interp, "fact!: txn begin failed");

    rc = mdb_put(txn, rel.dbi, &key, &data, 0);
    if (rc != MDB_SUCCESS) {
        mdb_txn_abort(txn);
        // fault: lisp::WRITE_FAILED
        return raise_error(interp, "fact!: put failed");
    }

    mdb_txn_commit(txn);
    return make_nil(interp);
}

// ============================================================
// (retract! relation val1 val2 ...) → nil
// ============================================================

fn Value* prim_retract_fact(Value*[] args, Env* env, Interp* interp) {
    // fault: lisp::ARITY_MISMATCH
    if (args.len < 1) return raise_error(interp, "retract!: expected (retract! relation val...)");

    Value* rel_val = args[0];
    if (rel_val == null || rel_val.tag != FFI_HANDLE) {
        // fault: lisp::TYPE_MISMATCH
        return raise_error(interp, "retract!: first argument must be a relation");
    }
    if (rel_val.ffi_val == null) return raise_error(interp, "retract!: relation handle is closed");
    Relation* rel = (Relation*)rel_val.ffi_val.lib_handle;
    // fault: lisp::TYPE_MISMATCH
    if (rel.db == null || !rel.db.open) return raise_error(interp, "retract!: database not open");

    usz val_count = args.len - 1;
    if (val_count != rel.col_count) {
        // fault: lisp::ARITY_MISMATCH
        return raise_error(interp, "retract!: wrong number of values for relation");
    }

    char[4096] key_buf;  // buffer: C-interop (keep) — binary tuple encoding for mdb_del
    usz? key_len = encode_tuple(args[1..], key_buf[..], 4096);
    if (catch err = key_len) {
        return raise_error(interp, "retract!: tuple too large for buffer");
    }

    MdbVal key;
    key.mv_size = key_len;
    key.mv_data = &key_buf;

    MdbTxn* txn = null;
    int rc = mdb_txn_begin(rel.db.env, null, 0, &txn);
    // fault: lisp::WRITE_FAILED
    if (rc != MDB_SUCCESS) return raise_error(interp, "retract!: txn begin failed");

    rc = mdb_del(txn, rel.dbi, &key, null);
    mdb_txn_commit(txn);
    return make_nil(interp);
}

// ============================================================
// Scan all tuples in a relation → list of dicts
// ============================================================

fn Value* relation_scan_all(Relation* rel, Interp* interp) {
    MdbTxn* txn = null;
    int rc = mdb_txn_begin(rel.db.env, null, MDB_RDONLY, &txn);
    if (rc != MDB_SUCCESS) return make_nil(interp);
    defer mdb_txn_abort(txn);

    MdbCursor* cursor = null;
    rc = mdb_cursor_open(txn, rel.dbi, &cursor);
    if (rc != MDB_SUCCESS) return make_nil(interp);
    defer mdb_cursor_close(cursor);

    Value* result = make_nil(interp);
    Value*[MAX_RELATION_COLS] vals;

    MdbVal key;
    MdbVal data;
    rc = mdb_cursor_get(cursor, &key, &data, MDB_FIRST);
    while (rc == MDB_SUCCESS) {
        // Decode tuple
        usz? count_opt = decode_tuple((char*)key.mv_data, key.mv_size, vals[..rel.col_count], interp);
        if (catch err = count_opt) {
            // Malformed data — skip this entry
            rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT);
            continue;
        }
        if (count_opt == rel.col_count) {
            // Build dict with column names as keys
            Value* dict = make_hashmap(interp, 8);
            for (usz i = 0; i < rel.col_count; i++) {
                Value* k = interp.alloc_value();
                k.tag = SYMBOL;
                k.sym_val = rel.columns[i];
                hashmap_set(dict.hashmap_val, k, vals[i], interp);
            }
            result = make_cons(interp, dict, result);
        }
        rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT);
    }

    return result;
}

// ============================================================
// (deduce-scan relation) → list of dicts (all tuples)
// Temporary query primitive until full Datalog engine
// ============================================================

// ============================================================
// (__define-relation db 'rel-name 'col1 'col2 ...) → Relation value
// Called by parser-generated code from (define [relation db] ...)
// ============================================================

fn Value* prim_define_relation(Value*[] args, Env* env, Interp* interp) {
    // fault: lisp::ARITY_MISMATCH
    if (args.len < 1) return raise_error(interp, "__define-relation: expected (cons db '(name col...))");

    // arg 0: (cons db (name col1 col2 ...))
    Value* pair = args[0];
    // fault: lisp::TYPE_MISMATCH
    if (!is_cons(pair)) return raise_error(interp, "__define-relation: expected cons pair");
    Value* db_val = car(pair);
    if (db_val == null || db_val.tag != FFI_HANDLE) {
        // fault: lisp::TYPE_MISMATCH
        return raise_error(interp, "__define-relation: car must be a deduce-open handle");
    }
    if (db_val.ffi_val == null) return raise_error(interp, "__define-relation: database handle is closed");
    DeduceDb* db = (DeduceDb*)db_val.ffi_val.lib_handle;

    Value* spec = cdr(pair);
    // fault: lisp::TYPE_MISMATCH
    if (!is_cons(spec)) return raise_error(interp, "__define-relation: expected (name col...) in cdr");
    // fault: lisp::EXPECTED_SYMBOL
    if (car(spec).tag != SYMBOL) return raise_error(interp, "__define-relation: first element must be relation name");
    SymbolId rel_name = car(spec).sym_val;

    Relation* rel = (Relation*)mem::malloc(Relation.sizeof);
    rel.name = rel_name;
    rel.db = db;
    rel.col_count = 0;
    rel.key_col = 0;
    rel.index_col = 0;

    Value* cols = cdr(spec);
    while (is_cons(cols) && rel.col_count < MAX_RELATION_COLS) {
        if (car(cols).tag == SYMBOL) {
            rel.columns[rel.col_count++] = car(cols).sym_val;
        }
        cols = cdr(cols);
    }

    // Open LMDB named database for this relation
    char[] name_str = interp.symbols.get_name(rel_name);
    char[64] dbi_name;  // buffer: C-interop (keep) — null-terminated for mdb_dbi_open
    usz nlen = name_str.len < 63 ? name_str.len : 63;
    for (usz i = 0; i < nlen; i++) dbi_name[i] = name_str[i];
    dbi_name[nlen] = 0;

    MdbTxn* txn = null;
    int rc = mdb_txn_begin(db.env, null, 0, &txn);
    if (rc != MDB_SUCCESS) {
        mem::free(rel);
        // fault: lisp::WRITE_FAILED
        return raise_error(interp, "__define-relation: txn begin failed");
    }

    rc = mdb_dbi_open(txn, &dbi_name, MDB_CREATE, &rel.dbi);
    if (rc != MDB_SUCCESS) {
        mdb_txn_abort(txn);
        mem::free(rel);
        // fault: lisp::WRITE_FAILED
        return raise_error(interp, "__define-relation: dbi open failed");
    }

    mdb_txn_commit(txn);

    Value* v = make_ffi_handle_ex(interp, (void*)rel, "deduce-relation", null, true, false);
    if (v == null) {
        mem::free(rel);
        return raise_error(interp, "__define-relation: out of memory");
    }
    return v;
}

// ============================================================
// (deduce-query relation filter-fn) → filtered list of dicts
// filter-fn takes a dict, returns truthy/nil
// ============================================================

fn Value* prim_deduce_query(Value*[] args, Env* env, Interp* interp) {
    // fault: lisp::ARITY_MISMATCH
    if (args.len < 2) return raise_error(interp, "deduce-query: expected (deduce-query relation filter-fn)");

    Value* rel_val = args[0];
    if (rel_val == null || rel_val.tag != FFI_HANDLE) {
        // fault: lisp::TYPE_MISMATCH
        return raise_error(interp, "deduce-query: first argument must be a relation");
    }
    if (rel_val.ffi_val == null) return raise_error(interp, "deduce-query: relation handle is closed");
    Relation* rel = (Relation*)rel_val.ffi_val.lib_handle;

    Value* filter_fn = args[1];

    // Scan all tuples and filter
    Value* all = relation_scan_all(rel, interp);
    Value* result = make_nil(interp);
    Value*[256] matches;
    usz match_count = 0;

    Value* curr = all;
    while (is_cons(curr) && match_count < 256) {
        Value* row = car(curr);
        // Apply filter function to row
        Value* keep = jit_apply_value(filter_fn, row, interp);
        if (keep != null && keep.tag != NIL) {
            matches[match_count++] = row;
        }
        curr = cdr(curr);
    }

    // Build result list in order
    for (isz i = (isz)match_count - 1; i >= 0; i--) {
        result = make_cons(interp, matches[(usz)i], result);
    }
    return result;
}

// ============================================================
// (deduce-count relation) → number of tuples
// ============================================================

fn Value* prim_deduce_count(Value*[] args, Env* env, Interp* interp) {
    // fault: lisp::ARITY_MISMATCH
    if (args.len < 1) return raise_error(interp, "deduce-count: expected (deduce-count relation)");

    Value* rel_val = args[0];
    if (rel_val == null || rel_val.tag != FFI_HANDLE) {
        // fault: lisp::TYPE_MISMATCH
        return raise_error(interp, "deduce-count: first argument must be a relation");
    }
    if (rel_val.ffi_val == null) return raise_error(interp, "deduce-count: relation handle is closed");
    Relation* rel = (Relation*)rel_val.ffi_val.lib_handle;

    MdbTxn* txn = null;
    int rc = mdb_txn_begin(rel.db.env, null, MDB_RDONLY, &txn);
    if (rc != MDB_SUCCESS) return make_int(interp, 0);
    defer mdb_txn_abort(txn);

    MdbCursor* cursor = null;
    rc = mdb_cursor_open(txn, rel.dbi, &cursor);
    if (rc != MDB_SUCCESS) return make_int(interp, 0);
    defer mdb_cursor_close(cursor);

    long count = 0;
    MdbVal key;
    MdbVal data;
    rc = mdb_cursor_get(cursor, &key, &data, MDB_FIRST);
    while (rc == MDB_SUCCESS) {
        count++;
        rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT);
    }

    return make_int(interp, count);
}

fn Value* prim_deduce_scan(Value*[] args, Env* env, Interp* interp) {
    // fault: lisp::ARITY_MISMATCH
    if (args.len < 1) return raise_error(interp, "deduce-scan: expected (deduce-scan relation)");

    Value* rel_val = args[0];
    if (rel_val == null || rel_val.tag != FFI_HANDLE) {
        // fault: lisp::TYPE_MISMATCH
        return raise_error(interp, "deduce-scan: first argument must be a relation");
    }
    if (rel_val.ffi_val == null) return raise_error(interp, "deduce-scan: relation handle is closed");
    Relation* rel = (Relation*)rel_val.ffi_val.lib_handle;

    return relation_scan_all(rel, interp);
}

// ============================================================
// (deduce 'cmd args...) — unified dispatch
//
// (deduce 'open 'memory)           → database handle
// (deduce 'open "/path")           → database handle
// (deduce 'scan relation)          → list of dicts
// (deduce 'query relation fn)      → filtered list of dicts
// (deduce 'count relation)         → int
// (deduce 'match relation pattern) → list of dicts
// (deduce 'fact! relation vals...) → nil
// (deduce 'retract! relation vals...)  → nil
// ============================================================

fn Value* prim_deduce(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return raise_error(interp, "deduce: expected (deduce 'command args...)");

    Value* cmd = args[0];
    if (cmd == null || cmd.tag != SYMBOL) {
        return raise_error(interp, "deduce: first argument must be a quoted symbol");
    }

    char[] name = interp.symbols.get_name(cmd.sym_val);

    // Dispatch on command symbol
    if (name.len == 4 && name[0] == 'o' && name[1] == 'p' && name[2] == 'e' && name[3] == 'n') {
        // (deduce 'open arg)
        return prim_deduce_open(args[1..], env, interp);
    }
    if (name.len == 4 && name[0] == 's' && name[1] == 'c' && name[2] == 'a' && name[3] == 'n') {
        // (deduce 'scan relation)
        return prim_deduce_scan(args[1..], env, interp);
    }
    if (name.len == 5 && name[0] == 'q' && name[1] == 'u' && name[2] == 'e' && name[3] == 'r' && name[4] == 'y') {
        // (deduce 'query relation filter-fn)
        return prim_deduce_query(args[1..], env, interp);
    }
    if (name.len == 5 && name[0] == 'c' && name[1] == 'o' && name[2] == 'u' && name[3] == 'n' && name[4] == 't') {
        // (deduce 'count relation)
        return prim_deduce_count(args[1..], env, interp);
    }
    if (name.len == 5 && name[0] == 'm' && name[1] == 'a' && name[2] == 't' && name[3] == 'c' && name[4] == 'h') {
        // (deduce 'match relation pattern)
        return prim_deduce_match(args[1..], env, interp);
    }
    if (name.len == 5 && name[0] == 'f' && name[1] == 'a' && name[2] == 'c' && name[3] == 't' && name[4] == '!') {
        // (deduce 'fact! relation vals...)
        return prim_assert_fact(args[1..], env, interp);
    }
    if (name.len == 8 && name[0] == 'r' && name[1] == 'e' && name[2] == 't' && name[3] == 'r'
        && name[4] == 'a' && name[5] == 'c' && name[6] == 't' && name[7] == '!') {
        // (deduce 'retract! relation vals...)
        return prim_retract_fact(args[1..], env, interp);
    }

    return raise_error(interp, "deduce: unknown command (expected 'open 'scan 'query 'count 'match 'fact! 'retract!)");
}
