#ifndef EMP_CONTEXT_CONTEXT_H__
#define EMP_CONTEXT_CONTEXT_H__

// Convenience umbrella: the BooleanContext concept and every reusable context
// (ClearCtx / CountCtx / DigestCtx / RecordCtx), plus IR replay over a context
// (execute_program / scheduled_execute_program). Include this when you want "the
// contexts and the ability to replay an ir program through one"; include the
// individual context/*.h or ir/*.h headers for finer-grained dependencies.

#include "emp-tool/ir/context/concept.h"
#include "emp-tool/ir/context/clear.h"
#include "emp-tool/ir/context/count.h"
#include "emp-tool/ir/context/digest.h"
#include "emp-tool/ir/context/record.h"
#include "emp-tool/ir/execute.h"
#include "emp-tool/ir/schedule.h"

#endif  // EMP_CONTEXT_CONTEXT_H__
