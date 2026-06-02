#ifndef EMP_FRONTEND_H__
#define EMP_FRONTEND_H__

// Umbrella for the protocol-neutral circuit frontend: record ordinary emp-tool
// circuit code into a BooleanProgram, annotate it with stats, and run/compile
// it through a protocol-specific Executor. Opt-in (NOT pulled by emp-tool.h):
// it binds RecWire-typed circuit aliases, which would otherwise collide with a
// protocol's own bare aliases (e.g. ag2pc's Bit = Bit_T<AG2PCWire>).

#include "emp-tool/frontend/boolean_program.h"
#include "emp-tool/frontend/record_backend.h"
#include "emp-tool/frontend/passes.h"
#include "emp-tool/frontend/circuit.h"
#include "emp-tool/frontend/executor.h"

// Backend-independent circuit templates (Bit_T<Wire>, UnsignedInt_T<Wire,N>, …)
// plus the EMP_USE_CIRCUIT_TYPES_* binding macros.
#include "emp-tool/circuits/circuit.h"
#include "emp-tool/circuits/circuit_types.h"

// Convenience aliases bound to RecWire, suffixed with _rec so they never clash
// with a protocol's bare Bit/Integer aliases in the same translation unit.
// Write bodies as `Bit_rec`, `UInt32_rec`, … (or `Bit_T<emp::frontend::RecWire>`
// directly). The macro injects these into namespace emp.
EMP_USE_CIRCUIT_TYPES_AS(emp::frontend::RecWire, _rec,
	Bit, BitVec, UnsignedInt, SignedInt, Float,
	UInt8, UInt16, UInt32, UInt64, Int8, Int16, Int32, Int64)

#endif  // EMP_FRONTEND_H__
