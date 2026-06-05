// Float_T<Wire> arithmetic/comparison ops as thin wrappers over the cached
// built-in circuits. Each op loads its argument bits, runs the shared evaluator
// (execute_program) over the process-wide-cached BooleanProgram, and rebuilds
// the typed result. The gate data lives once as an embedded .empbc blob
// (float_builtins.cpp), not as a giant array re-materialized inside every
// templated method on every call.
//
// Included from float32.h, inside namespace emp, after Float_T is defined.

// Compute dispatcher over Bit_T<Wire> slots: each op is realized with the Bit
// operators, which issue the matching call on the active Backend — so a builtin
// circuit replays identically to the old hand-inlined gate loop.
template <typename Wire>
struct Float32ComputeDispatcher {
	void and_gate(Bit_T<Wire>& o, const Bit_T<Wire>& a, const Bit_T<Wire>& b) { o = a & b; }
	void xor_gate(Bit_T<Wire>& o, const Bit_T<Wire>& a, const Bit_T<Wire>& b) { o = a ^ b; }
	void not_gate(Bit_T<Wire>& o, const Bit_T<Wire>& a)                       { o = !a; }
	// public_label is the mandatory const-gate API; Bit(v, PUBLIC) routes
	// through the optional (no-op-by-default) feed() instead.
	void const_gate(Bit_T<Wire>& o, bool v)                                   { backend->public_label(&o.bit, v); }
};

// Run a builtin over `num_in` packed input bits, writing `num_out` output bits.
// The scratch is thread_local so repeated float ops on a thread reuse one wire
// buffer; the cached program is immutable and safely shared.
template <typename Wire>
inline void run_float_builtin(emp::circuit::BuiltinCircuit id,
                              const Bit_T<Wire>* in, size_t num_in,
                              Bit_T<Wire>* out, size_t num_out) {
	static thread_local emp::circuit::CircuitScratch<Bit_T<Wire>> scratch;
	emp::circuit::execute_program<Bit_T<Wire>>(
	    emp::circuit::builtin_circuit(id), in, num_in, out, num_out, scratch,
	    Float32ComputeDispatcher<Wire>{});
}

// ---- binary Float -> Float ----
template <typename Wire>
inline Float_T<Wire> float32_binary_(emp::circuit::BuiltinCircuit id,
                                     const Float_T<Wire>& a, const Float_T<Wire>& b) {
	Bit_T<Wire> in[64], out[32];
	for (int i = 0; i < 32; ++i) { in[i] = a.value[i]; in[i + 32] = b.value[i]; }
	run_float_builtin<Wire>(id, in, 64, out, 32);
	Float_T<Wire> res;
	for (int i = 0; i < 32; ++i) res.value[i] = out[i];
	return res;
}

// ---- unary Float -> Float ----
template <typename Wire>
inline Float_T<Wire> float32_unary_(emp::circuit::BuiltinCircuit id, const Float_T<Wire>& a) {
	Bit_T<Wire> in[32], out[32];
	for (int i = 0; i < 32; ++i) in[i] = a.value[i];
	run_float_builtin<Wire>(id, in, 32, out, 32);
	Float_T<Wire> res;
	for (int i = 0; i < 32; ++i) res.value[i] = out[i];
	return res;
}

// ---- binary Float -> Bit (compare) ----
template <typename Wire>
inline Bit_T<Wire> float32_compare_(emp::circuit::BuiltinCircuit id,
                                    const Float_T<Wire>& a, const Float_T<Wire>& b) {
	Bit_T<Wire> in[64], out[1];
	for (int i = 0; i < 32; ++i) { in[i] = a.value[i]; in[i + 32] = b.value[i]; }
	run_float_builtin<Wire>(id, in, 64, out, 1);
	return out[0];
}

template <typename Wire>
Float_T<Wire> Float_T<Wire>::operator+(const Float_T<Wire>& rhs) const { return float32_binary_(emp::circuit::BuiltinCircuit::Float32Add, *this, rhs); }
template <typename Wire>
Float_T<Wire> Float_T<Wire>::operator-(const Float_T<Wire>& rhs) const { return float32_binary_(emp::circuit::BuiltinCircuit::Float32Sub, *this, rhs); }
template <typename Wire>
Float_T<Wire> Float_T<Wire>::operator*(const Float_T<Wire>& rhs) const { return float32_binary_(emp::circuit::BuiltinCircuit::Float32Mul, *this, rhs); }
template <typename Wire>
Float_T<Wire> Float_T<Wire>::operator/(const Float_T<Wire>& rhs) const { return float32_binary_(emp::circuit::BuiltinCircuit::Float32Div, *this, rhs); }

template <typename Wire>
Float_T<Wire> Float_T<Wire>::sqr()   const { return float32_unary_(emp::circuit::BuiltinCircuit::Float32Sq,   *this); }
template <typename Wire>
Float_T<Wire> Float_T<Wire>::sqrt()  const { return float32_unary_(emp::circuit::BuiltinCircuit::Float32Sqrt, *this); }
template <typename Wire>
Float_T<Wire> Float_T<Wire>::sin()   const { return float32_unary_(emp::circuit::BuiltinCircuit::Float32Sin,  *this); }
template <typename Wire>
Float_T<Wire> Float_T<Wire>::cos()   const { return float32_unary_(emp::circuit::BuiltinCircuit::Float32Cos,  *this); }
template <typename Wire>
Float_T<Wire> Float_T<Wire>::exp()   const { return float32_unary_(emp::circuit::BuiltinCircuit::Float32Exp,  *this); }
template <typename Wire>
Float_T<Wire> Float_T<Wire>::exp2()  const { return float32_unary_(emp::circuit::BuiltinCircuit::Float32Exp2, *this); }
template <typename Wire>
Float_T<Wire> Float_T<Wire>::ln()    const { return float32_unary_(emp::circuit::BuiltinCircuit::Float32Ln,   *this); }
template <typename Wire>
Float_T<Wire> Float_T<Wire>::log2()  const { return float32_unary_(emp::circuit::BuiltinCircuit::Float32Log2, *this); }

template <typename Wire>
Bit_T<Wire> Float_T<Wire>::equal(const Float_T<Wire>& rhs)      const { return float32_compare_(emp::circuit::BuiltinCircuit::Float32Eq,  *this, rhs); }
template <typename Wire>
Bit_T<Wire> Float_T<Wire>::less_than(const Float_T<Wire>& rhs)  const { return float32_compare_(emp::circuit::BuiltinCircuit::Float32Le,  *this, rhs); }
template <typename Wire>
Bit_T<Wire> Float_T<Wire>::less_equal(const Float_T<Wire>& rhs) const { return float32_compare_(emp::circuit::BuiltinCircuit::Float32Leq, *this, rhs); }
