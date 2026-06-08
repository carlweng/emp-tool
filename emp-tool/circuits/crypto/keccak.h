#ifndef EMP_CIRCUIT_CRYPTO_KECCAK_H__
#define EMP_CIRCUIT_CRYPTO_KECCAK_H__

// Keccak-f[1600] and SHA3-256 over the BooleanContext value layer. The primitive
// is the permutation on a 25-lane state (5x5 lanes of 64 bits); sha3_256<N> is the
// sponge for a compile-time public N-bit message. State layout (FIPS-202): lane
// (x,y) is state[5*y + x]; bit z is the lane's bit z (z=0 = LSB). The four linear
// steps (theta, rho, pi, iota) are XOR / lane rotate / index relabel / public-
// constant XOR — zero AND gates; chi is the only nonlinear step (1 AND/bit).
//
// Sponge I/O is LSB-first within each byte, byte-sequential: input bit j maps to
// state bit 64*(5y+x)+z with byte_j=j/8, lane=byte_j/8, x=lane%5, y=lane/5,
// z=8*(byte_j%8)+(j%8).

#include "emp-tool/circuits/bitvec.h"
#include "emp-tool/circuits/unsigned_int.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace emp {
namespace circuit {
namespace crypto {

// Rho rotation offsets RHO[y][x], and the iota round constants (FIPS-202 §3.2).
inline constexpr int KECCAK_RHO[5][5] = {
    {  0,  1, 62, 28, 27 },
    { 36, 44,  6, 55, 20 },
    {  3, 10, 43, 25, 39 },
    { 41, 45, 15, 21,  8 },
    { 18,  2, 61, 56, 14 },
};
inline constexpr uint64_t KECCAK_RC[24] = {
    0x0000000000000001ULL, 0x0000000000008082ULL, 0x800000000000808aULL,
    0x8000000080008000ULL, 0x000000000000808bULL, 0x0000000080000001ULL,
    0x8000000080008081ULL, 0x8000000000008009ULL, 0x000000000000008aULL,
    0x0000000000000088ULL, 0x0000000080008009ULL, 0x000000008000000aULL,
    0x000000008000808bULL, 0x800000000000008bULL, 0x8000000000008089ULL,
    0x8000000000008003ULL, 0x8000000000008002ULL, 0x8000000000000080ULL,
    0x000000000000800aULL, 0x800000008000000aULL, 0x8000000080008081ULL,
    0x8000000000008080ULL, 0x0000000080000001ULL, 0x8000000080008008ULL,
};

inline constexpr int keccak_lane(int x, int y) { return 5 * y + x; }

// Map an input/output bit index (LSB-first within byte, byte-sequential) to the
// 1600-bit state index (FIPS-202 §B.1).
inline constexpr std::size_t sha3_state_bit_index(std::size_t idx) {
  const std::size_t byte_j = idx / 8, bit_k = idx % 8, lane = byte_j / 8;
  const std::size_t x = lane % 5, y = lane / 5, z = 8 * (byte_j % 8) + bit_k;
  return 64 * (5 * y + x) + z;
}

// Keccak-f[1600]: 24 rounds of theta -> rho/pi -> chi -> iota, in place.
template <BooleanContext Ctx>
inline void keccak_f1600(Ctx& ctx, UInt_T<Ctx, 64> A[25]) {
  using U = UInt_T<Ctx, 64>;
  for (int r = 0; r < 24; ++r) {
    // theta
    U C[5], D[5];
    for (int x = 0; x < 5; ++x)
      C[x] = A[keccak_lane(x, 0)] ^ A[keccak_lane(x, 1)] ^ A[keccak_lane(x, 2)]
           ^ A[keccak_lane(x, 3)] ^ A[keccak_lane(x, 4)];
    for (int x = 0; x < 5; ++x) D[x] = C[(x + 4) % 5] ^ C[(x + 1) % 5].rotl(1);
    for (int x = 0; x < 5; ++x)
      for (int y = 0; y < 5; ++y) A[keccak_lane(x, y)] = A[keccak_lane(x, y)] ^ D[x];

    // rho + pi
    U B[25];
    for (int x = 0; x < 5; ++x)
      for (int y = 0; y < 5; ++y)
        B[keccak_lane(y, (2 * x + 3 * y) % 5)] = A[keccak_lane(x, y)].rotl(KECCAK_RHO[y][x]);
    for (int i = 0; i < 25; ++i) A[i] = B[i];

    // chi (the only AND step)
    for (int y = 0; y < 5; ++y) {
      U row[5];
      for (int x = 0; x < 5; ++x) row[x] = A[keccak_lane(x, y)];
      for (int x = 0; x < 5; ++x)
        A[keccak_lane(x, y)] = row[x] ^ ((~row[(x + 1) % 5]) & row[(x + 2) % 5]);
    }

    // iota
    A[0] = A[0] ^ U::constant(ctx, KECCAK_RC[r]);
  }
}

namespace detail {

// SHA3-256 of a fixed public N-bit message (rate 1088, domain 0x06, pad10*1).
// `in` points at the N message wires (a pointer, so N == 0 is well-formed).
template <BooleanContext Ctx, int N>
inline void sha3_256_wires(Ctx& ctx, typename Ctx::Wire out[256], const typename Ctx::Wire* in) {
  static_assert(N % 8 == 0, "sha3_256<N>: byte-granular message length only");
  static_assert(N >= 0, "sha3_256<N>: bit length must be non-negative");
  using W = typename Ctx::Wire;
  using U = UInt_T<Ctx, 64>;
  std::array<W, 1600> S;
  S.fill(ctx.public_bit(false));

  auto permute = [&]() {
    U A[25];
    for (int l = 0; l < 25; ++l) {
      A[l] = U(ctx);
      for (int z = 0; z < 64; ++z) A[l].w[z] = S[64 * l + z];
    }
    keccak_f1600<Ctx>(ctx, A);
    for (int l = 0; l < 25; ++l)
      for (int z = 0; z < 64; ++z) S[64 * l + z] = A[l].w[z];
  };

  std::size_t index = 0;
  for (std::size_t j = 0; j < N; ++j) {
    const std::size_t s = sha3_state_bit_index(index);
    S[s] = ctx.xor_gate(S[s], in[j]);
    if (++index == 1088) { permute(); index = 0; }
  }

  // Domain separator 0x06 at the next byte boundary, then pad10*1 final 1.
  index = 8 * ((index + 7) / 8);
  S[sha3_state_bit_index(index + 1)] = ctx.not_gate(S[sha3_state_bit_index(index + 1)]);
  S[sha3_state_bit_index(index + 2)] = ctx.not_gate(S[sha3_state_bit_index(index + 2)]);
  S[1087] = ctx.not_gate(S[1087]);
  permute();

  for (std::size_t i = 0; i < 256; ++i) out[i] = S[sha3_state_bit_index(i)];
}

}  // namespace detail

template <BooleanContext Ctx, int N>
inline BitVec_T<Ctx, 256> sha3_256(Ctx& ctx, const BitVec_T<Ctx, N>& in) {
  BitVec_T<Ctx, 256> out(ctx);
  detail::sha3_256_wires<Ctx, N>(ctx, out.w.data(), in.w.data());
  return out;
}

}  // namespace crypto
}  // namespace circuit
}  // namespace emp
#endif  // EMP_CIRCUIT_CRYPTO_KECCAK_H__
