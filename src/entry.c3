module main;

import std::io;
import lisp;

extern fn int system(char* command) @extern("system");

/**
 * AOT build: compile Lisp source to standalone binary.
 * Usage: ./main --build input.lisp -o output_binary
 */
fn int run_build(int argc, char** argv, int build_idx) {
    char* input_file = null;
    char* output_binary = null;

    // Parse: --build input.lisp [-o output]
    if (build_idx + 1 < argc) {
        input_file = argv[build_idx + 1];
    }
    for (int i = build_idx + 2; i < argc; i++) {
        if (str_eq(argv[i], "-o") && i + 1 < argc) {
            output_binary = argv[i + 1];
            break;
        }
    }

    if (input_file == null) {
        io::printn("Usage: ./main --build input.lisp [-o output]");
        return 1;
    }

    // Default output name: strip extension, or append .out
    char[512] default_out;
    usz default_out_len = 0;
    if (output_binary == null) {
        char* p = input_file;
        usz input_len = 0;
        while (*p != 0) { input_len++; p++; }
        // Find last dot
        isz last_dot = -1;
        for (usz i = 0; i < input_len; i++) {
            if (input_file[i] == '.') last_dot = (isz)i;
        }
        if (last_dot > 0) {
            default_out_len = (usz)last_dot;
        } else {
            default_out_len = input_len;
        }
        for (usz i = 0; i < default_out_len && i < 511; i++) {
            default_out[i] = input_file[i];
        }
        default_out[default_out_len] = 0;
        output_binary = &default_out[0];
    }

    // Read input file
    usz input_path_len = 0;
    char* ip = input_file;
    while (*ip != 0) { input_path_len++; ip++; }
    char[] input_path = input_file[:input_path_len];

    char[] source;
    if (try s = io::file::load_temp((String)input_path)) {
        source = s;
    } else {
        io::printfn("Error: cannot read input file '%s'", (ZString)input_file);
        return 1;
    }

    // Step 1: Compile Lisp → C3
    io::printfn("Compiling %s...", (ZString)input_file);
    thread_registry_init();
    lisp::Interp* interp = (lisp::Interp*)mem::malloc(lisp::Interp.sizeof);
    interp.init();

    char[] c3_code = lisp::compile_to_c3(source, interp);

    // Step 2: Write temp C3 file
    char[] temp_path = "build/_aot_temp.c3";
    if (try file = io::file::open((String)temp_path, "w")) {
        file.write(c3_code)!!;
        file.close()!!;
    } else {
        io::printn("Error: cannot write temp file build/_aot_temp.c3");
        mem::free(interp);
        thread_registry_shutdown();
        return 1;
    }

    // Step 3: Invoke c3c to compile
    io::printfn("Building %s...", (ZString)output_binary);

    // Build the c3c command
    usz out_len = 0;
    char* op = output_binary;
    while (*op != 0) { out_len++; op++; }

    char[1024] cmd_buf;
    usz cmd_len = 0;
    // Include all source files needed by runtime (will be slimmed after Phase 6)
    char[] prefix = "env -u LD_LIBRARY_PATH c3c compile src/main.c3 src/context.c3 src/continuation.c3 src/delimited.c3 src/ghost_index.c3 src/ffi_bindings.c3 src/test_ghost_lookup.c3 src/test_arena_bug.c3 src/test_alignment.c3 src/test_sparse_set.c3 src/test_typeid.c3 src/test_destructor_registry.c3 src/test_slot_table.c3 src/test_region.c3 src/lisp/eval.c3 src/lisp/parser.c3 src/lisp/value.c3 src/lisp/jit.c3 src/lisp/compiler.c3 src/lisp/runtime.c3 build/_aot_temp.c3 -l lightning -l readline -l dl -l m -L /usr/local/lib -o ";
    for (usz i = 0; i < prefix.len && cmd_len < 1023; i++) {
        cmd_buf[cmd_len] = prefix[i];
        cmd_len++;
    }
    for (usz i = 0; i < out_len && cmd_len < 1023; i++) {
        cmd_buf[cmd_len] = output_binary[i];
        cmd_len++;
    }
    char[] flags = " -l m";
    for (usz i = 0; i < flags.len && cmd_len < 1023; i++) {
        cmd_buf[cmd_len] = flags[i];
        cmd_len++;
    }
    cmd_buf[cmd_len] = 0;

    int exit_code = system(&cmd_buf[0]);

    mem::free(interp);
    thread_registry_shutdown();

    if (exit_code != 0) {
        io::printn("Error: c3c compilation failed");
        return 1;
    }

    io::printfn("Built: %s", (ZString)output_binary);
    return 0;
}

/**
 * Main entry point.
 *
 * Usage:
 *   ./main                              - Run all tests
 *   ./main -repl                        - Start interactive REPL
 *   ./main --repl                       - Start interactive REPL
 *   ./main --compile input.lisp out.c3  - Compile Lisp to C3
 *   ./main --build input.lisp -o prog   - AOT compile Lisp to standalone binary
 *   ./main --gen-e2e                    - Generate e2e compiler test files
 *   ./main script.pik                   - Run a script file
 */
fn int main(int argc, char** argv) {
    // Check for --build flag (AOT compile to standalone binary)
    for (int i = 1; i < argc; i++) {
        if (str_eq(argv[i], "--build")) {
            return run_build(argc, argv, i);
        }
    }

    // Check for compile flag
    bool run_compile = false;
    char* input_file = null;
    char* output_file = null;
    for (int i = 1; i < argc; i++) {
        if (str_eq(argv[i], "--compile") || str_eq(argv[i], "-compile")) {
            run_compile = true;
            if (i + 1 < argc) {
                input_file = argv[i + 1];
            }
            if (i + 2 < argc) {
                output_file = argv[i + 2];
            }
            break;
        }
    }

    if (run_compile) {
        // Compile mode
        if (input_file == null || output_file == null) {
            io::printn("Usage: ./main --compile input.lisp output.c3");
            return 1;
        }

        io::printfn("Compiling %s to %s...", (ZString)input_file, (ZString)output_file);

        // Read input file
        char[] input_path;
        usz input_len = 0;
        char* p = input_file;
        while (*p != 0) { input_len++; p++; }
        input_path = input_file[:input_len];

        if (try source = io::file::load_temp((String)input_path)) {
            // Initialize thread-local registry before interpreter
            thread_registry_init();

            // Initialize interpreter for symbol table (heap-allocated due to size)
            lisp::Interp* interp = (lisp::Interp*)mem::malloc(lisp::Interp.sizeof);
            interp.init();

            // Compile to C3
            char[] c3_code = lisp::compile_to_c3(source, interp);

            // Write output file
            char[] output_path;
            usz output_len = 0;
            p = output_file;
            while (*p != 0) { output_len++; p++; }
            output_path = output_file[:output_len];

            if (try file = io::file::open((String)output_path, "w")) {
                file.write(c3_code)!!;
                file.close()!!;
                io::printfn("Compilation successful: %s", (ZString)output_file);
                mem::free(interp);
                thread_registry_shutdown();
                return 0;
            } else {
                io::printfn("Error: cannot write output file %s", (ZString)output_file);
                mem::free(interp);
                thread_registry_shutdown();
                return 1;
            }
        } else {
            io::printfn("Error: cannot read input file %s", (ZString)input_file);
            return 1;
        }
    }

    // Check for --gen-e2e flag
    for (int i = 1; i < argc; i++) {
        if (str_eq(argv[i], "--gen-e2e")) {
            thread_registry_init();
            lisp::Interp* interp = (lisp::Interp*)mem::malloc(lisp::Interp.sizeof);
            interp.init();
            lisp::register_primitives(interp);
            lisp::register_destructors();
            lisp::register_stdlib(interp);
            interp.jit_enabled = true;

            lisp::generate_e2e_tests(interp);

            mem::free(interp);
            thread_registry_shutdown();
            return 0;
        }
    }

    // Check for REPL flag
    bool run_repl = false;
    for (int i = 1; i < argc; i++) {
        if (str_eq(argv[i], "-repl") || str_eq(argv[i], "--repl")) {
            run_repl = true;
            break;
        }
    }

    if (run_repl) {
        // Start REPL mode
        io::printn("Starting Lisp REPL...");

        // Initialize thread-local registry first
        thread_registry_init();

        // Initialize interpreter
        lisp::Interp* interp = (lisp::Interp*)mem::malloc(lisp::Interp.sizeof);
        interp.init();
        lisp::register_primitives(interp);
        lisp::register_destructors();
        lisp::register_stdlib(interp);
        interp.jit_enabled = true;

        // Run REPL
        lisp::repl(interp);

        // Clean up
        mem::free(interp);
        thread_registry_shutdown();
        return 0;
    }

    // Check for script file argument (any arg that isn't a known flag)
    if (argc > 1) {
        // argv[1] is not --compile, -repl, or --repl — treat as script file
        char* script_file = argv[1];
        usz script_path_len = 0;
        char* sp = script_file;
        while (*sp != 0) { script_path_len++; sp++; }
        char[] script_path = script_file[:script_path_len];

        // Read script file
        if (try source = io::file::load_temp((String)script_path)) {
            // Initialize thread-local registry
            thread_registry_init();

            // Initialize interpreter
            lisp::Interp* interp = (lisp::Interp*)mem::malloc(lisp::Interp.sizeof);
            interp.init();
            lisp::register_primitives(interp);
            lisp::register_destructors();
            lisp::register_stdlib(interp);
            interp.jit_enabled = true;

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
                mem::free(interp);
                thread_registry_shutdown();
                return 1;
            }

            // Print result if non-nil
            if (r.value != null && !lisp::is_nil(r.value)) {
                lisp::print_value(r.value, &interp.symbols);
                io::printn("");
            }

            mem::free(interp);
            thread_registry_shutdown();
            return 0;
        } else {
            io::printfn("Error: cannot read script file '%s'", (ZString)script_file);
            return 1;
        }
    }

    // Run tests mode
    io::printfn("Region Memory System v3 initialized");

    // Run destructor registry tests (O(1) lookup implementation)
    run_destructor_registry_tests();

    // Run ghost lookup tests (O(1) optimization verification)
    run_ghost_lookup_tests();

    // Run context capture/restore tests (x86_64 assembly primitives)
    // Note: These tests exercise low-level CPU state manipulation
    run_context_tests();

    // Run delimited continuation tests (reset/shift/resume system)
    // Note: These tests verify the continuation machinery including:
    // - Prompt stack operations
    // - Continuation allocation and lifecycle
    // - Region integration
    // - Effect handler patterns
    run_delimited_tests();

    // Initialize thread-local registry BEFORE Lisp tests (interpreter uses regions)
    thread_registry_init();
    io::printfn("Thread-local registry initialized, root id=%d", (uint)thread_registry().root_id);

    // Run Lisp interpreter tests
    lisp::run_lisp_tests();

    // Create a child region using convenience function
    RegionHandle child = create_region();
    io::printfn("Child region created: id=%d, gen=%d",
        (uint)child.region_id, (uint)child.generation);

    // Clean up
    release_region(child);  // Release our reference
    thread_registry_shutdown();

    io::printfn("System ready.");
    return 0;
}
