#ifndef EMP_CIRCUIT_BUILTIN_CIRCUIT_FILES_H__
#define EMP_CIRCUIT_BUILTIN_CIRCUIT_FILES_H__

// Disk-backed cache of the large hand-written boolean-circuit builtins recorded
// to IR (AES-128, SHA-256, SHA3-256, …) — the "big circuits run as IR replay"
// rule. Each `<name>.empbc` in emp-tool/circuits/files/ is loaded, validated,
// and cached once, then replayed through any BooleanContext. Mirrors
// float_circuit_files.h; see tools/record_builtins.cpp for the generator.

#include "emp-tool/circuits/boolean_program.h"

namespace emp {
namespace circuit {

// Load-or-fetch the cached builtin circuit by asset name (e.g. "aes128",
// "sha256_256", "sha3_256_256"). Throws (pointing at EMP_CIRCUIT_DIR) if absent.
const BooleanProgram& builtin_circuit(const char* name);

}  // namespace circuit
}  // namespace emp
#endif  // EMP_CIRCUIT_BUILTIN_CIRCUIT_FILES_H__
