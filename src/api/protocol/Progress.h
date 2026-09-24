/**
 * @file Progress.h
 * @brief Describe progress events shared by preview and inspection responses.
 *
 * Workflow:
 * 1. Accept a bounded, verified request at the read-only API boundary.
 * 2. Describe progress events shared by preview and inspection responses.
 * 3. Return typed evidence or an explicit error; do not start the simulation Driver.
 */

#pragma once

#include "api/Preview.h"
#include "api/protocol/Json.h"

namespace arch::api {
/** Publish a named bounded progress stage to the response and callback. */
inline void ReportStage(const PreviewRequest& request, detail::Json& result, const char* stage) {
    result["stage"] = stage;
    if (request.progress) request.progress(stage);
}
}
