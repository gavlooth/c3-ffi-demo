module lisp;


import std::io;
import main;

// =============================================================================
// REPLXX FFI DECLARATIONS
// =============================================================================
// replxx is a readline replacement with syntax highlighting, hints, and completions.
// replxx_input returns a library-managed pointer (do NOT free it). null on EOF.

// Opaque replxx handle — use void* since C3 requires uppercase for type defs
alias ReplxxHighlighterCb = fn void(char*, CInt*, CInt, void*);
alias ReplxxCompletionCb = fn void(char*, void*, CInt*, void*);

extern fn void* replxx_init() @extern("replxx_init");
extern fn void replxx_end(void* rx) @extern("replxx_end");
extern fn char* replxx_input(void* rx, char* prompt) @extern("replxx_input");
extern fn void replxx_history_add(void* rx, char* line) @extern("replxx_history_add");
extern fn int replxx_history_load(void* rx, char* filename) @extern("replxx_history_load");
extern fn int replxx_history_save(void* rx, char* filename) @extern("replxx_history_save");
extern fn void replxx_set_max_history_size(void* rx, CInt len) @extern("replxx_set_max_history_size");
extern fn void replxx_set_highlighter_callback(void* rx, ReplxxHighlighterCb cb, void* ud) @extern("replxx_set_highlighter_callback");
extern fn void replxx_set_completion_callback(void* rx, ReplxxCompletionCb cb, void* ud) @extern("replxx_set_completion_callback");
extern fn void replxx_add_completion(void* lc, char* str) @extern("replxx_add_completion");
extern fn void replxx_set_word_break_characters(void* rx, char* wb) @extern("replxx_set_word_break_characters");
extern fn void replxx_set_unique_history(void* rx, CInt val) @extern("replxx_set_unique_history");
extern fn int replxx_install_window_change_handler(void* rx) @extern("replxx_install_window_change_handler");
extern fn void replxx_enable_bracketed_paste(void* rx) @extern("replxx_enable_bracketed_paste");
extern fn void replxx_set_indent_multiline(void* rx, CInt val) @extern("replxx_set_indent_multiline");

// Replxx color constants
const CInt RXCOLOR_BLACK         = 0;
const CInt RXCOLOR_RED           = 1;
const CInt RXCOLOR_GREEN         = 2;
const CInt RXCOLOR_BROWN         = 3;
const CInt RXCOLOR_BLUE          = 4;
const CInt RXCOLOR_MAGENTA       = 5;
const CInt RXCOLOR_CYAN          = 6;
const CInt RXCOLOR_LIGHTGRAY     = 7;
const CInt RXCOLOR_GRAY          = 8;
const CInt RXCOLOR_BRIGHTRED     = 9;
const CInt RXCOLOR_BRIGHTGREEN   = 10;
const CInt RXCOLOR_YELLOW        = 11;
const CInt RXCOLOR_BRIGHTBLUE    = 12;
const CInt RXCOLOR_BRIGHTMAGENTA = 13;
const CInt RXCOLOR_BRIGHTCYAN    = 14;
const CInt RXCOLOR_WHITE         = 15;
const CInt RXCOLOR_DEFAULT       = 1 << 16;
const CInt RXCOLOR_BOLD          = 1 << 17;

// Global replxx handle for use in callbacks
void* g_replxx = null;
// Global interp for completion callback
Interp* g_repl_interp = null;


// =============================================================================
// SECTION 9: REPL
// =============================================================================

/**
 * Read a line of input from stdin.
 * Returns the number of characters read (not including null terminator).
 */
fn usz read_line(char[] buffer) {
    usz len = 0;
    usz max_len = buffer.len - 1;
    while (len < max_len) {
        if (try c = io::stdin().read_byte()) {
            if (c == '\n') {
                break;
            }
            buffer[len] = c;
            len++;
        } else {
            break;  // EOF or error
        }
    }
    buffer[len] = 0;  // Null terminate
    return len;
}

// =============================================================================
// REPLXX CALLBACKS — Syntax Highlighting + Completion
// =============================================================================

// Check if a symbol name is a keyword (special form)
fn bool is_keyword(char[] name) {
    // Special forms
    char[][] keywords = {
        "lambda", "define", "let", "if", "begin", "set!", "quote",
        "and", "or", "match", "reset", "shift", "signal", "handle",
        "resolve", "module", "import", "export", "with-continuation"
    };
    foreach (kw : keywords) {
        if (kw.len == name.len) {
            bool eq = true;
            for (usz i = 0; i < kw.len; i++) {
                if (kw[i] != name[i]) { eq = false; break; }
            }
            if (eq) return true;
        }
    }
    return false;
}

// Check if a symbol is a builtin constant
fn bool is_builtin_const(char[] name) {
    char[][] builtins = { "true", "false", "nil", "pi", "e" };
    foreach (b : builtins) {
        if (b.len == name.len) {
            bool eq = true;
            for (usz i = 0; i < b.len; i++) {
                if (b[i] != name[i]) { eq = false; break; }
            }
            if (eq) return true;
        }
    }
    return false;
}

// Helper: classify a symbol name and apply color to the range [start, end) in colors array
fn void flush_symbol(char* input, CInt* colors, isz start, CInt end_pos) {
    if (start < 0 || start >= (isz)end_pos) return;
    usz slen = (usz)end_pos - (usz)start;
    char[128] sbuf;
    if (slen > 127) slen = 127;
    for (usz k = 0; k < slen; k++) sbuf[k] = input[(usz)start + k];
    char[] sym = sbuf[:slen];

    CInt col = RXCOLOR_DEFAULT;
    if (is_keyword(sym)) {
        col = RXCOLOR_BRIGHTCYAN | RXCOLOR_BOLD;
    } else if (is_builtin_const(sym)) {
        col = RXCOLOR_BRIGHTMAGENTA;
    }
    for (isz j = start; j < (isz)end_pos; j++) {
        colors[(CInt)j] = col;
    }
}

struct ReplHighlighterState {
    bool in_string;
    bool in_comment;
    bool escape_next;
    int paren_depth;
    isz sym_start;
}

const CInt[6] REPL_PAREN_COLORS = {
    RXCOLOR_BRIGHTCYAN, RXCOLOR_BRIGHTMAGENTA, RXCOLOR_YELLOW,
    RXCOLOR_BRIGHTGREEN, RXCOLOR_BRIGHTBLUE, RXCOLOR_BRIGHTRED
};

fn bool repl_highlighter_is_whitespace(char c) @inline {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

fn bool repl_highlighter_is_quote_prefix(char* input, CInt i) @inline {
    return i == 0 || input[i - 1] == '(' || input[i - 1] == ' ' || input[i - 1] == '\t';
}

fn void repl_highlighter_flush_symbol(char* input, CInt* colors, ReplHighlighterState* state, CInt pos) @inline {
    flush_symbol(input, colors, state.sym_start, pos);
    state.sym_start = -1;
}

fn bool repl_highlighter_step_comment(char c, CInt* colors, CInt i, ReplHighlighterState* state) @inline {
    if (!state.in_comment) return false;
    colors[i] = RXCOLOR_GRAY;
    if (c == '\n') {
        state.in_comment = false;
    }
    return true;
}

fn bool repl_highlighter_step_string(char c, CInt* colors, CInt i, ReplHighlighterState* state) @inline {
    if (!state.in_string) return false;
    colors[i] = RXCOLOR_GREEN;
    if (state.escape_next) {
        state.escape_next = false;
        return true;
    }
    if (c == '\\') {
        state.escape_next = true;
        return true;
    }
    if (c == '"') {
        state.in_string = false;
    }
    return true;
}

fn void repl_highlighter_color_paren(CInt* colors, CInt i, char c, ReplHighlighterState* state) @inline {
    if (c == '(') {
        colors[i] = REPL_PAREN_COLORS[state.paren_depth % 6];
        state.paren_depth++;
        return;
    }
    state.paren_depth--;
    if (state.paren_depth < 0) {
        state.paren_depth = 0;
    }
    colors[i] = REPL_PAREN_COLORS[state.paren_depth % 6];
}

fn void repl_highlighter_step_normal(
    char* input,
    CInt* colors,
    CInt i,
    char c,
    ReplHighlighterState* state
) {
    if (c == ';') {
        repl_highlighter_flush_symbol(input, colors, state, i);
        state.in_comment = true;
        colors[i] = RXCOLOR_GRAY;
        return;
    }
    if (c == '"') {
        repl_highlighter_flush_symbol(input, colors, state, i);
        state.in_string = true;
        colors[i] = RXCOLOR_GREEN;
        return;
    }
    if (c == '(' || c == ')') {
        repl_highlighter_flush_symbol(input, colors, state, i);
        repl_highlighter_color_paren(colors, i, c, state);
        return;
    }
    if (c == '[' || c == ']') {
        repl_highlighter_flush_symbol(input, colors, state, i);
        colors[i] = RXCOLOR_YELLOW;
        return;
    }
    if (c == '{' || c == '}') {
        repl_highlighter_flush_symbol(input, colors, state, i);
        colors[i] = RXCOLOR_BROWN;
        return;
    }
    if (c == '\'' && repl_highlighter_is_quote_prefix(input, i)) {
        colors[i] = RXCOLOR_CYAN;
        return;
    }
    if (c >= '0' && c <= '9' && state.sym_start < 0) {
        colors[i] = RXCOLOR_BRIGHTMAGENTA;
        return;
    }
    if (repl_highlighter_is_whitespace(c)) {
        repl_highlighter_flush_symbol(input, colors, state, i);
        return;
    }
    if (state.sym_start < 0) {
        state.sym_start = (isz)i;
    }
}

// Syntax highlighter callback for replxx — colors individual code points
fn void lisp_highlighter(char* input, CInt* colors, CInt size, void* ud) {
    if (input == null || size <= 0) return;

    ReplHighlighterState state = {
        .in_string = false,
        .in_comment = false,
        .escape_next = false,
        .paren_depth = 0,
        .sym_start = -1,
    };

    for (CInt i = 0; i < size; i++) {
        char c = input[i];
        if (repl_highlighter_step_comment(c, colors, i, &state)) continue;
        if (repl_highlighter_step_string(c, colors, i, &state)) continue;
        repl_highlighter_step_normal(input, colors, i, c, &state);
    }

    // Flush trailing symbol
    flush_symbol(input, colors, state.sym_start, size);
}

fn usz repl_cstr_len_ptr(char* s) @inline {
    usz len = 0;
    while (s[len] != 0) {
        len++;
    }
    return len;
}

fn bool repl_is_completion_word_break(char c) @inline {
    return c == ' ' || c == '\t' || c == '(' || c == ')' ||
        c == '[' || c == ']' || c == '{' || c == '}' ||
        c == '\'' || c == '"';
}

fn isz repl_completion_find_word_start(char* input, usz len) {
    if (len == 0) return 0;
    isz word_start = (isz)len - 1;
    while (word_start >= 0) {
        char c = input[word_start];
        if (repl_is_completion_word_break(c)) {
            return word_start + 1;
        }
        if (word_start == 0) {
            break;
        }
        word_start--;
    }
    if (word_start < 0) return 0;
    return word_start;
}

fn bool repl_completion_matches_prefix(char[] name, char* prefix, usz prefix_len) @inline {
    if (name.len < prefix_len) return false;
    for (usz i = 0; i < prefix_len; i++) {
        if (name[i] != prefix[i]) return false;
    }
    return true;
}

fn void repl_completion_emit(void* completions, char[] name) {
    char[256] nbuf;
    usz copy = name.len;
    if (copy > 255) {
        copy = 255;
    }
    for (usz i = 0; i < copy; i++) {
        nbuf[i] = name[i];
    }
    nbuf[copy] = 0;
    replxx_add_completion(completions, &nbuf[0]);
}

// Completion callback — complete defined symbols
fn void lisp_completion(char* input, void* completions, CInt* context_len, void* ud) {
    if (input == null || g_repl_interp == null) return;

    usz len = repl_cstr_len_ptr(input);
    if (len == 0) return;

    isz word_start = repl_completion_find_word_start(input, len);

    usz prefix_len = len - (usz)word_start;
    if (prefix_len == 0) return;
    *context_len = (CInt)prefix_len;
    char* prefix = &input[word_start];

    // Search global env bindings
    Env* env = g_repl_interp.global_env;
    for (usz i = 0; i < env.binding_count; i++) {
        char[] name = g_repl_interp.symbols.get_name(env.bindings[i].name);
        if (repl_completion_matches_prefix(name, prefix, prefix_len)) {
            repl_completion_emit(completions, name);
        }
    }
}

/**
 * Read-Eval-Print-Loop.
 */
// Count net parenthesis depth in a string, skipping chars inside "..." strings
// and ; line comments. Returns the net depth (opens - closes).
struct ReplParenDepthState {
    int depth;
    bool in_string;
    bool in_comment;
    bool escape_next;
}

fn bool repl_paren_depth_step_string(char c, ReplParenDepthState* state) @inline {
    if (!state.in_string) return false;
    if (state.escape_next) {
        state.escape_next = false;
        return true;
    }
    if (c == '\\') {
        state.escape_next = true;
        return true;
    }
    if (c == '"') {
        state.in_string = false;
    }
    return true;
}

fn bool repl_paren_depth_step_comment(char c, ReplParenDepthState* state) @inline {
    if (!state.in_comment) return false;
    if (c == '\n') {
        state.in_comment = false;
    }
    return true;
}

fn void repl_paren_depth_step_normal(char c, ReplParenDepthState* state) @inline {
    if (c == ';') {
        state.in_comment = true;
        return;
    }
    if (c == '"') {
        state.in_string = true;
        return;
    }
    if (c == '(') {
        state.depth++;
        return;
    }
    if (c == ')') {
        state.depth--;
    }
}

fn int count_paren_depth(char[] input) {
    ReplParenDepthState state = {
        .depth = 0,
        .in_string = false,
        .in_comment = false,
        .escape_next = false,
    };

    for (usz i = 0; i < input.len; i++) {
        char c = input[i];
        if (repl_paren_depth_step_string(c, &state)) continue;
        if (repl_paren_depth_step_comment(c, &state)) continue;
        repl_paren_depth_step_normal(c, &state);
    }
    return state.depth;
}

fn usz repl_cstr_len(char* line) @inline {
    usz len = 0;
    while (line[len] != 0) len++;
    return len;
}

fn bool repl_is_quit_command(char* line, usz len) @inline {
    if (len >= 4 &&
        line[0] == 'q' && line[1] == 'u' &&
        line[2] == 'i' && line[3] == 't') {
        return true;
    }
    if (len >= 4 &&
        line[0] == 'e' && line[1] == 'x' &&
        line[2] == 'i' && line[3] == 't') {
        return true;
    }
    return false;
}

fn void repl_append_line(char[8192]* buf, usz* buf_len, char* line, usz len) {
    if (*buf_len > 0 && *buf_len < 8191) {
        (*buf)[*buf_len] = '\n';
        (*buf_len)++;
    }
    usz copy_len = len;
    if (*buf_len + copy_len > 8191) {
        copy_len = 8191 - *buf_len;
    }
    for (usz i = 0; i < copy_len; i++) {
        (*buf)[*buf_len + i] = line[i];
    }
    *buf_len += copy_len;
}

fn EvalResult repl_eval_line(char[] input, Interp* interp) {
    main::ScopeRegion* saved_scope = null;
    main::ScopeRegion* repl_child_scope = boundary_push_child_scope(interp, &saved_scope);
    if (repl_child_scope == null) {
        return eval_error("repl: failed to create child scope");
    }
    defer boundary_pop_child_scope(interp, saved_scope, repl_child_scope);

    EvalResult r = run(input, interp);
    if (!r.error.has_error && r.value != null) {
        r.value = boundary_copy_to_parent_site(r.value, interp, COPY_SITE_REPL_LINE_RESULT);
    }
    return r;
}

fn void repl_print_eval_outcome(EvalResult r, Interp* interp, char[] ansi_red, char[] ansi_green, char[] ansi_reset) {
    if (r.error.has_error) {
        io::print(ansi_red);
        if (r.error.line > 0) {
            io::printf("Error at line %d, column %d: ", (int)r.error.line, (int)r.error.column);
        } else {
            io::print("Error: ");
        }
        io::printf("%s", (ZString)&r.error.message);
        io::print(ansi_reset);
        io::printn("");
        return;
    }

    io::print(ansi_green);
    print_value(r.value, &interp.symbols);
    io::print(ansi_reset);
    io::printn("");
}

fn void repl_configure(void* rx, char* history_file) {
    replxx_set_max_history_size(rx, 1000);
    replxx_set_unique_history(rx, 1);
    replxx_install_window_change_handler(rx);
    replxx_set_word_break_characters(rx, " \t\n()[]{}';\"");
    replxx_set_indent_multiline(rx, 1);
    replxx_set_highlighter_callback(rx, &lisp_highlighter, null);
    replxx_set_completion_callback(rx, &lisp_completion, null);
    replxx_history_load(rx, history_file);
}

fn char* repl_prompt_for(usz buf_len) @inline {
    if (buf_len == 0) return "\x1b[1;34momni>\x1b[0m ";
    return "\x1b[1;34m ....\x1b[0m ";
}

fn bool repl_handle_eof(usz* buf_len) {
    if (*buf_len > 0) {
        io::printn("");
        *buf_len = 0;
        return false;
    }
    io::printn("\nGoodbye!");
    return true;
}

enum ReplLineAction : char {
    REPL_LINE_SKIP,
    REPL_LINE_QUIT,
    REPL_LINE_APPEND,
}

fn ReplLineAction repl_classify_line(char* line, usz len, usz* buf_len) {
    if (*buf_len > 0 && len == 0) {
        io::printn("Input cancelled.");
        *buf_len = 0;
        return REPL_LINE_SKIP;
    }
    if (*buf_len == 0 && len == 0) {
        return REPL_LINE_SKIP;
    }
    if (*buf_len == 0 && repl_is_quit_command(line, len)) {
        io::printn("Goodbye!");
        return REPL_LINE_QUIT;
    }
    return REPL_LINE_APPEND;
}

fn void repl_eval_buffer(void* rx, char[8192]* buf, usz* buf_len, Interp* interp, char[] ansi_red, char[] ansi_green, char[] ansi_reset) {
    char[] accumulated = (*buf)[:*buf_len];
    int depth = count_paren_depth(accumulated);
    if (depth > 0) return;

    (*buf)[*buf_len] = 0;
    replxx_history_add(rx, &(*buf)[0]);
    jit_gc();

    char[] input = (*buf)[:*buf_len];
    g_interrupted = false;
    EvalResult r = repl_eval_line(input, interp);
    repl_print_eval_outcome(r, interp, ansi_red, ansi_green, ansi_reset);
    *buf_len = 0;
}

fn bool repl_init_session(Interp* interp, void** rx_out, char* history_file) {
    void* rx = replxx_init();
    if (rx == null) {
        io::eprintn("Failed to initialize replxx");
        return false;
    }
    g_replxx = rx;
    g_repl_interp = interp;
    repl_configure(rx, history_file);
    *rx_out = rx;
    return true;
}

fn void repl_shutdown_session(void* rx, char* history_file) {
    replxx_history_save(rx, history_file);
    replxx_end(rx);
    g_replxx = null;
    g_repl_interp = null;
}

fn void repl_print_banner() {
    io::printn("Omni Lisp REPL (type 'quit' or 'exit' to leave, Ctrl-D for EOF)");
    io::printn("---");
}

fn bool repl_step(
    void* rx,
    Interp* interp,
    char[8192]* buf,
    usz* buf_len,
    char[] ansi_red,
    char[] ansi_green,
    char[] ansi_reset
) {
    char* line = (char*)replxx_input(rx, repl_prompt_for(*buf_len));
    if (line == null) {
        if (repl_handle_eof(buf_len)) return false;
        return true;
    }

    usz len = repl_cstr_len(line);
    ReplLineAction action = repl_classify_line(line, len, buf_len);
    if (action == REPL_LINE_SKIP) return true;
    if (action == REPL_LINE_QUIT) return false;

    repl_append_line(buf, buf_len, line, len);
    repl_eval_buffer(rx, buf, buf_len, interp, ansi_red, ansi_green, ansi_reset);
    return true;
}

fn void repl(Interp* interp) {
    // Install SIGINT handler so Ctrl+C interrupts eval instead of killing process
    signal(SIGINT_VAL, &sigint_handler);

    char* history_file = ".omni_history";
    void* rx = null;
    if (!repl_init_session(interp, &rx, history_file)) {
        return;
    }
    defer {
        repl_shutdown_session(rx, history_file);
    }

    // ANSI color codes for output
    char[] ansi_red     = "\x1b[31m";
    char[] ansi_green   = "\x1b[32m";
    char[] ansi_reset   = "\x1b[0m";

    repl_print_banner();

    // Buffer for accumulating multi-line input
    char[8192] buf;
    usz buf_len = 0;

    while (repl_step(rx, interp, &buf, &buf_len, ansi_red, ansi_green, ansi_reset)) {}
}
