/**
 * @file checkpoint_analysis_inputs.h
 * @brief Read the explicit inputs needed by checkpoint post-processing.
 *
 * This is not a simulation loader. It shares Core token/range diagnostics,
 * then requires the values consumed by the chosen metric. It never creates
 * a model, applies case defaults, or certifies simulation readiness.
 */
#pragma once
#include "core/config/InputResolution.h"
#include <fstream>
#include <sstream>

namespace checkpoint_analysis {
class Inputs {
public:
    explicit Inputs(std::istream& stream, const std::string& source = "metric-input") {
        parser_.Read(stream, source);
        arch::config::InputContext context;
        context.purpose = arch::config::ConfigurationPurpose::InitialState;
        inputs_ = arch::config::ResolveStandardInput(parser_, context);
        std::vector<ConfigInputDiagnostic> errors;
        for (const auto& error : inputs_.diagnostics)
            if (error.code != "MISSING_PARAMETER") errors.push_back(error);
        if (!errors.empty()) throw ConfigInputError(std::move(errors));
    }
    static Inputs File(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) throw std::runtime_error("Cannot open metric parameters: " + path);
        return Inputs(file, path);
    }
    template<class T> T standard(const char* key) const {
        if (const auto* value = arch::config::input_detail::get<T>(inputs_, key)) return *value;
        throw ConfigValueError(key, "MISSING_PARAMETER",
            "The checkpoint metric requires an explicit resolved value.");
    }
    std::string standard_token(const char* key) const {
        auto token = standard<std::string>(key);
        std::transform(token.begin(), token.end(), token.begin(), arch::dispatch::ascii_lower);
        return token;
    }
    double case_number(const char* key) const {
        if (!parser_.HasKey(key)) throw ConfigValueError(key, "MISSING_PARAMETER",
            "The analytic checkpoint reference requires this case input.");
        return ConfigParser::ParseNumber(key, parser_.GetString(key, ""));
    }
    GridConfig grid() const {
        GridConfig result;
        result.geometry = standard_token("geometry");
        result.nblockx1 = standard<int>("nblockx1");
        result.nblockx2 = standard<int>("nblockx2");
        result.nblockx3 = standard<int>("nblockx3");
        result.dim = result.nblockx2 == 0 ? 1 : result.nblockx3 == 0 ? 2 : 3;
        result.x1_min = standard<double>("x1_min");
        result.x1_max = standard<double>("x1_max");
        if (result.dim >= 2) {
            result.x2_min = standard<double>("x2_min");
            result.x2_max = standard<double>("x2_max");
        }
        if (result.dim == 3) {
            result.x3_min = standard<double>("x3_min");
            result.x3_max = standard<double>("x3_max");
        }
        return result;
    }
private:
    ConfigParser parser_;
    arch::config::StandardInputResolution inputs_;
};
} // namespace checkpoint_analysis
