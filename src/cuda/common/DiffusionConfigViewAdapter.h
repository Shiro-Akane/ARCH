/**
 * @file DiffusionConfigViewAdapter.h
 * @brief Warning-contained import of the committed diffusion configuration view.
 *
 * This header intentionally contains no declarations or logic.  The
 * DiffusionConfigView authority remains in DiffFlux.h; this adapter only keeps
 * warnings from its protected legacy include graph out of ordinary-C++ public
 * interface translation units.
 */

#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC system_header
#endif

#include "numerics/diffusion/DiffFlux.h"
