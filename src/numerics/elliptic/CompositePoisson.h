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
#include <span>
#include <limits>
#include <unordered_map>
#include <vector>

#include "core/CompensatedSum.h"
#include "numerics/elliptic/CartesianPoisson.h"

namespace arch::multigrid { class CompositeMultigrid; }

namespace arch::elliptic {
// Shared cancellation-safe face derivative, used by scalar tests and both
// production execution providers. Coefficients are built once on the Host.
/** Apply a cancellation-safe face stencil to potential differences and boundary data. */
ARCH_INLINE double composite_face_gradient(const double* x,int anchor,const int* samples,
    const double* coefficients,int count,double boundary_coefficient,double boundary_value) {
    arch::math::CompensatedSum sum;
    for(int k=0;k<count;++k)sum.add(coefficients[k]*(x[samples[k]]-x[anchor]));
    sum.add(boundary_coefficient*(boundary_value-x[anchor]));
    return sum.value();
}
enum class FaceBoundaryKind { Periodic, Dirichlet, Neumann };
struct CompositeBoundary {
    std::array<FaceBoundaryKind,6> sides{};
    bool constant_nullspace=false;
};
struct CompositeCell {
    int level = 0;
    std::array<int,3> index{}; // Cell coordinate at this refinement level.
    friend bool operator==(const CompositeCell&, const CompositeCell&) = default;
};
struct CompositeCellHash {
    std::size_t operator()(const CompositeCell& c) const noexcept;
};
// Records the actual final construction path; never inferred from coefficient values.
enum class FaceStencilConstruction { TwoPoint, PolynomialFit, EllipticRecovery };
struct CompositeFace {
    int left = 0, right = 0, axis = 0;
    FaceStencilConstruction construction=FaceStencilConstruction::TwoPoint;
    double area = 0.;
    std::array<double,3> fragment_width{}; // Tangential subface widths in native coordinates.
    int boundary_side = -1; // Interior: -1; physical face: 2*axis+side.
    double boundary_coefficient = 0.;
    std::array<double,3> center{};
    std::vector<int> samples;
    std::vector<double> coefficients; // Normal derivative, increasing coordinate.
    std::vector<int> value_samples;    // Radial face-potential interpolation.
    std::vector<double> value_coefficients;
    double value_boundary_coefficient = 0.;
};
enum class BoundaryErrorQuality { CertifiedAbsolute, Estimate, Unknown };
struct BoundaryPotentialError {
    double absolute_error=0.; // Potential units, e.g. cm^2/s^2.
    BoundaryErrorQuality quality=BoundaryErrorQuality::Unknown;
};
enum class BoundaryErrorStatus { Bounded, InvalidInput, UncertifiedInput, Overflow };
struct BoundaryRhsError {
    BoundaryErrorStatus status=BoundaryErrorStatus::InvalidInput;
    std::vector<double> cell_bounds; // RHS units, e.g. s^-2.
    double norm_upper=std::numeric_limits<double>::infinity();
};
struct WeightedNormInterval {
    BoundaryErrorStatus status=BoundaryErrorStatus::InvalidInput;
    double lower=0.,upper=std::numeric_limits<double>::infinity();
};
// Companion evaluation ledger for the exact mathematical operator defined by
// stored native coefficients/volumes/weights. Geometry and source construction
// remain separate mandatory certificates before full physical acceptance.
enum class PoissonArithmeticScope { StoredNativeCoefficients };
struct PoissonArithmeticError {
    BoundaryErrorStatus status=BoundaryErrorStatus::InvalidInput;
    PoissonArithmeticScope scope=PoissonArithmeticScope::StoredNativeCoefficients;
    std::vector<double> cell_bounds;
    double norm_upper=std::numeric_limits<double>::infinity();
};
/** Exact full-ring measures from the dyadic mesh identity, not stored weights.
 * Transient proof data only; this does not certify face-fit/stencil construction.
 */
struct NativeRzMeasureEnclosure {
    BoundaryErrorStatus status=BoundaryErrorStatus::InvalidInput;
    std::vector<double> volume_lower,volume_upper,volume_error_upper;
    std::vector<double> weight_lower,weight_upper,weight_error_upper;
    double total_volume_lower=0.,total_volume_upper=0.;
};
enum class BoundaryResidualStatus {
    Accepted, ResidualTooLarge, InvalidInput, UncertifiedInput, Overflow
};
struct BoundaryResidualAssessment {
    BoundaryResidualStatus status=BoundaryResidualStatus::InvalidInput;
    double tolerance_safe=0.;
    double rhs_norm_lower=0.,rhs_norm_upper=std::numeric_limits<double>::infinity();
    double residual_norm_upper=std::numeric_limits<double>::infinity();
    double rhs_error_upper=std::numeric_limits<double>::infinity();
    double total_residual_upper=std::numeric_limits<double>::infinity();
};
class CompositePoisson {
public:
    CompositePoisson(CartesianMesh base, std::vector<CompositeCell> cells,
                     BoundaryKind kind = BoundaryKind::Periodic);
    BoundaryKind boundary_kind() const { return kind_; }
    const CompositeBoundary& boundary() const { return boundary_; }
    bool has_constant_nullspace() const { return boundary_.constant_nullspace; }
    const CartesianMesh& base() const { return base_; }
    const auto& cells() const { return cells_; }
    const auto& volumes() const { return volumes_; }
    const auto& norm_weights() const { return weights_; }
    const auto& diagonal() const { return diagonal_; }
    const auto& faces() const { return faces_; }
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
    // Bound |B e_face| with the same stored native face coefficients.
    // Requires certified potential inputs; does not certify their origin,
    // geometry construction or source/RHS assembly rounding.
    BoundaryRhsError propagate_boundary_error(
        std::span<const BoundaryPotentialError> face_errors) const;
    WeightedNormInterval norm_interval(std::span<const double> x) const;
    NativeRzMeasureEnclosure native_rz_measure_enclosure() const;
    WeightedNormInterval native_rz_norm_interval(std::span<const double> x) const;
    PoissonArithmeticError bound_rhs_assembly_roundoff(
        std::span<const double> source,std::span<const double> boundary_values,
        std::span<const double> computed_rhs) const;
    PoissonArithmeticError bound_residual_evaluation_roundoff(
        std::span<const double> potential,std::span<const double> approximate_rhs,
        std::span<const double> computed_residual) const;

    // Mandatory external arithmetic ledgers: no default zero/fake certificate.
    BoundaryResidualAssessment assess_boundary_residual(
        std::span<const double> approximate_rhs,
        std::span<const double> computed_residual,
        const BoundaryRhsError& face_error,
        double rhs_assembly_error_upper,double residual_evaluation_error_upper,
        BoundaryErrorQuality evaluation_quality,double rtol,double atol) const;
    // Compare the actual projection with P_w*x=x-sum(stored_weight*x).
    // Does not certify weight construction or physical source preprocessing.
    PoissonArithmeticError bound_constant_mode_projection_roundoff(
        std::span<const double> input,std::span<const double> computed) const;
    double mean(std::span<const double> x) const;
    double norm(std::span<const double> x) const;
    double dot(std::span<const double> x, std::span<const double> y) const;
    void project(std::span<double> x) const;
private:
    friend class arch::multigrid::CompositeMultigrid;
    // Only the hierarchy owner can construct a derived operator from a real
    // fine operator. No public flag or AMR leaf level grants this provenance.
    CompositePoisson(CartesianMesh base, std::vector<CompositeCell> cells,
                     BoundaryKind kind, const CompositePoisson* fine);
    CartesianMesh base_;
    BoundaryKind kind_;
    CompositeBoundary boundary_;
    std::vector<CompositeCell> cells_;
    std::unordered_map<CompositeCell,int,CompositeCellHash> lookup_;
    std::vector<double> volumes_, diagonal_, weights_;
    std::vector<CompositeFace> faces_;
    std::vector<std::vector<int>> neighbors_;
    int max_level_ = 0;
    void build_faces();
    void fit_interface(CompositeFace& face) const;
    void fit_curved_face_value(CompositeFace& face) const;
    double face_area(const CompositeFace& face) const;
    double face_metric(const CompositeFace& face,int axis) const;
};
} // namespace arch::elliptic
