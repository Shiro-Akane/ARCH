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
#include "api/ApplicationContract.h"

namespace arch::api {
detail::Json PreviewSessionCapability();
#ifdef _WIN32
// One metadata owner serves session, AMR and case inspection capabilities.
inline detail::Json WindowsPreviewResourcePolicy() {
    return detail::Json::object({{"kind", contract::windows_preview_resource_policy},
        {"memoryKind", "committed"}, {"processCommitMiB", contract::windows_worker_commit_mib},
        {"cpuClock", "user"}, {"cpuScope", "resource-job"}, {"budgetRenewal", "per-request"}});
}
#endif
int RunPreviewSession();
}
