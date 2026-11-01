/**
 * @file ExecutionPreflight.cpp
 * @brief Expose production Setup and backend resolution as a Run preparation query.
 */
#include "api/ExecutionPreflight.h"

#include "api/protocol/LogCapture.h"
#include "api/protocol/Response.h"
#include "api/resources/WorkerLimits.h"
#include "core/config/RuntimeParams.h"
#include "core/files/FileFingerprint.h"
#include "core/problem/ProblemRegistry.h"
#include "driver/dispatch/ExecutionPreflight.h"
#include "physics/boundary/UserBoundary.h"

namespace arch::api {
namespace {
using detail::Json;

Json response_envelope(const PreviewRequest& request)
{
    return Json::object({{"schemaVersion", contract::schema_version},
        {"kind", "execution-preflight"}, {"version", "1"}, {"status", "error"},
        {"identity", Json::object({{"caseId", request.case_id},
            {"configRevision", core::string_sha256(request.config_text)},
            {"requestId", request.request_id}})},
        {"data", Json()},
        {"execution", Json::object({{"meshConstruction", "not_executed"},
            {"timeStepping", "not_executed"}})},
        {"diagnostics", Json::array()}});
}

const char* backend_name(dispatch::ComputeBackend backend)
{
    switch (backend) {
    case dispatch::ComputeBackend::Cpu: return "cpu";
    case dispatch::ComputeBackend::Cuda: return "cuda";
    case dispatch::ComputeBackend::Auto: return "auto";
    }
    throw std::logic_error("invalid backend id");
}

PreviewResponse serialize(Json& out, int code)
{
    auto response = SerializePreviewResponse(out, code);
    if (response.exit_code != 0) response.exit_code = 3;
    return response;
}
} // namespace

PreviewResponse ExecutionPreflightInputError(
    const PreviewRequest& request, const std::string& message)
{
    auto out = response_envelope(request);
    out["diagnostics"].push(Json::object({{"severity", "error"},
        {"code", "INVALID_REQUEST"}, {"message", message}}));
    return serialize(out, 3);
}

PreviewResponse CheckExecutionPreflight(const PreviewRequest& request)
{
    detail::CaptureLogs logs;
    auto out = response_envelope(request);
    int code = 3;
    try {
        ExecutionPreflightLimits limits;
        auto config = RuntimeParams::LoadText(request.config_text, request.case_id,
            arch::config::ConfigurationPurpose::Evolution);
        auto problem = ProblemRegistry::Get().Create(request.case_id);
        if (!problem) throw std::invalid_argument("Case is not registered in this binary");
        SpeciesManager species;
        const auto prepared = problem->SetupChecked(config, species);
        SimConfig effective = prepared.config();
        auto boundaries = boundary::ResolveCaseBoundaries(
            problem->RegisteredName(), problem->SourceFile(), effective);
        if (boundaries.physical || boundaries.gravity)
            boundaries.identity += ";case-sha=" + problem->SourceSha256();
        boundary::ScopedUserBoundarySelection selection(
            std::move(boundaries), effective, prepared.species());
        dispatch::StartupOrder order;
        const auto ready = dispatch::ResolveExecutionPreflight(
            effective, prepared.species(), order);
        out["data"] = Json::object({
            {"requestedBackend", backend_name(ready.backend.requested_backend)},
            {"resolvedBackend", backend_name(ready.backend.resolved_backend)},
            {"linearSolver", std::string(dispatch::canonical_policy_name<
                dispatch::LinearSolverPolicies>(ready.plan.linear_solver))},
            {"fallbackReason", ready.backend.fallback_reason}});
        out["status"] = "ok";
        code = 0;
    } catch (const std::exception& error) {
        out["diagnostics"].push(Json::object({{"severity", "error"},
            {"code", "RUN_PREFLIGHT_FAILED"}, {"message", error.what()}}));
    }
    if (!logs.info.text.empty())
        out["diagnostics"].push(Json::object({{"severity", "info"},
            {"code", "CORE_LOG"}, {"message", logs.info.message()}}));
    if (!logs.warning.text.empty())
        out["diagnostics"].push(Json::object({{"severity", "warning"},
            {"code", "CORE_LOG"}, {"message", logs.warning.message()}}));
    return serialize(out, code);
}
} // namespace arch::api
