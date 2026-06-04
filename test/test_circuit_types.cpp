// Test for circuits/circuit_types.h — the EMP_CIRCUIT_TYPES_ALL binding macro.
//
// emp-tool defines no bare circuit aliases in namespace emp by default; each
// consumer binds the circuit templates to its backend's wire at the point it
// chooses the backend. This test shows binding the standard set, and a second
// suffixed binding so two wires can coexist without name clashes.

#include "emp-tool/emp-tool.h"
#include <cstdint>
#include <cstdio>
#include <vector>

namespace emp {

// This program's backend (ClearBackend / HalfGate) uses the `block` wire, so
// bind the standard set in the namespace where we want the names.
EMP_CIRCUIT_TYPES_ALL(block)

// A suffixed set: same wire here, but in a real multi-backend build this would
// be a different wire (e.g. an authenticated-GC two-block label). Distinct
// names (Bit_g, UInt32_g) so both bindings can live in one TU.
EMP_CIRCUIT_TYPES_ALL_AS(block, _g)

}  // namespace emp

using namespace emp;

int main() {
	setup_clear_backend();
	bool ok = true;

	// Bare-name aliases produced by the macro behave exactly as before.
	UInt32 a(7u, ALICE), b(3u, BOB);
	ok &= ((a + b).reveal<uint32_t>(PUBLIC) == 10u);

	Bit flag(true, ALICE);
	ok &= (flag.reveal<bool>(PUBLIC) == true);

	// SHA256_Calculator alias works too (one-block "abc" check).
	{
		const uint8_t want0 = 0xba;
		std::vector<Bit> bits;
		for (uint8_t byte : {'a', 'b', 'c'})
			for (int k = 0; k < 8; ++k) bits.push_back(Bit(((byte >> k) & 1) != 0, ALICE));
		BitVec digest;
		SHA256_Calculator calc;
		calc.sha256(&digest, bits.data(), bits.size());
		uint8_t out[32];
		digest.reveal(out, PUBLIC);
		ok &= (out[0] == want0);   // first digest byte of SHA-256("abc")
	}

	// Suffixed aliases are distinct names over the same underlying type, so a
	// value flows freely between them.
	UInt32_g c(5u, ALICE);
	UInt32 d = c + a;            // UInt32_g + UInt32 -> same Bit_T<block> wires
	ok &= (d.reveal<uint32_t>(PUBLIC) == 12u);
	Bit_g g(false, ALICE);
	ok &= (g.reveal<bool>(PUBLIC) == false);

	printf("circuit_types binding macro: %s\n", ok ? "OK" : "FAIL");
	finalize_clear_backend();
	return ok ? 0 : 1;
}
