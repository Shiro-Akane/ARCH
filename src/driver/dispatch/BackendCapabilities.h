#pragma once

#include "PolicyDescriptor.h"
#include "RuntimeProbe.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace arch::dispatch
{

enum class BackendCapabilityCode : std::uint16_t
{
    Supported,
    InvalidPlan,
    BuildDisabled,
    RuntimeUnavailable,
    ComputeCapabilityTooLow,
    UnsupportedBinding,
    UnsupportedDimension,
    UnsupportedRootTopology,
    UnsupportedAmr,
    UnsupportedGravity,
    UnsupportedRestart,
    UnsupportedGeometry,
    UnsupportedNse,
    UnsupportedDiffusionMode,
    UnsupportedSpeciesCount,
    UnsupportedGhostDepth,
    UnsupportedStateLayout,
    UnsupportedBoundaryFeature,
};

struct CapabilityResult
{
    bool cpu_supported{};
    bool cuda_supported{};
    BackendCapabilityCode cpu_code{BackendCapabilityCode::InvalidPlan};
    BackendCapabilityCode cuda_code{BackendCapabilityCode::InvalidPlan};
};

enum class StartupPhase : std::uint8_t
{
    BeforeConstruction,
    Constructed,
    Allocated,
    Running,
};

struct BackendResolution
{
    ComputeBackend requested_backend{};
    ComputeBackend resolved_backend{};
    BackendCapabilityCode code{BackendCapabilityCode::InvalidPlan};
    std::string fallback_reason{};
    DeviceCapability device{};
};

enum class StartupEvent : std::uint8_t
{
    None,
    Parsed,
    RequirementsBuilt,
    Probed,
    SupportQueried,
    Resolved,
    Constructed,
    Allocated,
    Running,
};

class StartupOrder
{
public:
    void record(StartupEvent event)
    {
        const auto expected = static_cast<unsigned int>(last_) + 1u;
        if (static_cast<unsigned int>(event) != expected)
            throw std::logic_error("invalid startup event order");
        last_ = event;
    }

    [[nodiscard]] StartupEvent last() const noexcept { return last_; }

private:
    StartupEvent last_{StartupEvent::None};
};

namespace detail
{

constexpr bool execution_plan_is_valid(const ResolvedExecutionPlan& plan) noexcept
{
    if (!policy_id_registered<FluxPolicies>(plan.flux)
        || !policy_id_registered<ReconstructionPolicies>(plan.reconstruction)
        || !policy_id_registered<LimiterPolicies>(plan.limiter)
        || !policy_id_registered<TimeIntegratorPolicies>(plan.time_integrator)
        || !policy_id_registered<EosPolicies>(plan.eos)
        || !policy_id_registered<NetworkPolicies>(plan.network)
        || !policy_id_registered<OdeSolverPolicies>(plan.ode_solver)
        || !policy_id_registered<LinearSolverPolicies>(plan.linear_solver)
        || !policy_id_registered<DiffusionIntegratorPolicies>(plan.diffusion_integrator))
        return false;
    return true;
}

constexpr bool cpu_bindings_exist(const ResolvedExecutionPlan& plan) noexcept
{
    return policy_cpu_supported<FluxPolicies>(plan.flux)
        && policy_cpu_supported<ReconstructionPolicies>(plan.reconstruction)
        && policy_cpu_supported<LimiterPolicies>(plan.limiter)
        && policy_cpu_supported<TimeIntegratorPolicies>(plan.time_integrator)
        && policy_cpu_supported<EosPolicies>(plan.eos)
        && policy_cpu_supported<NetworkPolicies>(plan.network)
        && policy_cpu_supported<OdeSolverPolicies>(plan.ode_solver)
        && policy_cpu_supported<LinearSolverPolicies>(plan.linear_solver)
        && policy_cpu_supported<DiffusionIntegratorPolicies>(plan.diffusion_integrator);
}

constexpr bool cuda_bindings_exist(const ResolvedExecutionPlan& plan) noexcept
{
    return policy_cuda_supported<FluxPolicies>(plan.flux)
        && policy_cuda_supported<ReconstructionPolicies>(plan.reconstruction)
        && policy_cuda_supported<LimiterPolicies>(plan.limiter)
        && policy_cuda_supported<TimeIntegratorPolicies>(plan.time_integrator)
        && policy_cuda_supported<EosPolicies>(plan.eos)
        && policy_cuda_supported<NetworkPolicies>(plan.network)
        && policy_cuda_supported<OdeSolverPolicies>(plan.ode_solver)
        && policy_cuda_supported<LinearSolverPolicies>(plan.linear_solver)
        && policy_cuda_supported<DiffusionIntegratorPolicies>(plan.diffusion_integrator);
}

constexpr bool compute_capability_at_least(
    const DeviceCapability& device, int major, int minor) noexcept
{
    return device.compute_major > major
        || (device.compute_major == major && device.compute_minor >= minor);
}

constexpr StaticRequirements selected_static_requirements(
    const ResolvedExecutionPlan& plan) noexcept
{
    const StaticRequirements selected[] = {
        static_requirements_for<FluxPolicies>(plan.flux),
        static_requirements_for<ReconstructionPolicies>(plan.reconstruction),
        static_requirements_for<LimiterPolicies>(plan.limiter),
        static_requirements_for<TimeIntegratorPolicies>(plan.time_integrator),
        static_requirements_for<EosPolicies>(plan.eos),
        static_requirements_for<NetworkPolicies>(plan.network),
        static_requirements_for<OdeSolverPolicies>(plan.ode_solver),
        static_requirements_for<LinearSolverPolicies>(plan.linear_solver),
        static_requirements_for<DiffusionIntegratorPolicies>(plan.diffusion_integrator),
    };
    StaticRequirements aggregate{};
    for (const StaticRequirements& requirement : selected) {
        aggregate.ghost_depth = std::max(
            aggregate.ghost_depth, requirement.ghost_depth);
        aggregate.state_layout |= requirement.state_layout;
        if (requirement.minimum_cuda_cc_major
                > aggregate.minimum_cuda_cc_major
            || (requirement.minimum_cuda_cc_major
                    == aggregate.minimum_cuda_cc_major
                && requirement.minimum_cuda_cc_minor
                    > aggregate.minimum_cuda_cc_minor)) {
            aggregate.minimum_cuda_cc_major =
                requirement.minimum_cuda_cc_major;
            aggregate.minimum_cuda_cc_minor =
                requirement.minimum_cuda_cc_minor;
        }
    }
    return aggregate;
}

constexpr bool selected_minimum_compute_capability(
    const ResolvedExecutionPlan& plan, const DeviceCapability& device) noexcept
{
    const StaticRequirements required = selected_static_requirements(plan);
    return compute_capability_at_least(
        device, required.minimum_cuda_cc_major,
        required.minimum_cuda_cc_minor);
}

constexpr BoundaryFeatureMask known_boundary_features =
    boundary_bit(BoundaryFeature::Periodic)
    | boundary_bit(BoundaryFeature::Outflow)
    | boundary_bit(BoundaryFeature::Reflecting);

constexpr StateLayoutRequirement known_state_layout =
    StateLayoutRequirement::HydroConserved
    | StateLayoutRequirement::SpeciesMassFractions
    | StateLayoutRequirement::EnucDiagnostic;

constexpr std::string_view capability_name(BackendCapabilityCode code) noexcept
{
    switch (code) {
    case BackendCapabilityCode::Supported: return "supported";
    case BackendCapabilityCode::InvalidPlan: return "invalid plan";
    case BackendCapabilityCode::BuildDisabled: return "CUDA build disabled";
    case BackendCapabilityCode::RuntimeUnavailable: return "CUDA runtime unavailable";
    case BackendCapabilityCode::ComputeCapabilityTooLow: return "compute capability too low";
    case BackendCapabilityCode::UnsupportedBinding: return "policy binding unavailable";
    case BackendCapabilityCode::UnsupportedDimension: return "dimension unsupported";
    case BackendCapabilityCode::UnsupportedRootTopology: return "root topology unsupported";
    case BackendCapabilityCode::UnsupportedAmr: return "AMR unsupported";
    case BackendCapabilityCode::UnsupportedGravity: return "gravity unsupported";
    case BackendCapabilityCode::UnsupportedRestart: return "restart unsupported";
    case BackendCapabilityCode::UnsupportedGeometry: return "geometry unsupported";
    case BackendCapabilityCode::UnsupportedNse: return "NSE requirements invalid";
    case BackendCapabilityCode::UnsupportedDiffusionMode: return "diffusion mode invalid";
    case BackendCapabilityCode::UnsupportedSpeciesCount: return "species count unsupported";
    case BackendCapabilityCode::UnsupportedGhostDepth: return "ghost depth unsupported";
    case BackendCapabilityCode::UnsupportedStateLayout: return "state layout unsupported";
    case BackendCapabilityCode::UnsupportedBoundaryFeature: return "boundary feature unsupported";
    }
    return "unknown capability";
}

} // namespace detail

inline CapabilityResult query_support(
    const ResolvedExecutionPlan& plan,
    const ExecutionRequirements& requirements,
    const RuntimeProbeResult& probe) noexcept
{
    CapabilityResult result{};
    if (!detail::execution_plan_is_valid(plan)) return result;

    result.cpu_supported = detail::cpu_bindings_exist(plan);
    result.cpu_code = result.cpu_supported
        ? BackendCapabilityCode::Supported
        : BackendCapabilityCode::UnsupportedBinding;

    const auto reject_cuda = [&](BackendCapabilityCode code) {
        result.cuda_supported = false;
        result.cuda_code = code;
        return result;
    };
    if (probe.state == RuntimeProbeState::BuildDisabled)
        return reject_cuda(BackendCapabilityCode::BuildDisabled);
    if (probe.state != RuntimeProbeState::Available)
        return reject_cuda(BackendCapabilityCode::RuntimeUnavailable);
    if (!detail::selected_minimum_compute_capability(plan, probe.device))
        return reject_cuda(BackendCapabilityCode::ComputeCapabilityTooLow);
    if (!detail::cuda_bindings_exist(plan))
        return reject_cuda(BackendCapabilityCode::UnsupportedBinding);
    const StaticRequirements selected = detail::selected_static_requirements(plan);
    if (requirements.dimension < 1 || requirements.dimension > 3)
        return reject_cuda(BackendCapabilityCode::UnsupportedDimension);

    const int expected_x2 = requirements.dimension >= 2 ? 1 : 0;
    const int expected_x3 = requirements.dimension >= 3 ? 1 : 0;
    if (requirements.root_blocks_x1 != 1
        || requirements.root_blocks_x2 != expected_x2
        || requirements.root_blocks_x3 != expected_x3
        || requirements.uniform_multiblock)
        return reject_cuda(BackendCapabilityCode::UnsupportedRootTopology);
    if (requirements.amr)
        return reject_cuda(BackendCapabilityCode::UnsupportedAmr);
    if (requirements.gravity != GravityId::None)
        return reject_cuda(BackendCapabilityCode::UnsupportedGravity);
    if (requirements.restart)
        return reject_cuda(BackendCapabilityCode::UnsupportedRestart);
    if (requirements.geometry != GeometryId::Cartesian)
        return reject_cuda(BackendCapabilityCode::UnsupportedGeometry);
    const bool network_enabled = plan.network != NetworkId::None;
    const bool ode_enabled = plan.ode_solver != OdeSolverId::None;
    const bool linear_enabled = plan.linear_solver != LinearSolverId::None;
    if ((requirements.burn
         && (!network_enabled || !ode_enabled || !linear_enabled))
        || (!requirements.burn
            && (network_enabled || ode_enabled || linear_enabled)))
        return reject_cuda(BackendCapabilityCode::InvalidPlan);
    if (requirements.use_nse && !requirements.burn)
        return reject_cuda(BackendCapabilityCode::UnsupportedNse);
    if ((!requirements.diffusion
         && (requirements.thermal_diffusion || requirements.species_diffusion
             || requirements.viscous_diffusion))
        || (requirements.diffusion
            != (plan.diffusion_integrator != DiffusionIntegratorId::None)))
        return reject_cuda(BackendCapabilityCode::UnsupportedDiffusionMode);
    if (requirements.species_count > BurnLimits::MAX_SPECIES)
        return reject_cuda(BackendCapabilityCode::UnsupportedSpeciesCount);
    if (requirements.required_ghost_depth < selected.ghost_depth
        || requirements.required_ghost_depth > 3)
        return reject_cuda(BackendCapabilityCode::UnsupportedGhostDepth);

    const auto state_bits = static_cast<std::uint32_t>(requirements.state_layout);
    const auto known_state_bits = static_cast<std::uint32_t>(detail::known_state_layout);
    if ((state_bits & ~known_state_bits) != 0
        || !has_layout(requirements.state_layout,
                       StateLayoutRequirement::HydroConserved)
        || !has_layout(requirements.state_layout, selected.state_layout)
        || (requirements.species_diffusion
            && !has_layout(requirements.state_layout,
                           StateLayoutRequirement::SpeciesMassFractions)))
        return reject_cuda(BackendCapabilityCode::UnsupportedStateLayout);
    if ((requirements.boundary_features & ~detail::known_boundary_features) != 0)
        return reject_cuda(BackendCapabilityCode::UnsupportedBoundaryFeature);

    result.cuda_supported = true;
    result.cuda_code = BackendCapabilityCode::Supported;
    return result;
}

inline BackendResolution resolve_backend(
    ComputeBackend requested,
    const CapabilityResult& support,
    const RuntimeProbeResult& probe,
    StartupPhase phase)
{
    BackendResolution result{};
    result.requested_backend = requested;
    result.device = probe.device;
    if (requested == ComputeBackend::Cpu) {
        if (!support.cpu_supported)
            throw std::runtime_error(std::string(detail::capability_name(support.cpu_code)));
        result.resolved_backend = ComputeBackend::Cpu;
        result.code = support.cpu_code;
        return result;
    }
    if (requested == ComputeBackend::Cuda) {
        if (!support.cuda_supported)
            throw std::runtime_error(std::string(detail::capability_name(support.cuda_code)));
        result.resolved_backend = ComputeBackend::Cuda;
        result.code = support.cuda_code;
        return result;
    }
    if (requested != ComputeBackend::Auto)
        throw std::runtime_error("invalid requested backend");
    if (support.cuda_supported) {
        result.resolved_backend = ComputeBackend::Cuda;
        result.code = support.cuda_code;
        return result;
    }
    if (phase != StartupPhase::BeforeConstruction)
        throw std::runtime_error("backend selection is closed after construction begins");
    if (!support.cpu_supported)
        throw std::runtime_error(std::string(detail::capability_name(support.cpu_code)));
    result.resolved_backend = ComputeBackend::Cpu;
    result.code = support.cuda_code;
    result.fallback_reason = detail::capability_name(support.cuda_code);
    return result;
}

} // namespace arch::dispatch
