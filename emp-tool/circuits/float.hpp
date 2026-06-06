// Generic Float_T<Wire, W> implementation (W in {16,32,64}), included from
// float.h inside namespace emp after the class is declared. Three parts:
//   1. op dispatch over the on-disk fp<W> circuits for nontrivial ops,
//   2. host<->bits, direct bit ops, and the circuit-backed members,
//   3. float <-> fixed-point integer conversions (width-generic).
// The host value's IEEE pattern is extracted/reinjected via FloatTraits<W>
// (memcpy for fp32/fp64, software RNE for fp16).

// ===========================================================================
// 1. Op dispatch
// ===========================================================================

// Compute dispatcher over Bit_T<Wire> slots: each gate is realized with the Bit
// operators, which issue the matching call on the active Backend — so a loaded
// circuit replays identically to a hand-inlined gate loop.
template <typename Wire>
struct FloatComputeDispatcher {
	void and_gate(Bit_T<Wire>& o, const Bit_T<Wire>& a, const Bit_T<Wire>& b) { o = a & b; }
	void xor_gate(Bit_T<Wire>& o, const Bit_T<Wire>& a, const Bit_T<Wire>& b) { o = a ^ b; }
	void not_gate(Bit_T<Wire>& o, const Bit_T<Wire>& a)                       { o = !a; }
	// public_label is the const-gate API; Bit(v, PUBLIC) routes through the
	// optional (no-op-by-default) feed() instead.
	void const_gate(Bit_T<Wire>& o, bool v)                                   { backend->public_label(&o.bit, v); }
};

// Run the binary<W> circuit `op` over `num_in` packed bits -> `num_out` bits.
// The scratch is thread_local so repeated float ops on a thread reuse one wire
// buffer; the cached program is immutable and safely shared.
template <typename Wire, int W>
inline void run_float_circuit(const char* op,
                              const Bit_T<Wire>* in, size_t num_in,
                              Bit_T<Wire>* out, size_t num_out) {
	static thread_local emp::circuit::CircuitScratch<Bit_T<Wire>> scratch;
	emp::circuit::execute_program<Bit_T<Wire>>(
	    emp::circuit::float_circuit(W, op), in, num_in, out, num_out, scratch,
	    FloatComputeDispatcher<Wire>{});
}

template <typename Wire, int W>
inline Float_T<Wire, W> float_binary_(const char* op,
                                      const Float_T<Wire, W>& a, const Float_T<Wire, W>& b) {
	Bit_T<Wire> in[2 * W], out[W];
	for (int i = 0; i < W; ++i) { in[i] = a.value[i]; in[i + W] = b.value[i]; }
	run_float_circuit<Wire, W>(op, in, 2 * W, out, W);
	Float_T<Wire, W> res;
	for (int i = 0; i < W; ++i) res.value[i] = out[i];
	return res;
}

template <typename Wire, int W>
inline Float_T<Wire, W> float_unary_(const char* op, const Float_T<Wire, W>& a) {
	Bit_T<Wire> in[W], out[W];
	for (int i = 0; i < W; ++i) in[i] = a.value[i];
	run_float_circuit<Wire, W>(op, in, W, out, W);
	Float_T<Wire, W> res;
	for (int i = 0; i < W; ++i) res.value[i] = out[i];
	return res;
}

template <typename Wire, int W>
inline Float_T<Wire, W> float_ternary_(const char* op, const Float_T<Wire, W>& a,
                                       const Float_T<Wire, W>& b, const Float_T<Wire, W>& c) {
	Bit_T<Wire> in[3 * W], out[W];
	for (int i = 0; i < W; ++i) { in[i] = a.value[i]; in[i + W] = b.value[i]; in[i + 2 * W] = c.value[i]; }
	run_float_circuit<Wire, W>(op, in, 3 * W, out, W);
	Float_T<Wire, W> res;
	for (int i = 0; i < W; ++i) res.value[i] = out[i];
	return res;
}

// Predicates emit 8 output bits; bit 0 is the result.
template <typename Wire, int W>
inline Bit_T<Wire> float_compare_(const char* op,
                                  const Float_T<Wire, W>& a, const Float_T<Wire, W>& b) {
	Bit_T<Wire> in[2 * W], out[8];
	for (int i = 0; i < W; ++i) { in[i] = a.value[i]; in[i + W] = b.value[i]; }
	run_float_circuit<Wire, W>(op, in, 2 * W, out, 8);
	return out[0];
}

template <typename Wire, int W>
inline Bit_T<Wire> float_classify_(const char* op, const Float_T<Wire, W>& a) {
	Bit_T<Wire> in[W], out[8];
	for (int i = 0; i < W; ++i) in[i] = a.value[i];
	run_float_circuit<Wire, W>(op, in, W, out, 8);
	return out[0];
}

// ===========================================================================
// 2. host<->bits, bit ops, and circuit-backed members
// ===========================================================================

template<typename Wire, int W>
inline Float_T<Wire, W>::Float_T(host_t input, int party) {
	uint64_t bits = Traits::to_bits(input);
	BitVec_T<Wire> val(FLOAT_LEN, bits, party);
	for (int i = 0; i < FLOAT_LEN; ++i)
		value[i] = val.bits[i];
}

template<typename Wire, int W>
template<typename O>
inline O Float_T<Wire, W>::reveal(int party) const {
	uint64_t bits = 0;
	for (int i = 0; i < FLOAT_LEN; ++i) {
		bool tmp = value[i].template reveal<bool>(party);
		if (tmp) bits |= (uint64_t(1) << i);
	}
	host_t hv = Traits::from_bits(bits);
	if constexpr (std::is_same_v<O, std::string>)
		return std::to_string(hv);
	else
		return (O)hv;
}

template<typename Wire, int W>
inline Float_T<Wire, W> Float_T<Wire, W>::abs() const {
	Float_T<Wire, W> res(*this);
	res[FLOAT_LEN - 1] = Bit_T<Wire>(false, PUBLIC);
	return res;
}

template<typename Wire, int W>
inline Float_T<Wire, W> Float_T<Wire, W>::select(const Bit_T<Wire>& sel, const Float_T<Wire, W>& d) const {
	Float_T<Wire, W> res(*this);
	for (int i = 0; i < FLOAT_LEN; ++i)
		res.value[i] = value[i].select(sel, d.value[i]);
	return res;
}

template<typename Wire, int W>
inline Bit_T<Wire>& Float_T<Wire, W>::operator[](int index) {
	if (index < 0 || index >= FLOAT_LEN)
		error("Float_T::operator[]: index out of range");
	return value[index];
}

template<typename Wire, int W>
inline const Bit_T<Wire>& Float_T<Wire, W>::operator[](int index) const {
	if (index < 0 || index >= FLOAT_LEN)
		error("Float_T::operator[]: index out of range");
	return value[index];
}

template<typename Wire, int W>
inline Float_T<Wire, W> Float_T<Wire, W>::operator-() const {
	Float_T<Wire, W> res(*this);
	res[FLOAT_LEN - 1] = !res[FLOAT_LEN - 1];
	return res;
}

template<typename Wire, int W>
inline Float_T<Wire, W> Float_T<Wire, W>::operator^(const Float_T<Wire, W>& rhs) const {
	Float_T<Wire, W> res(*this);
	for (int i = 0; i < FLOAT_LEN; ++i)
		res[i] = res[i] ^ rhs[i];
	return res;
}

template<typename Wire, int W>
inline Float_T<Wire, W> Float_T<Wire, W>::operator^=(const Float_T<Wire, W>& rhs) {
	for (int i = 0; i < FLOAT_LEN; ++i)
		value[i] ^= rhs[i];
	return (*this);
}

template<typename Wire, int W>
inline Float_T<Wire, W> Float_T<Wire, W>::operator&(const Float_T<Wire, W>& rhs) const {
	Float_T<Wire, W> res(*this);
	for (int i = 0; i < FLOAT_LEN; ++i)
		res[i] = res[i] & rhs[i];
	return res;
}

// circuit-backed arithmetic
template <typename Wire, int W>
Float_T<Wire, W> Float_T<Wire, W>::operator+(const Float_T<Wire, W>& rhs) const { return float_binary_<Wire, W>("add", *this, rhs); }
template <typename Wire, int W>
Float_T<Wire, W> Float_T<Wire, W>::operator-(const Float_T<Wire, W>& rhs) const { return float_binary_<Wire, W>("sub", *this, rhs); }
template <typename Wire, int W>
Float_T<Wire, W> Float_T<Wire, W>::operator*(const Float_T<Wire, W>& rhs) const { return float_binary_<Wire, W>("mul", *this, rhs); }
template <typename Wire, int W>
Float_T<Wire, W> Float_T<Wire, W>::operator/(const Float_T<Wire, W>& rhs) const { return float_binary_<Wire, W>("div", *this, rhs); }
template <typename Wire, int W>
Float_T<Wire, W> Float_T<Wire, W>::min(const Float_T<Wire, W>& rhs) const { return float_binary_<Wire, W>("min", *this, rhs); }
template <typename Wire, int W>
Float_T<Wire, W> Float_T<Wire, W>::max(const Float_T<Wire, W>& rhs) const { return float_binary_<Wire, W>("max", *this, rhs); }

template <typename Wire, int W>
Float_T<Wire, W> Float_T<Wire, W>::sqr()   const { return float_unary_<Wire, W>("square", *this); }
template <typename Wire, int W>
Float_T<Wire, W> Float_T<Wire, W>::sqrt()  const { return float_unary_<Wire, W>("sqrt",  *this); }
template <typename Wire, int W>
Float_T<Wire, W> Float_T<Wire, W>::recip() const { return float_unary_<Wire, W>("recip", *this); }
template <typename Wire, int W>
Float_T<Wire, W> Float_T<Wire, W>::rsqrt() const { return float_unary_<Wire, W>("rsqrt", *this); }

template <typename Wire, int W>
Float_T<Wire, W> Float_T<Wire, W>::fma(const Float_T<Wire, W>& b, const Float_T<Wire, W>& c) const {
	return float_ternary_<Wire, W>("fma", *this, b, c);
}

// copysign: |this| with sign of rhs — pure wiring, realized directly on bits.
template <typename Wire, int W>
Float_T<Wire, W> Float_T<Wire, W>::copysign(const Float_T<Wire, W>& rhs) const {
	Float_T<Wire, W> res(*this);
	res.value[FLOAT_LEN - 1] = rhs.value[FLOAT_LEN - 1];
	return res;
}

// circuit-backed comparisons
template <typename Wire, int W>
Bit_T<Wire> Float_T<Wire, W>::equal(const Float_T<Wire, W>& rhs)         const { return float_compare_<Wire, W>("eq", *this, rhs); }
template <typename Wire, int W>
Bit_T<Wire> Float_T<Wire, W>::not_equal(const Float_T<Wire, W>& rhs)     const { return float_compare_<Wire, W>("ne", *this, rhs); }
template <typename Wire, int W>
Bit_T<Wire> Float_T<Wire, W>::less_than(const Float_T<Wire, W>& rhs)     const { return float_compare_<Wire, W>("lt", *this, rhs); }
template <typename Wire, int W>
Bit_T<Wire> Float_T<Wire, W>::less_equal(const Float_T<Wire, W>& rhs)    const { return float_compare_<Wire, W>("le", *this, rhs); }
template <typename Wire, int W>
Bit_T<Wire> Float_T<Wire, W>::greater_than(const Float_T<Wire, W>& rhs)  const { return float_compare_<Wire, W>("gt", *this, rhs); }
template <typename Wire, int W>
Bit_T<Wire> Float_T<Wire, W>::greater_equal(const Float_T<Wire, W>& rhs) const { return float_compare_<Wire, W>("ge", *this, rhs); }

// circuit-backed classifiers
template <typename Wire, int W>
Bit_T<Wire> Float_T<Wire, W>::is_nan()  const { return float_classify_<Wire, W>("isnan",  *this); }
template <typename Wire, int W>
Bit_T<Wire> Float_T<Wire, W>::is_inf()  const { return float_classify_<Wire, W>("isinf",  *this); }
template <typename Wire, int W>
Bit_T<Wire> Float_T<Wire, W>::is_zero() const { return float_classify_<Wire, W>("iszero", *this); }

// ===========================================================================
// 3. Float <-> fixed-point integer conversions (width-generic)
//
// IEEE-754: (-1)^sign · 1.mantissa · 2^(exp_field - BIAS). A non-negative
// integer with `s` fractional bits represents value · 2^-s.
//
// Float -> uint: treat "1.mantissa" as an (SGNFC_LEN+1)-bit unsigned M. The
// value is M · 2^(exp_field - BIAS - SGNFC_LEN); times 2^s shifts M by
// (exp_field - (BIAS + SGNFC_LEN - s)). Negative => right shift; past the
// working width => underflow to 0.
//
// int -> Float: align the leading one to bit SGNFC_LEN, exp_field = firstOneIdx
// + (BIAS - s). 16-bit internal signed arithmetic holds the exponent/shift for
// every width (only the low EXPNT_LEN bits are packed).
//
// Preconditions (bodies do NOT check): finite, non-subnormal input; the result
// fits in the chosen N. Float == 0 (exp_field == 0) returns 0.
// ===========================================================================

// internal signed-arithmetic width for the conversions (exponent/shift index)
static constexpr size_t kFloatConvArithW = 16;

template<typename Wire, int W>
template<int N>
inline UnsignedInt_T<Wire, N> Float_T<Wire, W>::to_unsigned(size_t s) const {
	// 1. (SGNFC_LEN+1)-bit significand: bits[0..SGNFC_LEN-1] = mantissa,
	//    bit[SGNFC_LEN] = implicit 1.
	UnsignedInt_T<Wire, 0> sig(SGNFC_LEN + 1, 0u, PUBLIC);
	for (int i = 0; i < SGNFC_LEN; ++i) sig.bits[i] = value[i];
	sig.bits[SGNFC_LEN] = Bit_T<Wire>(true, PUBLIC);

	// 2. Zero-extend to N bits (receiver width chosen by the caller).
	UnsignedInt_T<Wire, 0> sig_N = sig.resize(N);

	// 3. shift = exp_field - (BIAS + SGNFC_LEN - s).
	constexpr size_t AW = kFloatConvArithW;
	SignedInt_T<Wire, 0> exp_s(AW, 0, PUBLIC);
	for (int i = 0; i < EXPNT_LEN; ++i) exp_s.bits[i] = value[SGNFC_LEN + i];
	int64_t bias_const = (int64_t)(BIAS + SGNFC_LEN) - (int64_t)s;
	SignedInt_T<Wire, 0> shift  = exp_s - SignedInt_T<Wire, 0>(AW, bias_const, PUBLIC);
	SignedInt_T<Wire, 0> nshift = -shift;

	// 4. Apply the shift (both directions computed, unselected garbage masked).
	UnsignedInt_T<Wire, 0> shamtL = shift.as_unsigned().resize(N);
	UnsignedInt_T<Wire, 0> shamtR = nshift.as_unsigned().resize(N);
	UnsignedInt_T<Wire, 0> shifted_left  = sig_N << shamtL;
	UnsignedInt_T<Wire, 0> shifted_right = sig_N >> shamtR;
	Bit_T<Wire> shift_is_neg = shift.bits.back();
	UnsignedInt_T<Wire, 0> shifted = If(shift_is_neg)
	                                     .Then(shifted_right)
	                                     .Else(shifted_left);

	// 5. Zero guard: exp_field == 0 (input ±0 or subnormal) => 0.
	Bit_T<Wire> exp_nonzero = value[SGNFC_LEN];
	for (int i = 1; i < EXPNT_LEN; ++i)
		exp_nonzero = exp_nonzero | value[SGNFC_LEN + i];
	UnsignedInt_T<Wire, 0> zeroN(N, 0u, PUBLIC);
	shifted = If(exp_nonzero).Then(shifted).Else(zeroN);

	// 6. Rebrand as fixed-N return type.
	return UnsignedInt_T<Wire, N>(static_cast<const BitVec_T<Wire>&>(shifted));
}

template<typename Wire, int W>
template<int N>
inline SignedInt_T<Wire, N> Float_T<Wire, W>::to_signed(size_t s) const {
	SignedInt_T<Wire, N> mag = this->template to_unsigned<N>(s).as_signed();
	SignedInt_T<Wire, N> neg = -mag;
	return If(value[FLOAT_LEN - 1]).Then(neg).Else(mag);
}

template<typename Wire, size_t N>
template<int W>
inline Float_T<Wire, W> UnsignedInt_T<Wire, N>::to_float(size_t s) const {
	using F = Float_T<Wire, W>;
	constexpr size_t AW = kFloatConvArithW;

	// Work in runtime-width so shift sizes can mismatch freely.
	UnsignedInt_T<Wire, 0> u = this->resize(this->size());

	// Detect zero (any bit set).
	Bit_T<Wire> any_set = u.bits[0];
	for (size_t i = 1; i < u.size(); ++i) any_set = any_set | u.bits[i];

	// 1. firstOneIdx = (size-1) - leading_zeros.
	UnsignedInt_T<Wire, 0> lz = u.leading_zeros();
	SignedInt_T<Wire, 0> firstOneIdx =
		SignedInt_T<Wire, 0>(AW, (int64_t)(u.size() - 1), PUBLIC)
		- lz.resize(AW).as_signed();

	// 2. Align the leading 1 to bit SGNFC_LEN.
	SignedInt_T<Wire, 0> sgnfc(AW, (int64_t)F::SGNFC_LEN, PUBLIC);
	Bit_T<Wire> leftShift = firstOneIdx < sgnfc;
	SignedInt_T<Wire, 0> shamtL_s = sgnfc       - firstOneIdx;
	SignedInt_T<Wire, 0> shamtR_s = firstOneIdx - sgnfc;
	UnsignedInt_T<Wire, 0> shamtL = shamtL_s.as_unsigned().resize(u.size());
	UnsignedInt_T<Wire, 0> shamtR = shamtR_s.as_unsigned().resize(u.size());
	UnsignedInt_T<Wire, 0> shifted_left  = u << shamtL;
	UnsignedInt_T<Wire, 0> shifted_right = u >> shamtR;
	UnsignedInt_T<Wire, 0> shifted = If(leftShift)
	                                     .Then(shifted_left)
	                                     .Else(shifted_right);

	// 3. IEEE exponent field = firstOneIdx + (BIAS - s).
	int64_t bias_offset = (int64_t)F::BIAS - (int64_t)s;
	SignedInt_T<Wire, 0> exponent = firstOneIdx
		+ SignedInt_T<Wire, 0>(AW, bias_offset, PUBLIC);

	// 4. Pack sign(0) | exponent | mantissa (leading 1 at bit SGNFC_LEN dropped).
	F output;
	for (int i = 0; i < F::FLOAT_LEN; ++i) output.value[i] = Bit_T<Wire>(false, PUBLIC);
	for (int i = 0; i < F::EXPNT_LEN; ++i)
		output.value[F::SGNFC_LEN + i] = exponent.bits[i];
	for (int i = 0; i < F::SGNFC_LEN; ++i)
		output.value[i] = shifted.bits[i];

	// 5. Zero guard.
	F zero_float; for (int i = 0; i < F::FLOAT_LEN; ++i) zero_float.value[i] = Bit_T<Wire>(false, PUBLIC);
	return If(any_set).Then(output).Else(zero_float);
}

template<typename Wire, size_t N>
template<int W>
inline Float_T<Wire, W> SignedInt_T<Wire, N>::to_float(size_t s) const {
	using F = Float_T<Wire, W>;
	UnsignedInt_T<Wire, N> mag = abs();
	F mag_f = mag.template to_float<W>(s);
	mag_f.value[F::FLOAT_LEN - 1] =
		mag_f.value[F::FLOAT_LEN - 1] ^ this->bits.back();
	return mag_f;
}
