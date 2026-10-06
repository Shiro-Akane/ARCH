/**
 * @file burn_controller_input.h
 * @brief Load the actual BD inputs for a focused controller witness.
 *
 * Reuse the production case declaration but never construct Setup/Init. The
 * witness itself owns EOS and burn-cell preparation, not a second case recipe.
 */
#pragma once
#include "core/config/RuntimeParams.h"
#include "BurnOneZone/Configuration.h"

namespace burn_controller_fixture {
inline SimConfig load(const std::string& root) {
    static const bool registered = [] {
        ProblemRegistry::Get().Register("BurnOneZone",
            []() -> std::unique_ptr<ProblemGenerator> {
                throw std::runtime_error("controller witness must not construct a model");
            }, {"BurnOneZone", "production-declaration", true, arch::cases::BurnOneZoneConfiguration});
        return true;
    }();
    (void)registered;
    return RuntimeParams::Load(root + "/validation/burn/inputs/bd-config-v3.par", "BurnOneZone");
}
} // namespace burn_controller_fixture
