#ifndef EMP_FRONTEND_BOOLEAN_PROGRAM_H__
#define EMP_FRONTEND_BOOLEAN_PROGRAM_H__

// The frontend builds on the canonical core IR (emp::circuit::BooleanProgram in
// circuits/boolean_program.h) and re-exports its names here. The core IR is
// flat: inputs are wires [0, num_inputs), with no argument boundaries baked into
// the program. The typed frontend carries per-argument widths in the compiled
// circuit's CircuitSignature (circuit_artifact.h: arg_widths / return_width),
// separate from the program.
//
// This header adds only the frontend-local analysis metadata that the passes
// produce (LevelInfo, consumed by schedule_pass).

#include "emp-tool/circuits/boolean_program.h"
#include <vector>

namespace emp {
namespace frontend {

using emp::circuit::BooleanProgram;
using emp::circuit::Gate;
using emp::circuit::Op;

// AND-depth scheduling metadata (filled by schedule_pass). Populated now for
// future GMW round batching / depth-minimization; unused by constant-round
// backends.
struct LevelInfo {
	std::vector<std::vector<int>> and_gate_indices;  // gate indices per AND-depth level
	int depth = 0;                                   // max AND-depth
};

}  // namespace frontend
}  // namespace emp
#endif  // EMP_FRONTEND_BOOLEAN_PROGRAM_H__
