/**
 * @file VelocityDiagnostics.h
 * @brief Shared metric-aware divergence and vorticity diagnostics.
 *
 * Workflow:
 * 1. Convert conserved momentum to physical orthonormal velocity components.
 * 2. Evaluate divergence with finite-volume face measures from GridMetrics.
 * 3. Evaluate curl magnitude with the matching cylindrical or spherical metric terms.
 * 4. Reuse the same operator for AMR indicators and PLT diagnostics.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "grid/Grid.h"
#include "grid/GridMetrics.h"

namespace VelocityDiagnostics {

struct Values {
    double divergence = 0.0;
    double vorticity = 0.0;
};

/**
 * @brief Evaluates physical div(v) and |curl(v)| from orthonormal velocity components.
 *
 * Momentum storage follows the hydro source-term convention: VELX is radial for
 * curvilinear grids; VELY is azimuthal in spherical 2-D polar grids and polar
 * in spherical 3-D grids. Cylindrical RZ and 3-D grids use VELY=axial and
 * VELZ=azimuthal; spherical 3-D also uses VELZ=azimuthal. RZ has no phi derivative.
 * Logical-grid indices are materialized once because this routine is called in the
 * AMR and PLT inner loops; the component derivatives then reuse the same stencil.
 */
template<class GridView, class Component>
ARCH_INLINE Values evaluate(const GridView& grid, const Component& vel_x,
                       const Component& vel_y, const Component& vel_z,
                       int i, int j, int k)
{
    const int center = grid.GetIndex(i, j, k);
    const int x_minus = grid.GetIndex(i - 1, j, k);
    const int x_plus = grid.GetIndex(i + 1, j, k);
    const int y_minus = grid.dim >= 2 ? grid.GetIndex(i, j - 1, k) : center;
    const int y_plus = grid.dim >= 2 ? grid.GetIndex(i, j + 1, k) : center;
    const int z_minus = grid.dim == 3 ? grid.GetIndex(i, j, k - 1) : center;
    const int z_plus = grid.dim == 3 ? grid.GetIndex(i, j, k + 1) : center;

    const auto centered_difference = [](const Component& component,
                                        int low, int high, double spacing) {
        return (component[high] - component[low]) / (2.0 * spacing);
    };
    const auto ddx = [&](const Component& component) {
        return centered_difference(component, x_minus, x_plus, grid.dx1);
    };
    const auto ddy = [&](const Component& component) {
        return grid.dim >= 2 ? centered_difference(component, y_minus, y_plus, grid.dx2) : 0.0;
    };
    const auto ddz = [&](const Component& component) {
        return grid.dim == 3 ? centered_difference(component, z_minus, z_plus, grid.dx3) : 0.0;
    };
    const auto face_velocity = [&](const Component& component, int neighbour) {
        return 0.5 * (component[center] + component[neighbour]);
    };

    const double vel_x_center = vel_x[center];
    const double vel_y_center = vel_y[center];
    const double vel_z_center = vel_z[center];
    Values result;
    const double volume = GridMetrics::CellVolume(grid, i, j, k);
    double face_flux = 0.0;
    const auto add_face_flux = [&](int direction, const Component& component,
                                   int low, int high) {
        face_flux += GridMetrics::FaceArea(grid, direction, i, j, k, true) *
                         face_velocity(component, high) -
                     GridMetrics::FaceArea(grid, direction, i, j, k, false) *
                         face_velocity(component, low);
    };
    add_face_flux(0, vel_x, x_minus, x_plus);
    if (grid.dim >= 2) add_face_flux(1, vel_y, y_minus, y_plus);
    if (grid.dim == 3) add_face_flux(2, vel_z, z_minus, z_plus);
    const bool native=GridMetrics::is_axisymmetric_rz(grid);
    if (native) {
        // Negative-r axis ghosts are a signed coordinate extension: their
        // oriented face flux and r*dr volume have the same sign. The ratio
        // reproduces the even physical divergence; this does not authorize
        // a negative physical integration measure or a ghost publication.
        if (!std::isfinite(volume) || volume==0.0) {
            const double invalid=std::numeric_limits<double>::quiet_NaN();
            return {invalid,invalid};
        }
        result.divergence=face_flux/volume;
    } else {
        result.divergence = volume > 0.0 ? face_flux / volume : 0.0;
    }

    if (GridMetrics::geometry_kind(grid) == GridMetrics::Geometry::Cartesian) {
        const double omega_x = ddy(vel_z) - ddz(vel_y);
        const double omega_y = ddz(vel_x) - ddx(vel_z);
        const double omega_z = ddx(vel_y) - ddy(vel_x);
        result.vorticity = std::sqrt(omega_x * omega_x + omega_y * omega_y + omega_z * omega_z);
        return result;
    }

    // Native reflected ghosts retain signed r and signed physical u_phi.
    // The same cylindrical curl formula uses u_phi/r; taking |r| would turn
    // a rigid-rotation ghost curl from 2*Omega into zero. Existing charts
    // retain their original absolute-radius numerical convention exactly.
    const double radius = native ? grid.GetCellCenterX(i)
        : std::max(std::abs(grid.GetCellCenterX(i)), 0.5 * grid.dx1);
    if (native && (!std::isfinite(radius) || radius==0.0)) {
        const double invalid=std::numeric_limits<double>::quiet_NaN();
        return {invalid,invalid};
    }
    if (GridMetrics::geometry_kind(grid) == GridMetrics::Geometry::Cylindrical
        && (grid.dim == 3 || GridMetrics::is_axisymmetric_rz(grid))) {
        // Logical axes are (r,z,phi), or explicit axisymmetric (r,z).
        // RZ retains v_phi; ddz above is zero because there is no phi axis.
        const double omega_r = ddz(vel_y) / radius - ddy(vel_z);
        const double omega_phi = ddy(vel_x) - ddx(vel_y);
        const double omega_z = vel_z_center / radius + ddx(vel_z) - ddz(vel_x) / radius;
        result.vorticity = std::sqrt(omega_r * omega_r + omega_phi * omega_phi + omega_z * omega_z);
        return result;
    }

    if (grid.dim <= 2) {
        // The spherical 2-D specialization is polar (r,phi).
        // Cylindrical RZ has already used its three-component curl above.
        const double omega = (vel_y_center + radius * ddx(vel_y) - ddy(vel_x)) / radius;
        result.vorticity = std::abs(omega);
        return result;
    }


    // 3-D spherical logical axes are (r, theta, phi) with physical components
    // (v_r, v_theta, v_phi). The floor prevents numerical singularities in ghosts.
    const double theta = grid.GetCellCenterY(j);
    const double sin_theta = std::max(std::abs(std::sin(theta)), 1.0e-12);
    const double cos_theta = std::cos(theta);
    const double omega_r = (cos_theta * vel_z_center + sin_theta * ddy(vel_z) - ddz(vel_y)) /
                           (radius * sin_theta);
    const double omega_theta = ddz(vel_x) / (radius * sin_theta) -
                               (vel_z_center + radius * ddx(vel_z)) / radius;
    const double omega_phi = (vel_y_center + radius * ddx(vel_y) - ddy(vel_x)) / radius;
    result.vorticity = std::sqrt(omega_r * omega_r + omega_theta * omega_theta + omega_phi * omega_phi);
    return result;
}

} // namespace VelocityDiagnostics
