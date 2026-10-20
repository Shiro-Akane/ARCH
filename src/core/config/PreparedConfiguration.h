/**
 * @file PreparedConfiguration.h
 * @brief Own read-only application configuration after checked case preparation.
 *
 * Only ProblemGenerator preparation can construct this snapshot. It owns its
 * species and config; later edits to the preparation objects cannot change it.
 * Backend/resource readiness is established separately by runtime dispatch.
 */
#pragma once
#include "data/GlobalDefs.h"
#include "physics/species/Species.h"

class ProblemGenerator;

namespace arch::config {
class PreparedConfiguration {
    friend class ::ProblemGenerator;
    const SimConfig config_;
    const SpeciesManager species_;
    const ProblemGenerator* model_;
    PreparedConfiguration(const SimConfig& config, const SpeciesManager& species,
                          const ProblemGenerator& model)
        : config_(config), species_(species), model_(&model) {}
public:
    const SimConfig& config() const { return config_; }
    const SpeciesManager& species() const { return species_; }
    bool belongs_to(const ProblemGenerator& model) const { return model_ == &model; }
};
} // namespace arch::config
