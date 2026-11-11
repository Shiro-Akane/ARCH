/**
 * @file CudaMpiStagingPlan.cpp
 * @brief Build the staging witness topology with the ordinary C++ compiler.
 *
 * NVCC 12.8 does not emit the defaulted C++20 comparison operators used by
 * the Host planner. Keep that authority on the same side as production.
 */
#include "amr/exchange/ExchangePlan.h"

amr::SameLevelExchangePlan make_staging_plan(int dimension, int level,
    const std::array<std::int32_t, 3>& extent)
{
    const int count = 1 << dimension;
    std::vector<amr::SameLevelTopologyEntry> topology(count);
    for (int b = 0; b < count; ++b) {
        topology[b].logical = {dimension, level, unsigned(b & 1), unsigned((b >> 1) & 1), unsigned((b >> 2) & 1)};
        topology[b].handle = {{std::uint64_t(b + 1)}, {17}};
    }
    for (int b = 0; b < count; ++b)
        for (int axis = 0; axis < dimension; ++axis)
            for (int side = 0; side < 2; ++side)
                topology[b].neighbors[2 * axis + side] = topology[b ^ (1 << axis)].logical;
    return amr::make_same_level_exchange_plan(topology, dimension, extent, 2, {17});
}
