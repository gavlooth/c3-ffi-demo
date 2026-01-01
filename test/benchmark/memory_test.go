package benchmark

import (
	"runtime"
	"testing"

	"purple_go/pkg/jit"
	"purple_go/pkg/parser"
)

func BenchmarkMemoryUsage(b *testing.B) {
	var m runtime.MemStats

	code := "(fold + 0 (range 100000))"
	expr, _ := parser.Parse(code)

	j := jit.Get()
	if !j.IsAvailable() {
		b.Skip("JIT not available (gcc not found)")
	}

	runtime.GC()
	runtime.ReadMemStats(&m)
	allocBefore := m.TotalAlloc

	for i := 0; i < b.N; i++ {
		_, _ = jit.CompileAndRun(expr)
	}

	runtime.GC()
	runtime.ReadMemStats(&m)
	allocAfter := m.TotalAlloc

	b.ReportMetric(float64(allocAfter-allocBefore)/float64(b.N), "bytes/op")
}
