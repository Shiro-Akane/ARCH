#pragma once
#include "api/protocol/Json.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
namespace arch::api {
// Presentation facts only: do not clamp, shift, abs or otherwise change data.
struct ValueDomain {
    std::int64_t positive = 0, zero = 0, negative = 0, nonfinite = 0;
    double min_positive = std::numeric_limits<double>::infinity(), max_positive = 0;
    void observe(double x) {
        if (!std::isfinite(x)) ++nonfinite;
        else if (x == 0) ++zero;
        else if (x < 0) ++negative;
        else { ++positive; min_positive = std::min(min_positive, x); max_positive = std::max(max_positive, x); }
    }
    detail::Json json() const {
        using detail::Json;
        return Json::object({{"positiveCount", positive}, {"zeroCount", zero}, {"negativeCount", negative},
            {"nonFiniteCount", nonfinite}, {"canLog", positive > 0},
            {"minPositive", positive ? Json(min_positive) : Json()},
            {"maxPositive", positive ? Json(max_positive) : Json()}});
    }
};
} // namespace arch::api
