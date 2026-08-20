/**
 * @file UserInterface.h
 * @brief Case-facing registration macros and public initialization helpers.
 *
 * Workflow:
 * 1. A case includes this header and GlobalDefs.h as its complete ARCH surface.
 * 2. Registration macros publish class- or function-based cases before main().
 * 3. ProblemHelper exposes network/EOS Setup helpers without leaking internals.
 */

#pragma once

#include <memory>

#include "ProblemHelper.h"
#include "ProblemRegistry.h"

#include "../data/UserTypes.h"
#include "../interface/GenericProblem.h"

/**
 * @brief Macro to register a problem setup automatically.
 * Uses static initialization to register the problem before main() executes.
 * @param NAME String literal for the problem ID (e.g., "Sod").
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
