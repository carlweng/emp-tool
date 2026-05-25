#ifndef EMP_CIRCUIT_TYPES_H__
#define EMP_CIRCUIT_TYPES_H__

// Tools for binding the backend-independent circuit templates (Bit_T<Wire>,
// UnsignedInt_T<Wire,N>, …) to a concrete wire type. The wire is a property of
// the backend (sh2pc uses one `block`; an authenticated-GC backend uses two),
// so the concrete aliases live wherever the backend is chosen, not in the
// circuit headers. Include this, then at the backend-decision site write one
// statement (each macro injects the aliases into namespace emp itself — no
// `namespace emp { … }` wrapper needed):
//
//     EMP_USE_CIRCUIT_TYPES_ALL(block);                       // the whole set
//     EMP_USE_CIRCUIT_TYPES(block, Bit, UInt32, SHA256_Calculator);  // a subset
//     // -> using Bit = Bit_T<block>; using UInt32 = UInt32_T<block>; …
//
// EMP_USE_CIRCUIT_TYPES_AS appends a suffix so two wires can coexist:
//
//     EMP_USE_CIRCUIT_TYPES_AS(GenWire, _g, Bit, UInt32);     // Bit_g, UInt32_g
//
// Tie it to a backend by giving the backend a `wire_type` member and passing
// `MyBackend::wire_type` as the wire. Relies on the X / X_T naming every
// circuit type already follows (Bit/Bit_T, UInt32/UInt32_T, …). Invoke at
// namespace (file) scope — the macro opens `namespace emp` for you.

// --- token paste tolerant of an empty suffix -------------------------------
#define EMP_CAT_(a, b) a##b
#define EMP_CAT(a, b)  EMP_CAT_(a, b)

// one alias:  using <Name><Suffix> = <Name>_T<Wire>;
#define EMP_DEF_ONE(WIRE, SUF, NAME) \
	using EMP_CAT(NAME, SUF) = EMP_CAT(NAME, _T)<WIRE>;

// --- FOR_EACH(M, A, B, x...) -> M(A,B,x) for each x (up to 24) --------------
#define EMP_EXPAND(...) __VA_ARGS__
#define EMP_FE_1(M,A,B,x)      M(A,B,x)
#define EMP_FE_2(M,A,B,x,...)  M(A,B,x) EMP_EXPAND(EMP_FE_1(M,A,B,__VA_ARGS__))
#define EMP_FE_3(M,A,B,x,...)  M(A,B,x) EMP_EXPAND(EMP_FE_2(M,A,B,__VA_ARGS__))
#define EMP_FE_4(M,A,B,x,...)  M(A,B,x) EMP_EXPAND(EMP_FE_3(M,A,B,__VA_ARGS__))
#define EMP_FE_5(M,A,B,x,...)  M(A,B,x) EMP_EXPAND(EMP_FE_4(M,A,B,__VA_ARGS__))
#define EMP_FE_6(M,A,B,x,...)  M(A,B,x) EMP_EXPAND(EMP_FE_5(M,A,B,__VA_ARGS__))
#define EMP_FE_7(M,A,B,x,...)  M(A,B,x) EMP_EXPAND(EMP_FE_6(M,A,B,__VA_ARGS__))
#define EMP_FE_8(M,A,B,x,...)  M(A,B,x) EMP_EXPAND(EMP_FE_7(M,A,B,__VA_ARGS__))
#define EMP_FE_9(M,A,B,x,...)  M(A,B,x) EMP_EXPAND(EMP_FE_8(M,A,B,__VA_ARGS__))
#define EMP_FE_10(M,A,B,x,...) M(A,B,x) EMP_EXPAND(EMP_FE_9(M,A,B,__VA_ARGS__))
#define EMP_FE_11(M,A,B,x,...) M(A,B,x) EMP_EXPAND(EMP_FE_10(M,A,B,__VA_ARGS__))
#define EMP_FE_12(M,A,B,x,...) M(A,B,x) EMP_EXPAND(EMP_FE_11(M,A,B,__VA_ARGS__))
#define EMP_FE_13(M,A,B,x,...) M(A,B,x) EMP_EXPAND(EMP_FE_12(M,A,B,__VA_ARGS__))
#define EMP_FE_14(M,A,B,x,...) M(A,B,x) EMP_EXPAND(EMP_FE_13(M,A,B,__VA_ARGS__))
#define EMP_FE_15(M,A,B,x,...) M(A,B,x) EMP_EXPAND(EMP_FE_14(M,A,B,__VA_ARGS__))
#define EMP_FE_16(M,A,B,x,...) M(A,B,x) EMP_EXPAND(EMP_FE_15(M,A,B,__VA_ARGS__))
#define EMP_FE_17(M,A,B,x,...) M(A,B,x) EMP_EXPAND(EMP_FE_16(M,A,B,__VA_ARGS__))
#define EMP_FE_18(M,A,B,x,...) M(A,B,x) EMP_EXPAND(EMP_FE_17(M,A,B,__VA_ARGS__))
#define EMP_FE_19(M,A,B,x,...) M(A,B,x) EMP_EXPAND(EMP_FE_18(M,A,B,__VA_ARGS__))
#define EMP_FE_20(M,A,B,x,...) M(A,B,x) EMP_EXPAND(EMP_FE_19(M,A,B,__VA_ARGS__))
#define EMP_FE_21(M,A,B,x,...) M(A,B,x) EMP_EXPAND(EMP_FE_20(M,A,B,__VA_ARGS__))
#define EMP_FE_22(M,A,B,x,...) M(A,B,x) EMP_EXPAND(EMP_FE_21(M,A,B,__VA_ARGS__))
#define EMP_FE_23(M,A,B,x,...) M(A,B,x) EMP_EXPAND(EMP_FE_22(M,A,B,__VA_ARGS__))
#define EMP_FE_24(M,A,B,x,...) M(A,B,x) EMP_EXPAND(EMP_FE_23(M,A,B,__VA_ARGS__))
#define EMP_FE_PICK(_1,_2,_3,_4,_5,_6,_7,_8,_9,_10,_11,_12,_13,_14,_15,_16, \
                    _17,_18,_19,_20,_21,_22,_23,_24,NAME,...) NAME
#define EMP_FOR_EACH(M, A, B, ...)                                          \
	EMP_EXPAND(EMP_FE_PICK(__VA_ARGS__, EMP_FE_24,EMP_FE_23,EMP_FE_22,       \
		EMP_FE_21,EMP_FE_20,EMP_FE_19,EMP_FE_18,EMP_FE_17,EMP_FE_16,         \
		EMP_FE_15,EMP_FE_14,EMP_FE_13,EMP_FE_12,EMP_FE_11,EMP_FE_10,         \
		EMP_FE_9,EMP_FE_8,EMP_FE_7,EMP_FE_6,EMP_FE_5,EMP_FE_4,EMP_FE_3,      \
		EMP_FE_2,EMP_FE_1)(M, A, B, __VA_ARGS__))

// Emit `using <name><suffix> = <name>_T<WIRE>;` for each primitive — bare, with
// no namespace wrapper. Used by the public macros below and by code that is
// already inside `namespace emp`.
#define EMP_CIRCUIT_TYPES_RAW(WIRE, SUFFIX, ...) \
	EMP_FOR_EACH(EMP_DEF_ONE, WIRE, SUFFIX, __VA_ARGS__)

// Public: bind a subset of primitives into namespace emp (bare names).
#define EMP_USE_CIRCUIT_TYPES(WIRE, ...) \
	namespace emp { EMP_CIRCUIT_TYPES_RAW(WIRE, , __VA_ARGS__) }

// Public: same, with SUFFIX appended to each alias so two wires can coexist.
#define EMP_USE_CIRCUIT_TYPES_AS(WIRE, SUFFIX, ...) \
	namespace emp { EMP_CIRCUIT_TYPES_RAW(WIRE, SUFFIX, __VA_ARGS__) }

// Public: bind the whole standard primitive set into namespace emp. The names
// are listed literally so they reach the FOR_EACH as distinct arguments; this
// is the single source of truth for "all circuit types" — add a primitive by
// adding one name here.
#define EMP_USE_CIRCUIT_TYPES_ALL(WIRE)                          \
	namespace emp { EMP_CIRCUIT_TYPES_RAW(WIRE, ,               \
		Bit, BitVec, UnsignedInt, SignedInt, Float,             \
		UInt8, UInt16, UInt32, UInt64, Int8, Int16, Int32, Int64, \
		AES_Calculator, AES_128_CTR_Calculator,                 \
		Keccak_F_Calculator, SHA3_256_Calculator, SHA256_Calculator) }

// --- explicit class instantiations (a per-wire build optimization) ---------
// Separate from the aliases above because instantiation needs the real class
// template with explicit widths (UnsignedInt_T<W,32>, not the UInt32_T alias).
// Apply with EMP_EXTERN_TEMPLATE (in the binding header) and
// EMP_INSTANTIATE_TEMPLATE (in one .cpp). Variadic so the `<W, N>` comma
// survives being passed as a macro argument.
#define EMP_EXTERN_TEMPLATE(...)      extern template class __VA_ARGS__;
#define EMP_INSTANTIATE_TEMPLATE(...)        template class __VA_ARGS__;

#define EMP_CIRCUIT_CLASS_LIST(X, W)                                         \
	X(Bit_T<W>)                                                             \
	X(BitVec_T<W>)                                                          \
	X(UnsignedInt_T<W, 0>)  X(UnsignedInt_T<W, 8>)  X(UnsignedInt_T<W, 16>) \
	X(UnsignedInt_T<W, 32>) X(UnsignedInt_T<W, 64>)                         \
	X(SignedInt_T<W, 0>)    X(SignedInt_T<W, 8>)    X(SignedInt_T<W, 16>)   \
	X(SignedInt_T<W, 32>)   X(SignedInt_T<W, 64>)                           \
	X(Float_T<W>)                                                          \
	X(AES_Calculator_T<W>)         X(AES_128_CTR_Calculator_T<W>)           \
	X(Keccak_F_Calculator_T<W>)    X(SHA3_256_Calculator_T<W>)              \
	X(SHA256_Calculator_T<W>)

#endif  // EMP_CIRCUIT_TYPES_H__
