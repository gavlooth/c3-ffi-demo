# RFC: In-Place Generational Evolution (IPGE)

**Status:** Proposed  
**Author:** Christos Chatzifountas 
**Topic:** Memory Safety / Generational References  
**Target:** Vale Programming Language & High-Performance Systems

---

## 1. Abstract
This proposal introduces **In-Place Generational Evolution (IPGE)**, a mechanism for memory safety that provides **deterministic uniqueness** without the performance overhead of indirection tables. By evolving a 64-bit generation ID using a bijective Linear Congruential Generator (LCG) directly in the object's memory slot, we eliminate the "stochastic risk" of random IDs while maintaining the speed of direct pointers.

## 2. Motivation
Vale currently explores two models:
1. **Random Generational Refs:** Fast, but carries a theoretical $1/2^{64}$ collision risk (Stochastic).
2. **Generation Tables:** Deterministic, but introduces a "double-hop" memory latency (Indirection).

**IPGE** serves as the "Third Way"—offering the mathematical certainty of the Table model with the hardware-level speed of the Random model.

## 3. Technical Specification

### 3.1 The Evolution Function
Instead of generating a new random number for every allocation, the system evolves the existing bits at the memory address. We use a **Full-Period LCG**, ensuring the sequence visits all $2^{64}$ values before repeating.

$$G_{next} = (G_{current} \times A + C) \pmod{2^{64}}$$

**Constants:**
* **Multiplier (A):** `0x5851f42d4c957f2d` (Knuth MMIX)
* **Increment (C):** `0x1442695040888963` (Must be odd)

### 3.2 Algorithm Flow
1. **Bootstrap:** On the first allocation of a memory page, seed the slot with `(SystemClock ^ Random64)`.
2. **Allocation/Destruction:** - Load the 64-bit value at the target memory address.
   - Apply the Evolution Function (1 Multiply, 1 Add).
   - Write the result back to the memory slot.
   - Store this new ID in the pointer (reference).
3. **Dereference:** Compare the pointer's ID with the ID in memory.



## 4. Implementation Example (C-Style)

```c
typedef struct {
    uint64_t generation;
    // ... object data ...
} ValeObject;

// High-performance evolution step
void evolve_generation(uint64_t* slot) {
    const uint64_t A = 0x5851f42D4C957F2DULL;
    const uint64_t C = 0x1442695040888963ULL;
    
    // In-place evolution: Transforms the previous generation into a 
    // unique, hard-to-predict next generation.
    *slot = (*slot * A) + C;
}
