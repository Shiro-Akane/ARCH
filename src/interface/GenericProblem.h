/**
 * @file GenericProblem.h
 * @brief A generic bridge between user-defined initialization logic and the solver core.
 * * This class implements the `ProblemGenerator` interface. It abstracts away the
 * * complexities of memory management and parallel iteration.
 * *
 * * Key Responsibilities:
 * * 1. Invokes user callbacks to setup simulation parameters and species.
 * * 2. Manages the main initialization loop (parallelized).
 * * 3. Converts user-friendly Primitive Variables (rho, u, p) into
 * * Solver-friendly Conservative Variables (rho, mom, eng).
 */

#pragma once

#include <vector>

#include "ProblemGenerator.h"

#include "../data/FluidState.h"
#include "../data/UserTypes.h"
#include "../data/GlobalDefs.h"

#include "../grid/Grid.h"

#include "../physics/species/Species.h"
#include "../physics/eos/IdealGas.h"

class GenericProblemGenerator : public ProblemGenerator
{
    // User-provided callback functions
    SetupFunc user_setup;
    InitFunc user_init;

public:
    /**
     * @brief Constructor
     * @param s The user's setup function (configures grid & species).
     * @param i The user's initialization function (sets initial values per point).
     */
    GenericProblemGenerator(SetupFunc s, InitFunc i) : user_setup(s), user_init(i) {}
    std::string GetSolverName() override { return ""; }

    /**
     * @brief Retrieves simulation configuration (Grid, Time, etc.).
     * Note: We pass a dummy SpeciesManager because the user's setup function
     * defines both Config and Species, but here we only extract the Config.
     */
    void Setup(SimConfig &config, SpeciesManager &specs) override
    {
        if (user_setup)
        {
            user_setup(config, specs);
        }
    }

    /**
     * @brief The core initialization routine.
     * Maps the user's "Point-wise" logic to the system's "Array-based" architecture.
     * * @param state Output: The fluid state container to be populated.
     * @param grid  Input: Grid topology.
     * @param eos   Input: Equation of State for variable conversion.
     */
    void InitializeData(FluidState &state, const Grid &grid, const IdealGas &eos) override
    {
        int n_species = state.GetNumSpecies();
        // System handles the loop iteration and parallelization (OpenMP).
        // The user only needs to worry about the physics at a single point (x).

#pragma omp parallel for

        for (int i = 0; i < grid.GetTotalSize(); ++i)
        {
            // 1. Get physical coordinate for the current cell
            double x = grid.GetCellCenter(i);

            // 2. Prepare a "Basket" (PrimitiveData) for the user to fill
            PrimitiveData data;
            data.mass_fractions.resize(n_species, 0.0);

            // 3. Invoke User Logic
            // User fills 'data' based on coordinate 'x' (y, z are 0.0 for 1D)
            user_init(x, 0.0, 0.0, data);

            // 4. Post-Processing: Convert Primitive -> Conservative
            // The solver works with Conservative vars (Momentum, Total Energy),
            // but users think in Primitive vars (Velocity, Pressure).

            // Mass Density
            state.rho[i] = data.rho;

            // Momentum Density: rho * u
            state.mom[i] = data.rho * data.u;

            // Total Energy Density: Computed via EOS using P, rho, u
            state.eng[i] = eos.get_total_energy_primitive(data.rho, data.u, data.p, data.mass_fractions.data());

            // 5. Copy Species Mass Fractions
            for (int k = 0; k < n_species; ++k)
            {
                state.Y(k, i) = data.mass_fractions[k];
            }
        }
    }
};