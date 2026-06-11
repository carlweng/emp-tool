// Process-wide caches for the on-disk boolean-circuit builtins (crypto + float),
// see ir/builtins.h. Each is loaded once via the .empbc loader and cached.

#include "emp-tool/ir/builtins.h"
#include "emp-tool/ir/assets.h"
#include "emp-tool/ir/empbc.h"

#include <map>
#include <mutex>
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

	// On a missing/corrupt asset the callees error() out with the asset name and
	// the searched directories — fatal, so no context-wrapping layer here.
	std::string asset = key + ".empbc";
	std::string path = find_circuit_asset(asset);
	BooleanProgram prog = load_empbc_file(path.c_str());
	return cache.emplace(std::move(key), std::move(prog)).first->second;
}

const BooleanProgram& float_circuit(int width, const char* op) {
	static std::map<std::string, BooleanProgram> cache;
	static std::mutex mtx;
	std::string key = "fp" + std::to_string(width) + "_" + op;

	std::lock_guard<std::mutex> lk(mtx);
	auto it = cache.find(key);
	if (it != cache.end()) return it->second;

	std::string asset = key + ".empbc";
	std::string path = find_circuit_asset(asset);
	BooleanProgram prog = load_empbc_file(path.c_str());
	// emplace never invalidates existing nodes, so the returned reference
	// stays valid after the lock is released and across later inserts.
	return cache.emplace(std::move(key), std::move(prog)).first->second;
}

}  // namespace circuit
}  // namespace emp
