// core/utils.h, core/utils.hpp — small free-standing helpers. Throughput
// benchmark.
//
// What's in utils.h/utils.hpp (the parts worth testing):
//   bool_to_int<T>(const bool *)   pack 8*sizeof(T) bools (LSB-first) into T
//   bool_to_block(const bool *)    pack 128 bools into a block

#include "emp-tool/emp-tool.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace emp;
using namespace std;
using clk = chrono::high_resolution_clock;

// ---------- throughput bench ----------

template <typename Fn>
static double run_for(double seconds, Fn &&fn, void *clob) {
	for (int i = 0; i < 32; ++i) {
		fn();
		asm volatile("" : "+m"(*(char *)clob) : : "memory");
	}
	int64_t iters = 64;
	while (true) {
		auto a = clk::now();
		for (int64_t i = 0; i < iters; ++i) {
			fn();
			asm volatile("" : "+m"(*(char *)clob) : : "memory");
		}
		double el = chrono::duration<double>(clk::now() - a).count();
		if (el >= seconds) return double(iters) / el;
		iters *= 2;
	}
}

static void print_op(const string &lbl, double calls) {
	double Mops = calls / 1e6;
	cout << "  " << left << setw(36) << lbl
	     << fixed << setprecision(2)
	     << right << setw(8) << Mops << " Mops  "
	     << setw(7) << (3e9 / calls) << " cy/op @3GHz\n";
}

static void bench(double sec) {
	PRG prg;
	cout << "=== single-shot (chained-dep) ===\n";
	{
		bool bits[64];
		prg.random_bool(bits, 64);
		uint64_t sink = 0;
		double calls = run_for(sec, [&]() {
			uint64_t v = bool_to_int<uint64_t>(bits);
			sink ^= v;
			bits[v & 63] ^= 1;  // serial dep on output
		}, &sink);
		print_op("bool_to_int<uint64_t>", calls);
	}
	{
		alignas(16) bool bits[128];
		prg.random_bool(bits, 128);
		alignas(16) block sink = zero_block;
		double calls = run_for(sec, [&]() {
			block r = bool_to_block(bits);
			sink = sink ^ r;
			// chain: tweak one input bit using output's LSB
			bits[0] ^= getLSB(sink);
		}, &sink);
		print_op("bool_to_block", calls);
	}
}

int main(int argc, char **argv) {
	double sec = (argc >= 2) ? atof(argv[1]) : 0.2;
	bench(sec);
	return 0;
}
