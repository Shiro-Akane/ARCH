#include "driver/dispatch/BackendCapabilities.h"
#include "driver/dispatch/PolicyDescriptor.h"
#include "driver/dispatch/RuntimeProbe.h"

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

#ifndef ARCH_D1_TEST_CUDA_BUILD
#define ARCH_D1_TEST_CUDA_BUILD 0
#endif

namespace {

using namespace arch::dispatch;

void expect(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

RuntimeProbeResult available(int major = 9, int minor = 0)
{
    RuntimeProbeResult result{};
    result.state = RuntimeProbeState::Available;
    result.failure = ProbeFailureCode::None;
    result.device.ordinal = 0;
    result.device.compute_major = major;
    result.device.compute_minor = minor;
    result.device.runtime_version = 12080;
    result.device.driver_version = 12080;
    result.device.compiled_image_available = true;
    std::strncpy(result.device.device_name.data(), "fake", result.device.device_name.size() - 1);
    return result;
}

struct CountingLoader final : RuntimeLoader
{
    int calls = 0;
    RuntimeProbeResult answer = available();
    RuntimeProbeResult query(int) noexcept override
    {
        ++calls;
        return answer;
    }
};

ResolvedExecutionPlan supported_plan()
{
    return {FluxId::Hllc, ReconstructionId::Pcm, LimiterId::MinMod,
            TimeIntegratorId::Rk2, EosId::Ideal, NetworkId::None,
            OdeSolverId::None, LinearSolverId::None,
            DiffusionIntegratorId::None};
}

ExecutionRequirements supported_requirements()
{
    return {3, 1, 1, 1, GeometryId::Cartesian, false, false,
            GravityId::None, false, false, false, false, false, false,
            false, 0, 1, StateLayoutRequirement::HydroConserved,
            boundary_bit(BoundaryFeature::Outflow)};
}

void expect_cuda_code(const ResolvedExecutionPlan& plan,
                      const ExecutionRequirements& requirements,
                      const RuntimeProbeResult& probe,
                      BackendCapabilityCode code,
                      const char* label)
{
    const CapabilityResult support = query_support(plan, requirements, probe);
    expect(!support.cuda_supported && support.cuda_code == code, label);
}

void expect_cpu_code(const ResolvedExecutionPlan& plan,
                     const ExecutionRequirements& requirements,
                     const RuntimeProbeResult& probe,
                     BackendCapabilityCode code,
                     const char* label)
{
    const CapabilityResult support = query_support(plan, requirements, probe);
    expect(!support.cpu_supported && support.cpu_code == code, label);
}

void test_probe_matrix_and_failures()
{
    for (bool build : {false, true}) {
        for (ComputeBackend requested : {ComputeBackend::Cpu, ComputeBackend::Cuda,
                                         ComputeBackend::Auto}) {
            CountingLoader loader;
            const RuntimeProbeResult result = probe_runtime({requested, build, 0}, loader);
            if (requested == ComputeBackend::Cpu) {
                expect(result.state == RuntimeProbeState::NotRequested && loader.calls == 0,
                       "CPU must return NotRequested with zero loader calls");
            } else if (!build) {
                expect(result.state == RuntimeProbeState::BuildDisabled && loader.calls == 0,
                       "CUDA-off must return BuildDisabled with zero loader calls");
            } else {
                expect(result.state == RuntimeProbeState::Available && loader.calls == 1,
                       "CUDA-enabled CUDA/Auto must query exactly once");
            }
        }
    }

    for (const auto& result : {
             RuntimeProbeResult{RuntimeProbeState::Unavailable, ProbeFailureCode::LoaderUnavailable, {}},
             RuntimeProbeResult{RuntimeProbeState::Unavailable, ProbeFailureCode::DriverInitFailed, {}},
             RuntimeProbeResult{RuntimeProbeState::Unavailable, ProbeFailureCode::NoDevice, {}},
             RuntimeProbeResult{RuntimeProbeState::Unavailable, ProbeFailureCode::InvalidDeviceOrdinal, {}},
             RuntimeProbeResult{RuntimeProbeState::Unavailable, ProbeFailureCode::CapabilityQueryFailed, {}}}) {
        CountingLoader loader;
        loader.answer = result;
        const auto observed = probe_runtime({ComputeBackend::Cuda, true, 0}, loader);
        expect(observed.state == RuntimeProbeState::Unavailable
               && observed.failure == result.failure && loader.calls == 1,
               "probe failure must be tagged exactly");
    }
}

void test_code_image_contract()
{
    struct Case { const char* images; int major, minor, driver, compiler; bool expected; };
    const Case cases[]{
        {"86-real", 8, 6, 12030, 12030, true},
        {"80-real", 8, 6, 12020, 12030, true},
        {"86-real", 8, 0, 12030, 12030, false},
        {"86-real", 9, 0, 12030, 12030, false},
        {"86-virtual", 9, 0, 12030, 12030, true},
        {"86-virtual", 8, 0, 12030, 12030, false},
        {"86-virtual", 9, 0, 12020, 12030, false},
        {"80-real,90-real", 9, 0, 12030, 12030, true},
        {"86", 9, 0, 12030, 12030, true},
        {"86", 8, 6, 12020, 12030, true},
        {"86-virtual", 9, 0, 12030, 0, false},
        {"", 8, 6, 12030, 12030, false},
        {"86-real,", 8, 6, 12030, 12030, false},
        {"86-real,90a", 8, 6, 12030, 12030, false},
        {"native", 8, 6, 12030, 12030, false},
        {"86-real", 0, 0, 12030, 12030, false},
    };
    for (const auto& c : cases)
        expect(cuda_image_compatible(c.images, c.major, c.minor, c.driver, c.compiler) == c.expected,
               std::string("CUDA image compatibility: ") + c.images);
    const auto plan = supported_plan();
    auto probe = available();
    probe.device.compiled_image_available = false;
    const auto support = query_support(plan, supported_requirements(), probe);
    expect(resolve_backend(ComputeBackend::Auto, support, probe,
               StartupPhase::BeforeConstruction).resolved_backend == ComputeBackend::Cpu,
           "Auto may resolve missing images to CPU before construction");
    bool threw = false;
    try { (void)resolve_backend(ComputeBackend::Cuda, support, probe, StartupPhase::BeforeConstruction); }
    catch (const std::runtime_error&) { threw = true; }
    expect(threw, "explicit CUDA must reject an incompatible image");
}

void test_all_requirement_codes()
{
    const auto plan = supported_plan();
    const auto probe = available();
    auto requirements = supported_requirements();
    auto support = query_support(plan, requirements, probe);
    expect(support.cpu_supported && support.cuda_supported
           && support.cpu_code == BackendCapabilityCode::Supported
           && support.cuda_code == BackendCapabilityCode::Supported,
           "supported CPU/CUDA cell");
    auto missing_image = available(7, 5);
    missing_image.device.compiled_image_available = false;
    expect_cuda_code(plan, requirements, missing_image,
                     BackendCapabilityCode::CudaImageUnavailable, "missing image rejection");
    expect(query_support(plan, requirements, available(8, 0)).cuda_supported,
           "sm80 with a usable image must not be rejected by a local sm86 floor");
    expect(query_support(plan, requirements, available(8, 6)).cuda_supported,
           "sm86 acceptance");
    expect(query_support(plan, requirements, available(9, 0)).cuda_supported,
           "sm90 acceptance");
    expect_cuda_code(plan, requirements,
                     {RuntimeProbeState::BuildDisabled, ProbeFailureCode::None, {}},
                     BackendCapabilityCode::BuildDisabled, "build disabled");
    expect_cuda_code(plan, requirements,
                     {RuntimeProbeState::Unavailable, ProbeFailureCode::LoaderUnavailable, {}},
                     BackendCapabilityCode::RuntimeUnavailable, "runtime unavailable");
    expect_cuda_code(plan, requirements,
                     {RuntimeProbeState::NotRequested, ProbeFailureCode::None, {}},
                     BackendCapabilityCode::RuntimeUnavailable, "NotRequested is not CUDA support");

    auto p = plan;
    p.linear_solver = LinearSolverId::SparseKlu;
    expect_cuda_code(p, requirements, probe,
                     BackendCapabilityCode::SparseKluRequiresCpu,
                     "SparseKLU is CPU-only even when CUDA is available");
    p = plan;
    p.flux = static_cast<FluxId>(255);
    expect_cuda_code(p, requirements, probe, BackendCapabilityCode::InvalidPlan,
                     "invalid plan ID");
    p = plan; p.reconstruction = static_cast<ReconstructionId>(255);
    expect_cuda_code(p, requirements, probe, BackendCapabilityCode::InvalidPlan,
                     "invalid reconstruction ID");
    p = plan; p.limiter = static_cast<LimiterId>(255);
    expect_cuda_code(p, requirements, probe, BackendCapabilityCode::InvalidPlan,
                     "invalid limiter ID");
    p = plan; p.time_integrator = static_cast<TimeIntegratorId>(255);
    expect_cuda_code(p, requirements, probe, BackendCapabilityCode::InvalidPlan,
                     "invalid time ID");
    p = plan; p.eos = static_cast<EosId>(255);
    expect_cuda_code(p, requirements, probe, BackendCapabilityCode::InvalidPlan,
                     "invalid EOS ID");
    p = plan; p.network = static_cast<NetworkId>(255);
    expect_cuda_code(p, requirements, probe, BackendCapabilityCode::InvalidPlan,
                     "invalid network ID");
    p = plan; p.ode_solver = static_cast<OdeSolverId>(255);
    expect_cuda_code(p, requirements, probe, BackendCapabilityCode::InvalidPlan,
                     "invalid ODE ID");
    p = plan; p.linear_solver = static_cast<LinearSolverId>(255);
    expect_cuda_code(p, requirements, probe, BackendCapabilityCode::InvalidPlan,
                     "invalid linear ID");
    p = plan; p.diffusion_integrator = static_cast<DiffusionIntegratorId>(255);
    expect_cuda_code(p, requirements, probe, BackendCapabilityCode::InvalidPlan,
                     "invalid diffusion ID");

    p = plan; p.network = NetworkId::Iso7;
    expect_cuda_code(p, requirements, probe, BackendCapabilityCode::InvalidPlan,
                     "disabled burn requires network None");
    p = plan; p.ode_solver = OdeSolverId::BeNr;
    expect_cuda_code(p, requirements, probe, BackendCapabilityCode::InvalidPlan,
                     "disabled burn requires ODE None");
    p = plan; p.linear_solver = LinearSolverId::DenseLu;
    expect_cuda_code(p, requirements, probe, BackendCapabilityCode::InvalidPlan,
                     "disabled burn requires linear None");
    p = plan; p.diffusion_integrator = DiffusionIntegratorId::Rkl1;
    expect_cuda_code(p, requirements, probe,
                     BackendCapabilityCode::UnsupportedDiffusionMode,
                     "disabled diffusion requires integrator None");

    auto r = requirements;
    r.dimension = 0;
    expect_cuda_code(plan, r, probe, BackendCapabilityCode::UnsupportedDimension, "dimension low");
    r = requirements; r.dimension = 4;
    expect_cuda_code(plan, r, probe, BackendCapabilityCode::UnsupportedDimension, "dimension high");
    r = requirements; r.root_blocks_x1 = 2; r.uniform_multiblock = true;
    expect(query_support(plan, r, probe).cuda_supported,
           "uniform x1 multiblock");
    r = requirements; r.root_blocks_x2 = 2; r.uniform_multiblock = true;
    expect(query_support(plan, r, probe).cuda_supported,
           "uniform x2 multiblock");
    r = requirements; r.root_blocks_x3 = 0;
    expect_cuda_code(plan, r, probe, BackendCapabilityCode::UnsupportedRootTopology, "active axis root");
    r = requirements; r.dimension = 1; r.root_blocks_x2 = 1; r.root_blocks_x3 = 0;
    expect_cuda_code(plan, r, probe, BackendCapabilityCode::UnsupportedRootTopology, "inactive axis root");
    r = requirements; r.uniform_multiblock = true;
    expect_cuda_code(plan, r, probe, BackendCapabilityCode::UnsupportedRootTopology,
                     "uniform flag requires multiple roots");
    r = requirements; r.amr = true;
    expect(query_support(plan, r, probe).cuda_supported,
           "CUDA dynamic AMR support");
    r = requirements; r.gravity = GravityId::External;
    expect(query_support(plan, r, probe).cpu_supported,
           "CPU external gravity remains supported");
    expect(query_support(plan, r, probe).cuda_supported,
           "CUDA external gravity uses the common source authority");
    r = requirements; r.gravity = GravityId::Self;
    expect_cpu_code(plan, r, probe, BackendCapabilityCode::UnsupportedGravity,
                    "CPU self gravity");
    expect_cuda_code(plan, r, probe, BackendCapabilityCode::UnsupportedGravity, "self gravity");
    r = requirements; r.restart = true;
    expect(query_support(plan, r, probe).cuda_supported,
           "CUDA restart through the shared Host checkpoint schema");
    r.amr = true;
    expect(query_support(plan, r, probe).cuda_supported,
           "CUDA restart with a dynamic AMR leaf hierarchy");
    r = requirements; r.geometry = GeometryId::Cylindrical;
    expect(query_support(plan, r, probe).cuda_supported, "shared cylindrical geometry");
    r = requirements; r.geometry = GeometryId::Spherical;
    expect(query_support(plan, r, probe).cuda_supported, "shared spherical geometry");
    r = requirements; r.geometry = static_cast<GeometryId>(255);
    expect_cuda_code(plan, r, probe, BackendCapabilityCode::UnsupportedGeometry, "unknown geometry");
    r = requirements; r.burn = true;
    expect_cuda_code(plan, r, probe, BackendCapabilityCode::InvalidPlan, "burn plan mismatch");
    r = requirements; r.diffusion = true;
    expect_cuda_code(plan, r, probe, BackendCapabilityCode::UnsupportedDiffusionMode,
                     "diffusion plan mismatch");
    r = requirements; r.use_nse = true;
    expect_cpu_code(plan, r, probe, BackendCapabilityCode::UnsupportedNse,
                    "CPU NSE without burn");
    expect_cuda_code(plan, r, probe, BackendCapabilityCode::UnsupportedNse, "NSE without burn");
    r = requirements; r.thermal_diffusion = true;
    expect_cuda_code(plan, r, probe, BackendCapabilityCode::UnsupportedDiffusionMode,
                     "inactive thermal diffusion");
    r = requirements; r.species_diffusion = true;
    expect_cuda_code(plan, r, probe, BackendCapabilityCode::UnsupportedDiffusionMode,
                     "inactive species diffusion");
    r = requirements; r.viscous_diffusion = true;
    expect_cuda_code(plan, r, probe, BackendCapabilityCode::UnsupportedDiffusionMode,
                     "inactive viscous diffusion");
    r = requirements; r.species_count = BurnLimits::MAX_SPECIES + 1;
    r.state_layout |= StateLayoutRequirement::SpeciesMassFractions;
    expect(query_support(plan, r, probe).cuda_supported, "passive species exceed dense solver threshold");
    r = requirements; r.species_count = std::numeric_limits<std::size_t>::max();
    expect_cuda_code(plan, r, probe, BackendCapabilityCode::UnsupportedSpeciesCount, "species index overflow");
    r = requirements; r.required_ghost_depth = 4;
    expect_cuda_code(plan, r, probe, BackendCapabilityCode::UnsupportedGhostDepth, "ghost maximum");
    r = requirements; r.required_ghost_depth = 0;
    expect_cuda_code(plan, r, probe, BackendCapabilityCode::UnsupportedGhostDepth, "plan ghost minimum");
    r = requirements; r.state_layout = static_cast<StateLayoutRequirement>(1u << 12);
    expect_cuda_code(plan, r, probe, BackendCapabilityCode::UnsupportedStateLayout, "unknown state bit");
    r = requirements; r.state_layout = StateLayoutRequirement::SpeciesMassFractions;
    expect_cuda_code(plan, r, probe, BackendCapabilityCode::UnsupportedStateLayout, "missing hydro layout");
    r = requirements; r.boundary_features |= boundary_bit(BoundaryFeature::Unknown);
    expect_cuda_code(plan, r, probe, BackendCapabilityCode::UnsupportedBoundaryFeature,
                     "unknown boundary bit");

    auto burn_plan = plan;
    burn_plan.network = NetworkId::Iso7;
    burn_plan.ode_solver = OdeSolverId::BeNr;
    burn_plan.linear_solver = LinearSolverId::DenseLu;
    r = requirements;
    r.burn = true;
    r.use_nse = true;
    r.species_count = 7;
    r.state_layout |= StateLayoutRequirement::SpeciesMassFractions;
    r.state_layout |= StateLayoutRequirement::EnucDiagnostic;
    support = query_support(burn_plan, r, probe);
    expect(support.cpu_supported && support.cuda_supported,
           "burn and NSE supported route");
    auto large_burn = r;
    large_burn.species_count = BurnLimits::MAX_SPECIES + 1;
    expect_cuda_code(burn_plan, large_burn, probe,
                     BackendCapabilityCode::UnsupportedSpeciesCount,
                     "large transport does not open unconnected large burn");
    p = burn_plan; p.network = NetworkId::None;
    expect_cpu_code(p, r, probe, BackendCapabilityCode::UnsupportedNse,
                    "CPU NSE requires a compatible network");
    expect_cuda_code(p, r, probe, BackendCapabilityCode::UnsupportedNse,
                     "CUDA NSE requires a compatible network");
    auto burn_without_nse = r;
    burn_without_nse.use_nse = false;
    expect_cuda_code(p, burn_without_nse, probe,
                     BackendCapabilityCode::InvalidPlan,
                     "enabled burn without NSE still requires network");
    p = burn_plan; p.ode_solver = OdeSolverId::None;
    expect_cuda_code(p, r, probe, BackendCapabilityCode::InvalidPlan,
                     "enabled burn requires ODE solver");
    p = burn_plan; p.linear_solver = LinearSolverId::None;
    expect_cuda_code(p, r, probe, BackendCapabilityCode::InvalidPlan,
                     "enabled burn requires linear solver");

    p = burn_plan;
    p.linear_solver = LinearSolverId::CuDss;
    support = query_support(p, r, probe);
    expect(!support.cpu_supported
               && support.cpu_code == BackendCapabilityCode::CuDssRequiresCuda,
           "explicit CPU+cuDSS is rejected with a backend-specific reason");
#if ARCH_HAS_CUDSS_PROVIDER
    expect(support.cuda_supported,
           "committed cuDSS provider makes explicit CUDA+cuDSS available");
#else
    expect(!support.cuda_supported
               && support.cuda_code
                   == BackendCapabilityCode::CuDssProviderUnavailable,
           "parseable cuDSS fails closed without a committed provider");
#endif
    bool rejected = false;
    try {
        (void)resolve_backend(
            ComputeBackend::Cpu, support, probe,
            StartupPhase::BeforeConstruction);
    } catch (const std::runtime_error& error) {
        rejected = std::string(error.what())
            == "cuDSS requires compute_backend = cuda";
    }
    expect(rejected, "CPU+cuDSS reports the exact invalid pairing");

    p.linear_solver = LinearSolverId::SparseKlu;
    support = query_support(p, r, probe);
    expect(!support.cuda_supported
               && support.cuda_code
                   == BackendCapabilityCode::SparseKluRequiresCpu,
           "explicit CUDA+SparseKLU is rejected with a backend-specific reason");
    rejected = false;
    try {
        (void)resolve_backend(
            ComputeBackend::Cuda, support, probe,
            StartupPhase::BeforeConstruction);
    } catch (const std::runtime_error& error) {
        rejected = std::string(error.what())
            == "SparseKLU requires compute_backend = cpu";
    }
    expect(rejected, "CUDA+SparseKLU reports the exact invalid pairing");

#if ARCH_HAS_KLU
    expect(resolve_backend(
               ComputeBackend::Auto, support, probe,
               StartupPhase::BeforeConstruction).resolved_backend
               == ComputeBackend::Cpu,
           "compute Auto plus explicit SparseKLU pins CPU");
#else
    rejected = false;
    try {
        (void)resolve_backend(
            ComputeBackend::Auto, support, probe,
            StartupPhase::BeforeConstruction);
    } catch (const std::runtime_error& error) {
        rejected = std::string(error.what())
            == "SparseKLU was selected, but this ARCH build has KLU disabled";
    }
    expect(rejected,
           "compute Auto plus explicit SparseKLU fails when KLU is absent");
#endif

    p.linear_solver = LinearSolverId::CuDss;
    support = query_support(p, r, probe);
#if ARCH_HAS_CUDSS_PROVIDER
    expect(resolve_backend(
               ComputeBackend::Auto, support, probe,
               StartupPhase::BeforeConstruction).resolved_backend
               == ComputeBackend::Cuda,
           "compute Auto plus explicit cuDSS pins CUDA");
#else
    rejected = false;
    try {
        (void)resolve_backend(
            ComputeBackend::Auto, support, probe,
            StartupPhase::BeforeConstruction);
    } catch (const std::runtime_error& error) {
        rejected = std::string(error.what())
            == "cuDSS was selected, but this ARCH build has no CUDA cuDSS provider";
    }
    expect(rejected,
           "compute Auto plus explicit cuDSS fails when its provider is absent");
#endif

    const ExecutionPlanRequest automatic_request{
        burn_plan.flux, burn_plan.reconstruction, burn_plan.limiter,
        burn_plan.time_integrator, burn_plan.eos, burn_plan.network,
        burn_plan.ode_solver, LinearSolverRequest::Auto,
        burn_plan.diffusion_integrator};
    // Count temperature AND any passive source integral at the matrix boundary.
    static_assert(BurnLimits::MAX_ODE_NEQ == BurnLimits::MAX_SPECIES + 1);
    static_assert(BurnLimits::equation_count(29, 1) == 31);
    static_assert(BurnLimits::equation_count(30, 1) == 32);
    static_assert(!BurnLimits::uses_compact_matrix(
        BurnLimits::equation_count(std::numeric_limits<std::size_t>::max(), 1)));
    constexpr auto networks = make_policy_descriptors<NetworkPolicies>();
    for (const auto& network : networks) {
        if (network.id == NetworkId::None) continue;
        auto request = automatic_request;
        request.network = network.id;
        const auto largest_species = BurnLimits::MAX_ODE_NEQ - 1 - network.auxiliary_equations;
        for (const ComputeBackend backend : {ComputeBackend::Cpu, ComputeBackend::Cuda}) {
            const auto compact = materialize_execution_plan(request, backend, largest_species);
            const auto sparse = materialize_execution_plan(request, backend, largest_species + 1);
            expect(compact.ok && compact.value.linear_solver == LinearSolverId::DenseLu
                && network_ode_equations(network.id, largest_species) == BurnLimits::MAX_ODE_NEQ,
                "registered auxiliary state must be counted at the compact matrix boundary");
            expect(sparse.ok && sparse.value.linear_solver == (backend == ComputeBackend::Cpu
                    ? LinearSolverId::SparseKlu : LinearSolverId::CuDss),
                "registered auxiliary state must move the first oversized system to sparse Auto");
        }
    }
    for (const int species_count : {BurnLimits::MAX_SPECIES - 1,
                                    BurnLimits::MAX_SPECIES}) {
        for (const ComputeBackend backend : {ComputeBackend::Cpu, ComputeBackend::Cuda}) {
            const auto dense = materialize_execution_plan(
                automatic_request, backend, species_count);
            expect(dense.ok && dense.value.linear_solver == LinearSolverId::DenseLu,
                   "Auto includes the maximum dense ODE matrix on both backends");
        }
    }
    const auto large_cpu = materialize_execution_plan(
        automatic_request, ComputeBackend::Cpu,
        BurnLimits::MAX_SPECIES + 1);
    const auto large_cuda = materialize_execution_plan(
        automatic_request, ComputeBackend::Cuda,
        BurnLimits::MAX_SPECIES + 1);
    expect(large_cpu.ok && large_cuda.ok
               && large_cpu.value.linear_solver
                   == LinearSolverId::SparseKlu
               && large_cuda.value.linear_solver == LinearSolverId::CuDss,
           "Auto switches to CPU KLU/CUDA cuDSS at the first equation above the dense limit");
    struct ExplicitLinearChoice {
        LinearSolverRequest request;
        LinearSolverId expected;
    };
    for (const ExplicitLinearChoice choice : {
             ExplicitLinearChoice{LinearSolverRequest::DenseLu, LinearSolverId::DenseLu},
             ExplicitLinearChoice{LinearSolverRequest::SparseKlu, LinearSolverId::SparseKlu},
             ExplicitLinearChoice{LinearSolverRequest::CuDss, LinearSolverId::CuDss}}) {
        auto explicit_request = automatic_request;
        explicit_request.linear_solver = choice.request;
        for (const int species_count : {BurnLimits::MAX_SPECIES,
                                        BurnLimits::MAX_SPECIES + 1}) {
            for (const ComputeBackend backend : {ComputeBackend::Cpu, ComputeBackend::Cuda}) {
                const auto explicit_plan = materialize_execution_plan(
                    explicit_request, backend, species_count);
                expect(explicit_plan.ok && explicit_plan.value.linear_solver == choice.expected,
                       "explicit debug solver selection must not use Auto's size/backend heuristic");
            }
        }
    }
    // Materialization preserves explicit requests; the capability checks above
    // separately reject CPU+cuDSS, CUDA+KLU, and unavailable providers.
    auto large_requirements = r;
    large_requirements.species_count = BurnLimits::MAX_SPECIES + 1;
    // This synthetic capability-only probe isolates the linear-solver extent
    // contract using a known registered network. It is not an iso7(31) runtime
    // fixture: the typed production owner separately validates species order
    // and extent, and generated-network integration tests supply real metadata.
    support = query_support(
        large_cpu.value, large_cuda.value, large_requirements, probe);
#if ARCH_HAS_CUDSS_PROVIDER
    expect(support.cuda_supported,
           "registered sparse CUDA route is not limited by DenseLU's small-network threshold");
    const auto large_resolution = resolve_backend(
        ComputeBackend::Cuda, support, probe, StartupPhase::BeforeConstruction);
    expect(large_resolution.resolved_backend == ComputeBackend::Cuda
               && large_resolution.fallback_reason.empty(),
           "explicit large sparse CUDA route never silently falls back to CPU");
#else
    expect(!support.cuda_supported
               && support.cuda_code
                   == BackendCapabilityCode::CuDssProviderUnavailable,
           "large cuDSS candidate fails closed when the production provider is absent");
#endif

    auto diffusion_plan = plan;
    diffusion_plan.diffusion_integrator = DiffusionIntegratorId::Rkl1;
    r = requirements;
    r.diffusion = true;
    r.thermal_diffusion = true;
    expect(query_support(diffusion_plan, r, probe).cuda_supported,
           "active diffusion route");
    p = plan;
    expect_cuda_code(p, r, probe, BackendCapabilityCode::UnsupportedDiffusionMode,
                     "enabled diffusion requires non-None integrator");
}

void test_resolver_and_startup_order()
{
    const auto plan = supported_plan();
    const auto requirements = supported_requirements();
    const auto probe = available();
    const auto support = query_support(plan, requirements, probe);
    auto result = resolve_backend(ComputeBackend::Cpu, support, probe,
                                  StartupPhase::BeforeConstruction);
    expect(result.resolved_backend == ComputeBackend::Cpu
           && result.fallback_reason.empty(), "strict CPU");
    result = resolve_backend(ComputeBackend::Cuda, support, probe,
                             StartupPhase::BeforeConstruction);
    expect(result.resolved_backend == ComputeBackend::Cuda
           && result.fallback_reason.empty(), "strict CUDA");
    result = resolve_backend(ComputeBackend::Auto, support, probe,
                             StartupPhase::BeforeConstruction);
    expect(result.resolved_backend == ComputeBackend::Cuda
           && result.fallback_reason.empty(), "Auto prefers supported CUDA");

    const auto unavailable = RuntimeProbeResult{
        RuntimeProbeState::Unavailable, ProbeFailureCode::NoDevice, {}};
    const auto no_cuda = query_support(plan, requirements, unavailable);
    bool threw = false;
    try {
        (void)resolve_backend(ComputeBackend::Cuda, no_cuda, unavailable,
                              StartupPhase::BeforeConstruction);
    } catch (const std::runtime_error&) { threw = true; }
    expect(threw, "explicit CUDA may never fall back");
    result = resolve_backend(ComputeBackend::Auto, no_cuda, unavailable,
                             StartupPhase::BeforeConstruction);
    expect(result.resolved_backend == ComputeBackend::Cpu
           && result.code == BackendCapabilityCode::RuntimeUnavailable
           && !result.fallback_reason.empty(), "pre-construction Auto fallback");
    for (StartupPhase phase : {StartupPhase::Constructed, StartupPhase::Allocated,
                               StartupPhase::Running}) {
        threw = false;
        try { (void)resolve_backend(ComputeBackend::Auto, no_cuda, unavailable, phase); }
        catch (const std::runtime_error&) { threw = true; }
        expect(threw, "Auto fallback after construction/allocation/running is fatal");
    }

    StartupOrder order;
    for (StartupEvent event : {StartupEvent::Parsed, StartupEvent::RequirementsBuilt,
                               StartupEvent::Probed, StartupEvent::SupportQueried,
                               StartupEvent::Resolved, StartupEvent::Constructed,
                               StartupEvent::Allocated}) {
        order.record(event);
    }
    expect(order.last() == StartupEvent::Allocated, "canonical startup order");
    StartupOrder bad;
    bad.record(StartupEvent::Parsed);
    bad.record(StartupEvent::RequirementsBuilt);
    threw = false;
    try { bad.record(StartupEvent::SupportQueried); }
    catch (const std::logic_error&) { threw = true; }
    expect(threw, "query before probe must fail");
    StartupOrder allocation;
    allocation.record(StartupEvent::Parsed);
    allocation.record(StartupEvent::RequirementsBuilt);
    allocation.record(StartupEvent::Probed);
    allocation.record(StartupEvent::SupportQueried);
    threw = false;
    try { allocation.record(StartupEvent::Allocated); }
    catch (const std::logic_error&) { threw = true; }
    expect(threw, "allocation before resolve/construct must fail");
}

void test_native_probe()
{
    const auto cpu = probe_runtime_native({ComputeBackend::Cpu, true, 0});
    expect(cpu.state == RuntimeProbeState::NotRequested, "native CPU early return");
    const auto disabled = probe_runtime_native({ComputeBackend::Cuda, false, 0});
    expect(disabled.state == RuntimeProbeState::BuildDisabled, "native build-disabled early return");
#if ARCH_D1_TEST_CUDA_BUILD
    const auto native = probe_runtime_native({ComputeBackend::Cuda, true, 0});
    if (native.state != RuntimeProbeState::Available)
        std::cerr << "native_probe_state=" << static_cast<int>(native.state)
                  << " failure=" << static_cast<int>(native.failure) << '\n';
    expect(native.state == RuntimeProbeState::Available, "native CUDA probe availability");
    expect(native.device.compiled_image_available, "native compatible CUDA code image");
    expect(native.device.primary_context_active_before
               == native.device.primary_context_active_after,
           "native probe must not activate primary context");
    std::cout << "native_device=" << native.device.device_name.data()
              << " cc=" << native.device.compute_major << '.' << native.device.compute_minor
              << " runtime=" << native.device.runtime_version
              << " driver=" << native.device.driver_version
              << " primary_before=" << native.device.primary_context_active_before
              << " primary_after=" << native.device.primary_context_active_after << '\n';
#endif
}

void test_curvilinear_large_passive_capabilities()
{
    auto plan = supported_plan();
    plan.diffusion_integrator = DiffusionIntegratorId::Rkl2;
    for (GeometryId geometry : {GeometryId::Cartesian, GeometryId::Cylindrical,
                                GeometryId::Spherical}) {
        for (int dimension = 1; dimension <= 3; ++dimension) {
            auto requirements = supported_requirements();
            requirements.geometry = geometry;
            requirements.dimension = dimension;
            requirements.root_blocks_x2 = dimension >= 2 ? 1 : 0;
            requirements.root_blocks_x3 = dimension >= 3 ? 1 : 0;
            requirements.species_count = 41;
            requirements.state_layout |= StateLayoutRequirement::SpeciesMassFractions;
            requirements.amr = requirements.restart = true;
            requirements.diffusion = requirements.thermal_diffusion = true;
            requirements.viscous_diffusion = requirements.species_diffusion = true;
            const auto support = query_support(plan, requirements, available());
            expect(support.cpu_supported && support.cuda_supported,
                   "known geometry + large passive species + diffusion + AMR/restart");
            requirements.state_layout = StateLayoutRequirement::HydroConserved;
            expect_cuda_code(plan, requirements, available(),
                             BackendCapabilityCode::UnsupportedStateLayout,
                             "large transport requires composition state storage");
        }
    }
}

} // namespace

int main()
{
    test_probe_matrix_and_failures();
    test_code_image_contract();
    test_all_requirement_codes();
    test_curvilinear_large_passive_capabilities();
    test_resolver_and_startup_order();
    test_native_probe();
    std::cout << "runtime_probe_and_capabilities: ok\n";
}
