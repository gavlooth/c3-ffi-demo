package codegen

import "sync"

var (
	globalCodegen   *CodeGenerator
	globalCodegenMu sync.RWMutex
)

// SetGlobalCodeGenerator sets the global code generator for cross-package analysis.
func SetGlobalCodeGenerator(gen *CodeGenerator) {
	globalCodegenMu.Lock()
	defer globalCodegenMu.Unlock()
	globalCodegen = gen
}

// GlobalCodeGenerator returns the global code generator if set.
func GlobalCodeGenerator() *CodeGenerator {
	globalCodegenMu.RLock()
	defer globalCodegenMu.RUnlock()
	return globalCodegen
}

// ResetGlobalCodeGenerator clears the global generator (for tests).
func ResetGlobalCodeGenerator() {
	globalCodegenMu.Lock()
	defer globalCodegenMu.Unlock()
	globalCodegen = nil
}
