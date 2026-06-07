#ifndef EMP_FRONTEND_CIRCUIT_H__
#define EMP_FRONTEND_CIRCUIT_H__

// RETIRED. The old non-template `emp::frontend::Circuit` (a recorded
// BooleanProgram + analysis stats) collided with the compiled-circuit type of the
// BooleanContext frontend: `emp::frontend::Circuit<RetV, ArgVs...>` in
// emp-tool/frontend/circuit_fn.h, produced by `compile<rec::UInt<32>, ...>(body)`.
// Use that instead; for the raw program + analysis stats, work with
// emp::circuit::BooleanProgram and the passes in emp-tool/frontend/passes.h.

#error "emp-tool/frontend/circuit.h is retired; use emp-tool/frontend/circuit_fn.h (Circuit<RetV, ArgVs...> from compile<rec::...>(body)). See this header comment."

#endif  // EMP_FRONTEND_CIRCUIT_H__
