/**
 * @file test_resolved_execution_plan.cpp
 * @brief Verify configuration-to-policy resolution before backend creation.
 *
 * Aliases, defaults, capability requirements and factory routing must agree
 * with the selected physical configuration and linear-solver backend.
 */
#include "driver/dispatch/BackendCapabilities.h"
#include "driver/dispatch/DispatchImpl.h"
#include "driver/dispatch/PolicyDescriptor.h"
#include "driver/dispatch/ResolvedExecutionPlan.h"
#include "driver/dispatch/RuntimeProbe.h"
#include "numerics/burnsolver/BurnDispatch.h"
#include "numerics/diffusion/DiffDispatch.h"
#include "physics/gravity/GravityDispatch.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace {

using namespace arch::dispatch;

struct CpuHostBurnProbe {};

struct HostHandleProbeEos
{
    double get_eta(double, double, const double*) const
    {
        throw CpuHostBurnProbe{};
    }

    double get_cv(double, double, const double*) const noexcept { return 1.0; }
    double get_eint_from_T(double, double temperature,
                           const double*) const noexcept
    {
        return temperature;
    }
};

// Check the launch signature without instantiating a numerical driver body.
struct HydroLaunchTypeProbe {};

template <class... Context>
concept HasHydroLaunchContext = requires(
    amr::AMRControl& amr, const HostHandleProbeEos& eos,
    const Physical::Gravity::IGravityPolicy* gravity,
    const BurnerHandle<HostHandleProbeEos>& burn, const SimConfig& config,
    const SpeciesManager& species, const RunState& state, Context&&... context) {
    DispatchImpl::launch_run<HydroLaunchTypeProbe, HydroLaunchTypeProbe>(
        amr, eos, gravity, burn, config, species, state,
        std::forward<Context>(context)...);
};

template <class Config>
concept HasConfigOnlyBurnHandle = requires(const Config& config) {
    BurnDispatcher::make_handle<HostHandleProbeEos>(config);
};

template <class Config>
concept HasConfigOnlyBurnDispatch = requires(const Config& config) {
    BurnDispatcher::dispatch(config, [](auto&&) {});
};

template <class Config>
concept HasConfigOnlyDiffusionDispatch = requires(const Config& config) {
    Numerics::Diffusion::dispatch_diffusion(config, [](auto&&) {});
};

template <class Config>
concept HasConfigOnlyGravityFactory = requires(const Config& config) {
    Physical::Gravity::make_gravity(config);
};

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
    static_assert(HasHydroLaunchContext<
        const ResolvedExecutionPlan&, const ExecutionRequirements&,
        const BackendResolution&, StartupOrder&, const io::CheckpointProvenance&>);
    static_assert(!HasHydroLaunchContext<>);
    static_assert(!HasHydroLaunchContext<
        const ResolvedExecutionPlan&, const ExecutionRequirements&,
        const BackendResolution&, StartupOrder&>);
    static_assert(!HasHydroLaunchContext<
        const ResolvedExecutionPlan*, const ExecutionRequirements*,
        const BackendResolution*, StartupOrder*, const io::CheckpointProvenance*>);
    static_assert(!HasConfigOnlyBurnHandle<SimConfig>);
    static_assert(!HasConfigOnlyBurnDispatch<SimConfig>);
    static_assert(!HasConfigOnlyDiffusionDispatch<SimConfig>);
    static_assert(!HasConfigOnlyGravityFactory<SimConfig>);
    static_assert(std::is_standard_layout_v<ResolvedExecutionPlan>);
    static_assert(std::is_trivially_copyable_v<ResolvedExecutionPlan>);
    static_assert(std::is_standard_layout_v<ExecutionPlanRequest>);
    static_assert(std::is_trivially_copyable_v<ExecutionPlanRequest>);
    static_assert(std::is_standard_layout_v<ExecutionRequirements>);
    static_assert(std::is_trivially_copyable_v<ExecutionRequirements>);
    static_assert(std::is_standard_layout_v<DeviceCapability>);
    static_assert(std::is_trivially_copyable_v<DeviceCapability>);
    static_assert(list_size_v<FluxPolicies> == 5);
    static_assert(list_size_v<ReconstructionPolicies> == 3);
    static_assert(list_size_v<LimiterPolicies> == 4);
    static_assert(list_size_v<TimeIntegratorPolicies> == 3);
    static_assert(list_size_v<EosPolicies> == 4);
    static_assert(list_size_v<NetworkPolicies>
                  == 5 + ARCH_CUSTOM_NETWORK_COUNT);
    static_assert(list_size_v<OdeSolverPolicies> == 4);
    static_assert(list_size_v<LinearSolverPolicies> == 4);
    static_assert(list_size_v<DiffusionIntegratorPolicies> == 3);

    constexpr auto linear = make_policy_descriptors<LinearSolverPolicies>();
    expect(linear.size() == 4, "linear descriptor count");
    expect(linear[2].id == LinearSolverId::SparseKlu, "SparseKLU descriptor ID");
#if ARCH_HAS_KLU
    expect(linear[2].cpu_supported && !linear[2].cuda_supported,
           "SparseKLU support must follow the CPU KLU build binding");
#else
    expect(!linear[2].cpu_supported && !linear[2].cuda_supported,
           "SparseKLU support must derive from absent bindings");
#endif
    expect(linear[3].id == LinearSolverId::CuDss,
           "cuDSS descriptor ID");
#if ARCH_HAS_CUDSS_PROVIDER
    expect(!linear[3].cpu_supported && linear[3].cuda_supported,
           "cuDSS support must follow the committed CUDA provider binding");
#else
    expect(!linear[3].cpu_supported && !linear[3].cuda_supported,
           "cuDSS stays unavailable until a CUDA provider is committed");
#endif
    constexpr auto flux = make_policy_descriptors<FluxPolicies>();
    for (const auto& descriptor : flux) {
        expect(descriptor.cpu_supported && descriptor.cuda_supported,
               "flux support must derive from committed bindings");
        expect(descriptor.requirements.minimum_cuda_cc_major == 0
               && descriptor.requirements.minimum_cuda_cc_minor == 0,
               "ordinary flux has no model-derived sm_86 feature floor");
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
    constexpr auto networks = make_policy_descriptors<NetworkPolicies>();
    for (const auto& descriptor : networks) {
        expect(descriptor.cpu_supported,
               "every registered network has a CPU binding");
        const bool built_in_nse_network = descriptor.id == NetworkId::Aprox13
            || descriptor.id == NetworkId::Aprox19
            || descriptor.id == NetworkId::Aprox21
            || descriptor.id == NetworkId::Iso7;
        expect(descriptor.supports_nse == built_in_nse_network,
               "only built-in Timmes networks advertise NSE support");
        expect(network_supports_nse(descriptor.id)
                   == descriptor.supports_nse,
               "NSE capability lookup must derive from network metadata");
    }
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
    factory_config.numerics.reconstruction = "pcm";
    factory_config.numerics.limiter = "minmod";
    factory_config.numerics.time_integrator = "rk2";
    const auto no_table = []() -> int {
        throw std::logic_error("ideal-gas resolution must not inspect a table");
    };
    factory_config.numerics.solver_name = "Roe";
    const auto factory_alias = resolve_execution_plan(factory_config, no_table);
    expect(factory_alias.ok && !factory_alias.defaulted
               && factory_alias.value.flux == FluxId::Roe,
           "resolved factory plan consumes registration alias");
    factory_config.numerics.solver_name = "unknown";
    const auto factory_default = resolve_execution_plan(factory_config, no_table);
    expect(factory_default.ok && factory_default.defaulted
               && factory_default.value.flux == FluxId::Hllc,
           "resolved factory plan consumes registration default");
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
    expect(parse_registered_policy<LinearSolverPolicies>("CuDSS").value
               == LinearSolverId::CuDss,
           "cuDSS alias");
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
           && resolved.value.linear_solver == LinearSolverRequest::None
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
           && resolved.value.linear_solver == LinearSolverRequest::DenseLu
           && resolved.value.diffusion_integrator == DiffusionIntegratorId::Rkl1,
           "burn/diffusion plan IDs");
    config.physics.burn.odeconfig.linear_solver = "AuTo";
    resolved = resolve_execution_plan(config, [] { return 0; }, 21);
    expect(resolved.ok
               && resolved.value.linear_solver == LinearSolverRequest::Auto,
           "parser preserves backend-dependent Auto");
    auto cpu_plan = materialize_execution_plan(
        resolved.value, ComputeBackend::Cpu, 21);
    auto cuda_plan = materialize_execution_plan(
        resolved.value, ComputeBackend::Cuda, 21);
    expect(cpu_plan.ok && cuda_plan.ok
               && cpu_plan.value.linear_solver == LinearSolverId::DenseLu
               && cuda_plan.value.linear_solver == LinearSolverId::DenseLu,
           "Auto selects DenseLU through the dense-network limit");
    cpu_plan = materialize_execution_plan(
        resolved.value, ComputeBackend::Cpu, BurnLimits::MAX_SPECIES + 1);
    cuda_plan = materialize_execution_plan(
        resolved.value, ComputeBackend::Cuda, BurnLimits::MAX_SPECIES + 1);
    expect(cpu_plan.ok && cuda_plan.ok
               && cpu_plan.value.linear_solver == LinearSolverId::SparseKlu
               && cuda_plan.value.linear_solver == LinearSolverId::CuDss,
           "large-network Auto is materialized per backend");
    expect(!materialize_execution_plan(
                resolved.value, ComputeBackend::Auto, 21).ok,
           "unresolved compute Auto cannot enter a concrete plan");
    config.physics.burn.odeconfig.linear_solver = "DenseLU";
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
    config.execution.compute_backend = "cpu";

    struct FactorySelection {
        ResolvedExecutionPlan plan;
        ExecutionRequirements requirements;
    };
    // Exercise the production parsing, materialization and capability gates
    // before invoking typed factories. An explicit CPU request does not probe
    // CUDA; the test supplies that same NotRequested probe result.
    const auto resolve_cpu_factory = [&](std::size_t species_count) {
        const auto requested = parse_compute_backend(config.execution.compute_backend);
        expect(requested.ok && requested.value == ComputeBackend::Cpu,
               "factory fixture requests CPU execution");
        const auto parsed = resolve_execution_plan(config, []() -> int {
            throw std::logic_error("ideal-gas factory fixture requested a table rank");
        }, species_count);
        if (!parsed.ok) throw std::runtime_error(std::string(parsed.error));
        const auto cpu = materialize_execution_plan(
            parsed.value, ComputeBackend::Cpu, species_count);
        const auto cuda = materialize_execution_plan(
            parsed.value, ComputeBackend::Cuda, species_count);
        expect(cpu.ok && cuda.ok, "factory backend plans materialize");
        const auto requirements = resolve_execution_requirements(config, species_count);
        if (!requirements.ok)
            throw std::runtime_error(std::string(requirements.error));
        const RuntimeProbeResult probe{};
        const auto support = query_support(cpu.value, cuda.value, requirements.value, probe);
        const auto backend = resolve_backend(
            requested.value, support, probe, StartupPhase::BeforeConstruction);
        expect(backend.resolved_backend == ComputeBackend::Cpu,
               "factory CPU capability gate accepts the plan");
        return FactorySelection{cpu.value, requirements.value};
    };

    config.physics.burn.use_burn = false;
    auto selected = resolve_cpu_factory(0);
    BurnDispatcher::dispatch(
        config, selected.plan.network, selected.plan.ode_solver,
        selected.plan.linear_solver, [](auto burner) {
            expect(std::string(burn_route<decltype(burner)>()) == "none",
                   "disabled burn route");
        });
    config.physics.burn.use_burn = true;
    config.physics.burn.use_nse = true;
    for (const auto& [network, species_count] : {
             std::pair{"aprox13", NetAprox13::NUM_SPECIES},
             {"aprox19", NetAprox19::NUM_SPECIES},
             {"aprox21", NetAprox21::NUM_SPECIES},
             {"iso7", NetIso7::NUM_SPECIES}}) {
        for (const char* solver : {"BE_NR", "BD", "ROS4"}) {
            config.physics.burn.network_name = network;
            config.physics.burn.odeconfig.ode_solver = solver;
            config.physics.burn.odeconfig.linear_solver = "DenseLU";
            const std::string expected = std::string(network) + "."
                + (std::string(solver) == "BE_NR" ? "be_nr" :
                   std::string(solver) == "BD" ? "bd" : "ros4") + ".dense_lu";
            selected = resolve_cpu_factory(species_count);
            BurnDispatcher::dispatch(
                config, selected.plan.network, selected.plan.ode_solver,
                selected.plan.linear_solver, [&](auto burner) {
                    expect(std::string(burn_route<decltype(burner)>()) == expected,
                           "burn factory concrete type");
                });
        }
    }
    config.physics.burn.network_name = "aPrOx13";
    for (const auto& [solver, expected_ode] : {
             std::pair{"bE_nR", "be_nr"}, {"bD", "bd"}, {"rOs4", "ros4"}}) {
        config.physics.burn.odeconfig.ode_solver = solver;
        for (const char* linear : {"dEnSeLu", "DeNsE_lU", "aUtO"}) {
            config.physics.burn.odeconfig.linear_solver = linear;
            selected = resolve_cpu_factory(NetAprox13::NUM_SPECIES);
            BurnDispatcher::dispatch(
                config, selected.plan.network, selected.plan.ode_solver,
                selected.plan.linear_solver, [&](auto burner) {
                    expect(std::string(burn_route<decltype(burner)>())
                               == std::string("aprox13.") + expected_ode + ".dense_lu",
                           "case-insensitive burn aliases reach the typed factory");
                });
        }
    }
    bool direct_route = false;
    BurnDispatcher::dispatch(
        config, NetworkId::Aprox13, OdeSolverId::BeNr,
        LinearSolverId::DenseLu, [&](auto burner) {
            direct_route = std::string(burn_route<decltype(burner)>())
                == "aprox13.be_nr.dense_lu";
        });
    expect(direct_route,
           "direct burn factory route validates NSE on the concrete network");
    bool threw = false;
    config.physics.burn.network_name = "bad";
    try { (void)resolve_cpu_factory(NetAprox13::NUM_SPECIES); }
    catch (const std::runtime_error& error) {
        threw = std::string(error.what()) == "unknown burn network";
    }
    expect(threw, "unknown network is rejected before factory construction");
    config.physics.burn.network_name = "aprox13";
    config.physics.burn.odeconfig.ode_solver = "bad";
    threw = false;
    try { (void)resolve_cpu_factory(NetAprox13::NUM_SPECIES); }
    catch (const std::runtime_error& error) {
        threw = std::string(error.what()) == "unknown ODE solver";
    }
    expect(threw, "unknown ODE is rejected before factory construction");
    config.physics.burn.odeconfig.ode_solver = "BE_NR";
    config.physics.burn.odeconfig.linear_solver = "SparseKLU";
    threw = false;
    bool sparse_dispatched = false;
    try {
        selected = resolve_cpu_factory(NetAprox13::NUM_SPECIES);
        BurnDispatcher::dispatch(
            config, selected.plan.network, selected.plan.ode_solver,
            selected.plan.linear_solver, [&](auto) { sparse_dispatched = true; });
    }
    catch (const std::runtime_error& error) {
        threw = std::string(error.what())
            == "SparseKLU was selected, but this ARCH build has KLU disabled";
    }
#if ARCH_HAS_KLU
    expect(!threw && sparse_dispatched, "burn factory exposes its enabled SparseKLU provider");
#else
    expect(threw && !sparse_dispatched,
           "disabled SparseKLU is rejected before factory construction");
    threw = false;
    try {
        BurnDispatcher::dispatch(
            config, NetworkId::Aprox13, OdeSolverId::BeNr,
            LinearSolverId::SparseKlu, [&](auto) { sparse_dispatched = true; });
    } catch (const std::runtime_error& error) {
        threw = std::string(error.what())
            == "SparseKLU was selected, but this ARCH build has KLU disabled.";
    }
    expect(threw && !sparse_dispatched,
           "typed CPU factory also rejects an unavailable SparseKLU binding");
#endif
    config.physics.burn.odeconfig.linear_solver = "cUdSs";
    threw = false;
    try { (void)resolve_cpu_factory(NetAprox13::NUM_SPECIES); }
    catch (const std::runtime_error& error) {
        threw = std::string(error.what())
            == "cuDSS requires compute_backend = cuda";
    }
    expect(threw,
           "CPU capability resolution rejects case-insensitive explicit cuDSS");
    threw = false;
    try {
        BurnDispatcher::dispatch(
            config, NetworkId::Aprox13, OdeSolverId::BeNr,
            LinearSolverId::CuDss, [](auto) {});
    } catch (const std::logic_error& error) {
        threw = std::string(error.what())
            == "cuDSS requires compute_backend = cuda; the CPU burn factory "
               "has no cuDSS binding";
    }
    expect(threw, "typed CPU factory also rejects a cuDSS binding");

    config.physics.burn.use_burn = false;
    config.physics.diffusion.use_diffusion = false;
    selected = resolve_cpu_factory(0);
    Numerics::Diffusion::dispatch_diffusion(
        config, selected.plan.diffusion_integrator, [](auto integrator) {
            expect(std::string(diffusion_route<decltype(integrator)>()) == "none",
                   "disabled diffusion type");
        });
    config.physics.diffusion.use_diffusion = true;
    for (const char* name : {"RKL1", "rkl1", "RKL2", "rkl2"}) {
        config.physics.diffusion.integrator = name;
        selected = resolve_cpu_factory(0);
        Numerics::Diffusion::dispatch_diffusion(
            config, selected.plan.diffusion_integrator, [&](auto integrator) {
                expect(std::string(diffusion_route<decltype(integrator)>())
                           == (std::string(name).find('1') != std::string::npos ? "rkl1" : "rkl2"),
                       "diffusion factory concrete type");
            });
    }
    threw = false;
    try {
        Numerics::Diffusion::dispatch_diffusion(
            config, DiffusionIntegratorId::None, [](auto) {});
    } catch (const std::logic_error&) { threw = true; }
    expect(threw, "enabled diffusion rejects an inactive typed plan");
    config.physics.diffusion.use_diffusion = false;
    threw = false;
    try {
        Numerics::Diffusion::dispatch_diffusion(
            config, DiffusionIntegratorId::Rkl1, [](auto) {});
    } catch (const std::logic_error&) { threw = true; }
    expect(threw, "disabled diffusion rejects an active typed plan");

    config.physics.gravity.type = "eXtErNaL";
    config.physics.gravity.g_x = 1.0;
    config.physics.gravity.g_y = -2.0;
    config.physics.gravity.g_z = 3.0;
    selected = resolve_cpu_factory(0);
    auto gravity = Physical::Gravity::make_gravity(config, selected.requirements.gravity);
    const auto* external = dynamic_cast<const Physical::Gravity::ExternalGravity*>(gravity.get());
    expect(external && external->g_x == config.physics.gravity.g_x
               && external->g_y == config.physics.gravity.g_y
               && external->g_z == config.physics.gravity.g_z,
           "mixed-case external gravity preserves the configured acceleration");
    config.physics.gravity.type = "none";
    selected = resolve_cpu_factory(0);
    gravity = Physical::Gravity::make_gravity(config, selected.requirements.gravity);
    expect(dynamic_cast<const Physical::Gravity::GravityNone*>(gravity.get()) != nullptr,
           "resolved disabled gravity binds GravityNone");

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

void test_backend_aware_host_burn_handle()
{
    SimConfig config{};
    config.physics.burn.use_burn = true;
    config.physics.burn.use_nse = false;
    config.physics.burn.nuclearTempMin = 0.0;
    config.physics.burn.nuclearDensMin = 0.0;
    config.physics.burn.network_name = "aprox13";
    config.physics.burn.odeconfig.ode_solver = "BE_NR";
    config.physics.burn.odeconfig.linear_solver = "DenseLU";

    ResolvedExecutionPlan plan{};
    plan.network = NetworkId::Aprox13;
    plan.ode_solver = OdeSolverId::BeNr;
    plan.linear_solver = LinearSolverId::DenseLu;

    BackendResolution backend{};
    backend.requested_backend = ComputeBackend::Cpu;
    backend.resolved_backend = ComputeBackend::Cpu;
    backend.code = BackendCapabilityCode::Supported;

    double state[NetAprox13::ODE_NEQ]{};
    state[NetAprox13::ODE_NEQ - 1] = 1.0;
    double dt_recommended = 1.0;
    bool reached_cpu_burner = false;
    auto cpu_handle = BurnDispatcher::make_host_handle<HostHandleProbeEos>(
        config, plan, backend);
    try {
        (void)cpu_handle.integrate(
            state, 1.0, 1.0, HostHandleProbeEos{}, config.physics.burn,
            dt_recommended);
    } catch (const CpuHostBurnProbe&) {
        reached_cpu_burner = true;
    }
    expect(reached_cpu_burner,
           "resolved CPU binds the concrete host burner");

    // Auto is valid only after resolution.  A pre-construction fallback to CPU
    // must retain the same concrete host burner rather than a CUDA guard/no-op.
    backend.requested_backend = ComputeBackend::Auto;
    backend.resolved_backend = ComputeBackend::Cpu;
    auto fallback_handle =
        BurnDispatcher::make_host_handle<HostHandleProbeEos>(
            config, plan, backend);
    reached_cpu_burner = false;
    try {
        (void)fallback_handle.integrate(
            state, 1.0, 1.0, HostHandleProbeEos{}, config.physics.burn,
            dt_recommended);
    } catch (const CpuHostBurnProbe&) {
        reached_cpu_burner = true;
    }
    expect(reached_cpu_burner,
           "Auto fallback to CPU binds the concrete host burner");

    backend.requested_backend = ComputeBackend::Cuda;
    backend.resolved_backend = ComputeBackend::Cuda;
    auto cuda_handle = BurnDispatcher::make_host_handle<HostHandleProbeEos>(
        config, plan, backend);
    bool cuda_guard_threw = false;
    try {
        (void)cuda_handle.integrate(
            state, 1.0, 1.0, HostHandleProbeEos{}, config.physics.burn,
            dt_recommended);
    } catch (const std::logic_error& error) {
        cuda_guard_threw = std::string(error.what())
            == "active CUDA burn was invoked through the host burner handle";
    }
    expect(cuda_guard_threw,
           "active CUDA burn binds a fail-closed host guard");

    config.physics.burn.use_burn = false;
    plan.network = NetworkId::None;
    plan.ode_solver = OdeSolverId::None;
    plan.linear_solver = LinearSolverId::None;
    auto disabled_cuda_handle =
        BurnDispatcher::make_host_handle<HostHandleProbeEos>(
            config, plan, backend);
    expect(disabled_cuda_handle.integrate(
               nullptr, 0.0, 0.0, HostHandleProbeEos{}, config.physics.burn,
               dt_recommended),
           "disabled CUDA burn binds the no-op host burner");

    backend.requested_backend = ComputeBackend::Auto;
    backend.resolved_backend = ComputeBackend::Auto;
    bool threw = false;
    try {
        (void)BurnDispatcher::make_host_handle<HostHandleProbeEos>(
            config, plan, backend);
    } catch (const std::logic_error&) {
        threw = true;
    }
    expect(threw, "unresolved Auto backend is rejected");

    backend.requested_backend = ComputeBackend::Cuda;
    backend.resolved_backend = ComputeBackend::Cuda;
    config.physics.burn.use_burn = true;
    plan.network = NetworkId::Aprox13;
    plan.ode_solver = OdeSolverId::BeNr;
    plan.linear_solver = LinearSolverId::None;
    threw = false;
    try {
        (void)BurnDispatcher::make_host_handle<HostHandleProbeEos>(
            config, plan, backend);
    } catch (const std::logic_error&) {
        threw = true;
    }
    expect(threw, "active burn requires a complete resolved plan");

    config.physics.burn.use_burn = false;
    plan.linear_solver = LinearSolverId::DenseLu;
    threw = false;
    try {
        (void)BurnDispatcher::make_host_handle<HostHandleProbeEos>(
            config, plan, backend);
    } catch (const std::logic_error&) {
        threw = true;
    }
    expect(threw, "disabled burn rejects an active resolved plan");
}

} // namespace

int main()
{
    test_plain_cpp_contracts();
    test_aliases_defaults_and_plan();
    test_requirements();
    test_factory_routes_preserved();
    test_backend_aware_host_burn_handle();
    std::cout << "resolved_execution_plan: ok\n";
}
