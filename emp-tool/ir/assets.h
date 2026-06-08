#ifndef EMP_IR_ASSETS_H__
#define EMP_IR_ASSETS_H__

// Shared runtime resolver for persisted circuit assets. File-backed builtins
// should not bake their own path policy; ask find_circuit_asset("name.empbc").
// Search order:
//   1. EMP_CIRCUIT_DIR, when set
//   2. build/source-tree assets, for local development and sibling repos
//   3. install-tree assets beside the installed emp-tool headers

#include <string>
#include <vector>

namespace emp {
namespace circuit {

const std::vector<std::string>& circuit_asset_dirs();
std::string find_circuit_asset(const std::string& filename);

}  // namespace circuit
}  // namespace emp

#endif  // EMP_IR_ASSETS_H__
