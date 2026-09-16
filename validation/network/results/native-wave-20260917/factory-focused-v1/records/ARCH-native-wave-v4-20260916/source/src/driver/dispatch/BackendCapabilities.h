/**
 * @file BackendCapabilities.h
 * @brief Resolve backend availability against registered execution requirements.
 *
 * Build features, policy bindings and the runtime probe determine whether a
 * CPU or CUDA candidate can be constructed. Explicit incompatible selections
 * fail here; automatic selection is resolved before backend allocation.
 */

#pragma once

#include "PolicyDescriptor.h"
#include "RuntimeProbe.h"

#include <algorithm>
#include <cstdint>
#include <limits>
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
    CudaImageUnavailable,
    UnsupportedBinding,
    SparseKluRequiresCpu,
    CuDssRequiresCuda,
    SparseKluBuildUnavailable,
    CuDssProviderUnavailable,
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

constexpr bool plans_share_non_linear_policy(
    const ResolvedExecutionPlan& left,
    const ResolvedExecutionPlan& right) noexcept
{
    return left.flux == right.flux
        && left.reconstruction == right.reconstruction
        && left.limiter == right.limiter
        && left.time_integrator == right.time_integrator
        && left.eos == right.eos
        && left.network == right.network
        && left.ode_solver == right.ode_solver
        && left.diffusion_integrator == right.diffusion_integrator;
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
    case BackendCapabilityCode::CudaImageUnavailable:
        return "this ARCH binary has no usable CUDA image for the selected device/driver; rebuild with matching CMAKE_CUDA_ARCHITECTURES or update the driver for PTX";
    case BackendCapabilityCode::UnsupportedBinding: return "policy binding unavailable";
    case BackendCapabilityCode::SparseKluRequiresCpu:
        return "SparseKLU requires compute_backend = cpu";
    case BackendCapabilityCode::CuDssRequiresCuda:
        return "cuDSS requires compute_backend = cuda";
    case BackendCapabilityCode::SparseKluBuildUnavailable:
        return "SparseKLU was selected, but this ARCH build has KLU disabled";
    case BackendCapabilityCode::CuDssProviderUnavailable:
        return "cuDSS was selected, but this ARCH build has no CUDA cuDSS provider";
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
    const ResolvedExecutionPlan& cpu_plan,
    const ResolvedExecutionPlan& cuda_plan,
    const ExecutionRequirements& requirements,
    const RuntimeProbeResult& probe) noexcept
{
    CapabilityResult result{};
    if (!detail::execution_plan_is_valid(cpu_plan)
        || !detail::execution_plan_is_valid(cuda_plan)
        || !detail::plans_share_non_linear_policy(cpu_plan, cuda_plan))
        return result;

    if (cpu_plan.linear_solver == LinearSolverId::CuDss) {
        result.cpu_code = BackendCapabilityCode::CuDssRequiresCuda;
    } else {
        result.cpu_supported = detail::cpu_bindings_exist(cpu_plan);
        result.cpu_code = result.cpu_supported
            ? BackendCapabilityCode::Supported
            : (cpu_plan.linear_solver == LinearSolverId::SparseKlu
                   ? BackendCapabilityCode::SparseKluBuildUnavailable
                   : BackendCapabilityCode::UnsupportedBinding);
    }

    const bool cpu_nse_requirements_valid = !requirements.use_nse
        || (requirements.burn && network_supports_nse(cpu_plan.network));
    if (result.cpu_supported && requirements.gravity == GravityId::Self) {
        result.cpu_supported = false;
        result.cpu_code = BackendCapabilityCode::UnsupportedGravity;
    } else if (result.cpu_supported && !cpu_nse_requirements_valid) {
        result.cpu_supported = false;
        result.cpu_code = BackendCapabilityCode::UnsupportedNse;
    } else if (result.cpu_supported && requirements.burn
               && cpu_plan.linear_solver == LinearSolverId::DenseLu
               && !BurnLimits::uses_compact_matrix(network_ode_equations(
                   cpu_plan.network, requirements.species_count))) {
        result.cpu_supported = false;
        result.cpu_code = BackendCapabilityCode::UnsupportedSpeciesCount;
    }

    const auto reject_cuda = [&](BackendCapabilityCode code) {
        result.cuda_supported = false;
        result.cuda_code = code;
        return result;
    };
    if (cuda_plan.linear_solver == LinearSolverId::SparseKlu)
        return reject_cuda(BackendCapabilityCode::SparseKluRequiresCpu);
    if (probe.state == RuntimeProbeState::BuildDisabled)
        return reject_cuda(BackendCapabilityCode::BuildDisabled);
    if (probe.state != RuntimeProbeState::Available)
        return reject_cuda(BackendCapabilityCode::RuntimeUnavailable);
    if (!detail::selected_minimum_compute_capability(cuda_plan, probe.device))
        return reject_cuda(BackendCapabilityCode::ComputeCapabilityTooLow);
    if (!probe.device.compiled_image_available)
        return reject_cuda(BackendCapabilityCode::CudaImageUnavailable);
    const StaticRequirements selected =
        detail::selected_static_requirements(cuda_plan);
    if (requirements.dimension < 1 || requirements.dimension > 3)
        return reject_cuda(BackendCapabilityCode::UnsupportedDimension);

    const bool valid_root_topology = requirements.root_blocks_x1 >= 1
        && (requirements.dimension >= 2
            ? requirements.root_blocks_x2 >= 1
            : requirements.root_blocks_x2 == 0)
        && (requirements.dimension >= 3
            ? requirements.root_blocks_x3 >= 1
            : requirements.root_blocks_x3 == 0);
    const bool is_uniform_multiblock = requirements.root_blocks_x1 > 1
        || (requirements.dimension >= 2 && requirements.root_blocks_x2 > 1)
        || (requirements.dimension >= 3 && requirements.root_blocks_x3 > 1);
    if (!valid_root_topology
        || requirements.uniform_multiblock != is_uniform_multiblock)
        return reject_cuda(BackendCapabilityCode::UnsupportedRootTopology);
    if (requirements.gravity != GravityId::None && requirements.gravity != GravityId::External)
        return reject_cuda(BackendCapabilityCode::UnsupportedGravity);
    if (requirements.geometry != GeometryId::Cartesian
        && requirements.geometry != GeometryId::Cylindrical
        && requirements.geometry != GeometryId::Spherical)
        return reject_cuda(BackendCapabilityCode::UnsupportedGeometry);
    const bool cuda_nse_requirements_valid = !requirements.use_nse
        || (requirements.burn && network_supports_nse(cuda_plan.network));
    if (!cuda_nse_requirements_valid)
        return reject_cuda(BackendCapabilityCode::UnsupportedNse);
    const bool network_enabled = cuda_plan.network != NetworkId::None;
    const bool ode_enabled = cuda_plan.ode_solver != OdeSolverId::None;
    const bool linear_enabled = cuda_plan.linear_solver != LinearSolverId::None;
    if ((requirements.burn
         && (!network_enabled || !ode_enabled || !linear_enabled))
        || (!requirements.burn
            && (network_enabled || ode_enabled || linear_enabled)))
        return reject_cuda(BackendCapabilityCode::InvalidPlan);
    if ((!requirements.diffusion
         && (requirements.thermal_diffusion || requirements.species_diffusion
             || requirements.viscous_diffusion))
        || (requirements.diffusion
            != (cuda_plan.diffusion_integrator
                != DiffusionIntegratorId::None)))
        return reject_cuda(BackendCapabilityCode::UnsupportedDiffusionMode);
    // Transport and AMR use runtime composition views/workspace. Their bound
    // is the integer state index representation, not DenseLU's small-network
    // algorithm threshold; per-block extent/allocation checks follow later.
    if (requirements.species_count
        > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return reject_cuda(BackendCapabilityCode::UnsupportedSpeciesCount);
    if (!detail::cuda_bindings_exist(cuda_plan)) {
        return reject_cuda(
            cuda_plan.linear_solver == LinearSolverId::CuDss
                && !policy_cuda_supported<LinearSolverPolicies>(LinearSolverId::CuDss)
                ? BackendCapabilityCode::CuDssProviderUnavailable
                : BackendCapabilityCode::UnsupportedBinding);
    }
    // Compact DenseLU's algorithm limit is not the sparse backend's state
    // extent. The registry/provider checks above still reject an unbuilt route.
    if (requirements.burn && !BurnLimits::uses_compact_matrix(
            network_ode_equations(cuda_plan.network, requirements.species_count))
        && cuda_plan.linear_solver != LinearSolverId::CuDss)
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
        || ((requirements.species_count > 0 || requirements.species_diffusion)
            && !has_layout(requirements.state_layout,
                           StateLayoutRequirement::SpeciesMassFractions)))
        return reject_cuda(BackendCapabilityCode::UnsupportedStateLayout);
    if ((requirements.boundary_features & ~detail::known_boundary_features) != 0)
        return reject_cuda(BackendCapabilityCode::UnsupportedBoundaryFeature);

    result.cuda_supported = true;
    result.cuda_code = BackendCapabilityCode::Supported;
    return result;
}

inline CapabilityResult query_support(
    const ResolvedExecutionPlan& plan,
    const ExecutionRequirements& requirements,
    const RuntimeProbeResult& probe) noexcept
{
    return query_support(plan, plan, requirements, probe);
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
    if (!support.cpu_supported) {
        // An explicitly requested solver pins the only compatible backend.
        // Report the missing provider on that backend rather than the less
        // actionable cross-backend rejection from the other candidate.
        if (support.cpu_code == BackendCapabilityCode::CuDssRequiresCuda)
            throw std::runtime_error(
                std::string(detail::capability_name(support.cuda_code)));
        throw std::runtime_error(
            std::string(detail::capability_name(support.cpu_code)));
    }
    result.resolved_backend = ComputeBackend::Cpu;
    result.code = support.cuda_code;
    result.fallback_reason = detail::capability_name(support.cuda_code);
    return result;
}

} // namespace arch::dispatch
