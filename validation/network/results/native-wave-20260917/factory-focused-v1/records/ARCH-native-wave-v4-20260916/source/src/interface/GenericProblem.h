/**
 * @file GenericProblem.h
 * @brief Adapt callback or typed problem initializers to the common interface.
 *
 * Adapters retain user setup/initialization logic and forward primitive-value
 * callbacks to ProblemHelper::detail::PopulateState. That shared helper owns
 * host-block traversal and EOS-based conservative conversion; these adapters
 * neither implement numerical evolution nor own backend device storage.
 */

#pragma once

#include <functional>
#include <vector>

#include "ProblemGenerator.h"

#include "../amr/AMRControl.h"
#include "../core/ProblemHelper.h"
#include "../data/FluidState.h"
#include "../data/GlobalDefs.h"
#include "../data/UserTypes.h"
#include "../grid/Grid.h"
#include "../physics/species/Species.h"

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

    /**
     * @brief Invokes the registered callback to configure the simulation and species.
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
     * @param amr_ctrl Output: Host AMR blocks whose fields are populated.
     * @param config Input: Geometry and physical configuration.
     * @param specs Input: Registered composition layout.
     * @param context Input: Selected EOS identity for initialization.
     */
    void InitializeData(amr::AMRControl &amr_ctrl, const SimConfig &config,
                        const SpeciesManager &specs,
                        ProblemInitializationContext context) override
    {
        ProblemHelper::detail::PopulateState(amr_ctrl, config, specs, context,
            [&](const PointCoords& p, PrimitiveData& data) {
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

    void Setup(SimConfig &config, SpeciesManager &specs) override
    {
        user_model.Setup(config, specs);
    }

    void InitializeData(amr::AMRControl &amr_ctrl, const SimConfig &config,
                        const SpeciesManager &specs,
                        ProblemInitializationContext context) override
    {
        ProblemHelper::detail::PopulateState(amr_ctrl, config, specs, context,
            [&](const PointCoords& p, PrimitiveData& data) {
            user_model.Init(p, data);
        });
    }
};
