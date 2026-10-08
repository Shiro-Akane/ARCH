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
#include <limits>
#include <unordered_map>
#include <vector>

#include "core/CompensatedSum.h"
#include "numerics/elliptic/CartesianPoisson.h"

namespace arch::multigrid { class CompositeMultigrid; }

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
// Records the actual final construction path; never inferred from coefficient values.
enum class FaceStencilConstruction { TwoPoint, PolynomialFit, EllipticRecovery };
struct CompositeFace {
    int left = 0, right = 0, axis = 0;
    FaceStencilConstruction construction=FaceStencilConstruction::TwoPoint;
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
    // Actual native face-fragment endpoints. The normal endpoints coincide;
    // tangential bounds retain their original bits, never center +/- width/2.
    bool native_bounds = false;
    std::array<double,3> fragment_lower{};
    std::array<double,3> fragment_upper{};
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
/** Proof of the ideal root-dyadic final RZ stencil; not a Phi/force certificate.
 * The construction enum chooses the actual fitted or recovered policy.
 */
struct NativeRzStencilEnclosure {
    BoundaryErrorStatus status=BoundaryErrorStatus::InvalidInput;
    FaceStencilConstruction construction=FaceStencilConstruction::TwoPoint;
    std::size_t face_index=0;
    std::vector<double> coefficient_lower,coefficient_upper,coefficient_error_upper;
    double boundary_lower=0.,boundary_upper=0.,boundary_error_upper=0.;
    // Final eliminated anchor k: flux rows store k=-alpha*q separately from beta.
    double anchor_lower=0.,anchor_upper=0.,anchor_error_upper=0.;
    double inverse_residual_upper=0.,inverse_norm_upper=0.,lambda_error_upper=0.;
};
/** Ideal root-dyadic face geometry and signed native boundary-map factors.
 * Side arrays correspond to left/right cells; absent/interior boundary terms
 * are exact zero. Source/observer potential differences remain separate.
 */
struct NativeRzFaceEnclosure {
    BoundaryErrorStatus status=BoundaryErrorStatus::InvalidInput;
    std::size_t face_index=0;
    std::array<double,2> center_lower{},center_upper{},center_error_upper{};
    double area_lower=0.,area_upper=0.,area_error_upper=0.;
    std::array<double,2> area_over_volume_lower{},area_over_volume_upper{},area_over_volume_error_upper{};
    std::array<double,2> boundary_map_lower{},boundary_map_upper{},boundary_map_error_upper{};
};
/** B construction error for supplied stored face values, in ideal native RMS.
 * Does not certify those values at ideal source/observer coordinates.
 */
struct NativeRzBoundaryConstructionError {
    BoundaryErrorStatus status=BoundaryErrorStatus::InvalidInput;
    std::vector<double> cell_bounds;
    double native_norm_upper=std::numeric_limits<double>::infinity();
};
// Distinct input prevents accidentally lifting stored-coordinate ring errors
// into ideal-source/observer errors. Root scope must be supplied explicitly.
enum class NativeRzPotentialScope { Unknown, RootDyadicSourceAndObserver };
struct NativeRzFacePotentialError {
    BoundaryPotentialError error;
    NativeRzPotentialScope scope=NativeRzPotentialScope::Unknown;
};
/** Uniform potential-error sensitivity of the ideal native boundary map.
 * Units are inverse length squared; this is geometry, not a source certificate.
 */
struct NativeRzBoundarySensitivity {
    BoundaryErrorStatus status=BoundaryErrorStatus::InvalidInput;
    std::vector<double> cell_coefficients;
    double native_norm_upper=std::numeric_limits<double>::infinity();
};
struct NativeRzBoundaryPotentialError {
    BoundaryErrorStatus status=BoundaryErrorStatus::InvalidInput;
    std::vector<double> cell_bounds;
    double native_norm_upper=std::numeric_limits<double>::infinity();
};
/** Ideal final native A vs exact stored-coefficient A, for supplied phi.
 * Does not certify the continuous PDE or a RHS source/potential producer.
 */
struct NativeRzOperatorConstructionError {
    BoundaryErrorStatus status=BoundaryErrorStatus::InvalidInput;
    std::vector<double> cell_bounds;
    double native_norm_upper=std::numeric_limits<double>::infinity();
};
struct NativeRzResidualEvaluationError {
    BoundaryErrorStatus status=BoundaryErrorStatus::InvalidInput;
    NativeRzOperatorConstructionError construction;
    PoissonArithmeticError arithmetic;
    std::vector<double> cell_bounds;
    double native_norm_upper=std::numeric_limits<double>::infinity();
};
/** Complete native residual error, distinct from an RHS-only error ledger.
 * Source, boundary potential, assembly/apply arithmetic and the jointly
 * constructed A*phi-B*datum are included once by the authenticated producer.
 * This certifies a discrete residual; continuous physics stays separate.
 */
struct NativeRzCompleteResidualError {
    BoundaryErrorStatus status=BoundaryErrorStatus::InvalidInput;
    std::vector<double> cell_bounds;
    double native_norm_upper=std::numeric_limits<double>::infinity();
};
enum class ResidualErrorComposition { SeparateRhsAndOperator, CorrelatedPrescribedBoundary };
enum class BoundaryResidualNormScope { StoredNativeWeights, RootDyadicRzWeights };
enum class BoundaryResidualStatus {
    Accepted, ResidualTooLarge, InvalidInput, UncertifiedInput, Overflow
};
struct BoundaryResidualAssessment {
    BoundaryResidualStatus status=BoundaryResidualStatus::InvalidInput;
    ResidualErrorComposition error_composition=ResidualErrorComposition::SeparateRhsAndOperator;
    double complete_residual_error_upper=std::numeric_limits<double>::infinity();
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
    const auto& norm_weights() const { return weights_; }
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
    /** Actual leaf endpoints from the single native face authority. */
    double lower(int cell, int axis) const;
    double upper(int cell, int axis) const;
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
    NativeRzStencilEnclosure native_rz_stencil_enclosure(std::size_t face_index) const;
    NativeRzFaceEnclosure native_rz_face_enclosure(std::size_t face_index) const;
    NativeRzBoundaryConstructionError native_rz_boundary_construction_error(
        std::span<const double> boundary_values) const;
    NativeRzBoundarySensitivity native_rz_boundary_sensitivity() const;
    NativeRzBoundaryPotentialError native_rz_propagate_potential_error(
        std::span<const NativeRzFacePotentialError> face_errors) const;
    NativeRzOperatorConstructionError native_rz_operator_construction_error(
        std::span<const double> potential) const;
    NativeRzResidualEvaluationError native_rz_residual_evaluation_error(
        std::span<const double> potential,std::span<const double> computed_rhs,
        std::span<const double> computed_residual) const;
    // Joint construction of A*phi-B*datum keeps the SAME boundary coefficient
    // correlated; the original homogeneous construction API remains unchanged.
    NativeRzOperatorConstructionError native_rz_prescribed_residual_construction_error(
        std::span<const double> potential,std::span<const double> boundary_values) const;
    BoundaryResidualAssessment assess_native_rz_correlated_residual(
        std::span<const double> approximate_rhs,std::span<const double> computed_residual,
        const BoundaryRhsError& rhs_error,const NativeRzCompleteResidualError& complete_error,
        double rtol,double atol) const;
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
        BoundaryErrorQuality evaluation_quality,double rtol,double atol,
        BoundaryResidualNormScope norm_scope=BoundaryResidualNormScope::StoredNativeWeights) const;
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
    /** Build owned geometry-only proofs after all final face recovery decisions.
     * Density, potential, boundary values and field generations are not cached.
     */
    void prepare_native_rz_geometry_cache();
    /** Original outward measure construction, also used by uncached MG levels. */
    NativeRzMeasureEnclosure compute_native_rz_measure_enclosure() const;
    /** Original final-sample stencil proof, without changing its fit or status. */
    NativeRzStencilEnclosure compute_native_rz_stencil_enclosure(std::size_t face_index) const;
    /** Original face geometry proof; reuse completed stencil records if present. */
    NativeRzFaceEnclosure compute_native_rz_face_enclosure(std::size_t face_index) const;
    NativeRzOperatorConstructionError native_rz_operator_construction_error_impl(
        std::span<const double> potential,std::span<const double> boundary_values) const;
    // Only the hierarchy owner can construct a derived operator from a real
    // fine operator. No public flag or AMR leaf level grants this provenance.
    CompositePoisson(CartesianMesh base, std::vector<CompositeCell> cells,
                     BoundaryKind kind, const CompositePoisson* fine);
    CompositePoisson(CartesianMesh base, std::vector<CompositeCell> cells,
                     CompositeBoundary boundary, const CompositePoisson* fine);
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
    // Owned immutable payloads move/copy with the same final operator geometry.
    // Derived MG levels keep these vectors empty and use the original routines.
    NativeRzMeasureEnclosure native_rz_measure_cache_;
    std::vector<NativeRzStencilEnclosure> native_rz_stencil_cache_;
    std::vector<NativeRzFaceEnclosure> native_rz_face_cache_;
    bool native_rz_stencil_cache_ready_ = false;
    bool native_rz_geometry_cache_ready_ = false;
    CompositePoisson(CartesianMesh base, std::vector<CompositeCell> cells,
                     BoundaryKind kind, CompositeBoundary boundary, const CompositePoisson* fine);
    void prepare_boundary();
    void eliminate_boundary(CompositeFace& face, const double* fitted_coefficients,
                            double fitted_qB) const;
    void fit_boundary_face_value(CompositeFace& face, const double* fitted_coefficients,
                                 double fitted_qB) const;
    void build_faces();
    void fit_interface(CompositeFace& face) const;
    void fit_curved_face_value(CompositeFace& face) const;
    /** Bind exact normal/tangential endpoints before forming a native area. */
    void bind_native_face_bounds(CompositeFace& face, int tangent_owner,
                                 double normal_coordinate) const;
    double face_area(const CompositeFace& face) const;
    double face_metric(const CompositeFace& face,int axis) const;
};
} // namespace arch::elliptic
