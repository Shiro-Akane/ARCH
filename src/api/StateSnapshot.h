#pragma once
#include "Configuration.h"
#include "../physics/species/Species.h"
namespace arch::api {
void PublishStateSnapshot(detail::Json& state, const SimConfig& config);
detail::Json SpeciesSnapshot(const SpeciesManager& species);
}
