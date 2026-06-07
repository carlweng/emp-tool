// Process-wide cache for the on-disk boolean-circuit builtins (see
// builtin_circuit_files.h). Same shape as float_circuit_files.cpp.

#include "emp-tool/circuits/builtin_circuit_files.h"
#include "emp-tool/circuits/circuit_assets.h"
#include "emp-tool/circuits/empbc.h"

#include <map>
#include <mutex>
#include <stdexcept>
#include <string>

namespace emp {
namespace circuit {

const BooleanProgram& builtin_circuit(const char* name) {
	static std::map<std::string, BooleanProgram> cache;
	static std::mutex mtx;
	std::string key = name;

	std::lock_guard<std::mutex> lk(mtx);
	auto it = cache.find(key);
	if (it != cache.end()) return it->second;

	std::string asset = key + ".empbc";
	try {
		std::string path = find_circuit_asset(asset);
		BooleanProgram prog = load_empbc_file(path.c_str());
		return cache.emplace(std::move(key), std::move(prog)).first->second;
	} catch (const std::exception& e) {
		throw std::runtime_error(
		    std::string("builtin_circuit: could not load ") + asset + " (" + e.what() + ")");
	}
}

}  // namespace circuit
}  // namespace emp
