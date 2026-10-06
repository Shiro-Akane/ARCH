#pragma once
#include "core/config/PreparedConfiguration.h"
#include "driver/SolverDispatch.h"

namespace arch::config {
/** Frozen effective configuration after checked preparation and policy resolution.
 * Only the production startup owner can construct it; Driver cannot accept a
 * raw SimConfig or a different species registry in its place.
 */
class RuntimeConfiguration {
    friend void ::DispatchSolver(ProblemGenerator&, const arch::config::PreparedConfiguration&);
    const SimConfig config_;
    const SpeciesManager species_;
    RuntimeConfiguration(const SimConfig& effective, const PreparedConfiguration& prepared)
        : config_(effective), species_(prepared.species()) {}
public:
    const SimConfig& config() const { return config_; }
    const SpeciesManager& species() const { return species_; }
};
} // namespace arch::config
