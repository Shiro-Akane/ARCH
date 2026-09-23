/**
 * @file UniformGravity.cpp
 * @brief Adapt the reusable uniform Poisson solver to standalone gravity requests.
 *
 * Workflow:
 * 1. Receive active density with mesh and generation identity.
 * 2. Adapt the reusable uniform Poisson solver to standalone gravity requests.
 * 3. Publish a checked potential/acceleration field for the requested stage.
 */

#include <cmath>
#include <limits>
#include <stdexcept>

#include "physics/gravity/UniformGravity.h"

namespace arch::gravity {
namespace {
/** Validate and gather a padded host density view into contiguous Cartesian order. */
std::vector<double> read_density(const elliptic::CartesianMesh& mesh, grid::ConstScalarFieldView view)
{
    const auto& l = view.layout;
    if (view.memory != grid::FieldMemory::Host || l.centering != grid::FieldCentering::Cell ||
        l.dimension != mesh.dimension || view.data == nullptr || view.size == 0)
        throw std::invalid_argument("Uniform gravity requires a Host cell density view");
    std::size_t last = 0, span = 1;
    for (int a = 0; a < 3; ++a) {
        if (l.active_end[a] <= l.active_begin[a] || l.active_end[a] > l.extent[a] ||
            l.active_end[a]-l.active_begin[a] != static_cast<std::size_t>(mesh.cells[a]))
            throw std::invalid_argument("Uniform gravity active density box does not match mesh");
        if (l.extent[a] > 1) {
            if (l.stride[a] < span || l.stride[a] > (view.size-1) / (l.extent[a]-1))
                throw std::invalid_argument("Uniform gravity requires nonoverlapping x-fast strides");
            const auto additional = l.stride[a] * (l.extent[a]-1);
            if (additional > view.size-span)
                throw std::invalid_argument("Uniform gravity padded layout exceeds storage");
            span += additional;
        }
        const auto end = l.active_end[a]-1;
        if (end != 0 && l.stride[a] > (view.size-1-last) / end)
            throw std::invalid_argument("Uniform gravity density view exceeds storage");
        last += end * l.stride[a];
    }
    std::vector<double> density(mesh.size());
    for (int i = 0; i < mesh.size(); ++i) {
        const auto p = mesh.position(i);
        std::size_t offset = 0;
        for (int a = 0; a < 3; ++a) offset += (l.active_begin[a] + p[a]) * l.stride[a];
        density[i] = view.data[offset];
        if (!std::isfinite(density[i]) || density[i] < 0.)
            throw std::invalid_argument("Uniform gravity density must be finite and nonnegative");
    }
    return density;
}
}

/** Build the physical Poisson source, solve, then derive checked face/cell acceleration. */
UniformGravityResult solve_uniform_gravity(multigrid::HostMultigrid& solver,
    grid::ConstScalarFieldView view, const elliptic::BoundaryData& boundary,
    multigrid::SolveControl control, double gravitational_constant, std::span<const double> initial)
{
    if (!std::isfinite(gravitational_constant) || gravitational_constant <= 0.)
        throw std::invalid_argument("Uniform gravity requires finite positive G");
    const auto& mesh = solver.mesh();
    auto rhs = read_density(mesh, view);
    UniformGravityResult result;
    if (boundary.kind == elliptic::BoundaryKind::Periodic)
        result.removed_density_mean = elliptic::mean(rhs);
    // A=-Laplacian: A*Phi=-4*pi*G*(rho-<rho>) for periodic domains.
    const double coefficient = -4. * arch::constants::math::pi * gravitational_constant;
    if (!std::isfinite(coefficient)) return result;
    for (double& value : rhs) {
        const double source = value - result.removed_density_mean;
        value = coefficient * source;
        if (!std::isfinite(value) || (source != 0. && value == 0.)) return result;
    }
    // A double cannot always represent the mean of the original densities.
    // When the contrast is tiny, its subtraction error can dominate the mean
    // relative to the fluctuation. The physical periodic problem explicitly
    // removes this constant mode; the generic solver must still reject an
    // incompatible caller-supplied RHS. Account for both projections below.
    double source_mean_roundoff = 0.;
    if (boundary.kind == elliptic::BoundaryKind::Periodic) {
        source_mean_roundoff = elliptic::mean(rhs);
        elliptic::project_mean(rhs);
    }
    auto solution = solver.solve(rhs, boundary, control, initial);
    result.report = solution.report;
    result.report.removed_rhs_mean += source_mean_roundoff;
    if (result.report.status != multigrid::SolveStatus::Converged) return result;
    // Keep all fields local until every derivative has passed finite checks.
    std::array<std::vector<double>,3> faces, cells;
    for (int a = 0; a < mesh.dimension; ++a) {
        auto face_mesh = mesh;
        ++face_mesh.cells[a];
        faces[a].resize(face_mesh.size());
        cells[a].resize(mesh.size());
        for (int i = 0; i < face_mesh.size(); ++i) {
            const auto p = face_mesh.position(i);
            const int side = p[a] == 0 ? 0 : (p[a] == mesh.cells[a] ? 1 : -1);
            const double value = boundary.kind == elliptic::BoundaryKind::Dirichlet && side >= 0
                ? boundary.values[2*a+side][mesh.face_index(p,a)] : 0.;
            // g_face = -d(Phi)/dx on the oriented face.
            faces[a][i] = -elliptic::face_gradient(mesh, boundary.kind, solution.potential.data(), a, p, value);
            if (!std::isfinite(faces[a][i])) {
                result.report.status = multigrid::SolveStatus::NumericalFailure;
                return result;
            }
        }
        for (int i = 0; i < mesh.size(); ++i) {
            const auto p = mesh.position(i);
            auto high = p;
            ++high[a];
            cells[a][i] = 0.5*faces[a][face_mesh.index(p)] + 0.5*faces[a][face_mesh.index(high)];
        }
    }
    result.potential = std::move(solution.potential);
    result.face_acceleration = std::move(faces);
    result.cell_acceleration = std::move(cells);
    return result;
}
} // namespace arch::gravity
