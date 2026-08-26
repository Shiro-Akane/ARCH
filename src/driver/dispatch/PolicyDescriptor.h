#pragma once

#include "ResolvedExecutionPlan.h"
#include "../../data/GlobalDefs.h"

#include <array>
#include <cstddef>
#include <string_view>
#include <type_traits>
#include <utility>

namespace arch::dispatch
{

enum class UnknownPolicyBehavior : std::uint8_t { Error, UseDefault };

template <class Id, UnknownPolicyBehavior Behavior, class... Registrations>
struct TypeList
{
    using id_type = Id;
    static constexpr UnknownPolicyBehavior unknown_behavior = Behavior;
};

template <class List>
struct ListSize;

template <class Id, UnknownPolicyBehavior Behavior, class... Registrations>
struct ListSize<TypeList<Id, Behavior, Registrations...>>
    : std::integral_constant<std::size_t, sizeof...(Registrations)> {};

template <class List>
inline constexpr std::size_t list_size_v = ListSize<List>::value;

struct AbsentBinding {};

#define ARCH_DECLARE_BINDINGS(NAME) \
    struct Cpu##NAME##Binding {}; \
    struct Cuda##NAME##Binding {}

ARCH_DECLARE_BINDINGS(Vl);
ARCH_DECLARE_BINDINGS(Sw);
ARCH_DECLARE_BINDINGS(Roe);
ARCH_DECLARE_BINDINGS(Hll);
ARCH_DECLARE_BINDINGS(Hllc);
ARCH_DECLARE_BINDINGS(Pcm);
ARCH_DECLARE_BINDINGS(Muscl);
ARCH_DECLARE_BINDINGS(Ppm);
ARCH_DECLARE_BINDINGS(MinMod);
ARCH_DECLARE_BINDINGS(Mc);
ARCH_DECLARE_BINDINGS(SuperBee);
struct CpuVanLeerLimiterBinding {};
struct CudaVanLeerLimiterBinding {};
ARCH_DECLARE_BINDINGS(Euler);
ARCH_DECLARE_BINDINGS(Rk2);
ARCH_DECLARE_BINDINGS(Rk3);
ARCH_DECLARE_BINDINGS(Ideal);
ARCH_DECLARE_BINDINGS(Helmholtz);
ARCH_DECLARE_BINDINGS(Tabular3D);
ARCH_DECLARE_BINDINGS(Tabular4D);
ARCH_DECLARE_BINDINGS(NoNetwork);
ARCH_DECLARE_BINDINGS(Aprox13);
ARCH_DECLARE_BINDINGS(Aprox19);
ARCH_DECLARE_BINDINGS(Aprox21);
ARCH_DECLARE_BINDINGS(Iso7);
ARCH_DECLARE_BINDINGS(NoOde);
ARCH_DECLARE_BINDINGS(BeNr);
ARCH_DECLARE_BINDINGS(Bd);
ARCH_DECLARE_BINDINGS(Ros4);
ARCH_DECLARE_BINDINGS(NoLinear);
ARCH_DECLARE_BINDINGS(DenseLu);
struct CpuSparseKluBinding {};
ARCH_DECLARE_BINDINGS(NoDiffusion);
ARCH_DECLARE_BINDINGS(Rkl1);
ARCH_DECLARE_BINDINGS(Rkl2);

#undef ARCH_DECLARE_BINDINGS

struct VlPolicy {};
struct SwPolicy {};
struct RoePolicy {};
struct HllPolicy {};
struct HllcPolicy {};
struct PcmPolicy {};
struct MusclPolicy {};
struct PpmPolicy {};
struct MinModPolicy {};
struct McPolicy {};
struct SuperBeePolicy {};
struct VanLeerLimiterPolicy {};
struct EulerPolicy {};
struct Rk2Policy {};
struct Rk3Policy {};
struct IdealPolicy {};
struct HelmholtzPolicy {};
struct Tabular3DPolicy {};
struct Tabular4DPolicy {};
struct NoNetworkPolicy {};
struct Aprox13Policy {};
struct Aprox19Policy {};
struct Aprox21Policy {};
struct Iso7Policy {};
struct NoOdePolicy {};
struct BeNrPolicy {};
struct BdPolicy {};
struct Ros4Policy {};
struct NoLinearPolicy {};
struct DenseLuPolicy {};
struct SparseKluPolicy {};
struct NoDiffusionPolicy {};
struct Rkl1Policy {};
struct Rkl2Policy {};

struct StaticRequirements
{
    int ghost_depth;
    StateLayoutRequirement state_layout;
    int minimum_cuda_cc_major;
    int minimum_cuda_cc_minor;
};

template <class PolicyTag>
struct PolicyRegistration;

#define ARCH_REGISTER_POLICY(TAG, ID_TYPE, ID_VALUE, DEFAULT_VALUE, CPU_BINDING, CUDA_BINDING, GHOST, LAYOUT, ...) \
    template <> struct PolicyRegistration<TAG> { \
        using IdType = ID_TYPE; \
        static constexpr ID_TYPE id = ID_VALUE; \
        inline static constexpr std::string_view names[] = {__VA_ARGS__}; \
        inline static constexpr std::string_view parse_names[] = {__VA_ARGS__}; \
        static constexpr bool is_default = DEFAULT_VALUE; \
        using CpuBinding = CPU_BINDING; \
        using CudaBinding = CUDA_BINDING; \
        static constexpr StaticRequirements requirements{GHOST, LAYOUT, 8, 6}; \
    }

ARCH_REGISTER_POLICY(VlPolicy, FluxId, FluxId::Vl, false, CpuVlBinding, CudaVlBinding,
                     0, StateLayoutRequirement::HydroConserved, "vl", "vanleer");
ARCH_REGISTER_POLICY(SwPolicy, FluxId, FluxId::Sw, false, CpuSwBinding, CudaSwBinding,
                     0, StateLayoutRequirement::HydroConserved, "sw", "stegerwarming");
ARCH_REGISTER_POLICY(RoePolicy, FluxId, FluxId::Roe, false, CpuRoeBinding, CudaRoeBinding,
                     0, StateLayoutRequirement::HydroConserved, "roe");
ARCH_REGISTER_POLICY(HllPolicy, FluxId, FluxId::Hll, false, CpuHllBinding, CudaHllBinding,
                     0, StateLayoutRequirement::HydroConserved, "hll");
ARCH_REGISTER_POLICY(HllcPolicy, FluxId, FluxId::Hllc, true, CpuHllcBinding, CudaHllcBinding,
                     0, StateLayoutRequirement::HydroConserved, "hllc");

ARCH_REGISTER_POLICY(PcmPolicy, ReconstructionId, ReconstructionId::Pcm, true,
                     CpuPcmBinding, CudaPcmBinding, 1,
                     StateLayoutRequirement::HydroConserved, "pcm", "donor_cell");
ARCH_REGISTER_POLICY(MusclPolicy, ReconstructionId, ReconstructionId::Muscl, false,
                     CpuMusclBinding, CudaMusclBinding, 2,
                     StateLayoutRequirement::HydroConserved, "plm", "muscl");
ARCH_REGISTER_POLICY(PpmPolicy, ReconstructionId, ReconstructionId::Ppm, false,
                     CpuPpmBinding, CudaPpmBinding, 3,
                     StateLayoutRequirement::HydroConserved, "ppm");

ARCH_REGISTER_POLICY(MinModPolicy, LimiterId, LimiterId::MinMod, true,
                     CpuMinModBinding, CudaMinModBinding, 0,
                     StateLayoutRequirement::HydroConserved, "minmod");
ARCH_REGISTER_POLICY(McPolicy, LimiterId, LimiterId::Mc, false,
                     CpuMcBinding, CudaMcBinding, 0,
                     StateLayoutRequirement::HydroConserved, "mc");
ARCH_REGISTER_POLICY(SuperBeePolicy, LimiterId, LimiterId::SuperBee, false,
                     CpuSuperBeeBinding, CudaSuperBeeBinding, 0,
                     StateLayoutRequirement::HydroConserved, "superbee");
ARCH_REGISTER_POLICY(VanLeerLimiterPolicy, LimiterId, LimiterId::VanLeer, false,
                     CpuVanLeerLimiterBinding, CudaVanLeerLimiterBinding, 0,
                     StateLayoutRequirement::HydroConserved, "vanleer");

ARCH_REGISTER_POLICY(EulerPolicy, TimeIntegratorId, TimeIntegratorId::Euler, false,
                     CpuEulerBinding, CudaEulerBinding, 0,
                     StateLayoutRequirement::HydroConserved, "euler", "rk1");
ARCH_REGISTER_POLICY(Rk2Policy, TimeIntegratorId, TimeIntegratorId::Rk2, true,
                     CpuRk2Binding, CudaRk2Binding, 0,
                     StateLayoutRequirement::HydroConserved, "rk2", "ssprk2");
ARCH_REGISTER_POLICY(Rk3Policy, TimeIntegratorId, TimeIntegratorId::Rk3, false,
                     CpuRk3Binding, CudaRk3Binding, 0,
                     StateLayoutRequirement::HydroConserved, "rk3", "ssprk3");

ARCH_REGISTER_POLICY(IdealPolicy, EosId, EosId::Ideal, true,
                     CpuIdealBinding, CudaIdealBinding, 0,
                     StateLayoutRequirement::HydroConserved, "ideal");
ARCH_REGISTER_POLICY(HelmholtzPolicy, EosId, EosId::Helmholtz, false,
                     CpuHelmholtzBinding, CudaHelmholtzBinding, 0,
                     StateLayoutRequirement::HydroConserved, "helmholtz");
template <> struct PolicyRegistration<Tabular3DPolicy> {
    using IdType = EosId;
    static constexpr EosId id = EosId::Tabular3D;
    inline static constexpr std::string_view names[]{"tabular3d"};
    inline static constexpr std::string_view parse_names[]{"tabular"};
    static constexpr bool is_default = false;
    using CpuBinding = CpuTabular3DBinding;
    using CudaBinding = CudaTabular3DBinding;
    static constexpr StaticRequirements requirements{
        0, StateLayoutRequirement::HydroConserved, 8, 6};
};
template <> struct PolicyRegistration<Tabular4DPolicy> {
    using IdType = EosId;
    static constexpr EosId id = EosId::Tabular4D;
    inline static constexpr std::string_view names[]{"tabular4d"};
    inline static constexpr std::array<std::string_view, 0> parse_names{};
    static constexpr bool is_default = false;
    using CpuBinding = CpuTabular4DBinding;
    using CudaBinding = CudaTabular4DBinding;
    static constexpr StaticRequirements requirements{
        0, StateLayoutRequirement::HydroConserved, 8, 6};
};

ARCH_REGISTER_POLICY(NoNetworkPolicy, NetworkId, NetworkId::None, true,
                     CpuNoNetworkBinding, CudaNoNetworkBinding, 0,
                     StateLayoutRequirement::None, "none");
ARCH_REGISTER_POLICY(Aprox13Policy, NetworkId, NetworkId::Aprox13, false,
                     CpuAprox13Binding, CudaAprox13Binding, 0,
                     StateLayoutRequirement::SpeciesMassFractions | StateLayoutRequirement::EnucDiagnostic,
                     "aprox13");
ARCH_REGISTER_POLICY(Aprox19Policy, NetworkId, NetworkId::Aprox19, false,
                     CpuAprox19Binding, CudaAprox19Binding, 0,
                     StateLayoutRequirement::SpeciesMassFractions | StateLayoutRequirement::EnucDiagnostic,
                     "aprox19");
ARCH_REGISTER_POLICY(Aprox21Policy, NetworkId, NetworkId::Aprox21, false,
                     CpuAprox21Binding, CudaAprox21Binding, 0,
                     StateLayoutRequirement::SpeciesMassFractions | StateLayoutRequirement::EnucDiagnostic,
                     "aprox21");
ARCH_REGISTER_POLICY(Iso7Policy, NetworkId, NetworkId::Iso7, false,
                     CpuIso7Binding, CudaIso7Binding, 0,
                     StateLayoutRequirement::SpeciesMassFractions | StateLayoutRequirement::EnucDiagnostic,
                     "iso7");

ARCH_REGISTER_POLICY(NoOdePolicy, OdeSolverId, OdeSolverId::None, true,
                     CpuNoOdeBinding, CudaNoOdeBinding, 0,
                     StateLayoutRequirement::None, "none");
ARCH_REGISTER_POLICY(BeNrPolicy, OdeSolverId, OdeSolverId::BeNr, false,
                     CpuBeNrBinding, CudaBeNrBinding, 0,
                     StateLayoutRequirement::SpeciesMassFractions, "be_nr", "be-nr");
ARCH_REGISTER_POLICY(BdPolicy, OdeSolverId, OdeSolverId::Bd, false,
                     CpuBdBinding, CudaBdBinding, 0,
                     StateLayoutRequirement::SpeciesMassFractions, "bd");
ARCH_REGISTER_POLICY(Ros4Policy, OdeSolverId, OdeSolverId::Ros4, false,
                     CpuRos4Binding, CudaRos4Binding, 0,
                     StateLayoutRequirement::SpeciesMassFractions, "ros4");

ARCH_REGISTER_POLICY(NoLinearPolicy, LinearSolverId, LinearSolverId::None, true,
                     CpuNoLinearBinding, CudaNoLinearBinding, 0,
                     StateLayoutRequirement::None, "none");
ARCH_REGISTER_POLICY(DenseLuPolicy, LinearSolverId, LinearSolverId::DenseLu, false,
                     CpuDenseLuBinding, CudaDenseLuBinding, 0,
                     StateLayoutRequirement::SpeciesMassFractions, "denselu", "dense_lu");
#ifndef ARCH_HAS_KLU
#define ARCH_HAS_KLU 0
#endif
using CpuSparseKluBuildBinding = std::conditional_t<
    (ARCH_HAS_KLU != 0), CpuSparseKluBinding, AbsentBinding>;
ARCH_REGISTER_POLICY(SparseKluPolicy, LinearSolverId, LinearSolverId::SparseKlu, false,
                     CpuSparseKluBuildBinding, AbsentBinding, 0,
                     StateLayoutRequirement::SpeciesMassFractions, "sparseklu", "sparse_klu");

ARCH_REGISTER_POLICY(NoDiffusionPolicy, DiffusionIntegratorId, DiffusionIntegratorId::None, true,
                     CpuNoDiffusionBinding, CudaNoDiffusionBinding, 0,
                     StateLayoutRequirement::None, "none");
ARCH_REGISTER_POLICY(Rkl1Policy, DiffusionIntegratorId, DiffusionIntegratorId::Rkl1, false,
                     CpuRkl1Binding, CudaRkl1Binding, 0,
                     StateLayoutRequirement::HydroConserved, "rkl1");
ARCH_REGISTER_POLICY(Rkl2Policy, DiffusionIntegratorId, DiffusionIntegratorId::Rkl2, false,
                     CpuRkl2Binding, CudaRkl2Binding, 0,
                     StateLayoutRequirement::HydroConserved, "rkl2");

#undef ARCH_REGISTER_POLICY

using FluxPolicies = TypeList<FluxId, UnknownPolicyBehavior::UseDefault,
    VlPolicy, SwPolicy, RoePolicy, HllPolicy, HllcPolicy>;
using ReconstructionPolicies = TypeList<ReconstructionId, UnknownPolicyBehavior::UseDefault,
    PcmPolicy, MusclPolicy, PpmPolicy>;
using LimiterPolicies = TypeList<LimiterId, UnknownPolicyBehavior::UseDefault,
    MinModPolicy, McPolicy, SuperBeePolicy, VanLeerLimiterPolicy>;
using TimeIntegratorPolicies = TypeList<TimeIntegratorId, UnknownPolicyBehavior::UseDefault,
    EulerPolicy, Rk2Policy, Rk3Policy>;
using EosPolicies = TypeList<EosId, UnknownPolicyBehavior::Error,
    IdealPolicy, HelmholtzPolicy, Tabular3DPolicy, Tabular4DPolicy>;
using NetworkPolicies = TypeList<NetworkId, UnknownPolicyBehavior::Error,
    NoNetworkPolicy, Aprox13Policy, Aprox19Policy, Aprox21Policy, Iso7Policy>;
using OdeSolverPolicies = TypeList<OdeSolverId, UnknownPolicyBehavior::Error,
    NoOdePolicy, BeNrPolicy, BdPolicy, Ros4Policy>;
using LinearSolverPolicies = TypeList<LinearSolverId, UnknownPolicyBehavior::Error,
    NoLinearPolicy, DenseLuPolicy, SparseKluPolicy>;
using DiffusionIntegratorPolicies = TypeList<DiffusionIntegratorId, UnknownPolicyBehavior::Error,
    NoDiffusionPolicy, Rkl1Policy, Rkl2Policy>;

template <class Id>
struct PolicyDescriptor
{
    Id id;
    std::string_view canonical_name;
    bool is_default;
    bool cpu_supported;
    bool cuda_supported;
    StaticRequirements requirements;
};

template <class Registration>
consteval auto describe_policy()
{
    using Data = PolicyRegistration<Registration>;
    return PolicyDescriptor<typename Data::IdType>{
        Data::id, Data::names[0], Data::is_default,
        !std::is_same_v<typename Data::CpuBinding, AbsentBinding>,
        !std::is_same_v<typename Data::CudaBinding, AbsentBinding>,
        Data::requirements};
}

template <class List>
struct DescriptorBuilder;

template <class Id, UnknownPolicyBehavior Behavior, class... Registrations>
struct DescriptorBuilder<TypeList<Id, Behavior, Registrations...>>
{
    static consteval auto make()
    {
        return std::array{describe_policy<Registrations>()...};
    }
};

template <class List>
consteval auto make_policy_descriptors()
{
    return DescriptorBuilder<List>::make();
}

template <class T>
struct ParseResult
{
    T value{};
    bool ok = false;
    bool defaulted = false;
    std::string_view error{};
};

constexpr char ascii_lower(char value) noexcept
{
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
}

constexpr bool ascii_iequals(std::string_view left, std::string_view right) noexcept
{
    if (left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i)
        if (ascii_lower(left[i]) != ascii_lower(right[i])) return false;
    return true;
}

template <class Registration>
constexpr bool registration_matches(std::string_view value) noexcept
{
    for (std::string_view name : PolicyRegistration<Registration>::parse_names)
        if (ascii_iequals(value, name)) return true;
    return false;
}

template <class List>
struct PolicyParser;

template <class Id, UnknownPolicyBehavior Behavior, class... Registrations>
struct PolicyParser<TypeList<Id, Behavior, Registrations...>>
{
    static constexpr ParseResult<Id> parse(std::string_view value) noexcept
    {
        ParseResult<Id> result{};
        const bool matched = ([&] {
            if (registration_matches<Registrations>(value)) {
                result.value = PolicyRegistration<Registrations>::id;
                result.ok = true;
                return true;
            }
            return false;
        }() || ...);
        if (matched) return result;
        if constexpr (Behavior == UnknownPolicyBehavior::UseDefault) {
            const bool found_default = ([&] {
                if (PolicyRegistration<Registrations>::is_default) {
                    result.value = PolicyRegistration<Registrations>::id;
                    result.ok = true;
                    result.defaulted = true;
                    return true;
                }
                return false;
            }() || ...);
            if (found_default) return result;
        }
        result.error = "unknown registered policy";
        return result;
    }
};

template <class List>
constexpr auto parse_registered_policy(std::string_view value) noexcept
{
    return PolicyParser<List>::parse(value);
}

template <class List, class Function>
bool visit_policy(typename List::id_type id, Function&& function)
{
    return []<class Id, UnknownPolicyBehavior Behavior, class... Registrations>(
               TypeList<Id, Behavior, Registrations...>, Id selected, Function&& visitor) {
        return ((PolicyRegistration<Registrations>::id == selected
                     ? (visitor.template operator()<Registrations>(), true)
                     : false) || ...);
    }(List{}, id, std::forward<Function>(function));
}

template <class List>
constexpr bool policy_id_registered(typename List::id_type id) noexcept
{
    constexpr auto descriptors = make_policy_descriptors<List>();
    for (const auto& descriptor : descriptors)
        if (descriptor.id == id) return true;
    return false;
}

template <class List>
constexpr bool policy_cpu_supported(typename List::id_type id) noexcept
{
    constexpr auto descriptors = make_policy_descriptors<List>();
    for (const auto& descriptor : descriptors)
        if (descriptor.id == id) return descriptor.cpu_supported;
    return false;
}

template <class List>
constexpr bool policy_cuda_supported(typename List::id_type id) noexcept
{
    constexpr auto descriptors = make_policy_descriptors<List>();
    for (const auto& descriptor : descriptors)
        if (descriptor.id == id) return descriptor.cuda_supported;
    return false;
}

template <class List>
constexpr StaticRequirements static_requirements_for(typename List::id_type id) noexcept
{
    constexpr auto descriptors = make_policy_descriptors<List>();
    for (const auto& descriptor : descriptors)
        if (descriptor.id == id) return descriptor.requirements;
    return {};
}

inline ParseResult<ComputeBackend> parse_compute_backend(std::string_view value) noexcept
{
    if (ascii_iequals(value, "cpu")) return {ComputeBackend::Cpu, true, false, {}};
    if (ascii_iequals(value, "cuda")) return {ComputeBackend::Cuda, true, false, {}};
    if (ascii_iequals(value, "auto")) return {ComputeBackend::Auto, true, false, {}};
    return {{}, false, false, "unknown compute backend"};
}

inline ParseResult<GeometryId> parse_geometry(std::string_view value) noexcept
{
    if (ascii_iequals(value, "cartesian")) return {GeometryId::Cartesian, true, false, {}};
    if (ascii_iequals(value, "cylindrical")) return {GeometryId::Cylindrical, true, false, {}};
    if (ascii_iequals(value, "spherical")) return {GeometryId::Spherical, true, false, {}};
    return {{}, false, false, "unknown geometry"};
}

inline ParseResult<GravityId> parse_gravity(std::string_view value) noexcept
{
    if (ascii_iequals(value, "none")) return {GravityId::None, true, false, {}};
    if (ascii_iequals(value, "external")) return {GravityId::External, true, false, {}};
    if (ascii_iequals(value, "self")) return {GravityId::Self, true, false, {}};
    return {{}, false, false, "unknown gravity"};
}

inline ParseResult<BoundaryFeature> parse_boundary(std::string_view value) noexcept
{
    if (ascii_iequals(value, "periodic")) return {BoundaryFeature::Periodic, true, false, {}};
    if (ascii_iequals(value, "outflow")) return {BoundaryFeature::Outflow, true, false, {}};
    if (ascii_iequals(value, "reflect") || ascii_iequals(value, "reflecting"))
        return {BoundaryFeature::Reflecting, true, false, {}};
    return {BoundaryFeature::Unknown, false, false, "unknown boundary"};
}

template <class TableRankResolver>
ParseResult<ResolvedExecutionPlan> resolve_execution_plan(
    const SimConfig& config, TableRankResolver&& table_rank,
    std::size_t species_count = 0)
{
    ParseResult<ResolvedExecutionPlan> result{};
    const auto flux = parse_registered_policy<FluxPolicies>(config.numerics.solver_name);
    const auto reconstruction = parse_registered_policy<ReconstructionPolicies>(
        config.numerics.reconstruction);
    const auto limiter = parse_registered_policy<LimiterPolicies>(config.numerics.limiter);
    const auto time = parse_registered_policy<TimeIntegratorPolicies>(
        config.numerics.time_integrator);
    if (!flux.ok || !reconstruction.ok || !limiter.ok || !time.ok) {
        result.error = "invalid hydro policy";
        return result;
    }
    result.value.flux = flux.value;
    result.value.reconstruction = reconstruction.value;
    result.value.limiter = limiter.value;
    result.value.time_integrator = time.value;

    if (registration_matches<Tabular3DPolicy>(config.physics.eos_type)) {
        const int rank = table_rank();
        if (rank != 3 && rank != 4) {
            result.error = "Tabular EOS rank must be 3 or 4";
            return result;
        }
        result.value.eos = rank == 3 ? EosId::Tabular3D : EosId::Tabular4D;
    } else {
        const auto eos = parse_registered_policy<EosPolicies>(config.physics.eos_type);
        if (!eos.ok) { result.error = eos.error; return result; }
        result.value.eos = eos.value;
    }

    if (!config.physics.burn.use_burn) {
        result.value.network = NetworkId::None;
        result.value.ode_solver = OdeSolverId::None;
        result.value.linear_solver = LinearSolverId::None;
    } else {
        const auto network = parse_registered_policy<NetworkPolicies>(
            config.physics.burn.network_name);
        const auto ode = parse_registered_policy<OdeSolverPolicies>(
            config.physics.burn.odeconfig.ode_solver);
        ParseResult<LinearSolverId> linear{};
        if (ascii_iequals(
                config.physics.burn.odeconfig.linear_solver, "auto")) {
            linear.value = species_count > BurnLimits::MAX_SPECIES
                ? LinearSolverId::SparseKlu : LinearSolverId::DenseLu;
            linear.ok = true;
        } else {
            linear = parse_registered_policy<LinearSolverPolicies>(
                config.physics.burn.odeconfig.linear_solver);
        }
        if (!network.ok || network.value == NetworkId::None) {
            result.error = "unknown burn network";
            return result;
        }
        if (!ode.ok || ode.value == OdeSolverId::None) {
            result.error = "unknown ODE solver";
            return result;
        }
        if (!linear.ok || linear.value == LinearSolverId::None) {
            result.error = "unknown linear solver";
            return result;
        }
        result.value.network = network.value;
        result.value.ode_solver = ode.value;
        result.value.linear_solver = linear.value;
    }

    if (!config.physics.diffusion.use_diffusion) {
        result.value.diffusion_integrator = DiffusionIntegratorId::None;
    } else {
        const auto diffusion = parse_registered_policy<DiffusionIntegratorPolicies>(
            config.physics.diffusion.integrator);
        if (!diffusion.ok || diffusion.value == DiffusionIntegratorId::None) {
            result.error = "unknown diffusion integrator";
            return result;
        }
        result.value.diffusion_integrator = diffusion.value;
    }
    result.ok = true;
    result.defaulted = flux.defaulted || reconstruction.defaulted
        || limiter.defaulted || time.defaulted;
    return result;
}

inline ParseResult<ExecutionRequirements> resolve_execution_requirements(
    const SimConfig& config, std::size_t species_count)
{
    ParseResult<ExecutionRequirements> result{};
    const auto geometry = parse_geometry(config.grid.geometry);
    const auto gravity = parse_gravity(config.physics.gravity.type);
    if (!geometry.ok) { result.error = geometry.error; return result; }
    if (!gravity.ok) { result.error = gravity.error; return result; }

    BoundaryFeatureMask boundaries = 0;
    const auto add_boundary = [&](std::string_view name) {
        const auto parsed = parse_boundary(name);
        if (!parsed.ok) return false;
        boundaries |= boundary_bit(parsed.value);
        return true;
    };
    if (!add_boundary(config.grid.x1l_boundary_type)
        || !add_boundary(config.grid.x1r_boundary_type)
        || (config.grid.dim >= 2
            && (!add_boundary(config.grid.x2l_boundary_type)
                || !add_boundary(config.grid.x2r_boundary_type)))
        || (config.grid.dim == 3
            && (!add_boundary(config.grid.x3l_boundary_type)
                || !add_boundary(config.grid.x3r_boundary_type)))) {
        result.error = "unknown boundary feature";
        return result;
    }

    const auto reconstruction = parse_registered_policy<ReconstructionPolicies>(
        config.numerics.reconstruction);
    if (!reconstruction.ok) { result.error = reconstruction.error; return result; }
    const int ghost_depth = static_requirements_for<ReconstructionPolicies>(
        reconstruction.value).ghost_depth;
    const bool burn = config.physics.burn.use_burn;
    const bool diffusion = config.physics.diffusion.use_diffusion;
    StateLayoutRequirement layout = StateLayoutRequirement::HydroConserved;
    if (species_count > 0) layout |= StateLayoutRequirement::SpeciesMassFractions;
    if (burn) layout |= StateLayoutRequirement::EnucDiagnostic;

    result.value = {
        config.grid.dim,
        config.grid.nblockx1,
        config.grid.nblockx2,
        config.grid.nblockx3,
        geometry.value,
        config.grid.nblockx1 > 1
            || (config.grid.dim >= 2 && config.grid.nblockx2 > 1)
            || (config.grid.dim == 3 && config.grid.nblockx3 > 1),
        config.amr.lrefinemax > 0,
        gravity.value,
        config.io.restart,
        burn,
        diffusion,
        burn && config.physics.burn.use_nse,
        diffusion && config.physics.diffusion.use_thermal_diffusion,
        diffusion && config.physics.diffusion.use_species_diffusion,
        diffusion && config.physics.diffusion.use_viscous_diffusion,
        species_count,
        ghost_depth,
        layout,
        boundaries};
    result.ok = true;
    result.defaulted = reconstruction.defaulted;
    return result;
}

} // namespace arch::dispatch
