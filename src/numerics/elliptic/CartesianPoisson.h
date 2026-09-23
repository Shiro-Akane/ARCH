/** @file CartesianPoisson.h
 * Uniform cell-centered -Laplacian and compatible face gradient.
 * Math leaves borrow validated contiguous arrays; no allocation or backend state.
 * Workflow:
 * 1. Receive an explicit mesh/operator and signed cell-centered fields.
 * 2. Define reusable Cartesian cell and boundary contracts for elliptic solvers.
 * 3. Return corrections or fluxes through the shared numerical contract.
 */

#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <vector>

#include "core/ArchPortability.h"
#include "numerics/elliptic/EllipticMesh.h"

namespace arch::elliptic {
enum class BoundaryKind { Periodic, Dirichlet, RadialIsolated };

struct BoundaryData {
    BoundaryKind kind = BoundaryKind::Periodic;
    std::array<std::vector<double>, 6> values; // 2*axis + side; empty for periodic.
};

void validate_mesh(const CartesianMesh& mesh);
void validate_boundary(const CartesianMesh& mesh, const BoundaryData& boundary);
void validate_values(std::span<const double> values, std::size_t size);
double rms(std::span<const double> values);
double mean(std::span<const double> values);
void project_mean(std::span<double> values);
std::vector<double> effective_rhs(const CartesianMesh& mesh, const BoundaryData& boundary,
                                  std::span<const double> rhs);

// Face coordinate is in [0,n] along axis, cell coordinates transversely.
// All face derivatives point toward increasing coordinate, including the low face.
/** Evaluate the positive-coordinate face derivative, including the one-sided Dirichlet stencil. */
ARCH_HOST_DEVICE inline double face_gradient(const CartesianMesh& m, BoundaryKind kind,
    const double* u, int axis, std::array<int, 3> face, double boundary_value = 0.)
{
    const int normal = face[axis], n = m.cells[axis];
    auto left = face, right = face;
    if (kind == BoundaryKind::Periodic || (normal > 0 && normal < n)) {
        left[axis] = normal == 0 ? n - 1 : normal - 1;
        right[axis] = normal == n ? 0 : normal;
        // d(phi)/dx at an interior face = (phi_R - phi_L) / dx.
        return (u[m.index(right)] - u[m.index(left)]) / m.spacing[axis];
    }
    if (normal == 0) {
        right[axis] = 1;
        const double first = u[m.index(face)];
        // Quadratic one-sided face derivative: (9 phi_0 - phi_1 - 8 B)/(3 dx).
        return (8. * (first - boundary_value) + (first - u[m.index(right)]))
            / (3. * m.spacing[axis]);
    }
    left[axis] = n - 1;
    right[axis] = n - 2;
    const double last = u[m.index(left)];
    // High face: (8 B - 9 phi_{N-1} + phi_{N-2})/(3 dx).
    return (8. * (boundary_value - last) + (u[m.index(right)] - last))
        / (3. * m.spacing[axis]);
}

/** Apply A = -div(grad) by differences of oriented face gradients. */
ARCH_HOST_DEVICE inline double apply_cell(const CartesianMesh& m, BoundaryKind kind,
                                          const double* u, int cell)
{
    const auto p = m.position(cell);
    double value = 0.;
    for (int a = 0; a < m.dimension; ++a) {
        auto high = p;
        ++high[a];
        // (A phi)_i = -sum_a [grad(phi)_{i+1/2}-grad(phi)_{i-1/2}]/dx_a.
        value += (face_gradient(m, kind, u, a, p) - face_gradient(m, kind, u, a, high))
            / m.spacing[a];
    }
    return value;
}

/** Return the positive Jacobi diagonal of A = -Laplacian. */
ARCH_HOST_DEVICE inline double diagonal(const CartesianMesh& m, BoundaryKind kind, int cell)
{
    const auto p = m.position(cell);
    double value = 0.;
    for (int a = 0; a < m.dimension; ++a) {
        const bool edge = kind == BoundaryKind::Dirichlet && (p[a] == 0 || p[a] == m.cells[a]-1);
        value += (edge ? 4. : 2.) / m.spacing[a] / m.spacing[a];
    }
    return value;
}
} // namespace arch::elliptic
