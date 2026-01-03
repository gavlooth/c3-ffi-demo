package eval

import (
	"sync"

	"purple_go/pkg/ast"
)

// Process states
const (
	ProcReady   = 0
	ProcRunning = 1
	ProcParked  = 2
	ProcDone    = 3
)

// Scheduler manages green threads
type Scheduler struct {
	mu       sync.Mutex
	runQueue []*ast.Value // Queue of ready processes
	current  *ast.Value   // Currently running process
	running  bool         // Is scheduler running?
}

// Global scheduler instance
var globalScheduler = &Scheduler{}

// GetScheduler returns the global scheduler
func GetScheduler() *Scheduler {
	return globalScheduler
}

// Spawn creates a new process and adds it to the run queue
func (s *Scheduler) Spawn(thunk *ast.Value, menv *ast.Value) *ast.Value {
	s.mu.Lock()
	defer s.mu.Unlock()

	// Create a new process
	proc := ast.NewProcess(nil)
	proc.ProcState = ProcReady

	// Store the thunk and menv for later execution
	// We use the continuation field to store the thunk
	proc.ProcCont = thunk
	proc.ContMenv = menv

	s.runQueue = append(s.runQueue, proc)
	return proc
}

// Run starts the scheduler and runs until all processes complete
func (s *Scheduler) Run() {
	s.mu.Lock()
	if s.running {
		s.mu.Unlock()
		return
	}
	s.running = true
	s.mu.Unlock()

	for {
		s.mu.Lock()
		if len(s.runQueue) == 0 {
			s.running = false
			s.mu.Unlock()
			return
		}

		// Get next process
		proc := s.runQueue[0]
		s.runQueue = s.runQueue[1:]
		s.current = proc
		s.mu.Unlock()

		// Run the process
		s.runProcess(proc)
	}
}

// runProcess executes a single process step
func (s *Scheduler) runProcess(proc *ast.Value) {
	if proc.ProcState != ProcReady {
		return
	}

	proc.ProcState = ProcRunning

	// Execute the thunk
	thunk := proc.ProcCont
	menv := proc.ContMenv

	if thunk != nil && (ast.IsLambda(thunk) || ast.IsRecLambda(thunk) || ast.IsPrim(thunk)) {
		result := applyFn(thunk, ast.Nil, menv)
		proc.ProcResult = result
	}

	proc.ProcState = ProcDone
}

// Park parks the current process (for channel operations)
func (s *Scheduler) Park(proc *ast.Value, resume func(*ast.Value) *ast.Value) {
	s.mu.Lock()
	defer s.mu.Unlock()

	proc.ProcState = ProcParked
	// Store the resume function in the continuation
	proc.ContFn = resume
}

// Unpark wakes up a parked process with a value
func (s *Scheduler) Unpark(proc *ast.Value, value *ast.Value) {
	s.mu.Lock()
	defer s.mu.Unlock()

	if proc.ProcState != ProcParked {
		return
	}

	proc.ProcState = ProcReady

	// Call the resume function if set
	if proc.ContFn != nil {
		proc.ContFn(value)
	}

	s.runQueue = append(s.runQueue, proc)
}

// Channel operations

// ChanSend sends a value on a channel
// Returns true if sent immediately, false if should park
func ChanSend(ch *ast.Value, val *ast.Value) bool {
	if !ast.IsChan(ch) {
		return false
	}

	select {
	case ch.ChanSend <- val:
		return true
	default:
		return false
	}
}

// ChanRecv receives a value from a channel
// Returns (value, true) if received, (nil, false) if should park
func ChanRecv(ch *ast.Value) (*ast.Value, bool) {
	if !ast.IsChan(ch) {
		return nil, false
	}

	select {
	case val := <-ch.ChanRecv:
		return val, true
	default:
		return nil, false
	}
}

// ChanSendBlocking sends a value, blocking if necessary
// Returns true if send succeeded, false if channel was closed
func ChanSendBlocking(ch *ast.Value, val *ast.Value) (success bool) {
	if !ast.IsChan(ch) {
		return false
	}

	// Use defer/recover to handle sending to closed channel
	defer func() {
		if recover() != nil {
			success = false
		}
	}()

	// Attempt to send - will panic if channel is closed
	ch.ChanSend <- val
	return true
}

// ChanRecvBlocking receives a value, blocking if necessary
func ChanRecvBlocking(ch *ast.Value) *ast.Value {
	if !ast.IsChan(ch) {
		return ast.Nil
	}
	return <-ch.ChanRecv
}

// ChanClose closes a channel
func ChanClose(ch *ast.Value) {
	if !ast.IsChan(ch) {
		return
	}
	close(ch.ChanSend)
}
