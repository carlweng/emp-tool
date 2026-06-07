#ifndef EMP_CIRCUIT_ARTIFACT_H__
#define EMP_CIRCUIT_ARTIFACT_H__

// The canonical persisted/compiled circuit: a BooleanProgram plus the argument
// shape needed to feed it. NO baked analyses — count/liveness/schedule/layout
// are free functions over the program (frontend/passes.h), cached separately by
// the optional frontend `Circuit` wrapper when wanted. C++17 (lib-includable).

#include "emp-tool/circuits/boolean_program.h"
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace emp {
namespace circuit {

// Argument boundaries the flat IR doesn't carry: each argument's bit width and
// the return value's width. total_input_bits() must equal program.num_inputs;
// return_width must equal program.outputs.size().
struct CircuitSignature {
	std::vector<uint32_t> arg_widths;
	uint32_t return_width = 0;

	uint64_t total_input_bits() const {
		uint64_t s = 0; for (uint32_t w : arg_widths) s += w; return s;
	}
};

struct CircuitArtifact {
	BooleanProgram   program;
	CircuitSignature signature;
};

// Structural + signature consistency. Runs validate_program() and checks the
// signature matches the program's I/O. Throws std::runtime_error on mismatch.
inline void validate_artifact(const CircuitArtifact& a) {
	validate_program(a.program);
	const uint64_t ins = a.signature.total_input_bits();   // compared in 64-bit (no truncation)
	if (ins > UINT32_MAX)
		throw std::runtime_error("validate_artifact: total_input_bits exceeds UINT32_MAX");
	if (ins != (uint64_t)a.program.num_inputs)
		throw std::runtime_error("validate_artifact: sum(arg_widths) != program.num_inputs");
	if ((uint64_t)a.signature.return_width != (uint64_t)a.program.outputs.size())
		throw std::runtime_error("validate_artifact: return_width != program.outputs size");
}

}  // namespace circuit
}  // namespace emp
#endif  // EMP_CIRCUIT_ARTIFACT_H__
