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
    expect_cuda_code(plan, requirements, available(7, 5),
                     BackendCapabilityCode::ComputeCapabilityTooLow, "sm75 rejection");
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
    expect_cuda_code(p, requirements, probe, BackendCapabilityCode::UnsupportedBinding,
                     "absent binding");
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
    expect_cuda_code(plan, r, probe, BackendCapabilityCode::UnsupportedAmr, "AMR");
    r = requirements; r.gravity = GravityId::External;
    expect_cuda_code(plan, r, probe, BackendCapabilityCode::UnsupportedGravity, "external gravity");
    r = requirements; r.gravity = GravityId::Self;
    expect_cuda_code(plan, r, probe, BackendCapabilityCode::UnsupportedGravity, "self gravity");
    r = requirements; r.restart = true;
    expect_cuda_code(plan, r, probe, BackendCapabilityCode::UnsupportedRestart, "restart");
    r = requirements; r.geometry = GeometryId::Cylindrical;
    expect_cuda_code(plan, r, probe, BackendCapabilityCode::UnsupportedGeometry, "cylindrical");
    r = requirements; r.geometry = GeometryId::Spherical;
    expect_cuda_code(plan, r, probe, BackendCapabilityCode::UnsupportedGeometry, "spherical");
    r = requirements; r.burn = true;
    expect_cuda_code(plan, r, probe, BackendCapabilityCode::InvalidPlan, "burn plan mismatch");
    r = requirements; r.diffusion = true;
    expect_cuda_code(plan, r, probe, BackendCapabilityCode::UnsupportedDiffusionMode,
                     "diffusion plan mismatch");
    r = requirements; r.use_nse = true;
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
    expect_cuda_code(plan, r, probe, BackendCapabilityCode::UnsupportedSpeciesCount, "species maximum");
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
    expect(query_support(burn_plan, r, probe).cuda_supported,
           "burn and NSE supported route");
    p = burn_plan; p.network = NetworkId::None;
    expect_cuda_code(p, r, probe, BackendCapabilityCode::InvalidPlan,
                     "enabled burn requires network");
    p = burn_plan; p.ode_solver = OdeSolverId::None;
    expect_cuda_code(p, r, probe, BackendCapabilityCode::InvalidPlan,
                     "enabled burn requires ODE solver");
    p = burn_plan; p.linear_solver = LinearSolverId::None;
    expect_cuda_code(p, r, probe, BackendCapabilityCode::InvalidPlan,
                     "enabled burn requires linear solver");

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
    expect(native.state == RuntimeProbeState::Available, "H100 native probe availability");
    expect(native.device.compute_major >= 8, "H100 native compute capability");
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

} // namespace

int main()
{
    test_probe_matrix_and_failures();
    test_all_requirement_codes();
    test_resolver_and_startup_order();
    test_native_probe();
    std::cout << "runtime_probe_and_capabilities: ok\n";
}
