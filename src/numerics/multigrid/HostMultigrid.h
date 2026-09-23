/** @file HostMultigrid.h
 * Reusable single-domain CPU workspace for a constant-coefficient Poisson solve.
 * Synchronous and not thread-safe: one owner per concurrent solve.
 * Workflow:
 * 1. Receive an explicit mesh/operator and signed cell-centered fields.
 * 2. Own the reusable uniform-grid hierarchy and scratch arrays.
 * 3. Return corrections or fluxes through the shared numerical contract.
 */

#pragma once

#include <span>
#include <vector>

#include "numerics/elliptic/CartesianPoisson.h"
#include "numerics/linalg/DenseWrap.h"
#include "numerics/multigrid/MultigridTypes.h"

namespace arch::multigrid {
class HostMultigrid {
public:
    HostMultigrid(elliptic::CartesianMesh mesh, elliptic::BoundaryKind kind);
    const elliptic::CartesianMesh& mesh() const { return levels_.front().mesh; }
    elliptic::BoundaryKind boundary_kind() const { return kind_; }
    std::size_t level_count() const { return levels_.size(); }
    // Invalid inputs throw invalid_argument; numerical failure has an explicit report.
    SolveResult solve(std::span<const double> rhs, const elliptic::BoundaryData& boundary,
                      SolveControl control, std::span<const double> initial = {});
private:
    struct Level {
        elliptic::CartesianMesh mesh;
        std::vector<double> u, rhs, residual, scratch;
    };
    static constexpr int bottom_capacity = 64;
    elliptic::BoundaryKind kind_;
    std::vector<Level> levels_;
    DenseMatrixData<bottom_capacity> bottom_factors_;
    int bottom_pivots_[bottom_capacity]{};
    bool factor_bottom();
    bool solve_bottom(Level& level);
    void residual(Level& level);
    void smooth(Level& level);
    bool cycle(std::size_t level);
};
} // namespace arch::multigrid
