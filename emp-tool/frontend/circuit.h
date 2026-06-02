#ifndef EMP_FRONTEND_CIRCUIT_H__
#define EMP_FRONTEND_CIRCUIT_H__

// A compiled circuit: the recorded BooleanProgram plus the analysis stats,
// computed once so they amortize across many runs (replays).

#include "emp-tool/frontend/boolean_program.h"
#include "emp-tool/frontend/passes.h"

namespace emp {
namespace frontend {

struct Circuit {
	BooleanProgram prog;
	CountStats     count;
	LivenessStats  liveness;
	ScheduleStats  schedule;
	LayoutStats    layout;
};

}  // namespace frontend
}  // namespace emp
#endif  // EMP_FRONTEND_CIRCUIT_H__
