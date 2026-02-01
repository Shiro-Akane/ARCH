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
