#ifndef EMP_FRONTEND_CIRCUIT_H__
#define EMP_FRONTEND_CIRCUIT_H__

// A compiled circuit: the recorded BooleanProgram plus the analysis stats,
// computed once so they amortize across many runs (replays).

#include "emp-tool/frontend/boolean_program.h"
#include "emp-tool/frontend/passes.h"
#include <vector>

namespace emp {
namespace frontend {

struct Circuit {
	BooleanProgram prog;
	CountStats     count;
	LivenessStats  liveness;
	ScheduleStats  schedule;
	LayoutStats    layout;
};

// Per-party cleartext input bits, keyed by input-port index. An empty entry
// means "use the port's recorded fed_bits"; a non-empty entry overrides it
// (compiled circuit re-run with fresh values). Bits a party does not own are
// ignored by the protocol regardless.
struct BoundInputs {
	std::vector<std::vector<bool>> bits;   // bits[port_index]; may be shorter / sparse
};

}  // namespace frontend
}  // namespace emp
#endif  // EMP_FRONTEND_CIRCUIT_H__
