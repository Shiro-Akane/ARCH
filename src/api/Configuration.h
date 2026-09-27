/**
 * @file Configuration.h
 * @brief Declare configuration schema and resolved-input inspection responses.
 *
 * Workflow:
 * 1. Accept a bounded, verified request at the read-only API boundary.
 * 2. Declare configuration schema and resolved-input inspection responses.
 * 3. Return typed evidence or an explicit error; do not start the simulation Driver.
 */

#pragma once

#include "api/Preview.h"
#include "api/protocol/Json.h"
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
