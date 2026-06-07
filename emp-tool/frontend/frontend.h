#ifndef EMP_FRONTEND_H__
#define EMP_FRONTEND_H__

// Umbrella for the circuit-function frontend over the C++20 BooleanContext model:
// write a PURE circuit body once, compile() it into a context-free Circuit<Sig>,
// and run() it on ANY context (plaintext / garbled 2PC / ZK / ...), exactly like
// the built-in .empbc circuits. Typed circuit values (Bit/UInt/Int/Float<Ctx>)
// come from circuits/typed.h; context-free shapes from circuits/shape.h.
//
// The legacy Bit_T / global-Backend frontend (frontend/executor.h) is RETIRED;
// downstream protocols (ag2pc / agmpc / zk) migrate onto this surface.

#include "emp-tool/frontend/circuit_fn.h"

#endif  // EMP_FRONTEND_H__
