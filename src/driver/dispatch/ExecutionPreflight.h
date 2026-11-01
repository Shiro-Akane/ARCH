/**
 * @file ExecutionPreflight.h
 * @brief Shared execution-policy resolution before mesh or backend construction.
 */
#pragma once

#include "driver/dispatch/capability/BackendCapabilities.h"

struct SimConfig;
struct SpeciesManager;

namespace arch::dispatch {
struct ExecutionPreflight {
    ResolvedExecutionPlan plan;
    ExecutionRequirements requirements;
    BackendResolution backend;
};

// SetupChecked and the scoped case boundary selection belong to the caller.
// Only NSE auto selection modifies effective; no runtime resources are owned.
ExecutionPreflight ResolveExecutionPreflight(
    SimConfig& effective, const SpeciesManager& specs, StartupOrder& startup_order);
} // namespace arch::dispatch
