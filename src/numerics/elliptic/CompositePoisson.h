/** @file CompositePoisson.h
 * Periodic cell-centered finite-volume operator on a dyadic Cartesian leaf mesh.
 * The owner contains geometry and local face stencils, never fluid/AMR storage.
 */
#pragma once
#include "numerics/elliptic/CartesianPoisson.h"
#include "core/CompensatedSum.h"
#include <array>
#include <span>
#include <unordered_map>
#include <vector>

namespace arch::elliptic {
// Shared cancellation-safe face derivative, used by scalar tests and both
// production execution providers. Coefficients are built once on the Host.
ARCH_INLINE double composite_face_gradient(const double* x,int anchor,const int* samples,
    const double* coefficients,int count,double boundary_coefficient,double boundary_value) {
    arch::math::CompensatedSum sum;
    for(int k=0;k<count;++k)sum.add(coefficients[k]*(x[samples[k]]-x[anchor]));
    sum.add(boundary_coefficient*(boundary_value-x[anchor]));
    return sum.value();
}
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
    int boundary_side = -1; // Interior: -1; physical face: 2*axis+side.
    double boundary_coefficient = 0.;
    std::array<double,3> center{};
    std::vector<int> samples;
    std::vector<double> coefficients; // Normal derivative, increasing coordinate.
};
class CompositePoisson {
public:
    CompositePoisson(CartesianMesh base, std::vector<CompositeCell> cells,
                     BoundaryKind kind = BoundaryKind::Periodic);
    BoundaryKind boundary_kind() const { return kind_; }
    const CartesianMesh& base() const { return base_; }
    const auto& cells() const { return cells_; }
    const auto& volumes() const { return volumes_; }
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
    double mean(std::span<const double> x) const;
    double norm(std::span<const double> x) const;
    double dot(std::span<const double> x, std::span<const double> y) const;
    void project(std::span<double> x) const;
private:
    CartesianMesh base_;
    BoundaryKind kind_;
    std::vector<CompositeCell> cells_;
    std::unordered_map<CompositeCell,int,CompositeCellHash> lookup_;
    std::vector<double> volumes_, diagonal_, weights_;
    std::vector<CompositeFace> faces_;
    std::vector<std::vector<int>> neighbors_;
    int max_level_ = 0;
    void build_faces();
    void fit_interface(CompositeFace& face) const;
};
} // namespace arch::elliptic
