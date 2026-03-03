module lisp;

import std::core::mem;
import std::io;
import main;

// ============================================================
// BearSSL extern declarations — direct calls, no wrapper
// ============================================================

// BearSSL struct sizes (from bearssl.h on x86_64):
//   br_ssl_client_context: 3720 bytes
//   br_x509_minimal_context: 3168 bytes
//   br_sslio_context: 40 bytes
//   br_ssl_engine_context is at offset 0 inside br_ssl_client_context
//   BR_SSL_BUFSIZE_BIDI: 33178 bytes

const usz BEARSSL_CLIENT_CTX_SIZE = 3720;
const usz BEARSSL_X509_CTX_SIZE  = 3168;
const usz BEARSSL_SSLIO_CTX_SIZE = 40;
const usz BEARSSL_IOBUF_SIZE     = 33178;

// Callback type for br_sslio_init: int (*)(void *ctx, unsigned char *buf, size_t len)
alias BrSslioCallback = fn int(void* ctx, char* buf, usz len);

extern fn void br_ssl_client_init_full(void* cc, void* xc, void* tas, usz num_tas) @extern("br_ssl_client_init_full");
extern fn void br_ssl_engine_set_buffer(void* eng, void* iobuf, usz iobuf_len, int bidi) @extern("br_ssl_engine_set_buffer");
extern fn int br_ssl_client_reset(void* cc, char* server_name, int resume_session) @extern("br_ssl_client_reset");
extern fn void br_sslio_init(void* ctx, void* engine, BrSslioCallback low_read, void* read_ctx, BrSslioCallback low_write, void* write_ctx) @extern("br_sslio_init");
extern fn int br_sslio_read(void* ctx, char* buf, usz len) @extern("br_sslio_read");
extern fn int br_sslio_write_all(void* ctx, char* buf, usz len) @extern("br_sslio_write_all");
extern fn int br_sslio_flush(void* ctx) @extern("br_sslio_flush");
extern fn int br_sslio_close(void* ctx) @extern("br_sslio_close");

// Socket I/O callbacks (from csrc/tls_helpers.c — just 2 small C functions)
extern fn int omni_tls_sock_read(void* ctx, char* buf, usz len) @extern("omni_tls_sock_read");
extern fn int omni_tls_sock_write(void* ctx, char* buf, usz len) @extern("omni_tls_sock_write");

// ============================================================
// TLS Handle — all BearSSL state in one allocation
// ============================================================

struct TlsHandle {
    void* client_ctx;   // br_ssl_client_context (malloc'd, 3720 bytes)
    void* x509_ctx;     // br_x509_minimal_context (malloc'd, 3168 bytes)
    void* sslio_ctx;    // br_sslio_context (malloc'd, 40 bytes)
    void* iobuf;        // I/O buffer (malloc'd, 33178 bytes)
    int*  fd_ptr;       // pointer to fd (stable address for callbacks)
    bool  connected;
}

fn Value* make_tls_handle_val(Interp* interp, TlsHandle* th) {
    Value* v = make_ffi_handle_ex(interp, (void*)th, "tls-handle", &tls_handle_finalizer, true, false);
    if (v == null) {
        return raise_error(interp, "tls-connect: out of memory");
    }
    return v;
}

fn TlsHandle* get_tls_handle(Value* v) {
    if (v == null || v.tag != FFI_HANDLE) return null;
    if (v.ffi_val == null) return null;
    return (TlsHandle*)v.ffi_val.lib_handle;
}

fn void tls_handle_free(TlsHandle* th) {
    if (th == null) return;
    if (th.client_ctx != null) { mem::free(th.client_ctx); th.client_ctx = null; }
    if (th.x509_ctx != null) { mem::free(th.x509_ctx); th.x509_ctx = null; }
    if (th.sslio_ctx != null) { mem::free(th.sslio_ctx); th.sslio_ctx = null; }
    if (th.iobuf != null) { mem::free(th.iobuf); th.iobuf = null; }
    if (th.fd_ptr != null) { mem::free(th.fd_ptr); th.fd_ptr = null; }
}

fn void tls_handle_finalizer(void* handle) {
    TlsHandle* th = (TlsHandle*)handle;
    if (th == null) return;
    if (th.connected && th.sslio_ctx != null) {
        br_sslio_close(th.sslio_ctx);
        th.connected = false;
    }
    tls_handle_free(th);
}

// ============================================================
// (tls-connect tcp-handle hostname) → tls-handle
// ============================================================

fn Value* prim_tls_connect(Value*[] args, Env* env, Interp* interp) {
    // fault: lisp::ARITY_MISMATCH
    if (args.len < 2) return raise_error(interp, "tls-connect: expected (tls-connect tcp-handle hostname)");
    // fault: lisp::EXPECTED_STRING
    if (!is_string(args[1])) return raise_error(interp, "tls-connect: hostname must be a string");

    TcpHandle* tcp = get_tcp_handle(args[0]);
    // fault: lisp::TYPE_MISMATCH
    if (tcp == null || !tcp.connected) return raise_error(interp, "tls-connect: invalid TCP handle");

    char[] hostname = args[1].str_chars[:args[1].str_len];

    // Null-terminate hostname
    char[256] host_buf;  // buffer: C-interop (keep) — passed to br_ssl_client_reset
    usz hlen = hostname.len < 255 ? hostname.len : 255;
    for (usz i = 0; i < hlen; i++) host_buf[i] = hostname[i];
    host_buf[hlen] = 0;

    // Allocate BearSSL state
    TlsHandle* th = (TlsHandle*)mem::malloc(TlsHandle.sizeof);
    // fault: lisp::TLS_HANDSHAKE_FAILED
    if (th == null) return raise_error(interp, "tls-connect: out of memory");

    th.client_ctx = mem::malloc(BEARSSL_CLIENT_CTX_SIZE);
    th.x509_ctx = mem::malloc(BEARSSL_X509_CTX_SIZE);
    th.sslio_ctx = mem::malloc(BEARSSL_SSLIO_CTX_SIZE);
    th.iobuf = mem::malloc(BEARSSL_IOBUF_SIZE);
    th.fd_ptr = (int*)mem::malloc(int.sizeof);

    if (th.client_ctx == null || th.x509_ctx == null || th.sslio_ctx == null ||
        th.iobuf == null || th.fd_ptr == null) {
        tls_handle_free(th);
        mem::free(th);
        // fault: lisp::TLS_HANDSHAKE_FAILED
        return raise_error(interp, "tls-connect: out of memory");
    }

    *th.fd_ptr = tcp.fd;
    th.connected = true;

    // Initialize BearSSL: full client init (all cipher suites, no trust anchors for v1)
    br_ssl_client_init_full(th.client_ctx, th.x509_ctx, null, 0);

    // eng is at offset 0 inside client_ctx, so client_ctx IS the engine pointer
    br_ssl_engine_set_buffer(th.client_ctx, th.iobuf, BEARSSL_IOBUF_SIZE, 1);

    br_ssl_client_reset(th.client_ctx, &host_buf, 0);

    // Init I/O wrapper with socket callbacks
    br_sslio_init(th.sslio_ctx, th.client_ctx,
        (BrSslioCallback)&omni_tls_sock_read, th.fd_ptr,
        (BrSslioCallback)&omni_tls_sock_write, th.fd_ptr);

    return make_tls_handle_val(interp, th);
}

// ============================================================
// (tls-read tls-handle [max-bytes]) → string
// ============================================================

fn Value* prim_tls_read(Value*[] args, Env* env, Interp* interp) {
    // fault: lisp::ARITY_MISMATCH
    if (args.len < 1) return raise_error(interp, "tls-read: expected (tls-read handle [max-bytes])");

    TlsHandle* th = get_tls_handle(args[0]);
    // fault: lisp::TYPE_MISMATCH
    if (th == null || !th.connected) return raise_error(interp, "tls-read: invalid or closed handle");

    usz max_bytes = 4096;
    if (args.len >= 2 && is_int(args[1])) {
        max_bytes = (usz)args[1].int_val;
        if (max_bytes > 65536) max_bytes = 65536;
    }

    char* buf = (char*)mem::malloc(max_bytes);
    // fault: lisp::READ_FAILED
    if (buf == null) return raise_error(interp, "tls-read: out of memory");
    defer mem::free(buf);

    int received = br_sslio_read(th.sslio_ctx, buf, max_bytes);
    if (received < 0) {
        // fault: lisp::READ_FAILED
        return raise_error(interp, "tls-read: read failed");
    }
    if (received == 0) {
        return make_string(interp, "");
    }

    Value* result = make_string(interp, buf[:(usz)received]);
    return result;
}

// ============================================================
// (tls-write tls-handle data) → bytes written
// ============================================================

fn Value* prim_tls_write(Value*[] args, Env* env, Interp* interp) {
    // fault: lisp::ARITY_MISMATCH
    if (args.len < 2) return raise_error(interp, "tls-write: expected (tls-write handle data)");
    // fault: lisp::EXPECTED_STRING
    if (!is_string(args[1])) return raise_error(interp, "tls-write: data must be a string");

    TlsHandle* th = get_tls_handle(args[0]);
    // fault: lisp::TYPE_MISMATCH
    if (th == null || !th.connected) return raise_error(interp, "tls-write: invalid or closed handle");

    char[] data = args[1].str_chars[:args[1].str_len];
    int status = br_sslio_write_all(th.sslio_ctx, data.ptr, data.len);
    // fault: lisp::WRITE_FAILED
    if (status < 0) return raise_error(interp, "tls-write: write failed");

    int flush_status = br_sslio_flush(th.sslio_ctx);
    // fault: lisp::WRITE_FAILED
    if (flush_status < 0) return raise_error(interp, "tls-write: flush failed");

    return make_int(interp, (long)data.len);
}

// ============================================================
// (tls-close tls-handle) → nil
// ============================================================

fn Value* prim_tls_close(Value*[] args, Env* env, Interp* interp) {
    // fault: lisp::ARITY_MISMATCH
    if (args.len < 1) return raise_error(interp, "tls-close: expected (tls-close handle)");

    TlsHandle* th = get_tls_handle(args[0]);
    // fault: lisp::TYPE_MISMATCH
    if (th == null) return raise_error(interp, "tls-close: invalid handle");

    if (th.connected) {
        br_sslio_close(th.sslio_ctx);
        tls_handle_free(th);
        th.connected = false;
    }

    return make_nil(interp);
}
