/** @file HostCompositeMG.h
 * Geometric leaf coarsening and flexible GMRES with a composite V-cycle.
 */
#pragma once
#include "numerics/elliptic/CompositePoisson.h"
#include "numerics/multigrid/HostMultigrid.h"
#include <memory>
namespace arch::multigrid {
class HostCompositeMG {
public:
    HostCompositeMG(elliptic::CartesianMesh base, std::vector<elliptic::CompositeCell> cells);
    const elliptic::CompositePoisson& op() const { return levels_.front().op; }
    SolveResult solve(std::span<const double> rhs, SolveControl control);
    std::size_t level_count() const { return levels_.size(); }
private:
    struct Level {
        elliptic::CompositePoisson op;
        std::vector<int> parent;
        std::vector<double> u, rhs, residual, scratch;
        explicit Level(elliptic::CompositePoisson value);
    };
    std::vector<Level> levels_;
    std::unique_ptr<HostMultigrid> bottom_;
    void smooth(Level& l);
    void cycle(std::size_t level);
    std::vector<double> precondition(std::span<const double> rhs);
};
} // namespace arch::multigrid
