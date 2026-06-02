#ifndef EMP_CIRCUIT_VALUE_H__
#define EMP_CIRCUIT_VALUE_H__

#include <type_traits>
#include <vector>

namespace emp {

// Common base/marker for all circuit values (Bit, BitVec, UnsignedInt,
// SignedInt, Float, …). Empty and non-virtual on purpose: Bit_T must stay a
// bare wire (the gate-rate budget forbids a vtable / any indirection), so the
// interface is a member contract, not virtual dispatch. Inheriting this only
// tags the type; each circuit value provides the interface as members:
//
//   using wire_type = Wire;                       // (already from Sortable)
//   template<typename NW> using rebind = Self<NW>;// same shape, different wire
//   int  pack_size() const;                       // number of wires
//   void pack(wire_type* out) const;              // write pack_size() wires
//   void unpack(const wire_type* in, int n);      // (re)load from n wires
//
// `rebind` lets generic code name the same circuit shape over another wire
// (e.g. map a RecWire recording back to the live backend's wire); pack/unpack
// flatten and rebuild it. Generic code (the frontend record/replay layer) uses
// this contract instead of per-type external traits, so a new circuit type
// becomes usable just by providing these members.
//
// The methods are declared here as templated defaults whose bodies fail a
// static_assert: a type that implements a method hides the matching default
// (so it never fires), while a type that FORGETS one gets a clear
// "must implement …" error the moment that method is used. The assert is
// deferred through a dependent trait so it isn't checked unless instantiated.
// This is the zero-overhead stand-in for pure-virtual (no vtable on Bit_T).
template <typename> struct deferred_false : std::false_type {};

struct CircuitValue {
	template <typename Dummy = void>
	int pack_size() const {
		static_assert(deferred_false<Dummy>::value,
		              "circuit value must implement: int pack_size() const");
		return 0;
	}
	template <typename Wire>
	void pack(Wire *) const {
		static_assert(deferred_false<Wire>::value,
		              "circuit value must implement: void pack(wire_type* out) const");
	}
	template <typename Wire>
	void unpack(const Wire *, int) {
		static_assert(deferred_false<Wire>::value,
		              "circuit value must implement: void unpack(const wire_type* in, int n)");
	}
	// Conditional select (mux): cond ? alt : *this. Universal across all circuit
	// values and the basis of the If(cond).Then(a).Else(b) builder (sortable.h).
	template <typename Cond, typename Self>
	Self select(const Cond & /*cond*/, const Self &alt) const {
		static_assert(deferred_false<Self>::value,
		              "circuit value must implement: Self select(const Bit_T<wire_type>& cond, "
		              "const Self& alt) const  // returns cond ? alt : *this");
		return alt;
	}
	// `rebind` is a member alias template — no defaultable body; a missing one
	// surfaces as "no template named 'rebind'".
};

// Generic accessors over the interface above — usable on ANY circuit value
// without per-type external traits. The wire a value carries; the same shape
// over a different wire; flatten to a wire vector; rebuild from wires.
template <typename T> using wire_t = typename std::decay_t<T>::wire_type;
template <typename T, typename NW>
using rebind_t = typename std::decay_t<T>::template rebind<NW>;

template <typename T> inline std::vector<wire_t<T>> pack_wires(const T &v) {
	std::vector<wire_t<T>> w(v.pack_size());
	v.pack(w.data());
	return w;
}
template <typename T, typename Wire> inline T assemble(const Wire *w, int n) {
	T r;
	r.unpack(w, n);
	return r;
}

}  // namespace emp
#endif  // EMP_CIRCUIT_VALUE_H__

