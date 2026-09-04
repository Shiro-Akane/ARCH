/**
 * @file CompensatedSum.h
 * @brief Fixed-order compensated double summation shared by Host and CUDA.
 */
#pragma once

#include "ArchPortability.h"

#include <cmath>

namespace arch::math {

class CompensatedSum
{
public:
    ARCH_INLINE void add(double term)
    {
        const double next = sum_ + term;
        if (std::abs(sum_) >= std::abs(term))
            correction_ += (sum_ - next) + term;
        else
            correction_ += (term - next) + sum_;
        sum_ = next;
    }

    ARCH_INLINE double value() const { return sum_ + correction_; }

private:
    double sum_ = 0.0;
    double correction_ = 0.0;
};

} // namespace arch::math
