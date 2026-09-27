/** @file MGTransfer.h
 * Signed scalar transfer between factor-two Cartesian levels.
 * Both leaves are allocation-free and shareable with a future device executor.
 * Workflow:
 * 1. Receive an explicit mesh/operator and signed cell-centered fields.
 * 2. Restrict and prolong signed cell-centered corrections between factor-two levels.
 * 3. Return corrections or fluxes through the shared numerical contract.
 */

#pragma once

#include "numerics/elliptic/CartesianPoisson.h"

namespace arch::multigrid {
/** Average 2^d signed fine residuals to one coarse cell. */
ARCH_HOST_DEVICE inline double restrict_cell(const elliptic::CartesianMesh& fine,
    const elliptic::CartesianMesh& coarse, const double* values, int cell)
{
    const auto p = coarse.position(cell);
    const int children = 1 << fine.dimension;
    double result = 0.;
    for (int child = 0; child < children; ++child) {
        auto q = p;
        for (int a = 0; a < fine.dimension; ++a) q[a] = 2*p[a] + ((child >> a) & 1);
        // Scale before summation, retaining the sign of every residual.
        // R(r)_P = 2^-d * sum_{q in children(P)} r_q.
        result += values[fine.index(q)] / children;
    }
    return result;
}

/** Tensor-interpolate coarse corrections with 3/4 and 1/4 weights per axis. */
ARCH_HOST_DEVICE inline double prolong_cell(const elliptic::CartesianMesh& fine,
    const elliptic::CartesianMesh& coarse, elliptic::BoundaryKind kind,
    const double* values, int cell)
{
    const auto p = fine.position(cell);
    double result = 0.;
    for (int corner = 0; corner < (1 << fine.dimension); ++corner) {
        std::array<int,3> q{};
        double weight = 1.;
        for (int a = 0; a < fine.dimension; ++a) {
            const bool neighbor = (corner >> a) & 1;
            q[a] = p[a]/2 + (neighbor ? (p[a] % 2 ? 1 : -1) : 0);
            // Linear tensor interpolation: 3/4 near + 1/4 adjacent per axis.
            weight *= neighbor ? 0.25 : 0.75;
            if (q[a] < 0 || q[a] >= coarse.cells[a]) {
                if (kind == elliptic::BoundaryKind::Periodic)
                    q[a] = q[a] < 0 ? coarse.cells[a]-1 : 0;
                else {
                    q[a] = q[a] < 0 ? 0 : coarse.cells[a]-1;
                    weight = -weight; // Homogeneous correction BC only.
                }
            }
        }
        result += weight * values[coarse.index(q)];
    }
    return result;
}
} // namespace arch::multigrid
