/**
 * @file InitialMesh.h
 * @brief Create the initial Driver mesh from a registered case and resolved configuration.
 *
 * Workflow:
 * 1. Receive the registered case, resolved configuration and empty root topology.
 * 2. Initialize root blocks and ask the case to fill primitive fields.
 * 3. Return Native active interiors as provisional cell averages; the real
 *    EOS-bound Runtime completes physical BC/exchange/EOS before publication.
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
    control.tree->InitRootGrid(config, species.count(), context.geometry_semantics);
    if(context.geometry_semantics==GridMetrics::GeometrySemantics::AxisymmetricRz) {
        // Validate logical BC compatibility before calling the initializer.
        // Native Init fills actual active V/W means only. A temporary unbound
        // handler cannot certify physical ghosts: bound Runtime initialization
        // performs the real whole-domain phases and selected-EOS acceptance.
        BCHandler boundaries(config,context.geometry_semantics);
        for(int id:control.tree->GetActiveBlocks())
            (void)boundaries.logical_plan(control.pool->GetBlock(id).grid);
        problem.InitializeData(control, config, species, context);
    } else {
        problem.InitializeData(control, config, species, context);
    }
}
} // namespace arch::driver
