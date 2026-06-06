#ifndef EMP_CIRCUIT_FLOAT_H__
#define EMP_CIRCUIT_FLOAT_H__
#include "emp-tool/circuits/bit.h"
#include "emp-tool/circuits/bitvec.h"
#include "emp-tool/circuits/sortable.h"
#include "emp-tool/circuits/signed_int.h"
#include "emp-tool/circuits/unsigned_int.h"
#include "emp-tool/circuits/boolean_program.h"
#include <math.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <algorithm>
namespace emp {

// Per-width IEEE-754 parameters for Float_T<Wire, W>, plus host <-> bit-pattern
// conversions used by the input constructor and reveal(). Widths 16/32/64 map to
// the binary16/binary32/binary64 .empbc suites (prefix fp16/fp32/fp64).
//
// The host value type is `float` for fp16/fp32 and `double` for fp64. fp16 has
// no portable C++ scalar type, so its conversions are done in software (RNE).
inline uint16_t emp_float_to_half(float f) {
	uint32_t x;
	std::memcpy(&x, &f, sizeof(x));
	uint32_t sign = (x >> 16) & 0x8000u;
	uint32_t fexp = (x >> 23) & 0xFFu;
	uint32_t mant = x & 0x7FFFFFu;
	if (fexp == 0xFF)
		return (uint16_t)(sign | 0x7C00u | (mant ? 0x200u : 0u) | (mant >> 13));
	int e = (int)fexp - 127 + 15;
	if (e >= 0x1F) return (uint16_t)(sign | 0x7C00u);
	if (e <= 0) {
		if (e < -10) return (uint16_t)sign;
		mant |= 0x800000u;
		int shift = 14 - e;
		uint32_t h = mant >> shift;
		uint32_t rem = mant & ((1u << shift) - 1);
		uint32_t half = 1u << (shift - 1);
		if (rem > half || (rem == half && (h & 1u))) ++h;
		return (uint16_t)(sign | h);
	}
	uint32_t h = sign | ((uint32_t)e << 10) | (mant >> 13);
	uint32_t rem = mant & 0x1FFFu;
	if (rem > 0x1000u || (rem == 0x1000u && (h & 1u))) ++h;
	return (uint16_t)h;
}

inline float emp_half_to_float(uint16_t h) {
	uint32_t sign = (uint32_t)(h & 0x8000u) << 16;
	uint32_t exp  = (h >> 10) & 0x1Fu;
	uint32_t mant = h & 0x3FFu;
	uint32_t f;
	if (exp == 0) {
		if (mant == 0) {
			f = sign;
		} else {
			int e = 127 - 15 + 1;
			while (!(mant & 0x400u)) { mant <<= 1; --e; }
			mant &= 0x3FFu;
			f = sign | ((uint32_t)e << 23) | (mant << 13);
		}
	} else if (exp == 0x1F) {
		f = sign | 0x7F800000u | (mant << 13);
	} else {
		f = sign | ((exp - 15 + 127) << 23) | (mant << 13);
	}
	float r;
	std::memcpy(&r, &f, sizeof(r));
	return r;
}

template<int W> struct FloatTraits;

template<> struct FloatTraits<32> {
	using host_t = float;
	static constexpr int LEN = 32, BIAS = 127, SGNFC_LEN = 23, EXPNT_LEN = 8;
	static uint64_t to_bits(float v) { uint32_t b; std::memcpy(&b, &v, 4); return b; }
	static host_t from_bits(uint64_t b) { uint32_t u = (uint32_t)b; float f; std::memcpy(&f, &u, 4); return f; }
};

template<> struct FloatTraits<64> {
	using host_t = double;
	static constexpr int LEN = 64, BIAS = 1023, SGNFC_LEN = 52, EXPNT_LEN = 11;
	static uint64_t to_bits(double v) { uint64_t b; std::memcpy(&b, &v, 8); return b; }
	static host_t from_bits(uint64_t b) { double d; std::memcpy(&d, &b, 8); return d; }
};

template<> struct FloatTraits<16> {
	using host_t = float;
	static constexpr int LEN = 16, BIAS = 15, SGNFC_LEN = 10, EXPNT_LEN = 5;
	static uint64_t to_bits(float v) { return emp_float_to_half(v); }
	static host_t from_bits(uint64_t b) { return emp_half_to_float((uint16_t)b); }
};

namespace circuit {

// Load-or-fetch the cached on-disk circuit for IEEE binary<width> operation `op`
// (width in {16,32,64}; op like "add", "mul", "sqrt", "eq", ...).
const BooleanProgram& float_circuit(int width, const char* op);

}  // namespace circuit

// Width-generic IEEE-754 float circuit value. W in {16,32,64} selects
// binary16 / binary32 / binary64. Nontrivial arithmetic, comparisons, and
// classifiers are realized by matching on-disk circuits in
// emp-tool/circuits/files/ (fp16_*/fp32_*/fp64_*.empbc), loaded once and shared;
// simple structural ops such as abs, neg, and copysign are direct bit
// operations. There is no embedded gate table. The host scalar is `float` for
// fp16/fp32 and `double` for fp64 (FloatTraits<W>::host_t; fp16 host conversion
// is software).
//
// Bind with Float16 / Float32 / Float64; `Float` is an alias for Float32.
// Transcendentals (sin/cos/exp/...) are intentionally absent: the on-disk suite
// is bit-exact / correctly-rounded only.
template<typename Wire, int W>
class Float_T: public Sortable<Wire, Float_T<Wire, W>>, public CircuitValue { public:
	using Traits = FloatTraits<W>;
	using host_t = typename Traits::host_t;
	static constexpr int FLOAT_LEN = Traits::LEN;
	static constexpr int BIAS      = Traits::BIAS;
	static constexpr int SGNFC_LEN = Traits::SGNFC_LEN;
	static constexpr int EXPNT_LEN = Traits::EXPNT_LEN;

	std::array<Bit_T<Wire>, W> value;

	// Circuit-value interface (see circuit_value.h): W wires.
	template<typename NW> using rebind = Float_T<NW, W>;
	int  pack_size() const { return W; }
	void pack(Wire* out) const { for (int i = 0; i < W; ++i) out[i] = value[i].bit; }
	void unpack(const Wire* in, int /*n*/) { for (int i = 0; i < W; ++i) value[i].bit = in[i]; }

	Float_T(Float_T && in): value(std::move(in.value)) {}
	Float_T(const Float_T & in): value(in.value) {}
	Float_T& operator= (Float_T rhs) { std::swap(value, rhs.value); return *this; }

	// No-arg ctor: leaves `value` default-initialized (Bit_T<Wire>{}).
	Float_T() = default;
	// Input ctor: feeds `input`'s W-bit IEEE pattern as `party`'s input.
	// `party` has NO default — see Bit_T/BitVec_T for the rationale.
	Float_T(host_t input, int party);

	template<typename O>
	O reveal(int party = PUBLIC) const;

	Float_T abs() const;
	Float_T select(const Bit_T<Wire> & sel, const Float_T & rhs) const;

	// --- comparisons -> Bit (NaN-aware, via the fp<W>_* circuits) ---
	Bit_T<Wire> equal(const Float_T & rhs) const;
	Bit_T<Wire> not_equal(const Float_T & rhs) const;
	Bit_T<Wire> less_than(const Float_T & rhs) const;
	Bit_T<Wire> less_equal(const Float_T & rhs) const;
	Bit_T<Wire> greater_than(const Float_T & rhs) const;
	Bit_T<Wire> greater_equal(const Float_T & rhs) const;
	Bit_T<Wire> geq(const Float_T & rhs) const { return greater_equal(rhs); }  // Sortable hook

	// --- classifiers -> Bit ---
	Bit_T<Wire> is_nan() const;
	Bit_T<Wire> is_inf() const;
	Bit_T<Wire> is_zero() const;

	// --- arithmetic ---
	Float_T operator+(const Float_T& rhs) const;
	Float_T operator-(const Float_T& rhs) const;
	Float_T operator-() const;
	Float_T operator*(const Float_T& rhs) const;
	Float_T operator/(const Float_T& rhs) const;
	Float_T operator^(const Float_T& rhs) const;
	Float_T operator^=(const Float_T& rhs);
	Float_T operator&(const Float_T& rhs) const;

	Float_T sqr() const;
	Float_T sqrt() const;
	Float_T recip() const;                                  // 1 / x
	Float_T rsqrt() const;                                  // 1 / sqrt(x)
	Float_T fma(const Float_T& b, const Float_T& c) const;  // this*b + c (unfused)
	Float_T min(const Float_T& rhs) const;
	Float_T max(const Float_T& rhs) const;
	Float_T copysign(const Float_T& rhs) const;             // |this| with sign of rhs

	Bit_T<Wire>& operator[](int index);
	const Bit_T<Wire> & operator[](int index) const;
	size_t size() const { return W; }

	// Decode this float as a non-negative N-bit fixed-point integer with `s`
	// fractional bits. The IEEE sign bit is ignored. Truncates toward zero.
	// Returns 0 when the input is zero. NaN/Inf/subnormals are preconditions:
	// undefined. (Width-generic; the fp32 path is the exercised one.)
	template<int N> UnsignedInt_T<Wire, N> to_unsigned(size_t s) const;
	// Signed variant: composes to_unsigned with the IEEE sign bit.
	template<int N> SignedInt_T<Wire, N>   to_signed(size_t s) const;
};

// Default width (W = 32) is declared once in unsigned_int.h; the class
// definition must not repeat it.

#include "emp-tool/circuits/float.hpp"
}
#endif  // EMP_CIRCUIT_FLOAT_H__
