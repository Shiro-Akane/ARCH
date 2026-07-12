/**
 * @file UserInterface.h
 * @brief Macro definitions for auto-registering simulation problems.
 * * Enables "Plugin-style" development where new problems are added
 * * simply by compiling their source files, without modifying main.cpp.
 */

#pragma once

#include <memory>

#include "ProblemRegistry.h"

#include "../interface/GenericProblem.h"
#include "../data/UserTypes.h"

#include <vector>
#include <functional>

/**
 * @brief ProblemHelper provides high-level physics wrappers to hide internal modules (EOS, Networks) from the user.
 */
namespace ProblemHelper
{
    void SetupNetworkAndFractions(SimConfig &config, SpeciesManager &specs, std::vector<double> &default_X);
    double GetPressureFromRhoT(const SimConfig &config, const SpeciesManager &specs, double rho, double T, const double *X);
    
    // Hidden initialization dispatcher
    void PopulateState(FluidState &state, const Grid &grid, const SimConfig &config, const SpeciesManager &specs,
                       std::function<void(const PointCoords&, PrimitiveData&)> init_callback);
}

/**
 * @brief Macro to register a problem setup automatically.
 * * Uses static initialization to register the problem before main() executes.
 * * @param NAME String literal for the problem ID (e.g., "Sod").
 * @param SETUP_FUNC Callback for global parameter setup (SetupFunc).
 * @param INIT_FUNC Callback for initial data generation (InitFunc).
 */

#define REGISTER_PROBLEM(NAME, SETUP_FUNC, INIT_FUNC)                                                                                     \
    namespace                                                                                                                             \
    {                                                                                                                                     \
        /* Helper struct to trigger registration during static initialization */                                                          \
        struct ProxyRegisterer                                                                                                            \
        {                                                                                                                                 \
            ProxyRegisterer()                                                                                                             \
            {                                                                                                                             \
                /* The constructor runs before main(), registering the callback */                                                        \
                ProblemRegistry::Get().Register(NAME, []() { return std::make_unique<GenericProblemGenerator>(SETUP_FUNC, INIT_FUNC); }); \
            }                                                                                                                             \
        };                                                                                                                                \
                                                                                                                                          \
        /* Static instance forces the constructor to run at program startup */                                                            \
        static ProxyRegisterer global_proxy_instance;                                                                                     \
    }

/**
 * @brief Macro to register a class-based problem automatically.
 * @param NAME String literal for the problem ID.
 * @param CLASS_TYPE The user-defined class that implements Setup() and Init() const.
 */
#define REGISTER_PROBLEM_CLASS(NAME, CLASS_TYPE)                                                                                          \
    namespace                                                                                                                             \
    {                                                                                                                                     \
        struct ProxyRegistererClass                                                                                                       \
        {                                                                                                                                 \
            ProxyRegistererClass()                                                                                                        \
            {                                                                                                                             \
                ProblemRegistry::Get().Register(NAME, []() { return std::make_unique<TypedProblemGenerator<CLASS_TYPE>>(); });            \
            }                                                                                                                             \
        };                                                                                                                                \
        static ProxyRegistererClass global_proxy_class_instance;                                                                          \
    }
