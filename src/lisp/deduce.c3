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
extern fn int c_getpid() @extern("getpid");

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

const char[] DEDUCE_HEX_DIGITS = "0123456789abcdef";
usz g_deduce_memory_open_seq = 0;

fn usz deduce_format_usz_hex(char[] out, usz value) {
    if (out.len == 0) return 0;
    if (value == 0) {
        out[0] = '0';
        return 1;
    }

    char[2 * usz.sizeof] rev;
    usz count = 0;
    usz cur = value;
    while (cur != 0 && count < rev.len) {
        usz nibble = cur & 0xF;
        rev[count] = DEDUCE_HEX_DIGITS[nibble];
        count++;
        cur >>= 4;
    }

    for (usz i = 0; i < count; i++) {
        out[i] = rev[count - 1 - i];
    }
    return count;
}

fn bool deduce_append_slice(char[256]* out, usz* idx, char[] src) {
    if (*idx + src.len >= out.len) return false;
    for (usz i = 0; i < src.len; i++) {
        (*out)[*idx + i] = src[i];
    }
    *idx += src.len;
    return true;
}

fn bool deduce_append_hex(char[256]* out, usz* idx, usz value) {
    char[2 * usz.sizeof] hex_buf;
    usz hlen = deduce_format_usz_hex(&hex_buf, value);
    if (*idx + hlen >= out.len) return false;
    for (usz i = 0; i < hlen; i++) {
        (*out)[*idx + i] = hex_buf[i];
    }
    *idx += hlen;
    return true;
}

fn bool deduce_build_memory_path(char[256]* out, DeduceDb* db) {
    if (out == null || db == null) return false;
    usz idx = 0;
    if (!deduce_append_slice(out, &idx, "/tmp/deduce-")) return false;
    if (!deduce_append_hex(out, &idx, (usz)c_getpid())) return false;
    if (idx + 1 >= out.len) return false;
    (*out)[idx++] = '-';
    g_deduce_memory_open_seq++;
    if (!deduce_append_hex(out, &idx, g_deduce_memory_open_seq)) return false;
    if (idx + 1 >= out.len) return false;
    (*out)[idx++] = '-';
    if (!deduce_append_hex(out, &idx, (usz)db)) return false;
    if (idx >= out.len) return false;
    (*out)[idx] = 0;
    return true;
}

fn void deduce_db_finalizer(void* handle) {
    if (handle == null) return;
    DeduceDb* db = (DeduceDb*)handle;
    if (db.env != null && db.open) {
        mdb_env_close(db.env);
        db.env = null;
        db.open = false;
    }
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
        if (!deduce_build_memory_path(&path_buf, db)) {
            mdb_env_close(db.env);
            mem::free(db);
            return raise_error(interp, "deduce-open: failed to build memory path");
        }
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
    Value* v = make_ffi_handle_ex(interp, (void*)db, "deduce-db",
        &deduce_db_finalizer, true, false);
    if (v == null) {
        mdb_env_close(db.env);
        mem::free(db);
        return raise_error(interp, "deduce-open: out of memory");
    }
    return v;
}

// ============================================================
// Dispatch helpers
// ============================================================
fn bool deduce_cmd_matches(char[] name, char[] cmd) {
    if (name.len != cmd.len) return false;
    for (usz i = 0; i < name.len; i++) {
        if (name[i] != cmd[i]) return false;
    }
    return true;
}

fn Value* prim_deduce_dispatch(char[] name, Value*[] rest, Env* env, Interp* interp) {
    if (deduce_cmd_matches(name, "open")) return prim_deduce_open(rest, env, interp);
    if (deduce_cmd_matches(name, "scan")) return prim_deduce_scan(rest, env, interp);
    if (deduce_cmd_matches(name, "query")) return prim_deduce_query(rest, env, interp);
    if (deduce_cmd_matches(name, "count")) return prim_deduce_count(rest, env, interp);
    if (deduce_cmd_matches(name, "match")) return prim_deduce_match(rest, env, interp);
    if (deduce_cmd_matches(name, "fact!")) return prim_assert_fact(rest, env, interp);
    if (deduce_cmd_matches(name, "retract!")) return prim_retract_fact(rest, env, interp);
    return null;
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

    Value* out = prim_deduce_dispatch(name, args[1..], env, interp);
    if (out != null) return out;

    return raise_error(interp, "deduce: unknown command (expected 'open 'scan 'query 'count 'match 'fact! 'retract!)");
}
