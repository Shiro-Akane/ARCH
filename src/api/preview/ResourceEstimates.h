/**
 * @file ResourceEstimates.h
 * @brief Declare conservative preview resource calculations.
 *
 * Workflow:
 * 1. Accept a bounded, verified request at the read-only API boundary.
 * 2. Declare conservative preview resource calculations.
 * 3. Return typed evidence or an explicit error; do not start the simulation Driver.
 */

#pragma once

#include "api/Configuration.h"

namespace arch::api {
std::int64_t PaddedCells(int dimension);
detail::Json AmrResourceMetadata(const SimConfig&, int species_count = -1);
PreviewResponse EstimateAmrResources(const PreviewRequest&);
}
