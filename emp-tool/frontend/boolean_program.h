#ifndef EMP_FRONTEND_BOOLEAN_PROGRAM_H__
#define EMP_FRONTEND_BOOLEAN_PROGRAM_H__

// Protocol-neutral Boolean circuit IR captured from ordinary emp-tool circuit
// code. A RecordBackend (record_backend.h) drives the global Backend interface
// and emits one of these; the analysis passes (passes.h) annotate it; any
// protocol backend lowers it to its own representation and runs it. Nothing
// here knows about labels, MACs, triples, or any specific protocol.
//
// Wire ids are dense [0, num_wire), assigned in emission order: a wire is
// created either by an input port or by the gate that outputs it. There are no
// create/destroy markers — liveness is derived by a pass (see passes.h), which
// is exact for the bounded circuits this layer captures.

#include "emp-tool/core/constants.h"   // PUBLIC / ALICE / BOB
#include <cstddef>
#include <cstdint>
#include <vector>

namespace emp {
namespace frontend {

// Linear gates (XOR/NOT) are free; AND is the only costed op. Public constants
// are first-class so the IR stays protocol-neutral — each backend realizes them
// its own way (e.g. ag2pc synthesizes c0 = XOR(w,w), c1 = NOT c0).
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

// One secret input feed (or one parameter, later). Wires [base, base+n) carry
// the fed bits. `fed_bits` are the values this party passed at record time —
// real for ports it owns, dummy otherwise; an executor uses the owner's bits
// (BoundInputs may override). External (deferred) marks a boundary wire bound
// to ambient protocol state instead of fed from cleartext.
struct InputPort {
	enum class Kind { Fed, External };
	Kind kind  = Kind::Fed;
	int  owner = PUBLIC;
	int  base  = 0;
	int  n     = 0;
	std::vector<bool> fed_bits;   // size n for Fed; empty for External
};

// One reveal() call (or one returned wire bundle, later). Revealed decodes to
// cleartext at `to_party`; Wire (deferred) hands the live wires back to the
// caller without revealing.
struct OutputPort {
	enum class Kind { Revealed, Wire };
	Kind kind      = Kind::Revealed;
	int  to_party  = PUBLIC;       // Revealed only
	std::vector<int> wire_ids;     // wire ids revealed/returned, in order
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

	std::vector<Gate>       gates;     // topological (emission) order
	std::vector<InputPort>  inputs;    // Fed ports in feed() order
	std::vector<OutputPort> outputs;   // Revealed ports in reveal() order

	int total_input_bits() const {
		int n = 0; for (const auto& p : inputs)  n += p.n; return n;
	}
	int total_output_bits() const {
		int n = 0; for (const auto& p : outputs) n += (int)p.wire_ids.size(); return n;
	}
	int num_gate() const { return (int)gates.size(); }
};

}  // namespace frontend
}  // namespace emp
#endif  // EMP_FRONTEND_BOOLEAN_PROGRAM_H__
