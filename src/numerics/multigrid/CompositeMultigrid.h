/** Geometry and shared MG/FGMRES control; execution owns only arrays and loops.
 * Workflow:
 * 1. Receive an explicit mesh/operator and signed cell-centered fields.
 * 2. Declare composite hierarchy ownership, solve controls and resident result access.
 * 3. Return corrections or fluxes through the shared numerical contract.
 */

#pragma once

#include "numerics/elliptic/CompositePoisson.h"
#include "numerics/multigrid/CompositeExecution.h"
#include "numerics/multigrid/MultigridTypes.h"

namespace arch::multigrid {
class CompositeMultigrid {
public:
    CompositeMultigrid(elliptic::CartesianMesh base,std::vector<elliptic::CompositeCell> cells,
        elliptic::BoundaryKind kind=elliptic::BoundaryKind::Periodic,
        std::shared_ptr<CompositeExecution> execution={});
    const elliptic::CompositePoisson& op() const { return levels_.front().op; }
    SolveResult solve(std::span<const double> rhs,SolveControl control);
    SolveReport solve(const Vector& rhs,SolveControl control);
    const Vector& resident_potential() const { return x_; }
    CompositeExecution& execution() const { return *execution_; }
    std::size_t level_count() const { return levels_.size(); }
    double mean(const Vector& x,int level=0);
    double norm(const Vector& x,int level=0);
    void project(Vector& x,int level=0);
    void gradient(const Vector& x,Vector& out,const Vector& boundary={});
    void boundary_rhs(Vector& rhs,const Vector& values);
private:
    struct Level {
        elliptic::CompositePoisson op;
        std::vector<int> parent;
        Vector u,rhs,residual,scratch,face_values,weights,diagonal,boundary;
        Array<int> anchors;
        SparseArray faces,rows,restriction,prolongation;
        explicit Level(elliptic::CompositePoisson value):op(std::move(value)){}
    };
    std::shared_ptr<CompositeExecution> execution_;
    std::vector<Level> levels_;
    SparseArray bottom_;
    Vector source_,x_,residual_,work_;
    std::vector<Vector> basis_,correction_;
    void setup();
    void smooth(int level);
    void cycle(int level);
    void precondition(const Vector&,Vector&);
    void apply(int level,const Vector&,Vector&);
    double dot(const Vector&,const Vector&,int level=0);
};
}
