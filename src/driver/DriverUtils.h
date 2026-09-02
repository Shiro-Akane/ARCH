/**
 * @file DriverUtils.h
 * @brief Utility functions for boundary enforcement and time-step control.
 * Provides essential support for the main driver loop, specifically:
 * 1. Populating ghost cells to enforce boundary conditions (e.g., Outflow).
 * 2. Computing the adaptive time step (dt) based on the CFL stability criterion.
 */

/**
 * Workflow:
 * 1. Select the configured policy and determine a stable macro step.
 * 2. Apply hydro, diffusion, gravity, and burn operators in the documented order.
 * 3. Synchronize AMR leaves and emit diagnostics before continuing the evolution.
 */

#pragma once

#include "ReductionSpec.h"
#include "dispatch/PolicyDescriptor.h"

#include "../amr/BoundaryPlan.h"
#include "../data/FluidState.h"

#include "../core/RuntimeParams.h"

#include "../grid/Grid.h"

#include <bit>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace DriverReduction
{
enum class BlockReductionComponent : int {
    Hydro = 1,
    Diffusion = 2,
    BurnFirstHalf = 3,
    BurnSecondHalf = 4
};

inline amr::CellLogicalKey make_block_reduction_key(
    int level, std::uint64_t morton, std::uint32_t logical_x1,
    std::uint32_t logical_x2, std::uint32_t logical_x3,
    BlockReductionComponent component)
{
    const auto root = amr::root_logical_key_from_leaf(
        level, logical_x1, logical_x2, logical_x3);
    if (!root)
        throw std::runtime_error("Invalid block coordinates for reduction key");
    return {
        *root, level, morton,
        static_cast<int>(logical_x1), static_cast<int>(logical_x2),
        static_cast<int>(logical_x3), static_cast<int>(component)};
}

inline amr::CellLogicalKey make_accumulator_reduction_key(
    BlockReductionComponent component) noexcept
{
    constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
    return {{minimum, minimum, minimum}, 0, 0, 0, 0, 0,
            static_cast<int>(component)};
}

inline double reduce_block_minimum(
    double identity,
    std::span<const arch::reduction::ReductionCandidate> candidates)
{
    const auto result = arch::reduction::execute_host_reduction(
        arch::reduction::minimum_spec(identity), candidates);
    if (result.status != arch::reduction::ReductionStatus::Ok)
        throw std::runtime_error("Invalid block minimum reduction");
    return result.value;
}
} // namespace DriverReduction

namespace arch::boundary::host
{
struct HostBoundaryLayout {
    int dimension;
    int active_i;
    int active_j;
    int active_k;
    int ghost_depth;
    int active_origin_i;
    int active_origin_j;
    int active_origin_k;
    int total_x;
    int total_y;
    int total_z;
    int stride_y;
    int stride_z;
    int total_size;

    friend constexpr bool operator==(
        const HostBoundaryLayout&, const HostBoundaryLayout&) = default;
};

struct HostCompiledBoundaryTransfer {
    std::uint64_t logical_ordinal;
    int source_index;
    int destination_index;
    // rho, three momenta, energy, and scalar ENUC diagnostic.
    std::array<std::int8_t, 6> conserved_signs;
    std::int8_t species_sign;
};

struct HostCompiledBoundaryPlan {
    std::uint64_t logical_fingerprint;
    HostBoundaryLayout layout;
    std::array<BoundaryPhase, 3> phases;
    std::vector<HostCompiledBoundaryTransfer> transfers;
};

inline HostBoundaryLayout make_layout(const Grid& grid)
{
    if (grid.dim < 1 || grid.dim > 3 || grid.ng < 0)
        throw std::invalid_argument("Invalid runtime Grid boundary metadata");
    const auto ghost = static_cast<std::int64_t>(grid.ng);
    const auto expected_total_x = static_cast<std::int64_t>(amr::BLOCK_NX)
        + 2 * ghost;
    const auto expected_total_y = grid.dim >= 2
        ? static_cast<std::int64_t>(amr::BLOCK_NY) + 2 * ghost : 1;
    const auto expected_total_z = grid.dim == 3
        ? static_cast<std::int64_t>(amr::BLOCK_NZ) + 2 * ghost : 1;
    constexpr auto int_max = std::numeric_limits<int>::max();
    if (expected_total_x > int_max || expected_total_y > int_max
        || expected_total_z > int_max
        || grid.GetTotalX() != expected_total_x
        || grid.GetTotalY() != expected_total_y
        || grid.GetTotalZ() != expected_total_z)
        throw std::invalid_argument("Runtime Grid extents are not canonical");
    return {
        grid.dim,
        amr::BLOCK_NX, grid.dim >= 2 ? amr::BLOCK_NY : 1,
        grid.dim == 3 ? amr::BLOCK_NZ : 1,
        grid.ng,
        grid.ng, grid.dim >= 2 ? grid.ng : 0,
        grid.dim == 3 ? grid.ng : 0,
        grid.GetTotalX(), grid.GetTotalY(), grid.GetTotalZ(),
        grid.stride_y, grid.stride_z, grid.GetTotalSize()};
}

inline HostBoundaryLayout make_canonical_layout(int dimension)
{
    if (dimension < 1 || dimension > 3)
        throw std::invalid_argument("Boundary dimension must be in [1,3]");
    const int total_y = dimension >= 2
        ? amr::BLOCK_NY + 2 * amr::MAX_NG : 1;
    const int total_z = dimension == 3
        ? amr::BLOCK_NZ + 2 * amr::MAX_NG : 1;
    return {
        dimension,
        amr::BLOCK_NX, dimension >= 2 ? amr::BLOCK_NY : 1,
        dimension == 3 ? amr::BLOCK_NZ : 1,
        amr::MAX_NG,
        amr::MAX_NG, dimension >= 2 ? amr::MAX_NG : 0,
        dimension == 3 ? amr::MAX_NG : 0,
        amr::BLOCK_NX + 2 * amr::MAX_NG, total_y, total_z,
        amr::PAD_NX, amr::PAD_NX * total_y,
        amr::PAD_NX * total_y * total_z};
}

inline void validate_layout(
    const BoundaryPlan& plan, const HostBoundaryLayout& layout)
{
    const auto& input = plan.input();
    if (layout.dimension != input.dimension
        || layout.active_i != input.active_extent[0]
        || layout.active_j != input.active_extent[1]
        || layout.active_k != input.active_extent[2]
        || layout.ghost_depth != static_cast<int>(input.ghost_depth)
        || layout.total_x <= 0 || layout.total_y <= 0 || layout.total_z <= 0
        || layout.stride_y < layout.total_x || layout.stride_z <= 0
        || layout.total_size <= 0
        || !arch::boundary::detail::contains_active_region(
            layout.active_origin_i, layout.active_i, layout.ghost_depth,
            layout.total_x)
        || !arch::boundary::detail::contains_active_region(
            layout.active_origin_j, layout.active_j,
            layout.dimension >= 2 ? layout.ghost_depth : 0,
            layout.total_y)
        || !arch::boundary::detail::contains_active_region(
            layout.active_origin_k, layout.active_k,
            layout.dimension == 3 ? layout.ghost_depth : 0,
            layout.total_z)
        || static_cast<std::int64_t>(layout.stride_y) * layout.total_y
            > layout.stride_z
        || static_cast<std::int64_t>(layout.stride_z) * layout.total_z
            > layout.total_size)
        throw std::invalid_argument("Host boundary layout does not match logical plan");
}

inline int flatten_checked(
    const HostBoundaryLayout& layout, LogicalCellRef logical)
{
    const std::int64_t i = static_cast<std::int64_t>(layout.active_origin_i)
        + logical.i;
    const std::int64_t j = static_cast<std::int64_t>(layout.active_origin_j)
        + logical.j;
    const std::int64_t k = static_cast<std::int64_t>(layout.active_origin_k)
        + logical.k;
    if (i < 0 || i >= layout.total_x || j < 0 || j >= layout.total_y
        || k < 0 || k >= layout.total_z)
        throw std::out_of_range("Boundary logical coordinate is outside Host layout");
    const std::int64_t index = k * layout.stride_z + j * layout.stride_y + i;
    if (index < 0 || index >= layout.total_size
        || index > std::numeric_limits<int>::max())
        throw std::overflow_error("Boundary Host flattened index is invalid");
    return static_cast<int>(index);
}

inline HostCompiledBoundaryPlan compile(
    const BoundaryPlan& plan, const HostBoundaryLayout& layout)
{
    validate_layout(plan, layout);
    HostCompiledBoundaryPlan compiled{};
    compiled.logical_fingerprint = plan.fingerprint();
    compiled.layout = layout;
    for (std::size_t phase = 0; phase < compiled.phases.size(); ++phase)
        compiled.phases[phase] = plan.phases()[phase];
    compiled.transfers.reserve(plan.operations().size());
    for (const auto& operation : plan.operations()) {
        HostCompiledBoundaryTransfer transfer{};
        transfer.logical_ordinal = operation.ordinal;
        transfer.source_index = flatten_checked(layout, operation.source);
        transfer.destination_index = flatten_checked(layout, operation.destination);
        for (std::uint8_t field = 0; field < 5; ++field) {
            transfer.conserved_signs[field] = component_mapping(
                operation, static_cast<BoundaryFieldClass>(field)).sign;
        }
        transfer.conserved_signs[5] = component_mapping(
            operation, BoundaryFieldClass::AllSpecies).sign;
        transfer.species_sign = component_mapping(
            operation, BoundaryFieldClass::AllSpecies).sign;
        compiled.transfers.push_back(transfer);
    }
    return compiled;
}

inline void validate_state(
    const HostCompiledBoundaryPlan& compiled, const FluidState& state)
{
    const auto size = static_cast<std::size_t>(compiled.layout.total_size);
    const auto n_species = state.GetNumSpecies();
    if (state.rho.size() != size || state.mom_u.size() != size
        || state.mom_v.size() != size || state.mom_w.size() != size
        || state.eng.size() != size || state.enuc_rate.size() != size
        || n_species < 0
        || (n_species != 0
            && size > std::numeric_limits<std::size_t>::max()
                / static_cast<std::size_t>(n_species))
        || state.mass_fractions.size()
            != size * static_cast<std::size_t>(n_species))
        throw std::invalid_argument("FluidState does not match compiled boundary layout");
}

inline void validate_compiled_plan(const HostCompiledBoundaryPlan& compiled)
{
    if (compiled.transfers.size()
        > static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max()))
        throw std::invalid_argument("Host boundary transfer count is not executable");

    std::size_t expected_first = 0;
    for (std::size_t phase_index = 0;
         phase_index < compiled.phases.size(); ++phase_index) {
        const auto& phase = compiled.phases[phase_index];
        if (phase.id != static_cast<BoundaryPhaseId>(phase_index)
            || phase.first != expected_first
            || phase.first > compiled.transfers.size()
            || phase.count > compiled.transfers.size() - phase.first)
            throw std::invalid_argument("Invalid Host boundary phase metadata");
        expected_first += phase.count;
    }
    if (expected_first != compiled.transfers.size())
        throw std::invalid_argument("Host boundary phases do not cover transfers");

    for (std::size_t index = 0; index < compiled.transfers.size(); ++index) {
        const auto& transfer = compiled.transfers[index];
        const auto valid_sign = [](std::int8_t sign) {
            return sign == -1 || sign == 1;
        };
        bool signs_valid = valid_sign(transfer.species_sign);
        for (const auto sign : transfer.conserved_signs)
            signs_valid = signs_valid && valid_sign(sign);
        if (transfer.logical_ordinal != index
            || transfer.source_index < 0
            || transfer.source_index >= compiled.layout.total_size
            || transfer.destination_index < 0
            || transfer.destination_index >= compiled.layout.total_size
            || !signs_valid)
            throw std::invalid_argument("Invalid Host boundary transfer metadata");
    }
}

inline double signed_copy(double value, std::int8_t sign) noexcept
{
    return sign == -1 ? -value : value;
}

inline void execute(
    const HostCompiledBoundaryPlan& compiled, FluidState& state)
{
    validate_compiled_plan(compiled);
    validate_state(compiled, state);
    const int n_species = state.GetNumSpecies();
#pragma omp parallel
    {
        for (std::size_t phase_index = 0;
             phase_index < compiled.phases.size(); ++phase_index) {
            const auto phase = compiled.phases[phase_index];
#pragma omp for schedule(static)
            for (std::ptrdiff_t offset = 0;
                 offset < static_cast<std::ptrdiff_t>(phase.count); ++offset) {
                const auto& transfer = compiled.transfers[phase.first + offset];
                const int source = transfer.source_index;
                const int destination = transfer.destination_index;
                state.rho[destination] = signed_copy(
                    state.rho[source], transfer.conserved_signs[0]);
                state.mom_u[destination] = signed_copy(
                    state.mom_u[source], transfer.conserved_signs[1]);
                state.mom_v[destination] = signed_copy(
                    state.mom_v[source], transfer.conserved_signs[2]);
                state.mom_w[destination] = signed_copy(
                    state.mom_w[source], transfer.conserved_signs[3]);
                state.eng[destination] = signed_copy(
                    state.eng[source], transfer.conserved_signs[4]);
                state.enuc_rate[destination] = signed_copy(
                    state.enuc_rate[source], transfer.conserved_signs[5]);
                for (int species = 0; species < n_species; ++species) {
                    state.X(species, destination) = signed_copy(
                        state.X(species, source), transfer.species_sign);
                }
            }
        }
    }
}
} // namespace arch::boundary::host

/**
 * @brief Binds SimConfig to the boundary-policy interface used by RK stages.
 */
struct BCHandler
{
    explicit BCHandler(const SimConfig& config)
        : logical_plan_(make_logical_plan(config)),
          compiled_(arch::boundary::host::compile(
              logical_plan_,
              arch::boundary::host::make_canonical_layout(
                  logical_plan_.input().dimension)))
    {
    }

    void apply(FluidState &state, const Grid &grid) const
    {
        const auto actual = arch::boundary::host::make_layout(grid);
        if (!(actual == compiled_.layout))
            throw std::invalid_argument("Grid does not match prepared boundary layout");
        arch::boundary::host::execute(compiled_, state);
    }

    const arch::boundary::BoundaryPlan& logical_plan() const noexcept
    {
        return logical_plan_;
    }

private:
    static arch::boundary::BoundaryType parse_active_face(
        std::string_view value)
    {
        const auto parsed = arch::dispatch::parse_boundary(value);
        if (!parsed.ok)
            throw std::invalid_argument("Unknown active boundary type");
        switch (parsed.value) {
        case arch::dispatch::BoundaryFeature::Periodic:
            return arch::boundary::BoundaryType::Periodic;
        case arch::dispatch::BoundaryFeature::Outflow:
            return arch::boundary::BoundaryType::Outflow;
        case arch::dispatch::BoundaryFeature::Reflecting:
            return arch::boundary::BoundaryType::Reflecting;
        case arch::dispatch::BoundaryFeature::Unknown:
            break;
        }
        throw std::invalid_argument("Unknown active boundary type");
    }

    static arch::boundary::BoundaryPlan make_logical_plan(
        const SimConfig& config)
    {
        using arch::boundary::BoundaryAxis;
        using arch::boundary::BoundaryPlanInput;
        using arch::boundary::BoundarySide;
        using arch::boundary::BoundaryType;
        using arch::boundary::face_index;

        BoundaryPlanInput input{};
        input.dimension = config.grid.dim;
        input.active_extent = {
            amr::BLOCK_NX,
            input.dimension >= 2 ? amr::BLOCK_NY : 1,
            input.dimension == 3 ? amr::BLOCK_NZ : 1};
        input.ghost_depth = amr::MAX_NG;
        input.faces.fill(BoundaryType::Inactive);
        input.faces[face_index(BoundaryAxis::X1, BoundarySide::Lower)] =
            parse_active_face(config.grid.x1l_boundary_type);
        input.faces[face_index(BoundaryAxis::X1, BoundarySide::Upper)] =
            parse_active_face(config.grid.x1r_boundary_type);
        if (input.dimension >= 2) {
            input.faces[face_index(BoundaryAxis::X2, BoundarySide::Lower)] =
                parse_active_face(config.grid.x2l_boundary_type);
            input.faces[face_index(BoundaryAxis::X2, BoundarySide::Upper)] =
                parse_active_face(config.grid.x2r_boundary_type);
        }
        if (input.dimension == 3) {
            input.faces[face_index(BoundaryAxis::X3, BoundarySide::Lower)] =
                parse_active_face(config.grid.x3l_boundary_type);
            input.faces[face_index(BoundaryAxis::X3, BoundarySide::Upper)] =
                parse_active_face(config.grid.x3r_boundary_type);
        }
        return arch::boundary::make_boundary_plan(input);
    }

    arch::boundary::BoundaryPlan logical_plan_;
    arch::boundary::host::HostCompiledBoundaryPlan compiled_;
};

ARCH_INLINE double compute_cfl_candidate(
    const FluidVector& U, double c, int dim,
    double dx1, double dx2, double dx3)
{
    double rho = U.rho;
    double inv_dt_sum = (std::abs(U.mom_u / rho) + c) / dx1;

    if (dim >= 2)
        inv_dt_sum += (std::abs(U.mom_v / rho) + c) / dx2;

    if (dim == 3)
        inv_dt_sum += (std::abs(U.mom_w / rho) + c) / dx3;

    return 1.0 / std::max(inv_dt_sum, 1e-10);
}

ARCH_INLINE double cfl_inactive_cell_dt()
{
    return 1e10;
}

ARCH_INLINE bool is_cfl_cell_active(const FluidVector& U)
{
    return U.rho >= 1e-12;
}

ARCH_INLINE double compute_cfl_cell_dt(
    const FluidVector& U, double sound_speed, int dim,
    double dx1, double dx2, double dx3)
{
    if (!is_cfl_cell_active(U))
        return cfl_inactive_cell_dt();
    return compute_cfl_candidate(U, sound_speed, dim, dx1, dx2, dx3);
}

template <typename EosType>
ARCH_INLINE double evaluate_cfl_cell_dt(
    const FluidVector& U, const double* composition, const EosType& eos,
    int dim, double dx1, double dx2, double dx3)
{
    if (!is_cfl_cell_active(U))
        return cfl_inactive_cell_dt();
    const double pressure = eos.get_pressure(U, composition);
    const double sound_speed = eos.get_sound_speed(U, pressure, composition);
    return compute_cfl_cell_dt(U, sound_speed, dim, dx1, dx2, dx3);
}

ARCH_INLINE bool cfl_value_is_nan(double value)
{
#if defined(__CUDA_ARCH__)
    const std::uint64_t bits = static_cast<std::uint64_t>(
        __double_as_longlong(value));
#else
    const std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
#endif
    constexpr std::uint64_t exponent = 0x7ff0000000000000ULL;
    constexpr std::uint64_t mantissa = 0x000fffffffffffffULL;
    return (bits & exponent) == exponent && (bits & mantissa) != 0;
}

ARCH_INLINE double combine_cfl_minimum(double minimum, double candidate)
{
    const auto spec = arch::reduction::minimum_spec(cfl_inactive_cell_dt());
    auto state = arch::reduction::begin_reduction(spec);
    arch::reduction::combine_candidate(
        spec, state, {minimum, {}, true});
    amr::CellLogicalKey candidate_key{};
    candidate_key.component = 1;
    arch::reduction::combine_candidate(
        spec, state, {candidate, candidate_key, true});
    return arch::reduction::finalize_reduction(spec, state).value;
}

ARCH_INLINE double finalize_cfl_dt(double cfl_number, double minimum)
{
    return cfl_number * minimum;
}

/**
 * @brief Computes adaptive time step (dt) strictly evaluating 3D wave speeds.
 * Uses dt = CFL * min( dx1/(|u|+c), dx2/(|v|+c), dx3/(|w|+c) )
 */
template <typename EosType>
inline double adaptive_dt(const FluidState &state, const EosType &eos, const Grid &grid, double cfl_number)
{
    int n_species = state.GetNumSpecies();
    const auto reduction_spec = arch::reduction::minimum_spec(
        cfl_inactive_cell_dt());
    auto global_reduction = arch::reduction::begin_reduction(reduction_spec);

    const int ks = grid.Ks();
    const int ke = grid.Ke();
    const int js = grid.Js();
    const int je = grid.Je();
    const int nk = ke - ks;
    const int nj = je - js;

#pragma omp parallel
    {
        std::vector<double> Xi_cache(n_species);
        auto local_reduction = arch::reduction::begin_reduction(reduction_spec);

#pragma omp for schedule(static)
        for (int kj = 0; kj < nk * nj; ++kj)
        {
            int k = ks + kj / nj;
            int j = js + kj % nj;
            for (int i = grid.Is(); i < grid.Ie(); ++i)
            {
                int idx = grid.GetIndex(i, j, k);
                FluidVector U = state.get(idx);
                double cell_dt = cfl_inactive_cell_dt();
                if (is_cfl_cell_active(U)) {
                    for (int s = 0; s < n_species; ++s)
                        Xi_cache[s] = state.X(s, idx);
                    cell_dt = evaluate_cfl_cell_dt(
                        U, Xi_cache.data(), eos, grid.dim,
                        grid.dx1, grid.dx2, grid.dx3);
                }
                amr::CellLogicalKey cell_key{};
                cell_key.logical_i = i;
                cell_key.logical_j = j;
                cell_key.logical_k = k;
                cell_key.component = static_cast<int>(
                    DriverReduction::BlockReductionComponent::Hydro);
                arch::reduction::combine_candidate(
                    reduction_spec, local_reduction,
                    {cell_dt, cell_key, true});
            }
        }

#pragma omp critical(hydro_cfl_reduction)
        {
            arch::reduction::combine_state(
                reduction_spec, global_reduction, local_reduction);
        }
    }

    const auto result = arch::reduction::finalize_reduction(
        reduction_spec, global_reduction);
    if (result.status != arch::reduction::ReductionStatus::Ok)
        throw std::runtime_error("Invalid hydro CFL reduction");
    return finalize_cfl_dt(cfl_number, result.value);
}
