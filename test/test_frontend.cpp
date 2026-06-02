// The frontend's THREE execution modes, on the in-tree cleartext backend:
//   (3) DIRECT   — ordinary EMP code, evaluated as written.
//   (1) LIVE     — frontend::run(body, args): call a wire-generic body, get a
//                  typed result back; chain results; reveal after the call.
//   (2) COMPILED — frontend::compile(body, samples) -> Circuit (+ stats), then
//                  frontend::run(circuit, args) replays it; the same circuit is
//                  reused across many input sets.
// All three drive the same Backend through the same compile()/run() that drive
// AG2PC — here it's ClearBackend (plaintext). The focus is the modes' behavior
// (reuse, stats, chaining, reveal-outside), not type variety: we use UInt32
// throughout and add one Float line at the end just to show it isn't UInt-only.
#include "emp-tool/emp-tool.h"
#include "emp-tool/frontend/frontend.h"
EMP_USE_CIRCUIT_TYPES_ALL(block)
#include <cstdio>
using namespace std;
using namespace emp;
using namespace emp::frontend;

static bool all_ok = true;
static void check(const char *name, bool cond) {
	printf("  %-34s %s\n", name, cond ? "GOOD!" : "BAD!");
	if (!cond) all_ok = false;
}

// Templated functor: the C++17-compatible explicit-shape body spelling (a
// template member can't live in a function-local class, so it's here).
struct AddU32 {
	template <class W>
	UInt32_T<W> operator()(UInt32_T<W> x, UInt32_T<W> y) const { return x + y; }
};

int main() {
	setup_clear_backend("");

	auto add = [](auto x, auto y) { return x + y; };   // wire-generic bodies
	auto dbl = [](auto v)         { return v + v; };

	// ---- Mode 3: DIRECT — ordinary code, evaluated immediately ----
	{
		UInt32 a(32, 7, PUBLIC), b(32, 5, PUBLIC);
		UInt32 c = a + b;
		check("direct: a+b", c.reveal<uint32_t>(PUBLIC) == 12);
	}

	// ---- Mode 1: LIVE — run a body, get a typed result, chain it ----
	{
		UInt32 a(32, 7, PUBLIC), b(32, 5, PUBLIC);
		UInt32 c = run(add, a, b);             // typed result
		UInt32 d = run(dbl, c);                // chain c into the next run
		check("live: a+b",                 c.reveal<uint32_t>(PUBLIC) == 12);
		check("live: chained 2*(a+b)",     d.reveal<uint32_t>(PUBLIC) == 24);
	}

	// ---- Mode 2: COMPILED — compile once (with stats), run many ----
	{
		// Input shapes come from the TYPE arguments — no dummy values needed.
		auto circ = compile<UInt32, UInt32>(add);

		check("compiled: stat num_and==31", circ.circuit.count.num_and == 31);
		check("compiled: stat depth==31",   circ.circuit.schedule.levels.depth == 31);

		// Reuse the SAME compiled circuit with different inputs.
		UInt32 r1 = run(circ, UInt32(32, 7,   PUBLIC), UInt32(32, 5,  PUBLIC));
		UInt32 r2 = run(circ, UInt32(32, 100, PUBLIC), UInt32(32, 23, PUBLIC));
		check("compiled: run #1",            r1.reveal<uint32_t>(PUBLIC) == 12);
		check("compiled: run #2 (reused)",   r2.reveal<uint32_t>(PUBLIC) == 123);

		// Chain compiled circuits; reveal only at the end, outside the runs.
		auto dblc = compile<UInt32>(dbl);
		UInt32 z = run(circ, UInt32(32, 7, PUBLIC), UInt32(32, 5, PUBLIC));  // 12
		UInt32 w = run(dblc, z);                                            // 24
		check("compiled: chained 2*(a+b)",  w.reveal<uint32_t>(PUBLIC) == 24);
	}

	// ---- The three modes agree on the same computation ----
	{
		UInt32 a(32, 9, PUBLIC), b(32, 16, PUBLIC);
		uint32_t direct   = (a + b).reveal<uint32_t>(PUBLIC);
		uint32_t live     = run(add, a, b).reveal<uint32_t>(PUBLIC);
		uint32_t compiled = run(compile(add, a, b), a, b).reveal<uint32_t>(PUBLIC);
		check("direct == live == compiled",
		      direct == 25 && live == 25 && compiled == 25);
	}

	// ---- Switching between modes mid-computation ----
	// Each mode yields ordinary live wires on the same backend, so a value from
	// any mode flows straight into any other — no reveal/round-trip needed.
	{
		UInt32 a(32, 3, PUBLIC), b(32, 4, PUBLIC), c(32, 10, PUBLIC), e(32, 1, PUBLIC);
		auto   k = compile(add, a, b);          // a reusable 32-bit adder

		// direct -> live -> compiled -> direct
		UInt32 s = a + b;                        // DIRECT            -> 7
		UInt32 t = run(dbl, s);                  // LIVE     (uses s) -> 14
		UInt32 u = run(k, t, c);                 // COMPILED (uses t) -> 24
		UInt32 v = u + e;                        // DIRECT   (uses u) -> 25
		check("direct->live->compiled->direct", v.reveal<uint32_t>(PUBLIC) == 25);

		// the other way: compiled -> live -> direct
		UInt32 p = run(k, a, b);                 // COMPILED          -> 7
		UInt32 q = run(dbl, p);                  // LIVE     (uses p) -> 14
		UInt32 r = q + a;                        // DIRECT   (uses q) -> 17
		check("compiled->live->direct",         r.reveal<uint32_t>(PUBLIC) == 17);

		// a compiled output fed back through the SAME compiled circuit
		UInt32 g1 = run(k, a, b);                // 7
		UInt32 g2 = run(k, g1, c);               // 7 + 10 = 17  (reuses k, chained)
		check("compiled output -> same circuit", g2.reveal<uint32_t>(PUBLIC) == 17);
	}

	// ---- Body spellings: the body just has to be wire-generic ----
	{
		UInt32 a(32, 8, PUBLIC), b(32, 9, PUBLIC);   // -> 17

		// C++20 template lambda: explicit shape (UInt32_T<W>), wire W deduced.
		auto add_tl = []<class W>(UInt32_T<W> x, UInt32_T<W> y) { return x + y; };
		check("template lambda: live",
		      run(add_tl, a, b).reveal<uint32_t>(PUBLIC) == 17);
		check("template lambda: compiled",
		      run(compile<UInt32, UInt32>(add_tl), a, b).reveal<uint32_t>(PUBLIC) == 17);

		// Templated functor (defined at file scope): C++17-compatible spelling.
		check("functor: compiled",
		      run(compile<UInt32, UInt32>(AddU32{}), a, b).reveal<uint32_t>(PUBLIC) == 17);
	}

	// ---- Regression: a returned wire that an internal gate also read must not
	//      be marked freed (outputs are roots, needed at output assembly). ----
	{
		auto body = [](auto x) { auto dead = x + x; (void)dead; return x; };  // returns x
		auto c = compile<UInt32>(body);
		bool any_output_freed = false;
		for (int w : c.circuit.prog.outputs)
			for (const auto &g : c.circuit.layout.frees)
				for (int fw : g) if (fw == w) any_output_freed = true;
		check("layout: returned wires never freed", !any_output_freed);
	}

	// ---- Spot check that it isn't UInt-only (type variety lives elsewhere) ----
	{
		Float a(3.0f, PUBLIC), b(4.0f, PUBLIC);
		check("generality: Float compiled",
		      run(compile(add, a, b), a, b).reveal<double>(PUBLIC) == 7.0);
	}

	printf("test_frontend (cleartext): %s\n", all_ok ? "GOOD!" : "BAD!");
	return all_ok ? 0 : 1;
}
