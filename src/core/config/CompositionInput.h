/**
 * @file CompositionInput.h
 * @brief Resolve sparse raw composition before network normalization or Setup.
 *
 * Missing species are explicitly model-defined zero, not parsed input. A
 * positive finite supplied sum is required. This boundary does not normalize,
 * seed trace species, load EOS data or certify the resulting thermodynamics.
 */
#pragma once

#include "core/config/InputResolution.h"

namespace arch::config {
struct CompositionDeclaration {
    bool complete = false;
    std::string network;
    std::vector<std::string> keys;
};

// Implemented beside the network binding, using its actual species metadata.
CompositionDeclaration DescribeNetworkComposition(const StandardInputResolution& inputs);

struct CompositionInputResolution {
    bool declarations_complete = false;
    std::map<std::string, InputRecord> parameters;
    std::vector<ConfigInputDiagnostic> diagnostics;
};

inline std::string CompositionKey(std::string key) {
    std::transform(key.begin(), key.end(), key.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return key;
}

inline CompositionInputResolution ResolveCompositionInput(
    const ConfigParser& parser, const CompositionDeclaration& declaration) {
    CompositionInputResolution result;
    result.declarations_complete = declaration.complete;
    if (!declaration.complete) return result;
    double sum = 0.0;
    bool invalid = false;
    for (const auto& key : declaration.keys) {
        if (key != CompositionKey(key) || result.parameters.contains(key))
            throw std::logic_error("Invalid or duplicate composition declaration: " + key);
        InputRecord record;
        record.requirement = {false, {}}; // sparse members, required positive group
        std::string input_key;
        for (const auto& [candidate, locations] : parser.Occurrences()) {
            if (CompositionKey(candidate) != key) continue;
            input_key = candidate;
            record.locations.insert(record.locations.end(), locations.begin(), locations.end());
        }
        std::sort(record.locations.begin(), record.locations.end(),
                  [](const auto& a, const auto& b) { return a.line < b.line; });
        if (record.locations.size() > 1) {
            record.state = InputState::Duplicate;
            invalid = true;
            result.diagnostics.push_back({key, "DUPLICATE_PARAMETER",
                "More than one input names the same network species.", record.locations});
        } else if (!record.locations.empty()) {
            record.state = InputState::Present;
            try {
                const double value = ConfigParser::ParseNumber(input_key, parser.GetString(input_key, ""));
                record.parsed = value;
                if (value < 0.0)
                    throw ConfigValueError(input_key, "INVALID_RANGE", "Composition input must be nonnegative.");
                record.resolved = value;
                record.source = InputValueSource::Input;
                sum += value;
            } catch (const ConfigValueError& error) {
                record.state = InputState::Invalid;
                invalid = true;
                result.diagnostics.push_back({key, error.code, error.what(), record.locations});
            }
        } else {
            record.resolved = 0.0;
            record.source = InputValueSource::CaseDefined;
            record.source_evidence = InputSourceEvidence{
                "network:" + declaration.network + ":sparse-composition-input", {"network_name"}};
        }
        result.parameters.emplace(key, std::move(record));
    }
    if (!declaration.keys.empty() && !invalid && (!(sum > 0.0) || !std::isfinite(sum)))
        result.diagnostics.push_back({"composition", "INVALID_COMPOSITION",
            "Network composition requires a positive finite supplied sum; omitted species do not seed material.", {}});
    return result;
}
} // namespace arch::config
