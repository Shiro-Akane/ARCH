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
#ifdef _WIN32
struct WindowsResourceSnapshot {
    std::uint64_t commit_limit_bytes = 0;
    std::uint64_t job_user_limit_100ns = 0;
    std::uint64_t job_user_time_100ns = 0;
    std::uint32_t limit_flags = 0;
    std::uint32_t end_of_job_action = 0;
    bool commit_limit_enforced = false;
    bool job_user_limit_enforced = false;
    bool job_terminate_at_limit = false;
};
#endif
class SessionProcessLimits {
    std::uint64_t inherited_cpu_ceiling_ = 0;
#ifdef _WIN32
    void* resource_job_ = nullptr;
#endif
public:
    SessionProcessLimits();
    ~SessionProcessLimits();
    SessionProcessLimits(const SessionProcessLimits&) = delete;
    SessionProcessLimits& operator=(const SessionProcessLimits&) = delete;
    void begin_request(int cpu_seconds);
#ifdef _WIN32
    WindowsResourceSnapshot inspect_windows_resource_limits() const;
#endif
};
}
