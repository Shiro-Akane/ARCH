#include "api/CaseInspection.h"
#include "api/configuration/ParameterMetadata.h"
#include "api/configuration/ValueDomain.h"
#include "data/GlobalDefs.h"
#include "core/config/RuntimeParams.h"
#include <fstream>
#include <type_traits>
#include "core/files/InspectionSources.h"
#include <iostream>
#include <stdexcept>

static void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }

static std::string declared_input;
static SimConfig load_probe(const std::string& extra = "") {
    return RuntimeParams::LoadText(declared_input + extra, "probe",
        arch::config::ConfigurationPurpose::InitialState);
}
struct ProbeProblem final : ProblemGenerator {
    bool product, fail;
    double a{}, b{};
    ProbeProblem(bool product, bool fail = false) : product(product), fail(fail) {}
    void Setup(SimConfig& c, SpeciesManager& species) override {
        a = c.Get<double>("a", 0); b = c.Get<double>("b", 0);
        if (fail) throw std::runtime_error("setup error");
        species.add_species("probe", c.MaterialConstant(1, "probe.A"),
            c.MaterialConstant(1, "probe.Z"), c.MaterialInput("gamma"),
            c.MaterialConstant(1, "probe.Cv"));
    }
    void SampleInitialPrimitive(const PointCoords&, PrimitiveData& p) const override {
        p.rho = product ? a*b : a;
    }
    void InitializeData(amr::AMRControl&, const SimConfig&, const SpeciesManager&, ProblemInitializationContext) override {}
};
struct InvalidSetupProblem final : ProblemGenerator {
    bool entered = false;
    bool change_population;
    explicit InvalidSetupProblem(bool population = false) : change_population(population) {}
    void Setup(SimConfig& config, SpeciesManager& species) override {
        entered = true;
        if (change_population) {
            species.add_species("first", 1, 1, 1.4, 1);
            species.add_species("second", 1, 1, 1.4, 1);
        } else config.numerics.cfl = -0.5;
    }
    void InitializeData(amr::AMRControl&, const SimConfig&, const SpeciesManager&,
                        ProblemInitializationContext) override {}
};
struct Sink final : arch::preview::InitializationObserver {
    double rho = 0;
    void initial_primitive(const PointCoords&, const PrimitiveData& p) override { rho = p.rho; }
};
int main(int argc, char** argv) {
    using namespace arch;
    require(argc == 2, "named complete input fixture required");
    std::ifstream file(argv[1]);
    require(file.good(), "fixture not readable");
    declared_input.assign(std::istreambuf_iterator<char>(file), {});
    declared_input += "\na=2\nb=1\n";
    ProblemRegistry::Get().Register("probe", []() -> std::unique_ptr<ProblemGenerator> {
        throw std::runtime_error("input loader must not construct probe");
    }, {"probe", "test", true, [](const config::StandardInputResolution&) {
        config::CaseConfiguration declaration;
        declaration.complete = true;
        declaration.consumers.needs_network = false;
        declaration.consumers.needs_temperature_floor = false;
        declaration.consumers.needs_composition_floor = false;
        for (const auto* key : {"x_pos", "rho_left", "rho_right", "p_left",
                                "p_right", "u_left", "u_right", "a", "b"})
            declaration.parameters.push_back({key, "float", ""});
        return declaration;
    }});
    static_assert(!std::is_default_constructible_v<config::PreparedConfiguration>);
    static_assert(!std::is_constructible_v<config::PreparedConfiguration,
        const SimConfig&, const SpeciesManager&, const ProblemGenerator&>);

    const auto observe = [](bool product) {
        auto reads = std::make_shared<preview::ParameterReadTrace>(std::set<std::string>{}, true);
        auto config = load_probe();
        RuntimeParams::CaptureReads(config, reads);
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
    auto config = load_probe();
    RuntimeParams::CaptureReads(config, reads);
    config.parameter_reads = reads;
    auto original = config.parameter_reads;
    SpeciesManager species; ProbeProblem failing(false, true);
    bool caught = false;
    try { failing.InspectSetup(config, species, std::make_shared<preview::ParameterReadTrace>(std::set<std::string>{}, true)); }
    catch (const std::runtime_error&) { caught = true; }
    require(caught && config.parameter_reads == original, "restore observer on Setup failure");
    for (const bool observed : {false, true}) {
        auto prepared = load_probe();
        prepared.parameter_reads = original;
        SpeciesManager prepared_species;
        InvalidSetupProblem mutation;
        bool rejected = false;
        try {
            if (observed) mutation.InspectSetup(prepared, prepared_species, reads);
            else mutation.SetupChecked(prepared, prepared_species);
        } catch (const ConfigValueError& error) { rejected = error.key == "cfl"; }
        require(rejected && mutation.entered && prepared.parameter_reads == original,
                "post-Setup invalid controls escaped or observer was not restored");
        prepared.numerics.cfl = -1;
        InvalidSetupProblem before;
        try { before.SetupChecked(prepared, prepared_species); } catch (const ConfigValueError&) {}
        require(!before.entered, "invalid pre-Setup controls reached the model");
    }
    {
        auto prepared = load_probe("smallx=0.6\n");
        SpeciesManager prepared_species;
        InvalidSetupProblem population(true);
        bool rejected = false;
        try { population.SetupChecked(prepared, prepared_species); }
        catch (const ConfigValueError& error) { rejected = error.key == "smallx"; }
        require(rejected && population.entered && prepared_species.count() == 2,
                "post-Setup controls ignored the actual species count");
    }
    {
        SimConfig missing;
        SpeciesManager specs;
        InvalidSetupProblem model;
        bool rejected = false;
        try { model.SetupChecked(missing, specs); }
        catch (const ConfigValueError& e) { rejected = e.code == "INCOMPLETE_CONFIGURATION"; }
        require(rejected && !model.entered, "default storage entered Setup");
    }
    {
        auto input = load_probe();
        SpeciesManager specs;
        ProbeProblem model(false), other(false);
        const auto frozen = model.SetupChecked(input, specs);
        static_assert(std::is_same_v<decltype(frozen.config()), const SimConfig&>);
        static_assert(std::is_same_v<decltype(frozen.species()), const SpeciesManager&>);
        input.numerics.cfl = 0.25;
        specs.species_list.clear();
        require(frozen.config().numerics.cfl == 0.4
                && frozen.config().Get<double>("a", -1) == 2
                && frozen.species().count() == 1,
                "later preparation edit altered read-only snapshot");
        require(frozen.belongs_to(model) && !frozen.belongs_to(other),
                "prepared state lost model instance identity");
        InvalidSetupProblem blocked;
        bool rejected = false;
        try { blocked.SetupChecked(input, specs); }
        catch (const ConfigValueError& e) { rejected = e.code == "UNDECLARED_CONFIGURATION_CHANGE"; }
        require(rejected && !blocked.entered, "mutated loaded input entered Setup");
    }
    for (int kind = 0; kind < 6; ++kind) {
        struct Mutator final : ProblemGenerator {
            int kind;
            explicit Mutator(int value) : kind(value) {}
            void Setup(SimConfig& input, SpeciesManager&) override {
                if (kind == 0) input.numerics.cfl = 0.25;
                if (kind == 1) input.io.out_dir = "other";
                if (kind == 2) input.numerics.dt_max = 10;
                if (kind == 3) input.amr.refine_on_p = true;
                if (kind == 4) input.physics.gravity.G_const = 1;
                if (kind == 5) input = load_probe();
            }
            void InitializeData(amr::AMRControl&, const SimConfig&, const SpeciesManager&,
                                ProblemInitializationContext) override {}
        } mutation(kind);
        auto input = load_probe();
        const auto observer = input.parameter_reads;
        SpeciesManager specs;
        bool rejected = false;
        try { mutation.InspectSetup(input, specs, reads); }
        catch (const ConfigValueError& e) { rejected = e.code == "UNDECLARED_CONFIGURATION_CHANGE"; }
        require(rejected && input.parameter_reads == observer,
                "valid undeclared Setup mutation escaped or broke observer restoration");
    }
    for (double n : {1.25, 1e30, std::numeric_limits<double>::infinity()}) {
        caught = false;
        try { ConfigParser::ValidateNumeric<int>("mode", n); }
        catch (const std::invalid_argument&) { caught = true; }
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
