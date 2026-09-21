#pragma once
#include "api/protocol/Json.h"
#include "api/Preview.h"
#include "data/GlobalDefs.h"

namespace arch::api {
detail::Json ConfigurationSchema();
detail::Json RefinementMetadata(const SimConfig& config);
detail::Json DiffusionMetadata(const SimConfig& config);
PreviewResponse InspectConfiguration(const PreviewRequest& request);
detail::Json ConfigurationExtensions();
detail::Json CoordinateMetadata(const GridConfig& grid, const std::string& unit_system);
std::string UnitSystem(const SimConfig& config);
std::string FieldUnit(const std::string& key, const std::string& system);
std::string AxisUnit(const std::string& label, const std::string& system);
} // namespace arch::api
