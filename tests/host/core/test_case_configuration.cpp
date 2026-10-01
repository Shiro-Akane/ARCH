/**
 * @file test_case_configuration.cpp
 * @brief Query registered requirements without constructing a model or running Setup.
 */
#include <iostream>
#include <sstream>
#include <stdexcept>
#include "core/problem/ProblemRegistry.h"

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
struct DeclaredCase {
    DeclaredCase() { throw std::runtime_error("model construction is forbidden in declaration query"); }
    static arch::config::CaseConfiguration DescribeConfiguration(
        const arch::config::StandardInputResolution&) {
        arch::config::CaseConfiguration result;
        result.complete = true;
        result.parameters = {{"density", "float", "g/cm^3"}, {"mode", "int", "1"},
                             {"enabled", "bool", ""}};
        return result;
    }
};
struct UndeclaredCase {};
}
int main() {
    try {
        using namespace arch::config;
        auto& registry = ProblemRegistry::Get();
        const auto forbidden_factory = []() -> std::unique_ptr<ProblemGenerator> {
            throw std::runtime_error("registration query called the model factory");
        };
        registry.Register("declared", forbidden_factory,
            {"test", "test-source", true, &DescribeRegisteredCase<DeclaredCase>});
        registry.Register("undeclared", forbidden_factory,
            {"test", "test-source", true, &DescribeRegisteredCase<UndeclaredCase>});
        const auto declaration = registry.DescribeConfiguration("declared", {});
        require(declaration.complete && declaration.parameters.size() == 3, "declaration lost");
        require(!registry.DescribeConfiguration("undeclared", {}).complete, "undeclared model claimed coverage");
        bool unknown = false;
        try { (void)registry.DescribeConfiguration("unregistered", {}); }
        catch (const std::invalid_argument&) { unknown = true; }
        require(unknown, "unknown case accepted");

        ConfigParser parser;
        std::istringstream empty;
        parser.Read(empty);
        auto result = ResolveCaseInput(parser, declaration);
        require(result.diagnostics.size() == 3, "must aggregate every missing case parameter");
        for (const auto& [key, value] : result.parameters)
            require(value.state == InputState::Missing && !value.parsed && value.locations.empty(),
                    "missing case parameter acquired input/default/source");
        std::istringstream valid("density=1.25\nmode=0\nenabled=false");
        parser.Read(valid);
        result = ResolveCaseInput(parser, declaration);
        require(result.diagnostics.empty(), "valid declared input rejected");
        require(std::get<int>(*result.parameters.at("mode").parsed) == 0, "zero mode lost");
        require(!std::get<bool>(*result.parameters.at("enabled").parsed), "false switch lost");
        std::istringstream invalid("density=nan\nmode=1.5\nenabled=1");
        parser.Read(invalid);
        result = ResolveCaseInput(parser, declaration);
        require(result.diagnostics.size() == 3, "bad case tokens not aggregated");
        for (const auto& [key, value] : result.parameters)
            require(!value.resolved && !value.source, "invalid case parameter acquired effective value");

        std::istringstream duplicate("density=1\ndensity=2\nmode=1\nenabled=true");
        parser.Read(duplicate);
        result = ResolveCaseInput(parser, declaration);
        require(result.parameters.at("density").state == InputState::Duplicate,
                "duplicate case parameter selected a value");
        require(result.parameters.at("density").locations.size() == 2, "duplicate locations lost");
        auto conditional = declaration;
        conditional.parameters[0].requirement = {std::nullopt, {"eos_type"}};
        std::istringstream partial("mode=1\nenabled=true");
        parser.Read(partial);
        result = ResolveCaseInput(parser, conditional);
        require(result.diagnostics.empty() && !result.parameters.at("density").requirement.value,
                "unknown condition guessed missing physical input");
        CaseConfiguration choices;
        choices.complete = true;
        choices.parameters = {
            {"standing_wave", "string", "1", "simulation", {true, {}}, {"true", "false"}}};
        for (const auto token : {"true", "false", "TRUE", "False", "0", "1", "other"}) {
            std::istringstream input(std::string("standing_wave=") + token);
            parser.Read(input);
            const auto checked = ResolveCaseInput(parser, choices);
            const bool valid = std::string(token) == "true" || std::string(token) == "false";
            const auto& value = checked.parameters.at("standing_wave");
            require(checked.diagnostics.empty() == valid, "case options differ from owner tokens");
            if (valid) {
                require(value.source == InputValueSource::Input
                        && std::get<std::string>(*value.resolved) == token, "explicit option lost");
            } else {
                require(checked.diagnostics.front().code == "INVALID_OPTION"
                        && value.state == InputState::Invalid && !value.parsed && !value.resolved,
                        "unknown case option acquired a value");
                require(value.locations.size() == 1, "invalid option lost source location");
            }
        }
        std::istringstream no_choice;
        parser.Read(no_choice);
        result = ResolveCaseInput(parser, choices);
        require(result.diagnostics.size() == 1
                && result.diagnostics.front().code == "MISSING_PARAMETER",
                "case option list must not supply a default");
        std::cout << "PASS: static case declarations and partial typed case resolution\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
