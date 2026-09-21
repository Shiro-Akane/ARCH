/** @file HostMultigrid.h
 * Reusable single-domain CPU workspace for a constant-coefficient Poisson solve.
 * Synchronous and not thread-safe: one owner per concurrent solve.
 */
#pragma once
#include "numerics/elliptic/CartesianPoisson.h"
#include "numerics/linalg/DenseWrap.h"
#include <span>
#include <vector>

namespace arch::multigrid {
struct SolveControl {
    double relative_tolerance;
    double absolute_tolerance; // RHS units; positive even for zero source.
    int max_cycles;
};
enum class SolveStatus { Converged, MaxCycles, NumericalFailure };
struct SolveReport {
    SolveStatus status = SolveStatus::NumericalFailure;
    int cycles = 0;
    double rhs_rms = 0., initial_residual = 0., residual = 0., target = 0.;
    double removed_rhs_mean = 0.; // Roundoff only; incompatible RHS is rejected.
};
struct SolveResult {
    SolveReport report;
    std::vector<double> potential; // Empty unless converged.
};

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
