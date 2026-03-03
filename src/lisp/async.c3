module lisp;

import std::core::mem;
import std::io;
import main;

// ============================================================
// POSIX/libuv extern declarations — no C wrapper needed
// ============================================================

// POSIX sockets (blocking fast-path for TCP)
extern fn int c_socket(int domain, int type, int protocol) @extern("socket");
extern fn int c_connect(int sockfd, void* addr, uint addrlen) @extern("connect");
extern fn long c_send(int sockfd, void* buf, usz len, int flags) @extern("send");
extern fn long c_recv(int sockfd, void* buf, usz len, int flags) @extern("recv");
extern fn int c_close_fd(int fd) @extern("close");
extern fn int c_getaddrinfo(char* node, char* service, void* hints, void** res) @extern("getaddrinfo");
extern fn void c_freeaddrinfo(void* res) @extern("freeaddrinfo");
extern fn char* c_inet_ntop(int af, void* src, char* dst, uint size) @extern("inet_ntop");

// libuv — linked for future async scheduler use
extern fn int uv_loop_init(void* loop) @extern("uv_loop_init");
extern fn int uv_run(void* loop, int mode) @extern("uv_run");
extern fn int uv_loop_close(void* loop) @extern("uv_loop_close");
extern fn usz uv_loop_size() @extern("uv_loop_size");
extern fn usz uv_handle_size(int handle_type) @extern("uv_handle_size");
alias UvPollCb = fn void(void* handle, int status, int events);
alias UvCloseCb = fn void(void* handle);
alias UvTimerCb = fn void(void* handle);
extern fn int uv_poll_init(void* loop, void* handle, int fd) @extern("uv_poll_init");
extern fn int uv_poll_start(void* handle, int events, UvPollCb cb) @extern("uv_poll_start");
extern fn int uv_poll_stop(void* handle) @extern("uv_poll_stop");
extern fn int uv_timer_init(void* loop, void* handle) @extern("uv_timer_init");
extern fn int uv_timer_start(void* handle, UvTimerCb cb, ulong timeout, ulong repeat) @extern("uv_timer_start");
extern fn int uv_timer_stop(void* handle) @extern("uv_timer_stop");
extern fn void uv_close(void* handle, UvCloseCb cb) @extern("uv_close");
extern fn int uv_loop_alive(void* loop) @extern("uv_loop_alive");
const int UV_HANDLE_POLL = 8;
const int UV_HANDLE_TIMER = 13;
const int UV_READABLE = 1;
const int UV_RUN_DEFAULT = 0;
const int UV_RUN_ONCE = 1;
const int UV_RUN_NOWAIT = 2;

// errno helpers (Linux/glibc)
extern fn int* c_errno_location() @extern("__errno_location");
const int ERRNO_EAGAIN = 11;
const int ERRNO_EWOULDBLOCK = 11;
const int MSG_DONTWAIT = 0x40;

// sockaddr_in layout (for IPv4)
struct SockaddrIn {
    ushort sin_family;
    ushort sin_port;
    uint   sin_addr;
    char[8] sin_zero;  // buffer: C-interop (keep) — POSIX sockaddr_in struct padding
}

// addrinfo result — we only need ai_family, ai_addrlen, ai_addr, ai_next
// offsets vary by platform, so we read fields via helpers
const int AF_INET = 2;
const int SOCK_STREAM = 1;
const int IPPROTO_TCP = 6;

// htons
fn ushort htons(ushort val) {
    return (ushort)(((val & 0xFF) << 8) | ((val >> 8) & 0xFF));
}

// ============================================================
// TCP Handle — wraps a file descriptor
// ============================================================

struct TcpHandle {
    int fd;
    bool connected;
}

fn Value* make_tcp_handle(Interp* interp, int fd) {
    // Allocate in root_scope — connections are long-lived
    main::ScopeRegion* saved = interp.current_scope;
    interp.current_scope = interp.root_scope;
    Value* v = interp.alloc_value();
    main::scope_register_dtor(interp.root_scope, (void*)v, &scope_dtor_value);
    interp.current_scope = saved;

    v.tag = FFI_HANDLE;
    TcpHandle* th = (TcpHandle*)mem::malloc(TcpHandle.sizeof);
    th.fd = fd;
    th.connected = true;
    v.ffi_val = (FfiHandle*)th;
    return v;
}

fn TcpHandle* get_tcp_handle(Value* v) {
    if (v == null || v.tag != FFI_HANDLE) return null;
    return (TcpHandle*)v.ffi_val;
}

// ============================================================
// (tcp-connect host port) — blocking connect, returns handle
// ============================================================

fn Value* prim_tcp_connect(Value*[] args, Env* env, Interp* interp) {
    // fault: lisp::ARITY_MISMATCH
    if (args.len < 2) return raise_error(interp, "tcp-connect: expected (tcp-connect host port)");
    // fault: lisp::EXPECTED_STRING
    if (!is_string(args[0])) return raise_error(interp, "tcp-connect: host must be a string");
    // fault: lisp::EXPECTED_INT
    if (!is_int(args[1])) return raise_error(interp, "tcp-connect: port must be an integer");

    char[] host = args[0].str_chars[:args[0].str_len];
    int port = (int)args[1].int_val;

    // Null-terminate host for C
    char[256] host_buf;  // buffer: C-interop (keep) — null-terminated for c_getaddrinfo
    usz hlen = host.len < 255 ? host.len : 255;
    for (usz i = 0; i < hlen; i++) host_buf[i] = host[i];
    host_buf[hlen] = 0;

    // Null-terminate port
    char[8] port_buf;  // buffer: C-interop (keep) — null-terminated for c_getaddrinfo
    int plen = io::bprintf(&port_buf, "%d", port)!!.len;
    port_buf[plen] = 0;

    // Resolve hostname via getaddrinfo
    void* result = null;
    int status = c_getaddrinfo(&host_buf, &port_buf, null, &result);
    if (status != 0 || result == null) {
        // fault: lisp::DNS_FAILED
        return raise_error(interp, "tcp-connect: DNS resolution failed");
    }

    // struct addrinfo offsets (x86_64 glibc): flags=0, family=4, socktype=8, protocol=12, addrlen=16, addr=24
    int ai_family = *((int*)((char*)result + 4));
    int ai_socktype = *((int*)((char*)result + 8));
    int ai_protocol = *((int*)((char*)result + 12));
    uint ai_addrlen = *((uint*)((char*)result + 16));
    void* ai_addr = *((void**)((char*)result + 24));

    // Create socket
    int fd = c_socket(ai_family, ai_socktype, ai_protocol);
    if (fd < 0) {
        c_freeaddrinfo(result);
        // fault: lisp::CONNECTION_REFUSED
        return raise_error(interp, "tcp-connect: socket creation failed");
    }

    // Connect (blocking)
    int conn_status = c_connect(fd, ai_addr, ai_addrlen);
    c_freeaddrinfo(result);

    if (conn_status < 0) {
        c_close_fd(fd);
        // fault: lisp::CONNECTION_REFUSED
        return raise_error(interp, "tcp-connect: connection failed");
    }

    return make_tcp_handle(interp, fd);
}

// ============================================================
// (tcp-write handle data) — blocking write, returns bytes written
// ============================================================

fn Value* prim_tcp_write(Value*[] args, Env* env, Interp* interp) {
    // fault: lisp::ARITY_MISMATCH
    if (args.len < 2) return raise_error(interp, "tcp-write: expected (tcp-write handle data)");
    // fault: lisp::EXPECTED_STRING
    if (!is_string(args[1])) return raise_error(interp, "tcp-write: data must be a string");

    TcpHandle* th = get_tcp_handle(args[0]);
    // fault: lisp::TYPE_MISMATCH
    if (th == null || !th.connected) return raise_error(interp, "tcp-write: invalid or closed handle");

    char[] data = args[1].str_chars[:args[1].str_len];
    // fault: lisp::WRITE_FAILED
    long sent = c_send(th.fd, data.ptr, data.len, 0);
    if (sent < 0) return raise_error(interp, "tcp-write: send failed");

    return make_int(interp, sent);
}

// ============================================================
// (tcp-read handle [max-bytes]) — blocking read, returns string
// ============================================================

fn Value* prim_tcp_read(Value*[] args, Env* env, Interp* interp) {
    // fault: lisp::ARITY_MISMATCH
    if (args.len < 1) return raise_error(interp, "tcp-read: expected (tcp-read handle [max-bytes])");

    TcpHandle* th = get_tcp_handle(args[0]);
    // fault: lisp::TYPE_MISMATCH
    if (th == null || !th.connected) return raise_error(interp, "tcp-read: invalid or closed handle");

    usz max_bytes = 4096;
    if (args.len >= 2 && is_int(args[1])) {
        max_bytes = (usz)args[1].int_val;
        if (max_bytes > 65536) max_bytes = 65536;
    }
    long timeout_ms = -1;
    if (args.len >= 3 && is_int(args[2])) {
        timeout_ms = args[2].int_val;
        if (timeout_ms < 0) timeout_ms = -1;
    }

    // Async bridge: when called from a scheduler-managed fiber, register a non-blocking
    // read with libuv and suspend this fiber until data is ready.
    Value* async_result = scheduler_try_async_tcp_read(th.fd, max_bytes, timeout_ms, interp);
    if (async_result != null) return async_result;

    char* buf = (char*)mem::malloc(max_bytes);
    // fault: lisp::READ_FAILED
    if (buf == null) return raise_error(interp, "tcp-read: out of memory");
    defer mem::free(buf);

    long received = c_recv(th.fd, buf, max_bytes, 0);
    if (received < 0) {
        // fault: lisp::READ_FAILED
        return raise_error(interp, "tcp-read: recv failed");
    }
    if (received == 0) {
        return make_string(interp, "");  // EOF
    }

    Value* result = make_string(interp, buf[:(usz)received]);
    return result;
}

// ============================================================
// (tcp-read-timeout handle timeout-ms) — read with timeout
// ============================================================

fn Value* prim_tcp_read_timeout(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return raise_error(interp, "tcp-read-timeout: expected (tcp-read-timeout handle timeout-ms)");
    if (!is_int(args[1])) return raise_error(interp, "tcp-read-timeout: timeout-ms must be an integer");

    Value*[3] read_args;
    read_args[0] = args[0];             // handle
    read_args[1] = make_int(interp, 4096); // default max-bytes
    read_args[2] = args[1];             // timeout-ms
    return prim_tcp_read(read_args[..], env, interp);
}

// ============================================================
// (tcp-close handle) — close connection
// ============================================================

fn Value* prim_tcp_close(Value*[] args, Env* env, Interp* interp) {
    // fault: lisp::ARITY_MISMATCH
    if (args.len < 1) return raise_error(interp, "tcp-close: expected (tcp-close handle)");

    TcpHandle* th = get_tcp_handle(args[0]);
    // fault: lisp::TYPE_MISMATCH
    if (th == null) return raise_error(interp, "tcp-close: invalid handle");

    if (th.connected) {
        c_close_fd(th.fd);
        th.connected = false;
    }

    return make_nil(interp);
}

// ============================================================
// (dns-resolve hostname) — resolve to IP string
// ============================================================

fn Value* prim_dns_resolve(Value*[] args, Env* env, Interp* interp) {
    // fault: lisp::ARITY_MISMATCH
    if (args.len < 1) return raise_error(interp, "dns-resolve: expected (dns-resolve hostname)");
    // fault: lisp::EXPECTED_STRING
    if (!is_string(args[0])) return raise_error(interp, "dns-resolve: hostname must be a string");

    char[] host = args[0].str_chars[:args[0].str_len];

    char[256] host_buf;  // buffer: C-interop (keep) — null-terminated for c_getaddrinfo
    usz hlen = host.len < 255 ? host.len : 255;
    for (usz i = 0; i < hlen; i++) host_buf[i] = host[i];
    host_buf[hlen] = 0;

    void* result = null;
    int status = c_getaddrinfo(&host_buf, null, null, &result);
    if (status != 0 || result == null) {
        // fault: lisp::DNS_FAILED
        return raise_error(interp, "dns-resolve: resolution failed");
    }

    // struct addrinfo offsets (x86_64 glibc): flags=0, family=4, socktype=8, protocol=12, addrlen=16, addr=24
    int ai_family = *((int*)((char*)result + 4));
    void* ai_addr = *((void**)((char*)result + 24));

    char[64] ip_buf;  // buffer: C-interop (keep) — output buffer for c_inet_ntop
    if (ai_family == AF_INET) {
        // sockaddr_in: sin_addr is at offset 4 (after sin_family + sin_port)
        void* sin_addr = (char*)ai_addr + 4;
        c_inet_ntop(AF_INET, sin_addr, &ip_buf, 64);
    } else {
        c_freeaddrinfo(result);
        // fault: lisp::DNS_FAILED
        return raise_error(interp, "dns-resolve: IPv6 not yet supported");
    }

    c_freeaddrinfo(result);

    // Find string length
    usz ip_len = 0;
    while (ip_buf[ip_len] != 0 && ip_len < 63) ip_len++;

    return make_string(interp, ip_buf[:ip_len]);
}

// ============================================================
// (async-sleep ms) — blocking sleep (milliseconds)
// ============================================================

fn Value* prim_async_sleep(Value*[] args, Env* env, Interp* interp) {
    // fault: lisp::ARITY_MISMATCH
    if (args.len < 1) return raise_error(interp, "async-sleep: expected (async-sleep ms)");
    // fault: lisp::EXPECTED_INT
    if (!is_int(args[0])) return raise_error(interp, "async-sleep: ms must be an integer");

    long ms = args[0].int_val;
    if (ms > 0) {
        c_usleep((uint)(ms * 1000));
    }

    return make_nil(interp);
}
