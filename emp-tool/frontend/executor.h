#ifndef EMP_FRONTEND_EXECUTOR_H__
#define EMP_FRONTEND_EXECUTOR_H__

// RETIRED. The legacy Bit_T / global-Backend circuit frontend (compile / run /
// circuit_fn_traits over emp::CircuitValue) has been replaced by the C++20
// BooleanContext frontend in emp-tool/frontend/circuit_fn.h
// (emp::frontend::compile / run with the context-free shapes in
// emp-tool/circuits/shape.h). Port this consumer onto BooleanContext:
//   * arguments become context-free SHAPES: compile<UIntShape<32>, ...>(body)
//   * bodies are pure functions of typed values: [](auto a, auto b){ return a+b; }
//   * replay is explicit-context: frontend::run(ctx, circuit, args...)
// ag2pc / agmpc / zk migration is pending — see the plan.

#error "emp-tool/frontend/executor.h is retired; use emp-tool/frontend/circuit_fn.h (BooleanContext compile/run with circuits/shape.h). See this header comment for the migration."

#endif  // EMP_FRONTEND_EXECUTOR_H__
