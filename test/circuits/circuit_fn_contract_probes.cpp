// Compile-fail probes for the circuit-function contract (frontend/circuit_fn.h).
// The positive build (no NEG_CASE) compiles + runs; each NEG_CASE must fail to
// compile with the EXPECTED contract diagnostic (matched by the CMake test's
// PASS_REGULAR_EXPRESSION). C++20.

#include "emp-tool/emp-tool.h"
#include "emp-tool/ir/context/context.h"
#include "emp-tool/circuits/typed.h"
#include "emp-tool/circuits/frontend/circuit_fn.h"
#include "emp-tool/circuits/frontend/rec.h"
#include <cstdio>
#include <type_traits>

using namespace emp;
namespace cf = emp::frontend;

int main() {
    ClearCtx cx;
    auto x = UInt_T<ClearCtx, 32>::constant(cx, 1);
    auto y = UInt_T<ClearCtx, 32>::constant(cx, 2);

#if !defined(NEG_CASE)
    // Positive: compile + compiled run + live run, implicit form.
    auto add = [](auto a, auto b) { return a + b; };
    auto c = cf::compile<rec::UInt<32>, rec::UInt<32>>(add);
    auto r = cf::run(cx, c, x, y);
    auto lr = cf::run(add, x, y);
    printf("circuit_fn contract probe: PASS (%d %d)\n", (int)(r.w[0] & 1), (int)(lr.w[0] & 1));

#elif NEG_CASE == 1   // run + non-const lvalue-ref params -> not callable with prvalues
    auto bad = [](auto& a, auto& b) { return a + b; };
    (void)cf::run(bad, x, y);

#elif NEG_CASE == 2   // compile + non-const lvalue-ref params
    (void)cf::compile<rec::UInt<32>, rec::UInt<32>>([](auto& a, auto& b) { return a + b; });

#elif NEG_CASE == 3   // run + reference return -> must RETURN BY VALUE
    auto bad = [](auto a, auto b) -> decltype(a)& { (void)b; return a; };
    (void)cf::run(bad, x, y);

#elif NEG_CASE == 4   // compile + reference return
    (void)cf::compile<rec::UInt<32>, rec::UInt<32>>(
        [](auto a, auto b) -> decltype(a)& { (void)b; return a; });

#elif NEG_CASE == 5   // compile<int> : input is not a circuit value
    (void)cf::compile<int>([](auto a) { return a; });

#elif NEG_CASE == 6   // run + plain (non-circuit) return -> not a plain value
    auto bad = [](auto a, auto b) { (void)a; (void)b; return 5; };
    (void)cf::run(bad, x, y);

#elif NEG_CASE == 7   // run + void return -> not void
    auto bad = [](auto a, auto b) { (void)a; (void)b; };
    (void)cf::run(bad, x, y);

#elif NEG_CASE == 8   // compile + body callable in BOTH context forms -> ambiguous
    (void)cf::compile<rec::UInt<32>, rec::UInt<32>>(
        [](auto a, auto b, auto... rest) { (void)b; (void)sizeof...(rest); return a; });

#elif NEG_CASE == 9   // compile arg is a circuit value but NOT over RecordCtx
    (void)cf::compile<UInt_T<ClearCtx, 32>>([](auto a) { return a; });
#endif
    return 0;
}
