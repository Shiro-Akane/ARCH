/**
 * @file InitialComposition.h
 * @brief Read declared raw fractions before network-specific normalization.
 *
 * This host preparation step retains original spelling for observation. Missing
 * sparse members use the case-defined zero already checked by the input owner.
 * No floors, normalization or network equations are evaluated here.
 */
#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>
#include "data/GlobalDefs.h"
#include "physics/species/Species.h"

namespace arch::network {
inline std::vector<double> ReadInitialComposition(
    const SimConfig& config, const SpeciesManager& species) {
    config.RequireLoadedValues();
    std::vector<double> result;
    result.reserve(species.count());
    for (int i = 0; i < species.count(); ++i) {
        auto key = "x" + species.get_name(i);
        std::transform(key.begin(), key.end(), key.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        result.push_back(config.Get<double>(key, 0.0));
        if (config.parameter_reads)
            config.parameter_reads->record_unit(config.CaseInputKey(key), "1",
                "core-composition-input-before-normalization");
    }
    return result;
}
} // namespace arch::network
