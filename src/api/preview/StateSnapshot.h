/**
 * @file StateSnapshot.h
 * @brief Declare state-snapshot transport independent of a live integration.
 *
 * Workflow:
 * 1. Accept a bounded, verified request at the read-only API boundary.
 * 2. Declare state-snapshot transport independent of a live integration.
 * 3. Return typed evidence or an explicit error; do not start the simulation Driver.
 */

#pragma once

#include "api/Configuration.h"
#include "physics/species/Species.h"

namespace arch::api {
void PublishStateSnapshot(detail::Json& state, const SimConfig& config);
detail::Json SpeciesSnapshot(const SpeciesManager& species);
}
