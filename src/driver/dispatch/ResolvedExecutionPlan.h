#pragma once

#include <cstddef>
#include <cstdint>

namespace arch::dispatch
{

enum class ComputeBackend : std::uint8_t { Cpu, Cuda, Auto };
enum class FluxId : std::uint8_t { Vl, Sw, Roe, Hll, Hllc };
enum class ReconstructionId : std::uint8_t { Pcm, Muscl, Ppm };
enum class LimiterId : std::uint8_t { MinMod, Mc, SuperBee, VanLeer };
enum class TimeIntegratorId : std::uint8_t { Euler, Rk2, Rk3 };
enum class EosId : std::uint8_t { Ideal, Helmholtz, Tabular3D, Tabular4D };
enum class NetworkId : std::uint8_t { None, Aprox13, Aprox19, Aprox21, Iso7 };
enum class OdeSolverId : std::uint8_t { None, BeNr, Bd, Ros4 };
enum class LinearSolverId : std::uint8_t { None, DenseLu, SparseKlu };
enum class DiffusionIntegratorId : std::uint8_t { None, Rkl1, Rkl2 };
enum class GeometryId : std::uint8_t { Cartesian, Cylindrical, Spherical };
enum class GravityId : std::uint8_t { None, External, Self };

enum class StateLayoutRequirement : std::uint32_t
{
    None = 0,
    HydroConserved = 1u << 0,
    SpeciesMassFractions = 1u << 1,
    EnucDiagnostic = 1u << 2,
};

constexpr StateLayoutRequirement operator|(
    StateLayoutRequirement left, StateLayoutRequirement right) noexcept
{
    return static_cast<StateLayoutRequirement>(
        static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}

constexpr StateLayoutRequirement& operator|=(
    StateLayoutRequirement& left, StateLayoutRequirement right) noexcept
{
    left = left | right;
    return left;
}

constexpr bool has_layout(
    StateLayoutRequirement value, StateLayoutRequirement required) noexcept
{
    return (static_cast<std::uint32_t>(value)
            & static_cast<std::uint32_t>(required))
        == static_cast<std::uint32_t>(required);
}

enum class BoundaryFeature : std::uint32_t
{
    Periodic = 1u << 0,
    Outflow = 1u << 1,
    Reflecting = 1u << 2,
    Unknown = 1u << 31,
};

using BoundaryFeatureMask = std::uint32_t;

constexpr BoundaryFeatureMask boundary_bit(BoundaryFeature feature) noexcept
{
    return static_cast<BoundaryFeatureMask>(feature);
}

struct ResolvedExecutionPlan
{
    FluxId flux;
    ReconstructionId reconstruction;
    LimiterId limiter;
    TimeIntegratorId time_integrator;
    EosId eos;
    NetworkId network;
    OdeSolverId ode_solver;
    LinearSolverId linear_solver;
    DiffusionIntegratorId diffusion_integrator;
};

struct ExecutionRequirements
{
    int dimension;
    int root_blocks_x1;
    int root_blocks_x2;
    int root_blocks_x3;
    GeometryId geometry;
    bool uniform_multiblock;
    bool amr;
    GravityId gravity;
    bool restart;
    bool burn;
    bool diffusion;
    bool use_nse;
    bool thermal_diffusion;
    bool species_diffusion;
    bool viscous_diffusion;
    std::size_t species_count;
    int required_ghost_depth;
    StateLayoutRequirement state_layout;
    BoundaryFeatureMask boundary_features;
};

} // namespace arch::dispatch
