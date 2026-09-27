/**
 * @file PreviewSession.cpp
 * @brief Coordinate repeated preview requests with invalidation when input identity changes.
 *
 * Workflow:
 * 1. Accept a bounded, verified request at the read-only API boundary.
 * 2. Coordinate repeated preview requests with invalidation when input identity changes.
 * 3. Return typed evidence or an explicit error; do not start the simulation Driver.
 */

#include <chrono>
#include <iostream>
#include <limits>
#include <set>

#include "api/PreviewSession.h"

#include "api/CaseInspection.h"
#include "api/Configuration.h"
#include "api/preview/ResourceEstimates.h"
#include "api/resources/WorkerLimits.h"
#include "api/session/InitialSampleCache.h"
#include "api/session/SessionInput.h"
#include "core/files/FileFingerprint.h"
#include "core/files/VerifiedFileCache.h"
#include "physics/eos/eosdispatch.h"

namespace arch::api {
namespace {
using detail::Json;
using contract::Command;
using Clock = std::chrono::steady_clock;
/** Measure elapsed request time for session events. */
double milliseconds(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now()-start).count();
}
std::string required(const detail::SessionObject& input, const std::string& name) {
    const auto it = input.find(name);
    if (it == input.end() || !std::holds_alternative<std::string>(it->second))
        throw std::invalid_argument("Expected string member: " + name);
    return std::get<std::string>(it->second);
}
/** Lower a parsed session object to a typed preview request. */
PreviewRequest request_from(const detail::SessionObject& input, Command command) {
    PreviewRequest request;
    request.case_id = required(input, "caseId");
    request.config_text = required(input, "configText");
    request.request_id = required(input, "requestId");
    request.initial_mesh = command == Command::Mesh;
    if (request.case_id.empty() || request.case_id.size()>128 ||
        request.request_id.empty() || request.request_id.size()>128 ||
        request.config_text.empty() || request.config_text.size()>max_config_bytes)
        throw std::invalid_argument("Require nonempty caseId/requestId <= 128 bytes and configText <= 1 MiB");
    for (const auto& [key, value] : input) {
        if (key=="command" || key=="caseId" || key=="configText" || key=="requestId") continue;
        if (!std::holds_alternative<std::int64_t>(value))
            throw std::invalid_argument("Expected integer member: " + key);
        const auto n = std::get<std::int64_t>(value);
        if (n<std::numeric_limits<int>::min() || n>std::numeric_limits<int>::max())
            throw std::invalid_argument("Integer member exceeds int range: " + key);
        if (command == Command::Fields && key=="samples") { request.sample_count=int(n); request.sample_count_provided=true; }
        else if (command == Command::Fields && key=="samplesX1") request.samples_x1=int(n);
        else if (command == Command::Fields && key=="samplesX2") request.samples_x2=int(n);
        else if (command == Command::Mesh && key=="meshMaxBlocks") {
            if (n<1 || n>contract::mesh_max_blocks) throw std::invalid_argument("meshMaxBlocks must be 1..1024");
            request.mesh_max_blocks=int(n);
        } else if (command == Command::Mesh && key=="meshMemoryMiB") {
            if (n<contract::mesh_min_memory_mib || n>contract::mesh_max_memory_mib)
                throw std::invalid_argument("meshMemoryMiB must be 16..256");
            request.mesh_memory_mib=int(n);
        } else throw std::invalid_argument("Unknown or inapplicable member: " + key);
    }
    return request;
}
// Bound before allocation. Oversize/truncated frames close the session rather
// than attempting to resynchronize an arbitrarily large byte stream.
/** Read one bounded protocol line while retaining malformed-input evidence. */
bool line(std::string& out) {
    out.clear(); char c;
    while (std::cin.get(c)) {
        if (c=='\n') return true;
        if (out.size() == contract::session_max_request_bytes)
            throw std::length_error("Session request exceeds 8 MiB");
        out += c;
    }
    if (std::cin.bad() || !out.empty()) throw std::invalid_argument("Incomplete session frame: newline required");
    return false;
}
/** Build a sequenced session progress event. */
Json event(const char* kind, int sequence, const Json& identity) {
    return Json::object({{"kind", kind}, {"version", "1"}, {"sequence", sequence}, {"identity", identity}});
}
/** Write one complete JSON line to the session output stream. */
void emit(std::ostream& output, const Json& value) {
    output << value.dump(contract::session_max_response_bytes-1) << '\n' << std::flush;
    if (!output) throw std::runtime_error("Session output closed");
}
}

detail::Json PreviewSessionCapability() {
    return Json::object({{"version", "1"}, {"command", "--preview-session"},
#ifdef __linux__
        {"supported", true},
#else
        {"supported", false},
#endif
        {"workerPlatform", "linux"}, {"transport", "ndjson"}, {"maxInFlight", 1},
        {"commands", Json::array({"--inspect-config", "--amr-resources", "--inspect-case", "--preview", "--preview-amr", "reset-resources"})},
        {"maxRequests", contract::session_max_requests},
        {"maxRequestBytes", std::int64_t(contract::session_max_request_bytes)},
        {"maxResponseBytes", std::int64_t(contract::session_max_response_bytes)},
        {"maxConfigBytes", std::int64_t(max_config_bytes)},
        {"processAddressSpaceMiB", contract::worker_address_space_mib},
        {"heavyRequestCpuSeconds", contract::case_cpu_seconds}, {"heavyRequestWallSeconds", contract::case_wall_seconds},
        {"meshRequestCpuSeconds", contract::worker_cpu_seconds}, {"meshRequestWallSeconds", 45},
        {"maxRetainedTables", 1}, {"resourceReuse", "content-and-ordered-species-validated"},
        {"fileComparisonCacheMiB", contract::session_file_cache_mib},
        {"sourceValidation", "full-content-per-request-before-use-and-before-success"},
        {"resultReuse", false}, {"initialization", "fresh-model-full-setup-per-request"},
        {"cancellation", "host-terminate-and-reap-session"}, {"progress", "stage-events"},
        {"binaryIdentity", "host-process-generation-required"}});
}

/** Serve repeated bounded inspection/preview requests with resource reuse. */
int RunPreviewSession() {
    // This stream keeps the transport buffer even while scientific code's
    // cout/cerr are redirected by CaptureLogs. Progress never contaminates logs.
    std::ostream output(std::cout.rdbuf());
    InspectionEosCache resources;
    EOSDispatcher::CacheScope cache_scope(resources);
    core::VerifiedFileCache files(std::size_t(contract::session_file_cache_mib)*1024*1024);
    core::VerifiedFileCacheScope file_scope(files);
    int sequence=0;
    try {
        SessionProcessLimits limits;
        auto ready=event("preview-session-ready", sequence, Json());
        ready["capability"]=PreviewSessionCapability(); emit(output, ready);
        std::string frame;
        while (sequence<contract::session_max_requests && line(frame)) {
            ++sequence;
            const auto start=Clock::now();
            Json identity;
            try {
                const auto input=detail::SessionInput(frame).parse();
                const auto flag=required(input,"command");
                const auto id=required(input,"requestId");
                if (id.empty() || id.size()>128) throw std::invalid_argument("requestId must be 1..128 UTF-8 bytes");
                identity=Json::object({{"requestId",id}});
                if (flag=="reset-resources") {
                    if (input.size()!=2) throw std::invalid_argument("reset-resources accepts only command/requestId");
                    resources.clear(); files.clear();
                    auto done=event("preview-session-reset", sequence, identity);
                    done["status"]="ok"; emit(output, done); continue;
                }
                const auto* definition=contract::find(flag);
                if (!definition || !definition->takes_configuration)
                    throw std::invalid_argument("Unsupported session command");
                auto request=request_from(input,definition->command);
                identity["caseId"]=request.case_id;
                identity["configRevision"]=core::string_sha256(request.config_text);
                const bool heavy=definition->command==Command::InspectCase || definition->command==Command::Fields;
                limits.begin_request(heavy ? contract::case_cpu_seconds : contract::worker_cpu_seconds);
                Json stages=Json::array();
                auto stage_start=Clock::now();
                std::string active_stage;
                const auto finish_stage=[&] {
                    if (!active_stage.empty()) stages.push(Json::object({{"stage",active_stage},{"milliseconds",milliseconds(stage_start)}}));
                };
                request.progress=[&](std::string_view stage) {
                    finish_stage(); active_stage=stage; stage_start=Clock::now();
                    auto progress=event("preview-session-progress",sequence,identity);
                    progress["command"]=flag; progress["stage"]=active_stage;
                    progress["elapsedMilliseconds"]=milliseconds(start); emit(output,progress);
                };
                request.progress("request");
                Json sample_evaluation;
                request.sample_evaluation=[&](std::size_t count,std::size_t conversions,std::size_t hits) {
                    sample_evaluation=Json::object({{"initCalls",std::int64_t(count)},
                        {"eosConversions",std::int64_t(conversions)}, {"exactStateReuses",std::int64_t(hits)},
                        {"scope","this-request-fixed-eos"}, {"maxRetainedStates",std::int64_t(InitialSampleCache::max_entries)}});
                };
                const auto loads=resources.loads, hits=resources.hits;
                const auto comparisons=files.hits, hashes=files.hashes;
                PreviewResponse response;
                switch (definition->command) {
                    case Command::Configuration: response=InspectConfiguration(request); break;
                    case Command::Resources: response=EstimateAmrResources(request); break;
                    case Command::InspectCase: response=InspectCase(request); break;
                    default: response=GeneratePreview(request); break;
                }
                finish_stage();
                if (response.exit_code!=0) { resources.clear(); files.clear(); }
                auto result=event("preview-session-result",sequence,identity);
                result["command"]=flag; result["exitCode"]=response.exit_code;
                result["elapsedMilliseconds"]=milliseconds(start); result["stages"]=stages;
                result["resources"]=Json::object({{"tableLoads",std::int64_t(resources.loads-loads)},
                    {"tableHits",std::int64_t(resources.hits-hits)}, {"retainedTables",resources.resident()?1:0},
                    {"fileContentMatches",std::int64_t(files.hits-comparisons)}, {"fileHashes",std::int64_t(files.hashes-hashes)},
                    {"retainedFileBytes",std::int64_t(files.retained_bytes())},
                    {"sampleEvaluation",sample_evaluation},
                    {"clearedAfterError",response.exit_code!=0}, {"resultReused",false}});
                // The nested response is the existing bounded Core serializer,
                // not text accepted from the request or a second JSON parser.
                auto wire=result.dump(); wire.pop_back();
                wire += ",\"response\":"+response.json+"}\n";
                if (wire.size()>contract::session_max_response_bytes)
                    throw std::length_error("Session response exceeds 9 MiB");
                output << wire << std::flush;
                if (!output) throw std::runtime_error("Session output closed");
                limits.begin_request(contract::case_cpu_seconds);
            } catch (const std::exception& error) {
                resources.clear(); files.clear();
                auto failed=event("preview-session-error",sequence,identity);
                failed["code"]="INVALID_SESSION_REQUEST"; failed["message"]=error.what();
                failed["fatal"]=false; emit(output,failed);
                limits.begin_request(contract::case_cpu_seconds);
            }
        }
        auto end=event("preview-session-closed",sequence,Json());
        end["reason"]=sequence==contract::session_max_requests?"request-limit":"stdin-eof";
        emit(output,end); return 0;
    } catch (const std::exception& error) {
        resources.clear(); files.clear();
        auto failed=event("preview-session-error",sequence,Json());
        failed["code"]="SESSION_CLOSED"; failed["message"]=error.what(); failed["fatal"]=true;
        try { emit(output,failed); } catch (...) {}
        return 2;
    }
}
}
