#ifndef EMP_FRONTEND_BOOLEAN_PROGRAM_H__
#define EMP_FRONTEND_BOOLEAN_PROGRAM_H__

// Protocol-neutral Boolean circuit IR captured from a PURE circuit function:
// inputs are the function's arguments, the output is its return value. There is
// no I/O inside the circuit — no secret feed, no reveal (those are the caller's
// job, in direct mode, around the circuit). A RecordBackend (record_backend.h)
// drives the global Backend interface and emits one of these; the analysis
// passes (passes.h) annotate it; any protocol backend replays it. Nothing here
// knows about labels, MACs, triples, or any specific protocol.
//
// Wire ids are dense [0, num_wire), assigned in emission order: a wire is
// created either by an input-argument port or by the gate that outputs it.
// There are no create/destroy markers — liveness is derived by a pass (see
// passes.h), which is exact for the bounded circuits this layer captures.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace emp {
namespace frontend {

// Linear gates (XOR/NOT) are free; AND is the only costed op. Public constants
// are first-class CONST0/CONST1 gates (no operands) so the IR stays protocol-
// neutral — each backend realizes a known 0/1 wire in whatever way its protocol
// prescribes.
enum class Op : int8_t { AND, XOR, NOT, CONST0, CONST1 };

// in1 is unused for NOT and the CONST ops. `out` is the wire id produced.
struct Gate {
	int in0 = -1, in1 = -1, out = -1;
	Op  op  = Op::AND;
	bool is_and()    const { return op == Op::AND; }
	bool is_xor()    const { return op == Op::XOR; }
	bool is_not()    const { return op == Op::NOT; }
	bool is_const()  const { return op == Op::CONST0 || op == Op::CONST1; }
	bool is_linear() const { return op == Op::XOR  || op == Op::NOT; }
};

// One circuit argument. Wires [base, base+n) are its bits, bound to a live
// input value at run time (the caller passes the EMP object). Ownership /
// authentication lives in that live value, not here — a circuit is wire-typed,
// not party-typed.
struct InputPort {
	int base = 0;
	int n    = 0;
};

// AND-depth scheduling metadata (filled by schedule_pass). Populated now for
// future GMW round batching / depth-minimization; unused by constant-round
// backends.
struct LevelInfo {
	std::vector<std::vector<int>> and_gate_indices;  // gate indices per AND-depth level
	int depth = 0;                                   // max AND-depth
};

struct BooleanProgram {
	int         num_wire   = 0;    // total wire ids (set by RecordBackend::finalize)
	int         num_and    = 0;    // count of AND gates
	std::size_t wire_bytes = 0;    // sizeof(RecWire) at record time, for sanity checks

	std::vector<Gate>      gates;    // topological (emission) order
	std::vector<InputPort> inputs;   // one per circuit argument, in order
	std::vector<int>       outputs;  // wire ids of the return value (flattened)

	int total_input_bits() const {
		int n = 0; for (const auto& p : inputs) n += p.n; return n;
	}
	int total_output_bits() const { return (int)outputs.size(); }
	int num_gate() const { return (int)gates.size(); }
};

}  // namespace frontend
}  // namespace emp
#endif  // EMP_FRONTEND_BOOLEAN_PROGRAM_H__
