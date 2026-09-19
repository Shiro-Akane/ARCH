#include "ParameterMetadata.h"

#include <cmath>

namespace arch::api {
namespace {
using detail::Json;
Json value(const preview::ParameterValue &v) {
    return std::visit([](const auto &item) { return Json(item); }, v);
}
Json note(const char *code, const char *message) {
    return Json::object({{"severity", "warning"}, {"code", code}, {"message", message}});
}
Json constraints(const preview::AxisPosition &p) {
    return Json::object({{"min", p.min}, {"max", p.max},
        {"minInclusive", p.min_inclusive}, {"maxInclusive", p.max_inclusive}});
}
bool valid_bounds(const preview::AxisPosition &p) {
    return std::isfinite(p.min) && std::isfinite(p.max) && p.min < p.max;
}
} // namespace

void PublishParameterMetadata(Json &response, const preview::ParameterReadTrace &trace,
                              const std::vector<preview::AxisPosition> &positions,
                              bool preview_succeeded) {
    auto parameters = Json::array();
    auto bindings = Json::array();
    for (const auto &[key, read] : trace.reads()) {
        auto diagnostics = Json::array();
        if (read.ambiguous)
            diagnostics.push(note("AMBIGUOUS_PARAMETER_READ", "Different reads cannot be represented by one effective value."));
        else if (read.source == "unknown")
            diagnostics.push(note("PARAMETER_SOURCE_UNKNOWN", "The read value cannot be attributed to the parsed input."));
        else if (read.reason == "parse-failure")
            diagnostics.push(note("PARAMETER_DEFAULT_FALLBACK", "The existing reader used its default after the token could not be read as the requested type."));
        auto parameter = Json::object({{"key", key}, {"type", read.ambiguous ? Json() : Json(read.type)},
            {"explicitValue", !read.ambiguous && read.explicit_value ? value(*read.explicit_value) : Json()},
            {"effectiveValue", read.ambiguous ? Json() : value(read.effective_value)},
            {"defaultValue", read.ambiguous ? Json() : value(read.default_value)},
            {"valueSource", read.ambiguous ? "unknown" : read.source},
            {"sourceReason", read.ambiguous ? Json("ambiguous-reads") : read.reason.empty() ? Json() : Json(read.reason)},
            {"unit", Json()}, {"description", Json()}, {"diagnostics", diagnostics}});
        if (read.raw_value) parameter["rawValue"] = *read.raw_value;
        for (const auto &position : positions) {
            if (position.parameter_key != key || !valid_bounds(position)) continue;
            parameter["constraints"] = constraints(position);
            const auto *effective = std::get_if<double>(&read.effective_value);
            if (!preview_succeeded || read.ambiguous || read.source == "unknown" || !effective
                || !std::isfinite(position.coordinate) || position.coordinate != *effective
                || position.outside(position.coordinate)) continue;
            bindings.push(Json::object({{"id", position.id}, {"parameterKey", key},
                {"kind", "axis-position"}, {"axis", position.axis}, {"coordinate", position.coordinate},
                {"min", position.min}, {"max", position.max},
                {"minInclusive", position.min_inclusive}, {"maxInclusive", position.max_inclusive},
                {"clamping", "none"}, {"invalidBehavior", "retain-input-and-report"}, {"editable", true}}));
        }
        parameters.push(std::move(parameter));
    }
    response["parameterMetadata"] = Json::object({{"version", "1"},
        {"coverage", "observed-case-setup-reads"}, {"complete", false}, {"parameters", parameters}});
    response["graphicalBindings"] = Json::object({{"version", "1"}, {"items", bindings}});
}

detail::Json ParameterExtensionCapabilities() {
    return Json::object({
        {"parameterMetadata", Json::object({{"version", "1"}, {"complete", false},
            {"coverage", "observed-case-setup-reads"},
            {"cases", Json::array({Json::object({{"caseId", "Sod"}, {"keys", Json::array({"x_pos"})}})})}})},
        {"graphicalBindings", Json::object({{"version", "1"},
            {"cases", Json::array({Json::object({{"caseId", "Sod"},
                {"ids", Json::array({"Sod.x_pos"})}, {"kinds", Json::array({"axis-position"})}})})}})}
    });
}
} // namespace arch::api
