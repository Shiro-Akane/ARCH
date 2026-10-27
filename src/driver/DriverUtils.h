/**
 * @file DriverUtils.h
 * @brief Common boundary mapping, CFL candidates and reduction helpers.
 *
 * Host traversal and CUDA kernels consume the same logical boundary rules and
 * cell-level stability expressions. These helpers form candidates and enforce
 * boundary values; the driver owns operator ordering and macro-step selection.
 */

#pragma once

#if ARCH_MPI_ENABLED
#include <mpi.h>
#endif

#include "core/MathClassification.h"
#include "driver/schedule/ReductionSpec.h"
#include "driver/dispatch/PolicyDescriptor.h"

#include "amr/exchange/BoundaryPlan.h"
#include "data/FluidState.h"

#include "grid/Grid.h"
#include "numerics/curvilinear/GridMetrics.h"
#include "numerics/curvilinear/GeometricSources.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
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

inline double reduce_block_minimum(double identity,
                                   std::span<const arch::reduction::ReductionCandidate> candidates,
                                   int rank_count = 1)
{
    const auto result = arch::reduction::execute_host_reduction(
        arch::reduction::minimum_spec(identity), candidates);
    // Empty/NaN-only local input is an empty partial, not a global error.
    // The existing finalizer still rejects a globally empty reduction.
    if (result.status != arch::reduction::ReductionStatus::Ok &&
        !(rank_count > 1 && result.status == arch::reduction::ReductionStatus::Empty))
        throw std::runtime_error("Invalid block minimum reduction");
#if ARCH_MPI_ENABLED
    if (rank_count > 1) {
        int initialized = 0, size = 1;
        MPI_Initialized(&initialized);
        if (!initialized)
            throw std::logic_error("MPI reduction before initialization");
        MPI_Comm_size(MPI_COMM_WORLD, &size);
        if (size != rank_count)
            throw std::logic_error("MPI reduction communicator size mismatch");
        // Typed wire fields avoid transmitting C++ padding or process-local IDs.
        const auto &key = result.key;
        const std::array<std::int64_t, 9> wire{
            key.root.root_i,
            key.root.root_j,
            key.root.root_k,
            key.level,
            key.logical_i,
            key.logical_j,
            key.logical_k,
            key.component,
            static_cast<std::int64_t>(result.status == arch::reduction::ReductionStatus::Empty
                                          ? arch::reduction::ReductionStatus::Ok
                                          : result.status)};
        const std::array<std::uint64_t, 2> counts{key.morton, result.accepted_count};
        std::vector<double> values(rank_count);
        std::vector<std::int64_t> keys(static_cast<std::size_t>(rank_count) * wire.size());
        std::vector<std::uint64_t> totals(static_cast<std::size_t>(rank_count) * counts.size());
        MPI_Allgather(&result.value, 1, MPI_DOUBLE, values.data(), 1, MPI_DOUBLE, MPI_COMM_WORLD);
        MPI_Allgather(wire.data(), 9, MPI_INT64_T, keys.data(), 9, MPI_INT64_T, MPI_COMM_WORLD);
        MPI_Allgather(counts.data(), 2, MPI_UINT64_T, totals.data(), 2, MPI_UINT64_T,
                      MPI_COMM_WORLD);
        const auto spec = arch::reduction::minimum_spec(identity);
        auto merged = arch::reduction::begin_reduction(spec);
        for (int r = 0; r < rank_count; ++r) {
            const auto *k = keys.data() + 9 * r;
            const amr::CellLogicalKey global{{k[0], k[1], k[2]},     static_cast<int>(k[3]),
                                             totals[2 * r],          static_cast<int>(k[4]),
                                             static_cast<int>(k[5]), static_cast<int>(k[6]),
                                             static_cast<int>(k[7])};
            arch::reduction::combine_state(spec, merged,
                                           {values[r], global, totals[2 * r + 1],
                                            static_cast<arch::reduction::ReductionStatus>(k[8])});
        }
        const auto global = arch::reduction::finalize_reduction(spec, merged);
        if (global.status != arch::reduction::ReductionStatus::Ok)
            throw std::runtime_error("Invalid distributed block minimum reduction");
        return global.value;
    }
#else
    if (rank_count != 1)
        throw std::logic_error("distributed reduction requires ARCH_ENABLE_MPI=ON");
#endif
    return result.value;
}
} // namespace DriverReduction

ARCH_INLINE double compute_cfl_candidate(
    const FluidVector& U, double c, int dim,
    double dx1, double dx2, double dx3)
{
    if (!(c >= 0.0) || !arch::math::isfinite(c)) return std::numeric_limits<double>::quiet_NaN();
    // Sufficient convex-update bound: two face wave speeds per dimension.
    double rho = U.rho;
    double inv_dt_sum = (std::abs(U.mom_u / rho) + c) / dx1;

    if (dim >= 2)
        inv_dt_sum += (std::abs(U.mom_v / rho) + c) / dx2;

    if (dim == 3)
        inv_dt_sum += (std::abs(U.mom_w / rho) + c) / dx3;

    if (!arch::math::isfinite(inv_dt_sum) || inv_dt_sum < 0.0)
        return std::numeric_limits<double>::quiet_NaN();
    return inv_dt_sum == 0.0 ? std::numeric_limits<double>::max() : 0.5 / inv_dt_sum;
}

ARCH_INLINE double cfl_inactive_cell_dt()
{
    return std::numeric_limits<double>::max();
}

ARCH_INLINE bool is_cfl_cell_active(const FluidVector& U)
{
    return arch::math::isfinite(U.rho) && U.rho > 0.0;
}

ARCH_INLINE double compute_cfl_cell_dt(
    const FluidVector& U, double sound_speed, int dim,
    double dx1, double dx2, double dx3)
{
    if (!is_cfl_cell_active(U))
        return std::numeric_limits<double>::quiet_NaN();
    return compute_cfl_candidate(U, sound_speed, dim, dx1, dx2, dx3);
}

template <typename EosType>
ARCH_INLINE double evaluate_cfl_cell_dt(
    const FluidVector& U, const double* composition, const EosType& eos,
    int dim, double dx1, double dx2, double dx3)
{
    if (!is_cfl_cell_active(U))
        return std::numeric_limits<double>::quiet_NaN();
    // Some EOS policies derive the acoustic speed from rho,e,X directly.
    // Use that owner-provided query rather than recovering an unused pressure
    // through a second inverse; other EOS keep their pressure-based contract.
    if constexpr (requires { eos.get_sound_speed(U, composition); }) {
        return compute_cfl_cell_dt(U, eos.get_sound_speed(U, composition),
                                   dim, dx1, dx2, dx3);
    } else {
        const double pressure = eos.get_pressure(U, composition);
        const double sound_speed = eos.get_sound_speed(U, pressure, composition);
        return compute_cfl_cell_dt(U, sound_speed, dim, dx1, dx2, dx3);
    }
}

template <typename EosType>
ARCH_INLINE double evaluate_cfl_cell_dt(
    const FluidVector& U, const double* composition, const EosType& eos,
    const GridMetrics::GeometryView& grid, int i, int j)
{
    const double spatial=evaluate_cfl_cell_dt(U, composition, eos, grid.dim,
        GridMetrics::PhysicalSpacing(grid, 0, i, j),
        grid.dim >= 2 ? GridMetrics::PhysicalSpacing(grid, 1, i, j) : grid.dx2,
        grid.dim == 3 ? GridMetrics::PhysicalSpacing(grid, 2, i, j) : grid.dx3);
    if(grid.geometry==GridMetrics::Geometry::Cylindrical&&grid.dim==2) {
        const double source=TimeIntegration::cylindrical_rotation_source_dt(U,grid,i);
        if(!arch::math::isfinite(source))return arch::state::invalid();
        return std::min(spatial,source);
    }
    return spatial;
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
 * Uses dt = CFL / (2 * sum_axis((|u_axis|+c)/dx_axis)).
 * The two-face bound supports the shared conservative positivity limiter.
 */
template <typename EosType>
inline double adaptive_dt(const FluidState &state, const EosType &eos, const Grid &grid, double cfl_number,
                          bool parallel_rows = true)
{
    const auto geometry = GridMetrics::make_geometry_view(grid);
    arch::state::HostFailure failure;
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

    const auto scan_row = [&](int kj, auto& local_reduction,
                              std::vector<double>& Xi_cache) {
        try {
            const int k = ks + kj / nj;
            const int j = js + kj % nj;
            for (int i = grid.Is(); i < grid.Ie(); ++i) {
                const int idx = grid.GetIndex(i, j, k);
                const FluidVector U = state.get(idx);
                // Every visited cell is active. A reduction's Ignore-NaN
                // policy cannot turn a bad physical candidate into readiness.
                if (!is_cfl_cell_active(U))
                    throw std::runtime_error("Invalid active hydro CFL density: cell="
                        + std::to_string(idx));
                double cell_dt = std::numeric_limits<double>::quiet_NaN();
                if (is_cfl_cell_active(U)) {
                    for (int s = 0; s < n_species; ++s)
                        Xi_cache[s] = state.X(s, idx);
                    cell_dt = evaluate_cfl_cell_dt(
                        U, Xi_cache.data(), eos, geometry, i, j);
                }
                if (!(cell_dt > 0.0) || !std::isfinite(cell_dt))
                    throw std::runtime_error("Invalid active hydro CFL candidate: cell="
                        + std::to_string(idx));
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
        } catch (...) { failure.capture_current(); }
    };

    // A large block set is parallelized by the caller. Avoid launching a
    // mostly idle inner team for every one-dimensional or small patch.
    if (parallel_rows) {
#pragma omp parallel
        {
            std::vector<double> Xi_cache(n_species);
            auto local_reduction = arch::reduction::begin_reduction(reduction_spec);
#pragma omp for schedule(static)
            for (int kj = 0; kj < nk * nj; ++kj)
                scan_row(kj, local_reduction, Xi_cache);
#pragma omp critical(hydro_cfl_reduction)
            {
                arch::reduction::combine_state(
                    reduction_spec, global_reduction, local_reduction);
            }
        }
    } else {
        std::vector<double> Xi_cache(n_species);
        auto local_reduction = arch::reduction::begin_reduction(reduction_spec);
        for (int kj = 0; kj < nk * nj; ++kj)
            scan_row(kj, local_reduction, Xi_cache);
        arch::reduction::combine_state(
            reduction_spec, global_reduction, local_reduction);
    }

    failure.rethrow();
    const auto result = arch::reduction::finalize_reduction(
        reduction_spec, global_reduction);
    if (result.status != arch::reduction::ReductionStatus::Ok)
        throw std::runtime_error("Invalid hydro CFL reduction");
    return finalize_cfl_dt(cfl_number, result.value);
}
