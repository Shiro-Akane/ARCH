#include "api/ParameterMetadata.h"
#include "core/RuntimeParams.h"

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

    auto plain = RuntimeParams::LoadText("nblockx2=0\nnblockx3=0\nx_pos=.2\n");
    require(!plain.parameter_reads && plain.Get<double>("x_pos", .5) == .2, "ordinary config has no observer");
    require(arch::preview::AxisPosition{"", "", "x1", .5, 0, 1}.outside(0), "strict lower bound");
    require(arch::preview::AxisPosition{"", "", "x1", .5, 0, 1}.outside(1), "strict upper bound");
    std::cout << "Parameter observations, ambiguity, provenance and open bounds passed\n";
}
