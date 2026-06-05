#ifndef EMP_FRONTEND_BOOLEAN_PROGRAM_H__
#define EMP_FRONTEND_BOOLEAN_PROGRAM_H__

// The frontend builds on the canonical core IR (emp::circuit::BooleanProgram in
// circuits/boolean_program.h) and re-exports its names here. The core IR is
// flat: inputs are wires [0, num_inputs), there is no InputPort, no argument
// boundaries, no num_and. The typed frontend carries argument shapes on
// TypedCircuit (executor.h: arg_widths / return_width), NOT in the program.
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
