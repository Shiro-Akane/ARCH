/**
 * @file ConfigurationIdentity.h
 * @brief Freeze typed runtime configuration and accepted EOS provenance for output.
 *
 * Workflow:
 * 1. Receive the final immutable config, accepted EOS identity and resolved plan.
 * 2. Encode values with explicit types and exact binary64 bit patterns.
 * 3. Cache the resulting PlotSourceIdentity in the run's output owner.
 *
 * The raw parser-text digest is independent of the effective-configuration digest.
 */
#pragma once

#include <string>

#include "driver/dispatch/capability/ResolvedExecutionPlan.h"
#include "grid/GridGeometryView.h"
#include "io/hdf5/HDF5Writer.h"

struct SimConfig;

namespace arch::config {
/** Encode all final typed controls and private declared model/material inputs. */
std::string canonical_configuration_record(const SimConfig& config);

/** Build formal provenance once after scientific startup and actual backend resolution. */
io::PlotSourceIdentity make_plot_source_identity(
    const SimConfig& config, const io::CheckpointProvenance& provenance,
    const arch::dispatch::ResolvedExecutionPlan& plan,
    arch::dispatch::ComputeBackend backend,
    GridMetrics::GeometrySemantics semantics);
} // namespace arch::config
