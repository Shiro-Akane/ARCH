#pragma once
#include "interface/ProblemGenerator.h"
namespace arch::driver {
// Shared fresh-start boundary. Does not allocate a pool, evolve time or write output.
inline void InitializeRootState(amr::AMRControl& control, ProblemGenerator& problem,
                                const SimConfig& config, const SpeciesManager& species,
                                ProblemInitializationContext context) {
    control.tree->InitRootGrid(config, species.count());
    problem.InitializeData(control, config, species, context);
}
} // namespace arch::driver
