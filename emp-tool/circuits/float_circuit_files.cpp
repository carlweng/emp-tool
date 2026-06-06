// Process-wide cache for the on-disk float circuits.

#include "emp-tool/circuits/boolean_program.h"
#include "emp-tool/circuits/circuit_assets.h"
#include "emp-tool/circuits/empbc.h"

#include <map>
#include <mutex>
#include <stdexcept>
#include <string>

namespace emp {
namespace circuit {

const BooleanProgram& float_circuit(int width, const char* op) {
	static std::map<std::string, BooleanProgram> cache;
	static std::mutex mtx;
	std::string key = "fp" + std::to_string(width) + "_" + op;

	std::lock_guard<std::mutex> lk(mtx);
	auto it = cache.find(key);
	if (it != cache.end()) return it->second;

	std::string asset = key + ".empbc";
	try {
		std::string path = find_circuit_asset(asset);
		BooleanProgram prog = load_empbc_file(path.c_str());
		// emplace never invalidates existing nodes, so the returned reference
		// stays valid after the lock is released and across later inserts.
		return cache.emplace(std::move(key), std::move(prog)).first->second;
	} catch (const std::exception& e) {
		throw std::runtime_error(
		    std::string("float_circuit: could not load ") + asset + " (" + e.what() + ")");
	}
}

}  // namespace circuit
}  // namespace emp
