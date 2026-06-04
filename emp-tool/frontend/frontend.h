#ifndef EMP_FRONTEND_H__
#define EMP_FRONTEND_H__

// Umbrella for the protocol-neutral circuit frontend. A circuit is a PURE,
// wire-generic function — typed EMP arguments in, an EMP value out, no I/O
// inside. frontend::run(body, args...) calls it live; frontend::compile<...>(body)
// records it once (with stats) and frontend::run(circuit, args...) replays it
// through whatever Backend is installed. See docs/frontend.md.
//
// Opt-in (NOT pulled by emp-tool.h). The suffixed `*_rec` aliases below are an
// INTERNAL detail of recording (the wire a body is traced on); user code passes
// ordinary live values and need not name them.

#include "emp-tool/frontend/boolean_program.h"
#include "emp-tool/frontend/record_backend.h"
#include "emp-tool/frontend/passes.h"
#include "emp-tool/frontend/circuit.h"
#include "emp-tool/frontend/executor.h"

// Backend-independent circuit templates (Bit_T<Wire>, UnsignedInt_T<Wire,N>, …)
// plus the EMP_CIRCUIT_TYPES_* binding macros.
#include "emp-tool/circuits/circuit.h"
#include "emp-tool/circuits/circuit_types.h"

// Internal recording aliases bound to RecWire, suffixed with _rec so they never
// clash with a protocol's bare Bit/integer aliases in the same translation unit.
// These are the wire a body is traced on during compile(); ordinary user code
// does not name them (it passes live values and gets a live value back).
namespace emp {
EMP_CIRCUIT_TYPES_ALL_AS(emp::frontend::RecWire, _rec)
}  // namespace emp

#endif  // EMP_FRONTEND_H__
