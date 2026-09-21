/** @file CartesianPoisson.h
 * Uniform cell-centered -Laplacian and compatible face gradient.
 * Math leaves borrow validated contiguous arrays; no allocation or backend state.
 */
#pragma once
#include "core/ArchPortability.h"
#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace arch::elliptic {
enum class BoundaryKind { Periodic, Dirichlet };

struct CartesianMesh {
    int dimension = 1;
    std::array<int, 3> cells{2, 1, 1};
    std::array<double, 3> spacing{1., 1., 1.};
    std::array<double, 3> origin{};
    ARCH_HOST_DEVICE int size() const { return cells[0] * cells[1] * cells[2]; }
    ARCH_HOST_DEVICE int index(const std::array<int, 3>& p) const {
        return p[0] + cells[0] * (p[1] + cells[1] * p[2]);
    }
    ARCH_HOST_DEVICE std::array<int, 3> position(int i) const {
        return {i % cells[0], (i / cells[0]) % cells[1], i / (cells[0] * cells[1])};
    }
    // Face vectors use the same x-fast ordering with the normal extent set to 1.
    ARCH_HOST_DEVICE int face_index(std::array<int, 3> p, int axis) const {
        p[axis] = 0;
        return p[0] + (axis == 0 ? 1 : cells[0]) *
            (p[1] + (axis == 1 ? 1 : cells[1]) * p[2]);
    }
};

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
ARCH_HOST_DEVICE inline double face_gradient(const CartesianMesh& m, BoundaryKind kind,
    const double* u, int axis, std::array<int, 3> face, double boundary_value = 0.)
{
    const int normal = face[axis], n = m.cells[axis];
    auto left = face, right = face;
    if (kind == BoundaryKind::Periodic || (normal > 0 && normal < n)) {
        left[axis] = normal == 0 ? n - 1 : normal - 1;
        right[axis] = normal == n ? 0 : normal;
        return (u[m.index(right)] - u[m.index(left)]) / m.spacing[axis];
    }
    if (normal == 0) {
        right[axis] = 1;
        const double first = u[m.index(face)];
        return (8. * (first - boundary_value) + (first - u[m.index(right)]))
            / (3. * m.spacing[axis]);
    }
    left[axis] = n - 1;
    right[axis] = n - 2;
    const double last = u[m.index(left)];
    return (8. * (boundary_value - last) + (u[m.index(right)] - last))
        / (3. * m.spacing[axis]);
}

ARCH_HOST_DEVICE inline double apply_cell(const CartesianMesh& m, BoundaryKind kind,
                                          const double* u, int cell)
{
    const auto p = m.position(cell);
    double value = 0.;
    for (int a = 0; a < m.dimension; ++a) {
        auto high = p;
        ++high[a];
        value += (face_gradient(m, kind, u, a, p) - face_gradient(m, kind, u, a, high))
            / m.spacing[a];
    }
    return value;
}

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
