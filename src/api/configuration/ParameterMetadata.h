#pragma once

#include "api/protocol/Json.h"
#include "interface/PreviewMetadata.h"

namespace arch::api {
void PublishParameterMetadata(detail::Json &response, const preview::ParameterReadTrace &reads,
                              const std::vector<preview::AxisPosition> &positions,
                              bool preview_succeeded);
detail::Json ParameterExtensionCapabilities();
} // namespace arch::api
