#include "fixtures/core/ArrayFillCases.h"
#include <iostream>

template<class T> bool check_cases()
{
    const auto& bits = arch::test::ClassificationCases<T>::bits;
    for (std::size_t index = 0; index < bits.size(); ++index) {
        if (!arch::test::fill_pattern<T>(bits[index])) {
            std::cerr << "array fill mismatch size=" << sizeof(T)
                      << " pattern=" << index << '\n';
            return false;
        }
    }
    return true;
}

int main()
{
    if (!check_cases<float>() || !check_cases<double>()
        || !arch::test::fill_aliased_value()) return 1;
    std::cout << "FIXED_ARRAY_FILL_PASS float=16 double=16 elements=448 alias=1\n";
}
