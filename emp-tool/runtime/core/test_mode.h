#ifndef EMP_TEST_MODE_H__
#define EMP_TEST_MODE_H__

// Test-mode toggle for wire-byte determinism. When enabled, default-
// constructed randomness sources yield a deterministic byte stream so
// two runs of the same protocol produce byte-identical wire output.
//
// Toggle:
//   - $EMP_TEST_MODE=1 in the environment, OR
//   - emp::set_test_mode(true) at program start.
// First call to is_test_mode() caches the env-var read.
//
// Single-threaded determinism only: multiple threads racing for seeds
// from the global counter produce non-reproducible orderings.

#include <atomic>
#include <cstdint>
#include <cstdlib>

namespace emp {

namespace detail {

inline std::atomic<bool>& test_mode_flag() {
    static std::atomic<bool> flag(
        []() {
            const char* v = std::getenv("EMP_TEST_MODE");
            return v != nullptr && v[0] == '1';
        }());
    return flag;
}

// Monotonic seed counter consumed by PRG() and other randomness
// hooks in test mode. Each consumer gets a distinct seed; calls
// across consumers are interleaved by call order.
inline std::atomic<uint64_t>& test_seed_counter() {
    static std::atomic<uint64_t> ctr(0xC0FFEE12345ULL);
    return ctr;
}

// Determinism epoch, bumped by reset_test_seed_counter(). A long-lived
// test-mode PRG (e.g. ECGroup::rand_scalar's thread_local) records the
// epoch it was seeded at and re-seeds when the epoch changes, so a counter
// reset rewinds *every* randomness source -- not just fresh PRG()
// constructions -- keeping them in step across the reset.
inline std::atomic<uint64_t>& test_seed_epoch() {
    static std::atomic<uint64_t> ep(0);
    return ep;
}

}  // namespace detail

// Programmatic toggle. Affects all subsequent PRG()-default-
// constructions and ECGroup::rand_scalar calls. Has no effect on
// PRG instances already constructed.
inline void set_test_mode(bool on) {
    detail::test_mode_flag().store(on);
}

inline bool is_test_mode() {
    return detail::test_mode_flag().load();
}

// Yields the next deterministic seed counter value. Used internally
// by PRG() and ECGroup::rand_scalar in test mode.
inline uint64_t next_test_seed() {
    return detail::test_seed_counter().fetch_add(1);
}

// The current determinism epoch. A test-mode PRG that outlives a single
// unit of work compares against this to learn when a reset_test_seed_counter()
// means it must re-seed.
inline uint64_t current_test_seed_epoch() {
    return detail::test_seed_epoch().load();
}

// Reset the seed counter to its initial value and bump the epoch. Use
// before each independent unit (e.g. each protocol in a trace) to make
// that unit's randomness -- and thus its wire bytes -- independent of
// whatever consumed seeds before it.
inline void reset_test_seed_counter() {
    detail::test_seed_counter().store(0xC0FFEE12345ULL);
    detail::test_seed_epoch().fetch_add(1);
}

}  // namespace emp

#endif  // EMP_TEST_MODE_H__
