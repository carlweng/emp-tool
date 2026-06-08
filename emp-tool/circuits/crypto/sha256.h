#ifndef EMP_CIRCUIT_CRYPTO_SHA256_H__
#define EMP_CIRCUIT_CRYPTO_SHA256_H__

// SHA-256 (FIPS 180-4) over the BooleanContext value layer. The primitive is the
// word-level compression function; sha256<N> is the full padded hash for a
// compile-time public message length N (bits, a multiple of 8) — N being public
// gives a deterministic circuit. Every word operation is a UInt_T<Ctx,32> op:
// addition mod 2^32 (operator+), Ch/Maj (operator& / operator^), and the Σ/σ
// rotations/shifts (UInt_T::rotr / operator>>, all free of AND gates).
//
// I/O is LSB-first within each byte, byte-sequential; the message is read as
// big-endian 32-bit words and the digest emitted big-endian, so feeding bytes and
// reading the 256-bit output reproduces the standard digest.

#include "emp-tool/circuits/bitvec.h"
#include "emp-tool/circuits/unsigned_int.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace emp {
namespace circuit {
namespace crypto {

// Round constants K[0..63] and initial hash value H(0)[0..7] (FIPS 180-4).
inline constexpr uint32_t SHA256_K[64] = {
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
inline constexpr uint32_t SHA256_H0[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
};

template <BooleanContext Ctx> using SHA256Word = UInt_T<Ctx, 32>;

template <BooleanContext Ctx>
inline SHA256Word<Ctx> sha256_ch(const SHA256Word<Ctx>& x, const SHA256Word<Ctx>& y,
                                 const SHA256Word<Ctx>& z) { return z ^ (x & (y ^ z)); }
template <BooleanContext Ctx>
inline SHA256Word<Ctx> sha256_maj(const SHA256Word<Ctx>& x, const SHA256Word<Ctx>& y,
                                  const SHA256Word<Ctx>& z) { return (x & y) ^ (z & (x ^ y)); }
template <BooleanContext Ctx>
inline SHA256Word<Ctx> sha256_bsig0(const SHA256Word<Ctx>& x) { return x.rotr(2) ^ x.rotr(13) ^ x.rotr(22); }
template <BooleanContext Ctx>
inline SHA256Word<Ctx> sha256_bsig1(const SHA256Word<Ctx>& x) { return x.rotr(6) ^ x.rotr(11) ^ x.rotr(25); }
template <BooleanContext Ctx>
inline SHA256Word<Ctx> sha256_ssig0(const SHA256Word<Ctx>& x) { return x.rotr(7) ^ x.rotr(18) ^ (x >> 3); }
template <BooleanContext Ctx>
inline SHA256Word<Ctx> sha256_ssig1(const SHA256Word<Ctx>& x) { return x.rotr(17) ^ x.rotr(19) ^ (x >> 10); }

// One compression step: fold a 16-word message block into the 8-word state.
// state is updated in place (state[i] += compressed working variable).
template <BooleanContext Ctx>
inline void sha256_compress(Ctx& ctx, UInt_T<Ctx, 32> state[8],
                            const UInt_T<Ctx, 32> block[16]) {
  using U = UInt_T<Ctx, 32>;
  U W[64];
  for (int j = 0; j < 16; ++j) W[j] = block[j];
  for (int t = 16; t < 64; ++t)
    W[t] = sha256_ssig1<Ctx>(W[t - 2]) + W[t - 7] + sha256_ssig0<Ctx>(W[t - 15]) + W[t - 16];

  U a = state[0], b = state[1], c = state[2], d = state[3];
  U e = state[4], f = state[5], g = state[6], h = state[7];
  for (int t = 0; t < 64; ++t) {
    U T1 = h + sha256_bsig1<Ctx>(e) + sha256_ch<Ctx>(e, f, g) + U::constant(ctx, SHA256_K[t]) + W[t];
    U T2 = sha256_bsig0<Ctx>(a) + sha256_maj<Ctx>(a, b, c);
    h = g; g = f; f = e; e = d + T1;
    d = c; c = b; b = a; a = T1 + T2;
  }
  state[0] = state[0] + a; state[1] = state[1] + b;
  state[2] = state[2] + c; state[3] = state[3] + d;
  state[4] = state[4] + e; state[5] = state[5] + f;
  state[6] = state[6] + g; state[7] = state[7] + h;
}

// Big-endian word `word`, bit `b` -> flat bit index within a 64-byte block
// (LSB-first within byte). Used for both reading message words and writing the digest.
inline constexpr std::size_t sha256_word_bit_index(std::size_t word, std::size_t b) {
  return 8 * (4 * word + 3 - b / 8) + (b % 8);
}

namespace detail {

// Full SHA-256 of a fixed public N-bit message (N a multiple of 8). Padding is
// public, so the circuit shape is fixed per N. `in` points at the N message wires.
// Writes 256 output wires.
template <BooleanContext Ctx, int N>
inline void sha256_wires(Ctx& ctx, typename Ctx::Wire out[256], const typename Ctx::Wire* in) {
  static_assert(N % 8 == 0, "sha256<N>: byte-granular message length only");
  static_assert(N >= 0, "sha256<N>: bit length must be non-negative");
  using W = typename Ctx::Wire;
  using U = UInt_T<Ctx, 32>;
  const std::size_t L = static_cast<std::size_t>(N);
  const std::size_t total = ((L + 1 + 64 + 511) / 512) * 512;

  // Padded message, flat LSB-first wires: data bits are inputs, pad bits are public.
  std::vector<W> P(total, ctx.public_bit(false));
  for (std::size_t i = 0; i < L; ++i) P[i] = in[i];
  P[L + 7] = ctx.public_bit(true);                   // '1' bit = MSB of the 0x80 pad byte
  const std::size_t lenpos = total - 64;             // 64-bit big-endian length
  for (std::size_t t = 0; t < 8; ++t) {
    const uint64_t byte = (static_cast<uint64_t>(L) >> (8 * (7 - t))) & 0xff;
    for (std::size_t k = 0; k < 8; ++k)
      if ((byte >> k) & 1) P[lenpos + 8 * t + k] = ctx.public_bit(true);
  }

  U state[8];
  for (int i = 0; i < 8; ++i) state[i] = U::constant(ctx, SHA256_H0[i]);

  const std::size_t nblocks = total / 512;
  for (std::size_t blk = 0; blk < nblocks; ++blk) {
    U msg[16];
    for (int j = 0; j < 16; ++j) {
      msg[j] = U(ctx);
      for (int b = 0; b < 32; ++b) msg[j].w[b] = P[blk * 512 + sha256_word_bit_index(j, b)];
    }
    sha256_compress<Ctx>(ctx, state, msg);
  }

  for (int i = 0; i < 8; ++i)
    for (int b = 0; b < 32; ++b)
      out[sha256_word_bit_index(i, b)] = state[i].w[b];
}

}  // namespace detail

template <BooleanContext Ctx, int N>
inline BitVec_T<Ctx, 256> sha256(Ctx& ctx, const BitVec_T<Ctx, N>& in) {
  BitVec_T<Ctx, 256> out(ctx);
  detail::sha256_wires<Ctx, N>(ctx, out.w.data(), in.w.data());
  return out;
}

}  // namespace crypto
}  // namespace circuit
}  // namespace emp
#endif  // EMP_CIRCUIT_CRYPTO_SHA256_H__
