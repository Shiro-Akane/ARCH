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

#include "../data/FluidState.h"

#include "../core/RuntimeParams.h"

#include "../grid/Grid.h"

#include <bit>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>

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

/**
 * @brief Applies boundary conditions to the fluid state.
 * Supports outflow, reflecting, and periodic faces. Outflow ghosts copy the
 * nearest active cell; reflecting faces also reverse normal momentum.
 *
 * @param state The fluid state container (conservative variables).
 * @param grid  The grid topology information.
 */
inline void apply_boundary_conditions(FluidState &state, const Grid &grid, const SimConfig &cfg)
{
    // Species follow the same ghost-cell mapping as the conserved fields.
    int n_species = state.GetNumSpecies();

    // Copy one complete cell, including species mass fractions.
    auto copy_cell = [&](int src, int dst)
    {
        state.rho[dst] = state.rho[src];
        state.mom_u[dst] = state.mom_u[src];
        state.mom_v[dst] = state.mom_v[src];
        state.mom_w[dst] = state.mom_w[src];
        state.eng[dst] = state.eng[src];
        for (int k = 0; k < n_species; ++k)
        {
            state.X(k, dst) = state.X(k, src);
        }
    };

    auto reflect_cell = [&](int src, int dst, int dir)
    {
        copy_cell(src, dst);
        if (dir == 0)
            state.mom_u[dst] = -state.mom_u[dst]; // X-reflect
        if (dir == 1)
            state.mom_v[dst] = -state.mom_v[dst]; // Y-reflect
        if (dir == 2)
            state.mom_w[dst] = -state.mom_w[dst]; // Z-reflect
    };

    int ng = grid.ng;

    // 1. X-Direction Boundaries
    const int ks = grid.Ks();
    const int ke = grid.Ke();
    const int js = grid.Js();
    const int je = grid.Je();
    const int nk = ke - ks;
    const int nj = je - js;

#pragma omp parallel
    {
#pragma omp for schedule(static)
        for (int kj = 0; kj < nk * nj; ++kj)
    {
        int k = ks + kj / nj;
        int j = js + kj % nj;
        {
            int i_start = grid.Is();
            int i_end = grid.Ie() - 1;

            // Left X
            for (int g = 1; g <= ng; g++)
            {
                int dst = grid.GetIndex(i_start - g, j, k);
                if (cfg.grid.x1l_boundary_type == "periodic")
                    copy_cell(grid.GetIndex(i_end - g + 1, j, k), dst);
                else if (cfg.grid.x1l_boundary_type == "reflect")
                    reflect_cell(grid.GetIndex(i_start + g - 1, j, k), dst, 0);
                else
                    copy_cell(grid.GetIndex(i_start, j, k), dst); // outflow
            }
            // Right X
            for (int g = 1; g <= ng; g++)
            {
                int dst = grid.GetIndex(i_end + g, j, k);
                if (cfg.grid.x1r_boundary_type == "periodic")
                    copy_cell(grid.GetIndex(i_start + g - 1, j, k), dst);
                else if (cfg.grid.x1r_boundary_type == "reflect")
                    reflect_cell(grid.GetIndex(i_end - g + 1, j, k), dst, 0);
                else
                    copy_cell(grid.GetIndex(i_end, j, k), dst); // outflow
            }
        }
    }

    // 2. Y-direction boundaries. Include X ghost columns so corners are filled.
    if (grid.dim >= 2)
    {
        const int total_x = amr::BLOCK_NX + 2 * ng;
        const int nk2 = ke - ks;

#pragma omp for schedule(static)
        for (int ki = 0; ki < nk2 * total_x; ++ki)
        {
            int k = ks + ki / total_x;
            int i = ki % total_x;
            {
                int j_start = grid.Js();
                int j_end = grid.Je() - 1;

                // Bottom Y
                for (int g = 1; g <= ng; g++)
                {
                    int dst = grid.GetIndex(i, j_start - g, k);
                    if (cfg.grid.x2l_boundary_type == "periodic")
                        copy_cell(grid.GetIndex(i, j_end - g + 1, k), dst);
                    else if (cfg.grid.x2l_boundary_type == "reflect")
                        reflect_cell(grid.GetIndex(i, j_start + g - 1, k), dst, 1);
                    else
                        copy_cell(grid.GetIndex(i, j_start, k), dst);
                }
                // Top Y
                for (int g = 1; g <= ng; g++)
                {
                    int dst = grid.GetIndex(i, j_end + g, k);
                    if (cfg.grid.x2r_boundary_type == "periodic")
                        copy_cell(grid.GetIndex(i, j_start + g - 1, k), dst);
                    else if (cfg.grid.x2r_boundary_type == "reflect")
                        reflect_cell(grid.GetIndex(i, j_end - g + 1, k), dst, 1);
                    else
                        copy_cell(grid.GetIndex(i, j_end, k), dst);
                }
            }
        }
    }

    // 3. Z-direction boundaries. Include X/Y ghost rows to fill 3D corners.
    if (grid.dim == 3)
    {
#pragma omp for schedule(static)
        for (int j = 0; j < amr::BLOCK_NY + 2 * ng; ++j)
        { // Full Y
            for (int i = 0; i < amr::BLOCK_NX + 2 * ng; ++i)
            { // Full X
                int k_start = grid.Ks();
                int k_end = grid.Ke() - 1;

                // Back Z
                for (int g = 1; g <= ng; g++)
                {
                    int dst = grid.GetIndex(i, j, k_start - g);
                    if (cfg.grid.x3l_boundary_type == "periodic")
                        copy_cell(grid.GetIndex(i, j, k_end - g + 1), dst);
                    else if (cfg.grid.x3l_boundary_type == "reflect")
                        reflect_cell(grid.GetIndex(i, j, k_start + g - 1), dst, 2);
                    else
                        copy_cell(grid.GetIndex(i, j, k_start), dst);
                }
                // Front Z
                for (int g = 1; g <= ng; g++)
                {
                    int dst = grid.GetIndex(i, j, k_end + g);
                    if (cfg.grid.x3r_boundary_type == "periodic")
                        copy_cell(grid.GetIndex(i, j, k_start + g - 1), dst);
                    else if (cfg.grid.x3r_boundary_type == "reflect")
                        reflect_cell(grid.GetIndex(i, j, k_end - g + 1), dst, 2);
                    else
                        copy_cell(grid.GetIndex(i, j, k_end), dst);
                }
            }
        }
    }
    }
}

/**
 * @brief Binds SimConfig to the boundary-policy interface used by RK stages.
 */
struct BCHandler
{
    const SimConfig &config;

    void apply(FluidState &state, const Grid &grid) const
    {
        apply_boundary_conditions(state, grid, config);
    }
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
