/**
 * @file CaseConfiguration.h
 * @brief Declare case inputs before model construction or Setup.
 *
 * Declarations belong to the registered model. They describe configuration
 * requirements, not sampled physical states, and must not allocate resources.
 */
#pragma once

#include "core/config/InputResolution.h"

namespace arch::config {
struct CaseParameter {
    std::string key, type, unit;
    std::string usage = "simulation";
    ConditionResult requirement{true, {}};
    // Exact owner-defined tokens; no aliases or case folding are inferred.
    std::vector<std::string> options;
};
struct CaseConfiguration {
    bool complete = false;
    InputContext consumers;
    std::vector<CaseParameter> parameters;
};

// The registration stores this static callback; querying it never constructs T.
// Old/custom cases without a declaration remain explicitly incomplete.
template<class T>
CaseConfiguration DescribeRegisteredCase(const StandardInputResolution& inputs) {
    if constexpr (requires { T::DescribeConfiguration(inputs); })
        return T::DescribeConfiguration(inputs);
    else
        return {};
}

struct CaseInputResolution {
    bool declarations_complete = false;
    std::map<std::string, InputRecord> parameters;
    std::vector<ConfigInputDiagnostic> diagnostics;
};

// Resolve declared raw case values using the same strict scalar parser.
// Type/range/derived provisions beyond this boundary remain owner checks.
inline CaseInputResolution ResolveCaseInput(const ConfigParser& parser,
                                            const CaseConfiguration& declaration) {
    CaseInputResolution result;
    result.declarations_complete = declaration.complete;
    for (const auto& definition : declaration.parameters) {
        const auto& key = definition.key;
        if (result.parameters.contains(key))
            throw std::logic_error("Duplicate case input declaration: " + key);
        InputRecord record;
        record.locations = parser.Locations(key);
        record.requirement = definition.requirement;
        if (record.locations.size() > 1) {
            record.state = InputState::Duplicate;
            result.diagnostics.push_back({key, "DUPLICATE_PARAMETER",
                "Duplicate case input; no occurrence is selected.", record.locations});
        } else if (parser.HasKey(key)) {
            record.state = InputState::Present;
            try {
                const auto raw = parser.GetString(key, "");
                if (definition.type == "float") record.parsed = ConfigParser::ParseNumber(key, raw);
                else if (definition.type == "int") record.parsed = ConfigParser::ParseInteger(key, raw);
                else if (definition.type == "bool") record.parsed = ConfigParser::ParseBoolean(key, raw);
                else if (definition.type == "string") record.parsed = raw;
                else throw std::logic_error("Unsupported case input type: " + definition.type);
                if (!definition.options.empty()
                    && std::find(definition.options.begin(), definition.options.end(), raw)
                        == definition.options.end())
                    throw ConfigValueError(key, "INVALID_OPTION", "Unknown case parameter option.");
                record.resolved = record.parsed;
                record.source = InputValueSource::Input;
            } catch (const ConfigValueError& error) {
                record.state = InputState::Invalid;
                record.parsed.reset();
                result.diagnostics.push_back({key, error.code, error.what(), record.locations});
            }
        } else if (record.requirement.value == true) {
            result.diagnostics.push_back({key, "MISSING_PARAMETER",
                "Required case input has no declared value.", {}});
        }
        result.parameters.emplace(key, std::move(record));
    }
    return result;
}
} // namespace arch::config
