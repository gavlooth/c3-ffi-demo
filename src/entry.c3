module main;

import std::io;
import lisp;

extern fn int system(char* command) @extern("system");
extern fn char* c_getenv(char* name) @extern("getenv");

fn bool mem_stats_enabled() {
    char* raw = c_getenv("OMNI_MEM_STATS");
    return raw != null && raw[0] != 0 && raw[0] != '0';
}

fn usz cstr_len(char* s) @inline {
    usz n = 0;
    while (s[n] != 0) n++;
    return n;
}

fn int find_flag_index(int argc, char** argv, char[] f1, char[] f2 = "", char[] f3 = "") {
    for (int i = 1; i < argc; i++) {
        if (str_eq(argv[i], f1)) return i;
        if (f2.len > 0 && str_eq(argv[i], f2)) return i;
        if (f3.len > 0 && str_eq(argv[i], f3)) return i;
    }
    return -1;
}

const char[] AOT_BUILD_PREFIX = "env -u LD_LIBRARY_PATH c3c compile "
    "src/main.c3 src/scope_region.c3 src/stack_engine.c3 "
    "src/lisp/aot.c3 src/lisp/value.c3 src/lisp/eval.c3 src/lisp/parser.c3 "
    "src/lisp/jit.c3 src/lisp/primitives.c3 src/lisp/macros.c3 src/lisp/compiler.c3 "
    "src/lisp/utf8.c3 "
    "build/_aot_temp.c3 -l omni_chelpers -l lightning -l ffi -l dl -l m -L build -L /usr/local/lib -o ";

fn char* find_build_output_arg(int argc, char** argv, int build_idx) {
    for (int i = build_idx + 2; i < argc; i++) {
        if (str_eq(argv[i], "-o") && i + 1 < argc) {
            return argv[i + 1];
        }
    }
    return null;
}

fn char* derive_default_output_name(char* input_file, char[] default_out) {
    usz input_len = cstr_len(input_file);
    isz last_dot = -1;
    for (usz i = 0; i < input_len; i++) {
        if (input_file[i] == '.') last_dot = (isz)i;
    }
    usz out_len = last_dot > 0 ? (usz)last_dot : input_len;
    if (out_len > default_out.len - 1) out_len = default_out.len - 1;
    for (usz i = 0; i < out_len; i++) default_out[i] = input_file[i];
    default_out[out_len] = 0;
    return &default_out[0];
}

fn bool load_build_source(char* input_file, char[]* source_out) {
    char[] input_path = input_file[:cstr_len(input_file)];
    if (try src = io::file::load_temp((String)input_path)) {
        *source_out = src;
        return true;
    }
    io::printfn("Error: cannot read input file '%s'", (ZString)input_file);
    return false;
}

fn void parse_build_print_flags(int argc, char** argv, bool* print_last, bool* print_all) {
    *print_last = false;
    *print_all = false;
    for (int i = 0; i < argc; i++) {
        if (str_eq(argv[i], "--print-last")) *print_last = true;
        if (str_eq(argv[i], "--print-all")) *print_all = true;
    }
}

fn bool write_aot_temp_file(char[] c3_code) {
    char[] temp_path = "build/_aot_temp.c3";
    if (try file = io::file::open((String)temp_path, "w")) {
        defer (void)file.close();
        file.write(c3_code)!!;
        return true;
    }
    io::printn("Error: cannot write temp file build/_aot_temp.c3");
    return false;
}

fn int run_aot_backend_compile(char* output_binary) {
    char[2048] cmd_buf;
    usz cmd_len = 0;
    for (usz i = 0; i < AOT_BUILD_PREFIX.len && cmd_len < cmd_buf.len - 1; i++) {
        cmd_buf[cmd_len++] = AOT_BUILD_PREFIX[i];
    }
    usz out_len = cstr_len(output_binary);
    for (usz i = 0; i < out_len && cmd_len < cmd_buf.len - 1; i++) {
        cmd_buf[cmd_len++] = output_binary[i];
    }
    cmd_buf[cmd_len] = 0;
    return system(&cmd_buf[0]);
}

fn usz build_project_path(char[] path_buf, char* name, usz name_len, char[] suffix = "") {
    usz plen = 0;
    for (usz i = 0; i < name_len && plen < path_buf.len - 1; i++) {
        path_buf[plen++] = name[i];
    }
    if (suffix.len > 0 && plen < path_buf.len - 1) {
        path_buf[plen++] = '/';
        for (usz i = 0; i < suffix.len && plen < path_buf.len - 1; i++) {
            path_buf[plen++] = suffix[i];
        }
    }
    path_buf[plen] = 0;
    return plen;
}

fn void mkdir_project_path(char* name, usz name_len, char[] suffix = "") {
    char[512] path_buf;
    usz plen = build_project_path(path_buf[..], name, name_len, suffix);
    (void)plen;
    mkdir(&path_buf[0], 0o755);
}

fn void write_init_toml(char* name, usz name_len) {
    char[512] path_buf;
    usz plen = build_project_path(path_buf[..], name, name_len, "omni.toml");
    if (try file = io::file::open((String)path_buf[:plen], "w")) {
        defer (void)file.close();
        file.write("[project]\n")!!;
        file.write("name = \"")!!;
        file.write(name[:name_len])!!;
        file.write("\"\nversion = \"0.1.5\"\n\n")!!;
        file.write("[build]\n")!!;
        file.write("output-dir = \"build\"\n")!!;
        file.write("safety = \"safe\"        # \"safe\", \"fast\", or \"none\"\n")!!;
        file.write("opt = \"O0\"             # \"O0\", \"O1\", \"O2\", \"O3\", \"Os\", \"Oz\"\n")!!;
        file.write("debug-info = \"full\"    # \"full\", \"line-tables\", \"none\"\n")!!;
        file.write("# sanitize = \"none\"    # \"none\", \"address\", \"memory\", \"thread\"\n")!!;
        file.write("# single-module = false\n\n")!!;
        file.write("# FFI dependencies — uncomment and edit:\n")!!;
        file.write("# [dependencies.ffi.math]\n")!!;
        file.write("# library = \"m\"\n")!!;
        file.write("# headers = [\"/usr/include/math.h\"]\n")!!;
        file.write("# functions = [\"sin\", \"cos\", \"sqrt\"]\n")!!;
    }
}

fn void write_init_project_json(char* name, usz name_len) {
    char[512] path_buf;
    usz plen = build_project_path(path_buf[..], name, name_len, "build/project.json");
    if (try file = io::file::open((String)path_buf[:plen], "w")) {
        defer (void)file.close();
        file.write("// Generated from omni.toml — do not edit\n")!!;
        file.write("{\n")!!;
        file.write("    \"langrev\": \"1\",\n")!!;
        file.write("    \"version\": \"0.1.5\",\n")!!;
        file.write("    \"sources\": [\"../src\"],\n")!!;
        file.write("    \"output\": \".\",\n")!!;
        file.write("    \"safe\": true,\n")!!;
        file.write("    \"opt\": \"O0\",\n")!!;
        file.write("    \"debug-info\": \"full\",\n")!!;
        file.write("    \"targets\": {\n")!!;
        file.write("        \"main\": {\n")!!;
        file.write("            \"type\": \"executable\"\n")!!;
        file.write("        }\n")!!;
        file.write("    }\n")!!;
        file.write("}\n")!!;
    }
}

fn void write_init_main_omni(char* name, usz name_len) {
    char[512] path_buf;
    usz plen = build_project_path(path_buf[..], name, name_len, "src/main.omni");
    if (try file = io::file::open((String)path_buf[:plen], "w")) {
        defer (void)file.close();
        file.write("(println \"Hello from ")!!;
        file.write(name[:name_len])!!;
        file.write("!\")\n")!!;
    }
}

fn void print_init_summary(char* name) {
    io::printfn("Created project '%s'", (ZString)name);
    io::printn("");
    io::printfn("  %s/", (ZString)name);
    io::printn("    omni.toml       # Project config");
    io::printn("    src/main.omni   # Entry point");
    io::printn("    lib/ffi/        # Auto-generated FFI bindings");
    io::printn("    include/        # C headers");
    io::printn("    build/          # Build artifacts (contains project.json for c3c)");
    io::printn("");
    io::printn("Next steps:");
    io::printfn("  cd %s", (ZString)name);
    io::printn("  # Edit omni.toml to configure build and add FFI dependencies");
    io::printn("  # Run: omni --bind to generate bindings");
}

fn usz resolve_bind_project_dir(int argc, char** argv, int bind_idx, char[] proj_dir) {
    if (bind_idx + 1 < argc) {
        char* dir = argv[bind_idx + 1];
        usz len = 0;
        while (dir[len] != 0 && len < proj_dir.len - 1) {
            proj_dir[len] = dir[len];
            len++;
        }
        if (len > 0 && proj_dir[len - 1] == '/') len--;
        proj_dir[len] = 0;
        return len;
    }
    proj_dir[0] = '.';
    proj_dir[1] = 0;
    return 1;
}

fn usz build_bind_toml_path(char[] toml_path, char[] proj_dir) {
    usz tlen = 0;
    for (usz i = 0; i < proj_dir.len && tlen < toml_path.len - 1; i++) toml_path[tlen++] = proj_dir[i];
    if (tlen < toml_path.len - 1) toml_path[tlen++] = '/';
    char[] toml_suffix = "omni.toml";
    for (usz i = 0; i < toml_suffix.len && tlen < toml_path.len - 1; i++) toml_path[tlen++] = toml_suffix[i];
    toml_path[tlen] = 0;
    return tlen;
}

fn usz parse_ffi_dep_headers(lisp::LibclangHandle* clang, lisp::TomlFfiDep* dep,
    lisp::ParsedFunc* funcs, usz max_funcs) {
    usz func_count = 0;
    for (usz hi = 0; hi < dep.header_count; hi++) {
        char* header = &dep.headers[hi];
        usz hlen = cstr_len(header);
        usz found = lisp::parse_header(clang, header[:hlen],
            &funcs[func_count], max_funcs - func_count,
            &dep.functions, dep.function_count, dep.filter_functions);
        func_count += found;
    }
    return func_count;
}

fn usz build_bind_output_path(char[] out_path, char[] proj_dir, char* dep_name) {
    usz olen = 0;
    for (usz i = 0; i < proj_dir.len && olen < out_path.len - 1; i++) out_path[olen++] = proj_dir[i];
    char[] ffi_mid = "/lib/ffi/";
    for (usz i = 0; i < ffi_mid.len && olen < out_path.len - 1; i++) out_path[olen++] = ffi_mid[i];
    for (usz i = 0; dep_name[i] != 0 && olen < out_path.len - 1; i++) out_path[olen++] = dep_name[i];
    char[] omni_ext = ".omni";
    for (usz i = 0; i < omni_ext.len && olen < out_path.len - 1; i++) out_path[olen++] = omni_ext[i];
    out_path[olen] = 0;
    return olen;
}

/**
 * AOT build: compile Lisp source to standalone binary.
 * Usage: ./main --build input.lisp -o output_binary
 */
fn int run_build(int argc, char** argv, int build_idx) {
    char* input_file = build_idx + 1 < argc ? argv[build_idx + 1] : null;
    char* output_binary = find_build_output_arg(argc, argv, build_idx);
    if (input_file == null) {
        io::printn("Usage: ./main --build input.lisp [-o output]");
        return 1;
    }

    char[512] default_out;
    if (output_binary == null) output_binary = derive_default_output_name(input_file, default_out[..]);

    char[] source;
    if (!load_build_source(input_file, &source)) return 1;

    bool print_last;
    bool print_all;
    parse_build_print_flags(argc, argv, &print_last, &print_all);

    // Step 1: Compile Lisp → C3
    io::printfn("Compiling %s...", (ZString)input_file);
    thread_registry_init();
    defer thread_registry_shutdown();

    lisp::Interp* interp = (lisp::Interp*)mem::malloc(lisp::Interp.sizeof);
    defer mem::free(interp);
    interp.init();
    defer interp.destroy();

    char[] c3_code = lisp::compile_to_c3_ext(source, interp, print_last, print_all);
    if (c3_code.len == 0) return 1;

    // Step 2: Write temp C3 file
    if (!write_aot_temp_file(c3_code)) return 1;

    // Step 3: Invoke c3c to compile
    io::printfn("Building %s...", (ZString)output_binary);
    int exit_code = run_aot_backend_compile(output_binary);
    if (exit_code != 0) {
        io::printn("Error: c3c compilation failed");
        return 1;
    }

    io::printfn("Built: %s", (ZString)output_binary);
    return 0;
}

extern fn int mkdir(char* path, uint mode) @extern("mkdir");

/**
 * Scaffold a new Omni project directory.
 * Usage: ./main --init myproject
 */
fn int run_init(int argc, char** argv, int init_idx) {
    if (init_idx + 1 >= argc) {
        io::printn("Usage: ./main --init <project-name>");
        return 1;
    }

    char* name = argv[init_idx + 1];
    usz name_len = cstr_len(name);

    mkdir_project_path(name, name_len);
    mkdir_project_path(name, name_len, "src");
    mkdir_project_path(name, name_len, "lib");
    mkdir_project_path(name, name_len, "lib/ffi");
    mkdir_project_path(name, name_len, "include");
    mkdir_project_path(name, name_len, "build");

    write_init_toml(name, name_len);
    write_init_project_json(name, name_len);
    write_init_main_omni(name, name_len);
    print_init_summary(name);
    return 0;
}

/**
 * Generate FFI bindings from omni.toml using libclang.
 * Usage: ./main --bind [project-dir]
 */
fn int run_bind(int argc, char** argv, int bind_idx) {
    char[512] proj_dir;
    usz proj_dir_len = resolve_bind_project_dir(argc, argv, bind_idx, proj_dir[..]);

    // Load omni.toml
    char[512] toml_path;
    usz tlen = build_bind_toml_path(toml_path[..], proj_dir[..proj_dir_len]);

    lisp::TomlConfig* config = (lisp::TomlConfig*)mem::malloc(lisp::TomlConfig.sizeof);
    defer mem::free(config);

    if (!lisp::toml_load(toml_path[:tlen], config)) {
        io::printfn("Error: cannot read %s", (ZString)&toml_path[0]);
        return 1;
    }

    if (config.ffi_dep_count == 0) {
        io::printn("No FFI dependencies found in omni.toml");
        io::printn("Add a [dependencies.ffi.NAME] section to generate bindings.");
        return 0;
    }

    // Initialize libclang
    lisp::LibclangHandle clang;
    if (!lisp::libclang_init(&clang)) {
        io::printn("Error: libclang not found.");
        io::printn("");
        io::printn("Install libclang:");
        io::printn("  Arch:   pacman -S clang");
        io::printn("  Debian: apt install libclang-dev");
        io::printn("  Fedora: dnf install clang-devel");
        return 1;
    }
    defer lisp::libclang_destroy(&clang);

    // Allocate parsed functions buffer
    const usz MAX_FUNCS = 256;
    lisp::ParsedFunc* funcs = (lisp::ParsedFunc*)mem::malloc(lisp::ParsedFunc.sizeof * MAX_FUNCS);
    defer mem::free(funcs);

    usz total_bindings = 0;
    usz total_libs = 0;

    for (usz dep_idx = 0; dep_idx < config.ffi_dep_count; dep_idx++) {
        lisp::TomlFfiDep* dep = &config.ffi_deps[dep_idx];

        // Get null-terminated name and library
        char* dep_name = &dep.name;
        char* dep_lib = &dep.library;

        io::printfn("Parsing headers for '%s' (lib%s)...", (ZString)dep_name, (ZString)dep_lib);

        usz func_count = parse_ffi_dep_headers(&clang, dep, funcs, MAX_FUNCS);

        if (func_count == 0) {
            io::printfn("  Warning: no functions found for '%s'", (ZString)dep_name);
            continue;
        }

        char[512] out_path;
        usz olen = build_bind_output_path(out_path[..], proj_dir[..proj_dir_len], dep_name);

        // Get dep_name and dep_lib as slices
        usz dep_name_len = 0;
        while (dep_name[dep_name_len] != 0) dep_name_len++;
        usz dep_lib_len = 0;
        while (dep_lib[dep_lib_len] != 0) dep_lib_len++;

        if (lisp::generate_ffi_module(dep_name[:dep_name_len], dep_lib[:dep_lib_len],
                                       funcs, func_count, out_path[:olen])) {
            io::printfn("  Generated %s (%d functions)", (ZString)&out_path[0], (int)func_count);
            total_bindings += func_count;
            total_libs++;
        } else {
            io::printfn("  Error: failed to write %s", (ZString)&out_path[0]);
        }
    }

    io::printfn("\nGenerated %d bindings from %d libraries.", (int)total_bindings, (int)total_libs);
    return 0;
}

fn int print_help() {
    io::printn("omni 0.1.5 — A Lisp with modern semantics");
    io::printn("");
    io::printn("Usage: omni [options] [script.omni]");
    io::printn("");
    io::printn("Running:");
    io::printn("  omni                              Start the REPL");
    io::printn("  omni <script.omni>                Run a script file");
    io::printn("  omni --repl                       Start the REPL (explicit)");
    io::printn("");
    io::printn("Building:");
    io::printn("  omni --build <file> [-o output]   AOT compile to standalone binary");
    io::printn("  omni --compile <file> <out.c3>    Compile Omni source to C3");
    io::printn("");
    io::printn("Project management:");
    io::printn("  omni --init <name>                Scaffold a new Omni project");
    io::printn("  omni --bind [project-dir]         Generate FFI bindings from omni.toml");
    io::printn("");
    io::printn("Other:");
    io::printn("  omni --gen-e2e                    Generate end-to-end compiler tests");
    io::printn("  omni --stack-affinity-probe       Deliberately trigger stack ownership fail-fast probe");
    io::printn("  omni --version, -v                Print version");
    io::printn("  omni --help, -h                   Print this help");
    return 0;
}

fn int run_compile_mode(char* input_file, char* output_file) {
    if (input_file == null || output_file == null) {
        io::printn("Usage: ./main --compile input.lisp output.c3");
        return 1;
    }

    io::printfn("Compiling %s to %s...", (ZString)input_file, (ZString)output_file);

    // Read input file
    char[] input_path = input_file[:cstr_len(input_file)];
    if (try source = io::file::load_temp((String)input_path)) {
        // Initialize thread-local registry before interpreter
        thread_registry_init();

        // Initialize interpreter for symbol table (heap-allocated due to size)
        lisp::Interp* interp = (lisp::Interp*)mem::malloc(lisp::Interp.sizeof);
        interp.init();

        // Compile to C3
        char[] c3_code = lisp::compile_to_c3(source, interp);

        if (c3_code.len == 0) {
            interp.destroy();
            mem::free(interp);
            thread_registry_shutdown();
            return 1;
        }

        // Write output file
        char[] output_path = output_file[:cstr_len(output_file)];
        if (try file = io::file::open((String)output_path, "w")) {
            defer (void)file.close();
            file.write(c3_code)!!;
            io::printfn("Compilation successful: %s", (ZString)output_file);
            interp.destroy();
            mem::free(interp);
            thread_registry_shutdown();
            return 0;
        } else {
            io::printfn("Error: cannot write output file %s", (ZString)output_file);
            interp.destroy();
            mem::free(interp);
            thread_registry_shutdown();
            return 1;
        }
    } else {
        io::printfn("Error: cannot read input file %s", (ZString)input_file);
        return 1;
    }
}

fn int run_gen_e2e_mode() {
    thread_registry_init();
    lisp::Interp* interp = (lisp::Interp*)mem::malloc(lisp::Interp.sizeof);
    interp.init();
    lisp::register_primitives(interp);
    lisp::register_stdlib(interp);
    interp.flags.jit_enabled = true;

    lisp::generate_e2e_tests(interp);

    interp.destroy();
    mem::free(interp);
    thread_registry_shutdown();
    return 0;
}

fn int run_repl_mode() {
    io::printn("Starting Lisp REPL...");

    // Initialize thread-local registry first
    thread_registry_init();

    // Initialize interpreter
    lisp::Interp* interp = (lisp::Interp*)mem::malloc(lisp::Interp.sizeof);
    interp.init();
    lisp::register_primitives(interp);
    lisp::register_stdlib(interp);
    interp.flags.jit_enabled = true;

    // Run REPL
    lisp::repl(interp);

    // Clean up
    interp.destroy();
    mem::free(interp);
    thread_registry_shutdown();
    return 0;
}

fn int run_script_mode(char* script_file) {
    char[] script_path = script_file[:cstr_len(script_file)];

    // Read script file
    if (try source = io::file::load_temp((String)script_path)) {
        // Initialize thread-local registry
        thread_registry_init();

        // Initialize interpreter
        lisp::Interp* interp = (lisp::Interp*)mem::malloc(lisp::Interp.sizeof);
        interp.init();
        lisp::register_primitives(interp);
        lisp::register_stdlib(interp);
        interp.flags.jit_enabled = true;

        // Push script directory for relative import resolution
        lisp::push_source_dir(script_path, interp);

        // Run the script (multiple expressions)
        usz len = source.len;
        if (len > 65535) len = 65535;
        char[] src = source[:len];
        lisp::EvalResult r = lisp::run_program(src, interp);

        if (r.error.has_error) {
            // Print error
            usz msg_len = 0;
            while (msg_len < 256 && r.error.message[msg_len] != 0) {
                msg_len++;
            }
            if (r.error.line > 0) {
                io::printf("Error at line %d, column %d: ", (int)r.error.line, (int)r.error.column);
            } else {
                io::print("Error: ");
            }
            for (usz i = 0; i < msg_len; i++) {
                io::printf("%c", r.error.message[i]);
            }
            io::printn("");
            interp.destroy();
            mem::free(interp);
            thread_registry_shutdown();
            return 1;
        }

        // Print result if non-nil
        if (r.value != null && !lisp::is_nil(r.value)) {
            lisp::print_value(r.value, &interp.symbols);
            io::printn("");
        }

        interp.destroy();
        mem::free(interp);
        thread_registry_shutdown();
        return 0;
    } else {
        io::printfn("Error: cannot read script file '%s'", (ZString)script_file);
        return 1;
    }
}

fn int run_test_mode() {
    // Run tests mode
    io::printfn("Region Memory System v3 initialized");

    // Run stack engine tests (mmap stacks, context switch, coroutine lifecycle).
    // The stack engine test runner handles ASAN-specific overflow-test skipping.
    run_stack_engine_tests();

    // Run scope region tests (bump allocator, RC, freelist recycling)
    run_scope_region_tests();

    // Initialize thread-local registry BEFORE Lisp tests (interpreter uses regions)
    thread_registry_init();
    io::printfn("Thread-local registry initialized, root id=%d", (uint)thread_registry().root_id);

    // Run Lisp interpreter tests
    lisp::run_lisp_tests();

    if (mem_stats_enabled()) {
        lisp::print_memory_promotion_stats();
        scope_print_memory_stats();
    }

    thread_registry_shutdown();
    io::printfn("System ready.");
    return 0;
}

fn int run_stack_affinity_probe_mode() {
    io::printn("Running stack affinity violation probe...");
    return run_stack_engine_affinity_violation_probe();
}

/** Main entry point. */
fn int main(int argc, char** argv) {
    int idx = find_flag_index(argc, argv, "--help", "-h", "-help");
    if (idx >= 0) return print_help();

    idx = find_flag_index(argc, argv, "--version", "-v");
    if (idx >= 0) {
        io::printn("omni 0.1.5");
        return 0;
    }

    idx = find_flag_index(argc, argv, "--init");
    if (idx >= 0) return run_init(argc, argv, idx);

    idx = find_flag_index(argc, argv, "--bind");
    if (idx >= 0) return run_bind(argc, argv, idx);

    idx = find_flag_index(argc, argv, "--build");
    if (idx >= 0) return run_build(argc, argv, idx);

    idx = find_flag_index(argc, argv, "--compile", "-compile");
    if (idx >= 0) {
        char* input_file = idx + 1 < argc ? argv[idx + 1] : null;
        char* output_file = idx + 2 < argc ? argv[idx + 2] : null;
        return run_compile_mode(input_file, output_file);
    }

    idx = find_flag_index(argc, argv, "--gen-e2e");
    if (idx >= 0) return run_gen_e2e_mode();

    idx = find_flag_index(argc, argv, "--stack-affinity-probe");
    if (idx >= 0) return run_stack_affinity_probe_mode();

    idx = find_flag_index(argc, argv, "--repl", "-repl");
    if (idx >= 0) return run_repl_mode();

    if (argc > 1) return run_script_mode(argv[1]);
    return run_test_mode();
}
