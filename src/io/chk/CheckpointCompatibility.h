/**
 * @file CheckpointCompatibility.h
 * @brief Builds and validates the one Host checkpoint scientific identity.
 */

#pragma once

#include <string_view>

#include "../../driver/dispatch/ResolvedExecutionPlan.h"
#include "../hdf5/HDF5Writer.h"

struct SimConfig;
struct SpeciesManager;

namespace io {

/** Build provenance from the table bytes that the EOS loader accepted. */
CheckpointProvenance make_checkpoint_provenance(
    const SimConfig& config, const SpeciesManager& species,
    arch::dispatch::EosId resolved_eos,
    bool burn_enabled, std::string_view active_network, bool nse_enabled,
    std::string_view loaded_eos_table_sha256);

/** Inspect the current table on disk for pre-load restart verification. */
CheckpointProvenance inspect_checkpoint_provenance(
    const SimConfig& config, const SpeciesManager& species,
    arch::dispatch::EosId resolved_eos,
    bool burn_enabled, std::string_view active_network, bool nse_enabled);

/**
 * Validate a saved identity against the running executable/configuration.
 *
 * @return false only for a v1/v2 checkpoint without identity metadata.
 * @throws std::runtime_error for a present but incompatible identity.
 */
bool require_checkpoint_provenance_compatible(
    const CheckpointProvenance& saved,
    const CheckpointProvenance& expected);

/** Close the restart verification-to-EOS-load replacement window. */
void require_loaded_eos_table_compatible(
    std::string_view restart_verified_sha256,
    std::string_view loaded_eos_table_sha256);

} // namespace io
