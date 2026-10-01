#include "api/configuration/ParameterMetadata.h"
#include "core/config/RuntimeParams.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

static void require(bool good, const char *message) {
    if (!good) throw std::runtime_error(message);
}

int main() {
    auto trace = std::make_shared<arch::preview::ParameterReadTrace>(std::set<std::string>{"x_pos"});
    auto config = RuntimeParams::LoadText("nblockx2=0\nnblockx3=0\nx_pos=.35\n", trace);
    require(config.Get<double>("x_pos", .5) == .35, "observer must preserve Get result");
    require(config.Get<double>("uncovered", 7) == 7 && trace->reads().size() == 1, "bounded coverage");
    (void)config.Get<double>("x_pos", .5);
    require(!trace->reads().at("x_pos").ambiguous, "identical repeated reads are unambiguous");
    (void)config.Get<double>("x_pos", .7);
    require(trace->reads().at("x_pos").ambiguous, "different defaults must not be merged");
    auto output = arch::api::detail::Json::object();
    arch::api::PublishParameterMetadata(output, *trace, {{"Sod.x_pos", "x_pos", "x1", .35, 0, 1}}, true);
    const auto json = output.dump();
    require(json.find("\"effectiveValue\":null") != std::string::npos, "ambiguous effective value must be unknown");
    require(json.find("AMBIGUOUS_PARAMETER_READ") != std::string::npos, "ambiguity diagnostic");
    require(json.find("\"items\":[]") != std::string::npos, "ambiguous read cannot bind");

    auto changed = std::make_shared<arch::preview::ParameterReadTrace>(std::set<std::string>{"x_pos"});
    config = RuntimeParams::LoadText("nblockx2=0\nnblockx3=0\nx_pos=.35\n", changed);
    config.custom_params["x_pos"] = .4;
    require(config.Get<double>("x_pos", .5) == .4, "programmatic override remains effective");
    require(changed->reads().at("x_pos").source == "unknown", "override is not falsely attributed to input");
    arch::api::PublishParameterMetadata(output, *changed, {}, true);
    require(output.dump().find("\"constraints\"") == std::string::npos, "unknown constraints must be omitted");
    arch::api::PublishParameterMetadata(output, *changed, {{"Sod.x_pos", "x_pos", "x1", .4, 0, 1}}, true);
    require(output.dump().find("\"items\":[]") != std::string::npos, "unattributed value cannot bind");

    auto expressions = RuntimeParams::LoadText(
        "nblockx2=0\nnblockx3=0\nx1_max=exp(1)\ngravity_G=exp(-17)\n");
    require(std::abs(expressions.grid.x1_max - std::exp(1.0)) < 1e-14,
            "runtime grid expression evaluates exp");
    require(std::abs(expressions.physics.gravity.G_const - std::exp(-17.0)) < 1e-20,
            "runtime gravity expression evaluates exp");
    auto plain = RuntimeParams::LoadText("nblockx2=0\nnblockx3=0\nx_pos=.2\n");
    require(!plain.parameter_reads && plain.Get<double>("x_pos", .5) == .2, "ordinary config has no observer");
    require(RuntimeParams::LoadText("nblockx2=0\nnblockx3=0\nx_pos=1e2\n")
                .Get<double>("x_pos", .5) == 100.0,
            "custom numeric scientific notation");
    for (const std::string raw : {"2*pi", "exp(1)", "1.0suffix", "nan"}) {
        auto invalid = RuntimeParams::LoadText(
            "nblockx2=0\nnblockx3=0\nx_pos=" + raw + "\n");
        require(!invalid.custom_params.contains("x_pos"),
                "unsupported custom expression must not be truncated to a number");
        bool rejected = false;
        try { (void)invalid.Get<double>("x_pos", .5); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "numeric read of malformed custom parameter must fail");
    }
    // Ordinary execution and observed Preview must reject the same invalid input.
    for (const bool observed : {false, true}) {
        const auto make_config = [&](const std::string& token) {
            auto observer = observed ? std::make_shared<arch::preview::ParameterReadTrace>(
                std::set<std::string>{"value"}) : nullptr;
            return RuntimeParams::LoadText("nblockx2=0\nnblockx3=0\nvalue=" + token + "\n", observer);
        };
        for (const std::string token : {"1.5", "1.0", "1e0", "2147483648", "-2147483649", "1suffix", "nan"}) {
            auto invalid = make_config(token);
            bool rejected = false;
            try { (void)invalid.Get<int>("value", 99); }
            catch (const std::invalid_argument&) { rejected = true; }
            require(rejected, "integer strictness must not depend on observer");
        }
        for (const std::string token : {"0", "1", "2", "1.5", "yes", "nan"}) {
            auto invalid = make_config(token);
            bool rejected = false;
            try { (void)invalid.Get<bool>("value", false); }
            catch (const std::invalid_argument&) { rejected = true; }
            require(rejected, "boolean token strictness must not depend on observer");
        }
        for (const auto& [token, expected] : {
                std::pair<std::string, bool>{"true", true}, {"FALSE", false}}) {
            auto valid_bool = make_config(token);
            require(valid_bool.Get<bool>("value", !expected) == expected, "explicit bool lost");
            if (observed)
                require(valid_bool.parameter_reads->reads().at("value").source == "explicit",
                        "explicit bool lost provenance");
        }
        for (const auto& [token, expected] : {
                std::pair<std::string, int>{"+3", 3}, {"0", 0}, {"-2147483648", -2147483647 - 1}}) {
            auto valid_int = make_config(token);
            require(valid_int.Get<int>("value", 999) == expected, "valid integer changed");
        }
    }
    SimConfig programmatic;
    for (const auto number : {1.5, std::numeric_limits<double>::infinity(), 2147483648.0}) {
        programmatic.custom_params["integer"] = number;
        bool rejected = false;
        try { (void)programmatic.Get<int>("integer", 7); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "programmatic numeric cast silently truncated");
    }
    require(arch::preview::AxisPosition{"", "", "x1", .5, 0, 1}.outside(0), "strict lower bound");
    require(arch::preview::AxisPosition{"", "", "x1", .5, 0, 1}.outside(1), "strict upper bound");
    std::cout << "Parameter observations, ambiguity, provenance and open bounds passed\n";
}
