/**
 * @file PreviewSession.h
 * @brief Expose the session preview entry point while keeping protocol internals private.
 *
 * Workflow:
 * 1. Accept a bounded, verified request at the read-only API boundary.
 * 2. Expose the session preview entry point while keeping protocol internals private.
 * 3. Return typed evidence or an explicit error; do not start the simulation Driver.
 */

#pragma once

#include "api/protocol/Json.h"

namespace arch::api {
detail::Json PreviewSessionCapability();
int RunPreviewSession();
}
