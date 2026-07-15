#pragma once

#include "Dual.h"

namespace timmes {

struct RatePair {
    double forward;
    double reverse;
    double forward_dT = 0.0;
    double reverse_dT = 0.0;
};

struct RateValueAccessor {
    static constexpr bool tracks_temperature = false;

    TIMMES_HD static double forward(const RatePair& pair) { return pair.forward; }
    TIMMES_HD static double reverse(const RatePair& pair) { return pair.reverse; }
};

struct RateTemperatureAccessor {
    static constexpr bool tracks_temperature = true;

    TIMMES_HD static Dual<1> forward(const RatePair& pair)
    {
        Dual<1> result(pair.forward);
        result.deriv[0] = pair.forward_dT;
        return result;
    }

    TIMMES_HD static Dual<1> reverse(const RatePair& pair)
    {
        Dual<1> result(pair.reverse);
        result.deriv[0] = pair.reverse_dT;
        return result;
    }
};

} // namespace timmes
