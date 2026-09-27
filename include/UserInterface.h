/**
 * @file UserInterface.h
 * @brief Stable case-facing problem registration and initialization interface.
 *
 * Workflow:
 * 1. Include <UserInterface.h> and <GlobalDefs.h> in a case translation unit.
 * 2. Implement Setup to validate case parameters and register composition.
 * 3. Implement Init to fill primitive state in CGS units at each cell center.
 * 4. Register the case once with REGISTER_PROBLEM_CLASS; main discovers it.
 *
 * The implementation and its transitive dependencies stay owned by src/core.
 */
#pragma once

#include "core/config/UserInterface.h"
