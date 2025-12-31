package main

import (
	"bufio"
	"flag"
	"fmt"
	"io"
	"os"
	"strings"

	"purple_go/pkg/ast"
	"purple_go/pkg/codegen"
	"purple_go/pkg/eval"
	"purple_go/pkg/memory"
	"purple_go/pkg/parser"
)

var (
	compileMode = flag.Bool("c", false, "Compile to C code instead of interpreting")
	outputFile  = flag.String("o", "", "Output file (default: stdout)")
	evalExpr    = flag.String("e", "", "Evaluate expression from command line")
	verbose     = flag.Bool("v", false, "Verbose output")
)

func main() {
	flag.Usage = func() {
		fmt.Fprintf(os.Stderr, "Purple Go - ASAP Memory Management Compiler\n\n")
		fmt.Fprintf(os.Stderr, "Usage: %s [options] [file.purple]\n\n", os.Args[0])
		fmt.Fprintf(os.Stderr, "Options:\n")
		flag.PrintDefaults()
		fmt.Fprintf(os.Stderr, "\nExamples:\n")
		fmt.Fprintf(os.Stderr, "  %s -e '(+ 1 2)'              # Evaluate expression\n", os.Args[0])
		fmt.Fprintf(os.Stderr, "  %s -c -e '(lift 42)'         # Compile to C\n", os.Args[0])
		fmt.Fprintf(os.Stderr, "  %s program.purple            # Run file\n", os.Args[0])
		fmt.Fprintf(os.Stderr, "  %s -c program.purple -o out.c # Compile file to C\n", os.Args[0])
	}
	flag.Parse()

	var input string
	var err error

	if *evalExpr != "" {
		input = *evalExpr
	} else if flag.NArg() > 0 {
		filename := flag.Arg(0)
		data, err := os.ReadFile(filename)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Error reading file: %v\n", err)
			os.Exit(1)
		}
		input = string(data)
	} else {
		// Read from stdin
		data, err := io.ReadAll(os.Stdin)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Error reading stdin: %v\n", err)
			os.Exit(1)
		}
		input = string(data)
	}

	if strings.TrimSpace(input) == "" {
		// Interactive REPL mode
		runREPL()
		return
	}

	// Parse all expressions
	p := parser.New(input)
	exprs, err := p.ParseAll()
	if err != nil {
		fmt.Fprintf(os.Stderr, "Parse error: %v\n", err)
		os.Exit(1)
	}

	if len(exprs) == 0 {
		fmt.Fprintf(os.Stderr, "No expressions to process\n")
		os.Exit(1)
	}

	if *compileMode {
		// Compile to C
		compileToC(exprs)
	} else {
		// Interpret
		interpret(exprs)
	}
}

func interpret(exprs []*ast.Value) {
	env := eval.DefaultEnv()
	menv := eval.NewMenv(ast.Nil, env)

	for _, expr := range exprs {
		if *verbose {
			fmt.Printf("Evaluating: %s\n", expr.String())
		}

		result := eval.Eval(expr, menv)
		if result != nil {
			if ast.IsCode(result) {
				fmt.Printf("Code: %s\n", result.Str)
			} else {
				fmt.Printf("Result: %s\n", result.String())
			}
		}
	}
}

func compileToC(exprs []*ast.Value) {
	var output io.Writer = os.Stdout
	if *outputFile != "" {
		f, err := os.Create(*outputFile)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Error creating output file: %v\n", err)
			os.Exit(1)
		}
		defer f.Close()
		output = f
	}

	// Evaluate expressions to get code
	env := eval.DefaultEnv()
	menv := eval.NewMenv(ast.Nil, env)

	var codeExprs []*ast.Value
	for _, expr := range exprs {
		result := eval.Eval(expr, menv)
		if result != nil {
			codeExprs = append(codeExprs, result)
		}
	}

	// Generate complete C program
	gen := codegen.NewCodeGenerator(output)
	gen.GenerateProgram(codeExprs)

	if *outputFile != "" && *verbose {
		fmt.Fprintf(os.Stderr, "Generated C code written to %s\n", *outputFile)
	}
}

func runREPL() {
	fmt.Println("Purple Go REPL - ASAP Memory Management")
	fmt.Println("Type expressions to evaluate, 'quit' to exit, 'compile' to toggle compile mode")
	fmt.Println()

	env := eval.DefaultEnv()
	menv := eval.NewMenv(ast.Nil, env)
	compiling := false

	scanner := bufio.NewScanner(os.Stdin)
	for {
		if compiling {
			fmt.Print("purple(compile)> ")
		} else {
			fmt.Print("purple> ")
		}

		if !scanner.Scan() {
			break
		}

		line := strings.TrimSpace(scanner.Text())
		if line == "" {
			continue
		}

		switch line {
		case "quit", "exit":
			fmt.Println("Goodbye!")
			return
		case "compile":
			compiling = !compiling
			if compiling {
				fmt.Println("Compile mode ON - expressions will generate C code")
			} else {
				fmt.Println("Compile mode OFF - expressions will be interpreted")
			}
			continue
		case "help":
			fmt.Println("Commands:")
			fmt.Println("  quit     - exit the REPL")
			fmt.Println("  compile  - toggle compile mode")
			fmt.Println("  runtime  - print C runtime")
			fmt.Println("  help     - show this help")
			fmt.Println()
			fmt.Println("Examples:")
			fmt.Println("  (+ 1 2)            - add numbers")
			fmt.Println("  (lift 42)          - lift to code")
			fmt.Println("  (let ((x 10)) x)   - let binding")
			continue
		case "runtime":
			registry := codegen.NewTypeRegistry()
			registry.InitDefaultTypes()

			// Also include memory management runtimes
			gen := codegen.NewRuntimeGenerator(os.Stdout, registry)
			gen.GenerateAll()

			sccGen := memory.NewSCCGenerator(os.Stdout)
			sccGen.GenerateSCCRuntime()
			sccGen.GenerateSCCDetection()

			deferredGen := memory.NewDeferredGenerator(os.Stdout)
			deferredGen.GenerateDeferredRuntime()

			arenaGen := memory.NewArenaGenerator(os.Stdout)
			arenaGen.GenerateArenaRuntime()
			continue
		}

		p := parser.New(line)
		expr, err := p.Parse()
		if err != nil {
			fmt.Printf("Parse error: %v\n", err)
			continue
		}

		if expr == nil {
			continue
		}

		result := eval.Eval(expr, menv)
		if result != nil {
			if ast.IsCode(result) {
				if compiling {
					fmt.Println("Generated C:")
					fmt.Println(result.Str)
				} else {
					fmt.Printf("Code: %s\n", result.Str)
				}
			} else {
				fmt.Printf("=> %s\n", result.String())
			}
		}
	}
}
