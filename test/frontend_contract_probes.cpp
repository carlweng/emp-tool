// Probes for the frontend pure-circuit contract (circuit_fn_traits in
// executor.h). C++17 — no template lambdas, so this also guards C++17 support.
//
//   - default build (NEG_CASE undefined): the POSITIVE probe — valid generic
//     lambdas, a templated functor, live run, and compile-with-samples all
//     compile and run; prints GOOD!.
//   - NEG_CASE = 1..7: each is a CONTRACT VIOLATION that MUST fail to compile
//     with one clean diagnostic. CMake builds these as compile-fail tests whose
//     output must MATCH the expected contract message (PASS_REGULAR_EXPRESSION),
//     so a regressed/wrong/absent diagnostic fails the test — see
//     test/CMakeLists.txt.
//
// The seven negatives (matching the contract's checks):
//   1  run + non-const lvalue-reference parameter   -> not callable w/ prvalue
//   2  compile + non-const lvalue-reference param    -> not callable w/ prvalue
//   3  run + reference return                        -> must return by value
//   4  compile + reference return                    -> must return by value
//   5  compile<int> (non-circuit input TYPE)         -> input must be circuit value
//   6  run + plain (non-circuit) return              -> must return a circuit value
//   7  run + void return                             -> must return a circuit value, not void
#include "emp-tool/emp-tool.h"
#include "emp-tool/frontend/frontend.h"

// Use the standard block-wire circuit aliases in this test translation unit.
using namespace emp::block_types;
using namespace emp;

// A genuinely WIRE-GENERIC functor (templated operator(), not a class pinned to
// one wire) — usable in both compile (records on RecWire) and live run
// (evaluates on the backend's wire), exactly like a generic lambda.
struct AddFunctor {
  template <class W>
  UInt32_T<W> operator()(UInt32_T<W> a, UInt32_T<W> b) const { return a + b; }
};

#ifndef NEG_CASE

#include <cstdio>
int main() {
  setup_clear_backend("");
  bool ok = true;

  // Valid: generic lambda + wire-generic functor, compiled (no backend needed).
  auto add = [](auto a, auto b) { return a + b; };
  auto c1  = frontend::compile<UInt32, UInt32>(add);
  auto c2  = frontend::compile<UInt32, UInt32>(AddFunctor{});
  ok &= (c1.circuit.count.num_and == 31 && c2.circuit.count.num_and == 31);

  // Valid: the SAME functor live (instantiated on the live wire), a live lambda,
  // and compile-with-samples — all evaluated on the clear backend.
  UInt32 x(32, 40, PUBLIC), y(32, 2, PUBLIC);
  ok &= (frontend::run(add, x, y).reveal<uint32_t>(PUBLIC) == 42);
  ok &= (frontend::run(AddFunctor{}, x, y).reveal<uint32_t>(PUBLIC) == 42);
  auto cs = frontend::compile(AddFunctor{}, x, y);
  ok &= (frontend::run(cs, x, y).reveal<uint32_t>(PUBLIC) == 42);

  printf("frontend_contract_probes (positive): %s\n", ok ? "GOOD!" : "BAD!");
  return ok ? 0 : 1;
}

#else   // NEG_CASE: must fail to compile

void probe() {
#if NEG_CASE == 1
  UInt32 p(32, 1, PUBLIC);
  (void)frontend::run([](auto &x) { return x; }, p);
#elif NEG_CASE == 2
  (void)frontend::compile<UInt32>([](auto &x) { return x; });
#elif NEG_CASE == 3
  UInt32 p(32, 1, PUBLIC);
  (void)frontend::run(
      [](auto x) -> std::decay_t<decltype(x)> & {
        static std::decay_t<decltype(x)> s; (void)x; return s;
      },
      p);
#elif NEG_CASE == 4
  (void)frontend::compile<UInt32>([](auto x) -> std::decay_t<decltype(x)> & {
    static std::decay_t<decltype(x)> s; (void)x; return s;
  });
#elif NEG_CASE == 5
  (void)frontend::compile<int>([](auto x) { return x; });
#elif NEG_CASE == 6
  UInt32 p(32, 1, PUBLIC);
  (void)frontend::run([](auto x) { (void)x; return 7; }, p);
#elif NEG_CASE == 7
  UInt32 p(32, 1, PUBLIC);
  (void)frontend::run([](auto x) { (void)x; /* returns void */ }, p);
#endif
}

#endif
