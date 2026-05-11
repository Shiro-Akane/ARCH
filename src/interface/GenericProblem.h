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
    void InitializeData(FluidState &state, const Grid &grid, const SimConfig &config, const SpeciesManager &specs) override
    {
        IdealGas eos(config.physics.gamma, specs);
        int n_species = state.GetNumSpecies();

        int stride_y = grid.stride_y;
        int stride_z = grid.stride_z;
        int total_size = grid.GetTotalSize();
        // System handles the loop iteration and parallelization (OpenMP).
        // The user only needs to worry about the physics at a single point (x).

#pragma omp parallel
        {
            // Per-thread buffer: allocated once per thread, reused across iterations
            PrimitiveData data{};
            data.mass_fractions.resize(n_species, 0.0);

#pragma omp for schedule(static)
            for (int idx = 0; idx < total_size; ++idx)
            {
                int k = idx / stride_z;
                int rem = idx % stride_z;
                int j = rem / stride_y;
                int i = rem % stride_y;

                PointCoords p = grid.GetPhysicalCoords(i, j, k);

                data.rho = 0.0;
                data.u = 0.0;
                data.v = 0.0;
                data.w = 0.0;
                data.p = 0.0;
                std::fill(data.mass_fractions.begin(), data.mass_fractions.end(), 0.0);

                user_init(p, data);

                state.rho[idx] = data.rho;
                state.mom_x[idx] = data.rho * data.u;
                state.mom_y[idx] = data.rho * data.v;
                state.mom_z[idx] = data.rho * data.w;
                state.eng[idx] = eos.get_total_energy_primitive(data.rho, data.u, data.v, data.w, data.p, data.mass_fractions.data());

                for (int s = 0; s < n_species; ++s)
                    state.Y(s, idx) = data.mass_fractions[s];
            }
        }
    }
};