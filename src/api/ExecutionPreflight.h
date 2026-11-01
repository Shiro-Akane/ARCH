/**
 * @file ExecutionPreflight.h
 * @brief Check a Run request using production startup without constructing its mesh.
 */
#pragma once
#include "api/Preview.h"

namespace arch::api {
PreviewResponse CheckExecutionPreflight(const PreviewRequest& request);
PreviewResponse ExecutionPreflightInputError(
    const PreviewRequest& request, const std::string& message);
} // namespace arch::api
