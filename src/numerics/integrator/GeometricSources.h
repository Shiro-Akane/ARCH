/**
 * @file GeometricSources.h
 * @brief Common per-cell orthonormal momentum sources for every backend.
 *
 * These curvature terms complement the metric-weighted flux divergence; they
 * do not add mass or total-energy sources. Radial factors use GridMetrics'
 * cell-volume average of 1/r, not simply the inverse centre radius. Velocity
 * components follow the native orthonormal axes, including the shared 2D
 * polar specialization. The caller supplies the explicit stage duration dt.
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

#include "data/FluidState.h"
#include "grid/GridMetrics.h"
#include "numerics/reconstruction/RzCellPolynomial.h"

namespace TimeIntegration {

/**
 * Shared cylindrical curvature terms for a piecewise-constant accepted cell.
 * The loader chooses its actual angular component; the same formula serves
 * axisymmetric/full (r,z,phi) orthonormal bases.
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

/**
 * Explicit finite-volume (r,z,phi) source adapter for an axisymmetric r-z cell.
 * Evaluate authoritative EOS pressure once, then consume the shared cylindrical
 * radial source and full-volume inverse-radius metric. Native mom_v is z and mom_w is
 * phi even though there is no active phi derivative. No density/pressure floor,
 * mass/energy source or separate backend mathematics is introduced.
 * Generic GeometryView dispatch remains unchanged until its consumers migrate.
 */
template <typename EosType>
ARCH_HOST_DEVICE inline void add_rz_geometric_source_cell(
    const FluidVector& U, const double* composition, const EosType& eos,
    double r_left, double r_right, double dt, FluidVector& delta)
{
    const double pressure=eos.get_pressure(U,composition);
    const double inverse_radius=GridMetrics::Rz::InverseRadiusVolumeAverage(r_left,r_right);
    add_cylindrical_momentum_sources(U.rho,U.mom_u/U.rho,U.mom_w/U.rho,
        pressure,inverse_radius,dt,delta.mom_u,nullptr);
}

/** Integrate the RZ radial source using the same conservative point profile.
 * S_r V = 2*pi*dz*integral(P+rho*u_phi^2) dr: the radial Jacobian cancels
 * the curvature 1/r before quadrature, including the native axis cell.
 * A bad required stencil or thermodynamic state rejects the stage. The caller
 * owns composition scratch and commits the result only after this leaf passes.
 */
template<class StateReader,class FractionReader,class EosType>
ARCH_INLINE bool add_rz_integrated_geometric_source(
    const StateReader& read,const FractionReader& fraction,int index,int species,
    const EosType& eos,const GridMetrics::GeometryView& grid,int i,double dt,
    double* composition,FluidVector& delta)
{
    const auto cell=RzReconstruction::radial_cell(grid,i);
    const auto closure=RzThermodynamics::make_cell(read,index,grid,i);
    const auto profile=RzReconstruction::limited_profile(read,fraction,index,species,cell,closure);
    if(!profile.valid)return false;
    const double left=grid.GetFacePosL(i),right=grid.GetFacePosR(i);
    FluidVector integral{};
    for(int n=0;n<4;++n) {
        const double radius=RzReconstruction::certified_node_radius(n+2,closure);
        const auto point=profile.at(radius);
        for(int k=0;k<species;++k)
            composition[k]=RzReconstruction::limited_fraction(read,fraction,index,k,
                cell,radius,profile,point.rho);
        if(arch::state::validate(point,composition,species,1,0.,0.,
            std::numeric_limits<double>::max())!=arch::state::Status::valid)return false;
        const double pressure=eos.get_pressure(point,composition);
        if(!std::isfinite(pressure)||!(pressure>0.))return false;
        add_cylindrical_momentum_sources(point.rho,point.mom_u/point.rho,
            point.mom_w/point.rho,pressure,2./(left+right),
            .5*dt*RzReconstruction::quadrature_weight(n),integral.mom_u,nullptr);
    }
    if(!std::isfinite(integral.mom_u))return false;
    delta.mom_u+=integral.mom_u;
    return true;
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

    if (grid.geometry == Geometry::Cylindrical) {
        // Both cylindrical charts use (r,z,phi) physical components.
        const bool rz = grid.semantics == GridMetrics::GeometrySemantics::AxisymmetricRz;
        const double v_phi = (rz || grid.dim == 3) ? v_z : 0.0;
        // RZ torque divergence already contains the azimuthal curvature.
        // Retain the radial pressure/centrifugal source exactly.
        double* angular_delta = !rz && grid.dim==3 ? &delta.mom_w : nullptr;
        add_cylindrical_momentum_sources(rho,v_x,v_phi,p,inverse_radius,dt,
            delta.mom_u,angular_delta);
    } else if (grid.geometry == Geometry::Spherical) {
        if (grid.dim == 1) {
            delta.mom_u += dt * 2.0 * p * inverse_radius;
        } else if (grid.dim == 2) {
            // Preserve the project's 2D polar (r,phi) specialization.
            delta.mom_u += dt * (rho * v_y * v_y + p) * inverse_radius;
            delta.mom_v += dt * (-rho * v_x * v_y) * inverse_radius;
        } else if (grid.dim == 3) {
            const double theta = grid.SourceTheta(j);
            const double cot_theta = std::cos(theta) / std::sin(theta);
            delta.mom_u += dt * (rho * (v_y * v_y + v_z * v_z) + 2.0 * p) * inverse_radius;
            delta.mom_v += dt * (rho * v_z * v_z * cot_theta + p * cot_theta - rho * v_x * v_y) * inverse_radius;
            delta.mom_w += dt * (-rho * v_x * v_z - rho * v_y * v_z * cot_theta) * inverse_radius;
        }
    }
}

} // namespace TimeIntegration
