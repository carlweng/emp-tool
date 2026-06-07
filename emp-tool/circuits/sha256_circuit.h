#ifndef EMP_SHA256_CIRCUIT_H_
#define EMP_SHA256_CIRCUIT_H_

#include "emp-tool/circuits/bit.h"
#include "emp-tool/circuits/bitvec.h"
#include "emp-tool/circuits/unsigned_int.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace emp {

// ===========================================================================
// LEGACY Bit_T circuit kernel — RECORDING SOURCE, not the canonical layer.
// Hand-written gate circuit over Bit_T<Wire> + the global emp::Backend, NOT the
// BooleanContext typed layer (circuits/typed.h). Recorded once into
// sha256_256.empbc (tools/record_builtins.cpp); the runtime path is IR replay
// via execute_program(ctx, circuit::builtin_circuit("sha256_256")). Quarantined
// (opt-in include, not in emp-tool.h) until the global-backend consumers
// (ag2pc / agmpc) migrate to BooleanContext; port or retire then.
// ===========================================================================

// SHA-256 (FIPS 180-4) as hardcoded gates, in the style of aes_circuit.h /
// sha3_circuit.h. Each 32-bit word is a UnsignedInt_T<Wire, 32>, so every
// word operation reuses that type's existing gates:
//   - additions mod 2^32  -> operator+ (ripple adder in numeric_kernels.h)
//   - Ch / Maj            -> operator& / operator^
//   - rotate / shift / XOR-> operator>> / operator<< / operator^ (all 0 ANDs)
// Nothing here re-implements arithmetic; the only SHA-256-specific pieces are
// the constants, the round structure, and the byte<->word bit mapping.
//
// A right-rotate is gate-free: ROTR_n(x) = (x >> n) ^ (x << (32-n)). The two
// shifted halves occupy disjoint bit positions (>> n clears the top n bits,
// << (32-n) clears the bottom 32-n), so XOR equals the OR that defines rotate.
//
// I/O convention matches the rest of emp-tool — LSB-first within each byte,
// byte-sequential (same as BitVec_T / UnsignedInt_T). SHA-256 reads the message
// as big-endian 32-bit words and emits a big-endian digest, so word `j` bit `b`
// maps to the flat position given by word_bit_index(). Feeding bytes via
// BitVec_T(8, byte, party) and revealing the 256-bit output therefore
// reproduces the standard digest byte-for-byte.

namespace sha256_detail {

// Round constants K[0..63] (FIPS 180-4 §4.2.2).
inline constexpr uint32_t K[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
	0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
	0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
	0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
	0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
	0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
	0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
	0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
	0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

// Initial hash value H(0)[0..7] (FIPS 180-4 §5.3.3).
inline constexpr uint32_t H0[8] = {
	0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
	0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
};

}  // namespace sha256_detail

template <typename Wire> using SHA256Word = UnsignedInt_T<Wire, 32>;

// ROTR_n / SHR_n / Ch / Maj / Σ / σ — all expressed via UnsignedInt_T ops.
template <typename Wire>
inline SHA256Word<Wire> sha256_rotr(const SHA256Word<Wire>& x, int n) {
	return (x >> static_cast<size_t>(n)) ^ (x << static_cast<size_t>(32 - n));
}
template <typename Wire>
inline SHA256Word<Wire> sha256_shr(const SHA256Word<Wire>& x, int n) {
	return x >> static_cast<size_t>(n);
}
// Ch(x,y,z) = (x & y) ^ (~x & z) = z ^ (x & (y ^ z)). One AND-word.
template <typename Wire>
inline SHA256Word<Wire> sha256_ch(const SHA256Word<Wire>& x,
                                  const SHA256Word<Wire>& y,
                                  const SHA256Word<Wire>& z) {
	return z ^ (x & (y ^ z));
}
// Maj(x,y,z) = (x&y) ^ (x&z) ^ (y&z) = (x & y) ^ (z & (x ^ y)). Two AND-words.
template <typename Wire>
inline SHA256Word<Wire> sha256_maj(const SHA256Word<Wire>& x,
                                   const SHA256Word<Wire>& y,
                                   const SHA256Word<Wire>& z) {
	return (x & y) ^ (z & (x ^ y));
}
template <typename Wire>
inline SHA256Word<Wire> sha256_bsig0(const SHA256Word<Wire>& x) {
	return sha256_rotr<Wire>(x, 2) ^ sha256_rotr<Wire>(x, 13) ^ sha256_rotr<Wire>(x, 22);
}
template <typename Wire>
inline SHA256Word<Wire> sha256_bsig1(const SHA256Word<Wire>& x) {
	return sha256_rotr<Wire>(x, 6) ^ sha256_rotr<Wire>(x, 11) ^ sha256_rotr<Wire>(x, 25);
}
template <typename Wire>
inline SHA256Word<Wire> sha256_ssig0(const SHA256Word<Wire>& x) {
	return sha256_rotr<Wire>(x, 7) ^ sha256_rotr<Wire>(x, 18) ^ sha256_shr<Wire>(x, 3);
}
template <typename Wire>
inline SHA256Word<Wire> sha256_ssig1(const SHA256Word<Wire>& x) {
	return sha256_rotr<Wire>(x, 17) ^ sha256_rotr<Wire>(x, 19) ^ sha256_shr<Wire>(x, 10);
}

// One compression step: fold a 16-word message block into the 8-word state.
// state is updated in place (state[i] += compressed working variable).
template <typename Wire>
inline void sha256_compress(SHA256Word<Wire> state[8],
                            const SHA256Word<Wire> msg[16]) {
	using U = SHA256Word<Wire>;
	using namespace sha256_detail;

	// Message schedule W[0..63].
	U W[64];
	for (int j = 0; j < 16; ++j) W[j] = msg[j];
	for (int t = 16; t < 64; ++t)
		W[t] = sha256_ssig1<Wire>(W[t - 2]) + W[t - 7]
		     + sha256_ssig0<Wire>(W[t - 15]) + W[t - 16];

	U a = state[0], b = state[1], c = state[2], d = state[3];
	U e = state[4], f = state[5], g = state[6], h = state[7];

	for (int t = 0; t < 64; ++t) {
		U T1 = h + sha256_bsig1<Wire>(e) + sha256_ch<Wire>(e, f, g)
		     + U(K[t], PUBLIC) + W[t];
		U T2 = sha256_bsig0<Wire>(a) + sha256_maj<Wire>(a, b, c);
		h = g; g = f; f = e; e = d + T1;
		d = c; c = b; b = a; a = T1 + T2;
	}

	state[0] = state[0] + a; state[1] = state[1] + b;
	state[2] = state[2] + c; state[3] = state[3] + d;
	state[4] = state[4] + e; state[5] = state[5] + f;
	state[6] = state[6] + g; state[7] = state[7] + h;
}

// In-circuit SHA-256. Mirrors SHA3_256_Calculator_T's shape. Byte-granular
// input only (assert len % 8 == 0); the message length is public, so the
// padding bits are public constants.
template <typename Wire>
class SHA256_Calculator_T {
public:
	using B = Bit_T<Wire>;
	using U = SHA256Word<Wire>;
	B zero, one;

	SHA256_Calculator_T() : zero(false, PUBLIC), one(true, PUBLIC) {}

	// Big-endian word `word`, bit `b` -> flat input-bit index within a 64-byte
	// block (LSB-first within byte, byte-sequential). Used for both reading the
	// message words and writing the digest.
	static inline size_t word_bit_index(size_t word, size_t b) {
		return 8 * (4 * word + 3 - b / 8) + (b % 8);
	}

	// Hash `len_bits` (a multiple of 8) of wires; writes 256 output bits.
	void sha256(B output[256], const B* input, size_t len_bits) {
		assert(len_bits % 8 == 0 && "SHA256: byte-granular input only");
		const size_t L = len_bits;
		const size_t total = ((L + 1 + 64 + 511) / 512) * 512;   // padded length

		// Assemble the padded message as flat LSB-first bits. Data bits are the
		// input wires; pad bits are public constants.
		std::vector<B> P(total, zero);
		for (size_t i = 0; i < L; ++i) P[i] = input[i];
		P[L + 7] = one;                              // '1' bit = MSB of the 0x80 pad byte
		const size_t lenpos = total - 64;           // 64-bit big-endian length field
		for (size_t t = 0; t < 8; ++t) {
			const uint64_t byte = (static_cast<uint64_t>(L) >> (8 * (7 - t))) & 0xff;
			for (size_t k = 0; k < 8; ++k)
				if ((byte >> k) & 1) P[lenpos + 8 * t + k] = one;
		}

		U state[8];
		for (int i = 0; i < 8; ++i) state[i] = U(sha256_detail::H0[i], PUBLIC);

		const size_t nblocks = total / 512;
		for (size_t blk = 0; blk < nblocks; ++blk) {
			U msg[16];
			for (int j = 0; j < 16; ++j)
				for (int b = 0; b < 32; ++b)
					msg[j].bits[b] = P[blk * 512 + word_bit_index(j, b)];
			sha256_compress<Wire>(state, msg);
		}

		for (int i = 0; i < 8; ++i)
			for (int b = 0; b < 32; ++b)
				output[word_bit_index(i, b)] = state[i].bits[b];
	}

	// Convenience: concatenate `inputs` and hash; writes the 256-bit digest.
	void sha256(B output[256], const BitVec_T<Wire> inputs[], size_t input_count = 1) {
		std::vector<B> data;
		for (size_t i = 0; i < input_count; ++i)
			data.insert(data.end(), inputs[i].bits.begin(), inputs[i].bits.end());
		sha256(output, data.data(), data.size());
	}

	void sha256(BitVec_T<Wire>* output, const BitVec_T<Wire> inputs[],
	            size_t input_count = 1) {
		output->bits.resize(256);
		sha256(output->bits.data(), inputs, input_count);
	}

	void sha256(BitVec_T<Wire>* output, const B* input, size_t len_bits) {
		output->bits.resize(256);
		sha256(output->bits.data(), input, len_bits);
	}
};

}  // namespace emp

#endif  // EMP_SHA256_CIRCUIT_H_
