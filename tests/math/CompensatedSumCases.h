#pragma once

#include "core/CompensatedSum.h"

namespace arch::test {

struct SumCase {
    double terms[16];
    int count;
    double expected;
    bool products = false;
};

// Exact reference values chosen independently of the implementation.  Feed
// them through memory on both backends so an optimized build exercises the
// reduction, rather than folding a single literal arithmetic expression.
inline constexpr SumCase compensated_sum_cases[]{
    {{1.0e16, 1.0, -1.0e16}, 3, 1.0},
    {{-1.0e16, -1.0, 1.0e16}, 3, -1.0},
    {{1.0, 1.0e16, -1.0e16}, 3, 1.0},
    {{1.0, 0x1p-53, -1.0, 0x1p-53}, 4, 0x1p-52},
    {{1.0, 1.0e-200, -1.0}, 3, 1.0e-200},
    {{1.0, 0x1p-1074, -1.0}, 3, 0x1p-1074},
    {{0x1p60, 1.0, 2.0, 3.0, -0x1p60}, 5, 6.0},
    {{0x1p60, 1.0, -0x1p60, 0x1p60, 2.0, -0x1p60}, 6, 3.0},
    {{0.0}, 0, 0.0},
    {{1.0 + 0x1p-27, 1.0 - 0x1p-27, -1.0, 1.0}, 4, -0x1p-54, true},
    {{-1.0 - 0x1p-27, 1.0 - 0x1p-27, 1.0, 1.0}, 4, 0x1p-54, true},
};

ARCH_INLINE double evaluate_sum_case(const SumCase& sample)
{
    math::CompensatedSum sum;
    for (int term = 0; term < sample.count; term += sample.products ? 2 : 1) {
        if (sample.products) sum.add_product(sample.terms[term], sample.terms[term + 1]);
        else sum.add(sample.terms[term]);
    }
    return sum.value();
}

// Volatile applies only to test input staging, never to production math.
// Even with LTO the Host must read runtime terms before evaluating the sum.
inline double evaluate_runtime_sum_case(const SumCase& sample)
{
    volatile double runtime_terms[16]{};
    SumCase staged{};
    staged.count = sample.count;
    staged.products = sample.products;
    for (int term = 0; term < sample.count; ++term)
        runtime_terms[term] = sample.terms[term];
    for (int term = 0; term < sample.count; ++term)
        staged.terms[term] = runtime_terms[term];
    return evaluate_sum_case(staged);
}

} // namespace arch::test
