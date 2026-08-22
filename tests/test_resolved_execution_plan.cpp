#include "driver/dispatch/BackendCapabilities.h"
#include "driver/dispatch/DispatchImpl.h"
#include "driver/dispatch/PolicyDescriptor.h"
#include "driver/dispatch/ResolvedExecutionPlan.h"
#include "driver/dispatch/RuntimeProbe.h"
#include "numerics/burnsolver/BurnDispatch.h"
#include "numerics/diffusion/DiffDispatch.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace {

using namespace arch::dispatch;

void expect(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

template <class T>
const char* burn_route()
{
    if constexpr (std::is_same_v<T, DummyBurner>) return "none";
#define MATCH(NET, ODE, LABEL)                                                \
    if constexpr (std::is_same_v<                                            \
                      T, ODE<NET, DenseMatrixData<NET::ODE_NEQ>,             \
                             DenseLUSolver>>)                                \
        return LABEL
    MATCH(NetAprox13, Solver_BE_NR, "aprox13.be_nr.dense_lu");
    MATCH(NetAprox13, Solver_BD, "aprox13.bd.dense_lu");
    MATCH(NetAprox13, Solver_ROS4, "aprox13.ros4.dense_lu");
    MATCH(NetAprox19, Solver_BE_NR, "aprox19.be_nr.dense_lu");
    MATCH(NetAprox19, Solver_BD, "aprox19.bd.dense_lu");
    MATCH(NetAprox19, Solver_ROS4, "aprox19.ros4.dense_lu");
    MATCH(NetAprox21, Solver_BE_NR, "aprox21.be_nr.dense_lu");
    MATCH(NetAprox21, Solver_BD, "aprox21.bd.dense_lu");
    MATCH(NetAprox21, Solver_ROS4, "aprox21.ros4.dense_lu");
    MATCH(NetIso7, Solver_BE_NR, "iso7.be_nr.dense_lu");
    MATCH(NetIso7, Solver_BD, "iso7.bd.dense_lu");
    MATCH(NetIso7, Solver_ROS4, "iso7.ros4.dense_lu");
#undef MATCH
    return "unknown";
}

template <class T>
const char* diffusion_route()
{
    if constexpr (std::is_same_v<T, Numerics::Diffusion::NoDiffusionIntegrator>) return "none";
    if constexpr (std::is_same_v<T, RKL1TimeIntegrator>) return "rkl1";
    if constexpr (std::is_same_v<T, RKL2TimeIntegrator>) return "rkl2";
    return "unknown";
}

void test_plain_cpp_contracts()
{
    static_assert(std::is_standard_layout_v<ResolvedExecutionPlan>);
    static_assert(std::is_trivially_copyable_v<ResolvedExecutionPlan>);
    static_assert(std::is_standard_layout_v<ExecutionRequirements>);
    static_assert(std::is_trivially_copyable_v<ExecutionRequirements>);
    static_assert(std::is_standard_layout_v<DeviceCapability>);
    static_assert(std::is_trivially_copyable_v<DeviceCapability>);
    static_assert(list_size_v<FluxPolicies> == 5);
    static_assert(list_size_v<ReconstructionPolicies> == 3);
    static_assert(list_size_v<LimiterPolicies> == 4);
    static_assert(list_size_v<TimeIntegratorPolicies> == 3);
    static_assert(list_size_v<EosPolicies> == 4);
    static_assert(list_size_v<NetworkPolicies> == 5);
    static_assert(list_size_v<OdeSolverPolicies> == 4);
    static_assert(list_size_v<LinearSolverPolicies> == 3);
    static_assert(list_size_v<DiffusionIntegratorPolicies> == 3);

    constexpr auto linear = make_policy_descriptors<LinearSolverPolicies>();
    expect(linear.size() == 3, "linear descriptor count");
    expect(linear[2].id == LinearSolverId::SparseKlu, "SparseKLU descriptor ID");
    expect(!linear[2].cpu_supported && !linear[2].cuda_supported,
           "SparseKLU support must derive from absent bindings");
    constexpr auto flux = make_policy_descriptors<FluxPolicies>();
    for (const auto& descriptor : flux) {
        expect(descriptor.cpu_supported && descriptor.cuda_supported,
               "flux support must derive from committed bindings");
        expect(descriptor.requirements.minimum_cuda_cc_major == 8
               && descriptor.requirements.minimum_cuda_cc_minor == 6,
               "sm_86 static minimum");
    }
    const auto expect_supported = []<class List>() {
        constexpr auto descriptors = make_policy_descriptors<List>();
        for (const auto& descriptor : descriptors)
            expect(descriptor.cpu_supported && descriptor.cuda_supported,
                   "committed CPU/CUDA binding must project support");
    };
    expect_supported.template operator()<ReconstructionPolicies>();
    expect_supported.template operator()<LimiterPolicies>();
    expect_supported.template operator()<TimeIntegratorPolicies>();
    expect_supported.template operator()<EosPolicies>();
    expect_supported.template operator()<NetworkPolicies>();
    expect_supported.template operator()<OdeSolverPolicies>();
    expect_supported.template operator()<DiffusionIntegratorPolicies>();
}

void test_aliases_defaults_and_plan()
{
    for (const auto& [name, id] : {
             std::pair{"VL", FluxId::Vl}, {"vanleer", FluxId::Vl},
             {"SW", FluxId::Sw}, {"StegerWarming", FluxId::Sw},
             {"Roe", FluxId::Roe}, {"HLL", FluxId::Hll}, {"hllc", FluxId::Hllc}}) {
        const auto result = parse_registered_policy<FluxPolicies>(name);
        expect(result.ok && result.value == id && !result.defaulted, "flux alias");
    }
    auto fallback = parse_registered_policy<FluxPolicies>("unknown");
    expect(fallback.ok && fallback.defaulted && fallback.value == FluxId::Hllc,
           "unknown flux defaults HLLC");
    expect(parse_registered_policy<ReconstructionPolicies>("donor_cell").value
               == ReconstructionId::Pcm,
           "donor_cell alias");
    expect(parse_registered_policy<ReconstructionPolicies>("PLM").value
               == ReconstructionId::Muscl,
           "PLM alias");
    expect(parse_registered_policy<ReconstructionPolicies>("unknown").value
               == ReconstructionId::Pcm,
           "unknown reconstruction defaults PCM");
    expect(parse_registered_policy<LimiterPolicies>("MC").value == LimiterId::Mc,
           "MC alias");
    expect(parse_registered_policy<LimiterPolicies>("SuperBee").value
               == LimiterId::SuperBee,
           "SuperBee alias");
    expect(parse_registered_policy<LimiterPolicies>("VanLeer").value
               == LimiterId::VanLeer,
           "VanLeer alias");
    expect(parse_registered_policy<LimiterPolicies>("unknown").value
               == LimiterId::MinMod,
           "unknown limiter defaults MinMod");
    expect(parse_registered_policy<TimeIntegratorPolicies>("Euler").value
               == TimeIntegratorId::Euler,
           "Euler alias");
    expect(parse_registered_policy<TimeIntegratorPolicies>("RK1").value
               == TimeIntegratorId::Euler,
           "RK1 alias");
    expect(parse_registered_policy<TimeIntegratorPolicies>("SSPRK2").value
               == TimeIntegratorId::Rk2,
           "SSPRK2 alias");
    expect(parse_registered_policy<TimeIntegratorPolicies>("SSPRK3").value
               == TimeIntegratorId::Rk3,
           "SSPRK3 alias");
    expect(parse_registered_policy<TimeIntegratorPolicies>("unknown").value
               == TimeIntegratorId::Rk2,
           "unknown time integrator defaults RK2");
    SimConfig factory_config{};
    factory_config.numerics.solver_name = "Roe";
    expect(DispatchImpl::parse_flux_selection(factory_config).value == FluxId::Roe,
           "factory consumes registration alias");
    factory_config.numerics.solver_name = "unknown";
    const auto factory_default = DispatchImpl::parse_flux_selection(factory_config);
    expect(factory_default.defaulted && factory_default.value == FluxId::Hllc,
           "factory consumes registration default");
    expect(parse_registered_policy<EosPolicies>("Ideal").value == EosId::Ideal,
           "Ideal EOS alias");
    expect(parse_registered_policy<EosPolicies>("Helmholtz").value
               == EosId::Helmholtz,
           "Helmholtz EOS alias");
    expect(registration_matches<Tabular3DPolicy>("Tabular"),
           "Tabular rank-resolved alias belongs to its registration");
    expect(!registration_matches<Tabular3DPolicy>("tabular3d")
               && !registration_matches<Tabular4DPolicy>("tabular4d"),
           "diagnostic Tabular rank names are not accepted config aliases");
    expect(!parse_registered_policy<EosPolicies>("unknown").ok,
           "unknown EOS remains error");
    for (const auto& [name, id] : {
             std::pair{"aprox13", NetworkId::Aprox13},
             {"APROX19", NetworkId::Aprox19},
             {"aprox21", NetworkId::Aprox21},
             {"ISO7", NetworkId::Iso7}}) {
        const auto parsed = parse_registered_policy<NetworkPolicies>(name);
        expect(parsed.ok && parsed.value == id, "network alias");
    }
    expect(!parse_registered_policy<NetworkPolicies>("unknown").ok,
           "unknown network remains error");
    for (const auto& [name, id] : {
             std::pair{"BE_NR", OdeSolverId::BeNr}, {"be-nr", OdeSolverId::BeNr},
             {"BD", OdeSolverId::Bd}, {"ros4", OdeSolverId::Ros4}}) {
        const auto parsed = parse_registered_policy<OdeSolverPolicies>(name);
        expect(parsed.ok && parsed.value == id, "ODE alias");
    }
    expect(!parse_registered_policy<OdeSolverPolicies>("unknown").ok,
           "unknown ODE remains error");
    expect(parse_registered_policy<LinearSolverPolicies>("DenseLU").value
               == LinearSolverId::DenseLu,
           "DenseLU alias");
    expect(parse_registered_policy<LinearSolverPolicies>("sparse_klu").value
               == LinearSolverId::SparseKlu,
           "SparseKLU alias");
    expect(!parse_registered_policy<LinearSolverPolicies>("unknown").ok,
           "unknown linear solver remains error");
    expect(parse_registered_policy<DiffusionIntegratorPolicies>("RKL1").value
               == DiffusionIntegratorId::Rkl1,
           "RKL1 alias");
    expect(parse_registered_policy<DiffusionIntegratorPolicies>("rkl2").value
               == DiffusionIntegratorId::Rkl2,
           "RKL2 alias");
    expect(!parse_registered_policy<DiffusionIntegratorPolicies>("unknown").ok,
           "unknown diffusion remains error");

    SimConfig config{};
    config.numerics.solver_name = "Roe";
    config.numerics.reconstruction = "ppm";
    config.numerics.limiter = "VanLeer";
    config.numerics.time_integrator = "SSPRK3";
    config.physics.eos_type = "Tabular";
    config.physics.burn.use_burn = false;
    config.physics.diffusion.use_diffusion = false;
    int rank_calls = 0;
    auto resolved = resolve_execution_plan(config, [&] {
        ++rank_calls;
        return 3;
    });
    expect(resolved.ok && rank_calls == 1, "Tabular rank callback");
    expect(resolved.value.flux == FluxId::Roe
           && resolved.value.reconstruction == ReconstructionId::Ppm
           && resolved.value.limiter == LimiterId::VanLeer
           && resolved.value.time_integrator == TimeIntegratorId::Rk3
           && resolved.value.eos == EosId::Tabular3D,
           "hydro/EOS plan IDs");
    expect(resolved.value.network == NetworkId::None
           && resolved.value.ode_solver == OdeSolverId::None
           && resolved.value.linear_solver == LinearSolverId::None
           && resolved.value.diffusion_integrator == DiffusionIntegratorId::None,
           "disabled plan IDs");
    resolved = resolve_execution_plan(config, [] { return 4; });
    expect(resolved.ok && resolved.value.eos == EosId::Tabular4D,
           "Tabular4D rank");
    expect(!resolve_execution_plan(config, [] { return 2; }).ok,
           "invalid Tabular rank");
    for (const char* accepted : {"tabular", "Tabular", "tAbUlAr"}) {
        config.physics.eos_type = accepted;
        rank_calls = 0;
        resolved = resolve_execution_plan(config, [&] {
            ++rank_calls;
            return 3;
        });
        expect(resolved.ok && resolved.value.eos == EosId::Tabular3D
                   && rank_calls == 1,
               "every accepted Tabular spelling calls the rank authority");
    }
    for (const char* rejected : {"tabular3d", "Tabular3D", "tabular4d", "Tabular4D"}) {
        config.physics.eos_type = rejected;
        rank_calls = 0;
        resolved = resolve_execution_plan(config, [&] {
            ++rank_calls;
            return 3;
        });
        expect(!resolved.ok && rank_calls == 0,
               "latest-main unknown Tabular rank alias remains rejected");
    }

    config.physics.eos_type = "Helmholtz";
    config.physics.burn.use_burn = true;
    config.physics.burn.network_name = "aprox21";
    config.physics.burn.odeconfig.ode_solver = "ROS4";
    config.physics.burn.odeconfig.linear_solver = "DenseLU";
    config.physics.diffusion.use_diffusion = true;
    config.physics.diffusion.integrator = "rkl1";
    resolved = resolve_execution_plan(config, [] { return 0; });
    expect(resolved.ok && resolved.value.eos == EosId::Helmholtz
           && resolved.value.network == NetworkId::Aprox21
           && resolved.value.ode_solver == OdeSolverId::Ros4
           && resolved.value.linear_solver == LinearSolverId::DenseLu
           && resolved.value.diffusion_integrator == DiffusionIntegratorId::Rkl1,
           "burn/diffusion plan IDs");
    config.physics.burn.network_name = "bad";
    expect(!resolve_execution_plan(config, [] { return 0; }).ok,
           "unknown network remains error");
    config.physics.burn.network_name = "aprox13";
    config.physics.burn.odeconfig.ode_solver = "bad";
    expect(!resolve_execution_plan(config, [] { return 0; }).ok,
           "unknown ODE remains error");
    config.physics.burn.odeconfig.ode_solver = "BE_NR";
    config.physics.diffusion.integrator = "bad";
    expect(!resolve_execution_plan(config, [] { return 0; }).ok,
           "unknown diffusion remains error");
}

void test_requirements()
{
    SimConfig config{};
    config.grid.dim = 1;
    config.grid.nblockx1 = 1;
    config.grid.nblockx2 = 0;
    config.grid.nblockx3 = 0;
    config.grid.geometry = "cartesian";
    config.grid.x1l_boundary_type = "periodic";
    config.grid.x1r_boundary_type = "reflect";
    config.numerics.reconstruction = "ppm";
    config.physics.gravity.type = "none";
    auto requirements = resolve_execution_requirements(config, 0);
    expect(requirements.ok, "1D requirements");
    expect(requirements.value.dimension == 1
           && requirements.value.root_blocks_x1 == 1
           && requirements.value.root_blocks_x2 == 0
           && requirements.value.root_blocks_x3 == 0
           && requirements.value.required_ghost_depth == 3,
           "dimension-aware topology/ghost depth");
    expect((requirements.value.boundary_features & boundary_bit(BoundaryFeature::Periodic)) != 0
           && (requirements.value.boundary_features & boundary_bit(BoundaryFeature::Reflecting)) != 0,
           "boundary feature union");

    config.grid.dim = 3;
    config.grid.nblockx1 = 2;
    config.grid.nblockx2 = 1;
    config.grid.nblockx3 = 1;
    config.physics.burn.use_burn = true;
    config.physics.burn.use_nse = true;
    config.physics.diffusion.use_diffusion = true;
    config.physics.diffusion.use_thermal_diffusion = true;
    config.physics.diffusion.use_species_diffusion = true;
    config.physics.diffusion.use_viscous_diffusion = true;
    config.io.restart = true;
    config.amr.lrefinemax = 1;
    requirements = resolve_execution_requirements(config, 21);
    expect(requirements.ok && requirements.value.uniform_multiblock
           && requirements.value.amr && requirements.value.restart
           && requirements.value.burn && requirements.value.use_nse
           && requirements.value.diffusion
           && requirements.value.thermal_diffusion
           && requirements.value.species_diffusion
           && requirements.value.viscous_diffusion
           && requirements.value.species_count == 21,
           "complete dynamic requirements");
    expect(has_layout(requirements.value.state_layout,
                      StateLayoutRequirement::HydroConserved)
           && has_layout(requirements.value.state_layout,
                         StateLayoutRequirement::SpeciesMassFractions)
           && has_layout(requirements.value.state_layout,
                         StateLayoutRequirement::EnucDiagnostic),
           "state layout derivation");

    config.grid.geometry = "bad";
    expect(!resolve_execution_requirements(config, 21).ok,
           "unknown geometry structured error");
    config.grid.geometry = "cartesian";
    config.physics.gravity.type = "bad";
    expect(!resolve_execution_requirements(config, 21).ok,
           "unknown gravity structured error");
    config.physics.gravity.type = "none";
    config.grid.x2l_boundary_type = "bad";
    expect(!resolve_execution_requirements(config, 21).ok,
           "unknown boundary structured error");
}

void test_factory_routes_preserved()
{
    SimConfig config{};
    config.physics.burn.use_burn = false;
    BurnDispatcher::dispatch(config, [](auto burner) {
        expect(std::string(burn_route<decltype(burner)>()) == "none", "disabled burn route");
    });
    config.physics.burn.use_burn = true;
    for (const char* network : {"aprox13", "aprox19", "aprox21", "iso7"}) {
        for (const char* solver : {"BE_NR", "BD", "ROS4"}) {
            config.physics.burn.network_name = network;
            config.physics.burn.odeconfig.ode_solver = solver;
            config.physics.burn.odeconfig.linear_solver = "DenseLU";
            const std::string expected = std::string(network) + "."
                + (std::string(solver) == "BE_NR" ? "be_nr" :
                   std::string(solver) == "BD" ? "bd" : "ros4") + ".dense_lu";
            BurnDispatcher::dispatch(config, [&](auto burner) {
                expect(std::string(burn_route<decltype(burner)>()) == expected,
                       "burn factory concrete type");
            });
        }
    }
    bool threw = false;
    config.physics.burn.network_name = "bad";
    try { BurnDispatcher::dispatch(config, [](auto) {}); }
    catch (const std::runtime_error&) { threw = true; }
    expect(threw, "burn factory unknown network error");
    config.physics.burn.network_name = "aprox13";
    config.physics.burn.odeconfig.ode_solver = "bad";
    threw = false;
    try { BurnDispatcher::dispatch(config, [](auto) {}); }
    catch (const std::runtime_error&) { threw = true; }
    expect(threw, "burn factory unknown ODE error");
    config.physics.burn.odeconfig.ode_solver = "BE_NR";
    config.physics.burn.odeconfig.linear_solver = "SparseKLU";
    threw = false;
    try { BurnDispatcher::dispatch(config, [](auto) {}); }
    catch (const std::runtime_error& error) {
        threw = std::string(error.what()) == "SparseKLU not fully implemented.";
    }
    expect(threw, "burn factory SparseKLU error");

    config.physics.diffusion.use_diffusion = false;
    Numerics::Diffusion::dispatch_diffusion(config, [](auto selected) {
        expect(std::string(diffusion_route<decltype(selected)>()) == "none",
               "disabled diffusion type");
    });
    config.physics.diffusion.use_diffusion = true;
    for (const char* name : {"RKL1", "rkl1", "RKL2", "rkl2"}) {
        config.physics.diffusion.integrator = name;
        Numerics::Diffusion::dispatch_diffusion(config, [&](auto selected) {
            expect(std::string(diffusion_route<decltype(selected)>())
                       == (std::string(name).find('1') != std::string::npos ? "rkl1" : "rkl2"),
                   "diffusion factory concrete type");
        });
    }

    const auto expect_hydro_route = []<class ExpectedFlux, class ExpectedReconstruction>(
                                        ResolvedExecutionPlan plan) {
        bool route = false;
        const bool found = DispatchImpl::visit_hydro_cpu_route(
            plan, [&]<class Flux, class Reconstruction>() {
                route = std::is_same_v<Flux, ExpectedFlux>
                    && std::is_same_v<Reconstruction, ExpectedReconstruction>;
            });
        expect(found && route, "hydro factory concrete type");
    };
    ResolvedExecutionPlan plan{};
    plan.reconstruction = ReconstructionId::Pcm;
    plan.limiter = LimiterId::MinMod;
    plan.flux = FluxId::Vl;
    expect_hydro_route.template operator()<FluxVL<PCMReconstruction>, PCMReconstruction>(plan);
    plan.flux = FluxId::Sw;
    expect_hydro_route.template operator()<FluxSW<PCMReconstruction>, PCMReconstruction>(plan);
    plan.flux = FluxId::Roe;
    expect_hydro_route.template operator()<FluxRoe<PCMReconstruction>, PCMReconstruction>(plan);
    plan.flux = FluxId::Hll;
    expect_hydro_route.template operator()<FluxHLL<PCMReconstruction>, PCMReconstruction>(plan);
    plan.flux = FluxId::Hllc;
    expect_hydro_route.template operator()<FluxHLLC<PCMReconstruction>, PCMReconstruction>(plan);
    plan.flux = FluxId::Hll;
    plan.reconstruction = ReconstructionId::Ppm;
    expect_hydro_route.template operator()<FluxHLL<PPMReconstruction>, PPMReconstruction>(plan);
    plan.reconstruction = ReconstructionId::Muscl;
    plan.limiter = LimiterId::MinMod;
    expect_hydro_route.template operator()<FluxHLL<MusclReconstruction<MinMod>>,
                                           MusclReconstruction<MinMod>>(plan);
    plan.limiter = LimiterId::Mc;
    expect_hydro_route.template operator()<FluxHLL<MusclReconstruction<McLimiter>>,
                                           MusclReconstruction<McLimiter>>(plan);
    plan.limiter = LimiterId::SuperBee;
    expect_hydro_route.template operator()<FluxHLL<MusclReconstruction<SuperBee>>,
                                           MusclReconstruction<SuperBee>>(plan);
    plan.limiter = LimiterId::VanLeer;
    expect_hydro_route.template operator()<FluxHLL<MusclReconstruction<VanLeer>>,
                                           MusclReconstruction<VanLeer>>(plan);
}

} // namespace

int main()
{
    test_plain_cpp_contracts();
    test_aliases_defaults_and_plan();
    test_requirements();
    test_factory_routes_preserved();
    std::cout << "resolved_execution_plan: ok\n";
}
