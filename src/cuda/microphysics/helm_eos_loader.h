/**
 * @file helm_eos_loader.h
 * @brief Aggregate declarations of immutable CUDA EOS storage owners.
 *
 * This include-only entry provides no loading or thermodynamic implementation.
 * Individual owners expose the common EOS views with device-resident data.
 */

#pragma once

// Aggregate header for callers that need every immutable CUDA EOS
// owner. Implementation translation units include the narrower owner header
// matching their resource responsibility.
#include "cuda/microphysics/device_species_owner.h"
#include "cuda/microphysics/helm_eos_device_owner.h"
#include "cuda/microphysics/tabular3_eos_device_owner.h"
#include "cuda/microphysics/tabular4_eos_device_owner.h"
