#pragma once

// Compatibility umbrella for callers that need every immutable CUDA EOS
// owner. Implementation translation units include the narrower owner header
// matching their resource responsibility.
#include "cuda/microphysics/device_species_owner.h"
#include "cuda/microphysics/helm_eos_device_owner.h"
#include "cuda/microphysics/tabular3_eos_device_owner.h"
#include "cuda/microphysics/tabular4_eos_device_owner.h"
