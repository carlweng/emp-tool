// Test for circuits/boolean_program.h + circuits/empbc.h — the canonical IR,
// its validator, the for_each_gate / execute_program execution split, and the
// .empbc codec (u16/u32 roundtrip + malformed rejection). No backend needed:
// these are pure data-structure / serialization checks.

#include "emp-tool/circuits/boolean_program.h"
#include "emp-tool/circuits/empbc.h"
#include <cassert>
#include <cstdio>
#include <stdexcept>
#include <vector>

using namespace emp::circuit;

static bool throws(void (*fn)(const BooleanProgram&), const BooleanProgram& p) {
	try { fn(p); } catch (const std::runtime_error&) { return true; }
	return false;
}

// A 2-input program: out = (a AND b) XOR 1, plus a passthrough of `a`.
// wires: 0,1 inputs; 2 = a&b; 3 = const1; 4 = 2 ^ 3. outputs = {4, 0}.
static BooleanProgram sample() {
	BooleanProgram p;
	p.num_wires  = 5;
	p.num_inputs = 2;
	p.gates = {
		Gate{0, 1, 2, Op::And},
		Gate{0, 0, 3, Op::Const1},
		Gate{2, 3, 4, Op::Xor},
	};
	p.outputs = {4, 0};
	return p;
}

// Compute dispatcher over plain bytes (0/1) so we can evaluate concretely.
struct ByteDispatcher {
	void and_gate(uint8_t& o, const uint8_t& a, const uint8_t& b) { o = a & b; }
	void xor_gate(uint8_t& o, const uint8_t& a, const uint8_t& b) { o = a ^ b; }
	void not_gate(uint8_t& o, const uint8_t& a)                   { o = a ^ 1; }
	void const_gate(uint8_t& o, bool v)                           { o = v ? 1 : 0; }
};

int main() {
	bool ok = true;
	auto check = [&](bool c, const char* msg) {
		if (!c) { printf("FAIL: %s\n", msg); ok = false; }
	};

	// ---- validation: the well-formed program passes ----
	BooleanProgram p = sample();
	try { validate_program(p); } catch (...) { check(false, "valid program rejected"); }

	// ---- validation: each invariant is enforced ----
	{ BooleanProgram b = sample(); b.gates[0].in1 = 99;  check(throws(validate_program, b), "in-range read not enforced"); }
	{ BooleanProgram b = sample(); b.outputs[0] = 99;    check(throws(validate_program, b), "output bound not enforced"); }
	{ BooleanProgram b = sample(); b.gates[2].out = 0;   check(throws(validate_program, b), "write-to-input not enforced"); }
	{ BooleanProgram b = sample(); b.gates[2].out = 2;   check(throws(validate_program, b), "single-definition not enforced"); }
	{ BooleanProgram b = sample(); b.gates[0].in0 = 4;   check(throws(validate_program, b), "read-before-define not enforced"); }
	{ BooleanProgram b = sample(); b.gates[1].in0 = 1;   check(throws(validate_program, b), "non-canonical const operand not rejected"); }  // gate 1 is Const1
	{ BooleanProgram b; b.num_wires = 2; b.num_inputs = 1; b.gates = { Gate{0, 7, 1, Op::Not} }; b.outputs = {1};
	  check(throws(validate_program, b), "non-canonical Not in1 not rejected"); }
	{ BooleanProgram b; b.num_wires = 3; b.num_inputs = 1;
	  b.gates = { Gate{0, 0, 1, Op::Xor} }; b.outputs = {1};  // wire 2 counted but never defined
	  check(throws(validate_program, b), "non-dense program (hole) not rejected"); }

	// ---- execute_program matches a hand evaluation for all 4 input combos ----
	for (int a = 0; a < 2; ++a) for (int b = 0; b < 2; ++b) {
		uint8_t in[2]  = { (uint8_t)a, (uint8_t)b };
		uint8_t out[2] = { 0, 0 };
		CircuitScratch<uint8_t> sc;
		execute_program<uint8_t>(p, in, 2, out, 2, sc, ByteDispatcher{});
		check(out[0] == ((uint8_t)((a & b) ^ 1)), "execute_program: out0 wrong");
		check(out[1] == (uint8_t)a,                "execute_program: passthrough wrong");
	}

	// ---- .empbc roundtrip (u16 form, since num_wires is tiny) ----
	{
		std::vector<uint8_t> bytes = save_empbc(p);
		check(bytes[4] == 1 && bytes[5] == 0, "empbc version mismatch");
		check(bytes[6] == 2, "small program should use 16-bit index width");
		BooleanProgram q = load_empbc(bytes);
		check(q.num_wires == p.num_wires && q.num_inputs == p.num_inputs, "u16 roundtrip header");
		check(q.gates.size() == p.gates.size() && q.outputs == p.outputs,  "u16 roundtrip body");
		check(q.gates[2].op == Op::Xor && q.gates[2].in0 == 2,             "u16 roundtrip gate");
	}

	// ---- .empbc u32 form: force a wide program (>65535 wires) ----
	{
		BooleanProgram big;
		big.num_inputs = 2;
		// One XOR chain long enough to push past the 16-bit boundary.
		uint32_t prev0 = 0, prev1 = 1, w = 2;
		const uint32_t N = 70000;
		for (uint32_t i = 0; i < N; ++i) { big.gates.push_back(Gate{prev0, prev1, w, Op::Xor}); prev1 = prev0; prev0 = w; ++w; }
		big.num_wires = w;
		big.outputs = { w - 1 };
		std::vector<uint8_t> bytes = save_empbc(big);
		check(bytes[6] == 4, "wide program should use 32-bit index width");
		BooleanProgram q = load_empbc(bytes);
		check(q.num_wires == big.num_wires && q.gates.size() == N, "u32 roundtrip");
	}

	// ---- malformed/truncated .empbc are rejected ----
	{
		std::vector<uint8_t> bytes = save_empbc(p);
		auto rejects = [&](std::vector<uint8_t> b) {
			try { load_empbc(b); } catch (const std::runtime_error&) { return true; } return false;
		};
		check(rejects({}),                                         "empty buffer not rejected");
		{ auto b = bytes; b[0] = 'X';            check(rejects(b), "bad magic not rejected"); }
		{ auto b = bytes; b.pop_back();          check(rejects(b), "truncated tail not rejected"); }
		{ auto b = bytes; b.push_back(0);        check(rejects(b), "trailing byte not rejected"); }
		{ auto b = bytes; b[6] = 7;              check(rejects(b), "bad index_width not rejected"); }
		// Corrupt an op code (first gate's op byte: header 24 + 3*2 = byte 30).
		{ auto b = bytes; b[24 + 3 * 2] = 0x7F;  check(rejects(b), "bad op code not rejected"); }
	}

	if (ok) printf("test_boolean_program: all checks passed\n");
	return ok ? 0 : 1;
}
