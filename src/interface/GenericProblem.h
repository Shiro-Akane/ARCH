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
#include <functional>

namespace ProblemHelper {
    void PopulateState(FluidState &state, const Grid &grid, const SimConfig &config, const SpeciesManager &specs,
                       std::function<void(const PointCoords&, PrimitiveData&)> init_callback);
}

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
        ProblemHelper::PopulateState(state, grid, config, specs, [&](const PointCoords& p, PrimitiveData& data) {
            user_init(p, data);
        });
    }
};

/**
 * @brief A generic bridge between Object-Oriented problem logic and the solver core.
 * @tparam T The user-defined Problem Class, which should implement Setup() and Init() const.
 */
template <typename T>
class TypedProblemGenerator : public ProblemGenerator
{
    T user_model;

public:
    TypedProblemGenerator() = default;

    std::string GetSolverName() override { return ""; }

    void Setup(SimConfig &config, SpeciesManager &specs) override
    {
        user_model.Setup(config, specs);
    }

    void InitializeData(FluidState &state, const Grid &grid, const SimConfig &config, const SpeciesManager &specs) override
    {
        ProblemHelper::PopulateState(state, grid, config, specs, [&](const PointCoords& p, PrimitiveData& data) {
            user_model.Init(p, data);
        });
    }
};