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

namespace arch::api {
using detail::Json;
/** List registry entries and honest preview capabilities without constructing cases. */
std::string RegisteredCases() {
    auto cases = Json::array();
    for (const auto& name : ProblemRegistry::Get().Names()) {
        const bool supported = name == "Sod" || name == "CellularDet";
        cases.push(Json::object({{"caseId", name}, {"initialFieldPreview", supported},
#ifdef __linux__
            {"initialAmrPreview", supported},
#else
            {"initialAmrPreview", false},
#endif
            {"previewDimensions", name == "Sod" ? Json::array({1}) : name == "CellularDet" ? Json::array({2}) : Json::array()},
            {"automaticCustomUnitInference", false},
            {"inspection", CaseInspectionCapability(name, *ProblemRegistry::Get().Registration(name))}}));
    }
    return Json::object({{"schemaVersion", "1.0"}, {"version", "1"}, {"kind", "registered-cases"},
        {"status", "ok"}, {"cases", cases}, {"setup", "not_executed"},
        {"sourceFreshness", "host-build-manifest-required"}, {"cuda", "not_initialized"}}).dump();
}
}
