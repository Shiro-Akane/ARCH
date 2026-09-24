/**
 * @file CartesianPoisson.cpp
 * @brief Apply the uniform-grid finite-volume Poisson operator and boundary contributions.
 *
 * Workflow:
 * 1. Receive an explicit mesh/operator and signed cell-centered fields.
 * 2. Apply the uniform-grid finite-volume Poisson operator and boundary contributions.
 * 3. Return corrections or fluxes through the shared numerical contract.
 */

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "numerics/elliptic/CartesianPoisson.h"

#include "core/CompensatedSum.h"

namespace arch::elliptic {
/** Reject non-Cartesian mesh extents and spacing before building an operator. */
void validate_mesh(const CartesianMesh& m)
{
    if (m.dimension < 1 || m.dimension > 3) throw std::invalid_argument("Poisson dimension must be 1..3");
    std::size_t count = 1;
    double smallest = std::numeric_limits<double>::max(), largest = 0.;
    double diagonal_bound = 0.;
    for (int a = 0; a < 3; ++a) {
        const int n = m.cells[a];
        if (n <= 0 || (a < m.dimension ? (n < 2 || (n & (n-1)) != 0) : n != 1))
            throw std::invalid_argument("Poisson active extents must be powers of two >= 2; inactive extents must be 1");
        if (count > static_cast<std::size_t>(std::numeric_limits<int>::max()) / n)
            throw std::invalid_argument("Poisson indexing capacity exceeded");
        count *= n;
        if (!std::isfinite(m.origin[a]) || !std::isfinite(m.spacing[a]) || m.spacing[a] <= 0.)
            throw std::invalid_argument("Poisson geometry must be finite with positive spacing");
        if (a < m.dimension) {
            const double coefficient = 4. / m.spacing[a] / m.spacing[a];
            if (!std::isfinite(coefficient) || coefficient <= 0. ||
                !std::isfinite(m.origin[a] + n * m.spacing[a]))
                throw std::invalid_argument("Poisson geometry exceeds arithmetic range");
            diagonal_bound += coefficient;
            smallest = std::min(smallest, m.spacing[a]);
            largest = std::max(largest, m.spacing[a]);
        }
    }
    if (!std::isfinite(diagonal_bound) ||
        (m.geometry == Geometry::Cartesian && largest / smallest > 2.))
        throw std::invalid_argument("Poisson prototype requires valid spacing and finite diagonal");
    if (m.geometry != Geometry::Cartesian) {
        if (m.origin[0] < 0.)
            throw std::invalid_argument("Curvilinear gravity requires nonnegative radius");
        if (m.dimension == 2) {
            const double turn = 2. * std::acos(-1.);
            if (std::abs(m.cells[1]*m.spacing[1]-turn) > 64.*std::numeric_limits<double>::epsilon()*turn)
                throw std::invalid_argument("Polar gravity requires a full azimuthal turn");
        }
        if (m.dimension == 3) {
            const int azimuth = 2;
            const double turn = 2. * std::acos(-1.);
            if (std::abs(m.cells[azimuth]*m.spacing[azimuth]-turn) > 64.*std::numeric_limits<double>::epsilon()*turn)
                throw std::invalid_argument("Curvilinear gravity requires a full azimuthal turn");
            if (m.geometry == Geometry::Spherical &&
                (m.origin[1] < 0. || m.origin[1]+m.cells[1]*m.spacing[1] > std::acos(-1.)))
                throw std::invalid_argument("Spherical polar angle must stay in [0, pi]");
        }
    }
}

/** Check the borrowed vector extent and finiteness before arithmetic. */
void validate_values(std::span<const double> v, std::size_t size)
{
    if (v.size() != size || (size != 0 && v.data() == nullptr))
        throw std::invalid_argument("Poisson array extent mismatch");
    for (double x : v) if (!std::isfinite(x)) throw std::invalid_argument("Poisson input contains nonfinite values");
}

/** Check the face-data shape and finite Dirichlet values. */
void validate_boundary(const CartesianMesh& m, const BoundaryData& b)
{
    if (b.kind != BoundaryKind::Periodic && b.kind != BoundaryKind::Dirichlet)
        throw std::invalid_argument("Unsupported Poisson boundary kind");
    for (int a = 0; a < 3; ++a) for (int s = 0; s < 2; ++s)
        validate_values(b.values[2*a+s], b.kind == BoundaryKind::Dirichlet && a < m.dimension
            ? m.size() / m.cells[a] : 0);
}

namespace {
/** Scale a vector before RMS calculations to avoid overflow. */
double magnitude(std::span<const double> v)
{
    double scale = 0.;
    for (double x : v) {
        if (!std::isfinite(x)) return std::numeric_limits<double>::infinity();
        scale = std::max(scale, std::abs(x));
    }
    return scale;
}
}
/** Return sqrt(sum(v_i^2)/N) with overflow-safe scaling. */
double rms(std::span<const double> v)
{
    const double scale = magnitude(v);
    if (scale == 0. || !std::isfinite(scale)) return scale;
    arch::math::CompensatedSum sum;
    for (double x : v) { const double q = x / scale; sum.add(q*q); }
    return scale * std::sqrt(sum.value() / static_cast<double>(v.size()));
}
/** Return the compensated arithmetic mean of active cells. */
double mean(std::span<const double> v)
{
    const double scale = magnitude(v);
    if (scale == 0. || !std::isfinite(scale)) return scale;
    arch::math::CompensatedSum sum;
    for (double x : v) sum.add(x / scale);
    return scale * (sum.value() / static_cast<double>(v.size()));
}
/** Remove the constant null mode from a periodic field. */
void project_mean(std::span<double> v)
{
    const double average = mean(v);
    for (double& x : v) x -= average;
}

/** Move inhomogeneous Dirichlet face terms to the Poisson RHS. */
std::vector<double> effective_rhs(const CartesianMesh& m, const BoundaryData& b,
                                 std::span<const double> rhs)
{
    validate_mesh(m);
    validate_boundary(m, b);
    validate_values(rhs, m.size());
    std::vector<double> result(rhs.begin(), rhs.end());
    if (b.kind == BoundaryKind::Dirichlet) {
        for (int i = 0; i < m.size(); ++i) {
            const auto p = m.position(i);
            for (int a = 0; a < m.dimension; ++a) {
                const int side = p[a] == 0 ? 0 : (p[a] == m.cells[a]-1 ? 1 : -1);
                if (side >= 0) result[i] += (8./3.) * b.values[2*a+side][m.face_index(p,a)]
                    / m.spacing[a] / m.spacing[a];
            }
        }
    }
    return result; // Arithmetic failure is reported by the solve, not clipped.
}
} // namespace arch::elliptic
