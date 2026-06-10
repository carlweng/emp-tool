#ifndef EMP_CIRCUIT_CRYPTO_AES128_H__
#define EMP_CIRCUIT_CRYPTO_AES128_H__

#include "emp-tool/ir/context/concept.h"
#include "emp-tool/circuits/bitvec.h"
#include "emp-tool/circuits/unsigned_int.h"   // for the CTR counter increment
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace emp {
namespace circuit {
namespace crypto {

// AES-128 over the BooleanContext value layer. The public API is BitVec_T-first:
// blocks, keys, IVs, and byte values are typed bit-vectors. Implementation helpers
// operate on bare Ctx::Wire arrays so the bulk circuit state does not become
// Bit_T-per-bit storage. Round constants fold in with NOT gates, so no public
// constants are needed. `aes128_encrypt` is the block primitive; `aes128_ctr`
// provides CTR mode (NIST SP 800-38A) on top of it. Other modes (CBC, ...) are
// caller-composed from the block primitive. Bit convention: LSB at index 0 within
// each byte; bytes natural order; state column-major 4x4.

namespace detail {

// AES SBox (Boyar-Peralta 2010): 32 ANDs, 81 XORs, 4 NOTs.
// Public bit convention: U[0]=LSB, U[7]=MSB; S[0]=LSB, S[7]=MSB.
// (BP paper uses MSB-first; we flip indices once at entry/exit so the
// published formulas stay verbatim. Pure renaming — no gates added.)
template <BooleanContext Ctx>
inline void aes_sbox(Ctx& ctx, const typename Ctx::Wire U_lsb[8], typename Ctx::Wire S_lsb[8]) {
	using W = typename Ctx::Wire;
	auto X  = [&](W a, W b) { return ctx.xor_gate(a, b); };
	auto An = [&](W a, W b) { return ctx.and_gate(a, b); };
	auto Nt = [&](W a)      { return ctx.not_gate(a); };

	W U[8];
	for (int i = 0; i < 8; ++i) U[i] = U_lsb[7 - i];

	W t1  = X(U[3], U[5]);
	W t2  = X(U[0], U[6]);
	W t3  = X(U[0], U[3]);
	W t4  = X(U[0], U[5]);
	W t5  = X(U[1], U[2]);
	W t6  = X(t5,  U[7]);
	W t7  = X(t6,  U[3]);
	W t8  = X(t2,  t1);
	W t9  = X(t6,  U[0]);
	W t10 = X(t6,  U[6]);
	W t11 = X(t10, t4);
	W t12 = X(U[4], t8);
	W t13 = X(t12, U[5]);
	W t14 = X(t12, U[1]);
	W t15 = X(t13, U[7]);
	W t16 = X(t13, t5);
	W t17 = X(t14, t3);
	W t18 = X(U[7], t17);
	W t19 = X(t16, t17);
	W t20 = X(t16, t4);
	W t21 = X(t5,  t17);
	W t22 = X(t2,  t21);
	W t23 = X(U[0], t21);

	W t24 = An(t8,  t13);
	W t25 = An(t11, t15);
	W t27 = An(t7,  U[7]);
	W t29 = An(t2,  t21);
	W t30 = An(t10, t6);
	W t32 = An(t9,  t18);
	W t34 = An(t3,  t17);
	W t35 = An(t1,  t19);
	W t37 = An(t4,  t16);
	W t26 = X(t25, t24);
	W t28 = X(t27, t24);
	W t31 = X(t30, t29);
	W t33 = X(t32, t29);
	W t36 = X(t35, t34);
	W t38 = X(t37, t34);
	W t39 = X(t26, t14);
	W t40 = X(t28, t38);
	W t41 = X(t31, t36);
	W t42 = X(t33, t38);
	W t43 = X(t39, t36);
	W t44 = X(t40, t20);
	W t45 = X(t41, t22);
	W t46 = X(t42, t23);

	W t47 = X(t43, t44);
	W t48 = An(t43, t45);
	W t49 = X(t46, t48);
	W t50 = An(t47, t49);
	W t51 = X(t50, t44);
	W t52 = X(t45, t46);
	W t53 = X(t44, t48);
	W t54 = An(t53, t52);
	W t55 = X(t54, t46);
	W t56 = X(t45, t55);
	W t57 = X(t49, t55);
	W t58 = An(t46, t57);
	W t59 = X(t58, t56);
	W t60 = X(t49, t58);
	W t61 = An(t51, t60);
	W t62 = X(t47, t61);
	W t63 = X(t62, t59);
	W t64 = X(t51, t55);
	W t65 = X(t51, t62);
	W t66 = X(t55, t59);
	W t67 = X(t64, t63);

	W t68 = An(t66, t13);
	W t69 = An(t59, t15);
	W t70 = An(t55, U[7]);
	W t71 = An(t65, t21);
	W t72 = An(t62, t6);
	W t73 = An(t51, t18);
	W t74 = An(t64, t17);
	W t75 = An(t67, t19);
	W t76 = An(t63, t16);
	W t77 = An(t66, t8);
	W t78 = An(t59, t11);
	W t79 = An(t55, t7);
	W t80 = An(t65, t2);
	W t81 = An(t62, t10);
	W t82 = An(t51, t9);
	W t83 = An(t64, t3);
	W t84 = An(t67, t1);
	W t85 = An(t63, t4);

	W t86 = X(t83, t84);
	W t87 = X(t78, t86);
	W t88 = X(t77, t87);
	W t89 = X(t68, t70);
	W t90 = X(t69, t68);
	W t91 = X(t71, t72);
	W t92 = X(t80, t89);
	W t93 = X(t75, t91);
	W t94 = X(t76, t92);
	W t95 = X(t93, t94);
	W t96 = X(t91, t90);
	W t97 = X(t71, t73);
	W t98 = X(t81, t86);
	W t99 = X(t89, t97);
	W t100 = X(t74, t93);
	W t101 = X(t82, t95);
	W t102 = X(t98, t99);
	W t103 = X(t83, t100);
	W t104 = X(t87, t79);
	W t105 = X(t101, t103);

	// S3 must be computed before S1 / S4 (they reference it).
	W S[8];
	S[0] = X(t88, t100);
	S[3] = X(t88, t96);
	S[1] = Nt(X(S[3], t100));
	S[2] = Nt(X(t105, t85));
	S[4] = X(t99, S[3]);
	S[5] = X(t104, t101);
	S[6] = Nt(X(t95, t102));
	S[7] = Nt(X(t80, t102));

	for (int i = 0; i < 8; ++i) S_lsb[i] = S[7 - i];
}

// xtime: multiply by x in GF(2^8) with poly x^8+x^4+x^3+x+1.
// LSB-first: in[0]=bit 0, in[7]=bit 7. Pure XOR, no ANDs.
template <BooleanContext Ctx>
inline void aes_xtime(Ctx& ctx, const typename Ctx::Wire in[8], typename Ctx::Wire out[8]) {
	const typename Ctx::Wire m = in[7];
	out[0] = m;
	out[1] = ctx.xor_gate(in[0], m);
	out[2] = in[1];
	out[3] = ctx.xor_gate(in[2], m);
	out[4] = ctx.xor_gate(in[3], m);
	out[5] = in[4];
	out[6] = in[5];
	out[7] = in[6];
}

// AES-128 key expansion: 128-bit key -> 11 round keys (1408 bits total).
// expanded[r*128 .. (r+1)*128) holds round key r. 40 SBox calls.
template <BooleanContext Ctx>
inline void aes128_key_schedule(Ctx& ctx, const typename Ctx::Wire key[128],
                                typename Ctx::Wire expanded[1408]) {
	using W = typename Ctx::Wire;

	for (int i = 0; i < 128; ++i) expanded[i] = key[i];

	static const unsigned char Rcon[11] = {
		0x00, 0x01, 0x02, 0x04, 0x08, 0x10,
		0x20, 0x40, 0x80, 0x1b, 0x36
	};

	for (int r = 1; r <= 10; ++r) {
		W *prev = expanded + (r - 1) * 128;
		W *cur  = expanded + r * 128;

		W tmp[32];
		// RotWord on bytes 12..15 of prev: [b12,b13,b14,b15] -> [b13,b14,b15,b12]
		for (int b = 0; b < 4; ++b) {
			int src_byte = 12 + ((b + 1) & 3);
			for (int k = 0; k < 8; ++k)
				tmp[b * 8 + k] = prev[src_byte * 8 + k];
		}
		// SubWord: SBox each of the 4 bytes.
		for (int b = 0; b < 4; ++b) {
			W in[8], out[8];
			for (int k = 0; k < 8; ++k) in[k] = tmp[b * 8 + k];
			aes_sbox<Ctx>(ctx, in, out);
			for (int k = 0; k < 8; ++k) tmp[b * 8 + k] = out[k];
		}
		// XOR Rcon[r] into byte 0 of tmp (LSB-first bit indexing).
		for (int k = 0; k < 8; ++k)
			if ((Rcon[r] >> k) & 1) tmp[k] = ctx.not_gate(tmp[k]);

		// Word 0 of cur = word 0 of prev XOR tmp
		for (int k = 0; k < 32; ++k) cur[k] = ctx.xor_gate(prev[k], tmp[k]);
		// Words 1..3 of cur = word w-1 of cur XOR word w of prev
		for (int w = 1; w < 4; ++w)
			for (int k = 0; k < 32; ++k)
				cur[w * 32 + k] = ctx.xor_gate(cur[(w - 1) * 32 + k], prev[w * 32 + k]);
	}
}

// AES-128 encrypt with an already-expanded key. 160 SBox calls.
template <BooleanContext Ctx>
inline void aes128_encrypt_block(Ctx& ctx, const typename Ctx::Wire plaintext[128],
                                 const typename Ctx::Wire expanded_key[1408],
                                 typename Ctx::Wire ciphertext[128]) {
	using W = typename Ctx::Wire;
	W state[128];
	for (int i = 0; i < 128; ++i) state[i] = ctx.xor_gate(plaintext[i], expanded_key[i]);

	for (int r = 1; r <= 10; ++r) {
		// SubBytes: 16 SBoxes
		for (int b = 0; b < 16; ++b) {
			W in[8], out[8];
			for (int k = 0; k < 8; ++k) in[k] = state[b * 8 + k];
			aes_sbox<Ctx>(ctx, in, out);
			for (int k = 0; k < 8; ++k) state[b * 8 + k] = out[k];
		}

		// ShiftRows: new state[c, row] = old state[(c+row) mod 4, row]
		W shifted[128];
		for (int c = 0; c < 4; ++c)
			for (int row = 0; row < 4; ++row) {
				int src_byte = 4 * ((c + row) & 3) + row;
				int dst_byte = 4 * c + row;
				for (int k = 0; k < 8; ++k)
					shifted[dst_byte * 8 + k] = state[src_byte * 8 + k];
			}
		for (int i = 0; i < 128; ++i) state[i] = shifted[i];

		// MixColumns (skip on final round)
		if (r != 10) {
			W mixed[128];
			for (int c = 0; c < 4; ++c) {
				const W *col = state + c * 32;
				const W *a = col + 0;
				const W *b = col + 8;
				const W *cc = col + 16;
				const W *d = col + 24;

				W t[8], ab[8], bc[8], cd[8], da[8];
				W xab[8], xbc[8], xcd[8], xda[8];
				for (int k = 0; k < 8; ++k) {
					t[k]  = ctx.xor_gate(ctx.xor_gate(a[k], b[k]), ctx.xor_gate(cc[k], d[k]));
					ab[k] = ctx.xor_gate(a[k], b[k]);
					bc[k] = ctx.xor_gate(b[k], cc[k]);
					cd[k] = ctx.xor_gate(cc[k], d[k]);
					da[k] = ctx.xor_gate(d[k], a[k]);
				}
				aes_xtime<Ctx>(ctx, ab, xab);
				aes_xtime<Ctx>(ctx, bc, xbc);
				aes_xtime<Ctx>(ctx, cd, xcd);
				aes_xtime<Ctx>(ctx, da, xda);

				W *na = mixed + c * 32 + 0;
				W *nb = mixed + c * 32 + 8;
				W *nc = mixed + c * 32 + 16;
				W *nd = mixed + c * 32 + 24;
				for (int k = 0; k < 8; ++k) {
					na[k] = ctx.xor_gate(a[k],  ctx.xor_gate(t[k], xab[k]));
					nb[k] = ctx.xor_gate(b[k],  ctx.xor_gate(t[k], xbc[k]));
					nc[k] = ctx.xor_gate(cc[k], ctx.xor_gate(t[k], xcd[k]));
					nd[k] = ctx.xor_gate(d[k],  ctx.xor_gate(t[k], xda[k]));
				}
			}
			for (int i = 0; i < 128; ++i) state[i] = mixed[i];
		}

		// AddRoundKey r
		const W *rk = expanded_key + r * 128;
		for (int i = 0; i < 128; ++i) state[i] = ctx.xor_gate(state[i], rk[i]);
	}

	for (int i = 0; i < 128; ++i) ciphertext[i] = state[i];
}

// Convenience: key schedule + block encrypt in one call.
template <BooleanContext Ctx>
inline void aes128_encrypt(Ctx& ctx, const typename Ctx::Wire plaintext[128],
                           const typename Ctx::Wire key[128], typename Ctx::Wire ciphertext[128]) {
	typename Ctx::Wire expanded[1408];
	aes128_key_schedule<Ctx>(ctx, key, expanded);
	aes128_encrypt_block<Ctx>(ctx, plaintext, expanded, ciphertext);
}

// Add `delta` to the big-endian 64-bit counter in bytes 8..15 of a 128-bit CTR
// block (NIST SP 800-38A: byte 15 is the LSB; carry beyond byte 8 is dropped).
template <BooleanContext Ctx>
inline void ctr_inc_be64(Ctx& ctx, typename Ctx::Wire block[128], uint64_t delta) {
	if (delta == 0) return;
	UInt_T<Ctx, 64> ctr(ctx);
	for (int byte = 0; byte < 8; ++byte) {            // pack bytes 15,14,...,8 LSB-first
		int src = 15 - byte;
		for (int k = 0; k < 8; ++k) ctr.w[byte * 8 + k] = block[src * 8 + k];
	}
	ctr = ctr + UInt_T<Ctx, 64>::constant(ctx, delta);
	for (int byte = 0; byte < 8; ++byte) {
		int dst = 15 - byte;
		for (int k = 0; k < 8; ++k) block[dst * 8 + k] = ctr.w[byte * 8 + k];
	}
}

// AES-128-CTR (NIST SP 800-38A): keystream block i = AES(key, counter_i), out =
// in ^ keystream, for length_bits bits (a partial final block is allowed). The
// 128-bit `iv` is the initial counter block; bytes 8..15 are the big-endian
// counter incremented per block. `start_chunk` offsets the initial counter.
template <BooleanContext Ctx>
inline void aes128_ctr(Ctx& ctx, const typename Ctx::Wire key[128], const typename Ctx::Wire iv[128],
                       const typename Ctx::Wire* in, typename Ctx::Wire* out, std::size_t length_bits,
                       uint64_t start_chunk = 0) {
	using W = typename Ctx::Wire;
	W round_keys[1408];
	aes128_key_schedule<Ctx>(ctx, key, round_keys);
	W counter[128];
	for (int i = 0; i < 128; ++i) counter[i] = iv[i];
	ctr_inc_be64<Ctx>(ctx, counter, start_chunk);
	for (std::size_t done = 0; done < length_bits; ) {
		W ks[128];
		aes128_encrypt_block<Ctx>(ctx, counter, round_keys, ks);
		std::size_t blk = std::min<std::size_t>(128, length_bits - done);
		for (std::size_t i = 0; i < blk; ++i) out[done + i] = ctx.xor_gate(in[done + i], ks[i]);
		done += blk;
		if (done < length_bits) ctr_inc_be64<Ctx>(ctx, counter, 1);   // no increment past the last block
	}
}

}  // namespace detail

template <BooleanContext Ctx>
inline BitVec_T<Ctx, 8> aes_sbox(Ctx& ctx, const BitVec_T<Ctx, 8>& byte) {
	BitVec_T<Ctx, 8> out(ctx);
	detail::aes_sbox<Ctx>(ctx, byte.w.data(), out.w.data());
	return out;
}

template <BooleanContext Ctx>
inline BitVec_T<Ctx, 8> aes_xtime(Ctx& ctx, const BitVec_T<Ctx, 8>& byte) {
	BitVec_T<Ctx, 8> out(ctx);
	detail::aes_xtime<Ctx>(ctx, byte.w.data(), out.w.data());
	return out;
}

template <BooleanContext Ctx>
inline BitVec_T<Ctx, 1408> aes128_key_schedule(Ctx& ctx, const BitVec_T<Ctx, 128>& key) {
	BitVec_T<Ctx, 1408> expanded(ctx);
	detail::aes128_key_schedule<Ctx>(ctx, key.w.data(), expanded.w.data());
	return expanded;
}

template <BooleanContext Ctx>
inline BitVec_T<Ctx, 128> aes128_encrypt_block(Ctx& ctx, const BitVec_T<Ctx, 128>& plaintext,
                                               const BitVec_T<Ctx, 1408>& expanded_key) {
	BitVec_T<Ctx, 128> ciphertext(ctx);
	detail::aes128_encrypt_block<Ctx>(ctx, plaintext.w.data(), expanded_key.w.data(), ciphertext.w.data());
	return ciphertext;
}

template <BooleanContext Ctx>
inline BitVec_T<Ctx, 128> aes128_encrypt(Ctx& ctx, const BitVec_T<Ctx, 128>& plaintext,
                                         const BitVec_T<Ctx, 128>& key) {
	BitVec_T<Ctx, 128> ciphertext(ctx);
	detail::aes128_encrypt<Ctx>(ctx, plaintext.w.data(), key.w.data(), ciphertext.w.data());
	return ciphertext;
}

template <BooleanContext Ctx>
inline BitVec_T<Ctx, 128> ctr_inc_be64(Ctx& ctx, const BitVec_T<Ctx, 128>& block, uint64_t delta) {
	BitVec_T<Ctx, 128> out(block);
	detail::ctr_inc_be64<Ctx>(ctx, out.w.data(), delta);
	return out;
}

template <BooleanContext Ctx, int N>
inline BitVec_T<Ctx, N> aes128_ctr(Ctx& ctx, const BitVec_T<Ctx, 128>& key,
                                   const BitVec_T<Ctx, 128>& iv,
                                   const BitVec_T<Ctx, N>& in,
                                   uint64_t start_chunk = 0) {
	static_assert(N >= 0, "aes128_ctr<N>: bit length must be non-negative");
	BitVec_T<Ctx, N> out(ctx);
	detail::aes128_ctr<Ctx>(ctx, key.w.data(), iv.w.data(), in.w.data(), out.w.data(),
	                        static_cast<std::size_t>(N), start_chunk);
	return out;
}

}  // namespace crypto
}  // namespace circuit
}  // namespace emp

#endif  // EMP_CIRCUIT_CRYPTO_AES128_H__
