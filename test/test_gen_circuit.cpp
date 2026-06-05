#include "emp-tool/emp-tool.h"
#include "emp-tool/circuits/empbc.h"
#include <cstdio>
#include <vector>

// ClearBackend's circuit-CAPTURE path: run a computation with a filename set,
// and the executed circuit is written as a native .empbc BooleanProgram.
// Here we capture, reload, validate, and re-execute the reloaded program on the
// plaintext backend, checking it reproduces the original
// outputs — including public-constant reveals (which become real Const gates,
// not the old z/o-index XOR trailer).

using namespace emp::block_types;
using namespace emp;
using emp::circuit::BooleanProgram;
using emp::circuit::CircuitScratch;

// Compute dispatcher over Bit slots: each op uses the Bit operators, which
// dispatch to the active (plaintext) backend.
struct BitCompute {
	void and_gate(Bit& o, const Bit& a, const Bit& b) { o = a & b; }
	void xor_gate(Bit& o, const Bit& a, const Bit& b) { o = a ^ b; }
	void not_gate(Bit& o, const Bit& a)               { o = !a; }
	void const_gate(Bit& o, bool v)                   { backend->public_label(&o.bit, v); }
};

static std::vector<bool> run_empbc(const BooleanProgram& p, const std::vector<bool>& inb) {
	std::vector<Bit> in(p.num_inputs), out(p.outputs.size());
	for (uint32_t i = 0; i < p.num_inputs; ++i) in[i] = Bit(inb[i], ALICE);
	CircuitScratch<Bit> sc;
	emp::circuit::execute_program<Bit>(p, in.data(), in.size(), out.data(), out.size(), sc, BitCompute{});
	std::vector<bool> r(out.size());
	for (size_t i = 0; i < out.size(); ++i) r[i] = out[i].reveal();
	return r;
}

// Reveal one Alice bit and two public constants; the captured program must
// reproduce {a, 1, 0} for either value of a — the case where a public-constant
// reveal must NOT leak input wire 0.
static void const_reveal_roundtrip() {
	const char* fname = "const_reveal.empbc";
	setup_clear_backend(fname);
	Bit a(false, ALICE);
	Bit one(true, PUBLIC);
	Bit zero(false, PUBLIC);
	a.reveal(); one.reveal(); zero.reveal();
	finalize_clear_backend();

	BooleanProgram p = emp::circuit::load_empbc_file(fname);
	if (p.num_inputs != 1 || p.outputs.size() != 3)
		error("const_reveal: unexpected shape");

	for (bool a_val : {false, true}) {
		setup_clear_backend();
		std::vector<bool> got = run_empbc(p, {a_val});
		finalize_clear_backend();
		if (got[0] != a_val || got[1] != true || got[2] != false) {
			printf("a_in=%d got=%d,%d,%d expected=%d,1,0\n",
			       a_val, (int)got[0], (int)got[1], (int)got[2], a_val);
			error("const_reveal: captured circuit mishandles public constants");
		}
	}
	std::remove(fname);
	printf("const_reveal_roundtrip: success\n");
}

// Capture a non-trivial circuit (XOR then sort), reload it, and check the
// reloaded program reproduces the directly-evaluated outputs bit-for-bit.
static void capture_roundtrip() {
	const char* fname = "gen.empbc";
	const int n = 8, w = 32;

	auto inputs_flat = [&]() {
		std::vector<bool> inb;
		for (int i = 0; i < n; ++i) {
			for (int b = 0; b < w; ++b) inb.push_back((((n - i) >> b) & 1) != 0);
			for (int b = 0; b < w; ++b) inb.push_back(((i >> b) & 1) != 0);
		}
		return inb;
	};

	// Direct evaluation (no capture) to get the reference outputs.
	std::vector<bool> ref;
	{
		setup_clear_backend();
		std::vector<UInt32> A(n);
		for (int i = 0; i < n; ++i)
			A[i] = UInt32(w, (uint32_t)(n - i), ALICE) ^ UInt32(w, (uint32_t)i, ALICE);
		sort(A.data(), n);
		for (int i = 0; i < n; ++i)
			for (int b = 0; b < w; ++b) ref.push_back(A[i][b].reveal());
		finalize_clear_backend();
	}

	// Capture the same computation. ALL inputs are fed first (every UInt32
	// constructor), THEN the XOR + sort gates run — so inputs stay the leading
	// wires [0, num_inputs), as the capture path requires. (Interleaving a feed
	// after a gate would make validate_program reject the capture.)
	setup_clear_backend(fname);
	{
		std::vector<UInt32> X, Y;
		X.reserve(n); Y.reserve(n);
		for (int i = 0; i < n; ++i) {
			X.emplace_back(w, (uint32_t)(n - i), ALICE);
			Y.emplace_back(w, (uint32_t)i, ALICE);
		}
		std::vector<UInt32> A(n);
		for (int i = 0; i < n; ++i) A[i] = X[i] ^ Y[i];
		sort(A.data(), n);
		for (int i = 0; i < n; ++i) A[i].reveal<std::string>();
	}
	finalize_clear_backend();

	BooleanProgram p = emp::circuit::load_empbc_file(fname);
	if (p.num_inputs != (uint32_t)(2 * n * w))
		error("capture: unexpected input count");

	setup_clear_backend();
	std::vector<bool> got = run_empbc(p, inputs_flat());
	finalize_clear_backend();

	if (got.size() != ref.size())
		error("capture: output size mismatch");
	for (size_t i = 0; i < ref.size(); ++i)
		if (got[i] != ref[i]) error("capture: reloaded circuit disagrees with direct eval");
	std::remove(fname);
	printf("capture_roundtrip: success (%zu inputs, %zu outputs)\n",
	       (size_t)p.num_inputs, p.outputs.size());
}

int main() {
	const_reveal_roundtrip();
	capture_roundtrip();
	printf("test_gen_circuit: all checks passed\n");
	return 0;
}
