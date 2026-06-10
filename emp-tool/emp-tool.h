#ifndef EMP_TOOL_H__
#define EMP_TOOL_H__

// emp-tool — the complete public umbrella over the three layers:
//   runtime  — the substrate: core / crypto / io / garbling leaf primitives.
//   ir       — context-free Boolean IR, the reusable contexts, and the generic
//              WireValue + session (Session / DirectSession / SessionIO) contracts.
//   circuits — concrete value families, numeric kernels, sort, in-circuit crypto,
//              and the compile/run frontend.
//
// Normal users include this header. Code that needs only one layer includes that
// layer's umbrella (runtime/runtime.h, ir/ir.h, circuits/circuits.h); internal
// headers include the narrowest layer header or direct dependency they need, never
// this top umbrella.

#include "emp-tool/runtime/runtime.h"
#include "emp-tool/ir/ir.h"
#include "emp-tool/circuits/circuits.h"

#endif  // EMP_TOOL_H__
