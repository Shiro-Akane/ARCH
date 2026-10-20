/**
 * @file Discovery.cpp
 * @brief Enumerate registered cases and capabilities through the same inspection contract.
 *
 * Workflow:
 * 1. Accept a bounded, verified request at the read-only API boundary.
 * 2. Enumerate registered cases and capabilities through the same inspection contract.
 * 3. Return typed evidence or an explicit error; do not start the simulation Driver.
 */

#include "api/CaseInspection.h"
#include "api/Preview.h"

namespace arch::api {
using detail::Json;
/** List registry entries and honest preview capabilities without constructing cases. */
std::string RegisteredCases() {
    auto cases = Json::array();
    for (const auto& name : ProblemRegistry::Get().Names()) {
        const auto domain = PreviewDomain(name);
        const bool supported = domain.dimensions != 0;
        auto dimensions = Json::array();
        for (int dim = 1; dim <= 3; ++dim)
            if (domain.dimensions & (1u << dim)) dimensions.push(dim);
        cases.push(Json::object({{"caseId", name}, {"initialFieldPreview", supported},
#ifdef __linux__
            {"initialAmrPreview", supported},
#else
            {"initialAmrPreview", false},
#endif
            {"previewDimensions", dimensions},
            {"previewScope", "documented initializer domain; configuration-dependent Setup validation required"},
            {"automaticCustomUnitInference", false},
            {"inspection", CaseInspectionCapability(name, *ProblemRegistry::Get().Registration(name))}}));
    }
    return Json::object({{"schemaVersion", "1.0"}, {"version", "1"}, {"kind", "registered-cases"},
        {"status", "ok"}, {"cases", cases}, {"setup", "not_executed"},
        {"sourceFreshness", "host-build-manifest-required"}, {"cuda", "not_initialized"}}).dump();
}
}
