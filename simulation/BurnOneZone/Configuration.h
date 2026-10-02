/**
 * @file Configuration.h
 * @brief Declare the one-zone inputs without constructing or initializing a model.
 */
#pragma once
#include "core/config/CaseConfiguration.h"

namespace arch::cases {
inline config::CaseConfiguration BurnOneZoneConfiguration(
    const config::StandardInputResolution& inputs)
{
    arch::config::CaseConfiguration result;
    result.complete = true;
    result.consumers.needs_network = true;
    result.consumers.needs_temperature_floor = false;
    result.composition = arch::config::DescribeNetworkComposition(inputs);
    if (result.composition->complete)
        result.consumers.needs_composition_floor = !result.composition->keys.empty();
    result.parameters = {
        {"rho0", "float", "g/cm^3", "verification"},
        {"temperature0", "float", "K", "verification"}};
    return result;
}
} // namespace arch::cases
