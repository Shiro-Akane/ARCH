/**
 * @file WorkerLimits.h
 * @brief Declare resource limits and failure reporting for bounded workers.
 *
 * Workflow:
 * 1. Accept a bounded, verified request at the read-only API boundary.
 * 2. Declare resource limits and failure reporting for bounded workers.
 * 3. Return typed evidence or an explicit error; do not start the simulation Driver.
 */

#pragma once

#include <cstdint>

#include "api/ApplicationContract.h"

namespace arch::api {
void ApplyInspectionProcessLimits(int cpu_seconds = contract::worker_cpu_seconds);
class SessionProcessLimits {
    std::uint64_t inherited_cpu_ceiling_ = 0;
public:
    SessionProcessLimits();
    void begin_request(int cpu_seconds);
};
}
