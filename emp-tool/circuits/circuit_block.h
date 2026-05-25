#ifndef EMP_CIRCUIT_BLOCK_H__
#define EMP_CIRCUIT_BLOCK_H__

// Opt-in batteries-included binding of the circuit layer to the `block` wire —
// what all of emp-tool's own backends (HalfGate, ClearBackend, privacy-free)
// operate on. Nothing includes this automatically (there is no default wire);
// include it when you want the full standard block alias set in one line, plus
// the extern-template build speedup. Otherwise call EMP_USE_CIRCUIT_TYPES
// yourself with your backend's wire.

#include "emp-tool/core/block.h"
#include "emp-tool/circuits/circuit.h"

// All friendly aliases for the block wire (Bit, BitVec, …, the fixed-width
// ints, and the calculators). The macro opens namespace emp itself.
EMP_USE_CIRCUIT_TYPES_ALL(block)

// Skip per-TU implicit instantiation of the block classes; the matching
// definitions are emitted once in circuit.cpp.
namespace emp {
EMP_CIRCUIT_CLASS_LIST(EMP_EXTERN_TEMPLATE, block)
}  // namespace emp

#endif  // EMP_CIRCUIT_BLOCK_H__
