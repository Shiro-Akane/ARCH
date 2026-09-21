#pragma once
#include "Configuration.h"
namespace arch::api {
std::int64_t PaddedCells(int dimension);
detail::Json AmrResourceMetadata(const SimConfig&, int species_count = -1);
PreviewResponse EstimateAmrResources(const PreviewRequest&);
}
