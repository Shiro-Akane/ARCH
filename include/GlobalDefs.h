/**
 * @file GlobalDefs.h
 * @brief Stable case-facing configuration, state diagnostics and CGS constants.
 *
 * Workflow:
 * 1. Include <GlobalDefs.h> alongside <UserInterface.h> in a user case.
 * 2. Read typed SimConfig values during Setup and set primitive fields in Init.
 * 3. Use arch::constants for shared CGS values such as math::pi and
 *    gravity::cgs::gravitational_constant. The defining implementation remains
 *    private to src, so cases need no src include path.
 */
#pragma once

#include "data/GlobalDefs.h"
#include "physics/constant/PhysicalConstants.h"
