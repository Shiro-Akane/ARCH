#include "api/CaseInspection.h"
#include "api/configuration/ParameterMetadata.h"
#include "api/configuration/ValueDomain.h"
#include "core/config/RuntimeParams.h"
#include "core/files/InspectionSources.h"
#include <iostream>
#include <stdexcept>

static void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
struct ProbeProblem final : ProblemGenerator {
    bool product, fail;
    double a{}, b{};
    ProbeProblem(bool product, bool fail = false) : product(product), fail(fail) {}
    void Setup(SimConfig& c, SpeciesManager&) override {
        a = c.Get<double>("a", 0); b = c.Get<double>("b", 0);
        if (fail) throw std::runtime_error("setup error");
    }
    void SampleInitialPrimitive(const PointCoords&, PrimitiveData& p) const override {
        p.rho = product ? a*b : a;
    }
    void InitializeData(amr::AMRControl&, const SimConfig&, const SpeciesManager&, ProblemInitializationContext) override {}
};
struct Sink final : arch::preview::InitializationObserver {
    double rho = 0;
    void initial_primitive(const PointCoords&, const PrimitiveData& p) override { rho = p.rho; }
};
int main() {
    using namespace arch;
    const auto observe = [](bool product) {
        auto reads = std::make_shared<preview::ParameterReadTrace>(std::set<std::string>{}, true);
        auto config = RuntimeParams::LoadText("nblockx2=0\nnblockx3=0\na=2\nb=1\n", reads);
        config.parameter_reads.reset();
        SpeciesManager species; ProbeProblem model(product); Sink sink; PrimitiveData primitive;
        model.InspectSetup(config, species, reads);
        require(!config.parameter_reads, "Setup restores caller observer");
        model.InspectInitialPrimitive({}, primitive, sink);
        require(sink.rho == 2, "real Init sink captured");
        auto result = api::detail::Json::object();
        api::PublishParameterMetadata(result, *reads, {}, false);
        require(result.dump().find("\"status\":\"uncovered\"") != std::string::npos, "never infer a unit from equal scalar values");
        return result.dump();
    };
    // Identical reads and sinks cannot distinguish rho=a from rho=a*b when
    // b=1. In the latter expression a's unit depends on b, absent from IO.
    require(observe(false) == observe(true), "boundary is observationally identical without expression provenance");
    auto reads = std::make_shared<preview::ParameterReadTrace>(std::set<std::string>{}, true);
    auto config = RuntimeParams::LoadText("a=2\nb=1\n", reads);
    auto original = config.parameter_reads;
    SpeciesManager species; ProbeProblem failing(false, true);
    bool caught = false;
    try { failing.InspectSetup(config, species, std::make_shared<preview::ParameterReadTrace>(std::set<std::string>{}, true)); }
    catch (const std::runtime_error&) { caught = true; }
    require(caught && config.parameter_reads == original, "restore observer on Setup failure");
    for (double n : {1.25, 1e30, std::numeric_limits<double>::infinity()}) {
        config.custom_params["mode"] = n; caught = false;
        try { (void)config.Get<int>("mode", 0); } catch (const std::invalid_argument&) { caught = true; }
        require(caught, "refuse fractional/out-of-range/nonfinite integer before conversion");
    }
    reads->observe("x_pos", 0.5, 0.5, false);
    api::AddAuditedCaseUnits("Sod", {"Sod.cpp", "changed-source", true}, 1, *reads);
    require(!reads->units.contains("x_pos"), "stale source must not publish audited units");
    (void)config.Get<double>("a", 0.0);
    reads->record_unit("a", "cm", "first"); reads->record_unit("a", "s", "second");
    auto json = api::detail::Json::object(); api::PublishParameterMetadata(json, *reads, {}, false);
    require(json.dump().find("UNIT_EVIDENCE_CONFLICT") != std::string::npos, "conflicting units are explicit");
    api::ValueDomain domain;
    for (double x : {0., -0., -2., 3., 5., std::numeric_limits<double>::infinity()}) domain.observe(x);
    require(domain.positive == 2 && domain.zero == 2 && domain.negative == 1 && domain.nonfinite == 1, "zero is distinct from negative");
    require(domain.min_positive == 3 && domain.max_positive == 5, "positive log range");
    api::ValueDomain empty; empty.observe(0); empty.observe(-1);
    require(empty.json().dump().find("\"canLog\":false") != std::string::npos, "no positive values cannot use log");
    core::InspectionSources sources;
    int calls = 0; std::string generation = "first";
    const auto read = [&] { ++calls; return generation; };
    require(sources.fingerprint("EOS", read) == "first", "initial source content");
    for (int i = 0; i < 80; ++i) require(sources.fingerprint("EOS", read) == "first", "same source generation");
    require(calls == 1, "repeated queries do not reread whole source");
    sources.validate(); require(calls == 2, "final source content is checked again");
    generation = "other"; caught = false;
    try { sources.validate(); } catch (const std::runtime_error&) { caught = true; }
    require(caught, "changed source rejects the completed request");
    std::cout << "Initialization boundary, source guards, strict integers, evidence conflicts and log domains passed\n";
}
