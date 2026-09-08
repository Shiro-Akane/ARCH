/**
 * @file test_compensated_sum.cpp
 * @brief Check compensated sums using runtime-fed host reference cases.
 *
 * The shared fixture includes cancellation and tiny residuals. Runtime input
 * staging ensures the optimizer does not replace the test with constant results.
 */
#include "../math/CompensatedSumCases.h"

#include <iomanip>
#include <iostream>

int main()
{
    for (const auto& sample : arch::test::compensated_sum_cases) {
        const double actual = arch::test::evaluate_runtime_sum_case(sample);
        if (actual != sample.expected) {
            std::cerr << std::setprecision(17)
                      << "compensated sum: expected=" << sample.expected
                      << " actual=" << actual << '\n';
            return 1;
        }
    }
    std::cout << "COMPENSATED_SUM_PASS\n";
}
