/**
 * @file helm_eos_loader.h
 * @brief Aggregate declarations of immutable CUDA EOS storage owners.
 *
 * This include-only entry provides no loading or thermodynamic implementation.
 * Individual owners expose the common EOS views with device-resident data.
 * Workflow:
 * 1. Receive verified EOS tables and species metadata.
 * 2. Prepare or bind EOS views for CUDA microphysics.
 * 3. Expose owned device views without retaining caller buffers.
 */

#pragma once

// Aggregate header for callers that need every immutable CUDA EOS
// owner. Implementation translation units include the narrower owner header
// matching their resource responsibility.
#include "cuda/microphysics/network/device_species_owner.h"
#include "cuda/microphysics/eos/owners/helm_eos_device_owner.h"
#include "cuda/microphysics/eos/owners/tabular3_eos_device_owner.h"
#include "cuda/microphysics/eos/owners/tabular4_eos_device_owner.h"
