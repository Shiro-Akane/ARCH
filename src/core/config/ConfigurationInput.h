/**
 * @file ConfigurationInput.h
 * @brief Combine standard, registered case and auxiliary input before Setup.
 *
 * This analysis allocates only input records. It does not construct a model,
 * evaluate an EOS, allocate a mesh or establish simulation readiness.
 */
#pragma once

#include "core/problem/ProblemRegistry.h"

namespace arch::config {
struct ConfigurationInput {
    std::string case_id;
    ConfigurationPurpose purpose = ConfigurationPurpose::Evolution;
    // Filled only at successful loading; partial analysis must not demand a valid parser.
    std::map<std::string, std::string> raw_tokens;
    StandardInputResolution standard;
    CaseConfiguration declaration;
    CaseInputResolution model;
    std::map<std::string, InputRecord> auxiliary;
    InputOwnershipResolution ownership;
    std::vector<ConfigInputDiagnostic> diagnostics;

    bool requirements_known() const {
        if (!model.declarations_complete || !ownership.complete) return false;
        for (const auto& [key, record] : standard.parameters)
            if (!record.requirement.value.has_value()) return false;
        for (const auto& [key, record] : model.parameters)
            if (!record.requirement.value.has_value()) return false;
        return true;
    }
    void RequireDeclaredInputs() const {
        if (!diagnostics.empty()) throw ConfigInputError(diagnostics);
        if (!requirements_known())
            throw ConfigInputError({{"", "INCOMPLETE_CONFIGURATION",
                "Configuration requirements or input consumers remain unresolved.", {}}});
    }
};

inline ConfigurationInput AnalyzeConfigurationInput(
    const ConfigParser& parser, const std::string& case_id,
    ConfigurationPurpose purpose = ConfigurationPurpose::Evolution) {
    ConfigurationInput result;
    result.case_id = case_id;
    result.purpose = purpose;
    InputContext context;
    context.purpose = purpose;
    result.standard = ResolveStandardInput(parser, context);
    const auto& registry = ProblemRegistry::Get();
    const auto* registration = registry.Registration(case_id);
    if (registration) {
        result.declaration = registry.DescribeConfiguration(case_id, result.standard);
        context = result.declaration.consumers;
        context.purpose = purpose;
        result.standard = ResolveStandardInput(parser, context);
        result.model = ResolveCaseInput(parser, result.declaration);
        result.ownership = ResolveInputOwnership(parser, result.declaration, {"log_dir"});
    }
    result.diagnostics = result.standard.diagnostics;
    if (!registration)
        result.diagnostics.push_back({"case", "UNKNOWN_CASE",
            "No case is registered under the supplied case ID: " + case_id, {}});
    else if (!result.declaration.complete)
        result.diagnostics.push_back({"case", "INCOMPLETE_CASE_DECLARATION",
            "The registered case has not declared all configuration consumers.", {}});

    // The main logging owner derives its directory from out_dir when absent.
    InputRecord log;
    log.requirement = {false, {}};
    log.locations = parser.Locations("log_dir");
    if (log.locations.size() > 1) log.state = InputState::Duplicate;
    else if (parser.HasKey("log_dir")) {
        log.state = InputState::Present;
        log.parsed = parser.GetString("log_dir", "");
        log.resolved = log.parsed;
        log.source = InputValueSource::Input;
    } else if (const auto* directory = input_detail::get<std::string>(result.standard, "out_dir")) {
        log.resolved = *directory;
        log.source = InputValueSource::Derived;
        log.source_evidence = InputSourceEvidence{"main:log-output-directory", {"out_dir"}};
    }
    result.auxiliary.emplace("log_dir", std::move(log));

    // The parser and case layer can both diagnose one repeated key. Preserve
    // all source positions while emitting one diagnostic for the same key/code.
    const auto append = [&](const ConfigInputDiagnostic& diagnostic) {
        auto it = std::find_if(result.diagnostics.begin(), result.diagnostics.end(),
            [&](const auto& item) { return item.key == diagnostic.key && item.code == diagnostic.code
                    && (item.code == "DUPLICATE_PARAMETER" || item.message == diagnostic.message); });
        if (it == result.diagnostics.end()) result.diagnostics.push_back(diagnostic);
        else {
            for (const auto& key : diagnostic.related_keys)
                if (std::find(it->related_keys.begin(), it->related_keys.end(), key) == it->related_keys.end())
                    it->related_keys.push_back(key);
            for (const auto& location : diagnostic.locations) {
            const auto same = [&](const auto& item) {
                return item.source == location.source && item.line == location.line
                    && item.column == location.column && item.end_column == location.end_column;
            };
            if (std::none_of(it->locations.begin(), it->locations.end(), same))
                it->locations.push_back(location);
            }
        }
    };
    for (const auto& diagnostic : result.model.diagnostics) append(diagnostic);
    for (const auto& diagnostic : result.ownership.diagnostics) append(diagnostic);
    return result;
}
} // namespace arch::config
