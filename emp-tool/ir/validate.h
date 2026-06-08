#ifndef EMP_IR_VALIDATE_H__
#define EMP_IR_VALIDATE_H__

// Validation for the Boolean-circuit IR. ONE path, run at construction / load /
// before executing an externally supplied program — never inside the execution
// hot loop. Throws std::runtime_error on the first violation. Enforces the IR's
// invariants so every downstream consumer (frontend replay, float builtins,
// ag2pc) may trust the structure without re-checking.

#include "emp-tool/ir/program.h"
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace emp {
namespace circuit {

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

}  // namespace circuit
}  // namespace emp
#endif  // EMP_IR_VALIDATE_H__
