/** @file CompositePoisson.h
 * Periodic cell-centered finite-volume operator on a dyadic Cartesian leaf mesh.
 * The owner contains geometry and local face stencils, never fluid/AMR storage.
 * Workflow:
 * 1. Receive an explicit mesh/operator and signed cell-centered fields.
 * 2. Describe composite leaf geometry, face stencils and operator queries.
 * 3. Return corrections or fluxes through the shared numerical contract.
 */

#pragma once

#include <array>
#include <limits>
#include <span>
#include <unordered_map>
#include <vector>

#include "core/CompensatedSum.h"
#include "numerics/elliptic/CartesianPoisson.h"

namespace arch::elliptic {
// Shared cancellation-safe face derivative, used by scalar tests and both
// production execution providers. Coefficients are built once on the Host.
/** Apply a face stencil without cancelling prescribed flux against a large potential.
 *  A flux boundary evaluates g=sum_i w_i*(Phi_i-Phi_A)+beta*c+w_A*Phi_A,
 *  with w_A=-alpha*qB. Keeping beta*c separate preserves a small Neumann datum
 *  even when Phi_A is too large to resolve c-Phi_A in double precision.
 *  Zero coefficients do not read the field: a pure Neumann derivative depends
 *  only on c/b, including when differences of finite potentials would overflow.
 *  Dirichlet/interior/periodic rows retain their original evaluation order. */
ARCH_INLINE double composite_face_gradient(const double* x,int anchor,const int* samples,
    const double* coefficients,int count,double boundary_coefficient,double boundary_value,
    double anchor_coefficient=0.,bool flux_boundary=false) {
    arch::math::CompensatedSum sum;
    if(flux_boundary) {
        for(int k=0;k<count;++k) if(coefficients[k]!=0.)
            sum.add(coefficients[k]*(x[samples[k]]-x[anchor]));
        sum.add(boundary_coefficient*boundary_value);
        if(anchor_coefficient!=0.)sum.add(anchor_coefficient*x[anchor]);
        return sum.value();
    }
    for(int k=0;k<count;++k)sum.add(coefficients[k]*(x[samples[k]]-x[anchor]));
    sum.add(boundary_coefficient*(boundary_value-x[anchor]));
    if(anchor_coefficient!=0.)sum.add(anchor_coefficient*x[anchor]);
    return sum.value();
}
enum class FaceBoundaryKind { Periodic, Dirichlet, Neumann, Robin };
/** One side policy for a*Phi+b*dPhi/dn=c: Dirichlet a=1,b=0; Neumann a=0,b=1;
 *  Robin finite a>=0,b>0. Coefficients are constant per domain side. */
struct FaceBoundaryCondition {
    FaceBoundaryKind kind = FaceBoundaryKind::Neumann;
    double a = 0., b = 1.;
};
struct CompositeBoundary {
    std::array<FaceBoundaryKind,6> sides{};
    bool constant_nullspace=false;
    std::array<FaceBoundaryCondition,6> conditions{};
};
struct CompositeCell {
    int level = 0;
    std::array<int,3> index{}; // Cell coordinate at this refinement level.
    friend bool operator==(const CompositeCell&, const CompositeCell&) = default;
};
struct CompositeCellHash {
    std::size_t operator()(const CompositeCell& c) const noexcept;
};
struct CompositeFace {
    int left = 0, right = 0, axis = 0;
    double area = 0.;
    std::array<double,3> fragment_width{}; // Tangential subface widths in native coordinates.
    int boundary_side = -1; // Interior: -1; physical face: 2*axis+side.
    double boundary_coefficient = 0.;
    double anchor_coefficient = 0.; // Explicit anchor weight of the eliminated row.
    std::array<double,3> center{};
    std::vector<int> samples;
    std::vector<double> coefficients; // Normal derivative, increasing coordinate.
    std::vector<int> value_samples;    // Radial face-potential interpolation.
    std::vector<double> value_coefficients;
    double value_boundary_coefficient = 0.;
};
class CompositePoisson {
public:
    CompositePoisson(CartesianMesh base, std::vector<CompositeCell> cells,
                     BoundaryKind kind = BoundaryKind::Periodic);
    CompositePoisson(CartesianMesh base, std::vector<CompositeCell> cells,
                     CompositeBoundary boundary);
    BoundaryKind boundary_kind() const { return kind_; }
    const CompositeBoundary& boundary() const { return boundary_; }
    bool has_constant_nullspace() const { return boundary_.constant_nullspace; }
    bool periodic_boundary() const { return periodic_only_; }
    /** FP64 roundoff bound accepted by the volume-weighted compatibility test. */
    static constexpr double compatibility_roundoff = 64.*std::numeric_limits<double>::epsilon();
    /** Reject a source whose volume-weighted mean violates a pure Neumann/Robin
     *  compatibility condition; periodic sources keep their legacy projection. */
    void validate_compatibility(std::span<const double> rhs) const;
    const CartesianMesh& base() const { return base_; }
    const auto& cells() const { return cells_; }
    const auto& volumes() const { return volumes_; }
    const auto& diagonal() const { return diagonal_; }
    const auto& faces() const { return faces_; }
    /** Select direct boundary-data evaluation from the existing side policy. */
    bool has_flux_boundary(const CompositeFace& face) const {
        if(face.boundary_side<0)return false;
        const auto kind=boundary_.conditions[face.boundary_side].kind;
        return kind==FaceBoundaryKind::Neumann || kind==FaceBoundaryKind::Robin;
    }
    int size() const { return static_cast<int>(cells_.size()); }
    int max_level() const { return max_level_; }
    double width(int cell, int axis) const;
    std::array<double,3> center(int cell) const;
    // Locate a leaf using coordinates in units of root cells, with periodic wrap.
    int locate(std::array<double,3> point) const;
    void apply(std::span<const double> x, std::span<double> out) const;
    double face_gradient(std::span<const double> x, const CompositeFace& face,
                         double boundary_value = 0.) const;
    std::vector<double> effective_rhs(std::span<const double> source,
                                     std::span<const double> boundary_values) const;
    double mean(std::span<const double> x) const;
    double norm(std::span<const double> x) const;
    double dot(std::span<const double> x, std::span<const double> y) const;
    void project(std::span<double> x) const;
private:
    CartesianMesh base_;
    BoundaryKind kind_;
    CompositeBoundary boundary_;
    bool radial_ = false, periodic_only_ = false;
    std::vector<CompositeCell> cells_;
    std::unordered_map<CompositeCell,int,CompositeCellHash> lookup_;
    std::vector<double> volumes_, diagonal_, weights_;
    std::vector<CompositeFace> faces_;
    std::vector<std::vector<int>> neighbors_;
    int max_level_ = 0;
    CompositePoisson(CartesianMesh base, std::vector<CompositeCell> cells,
                     BoundaryKind kind, CompositeBoundary boundary);
    void prepare_boundary();
    void eliminate_boundary(CompositeFace& face, const double* fitted_coefficients,
                            double fitted_qB) const;
    void fit_boundary_face_value(CompositeFace& face, const double* fitted_coefficients,
                                 double fitted_qB) const;
    void build_faces();
    void fit_interface(CompositeFace& face) const;
    void fit_curved_face_value(CompositeFace& face) const;
    double face_area(const CompositeFace& face) const;
    double face_metric(const CompositeFace& face,int axis) const;
};
} // namespace arch::elliptic
