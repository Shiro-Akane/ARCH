/**
 * @file RefinementThermodynamics.h
 * @brief Evaluate refinement thermodynamics through the existing EOS bridge without retaining transient state.
 *
 * Workflow:
 * 1. Read active cells and topology from their AMR owner.
 * 2. Evaluate refinement thermodynamics through the existing EOS bridge without retaining transient state.
 * 3. Expose only stable bindings or derived indicators to downstream solvers.
 */

#pragma once

#include "amr/topology/AmrTree.h"

namespace amr {
// One EOS batch adapter for the simulation driver and the CPU initial mesh preview.
/** Bind the shared EOS evaluator for density, pressure, temperature and gamma1 refinement fields. */
template<class EosPolicy>
void BindRefinementThermodynamics(AmrTree& tree, const EosPolicy& eos) {
    tree.SetThermodynamicEvaluator([&eos](const FluidState& state,
                                                     std::vector<double>* pressure,
                                                     std::vector<double>* temperature,
                                                     std::vector<double>* gamma1) {
        const int total_size = static_cast<int>(state.rho.size());
        const int n_species = state.GetNumSpecies();
        if (pressure) pressure->assign(total_size, 0.0);
        if (temperature) temperature->assign(total_size, 0.0);
        if (gamma1) gamma1->assign(total_size, std::numeric_limits<double>::quiet_NaN());
        std::vector<double> Xi(n_species, 0.0);
        for (int index = 0; index < total_size; ++index) {
            for (int species = 0; species < n_species; ++species)
                Xi[species] = state.X(species, index);
            const auto values = amr::indicator::thermodynamics(
                state.get(index), Xi.data(), eos,
                pressure != nullptr, temperature != nullptr, gamma1 != nullptr);
            if (pressure) (*pressure)[index] = values.pressure;
            if (temperature) (*temperature)[index] = values.temperature;
            if (gamma1) (*gamma1)[index] = values.gamma1;
        }
    });
}
} // namespace amr
