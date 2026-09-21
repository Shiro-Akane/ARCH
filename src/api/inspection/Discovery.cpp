#include "api/CaseInspection.h"
namespace arch::api {
using detail::Json;
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
