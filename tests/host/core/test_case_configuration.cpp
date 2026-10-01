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
        auto insensitive = choices;
        insensitive.parameters[0].options_ignore_case = true;
        std::istringstream upper("standing_wave=TRUE");
        parser.Read(upper);
        require(ResolveCaseInput(parser, insensitive).diagnostics.empty(),
                "owner-declared case-insensitive option rejected");
        for (const auto network : {"iso7", "aprox13", "aprox19", "aprox21"}) {
            std::istringstream selection(std::string("network_name=") + network);
            parser.Read(selection);
            const auto inputs = ResolveStandardInput(parser, {});
            const auto species = DescribeNetworkComposition(inputs);
            require(species.complete && !species.keys.empty(), "registered species metadata missing");
            require(std::find(species.keys.begin(), species.keys.end(), "xc12") != species.keys.end(),
                    "actual network lacks expected carbon input");
            std::istringstream carbon("XC12=0.75");
            parser.Read(carbon);
            auto fractions = ResolveCompositionInput(parser, species);
            require(fractions.diagnostics.empty(), "valid sparse composition rejected");
            require(std::get<double>(*fractions.parameters.at("xc12").resolved) == 0.75,
                    "composition inspection normalized raw input");
            const auto& omitted = fractions.parameters.at("xhe4");
            require(omitted.state == InputState::Missing && !omitted.parsed
                    && omitted.source == InputValueSource::CaseDefined
                    && std::get<double>(*omitted.resolved) == 0.0 && omitted.source_evidence,
                    "omitted species lost model-defined zero provenance");
            for (const auto bad : {"XC12=0.5\nxc12=0.5", "xc12=nan", "xc12=-0.1",
                                   "xc12=0", "", "xc12=1e308\nxhe4=1e308"}) {
                std::istringstream invalid_fractions(bad);
                parser.Read(invalid_fractions);
                fractions = ResolveCompositionInput(parser, species);
                require(!fractions.diagnostics.empty(), "invalid sparse composition accepted");
            }
            std::istringstream duplicates("xc12=0.5\nXC12=0.5");
            parser.Read(duplicates);
            fractions = ResolveCompositionInput(parser, species);
            require(fractions.parameters.at("xc12").state == InputState::Duplicate
                    && fractions.parameters.at("xc12").locations.size() == 2
                    && !fractions.parameters.at("xc12").resolved,
                    "case-insensitive duplicate selected a fraction");
        }
        std::istringstream none("network_name=none");
        parser.Read(none);
        auto species = DescribeNetworkComposition(ResolveStandardInput(parser, {}));
        require(species.complete && species.keys.empty(), "none acquired network species");
        std::istringstream missing_network;
        parser.Read(missing_network);
        species = DescribeNetworkComposition(ResolveStandardInput(parser, {}));
        require(!species.complete, "missing network treated as none");
        require(!ResolveCompositionInput(parser, species).declarations_complete,
                "unresolved network claimed composition coverage");
        auto composite = declaration;
        composite.composition = species;
        require(!ResolveCaseInput(parser, composite).declarations_complete,
                "case hid incomplete composition coverage");
        std::istringstream owned_network("network_name=iso7");
        parser.Read(owned_network);
        composite.composition = DescribeNetworkComposition(ResolveStandardInput(parser, {}));
        std::istringstream ownership(
            "network_name=iso7\nXC12=1\nxc122=0.2\ndensity=1\n"
            "log_dir=logs\ntimeintegrator=RK2\n");
        parser.Read(ownership);
        auto owners = ResolveInputOwnership(parser, composite, {"log_dir"});
        require(owners.complete && owners.diagnostics.size() == 1
                && owners.diagnostics.front().key == "xc122"
                && owners.diagnostics.front().code == "UNKNOWN_PARAMETER",
                "unknown isotope bypassed declared input ownership");
        require(owners.diagnostics.front().locations.front().line == 3,
                "unknown isotope lost source line");
        require(ResolveInputOwnership(parser, composite).diagnostics.size() == 2,
                "unregistered auxiliary key implicitly accepted");
        composite.complete = false;
        owners = ResolveInputOwnership(parser, composite);
        require(!owners.complete && owners.diagnostics.empty(),
                "incomplete declarations guessed unknown input");
        std::cout << "PASS: static case declarations and partial typed case resolution\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
