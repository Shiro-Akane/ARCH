/**
 * @file InitialMesh.h
 * @brief Create the initial Driver mesh from a registered case and resolved configuration.
 *
 * Workflow:
 * 1. Receive the registered case, resolved configuration and empty root topology.
 * 2. Initialize root blocks and ask the case to fill primitive fields.
 * 3. Return active interiors as provisional cell averages; the real EOS-bound
 *    Runtime completes physical BC/exchange/EOS before publication.
 */

#pragma once

#include "driver/DriverUtils.h"
#include "interface/ProblemGenerator.h"
#include "physics/boundary/PhysicalBoundaryHandler.h"

namespace arch::driver {
// Shared fresh-start boundary. Does not allocate a pool, evolve time or write output.
inline void InitializeRootState(amr::AMRControl& control, ProblemGenerator& problem,
                                const SimConfig& config, const SpeciesManager& species,
                                ProblemInitializationContext context) {
    control.tree->InitRootGrid(config, species.count());
    problem.InitializeData(control, config, species, context);
}
} // namespace arch::driver
