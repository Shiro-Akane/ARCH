/**
 * @file GeometricSources.h
 * @brief Shared curvilinear momentum sources for one accepted cell.
 *
 * Workflow:
 * 1. Consume the actual coordinate layout, physical means and source duration.
 * 2. Apply common cylindrical/polar/spherical curvature with the same measures
 *    used by the flux divergence; these terms do not source mass or total energy.
 * The caller owns stage scratch, geometry/state preparation and publication.
 * No allocation, extra evolved field, floor or clipping is introduced.
 */
#pragma once

#include <cmath>
#include <limits>
#include "core/ArchPortability.h"
#include "data/FluidState.h"
#include "numerics/curvilinear/GridMetrics.h"
#include "numerics/state/StateAdmissibility.h"

namespace TimeIntegration {

/** Bound the unresolved-azimuth centrifugal impulse, independently of gravity.
 * The spatial CFL already controls the resolved pressure/transport fluxes.
 * For a=u_phi^2*<1/r>, a radial impulse d changes worst-case specific K by
 * |u_r|*d+d^2/2. Its positive root at the available specific internal energy
 * e is d=2e/(hypot(u_r,sqrt(2e))+|u_r|). Using half that impulse consumes at
 * most half e, leaving a margin for the coupled flux/RK trial. This local
 * source bound is not a whole-step positivity proof or a fictitious phi CFL.
 * Zero swirl has no added limit; invalid active thermal data fail explicitly.
 */
ARCH_HOST_DEVICE inline double cylindrical_rotation_source_dt(
    const FluidVector& state,const GridMetrics::GeometryView& grid,int i)
{
    if(grid.geometry!=GridMetrics::Geometry::Cylindrical||grid.dim!=2)
        return std::numeric_limits<double>::max();
    const auto recovered=arch::state::recover(state);
    if(recovered.status!=arch::state::Status::valid)return arch::state::invalid();
    if(recovered.w==0.)return std::numeric_limits<double>::max();
    const double inverse_radius=GridMetrics::InverseRadiusVolumeAverage(grid,i);
    const double acceleration=recovered.w*recovered.w*inverse_radius;
    if(!std::isfinite(inverse_radius)||!(inverse_radius>0.)||
       !std::isfinite(acceleration))return arch::state::invalid();
    if(acceleration==0.)return std::numeric_limits<double>::max();
    const double radial_speed=std::abs(recovered.u);
    const double impulse=2.*recovered.internal/
        (std::hypot(radial_speed,std::sqrt(2.*recovered.internal))+radial_speed);
    const double dt=.5*impulse/acceleration;
    return std::isfinite(dt)&&dt>0.?dt:arch::state::invalid();
}

/**
 * Shared cylindrical curvature terms for a piecewise-constant accepted cell.
 * The loader chooses its actual angular component; the same formula serves
 * polar and full cylindrical orthonormal bases.
 * Positive density, resolved pressure and GridMetrics average 1/r are inputs.
 * Angular momentum density gets -rho*v_r*v_phi/r; z, mass and energy do not.
 */
ARCH_HOST_DEVICE inline void add_cylindrical_momentum_sources(
    double rho, double radial_velocity, double angular_velocity,
    double pressure, double inverse_radius, double dt,
    double& radial_delta, double* angular_delta)
{
    radial_delta += dt * (rho * angular_velocity * angular_velocity + pressure) * inverse_radius;
    if (angular_delta)
        *angular_delta += dt * (-rho * radial_velocity * angular_velocity) * inverse_radius;
}

template <typename EosType>
ARCH_HOST_DEVICE inline void add_geometric_source_cell(
    const FluidVector& U, const double* composition, const EosType& eos,
    const GridMetrics::GeometryView& grid, int i, int j, double dt,
    FluidVector& delta)
{
    using GridMetrics::Geometry;
    if (grid.geometry == Geometry::Cartesian) return;
    const double r = grid.GetCellCenterX(i);
    if (r == 0.0) return;
    const double p = eos.get_pressure(U, composition);
    const double rho = U.rho;
    const double v_x = U.mom_u / rho;
    const double v_y = U.mom_v / rho;
    const double v_z = U.mom_w / rho;
    const double inverse_radius = GridMetrics::InverseRadiusVolumeAverage(grid, i);

    // The caller lends an authenticated chart; its component order also serves
    // diffusion and source arithmetic stays in the shared cylindrical leaf.
    const auto layout=GridMetrics::resolve_coordinate_layout(
        grid.geometry,grid.dim);
    if (grid.geometry == Geometry::Cylindrical ||
        (grid.geometry == Geometry::Spherical && grid.dim == 2)) {
        const bool polar=layout.basis==GridMetrics::CoordinateLayout::Basis::CylindricalRPhiZ;
        // Radial 1D stays pressure-only. Both cylindrical multidimensional
        // charts retain uphi in mom_w, even when phi has no spatial derivative.
        const double v_phi=polar?v_y:(grid.dim>=2?v_z:0.0);
        // Axisymmetric angular transport uses integral r*dA / integral r*dV;
        // that conservative torque divergence already absorbs -rho*ur*uphi/r.
        // Adding it here again would double-count curvature. Retained polar
        // and full Cyl3 paths keep their original ordinary-divergence source.
        double* angular_delta=polar?&delta.mom_v:
            (grid.dim==3?&delta.mom_w:nullptr);
        add_cylindrical_momentum_sources(rho,v_x,v_phi,p,inverse_radius,dt,
            delta.mom_u,angular_delta);
    } else if (grid.geometry == Geometry::Spherical) {
        if (grid.dim == 1) {
            delta.mom_u += dt * 2.0 * p * inverse_radius;
        } else if (grid.dim == 3) {
            const double theta = grid.SourceTheta(j);
            const double cot_theta = std::cos(theta) / std::sin(theta);
            delta.mom_u += dt * (rho * (v_y * v_y + v_z * v_z) + 2.0 * p) * inverse_radius;
            delta.mom_v += dt * (rho * v_z * v_z * cot_theta + p * cot_theta - rho * v_x * v_y) * inverse_radius;
            delta.mom_w += dt * (-rho * v_x * v_z - rho * v_y * v_z * cot_theta) * inverse_radius;
        }
    }
}

/** Work-adjoint centrifugal coefficient for the native (r,z) cylindrical cell.
 *
 * The continuum radial source is rho*u_phi^2/r.  Its point PWC evaluation is
 * kept above as the separate analytical oracle.  At a resolved stage the
 * azimuthal momentum is transported with torque measure T=integral r dA and
 * cell moment W=integral r dV, while mass and radial momentum use A/V.
 * For one face, the angular kinetic-energy change implied by that torque and
 * the mass transfer is
 *   Fmass*u_phi^2*(T*(V/W) - A/2),
 * whereas the energy flux advects Fmass*u_phi^2*A/2.  The per-face coefficient
 * below repairs exactly that difference without an energy source:
 *   C_upper = T_upper/W - A_upper/V,
 *   C_lower = A_lower/V - T_lower/W.
 * For an open interior cell C_lower+C_upper = 2/ell - <1/r> = 1/r + O(dr^2),
 * so the corrected source still converges to the point source.  A reflecting
 * face carries neither mass nor torque flux, so only that face's contribution
 * is omitted.  The axis has A=T=0 and already contributes nothing.  The
 * pressure p*<1/r> source is unchanged and remains the ordinary equilibrium
 * term.  Constant-transported-state work balance is exact across open faces and
 * one-face walls; variable-state consistency follows at the same quadrature
 * order.  Axial faces are neutral here: T_z/W = A_z/V = 1/dz exactly, so only
 * the radial faces enter.  No evolved J/ell state or extra phi source is added.
 */
ARCH_HOST_DEVICE inline double axisymmetric_centrifugal_work_coefficient(
    const GridMetrics::GeometryView& grid, int i, bool lower_open,
    bool upper_open)
{
    using GridMetrics::Geometry;
    using Basis=GridMetrics::CoordinateLayout::Basis;
    const auto layout=GridMetrics::resolve_coordinate_layout(grid.geometry,grid.dim);
    if (grid.geometry!=Geometry::Cylindrical || grid.dim!=2 ||
        layout.basis!=Basis::CylindricalRZPhi)
        return std::numeric_limits<double>::quiet_NaN();
    const double volume=GridMetrics::CellVolume(grid,i,0,0);
    const double moment=GridMetrics::AngularMomentumVolumeMoment(grid,i,0,0);
    const double lower=grid.GetFacePosL(i),upper=grid.GetFacePosR(i);
    if (!std::isfinite(volume)||!(volume>0.)||!std::isfinite(moment)||!(moment>0.)
        ||!std::isfinite(lower)||lower<0.||!std::isfinite(upper)||!(upper>lower))
        return std::numeric_limits<double>::quiet_NaN();
    // Factor the exact annular moments with eta=lower/upper. Directly
    // subtracting T/W and A/V cancels two O(1/dr) terms in a thin ring;
    // forming r^3 also overflows unnecessarily. These bounded polynomials
    // give C_upper=(1+2*eta)/den/upper and C_lower=eta*(2+eta)/den/upper,
    // den=(1+eta)*(1+eta+eta^2), including the regular lower=0 axis limit.
    const double eta=lower/upper;
    const double denominator=(1.+eta)*(1.+eta+eta*eta);
    double coefficient=0.;
    if (lower_open) coefficient+=(eta*(2.+eta)/denominator)/upper;
    if (upper_open) coefficient+=((1.+2.*eta)/denominator)/upper;
    return coefficient;
}

/** Apply the shared stage source, selecting the work-adjoint cell quadrature
 * only for the native (r,z) cylindrical chart.  Every retained chart re-enters
 * add_geometric_source_cell unchanged, preserving its exact arithmetic.  The
 * caller supplies authenticated radial face openness; an empty/default view is
 * the ordinary open-cell path used by direct numerical leaves.
 */
template <typename EosType>
ARCH_HOST_DEVICE inline void add_axisymmetric_work_source_cell(
    const FluidVector& U, const double* composition, const EosType& eos,
    const GridMetrics::GeometryView& grid, int i, int j, double dt,
    bool lower_open, bool upper_open, FluidVector& delta)
{
    using GridMetrics::Geometry;
    using Basis=GridMetrics::CoordinateLayout::Basis;
    const auto layout=GridMetrics::resolve_coordinate_layout(grid.geometry,grid.dim);
    if (grid.geometry!=Geometry::Cylindrical || grid.dim!=2 ||
        layout.basis!=Basis::CylindricalRZPhi) {
        add_geometric_source_cell(U,composition,eos,grid,i,j,dt,delta);
        return;
    }
    const double rho=U.rho;
    const double pressure=eos.get_pressure(U,composition);
    const double azimuthal_speed=U.mom_w/rho;
    const double inverse_radius=GridMetrics::InverseRadiusVolumeAverage(grid,i);
    const double coefficient=axisymmetric_centrifugal_work_coefficient(
        grid,i,lower_open,upper_open);
    // The torque divergence already absorbs -rho*u_r*u_phi/r; no second phi
    // source and no energy source are added.  Pressure keeps the established
    // <1/r> quadrature used by the constant-pressure equilibrium identity.
    delta.mom_u+=dt*(rho*azimuthal_speed*azimuthal_speed*coefficient
        +pressure*inverse_radius);
}

} // namespace TimeIntegration
