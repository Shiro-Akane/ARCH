#include "math/ClassificationCases.h"
#include <bit>
#include <iostream>
#include <limits>

template<class T> bool check_cases()
{
    const auto& bits = arch::test::ClassificationCases<T>::bits;
    for (std::size_t i = 0; i < bits.size(); ++i) {
        const auto value = std::bit_cast<T>(bits[i]);
        if (arch::test::classify(value) != arch::test::classification_expected[i]) {
            std::cerr << "host classification mismatch size=" << sizeof(T)
                      << " case=" << i << '\n';
            return false;
        }
    }
    return true;
}
int main()
{
    if (!check_cases<float>() || !check_cases<double>()) return 1;
    // Preserve the host's native long-double classification as well.
    for (const auto value : {0.0L, -0.0L, std::numeric_limits<long double>::max(),
             std::numeric_limits<long double>::infinity(),
             std::numeric_limits<long double>::quiet_NaN()}) {
        const auto expected = (std::isfinite(value) ? 1u : 0u)
            | (std::isnan(value) ? 2u : 0u) | (std::signbit(value) ? 4u : 0u);
        if (arch::test::classify(value) != expected) return 1;
    }
    std::cout << "MATH_CLASSIFICATION_PASS float=16 double=16 host-long-double\n";
}
