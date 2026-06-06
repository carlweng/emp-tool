#ifndef EMP_CIRCUIT_TYPES_H__
#define EMP_CIRCUIT_TYPES_H__

#include <cstddef>

namespace emp {

// Width sentinel for integer circuit types whose width is stored at runtime.
inline constexpr std::size_t runtime_width = 0;

}  // namespace emp

// Tools for binding the backend-independent circuit templates (Bit_T<Wire>,
// UnsignedInt_T<Wire,N>, ...) to a concrete wire type. The wire is a property of
// the backend (sh2pc uses one `block`; an authenticated-GC backend uses two),
// so bare concrete aliases live wherever the backend is chosen, not in the
// core template headers or implicitly in emp. Include this, then open the
// namespace where the aliases should live and bind the full standard set:
//
//     namespace emp::my_wire_types {
//     EMP_CIRCUIT_TYPES_ALL(MyBackend::wire_type);            // the whole set
//     }
//     // -> using Bit = emp::Bit_T<MyBackend::wire_type>; ...
//
// EMP_CIRCUIT_TYPES_ALL_AS appends a suffix so two wires can coexist:
//
//     EMP_CIRCUIT_TYPES_ALL_AS(GenWire, _g);                  // Bit_g, UInt32_g
//
// Tie it to a backend by giving the backend a `wire_type` member and passing
// `MyBackend::wire_type` as the wire. Relies on the X / X_T naming every
// circuit type already follows (Bit/Bit_T, UInt32/UInt32_T, ...). Invoke at
// namespace scope.

// --- token paste tolerant of an empty suffix -------------------------------
#define EMP_CAT_(a, b) a##b
#define EMP_CAT(a, b)  EMP_CAT_(a, b)

// The standard circuit surface. Each row carries the public alias name and the
// exact class template specialization it names; alias binding and explicit
// instantiation both derive from this one list.
#define EMP_CIRCUIT_TYPE_LIST(APPLY, CTX, WIRE, SUFFIX)                      \
	APPLY(CTX, WIRE, SUFFIX, Bit, Bit_T<WIRE>)                               \
	APPLY(CTX, WIRE, SUFFIX, BitVec, BitVec_T<WIRE>)                         \
	APPLY(CTX, WIRE, SUFFIX, UnsignedInt, UnsignedInt_T<WIRE, emp::runtime_width>) \
	APPLY(CTX, WIRE, SUFFIX, SignedInt, SignedInt_T<WIRE, emp::runtime_width>) \
	APPLY(CTX, WIRE, SUFFIX, Float16, Float_T<WIRE, 16>)                     \
	APPLY(CTX, WIRE, SUFFIX, Float32, Float_T<WIRE, 32>)                     \
	APPLY(CTX, WIRE, SUFFIX, Float64, Float_T<WIRE, 64>)                     \
	APPLY(CTX, WIRE, SUFFIX, UInt8, UnsignedInt_T<WIRE, 8>)                  \
	APPLY(CTX, WIRE, SUFFIX, UInt16, UnsignedInt_T<WIRE, 16>)                \
	APPLY(CTX, WIRE, SUFFIX, UInt32, UnsignedInt_T<WIRE, 32>)                \
	APPLY(CTX, WIRE, SUFFIX, UInt64, UnsignedInt_T<WIRE, 64>)                \
	APPLY(CTX, WIRE, SUFFIX, Int8, SignedInt_T<WIRE, 8>)                     \
	APPLY(CTX, WIRE, SUFFIX, Int16, SignedInt_T<WIRE, 16>)                   \
	APPLY(CTX, WIRE, SUFFIX, Int32, SignedInt_T<WIRE, 32>)                   \
	APPLY(CTX, WIRE, SUFFIX, Int64, SignedInt_T<WIRE, 64>)                   \
	APPLY(CTX, WIRE, SUFFIX, AES_Calculator, AES_Calculator_T<WIRE>)         \
	APPLY(CTX, WIRE, SUFFIX, AES_128_CTR_Calculator, AES_128_CTR_Calculator_T<WIRE>) \
	APPLY(CTX, WIRE, SUFFIX, Keccak_F_Calculator, Keccak_F_Calculator_T<WIRE>) \
	APPLY(CTX, WIRE, SUFFIX, SHA3_256_Calculator, SHA3_256_Calculator_T<WIRE>) \
	APPLY(CTX, WIRE, SUFFIX, SHA256_Calculator, SHA256_Calculator_T<WIRE>)

// One alias row: using <Name><Suffix> = emp::<TemplateSpecialization>;
#define EMP_CIRCUIT_TYPE_ALIAS(_CTX, _WIRE, SUFFIX, NAME, ...) \
	using EMP_CAT(NAME, SUFFIX) = emp::__VA_ARGS__;

// Public: bind the whole standard primitive set in the current namespace.
// `Float` is a convenience alias for Float32 (Float_T<WIRE, 32>), appended here
// rather than as a list row so it is not separately instantiated/extern-declared.
#define EMP_CIRCUIT_TYPES_ALL_AS(WIRE, SUFFIX) \
	EMP_CIRCUIT_TYPE_LIST(EMP_CIRCUIT_TYPE_ALIAS, EMP_CIRCUIT_TYPES_ALIAS_CTX, WIRE, SUFFIX) \
	using EMP_CAT(Float, SUFFIX) = EMP_CAT(Float32, SUFFIX);

// Public: bind the whole standard primitive set without a suffix.
#define EMP_CIRCUIT_TYPES_ALL(WIRE) \
	EMP_CIRCUIT_TYPES_ALL_AS(WIRE, )

// --- explicit class instantiations (a per-wire build optimization) ---------
// Apply with EMP_EXTERN_TEMPLATE (in the binding header) and
// EMP_INSTANTIATE_TEMPLATE (in one .cpp).
#define EMP_EXTERN_TEMPLATE(...)      extern template class __VA_ARGS__;
#define EMP_INSTANTIATE_TEMPLATE(...)        template class __VA_ARGS__;

#define EMP_CIRCUIT_CLASS_ENTRY(ACTION, _WIRE, _SUFFIX, _NAME, ...) ACTION(__VA_ARGS__)

#define EMP_CIRCUIT_CLASS_LIST(ACTION, WIRE) \
	EMP_CIRCUIT_TYPE_LIST(EMP_CIRCUIT_CLASS_ENTRY, ACTION, WIRE, )

#endif  // EMP_CIRCUIT_TYPES_H__
