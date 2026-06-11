#ifndef EMP_IR_BUILTINS_H__
#define EMP_IR_BUILTINS_H__

// Disk-backed cache of the large boolean-circuit builtins stored as IR (AES-128,
// SHA-256, SHA3-256, and the per-width float ops) — the "big circuits run as IR
// replay" rule. Each `<name>.empbc` in emp-tool/ir/files/ is loaded, validated,
// and cached once, then replayed through any BooleanContext (ir/execute.h). The
// matching context-generic kernels live in circuits/crypto/{aes128,sha256,
// keccak}.h; the float ops are reached through circuits/float_traits.h.

#include "emp-tool/ir/program.h"

namespace emp {
namespace circuit {

// Load-or-fetch the cached builtin circuit by asset name (e.g. "aes128",
// "sha256_256", "sha3_256_256"). error()s (pointing at EMP_CIRCUIT_DIR) if absent.
const BooleanProgram& builtin_circuit(const char* name);

// Load-or-fetch the cached float op circuit "fp<width>_<op>" (e.g. width 32,
// op "add" -> fp32_add). error()s (pointing at EMP_CIRCUIT_DIR) if absent.
const BooleanProgram& float_circuit(int width, const char* op);

}  // namespace circuit
}  // namespace emp
#endif  // EMP_IR_BUILTINS_H__
