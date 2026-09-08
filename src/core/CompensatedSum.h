/**
 * @file CompensatedSum.h
 * @brief Fixed-order compensated double summation shared by Host and CUDA.
 */
#pragma once

#include "ArchPortability.h"

#include <cmath>

#if defined(__FAST_MATH__)
#error "ARCH compensated summation requires strict floating-point semantics; use arch_build_contract (no fast-math)."
#endif

namespace arch::math {

class CompensatedSum
{
public:
    // Neumaier compensation, in one fixed order on Host and device.  The
    // build contract forbids reassociation and implicit FMA contraction.
    // Supported inputs and their accumulated result must remain finite.
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

    // Explicit FMA recovers the multiplication residual; it is intentional
    // arithmetic, not compiler reassociation/contraction. Inputs, products and
    // the accumulated result must be finite. Useful for nearly cancelling
    // nuclear mass/production dot products.
    ARCH_INLINE void add_product(double left, double right)
    {
        const double product = left * right;
        const double residual = std::fma(left, right, -product);
        add(product);
        add(residual);
    }

private:
    double sum_ = 0.0;
    double correction_ = 0.0;
};

} // namespace arch::math
