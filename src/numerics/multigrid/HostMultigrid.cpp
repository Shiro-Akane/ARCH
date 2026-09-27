/**
 * @file HostMultigrid.cpp
 * @brief Solve uniform Cartesian Poisson systems with bounded V-cycles.
 *
 * Workflow:
 * 1. Receive an explicit mesh/operator and signed cell-centered fields.
 * 2. Solve uniform Cartesian Poisson systems with bounded V-cycles.
 * 3. Return corrections or fluxes through the shared numerical contract.
 */

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "numerics/multigrid/HostMultigrid.h"

#include "numerics/multigrid/MGTransfer.h"

namespace arch::multigrid {
using elliptic::BoundaryKind;
using elliptic::project_mean;
using elliptic::rms;
namespace {
// The validated bottom extent is a power of two, bounded independently of domain size.
/** Dispatch a bounded bottom matrix dimension to compile-time DenseLU. */
template<class F> bool with_bottom_size(int n, F&& operation)
{
    switch (n) {
    case 2: return operation.template operator()<2>();
    case 4: return operation.template operator()<4>();
    case 8: return operation.template operator()<8>();
    case 16: return operation.template operator()<16>();
    case 32: return operation.template operator()<32>();
    case 64: return operation.template operator()<64>();
    default: throw std::logic_error("Invalid multigrid bottom extent");
    }
}
}
/** Build factor-two Cartesian levels and factorize the coarsest operator once. */
HostMultigrid::HostMultigrid(elliptic::CartesianMesh m, BoundaryKind kind) : kind_(kind)
{
    elliptic::validate_mesh(m);
    if (m.geometry != elliptic::Geometry::Cartesian)
        throw std::invalid_argument("Standalone multigrid requires Cartesian geometry");
    if (kind != BoundaryKind::Periodic && kind != BoundaryKind::Dirichlet)
        throw std::invalid_argument("Unsupported multigrid boundary kind");
    std::vector<elliptic::CartesianMesh> meshes{m};
    while (*std::min_element(m.cells.begin(), m.cells.begin()+m.dimension) > 2) {
        for (int a = 0; a < m.dimension; ++a) { m.cells[a] /= 2; m.spacing[a] *= 2.; }
        elliptic::validate_mesh(m);
        meshes.push_back(m);
    }
    if (m.size() > bottom_capacity)
        throw std::invalid_argument("Multigrid prototype requires at most 64 bottom unknowns");
    for (const auto& grid : meshes) {
        const auto n = static_cast<std::size_t>(grid.size());
        levels_.push_back({grid, std::vector<double>(n), std::vector<double>(n),
                          std::vector<double>(n), std::vector<double>(n)});
    }
    if (!factor_bottom()) throw std::runtime_error("Multigrid bottom factorization failed");
}

/** Assemble the bottom matrix by operator columns and fix periodic gauge. */
bool HostMultigrid::factor_bottom()
{
    auto& bottom = levels_.back();
    const int n = bottom.mesh.size();
    for (int col = 0; col < n; ++col) {
        bottom.u[col] = 1.;
        for (int row = 0; row < n; ++row)
            bottom_factors_.data[row][col] = elliptic::apply_cell(bottom.mesh, kind_, bottom.u.data(), row);
        bottom.u[col] = 0.;
    }
    if (kind_ == BoundaryKind::Periodic) {
        for (int col = 0; col < n; ++col) bottom_factors_.data[0][col] = col == 0 ? 1. : 0.;
    }
    return with_bottom_size(n, [&]<int N>() {
        return DenseLUSolver::factorize<N, bottom_capacity>(bottom_factors_, bottom_pivots_);
    });
}

/** Solve the cached bottom factorization and project periodic mean. */
bool HostMultigrid::solve_bottom(Level& level)
{
    double solution[bottom_capacity]{};
    std::copy(level.rhs.begin(), level.rhs.end(), solution);
    if (kind_ == BoundaryKind::Periodic) solution[0] = 0.;
    with_bottom_size(level.mesh.size(), [&]<int N>() {
        DenseLUSolver::solve_with_factors<N, bottom_capacity>(bottom_factors_, bottom_pivots_, solution);
        return true;
    });
    std::copy_n(solution, level.mesh.size(), level.u.begin());
    if (kind_ == BoundaryKind::Periodic) project_mean(level.u);
    return std::isfinite(rms(level.u));
}

/** Recompute r = b - A u on the current mesh. */
void HostMultigrid::residual(Level& l)
{
    for (int i = 0; i < l.mesh.size(); ++i)
        l.residual[i] = l.rhs[i] - elliptic::apply_cell(l.mesh, kind_, l.u.data(), i);
}
/** Apply damped Jacobi relaxation to reduce high-frequency error. */
void HostMultigrid::smooth(Level& l)
{
    // Internal algorithm choices, not physical configuration controls.
    constexpr int sweeps = 3;
    constexpr double weight = 2./3.;
    for (int sweep = 0; sweep < sweeps; ++sweep) {
        for (int i = 0; i < l.mesh.size(); ++i) {
            const double r = l.rhs[i] - elliptic::apply_cell(l.mesh, kind_, l.u.data(), i);
            // u_new = u + (2/3)*D^-1*(b-Au).
            l.scratch[i] = l.u[i] + weight * (r / elliptic::diagonal(l.mesh, kind_, i));
        }
        l.u.swap(l.scratch);
        if (kind_ == BoundaryKind::Periodic) project_mean(l.u);
    }
}
/** Perform one pre-smooth, residual restriction, coarse correction and post-smooth V-cycle. */
bool HostMultigrid::cycle(std::size_t index)
{
    auto& fine = levels_[index];
    if (index + 1 == levels_.size()) return solve_bottom(fine);
    auto& coarse = levels_[index+1];
    smooth(fine);
    residual(fine);
    if (kind_ == BoundaryKind::Periodic) project_mean(fine.residual);
    for (int i = 0; i < coarse.mesh.size(); ++i)
        coarse.rhs[i] = restrict_cell(fine.mesh, coarse.mesh, fine.residual.data(), i);
    if (kind_ == BoundaryKind::Periodic) project_mean(coarse.rhs);
    std::fill(coarse.u.begin(), coarse.u.end(), 0.);
    if (!cycle(index+1)) return false;
    for (int i = 0; i < fine.mesh.size(); ++i)
        fine.u[i] += prolong_cell(fine.mesh, coarse.mesh, kind_, coarse.u.data(), i);
    if (kind_ == BoundaryKind::Periodic) project_mean(fine.u);
    smooth(fine);
    return std::isfinite(rms(fine.u));
}

/** Validate the source and accept only a recomputed fine-grid residual below tolerance. */
SolveResult HostMultigrid::solve(std::span<const double> rhs, const elliptic::BoundaryData& boundary,
                                SolveControl c, std::span<const double> initial)
{
    if (!std::isfinite(c.relative_tolerance) || c.relative_tolerance < 0. || c.relative_tolerance >= 1. ||
        !std::isfinite(c.absolute_tolerance) || c.absolute_tolerance <= 0. || c.max_cycles < 1)
        throw std::invalid_argument("Invalid Poisson convergence controls");
    if (boundary.kind != kind_) throw std::invalid_argument("Poisson boundary differs from workspace");
    auto& fine = levels_.front();
    if (!initial.empty()) elliptic::validate_values(initial, fine.mesh.size());
    // Complete all borrowed reads before replacing reusable workspace storage.
    auto effective = elliptic::effective_rhs(fine.mesh, boundary, rhs);
    SolveResult result;
    auto& report = result.report;
    if (!std::isfinite(rms(effective))) return result;
    if (kind_ == BoundaryKind::Periodic) {
        report.removed_rhs_mean = elliptic::mean(effective);
        const double norm = rms(effective);
        if (norm != 0. && std::abs(report.removed_rhs_mean / norm) >
            64. * std::numeric_limits<double>::epsilon())
            throw std::invalid_argument("Periodic Poisson RHS has incompatible nonzero mean");
        project_mean(effective);
    }
    fine.rhs = std::move(effective);
    if (initial.empty()) std::fill(fine.u.begin(), fine.u.end(), 0.);
    else std::copy(initial.begin(), initial.end(), fine.u.begin());
    if (kind_ == BoundaryKind::Periodic) project_mean(fine.u);
    report.rhs_rms = rms(fine.rhs);
    report.target = std::max(c.absolute_tolerance, c.relative_tolerance * report.rhs_rms);
    residual(fine);
    report.initial_residual = rms(fine.residual);
    report.residual = report.initial_residual;
    for (;;) {
        // Always test the unprojected, recomputed original fine-operator residual.
        if (!std::isfinite(report.residual)) return result;
        if (report.residual <= report.target) {
            report.status = SolveStatus::Converged;
            result.potential = fine.u;
            return result;
        }
        if (report.cycles == c.max_cycles) {
            report.status = SolveStatus::MaxCycles;
            return result;
        }
        ++report.cycles;
        if (!cycle(0)) {
            report.residual = std::numeric_limits<double>::infinity();
            return result;
        }
        residual(fine);
        report.residual = rms(fine.residual);
    }
}
} // namespace arch::multigrid
