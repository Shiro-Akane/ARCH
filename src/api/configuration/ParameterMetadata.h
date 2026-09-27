/**
 * @file ParameterMetadata.h
 * @brief Describe a standard parameter once for both parser and inspection callers.
 *
 * Workflow:
 * 1. Accept a bounded, verified request at the read-only API boundary.
 * 2. Describe a standard parameter once for both parser and inspection callers.
 * 3. Return typed evidence or an explicit error; do not start the simulation Driver.
 */

#pragma once

#include "api/protocol/Json.h"
#include "interface/PreviewMetadata.h"

namespace arch::api {
void PublishParameterMetadata(detail::Json &response, const preview::ParameterReadTrace &reads,
                              const std::vector<preview::AxisPosition> &positions,
                              bool preview_succeeded);
detail::Json ParameterExtensionCapabilities();
} // namespace arch::api
