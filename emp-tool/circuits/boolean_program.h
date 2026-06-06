#ifndef EMP_CIRCUIT_BOOLEAN_PROGRAM_H__
#define EMP_CIRCUIT_BOOLEAN_PROGRAM_H__

// The canonical, protocol-neutral Boolean-circuit IR for emp-tool. ONE in-memory
// representation, ONE gate encoding. A program's inputs are wires [0, num_inputs);
// every other wire is produced by exactly one gate, in topological (emission)
// order; the return value is the explicit `outputs` wire list. The IR knows
// nothing about parties, owners, argument boundaries, labels, MACs, or any
// protocol — those live in the layers that produce/consume it (the typed
// frontend carries argument shapes; a protocol backend carries ownership).
//
// Execution is split into one primitive and one wrapper:
//   for_each_gate(prog, visitor)  — the single place that owns the exhaustive
//                                   op switch; a new Op is a compile error at
//                                   every visitor, never a silent fallthrough.
//   execute_program(prog, ...)    — the in/out wrapper most callers want: seed
//                                   input slots, walk once, read output slots.
// A consumer with a different wire model (e.g. ag2pc, which replays the same
// program once per garbling phase against a swapped backend and threads wire
// IDs rather than values) calls for_each_gate directly.

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace emp {
namespace circuit {

// Linear gates (Xor/Not) are free; And is the only costed op. Public constants
// are first-class Const0/Const1 gates (no operands) so the IR stays protocol-
// neutral — each backend realizes a known 0/1 wire however its protocol
// prescribes. The on-disk .empbc op codes are exactly these values.
enum class Op : uint8_t { And = 0, Xor = 1, Not = 2, Const0 = 3, Const1 = 4 };

// in1 is unused for Not/Const0/Const1 and is normalized to 0; out is the wire id
// produced. Validation and execution only read the operands relevant to the op,
// so the normalized 0 is never interpreted as a real wire.
struct Gate {
	uint32_t in0 = 0, in1 = 0, out = 0;
	Op       op  = Op::And;
	bool is_and()    const { return op == Op::And; }
	bool is_xor()    const { return op == Op::Xor; }
	bool is_not()    const { return op == Op::Not; }
	bool is_const()  const { return op == Op::Const0 || op == Op::Const1; }
	bool is_linear() const { return op == Op::Xor  || op == Op::Not; }
};

struct BooleanProgram {
	uint32_t num_wires  = 0;   // total wire ids; valid ids are [0, num_wires)
	uint32_t num_inputs = 0;   // input wires are [0, num_inputs)

	std::vector<Gate>     gates;     // topological (emission) order
	std::vector<uint32_t> outputs;   // wire ids of the return value (flattened)

	uint32_t total_input_bits()  const { return num_inputs; }
	uint32_t total_output_bits() const { return (uint32_t)outputs.size(); }
	uint32_t num_gate()          const { return (uint32_t)gates.size(); }
};

// ---------------------------------------------------------------------------
// Validation. ONE path, run at construction / load / before executing an
// externally supplied program — never inside the execution hot loop. Throws
// std::runtime_error on the first violation. Enforces the IR's invariants so
// every downstream consumer (frontend replay, float builtins, ag2pc) may trust
// the structure without re-checking.
// ---------------------------------------------------------------------------
inline void validate_program(const BooleanProgram& p) {
	const uint64_t NW = p.num_wires;
	if (p.num_inputs > NW)
		throw std::runtime_error("validate_program: num_inputs exceeds num_wires");

	const uint64_t NG = p.gates.size();
	const uint64_t non_inputs = NW - p.num_inputs;
	if (NG != non_inputs)
		throw std::runtime_error("validate_program: num_wires must equal num_inputs + num_gates (dense program)");

	// A non-input wire is "defined" once its producer has been seen; input
	// wires are defined up front. Track only non-input wires, not all
	// num_wires, so a malformed file cannot force allocation proportional to a
	// huge declared input count during validation.
	std::vector<char> defined_non_input((size_t)non_inputs, 0);

	auto at = [](size_t gi, uint32_t w) {
		return " (gate " + std::to_string(gi) + ", wire " + std::to_string(w) + ")";
	};
	size_t gi = 0;
	auto chk_read = [&](uint32_t w, const char* what) {
		if (w >= NW) throw std::runtime_error(std::string("validate_program: ") + what + " index out of range" + at(gi, w));
		if (w >= p.num_inputs && !defined_non_input[w - p.num_inputs])
			throw std::runtime_error(std::string("validate_program: ") + what + " read before defined" + at(gi, w));
	};
	for (; gi < p.gates.size(); ++gi) {
		const Gate& g = p.gates[gi];
		switch (g.op) {
			case Op::And: case Op::Xor:
				chk_read(g.in0, "gate in0");
				chk_read(g.in1, "gate in1");
				break;
			case Op::Not:
				chk_read(g.in0, "gate in0");
				// Canonical form: the unused operand is exactly 0.
				if (g.in1 != 0)
					throw std::runtime_error("validate_program: Not gate has non-canonical in1 (must be 0)" + at(gi, g.in1));
				break;
			case Op::Const0: case Op::Const1:
				if (g.in0 != 0 || g.in1 != 0)
					throw std::runtime_error("validate_program: const gate has non-canonical operands (must be 0)" + at(gi, g.out));
				break;
			default:
				throw std::runtime_error("validate_program: unknown gate op");
		}
		if (g.out >= NW)
			throw std::runtime_error("validate_program: gate out index out of range");
		if (g.out < p.num_inputs)
			throw std::runtime_error("validate_program: gate writes an input wire");
		const uint32_t out_idx = g.out - p.num_inputs;
		if (defined_non_input[out_idx])
			throw std::runtime_error("validate_program: wire defined more than once");
		defined_non_input[out_idx] = 1;
	}

	for (uint32_t w : p.outputs) {
		if (w >= NW) throw std::runtime_error("validate_program: output index out of range");
		if (w >= p.num_inputs && !defined_non_input[w - p.num_inputs])
			throw std::runtime_error("validate_program: output wire never defined");
	}
}

// ---------------------------------------------------------------------------
// The shared primitive: walk gates once, in order, dispatching each to the
// matching visitor method. This is the ONLY place the op switch lives.
//
// Visitor contract (methods take wire ids; the visitor owns the wire storage):
//   void and_gate(uint32_t out, uint32_t in0, uint32_t in1);
//   void xor_gate(uint32_t out, uint32_t in0, uint32_t in1);
//   void not_gate(uint32_t out, uint32_t in0);
//   void const_gate(uint32_t out, bool value);   // value: Const1 -> true
// Adding a new Op forces a new case here AND a new method on every visitor.
// ---------------------------------------------------------------------------
template <class Visitor>
inline void for_each_gate(const BooleanProgram& p, Visitor&& v) {
	for (const Gate& g : p.gates) {
		switch (g.op) {
			case Op::And:    v.and_gate(g.out, g.in0, g.in1); break;
			case Op::Xor:    v.xor_gate(g.out, g.in0, g.in1); break;
			case Op::Not:    v.not_gate(g.out, g.in0);        break;
			case Op::Const0: v.const_gate(g.out, false);      break;
			case Op::Const1: v.const_gate(g.out, true);       break;
		}
	}
}

// Reusable wire buffer so repeated executions of cached programs (e.g. float
// builtins) don't reallocate. Caller-owned or thread_local — never one shared
// mutable process-wide instance (immutable loaded *programs* may be shared, the
// scratch may not).
template <class Wire>
struct CircuitScratch {
	std::vector<Wire> wires;
	void ensure(uint32_t n) { if (wires.size() < n) wires.resize(n); }
};

// ---------------------------------------------------------------------------
// The in/out wrapper over for_each_gate, for callers whose wires carry values:
// copy `num_in` input values into wire slots [0, num_inputs), walk once, copy
// the output slots into `outputs`. Generic over the wire slot type and a
// Dispatcher that realizes each op on slots:
//
//   void and_gate(Wire& out, const Wire& a, const Wire& b);
//   void xor_gate(Wire& out, const Wire& a, const Wire& b);
//   void not_gate(Wire& out, const Wire& a);
//   void const_gate(Wire& out, bool value);
//
// (Pointer+count rather than std::span: the library floor is C++17.)
// ---------------------------------------------------------------------------
template <class Wire, class Dispatcher>
inline void execute_program(const BooleanProgram& p,
                            const Wire* inputs, size_t num_in,
                            Wire* outputs, size_t num_out,
                            CircuitScratch<Wire>& scratch,
                            Dispatcher&& dispatch) {
	if (num_in != p.num_inputs)
		throw std::runtime_error("execute_program: input count != program num_inputs");
	if (num_out != p.outputs.size())
		throw std::runtime_error("execute_program: output count != program outputs");

	scratch.ensure(p.num_wires);
	Wire* w = scratch.wires.data();
	for (size_t i = 0; i < num_in; ++i) w[i] = inputs[i];

	// Bridge wire ids (from for_each_gate) to value slots (for the Dispatcher).
	struct Bridge {
		Wire* w; Dispatcher& d;
		void and_gate(uint32_t o, uint32_t a, uint32_t b) { d.and_gate(w[o], w[a], w[b]); }
		void xor_gate(uint32_t o, uint32_t a, uint32_t b) { d.xor_gate(w[o], w[a], w[b]); }
		void not_gate(uint32_t o, uint32_t a)             { d.not_gate(w[o], w[a]); }
		void const_gate(uint32_t o, bool v)               { d.const_gate(w[o], v); }
	};
	for_each_gate(p, Bridge{w, dispatch});

	for (size_t i = 0; i < num_out; ++i) outputs[i] = w[p.outputs[i]];
}

}  // namespace circuit
}  // namespace emp
#endif  // EMP_CIRCUIT_BOOLEAN_PROGRAM_H__
