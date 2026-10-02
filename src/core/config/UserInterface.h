/**
 * @file UserInterface.h
 * @brief Case-facing registration macros and public initialization helpers.
 *
 * Workflow:
 * 1. A case includes this header and GlobalDefs.h as its complete ARCH surface.
 * 2. Registration macros publish class- or function-based cases before main().
 * 3. Boundary macros publish named physical/gravity callbacks for the case.
 * 4. ProblemHelper exposes network/EOS Setup helpers without leaking internals.
 */

#pragma once

#include <memory>

#ifndef ARCH_CASE_SOURCE_SHA256
#define ARCH_CASE_SOURCE_SHA256 ""
#endif

#include "core/problem/ProblemHelper.h"
#include "core/problem/ProblemRegistry.h"

#include "physics/boundary/UserBoundary.h"

#include "data/UserTypes.h"
#include "interface/GenericProblem.h"

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
                ProblemRegistry::Get().Register(NAME, []() { return std::make_unique<GenericProblemGenerator>(SETUP_FUNC, INIT_FUNC); }, {__FILE__, ARCH_CASE_SOURCE_SHA256, true}); \
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
                ProblemRegistry::Get().Register(NAME, []() { return std::make_unique<TypedProblemGenerator<CLASS_TYPE>>(); }, {__FILE__, ARCH_CASE_SOURCE_SHA256, true});            \
            }                                                                                                                             \
        };                                                                                                                                \
        static ProxyRegistererClass global_proxy_class_instance;                                                                          \
    }

/**
 * @brief Token-paste helper that expands arguments before concatenation.
 * @param LHS Left token or prefix.
 * @param RHS Right token or suffix (often __COUNTER__).
 */
#define ARCH_BOUNDARY_CONCAT_IMPL(LHS, RHS) LHS##RHS
#define ARCH_BOUNDARY_CONCAT(LHS, RHS) ARCH_BOUNDARY_CONCAT_IMPL(LHS, RHS)

/**
 * @brief Shared expansion for the four boundary registration macros.
 * Every invocation gets unique registrar and instance names from __COUNTER__,
 * so one translation unit may register many boundaries without redefinition.
 * @param REGISTRAR_PREFIX Unique struct-name prefix for this callback kind.
 * @param INSTANCE_PREFIX Unique static-instance prefix for this callback kind.
 * @param REGISTER_MEMBER BoundaryRegistry member (RegisterPhysical/Gravity).
 * @param NAME Case-facing registration name.
 * @param CALLBACK Free function or default-constructible callable class value.
 * @param ID Unique integer from __COUNTER__.
 */
#define ARCH_BOUNDARY_REGISTER_IMPL(REGISTRAR_PREFIX, INSTANCE_PREFIX, REGISTER_MEMBER, NAME, CALLBACK, ID)                              \
    namespace                                                                                                                            \
    {                                                                                                                                    \
        /* Registrar type that publishes one boundary callback at static init. */                                                        \
        struct ARCH_BOUNDARY_CONCAT(REGISTRAR_PREFIX, ID)                                                                                 \
        {                                                                                                                                \
            ARCH_BOUNDARY_CONCAT(REGISTRAR_PREFIX, ID)()                                                                                  \
            {                                                                                                                            \
                ::arch::boundary::BoundaryRegistry::Get().REGISTER_MEMBER(NAME, CALLBACK, __FILE__, ARCH_BOUNDARY_SOURCE_SHA256);         \
            }                                                                                                                            \
        };                                                                                                                               \
        /* Static instance forces the registrar constructor before main(). */                                                            \
        static ARCH_BOUNDARY_CONCAT(REGISTRAR_PREFIX, ID) ARCH_BOUNDARY_CONCAT(INSTANCE_PREFIX, ID);                                      \
    }

/**
 * @brief Register a physical boundary callback (free function or functor).
 * @param NAME Case name resolved later by BoundaryRegistry::Resolve.
 * @param FUNC PhysicalBoundaryData(const PhysicalBoundaryContext&) callable.
 */
#define REGISTER_PHYSICAL_BOUNDARY(NAME, FUNC)                                                                                           \
    ARCH_BOUNDARY_REGISTER_IMPL(ArchPhysicalBoundaryRegistrar_, arch_physical_boundary_registrar_, RegisterPhysical, NAME, FUNC,          \
                                __COUNTER__)

/**
 * @brief Register a gravity boundary callback (free function or functor).
 * @param NAME Case name resolved later by BoundaryRegistry::Resolve.
 * @param FUNC GravityBoundaryData(const GravityBoundaryContext&) callable.
 */
#define REGISTER_GRAVITY_BOUNDARY(NAME, FUNC)                                                                                            \
    ARCH_BOUNDARY_REGISTER_IMPL(ArchGravityBoundaryRegistrar_, arch_gravity_boundary_registrar_, RegisterGravity, NAME, FUNC,             \
                                __COUNTER__)

/**
 * @brief Register a default-constructed physical boundary callback class.
 * @param NAME Case name resolved later by BoundaryRegistry::Resolve.
 * @param TYPE Default-constructible class with a const call operator.
 */
#define REGISTER_PHYSICAL_BOUNDARY_CLASS(NAME, TYPE)                                                                                     \
    ARCH_BOUNDARY_REGISTER_IMPL(ArchPhysicalBoundaryClassRegistrar_, arch_physical_boundary_class_registrar_, RegisterPhysical, NAME,      \
                                TYPE{}, __COUNTER__)

/**
 * @brief Register a default-constructed gravity boundary callback class.
 * @param NAME Case name resolved later by BoundaryRegistry::Resolve.
 * @param TYPE Default-constructible class with a const call operator.
 */
#define REGISTER_GRAVITY_BOUNDARY_CLASS(NAME, TYPE)                                                                                      \
    ARCH_BOUNDARY_REGISTER_IMPL(ArchGravityBoundaryClassRegistrar_, arch_gravity_boundary_class_registrar_, RegisterGravity, NAME,         \
                                TYPE{}, __COUNTER__)
