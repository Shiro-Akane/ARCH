/**
 * @file DiffFlux.h
 * @brief Computes diffusion fluxes and operators for explicit time integration.
 *
 * Workflow:
 * 1. Recover thermal inputs from the actual conserved state.
 * 2. Calculate diffusion coefficients (viscosity, thermal conductivity, species diffusivity).
 * 3. Compute face-centered gradients for the requested diffusion dimension.
 * 4. Generate diffusion fluxes and geometric source terms (for momentum).
 * 5. Combine multi-dimensional fluxes into a generic L(U) operator.
 */

#pragma once

#include "physics/boundary/BoundaryDiagnostics.h"
#include "physics/boundary/BoundaryFlux.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "numerics/integrator/TimeIntegratorHelper.h"
#include "numerics/curvilinear/CurvilinearViscousStress.h"
#include "numerics/diffusion/DiffusionTypes.h"
#include "numerics/diffusion/NewtonianViscousStress.h"

#include "data/FluidState.h"
#include "data/GlobalDefs.h"
#include "driver/schedule/ReductionSpec.h"
#include "grid/Grid.h"
#include "physics/diffusionCoe/diffusion_math.hpp"
#include "physics/eos/eos_state.h"
#include "physics/species/Species.h"

// Diffusive flux and operator assembly.

namespace DiffFlux
{
    using DiffusionGeometry = GridMetrics::Geometry;

    inline DiffusionGeometry diffusion_geometry_from_name(const std::string& geometry)
    {
        return GridMetrics::geometry_from_name(geometry);
    }

    ARCH_INLINE constexpr double diffusion_dt_sentinel()
    {
        return std::numeric_limits<double>::max();
    }

    ARCH_INLINE bool diffusion_routes_enabled(const DiffusionConfigView& config)
    {
        return config.use_diffusion
            && (config.use_thermal_diffusion
                || config.use_viscous_diffusion
                || config.use_species_diffusion);
    }

    ARCH_INLINE bool diffusion_face_is_active(double rho_left, double rho_right)
    {
        return std::isfinite(rho_left) && rho_left > 0.0
            && std::isfinite(rho_right) && rho_right > 0.0;
    }

    ARCH_INLINE double diffusion_face_spacing(
        DiffusionGeometry geometry, int dim, int direction,
        double dx1, double dx2, double dx3, double radius, double theta)
    {
        return GridMetrics::PhysicalSpacing(
            geometry, dim, direction, dx1, dx2, dx3, radius, theta);
    }

    /** Ordinary right-indexed face spacing from the common metric owner. */
    ARCH_INLINE double diffusion_face_spacing(
        const GridMetrics::GeometryView& grid,int direction,int i,int j)
    {
        return GridMetrics::CellPairDistance(grid,direction,i,j);
    }

    ARCH_INLINE DiffusionCoefficients evaluate_diffusion_coefficients(
        const eos_state_t& state, const DiffusionConfigView& config,
        int species_count, const double* charge, const double* inverse_mass)
    {
        DiffusionCoefficients coefficients{};
        if (!(state.rho > 0.0) || !std::isfinite(state.rho)
            || !(state.T > 0.0) || !std::isfinite(state.T)
            || (config.use_thermal_diffusion && (!(state.cv > 0.0) || !std::isfinite(state.cv)))
            || !std::isfinite(config.nu_visc) || config.nu_visc < 0.0
            || !std::isfinite(config.alpha_therm) || config.alpha_therm < 0.0
            || !std::isfinite(config.D_spec) || config.D_spec < 0.0) {
            coefficients.valid = false;
            return coefficients;
        }
        const bool stellar = species_count > 0 && state.xne > 0.0;
        if (config.nu_visc > 0.0 || config.alpha_therm > 0.0) {
            if (stellar) {
                coefficients.valid = false;
                return coefficients;
            }
            coefficients.nu_visc = config.nu_visc;
            coefficients.alpha_therm = config.alpha_therm;
            coefficients.D_spec = config.D_spec;
            return coefficients;
        }

        if (stellar) {
            const double conductivity =
                ConductivityMath::compute_stellar_conductivity(
                    state.T, state.rho, state.pele, state.xne, state.eta,
                    state.Xi, species_count, charge, inverse_mass);
            const double cv_value = state.cv;
            if (!(cv_value > 0.0) || !std::isfinite(cv_value)) { coefficients.valid = false; return coefficients; }
            coefficients.alpha_therm =
                (conductivity / state.rho) / cv_value;
            coefficients.valid = std::isfinite(coefficients.alpha_therm)
                && coefficients.alpha_therm >= 0.0;
        } else {
            coefficients.nu_visc = config.nu_visc;
            coefficients.alpha_therm = config.alpha_therm;
            coefficients.D_spec = config.D_spec;
        }
        return coefficients;
    }

    template <typename EosType, typename SpeciesAccessor>
    ARCH_INLINE DiffusionCoefficients evaluate_diffusion_coefficients_from_eos(
        const EosType& eos, const SpeciesAccessor& species,
        const DiffusionConfigView& config, double rho, double temperature,
        const double* composition, double* charge, double* inverse_mass)
    {
        eos_state_t state{};
        state.rho = rho;
        state.T = temperature;
        state.Xi = composition;
        eos.evaluate_state(state);

        const bool needs_arrays = species.size() > 0 && state.xne > 0.0
            && !(config.nu_visc > 0.0 || config.alpha_therm > 0.0);
        if (needs_arrays) {
            for (int species_index = 0;
                 species_index < species.size(); ++species_index) {
                charge[species_index] = species.get_Z(species_index);
                inverse_mass[species_index] = 1.0 / species.get_A(species_index);
            }
        }
        return evaluate_diffusion_coefficients(
            state, config, species.size(), charge, inverse_mass);
    }

    // Rotation of the orthonormal basis per unit physical distance. Applying
    // it to velocity components supplies the connection in D_d v = d_d v + C_d v.
    // The same C_d acts on momentum flux in the vector-divergence source.
    struct ViscousBasisRotation {
        double x = 0.0, y = 0.0, z = 0.0;

        ARCH_INLINE double infinity_norm() const
        {
            return std::max(std::abs(x) + std::abs(y),
                std::max(std::abs(x) + std::abs(z), std::abs(y) + std::abs(z)));
        }

        ARCH_INLINE FluidVector apply(const FluidVector& vector) const
        {
            return {0.0, y * vector.mom_w - z * vector.mom_v,
                    z * vector.mom_u - x * vector.mom_w,
                    x * vector.mom_v - y * vector.mom_u, 0.0};
        }
    };

    ARCH_INLINE ViscousBasisRotation viscous_basis_rotation(
        const GridMetrics::GeometryView& grid, int direction, int i, int j)
    {
        if (grid.geometry == DiffusionGeometry::Cartesian || direction == 0)
            return {};
        const double inverse_radius = 1.0 / grid.GetCellCenterX(i);
        if (grid.dim == 2 && grid.geometry == DiffusionGeometry::Spherical)
            return {0.0, 0.0, inverse_radius}; // spherical polar (r,phi)
        if (grid.geometry == DiffusionGeometry::Cylindrical)
            return direction == 2 ? ViscousBasisRotation{0.0, -inverse_radius, 0.0}
                                  : ViscousBasisRotation{}; // (r,z,phi)
        if (direction == 1) return {0.0, 0.0, inverse_radius};
        const double theta = grid.GetCellCenterY(j);
        return {inverse_radius * std::cos(theta) / std::sin(theta), -inverse_radius, 0.0};
    }

    ARCH_INLINE FluidVector viscous_velocity(const FluidVector& state)
    {
        return {0.0, state.mom_u / state.rho, state.mom_v / state.rho,
                state.mom_w / state.rho, 0.0};
    }

    ARCH_INLINE void assemble_diffusion_face_flux(
        const FluidVector& left, const FluidVector& right,
        double temperature_left, double temperature_right,
        double face_density, const double* species_left,
        const double* species_right, int species_count, double spacing,
        double heat_capacity, const DiffusionCoefficients& coefficients,
        const DiffusionConfigView& config, FluidVector& flux,
        double* species_flux, int species_flux_stride,
        ViscousBasisRotation rotation = {})
    {
        const double temperature_gradient =
            (temperature_right - temperature_left) / spacing;
        const double thermal_flux = config.use_thermal_diffusion
            ? (-coefficients.alpha_therm * face_density
               * heat_capacity * temperature_gradient)
            : 0.0;

        flux.rho = 0.0;
        const double velocity_face_x =
            0.5 * (left.mom_u / left.rho + right.mom_u / right.rho);
        const double velocity_face_y =
            0.5 * (left.mom_v / left.rho + right.mom_v / right.rho);
        const double velocity_face_z =
            0.5 * (left.mom_w / left.rho + right.mom_w / right.rho);

        if (config.use_viscous_diffusion) {
            const FluidVector connection = rotation.apply(
                {0.0, velocity_face_x, velocity_face_y, velocity_face_z, 0.0});
            const double velocity_gradient_x =
                (right.mom_u / right.rho - left.mom_u / left.rho) / spacing + connection.mom_u;
            const double velocity_gradient_y =
                (right.mom_v / right.rho - left.mom_v / left.rho) / spacing + connection.mom_v;
            const double velocity_gradient_z =
                (right.mom_w / right.rho - left.mom_w / left.rho) / spacing + connection.mom_w;
            flux.mom_u = -coefficients.nu_visc * face_density * velocity_gradient_x;
            flux.mom_v = -coefficients.nu_visc * face_density * velocity_gradient_y;
            flux.mom_w = -coefficients.nu_visc * face_density * velocity_gradient_z;
        }
        flux.eng = thermal_flux
            + (flux.mom_u * velocity_face_x
               + flux.mom_v * velocity_face_y
               + flux.mom_w * velocity_face_z);

        if (config.use_species_diffusion) {
            for (int species = 0; species < species_count; ++species) {
                const double composition_gradient =
                    (species_right[species] - species_left[species]) / spacing;
                species_flux[species * species_flux_stride] =
                    -face_density * coefficients.D_spec * composition_gradient;
            }
        }
    }

    // Internal mathematical data: not part of the backend launch/layout ABI.
    struct DiffusionFaceProperties {
        double density = 0.0;
        double temperature_left = 0.0;
        double temperature_right = 0.0;
        double heat_capacity = 0.0;
        DiffusionCoefficients coefficients{};
    };

    template <typename EosType, typename SpeciesAccessor>
    ARCH_INLINE DiffusionFaceProperties evaluate_diffusion_face_properties(
        const FluidVector& left, const FluidVector& right,
        const double* species_left, const double* species_right, int species_count,
        const EosType& eos, const SpeciesAccessor& species,
        const DiffusionConfigView& config, double* species_face,
        double* charge, double* inverse_mass)
    {
        DiffusionFaceProperties face{};
        const double internal_left = arch::state::recover(left).internal;
        const double internal_right = arch::state::recover(right).internal;

        face.temperature_left =
            eos.get_temperature(left.rho, internal_left, species_left);
        face.temperature_right =
            eos.get_temperature(right.rho, internal_right, species_right);
        face.density = 0.5 * (left.rho + right.rho);
        const double face_temperature =
            0.5 * (face.temperature_left + face.temperature_right);
        for (int index = 0; index < species_count; ++index)
            species_face[index] = 0.5 * (species_left[index] + species_right[index]);

        face.coefficients =
            evaluate_diffusion_coefficients_from_eos(
                eos, species, config, face.density, face_temperature,
                species_face, charge, inverse_mass);
        if (face.coefficients.valid && config.use_thermal_diffusion) {
            face.heat_capacity = eos.get_cv(face.density, face_temperature, species_face);
            face.coefficients.valid = face.heat_capacity > 0.0 && std::isfinite(face.heat_capacity);
        }
        face.coefficients.valid = face.coefficients.valid
            && face.temperature_left > 0.0 && std::isfinite(face.temperature_left)
            && face.temperature_right > 0.0 && std::isfinite(face.temperature_right);
        if(face.coefficients.valid&&config.use_viscous_diffusion) {
            // The original face law must not erase a configured positive nu
            // when rho*nu is unrepresentable, in any computational geometry.
            double mu=0.;
            face.coefficients.valid=NewtonianViscousStress::dynamic_viscosity(
                face.density,face.coefficients.nu_visc,mu);
        }
        return face;
    }

    template <typename EosType, typename SpeciesAccessor>
    ARCH_INLINE DiffusionFaceStatus evaluate_diffusion_face(
        const FluidVector& left, const FluidVector& right,
        const double* species_left_source, const double* species_right_source,
        int species_count, int species_source_stride, double spacing,
        const EosType& eos, const SpeciesAccessor& species,
        const DiffusionConfigView& config,
        double* species_left, double* species_right, double* species_face,
        double* charge, double* inverse_mass,
        FluidVector& flux, double* species_flux, int species_flux_stride,
        ViscousBasisRotation rotation = {},
        DiffusionFaceProperties* properties = nullptr)
    {
        if (!diffusion_face_is_active(left.rho, right.rho)
            || !(spacing > 0.0) || !std::isfinite(spacing)) return {false, false};
        for (int index = 0; index < species_count; ++index) {
            species_left[index] = species_left_source[index * species_source_stride];
            species_right[index] = species_right_source[index * species_source_stride];
        }
        const auto face = evaluate_diffusion_face_properties(left, right,
            species_left, species_right, species_count, eos, species, config,
            species_face, charge, inverse_mass);
        if (!face.coefficients.valid) return {true, false};
        if(properties) *properties=face;
        assemble_diffusion_face_flux(
            left, right, face.temperature_left, face.temperature_right, face.density,
            species_left, species_right, species_count, spacing,
            face.heat_capacity, face.coefficients, config, flux,
            species_flux, species_flux_stride, rotation);
        return {true, true};
    }

    /** Gather only the transverse centred columns used by a Cartesian face.
     * Workflow: validate actual storage extents and each borrowed positive-rho
     * velocity; form G_ik=(u_i,+-u_i,-)/(2h_k) for active k!=direction.
     * The normal column is zero because the paired traction uses the true
     * normal face difference. Thus one real corner halo suffices; no second
     * normal ghost is read. Inactive physical partials remain actual zero.
     */
    template<class StateReader>
    ARCH_INLINE bool cartesian_transverse_gradient(const StateReader& read,
        const GridMetrics::GeometryView& grid,int cell,int direction,
        NewtonianViscousStress::VelocityGradient& output)
    {
        if(grid.geometry!=DiffusionGeometry::Cartesian||
           grid.dim<1||grid.dim>3||direction<0||direction>=grid.dim||
           grid.ng<1||grid.stride_y<=0||grid.stride_z<=0||grid.total_size<=0||
           cell<0||cell>=grid.total_size)return false;
        const int extents[3]{grid.stride_y,grid.stride_z/grid.stride_y,
            grid.total_size/grid.stride_z};
        const int coordinate[3]{cell%grid.stride_y,
            (cell%grid.stride_z)/grid.stride_y,cell/grid.stride_z};
        const int strides[3]{1,grid.stride_y,grid.stride_z};
        const double spacing[3]{grid.dx1,grid.dx2,grid.dx3};
        NewtonianViscousStress::VelocityGradient candidate{};
        for(int axis=0;axis<grid.dim;++axis)if(axis!=direction) {
            if(coordinate[axis]<=0||coordinate[axis]>=extents[axis]-1||
               !std::isfinite(spacing[axis])||!(spacing[axis]>0.))return false;
            const auto left=read(cell-strides[axis]),right=read(cell+strides[axis]);
            if(!diffusion_face_is_active(left.rho,right.rho))return false;
            const double a[3]{left.mom_u/left.rho,left.mom_v/left.rho,left.mom_w/left.rho};
            const double b[3]{right.mom_u/right.rho,right.mom_v/right.rho,right.mom_w/right.rho};
            for(int component=0;component<3;++component) {
                if(!std::isfinite(a[component])||!std::isfinite(b[component]))return false;
                const double value=.5*((b[component]-a[component])/spacing[axis]);
                if(!std::isfinite(value))return false;
                candidate[3*component+axis]=value;
            }
        }
        output=candidate;return true;
    }

    /** Replace Cartesian viscosity and pair energy with the identical traction.
     * Workflow: use already checked actual left/right EOS temperatures and Xi;
     * evaluate the existing coefficient owner at EACH cell, not at face rho/T;
     * form mu_c=rho_c*nu_c and the shared product-weighted traction. Thermal
     * flux is recomputed with its ORIGINAL face coefficient/cv/T expression,
     * then the same new momentum flux is dotted with the ORIGINAL face velocity.
     * Species flux is untouched and no extra dissipative heat is introduced.
     * Both backends borrow this one leaf before publishing their face vector.
     */
    template<class EosType,class SpeciesAccessor,class StateReader>
    ARCH_INLINE bool replace_cartesian_viscous_flux(const StateReader& read,
        int right_cell,const GridMetrics::GeometryView& grid,int direction,
        double spacing,const EosType& eos,const SpeciesAccessor& species,
        const DiffusionConfigView& config,const double* composition_left,
        const double* composition_right,const DiffusionFaceProperties& face,
        double* charge,double* inverse_mass,FluidVector& flux)
    {
        if(grid.geometry!=DiffusionGeometry::Cartesian||!config.use_viscous_diffusion)return true;
        // The existing coefficient owner always returns nu=0 when configured
        // nu is exactly zero, including stellar conductivity. The already
        // validated original face flux then carries zero viscous traction;
        // no extra EOS evaluation or unused transverse stencil is needed.
        if(config.nu_visc==0.)return true;
        if(direction<0||direction>=grid.dim)return false;
        const int stride=direction==0?1:direction==1?grid.stride_y:grid.stride_z;
        if(right_cell<stride||right_cell>=grid.total_size)return false;
        const auto left=read(right_cell-stride),right=read(right_cell);
        if(!diffusion_face_is_active(left.rho,right.rho))return false;
        const auto lc=evaluate_diffusion_coefficients_from_eos(eos,species,config,
            left.rho,face.temperature_left,composition_left,charge,inverse_mass);
        const auto rc=evaluate_diffusion_coefficients_from_eos(eos,species,config,
            right.rho,face.temperature_right,composition_right,charge,inverse_mass);
        if(!lc.valid||!rc.valid)return false;
        double mu_left=0.,mu_right=0.;
        if(!NewtonianViscousStress::dynamic_viscosity(left.rho,lc.nu_visc,mu_left)||
           !NewtonianViscousStress::dynamic_viscosity(right.rho,rc.nu_visc,mu_right))return false;
        NewtonianViscousStress::VelocityGradient gl{},gr{};
        // Zero viscosity has no transverse-stencil obligation; the point leaf
        // still checks every actual face velocity before its zero shortcut.
        if((mu_left!=0.||mu_right!=0.)&&
           (!cartesian_transverse_gradient(read,grid,right_cell-stride,direction,gl)||
            !cartesian_transverse_gradient(read,grid,right_cell,direction,gr)))return false;
        const NewtonianViscousStress::Vector ul{left.mom_u/left.rho,left.mom_v/left.rho,left.mom_w/left.rho};
        const NewtonianViscousStress::Vector ur{right.mom_u/right.rho,right.mom_v/right.rho,right.mom_w/right.rho};
        NewtonianViscousStress::Vector traction{};
        if(!NewtonianViscousStress::cartesian_paired_traction(direction,spacing,
            ul,ur,gl,gr,mu_left,mu_right,traction))return false;
        const double thermal_flux=config.use_thermal_diffusion
            ?(-face.coefficients.alpha_therm*face.density*face.heat_capacity
                *((face.temperature_right-face.temperature_left)/spacing)):0.;
        const NewtonianViscousStress::Vector velocity{.5*(ul[0]+ur[0]),.5*(ul[1]+ur[1]),.5*(ul[2]+ur[2])};
        const NewtonianViscousStress::Vector momentum{-traction[0],-traction[1],-traction[2]};
        double work=0.;
        if(!NewtonianViscousStress::power(velocity,momentum,work))return false;
        FluidVector candidate=flux;
        candidate.mom_u=momentum[0];candidate.mom_v=momentum[1];candidate.mom_w=momentum[2];
        candidate.eng=thermal_flux+work;
        if(!std::isfinite(candidate.eng))return false;
        flux=candidate;return true;
    }

    /** Assemble curved stress and paired work through one common face owner.
     * Workflow: retain the existing coefficient/EOS/species owner and its
     * selected face spacing. Compute the original Stokes traction and pair
     * its stress work with the same physical velocity trace.
     */
    template<class StateReader>
    ARCH_INLINE bool replace_curvilinear_viscous_flux(const StateReader& read,
        int right_cell,const GridMetrics::GeometryView& grid,int direction,
        int i,int j,const DiffusionConfigView& config,
        const DiffusionFaceProperties& face,FluidVector& flux,
        double* physical_work_velocity)
    {
        if(grid.geometry==DiffusionGeometry::Cartesian||
           (!config.use_viscous_diffusion&&!physical_work_velocity))return true;
        const double nu=config.use_viscous_diffusion?face.coefficients.nu_visc:0.;
        if(grid.dim==1||nu!=0.) {
            const int stride=direction==0?1:direction==1?grid.stride_y:grid.stride_z;
            const auto left=read(right_cell-stride),right=read(right_cell);
            double mu_left=0.,mu_right=0.;
            if(!NewtonianViscousStress::dynamic_viscosity(left.rho,nu,mu_left)||
               !NewtonianViscousStress::dynamic_viscosity(right.rho,nu,mu_right))return false;
            NewtonianViscousStress::Vector traction{},velocity{};
            if(grid.dim==1) {
                const auto radial=CurvilinearViscousStress::radial_face(read,right_cell,grid,i,nu);
                if(!radial.valid||!NewtonianViscousStress::traction(radial.stress,0,traction))return false;
                velocity=radial.work_velocity;
            } else if(!CurvilinearViscousStress::face_traction(read,right_cell,grid,direction,i,j,
                mu_left,mu_right,traction,velocity))return false;
            const NewtonianViscousStress::Vector momentum{-traction[0],-traction[1],
                -traction[2]};
            double power=0.;
            if(!NewtonianViscousStress::power(velocity,momentum,power))return false;
            // The thermal owner uses its original actual normal face spacing.
            const double spacing=diffusion_face_spacing(grid,direction,i,j);
            const double heat=config.use_thermal_diffusion
                ?-face.coefficients.alpha_therm*face.density*face.heat_capacity
                    *((face.temperature_right-face.temperature_left)/spacing):0.;
            FluidVector candidate=flux;
            candidate.mom_u=momentum[0];candidate.mom_v=momentum[1];candidate.mom_w=momentum[2];
            candidate.eng=heat+power;
            if(!std::isfinite(candidate.eng))return false;
            flux=candidate;
            if(physical_work_velocity)for(int component=0;component<3;++component)
                physical_work_velocity[component]=velocity[component];
        }

        return true;
    }

    /** Add half an absolute velocity-matrix row bound for one Cartesian face.
     * Each centred transverse derivative has absolute coefficient sum 1/h_k;
     * a normal difference has 2/h_j. Triangle bounds retain BOTH mu products,
     * including terms that could cancel across two faces, so no cancellation
     * is presumed. Divide by the actual rho cell capacity. The maximum final
     * row gives |lambda|<=2*rate; dt<=1/rate bounds a negative-real FE interval
     * once the separate global dissipativity/BC contract is proved. This leaf
     * alone does not prove real nonpositive spectrum or RKL qualification.
     */
    ARCH_INLINE bool cartesian_viscous_row_bound(double mu_left,double mu_right,
        double rho,const GridMetrics::GeometryView& grid,int direction,double* rows)
    {
        if(!rows||grid.geometry!=DiffusionGeometry::Cartesian||grid.dim<1||grid.dim>3||
           direction<0||direction>=grid.dim||!std::isfinite(rho)||!(rho>0.)||
           !std::isfinite(mu_left)||mu_left<0.||!std::isfinite(mu_right)||mu_right<0.)return false;
        const double h[3]{grid.dx1,grid.dx2,grid.dx3};
        for(int k=0;k<grid.dim;++k)if(!std::isfinite(h[k])||!(h[k]>0.))return false;
        double mu_face=0.;
        if(!NewtonianViscousStress::nonnegative_coefficient_mean(mu_left,mu_right,mu_face))return false;
        const double capacity=mu_face/rho;
        if(!std::isfinite(capacity)||(mu_face>0.&&capacity==0.))return false;
        double candidate[3]{rows[0],rows[1],rows[2]};
        for(int i=0;i<3;++i) {
            candidate[i]+=capacity*(i==direction?4./3.:1.)/h[direction]/h[direction];
            if(i==direction) {
                for(int k=0;k<grid.dim;++k)if(k!=direction)
                    candidate[i]+=capacity/(3.*h[direction])/h[k];
            } else if(i<grid.dim)
                candidate[i]+=capacity/(2.*h[direction])/h[i];
            if(!std::isfinite(candidate[i])||candidate[i]<0.)return false;
        }
        for(int i=0;i<3;++i)rows[i]=candidate[i];
        return true;
    }

    ARCH_INLINE double raw_forward_euler_candidate(
        double maximum_coefficient, const double* spacing, int dimension)
    {
        if (!(maximum_coefficient > 0.0))
            return diffusion_dt_sentinel();
        double inverse_dt = 0.0;
        for (int direction = 0; direction < dimension; ++direction)
            inverse_dt += 2.0 * maximum_coefficient
                / (spacing[direction] * spacing[direction]);
        return (inverse_dt > 0.0 ? 1.0 / inverse_dt : diffusion_dt_sentinel());
    }

    // Cell-source part of a frozen-coefficient vector-operator row bound.
    // Face transport contributes separately using its actual rho/cv weights.
    ARCH_INLINE double viscous_source_stability_rate(
        double viscosity, const GridMetrics::GeometryView& grid, int i, int j)
    {
        if (!(viscosity > 0.) || grid.geometry == DiffusionGeometry::Cartesian) return 0.;
        const double radius = grid.GetCellCenterX(i);
        const double inverse_radius = GridMetrics::InverseRadiusVolumeAverage(grid, i);
        if (grid.dim == 1) {
            const double angular_dimensions = grid.geometry == DiffusionGeometry::Spherical ? 2.0 : 1.0;
            return viscosity * angular_dimensions * inverse_radius / radius;
        }
        double rate = 0.0;
        for (int direction = 1; direction < grid.dim; ++direction) {
            const double spacing = GridMetrics::PhysicalSpacing(grid, direction, i, j);
            const double connection = viscous_basis_rotation(grid, direction, i, j).infinity_norm();
            rate += viscosity * radius * inverse_radius
                * (connection / spacing + connection * connection);
        }
        return rate;
    }

    ARCH_INLINE double finalize_raw_diffusion_dt(double minimum)
    {
        return 1.0 * minimum;
    }

    ARCH_INLINE double combine_diffusion_minimum(
        double minimum, double candidate)
    {
        const auto spec = arch::reduction::minimum_spec(
            diffusion_dt_sentinel());
        auto state = arch::reduction::begin_reduction(spec);
        arch::reduction::combine_candidate(
            spec, state, {minimum, {}, true});
        amr::CellLogicalKey candidate_key{};
        candidate_key.component = 1;
        arch::reduction::combine_candidate(
            spec, state, {candidate, candidate_key, true});
        return arch::reduction::finalize_reduction(spec, state).value;
    }

    template <typename EosType, typename SpeciesAccessor, typename StateReader>
    ARCH_INLINE DiffusionDtCandidate evaluate_diffusion_dt_candidate(
        const FluidVector& value, const double* species_source,
        int species_count, int species_source_stride,
        const EosType& eos, const SpeciesAccessor& species,
        const DiffusionConfigView& config,
        const GridMetrics::GeometryView& grid, int i, int j, int k, double* composition,
        double* neighbour_composition, double* face_composition,
        double* charge, double* inverse_mass, const StateReader& read_state)
    {
        if (grid.geometry == DiffusionGeometry::Unsupported)
            return {diffusion_dt_sentinel(), false};
        if (!(value.rho > 0.0) || !std::isfinite(value.rho)) return {diffusion_dt_sentinel(), false};
        const int cell = grid.GetIndex(i, j, k);
        for (int index = 0; index < species_count; ++index)
            composition[index] = species_source[index * species_source_stride];
        const double internal = arch::state::recover(value).internal;
        const double temperature =
            eos.get_temperature(value.rho, internal, composition);
        const DiffusionCoefficients coefficients =
            evaluate_diffusion_coefficients_from_eos(
                eos, species, config, value.rho, temperature,
                composition, charge, inverse_mass);
        if (!coefficients.valid) return {diffusion_dt_sentinel(), false};

        const double cell_cv = config.use_thermal_diffusion
            ? eos.get_cv(value.rho, temperature, composition) : 1.;
        if (!(cell_cv > 0.0) || !std::isfinite(cell_cv)) return {diffusion_dt_sentinel(), false};
        const double volume = GridMetrics::CellVolume(grid, i, j, k);
        double maximum = 0., inverse_dt = 0.;
        double cartesian_rows[3]{},curved_rows[3]{};
        const bool curved_viscous=grid.geometry!=DiffusionGeometry::Cartesian
            &&config.use_viscous_diffusion;
        for (int direction = 0; direction < grid.dim; ++direction) {
            // Retain the original current-cell spacing for both faces.
            const double cell_spacing = GridMetrics::PhysicalSpacing(grid, direction, i, j);
            if (!(cell_spacing > 0.0) || !(volume > 0.0))
                return {diffusion_dt_sentinel(), false};
            const int stride = direction == 0 ? 1 : direction == 1 ? grid.stride_y : grid.stride_z;
            const double connection = config.use_viscous_diffusion
                ? viscous_basis_rotation(grid, direction, i, j).infinity_norm() : 0.;
            for (int side = 0; side < 2; ++side) {
                const double spacing=cell_spacing;
                const int neighbour = cell + (side ? stride : -stride);
                const auto adjacent = read_state(neighbour);
                if (!diffusion_face_is_active(value.rho, adjacent.rho)) return {diffusion_dt_sentinel(), false};
                for (int sp = 0; sp < species_count; ++sp)
                    neighbour_composition[sp] = read_state.fraction(sp, neighbour);
                const auto face = side
                    ? evaluate_diffusion_face_properties(value, adjacent, composition,
                        neighbour_composition, species_count, eos, species, config,
                        face_composition, charge, inverse_mass)
                    : evaluate_diffusion_face_properties(adjacent, value, neighbour_composition,
                        composition, species_count, eos, species, config,
                        face_composition, charge, inverse_mass);
                if (!face.coefficients.valid) return {diffusion_dt_sentinel(), false};
                if(grid.geometry==DiffusionGeometry::Cartesian&&config.use_viscous_diffusion
                   &&config.nu_visc!=0.) {
                    const double adjacent_temperature=side?face.temperature_right:face.temperature_left;
                    const auto adjacent_coefficients=evaluate_diffusion_coefficients_from_eos(
                        eos,species,config,adjacent.rho,adjacent_temperature,neighbour_composition,charge,inverse_mass);
                    double mu_cell=0.,mu_adjacent=0.;
                    if(!adjacent_coefficients.valid||
                       !NewtonianViscousStress::dynamic_viscosity(value.rho,coefficients.nu_visc,mu_cell)||
                       !NewtonianViscousStress::dynamic_viscosity(adjacent.rho,adjacent_coefficients.nu_visc,mu_adjacent)||
                       !cartesian_viscous_row_bound(mu_cell,mu_adjacent,
                        value.rho,grid,direction,cartesian_rows))return {diffusion_dt_sentinel(),false};
                }
                // The unknowns are velocity, temperature and mass fraction;
                // their cell capacities are rho, rho*cv and rho respectively.
                const double density_ratio = face.density / value.rho;
                const double viscosity = config.use_viscous_diffusion
                    ? face.coefficients.nu_visc * density_ratio : 0.;
                double transport = viscosity;
                if (config.use_species_diffusion)
                    transport = std::max(transport, face.coefficients.D_spec * density_ratio);
                if (config.use_thermal_diffusion)
                    transport = std::max(transport, face.coefficients.alpha_therm * density_ratio
                        * face.heat_capacity / cell_cv);
                maximum = std::max(maximum, transport);
                const double area_per_volume = GridMetrics::FaceArea(grid, direction, i, j, k, side != 0) / volume;
                inverse_dt += area_per_volume * (transport / spacing + viscosity * connection);
                if(curved_viscous&&viscosity>0.&&grid.dim==1) {
                    NewtonianViscousStress::Vector rows{curved_rows[0],curved_rows[1],curved_rows[2]};
                    double mu=0.;
                    if(!NewtonianViscousStress::dynamic_viscosity(face.density,face.coefficients.nu_visc,mu)||
                       !CurvilinearViscousStress::radial_row_sums(grid,i,side!=0,value.rho,mu,rows))
                        return {diffusion_dt_sentinel(),false};
                    for(int component=0;component<3;++component)curved_rows[component]=rows[component];
                } else if(curved_viscous&&viscosity>0.) {
                    double mu_center=0.,mu_adjacent=0.;
                    if(!NewtonianViscousStress::dynamic_viscosity(value.rho,face.coefficients.nu_visc,mu_center)||
                       !NewtonianViscousStress::dynamic_viscosity(adjacent.rho,face.coefficients.nu_visc,mu_adjacent))
                        return {diffusion_dt_sentinel(),false};
                    NewtonianViscousStress::Vector rows{curved_rows[0],curved_rows[1],curved_rows[2]};
                    if(!CurvilinearViscousStress::face_row_sums(grid,i,j,k,direction,side!=0,value.rho,
                        side?mu_center:mu_adjacent,side?mu_adjacent:mu_center,rows))
                        return {diffusion_dt_sentinel(),false};
                    for(int component=0;component<3;++component)curved_rows[component]=rows[component];
                }

            }
        }
        // Any positive transport appears in the operator; a dimensional
        // coefficient cutoff cannot safely disable its timestep constraint.
        const double viscosity = config.use_viscous_diffusion ? coefficients.nu_visc : 0.0;
        const double source_rate = viscous_source_stability_rate(viscosity, grid, i, j);
        if (!(maximum > 0.) && !(source_rate > 0.)) return {};
        inverse_dt += source_rate;
        if(curved_viscous&&viscosity>0.&&grid.dim>1) {
            NewtonianViscousStress::Tensor sums{};
            if(!CurvilinearViscousStress::stress_row_sums(grid,i,j,-1,sums))return {diffusion_dt_sentinel(),false};
            const double factor=viscosity/grid.GetCellCenterX(i);
            const auto frame=CurvilinearViscousStress::frame(grid);
            if(frame==NewtonianViscousStress::Frame::CylindricalRZPhi) {
                curved_rows[0]+=factor*sums[8];
                if(grid.dim!=2)curved_rows[2]+=factor*sums[2];
            } else if(frame==NewtonianViscousStress::Frame::CylindricalRPhiZ) {
                curved_rows[0]+=factor*sums[4];curved_rows[1]+=factor*sums[1];
            } else {
                const double theta=grid.SourceTheta(j),cot=grid.dim==3?std::abs(std::cos(theta)/std::sin(theta)):0.;
                curved_rows[0]+=factor*(sums[4]+sums[8]);
                curved_rows[1]+=factor*(sums[1]+cot*sums[8]);
                curved_rows[2]+=factor*(sums[2]+cot*sums[5]);
            }
        }
        if(curved_viscous)for(double row:curved_rows) {
            if(!std::isfinite(row)||row<0.)return {diffusion_dt_sentinel(),false};
            inverse_dt=std::max(inverse_dt,row);
        }
        if(grid.geometry==DiffusionGeometry::Cartesian&&config.use_viscous_diffusion)
            for(int component=0;component<3;++component)inverse_dt=std::max(inverse_dt,cartesian_rows[component]);
        if (!std::isfinite(inverse_dt) || !(inverse_dt > 0.))
            return {diffusion_dt_sentinel(), false};
        return {1. / inverse_dt, true};
    }

    // 1. SFINAE Checks and Coefficient Extraction
    /**
     * @brief Retrieves diffusion coefficients. Uses eos_state_t and diffusion_math directly.
     */
    template <typename EosType>
    inline void get_coeffs(const EosType& eos, const SimConfig& config,
                           double rho, double T, const double* Xi,
                           double& nu_visc, double& alpha_therm, double& D_spec)
    {
        const SpeciesManager* specs = eos.get_species_manager();
        const SpeciesHostView species =
            specs ? specs->get_host_view() : SpeciesHostView{};
        std::vector<double> charge(species.size());
        std::vector<double> inverse_mass(species.size());
        const DiffusionCoefficients coefficients =
            evaluate_diffusion_coefficients_from_eos(
                eos, species, make_diffusion_config_view(config),
                rho, T, Xi, charge.data(), inverse_mass.data());
        if (!coefficients.valid) {
            throw std::runtime_error("Invalid diffusion state, heat capacity or transport coefficient");
        }
        nu_visc = coefficients.nu_visc;
        alpha_therm = coefficients.alpha_therm;
        D_spec = coefficients.D_spec;
    }

/**
 * Observe the final oriented diffusion flux on physical outer faces.
 * Heat follows the shared DiffFlux convention: F_E minus the momentum flux
 * projected on the average face velocity. Only owned planes are written.
 */
inline void capture_diffusion_surface_flux(
    const FluidState& state, const Grid& grid, int dir,
    const std::vector<FluidVector>& flux_buffer,
    const std::vector<double>& spec_flux_buffer)
{
    const auto storage = state.boundary_flux_capture;
    if (!storage) return;
    const arch::boundary::BoundaryFluxCaptureView view = storage->view();
    const auto geometry=GridMetrics::make_geometry_view(grid);
    const int stride = (dir == 0) ? 1 : ((dir == 1) ? grid.stride_y : grid.stride_z);
    const int species = state.GetNumSpecies();
    const int total_size = grid.GetTotalSize();
    const int lower[3]{grid.Is(), grid.Js(), grid.Ks()};
    const int upper[3]{grid.Ie(), grid.Je(), grid.Ke()};
    const int tangent_a = (dir + 1) % 3, tangent_b = (dir + 2) % 3;
    for (int side = 0; side < 2; ++side) {
        if (!view.stage[2 * dir + side]) continue;
        int face[3]{lower[0], lower[1], lower[2]};
        int cell[3]{lower[0], lower[1], lower[2]};
        face[dir] = side == 0 ? lower[dir] : upper[dir];
        cell[dir] = side == 0 ? lower[dir] : upper[dir] - 1;
        for (int ib = lower[tangent_b]; ib < upper[tangent_b]; ++ib) {
            cell[tangent_b] = face[tangent_b] = ib;
            for (int ia = lower[tangent_a]; ia < upper[tangent_a]; ++ia) {
                cell[tangent_a] = face[tangent_a] = ia;
                const int index = grid.GetIndex(cell[0], cell[1], cell[2])
                    + (side == 1 ? stride : 0);
                const FluidVector& lef = state.get(index - stride);
                const FluidVector& rig = state.get(index);
                double velocity[3]{
                    .5 * (lef.mom_u / lef.rho + rig.mom_u / rig.rho),
                    .5 * (lef.mom_v / lef.rho + rig.mom_v / rig.rho),
                    .5 * (lef.mom_w / lef.rho + rig.mom_w / rig.rho)};
                if(geometry.geometry!=DiffusionGeometry::Cartesian&&geometry.dim==1) {
                    const auto work=CurvilinearViscousStress::radial_face(
                        [&state](int c){return state.get(c);},index,geometry,face[0],0.);
                    if(!work.valid)throw std::runtime_error("Invalid radial diffusion boundary work observation");
                    for(int component=0;component<3;++component)velocity[component]=work.work_velocity[component];
                }
                if(geometry.geometry==DiffusionGeometry::Cylindrical&&geometry.dim==2) {
                    double shear=0.;
                    if(!CurvilinearViscousStress::angular_face_kinematics(geometry,dir,
                        face[0],face[1],face[2],lef.mom_w/lef.rho,rig.mom_w/rig.rho,
                        shear,velocity[2]))throw std::runtime_error("Invalid angular diffusion boundary work observation");
                }
                const FluidVector& flux = flux_buffer[index];
                const double heat = flux.eng - flux.mom_u * velocity[0]
                    - flux.mom_v * velocity[1] - flux.mom_w * velocity[2];
                arch::boundary::CaptureBoundaryFlux(view, dir,
                    face[0], face[1], face[2],
                    lower[0], upper[0], lower[1], upper[1], lower[2], upper[2],
                    flux,
                    species > 0 ? spec_flux_buffer.data() + index : nullptr,
                    species, total_size, heat);
            }
        }
    }
}


    // 2. Flux Computation

    /**
     * @brief Computes physical flux density for 1D diffusion along 'dir'
     */
    template <typename EosType>
    static void compute_fluxes(const FluidState& state, const EosType& eos, const Grid& grid, const SimConfig& config,
                               std::vector<FluidVector>& flux_out,
                               std::vector<double>& spec_flux_out,
                               int dir, bool capture_budget = true)
    {
        arch::state::HostFailure failure;
        int n_species = state.GetNumSpecies();
        int stride = (dir == 0) ? 1 : ((dir == 1) ? grid.stride_y : grid.stride_z);

        int k_end = (dir == 2) ? grid.Ke() + 1 : grid.Ke();
        int j_end = (dir == 1) ? grid.Je() + 1 : grid.Je();
        int i_end = (dir == 0) ? grid.Ie() + 1 : grid.Ie();

        const bool do_thermal = config.physics.diffusion.use_thermal_diffusion;
        const bool do_viscous = config.physics.diffusion.use_viscous_diffusion;
        const bool do_species = config.physics.diffusion.use_species_diffusion;

        if (!config.physics.diffusion.use_diffusion
            || (!do_thermal && !do_viscous && !do_species)) return;

        const SpeciesManager* species_manager = eos.get_species_manager();
        const SpeciesHostView species = species_manager
            ? species_manager->get_host_view() : SpeciesHostView{};
        const DiffusionConfigView diffusion_config =
            make_diffusion_config_view(config);
        const auto geometry_view=GridMetrics::make_geometry_view(grid);
        const DiffusionGeometry geometry =
            diffusion_geometry_from_name(grid.geometry);
        if (geometry == DiffusionGeometry::Unsupported) {
            std::cerr << "[FATAL ERROR] Unsupported diffusion geometry: "
                      << grid.geometry << std::endl;
            throw std::runtime_error("Unsupported diffusion geometry");
        }

        #pragma omp parallel
        {
            std::vector<double> Xi_L(n_species), Xi_R(n_species), Xi_face(n_species);
            std::vector<double> charge(species.size()), inverse_mass(species.size());

            #pragma omp for schedule(static)
            for (int k = grid.Ks(); k < k_end; ++k) {
                try {
                    for (int j = grid.Js(); j < j_end; ++j) {
                        for (int i = grid.Is(); i < i_end; ++i) {
                            int idx_R = grid.GetIndex(i, j, k);
                            int idx_L = idx_R - stride;

                            FluidVector U_L = state.get(idx_L);
                            FluidVector U_R = state.get(idx_R);
                            const auto read = [&state](int cell) { return state.get(cell); };
                            FluidVector F_diff;
                            DiffusionFaceProperties properties{};
                            const double spacing = diffusion_face_spacing(geometry_view,dir,i,j);
                            const DiffusionFaceStatus status = evaluate_diffusion_face(
                                U_L, U_R,
                                n_species > 0 ? state.mass_fractions.data() + idx_L : nullptr,
                                n_species > 0 ? state.mass_fractions.data() + idx_R : nullptr,
                                n_species, grid.GetTotalSize(), spacing,
                                eos, species, diffusion_config,
                                Xi_L.data(), Xi_R.data(), Xi_face.data(),
                                charge.data(), inverse_mass.data(), F_diff,
                                n_species > 0 ? spec_flux_out.data() + idx_R : nullptr,
                                grid.GetTotalSize(), do_viscous ? viscous_basis_rotation(
                                    geometry_view, dir, i, j)
                                    : ViscousBasisRotation{}, &properties);
                            if (!status.valid) {
                                throw std::runtime_error("Invalid diffusion state, heat capacity or transport coefficient");
                            }
                            if (!status.active) continue;
                            if(!replace_cartesian_viscous_flux(read,idx_R,geometry_view,dir,spacing,
                                eos,species,diffusion_config,Xi_L.data(),Xi_R.data(),properties,
                                charge.data(),inverse_mass.data(),F_diff))
                                throw std::runtime_error("Invalid Cartesian Newtonian traction or paired work");
                            const auto* controls=state.diffusion_boundary
                                ?state.diffusion_boundary->view().at(dir,i,j,k,
                                    grid.Is(),grid.Ie(),grid.Js(),grid.Je(),grid.Ks(),grid.Ke(),n_species):nullptr;
                            const bool radial=geometry_view.geometry!=DiffusionGeometry::Cartesian
                                &&geometry_view.dim==1;
                            const bool moment_work=radial||(geometry_view.geometry==DiffusionGeometry::Cylindrical&&geometry_view.dim==2);
                            double work_velocity[3]{.5*(U_L.mom_u/U_L.rho+U_R.mom_u/U_R.rho),
                                .5*(U_L.mom_v/U_L.rho+U_R.mom_v/U_R.rho),
                                .5*(U_L.mom_w/U_L.rho+U_R.mom_w/U_R.rho)};
                            if(!replace_curvilinear_viscous_flux(read,idx_R,geometry_view,dir,i,j,
                                diffusion_config,properties,F_diff,
                                moment_work?work_velocity:nullptr))
                                throw std::runtime_error("Invalid curvilinear Newtonian traction or paired work");
                            if (controls) {
                                const int coordinate[3]{i,j,k}, lower[3]{grid.Is(),grid.Js(),grid.Ks()};
                                arch::boundary::ApplyDiffusionBoundaryFlux(controls,
                                    coordinate[dir] == lower[dir] ? -1. : 1., U_L, U_R, F_diff,
                                    n_species ? spec_flux_out.data() + idx_R : nullptr, n_species, grid.GetTotalSize(),
                                    moment_work?work_velocity:nullptr);
                            }
                            flux_out[idx_R] = F_diff;
                        }
                    }

                } catch (...) { failure.capture_current(); }
            }
        }

        failure.rethrow();
        if (capture_budget) capture_diffusion_surface_flux(state, grid, dir, flux_out, spec_flux_out);
    }

    // 3. Geometric Source Terms

    /** Shared EOS/coefficient/source leaf; backends supply only memory views. */
    template <typename EosType, typename SpeciesAccessor, typename StateReader>
    ARCH_INLINE DiffusionFaceStatus evaluate_geometric_diffusion_cell(
        const FluidVector& U, const double* composition,
        const EosType& eos, const SpeciesAccessor& species,
        const DiffusionConfigView& config,
        const GridMetrics::GeometryView& grid, int i, int j, int k, double dt,
        double* charge, double* inverse_mass, FluidVector& delta,
        const StateReader& read_state)
    {
        DiffusionFaceStatus status{};
        if (!config.use_viscous_diffusion
            || grid.geometry == DiffusionGeometry::Cartesian) return status;
        const double rho = U.rho;
        if (!(rho > 0.0) || !std::isfinite(rho)) { status.valid = false; return status; }
        const double r = grid.GetCellCenterX(i);
        if (r == 0.0) return status;
        status.active = true;
        const double e_int = arch::state::recover(U).internal;
        const double temperature = eos.get_temperature(rho, e_int, composition);
        const auto coefficients = evaluate_diffusion_coefficients_from_eos(
            eos, species, config, rho, temperature, composition, charge, inverse_mass);
        status.valid = coefficients.valid;
        if (!status.valid) return status;
        double dynamic_viscosity=0.;
        if(!NewtonianViscousStress::dynamic_viscosity(rho,coefficients.nu_visc,dynamic_viscosity)) {
            status.valid=false;return status;
        }
        if (dynamic_viscosity == 0.) return status;
        NewtonianViscousStress::Tensor tau{};
        NewtonianViscousStress::Vector source{};
        if(grid.dim==1) {
            if(!CurvilinearViscousStress::radial_connection_source(read_state,grid.GetIndex(i,j,k),
                grid,i,coefficients.nu_visc,source)) {status.valid=false;return status;}
        } else if(!CurvilinearViscousStress::cell_stress(read_state,grid.GetIndex(i,j,k),
            grid,i,j,dynamic_viscosity,tau)||
           !CurvilinearViscousStress::connection_source(tau,grid,i,j,source)) {
            status.valid=false;return status;
        }
        delta.mom_u+=dt*source[0];delta.mom_v+=dt*source[1];delta.mom_w+=dt*source[2];
        return status;
    }

    /**
     * @brief Adds matched Stokes geometry in the retained physical components.
     */
    template <typename EosType>
    inline void add_geometric_sources(std::vector<FluidVector>& dU, const FluidState& state,
                                      const EosType& eos, const Grid& grid, const SimConfig& config, double dt)
    {
        arch::state::HostFailure failure;
        if (!config.physics.diffusion.use_viscous_diffusion) return;
        if (grid.geometry == "cartesian") return;

        int n_species = state.GetNumSpecies();
        const auto geometry = GridMetrics::make_geometry_view(grid);
        const auto diffusion_config = make_diffusion_config_view(config);
        const SpeciesManager* species_manager = eos.get_species_manager();
        const SpeciesHostView species = species_manager
            ? species_manager->get_host_view() : SpeciesHostView{};

        #pragma omp parallel
        {
            std::vector<double> Xi(n_species);
            std::vector<double> charge(species.size()), inverse_mass(species.size());
            #pragma omp for schedule(static)
            for (int k = grid.Ks(); k < grid.Ke(); ++k) {
                try {
                    for (int j = grid.Js(); j < grid.Je(); ++j) {
                        for (int i = grid.Is(); i < grid.Ie(); ++i) {
                            int idx = grid.GetIndex(i, j, k);
                            state.get_species_to_buffer(idx, Xi.data());
                            const auto status = evaluate_geometric_diffusion_cell(
                                state.get(idx), Xi.data(), eos, species, diffusion_config,
                                geometry, i, j, k, dt, charge.data(), inverse_mass.data(), dU[idx],
                                [&state](int cell) { return state.get(cell); });
                            if (!status.valid) {
                throw std::runtime_error("Invalid diffusion state, heat capacity or transport coefficient");
                            }
                        }
                    }

                } catch (...) { failure.capture_current(); }
            }
        }

        failure.rethrow();
    }

    // 4. Operator Evaluation (L_U)

    /**
     * @brief Computes L(U) = div( D grad U ) as a generic wrapper for time integrators
     */
    template <typename EosType>
    void compute_diffusion_operator(const FluidState& state, FluidState& L_U,
                                    const EosType& eos, const Grid& grid, const SimConfig& config)
    {
        (void)GridMetrics::make_geometry_view(grid); // fail before destination writes
        int n_spec = state.GetNumSpecies();

        #pragma omp parallel for schedule(static)
        for (int i = 0; i < grid.GetTotalSize(); ++i) {
            L_U.rho[i] = 0.0;
            L_U.mom_u[i] = 0.0;
            L_U.mom_v[i] = 0.0;
            L_U.mom_w[i] = 0.0;
            L_U.eng[i] = 0.0;
            for (int k = 0; k < n_spec; ++k) {
                L_U.X(k, i) = 0.0;
            }
        }

        const bool do_thermal = config.physics.diffusion.use_thermal_diffusion;
        const bool do_viscous = config.physics.diffusion.use_viscous_diffusion;
        const bool do_species = config.physics.diffusion.use_species_diffusion;

        if (!config.physics.diffusion.use_diffusion
            || (!do_thermal && !do_viscous && !do_species)) return;

        std::vector<FluidVector> dU(grid.GetTotalSize());
        std::vector<double> d_spec(grid.GetTotalSize() * n_spec, 0.0);
        std::vector<FluidVector> flux_buffer(grid.GetTotalSize());
        std::vector<double> spec_flux_buffer(grid.GetTotalSize() * n_spec, 0.0);

        for (int dir = 0; dir < grid.dim; ++dir) {
            std::fill(flux_buffer.begin(), flux_buffer.end(), FluidVector());
            std::fill(spec_flux_buffer.begin(), spec_flux_buffer.end(), 0.0);

            compute_fluxes(state, eos, grid, config, flux_buffer, spec_flux_buffer, dir, true);
            TimeIntegration::accumulate_divergence(dU, d_spec, flux_buffer, spec_flux_buffer, grid, 1.0, dir, n_spec);
        }

        add_geometric_sources(dU, state, eos, grid, config, 1.0);

        #pragma omp parallel for schedule(static)
        for (int i = 0; i < grid.GetTotalSize(); ++i) {
            L_U.rho[i] = dU[i].rho;
            L_U.mom_u[i] = dU[i].mom_u;
            L_U.mom_v[i] = dU[i].mom_v;
            L_U.mom_w[i] = dU[i].mom_w;
            L_U.eng[i] = dU[i].eng;
            for (int k = 0; k < n_spec; ++k) {
                L_U.X(k, i) = d_spec[k * grid.GetTotalSize() + i];
            }
        }
    }

    // 5. Adaptive Time Stepping

    struct HostDiffusionStateReader {
        const FluidState& state;
        FluidVector operator()(int cell) const { return state.get(cell); }
        double fraction(int species, int cell) const { return state.X(species, cell); }
    };

    /**
     * @brief Computes the explicit diffusion limit from current face transport.
     * Physical and inter-block ghosts must be complete before this call.
     */
    template <typename EosType>
    inline double adaptive_dt_diff(const FluidState &state, const EosType &eos, const Grid &grid, const SimConfig &config, double cfl_number)
    {
        arch::state::HostFailure failure;
        if (!config.physics.diffusion.use_diffusion)
            return diffusion_dt_sentinel();

        int n_species = state.GetNumSpecies();
        const auto reduction_spec = arch::reduction::minimum_spec(
            diffusion_dt_sentinel());
        auto global_reduction = arch::reduction::begin_reduction(reduction_spec);
        amr::CellLogicalKey seed_key{};
        seed_key.logical_i = -1;
        seed_key.component = 2;
        arch::reduction::combine_candidate(
            reduction_spec, global_reduction,
            {diffusion_dt_sentinel(), seed_key, true});

        const int ks = grid.Ks();
        const int ke = grid.Ke();
        const int js = grid.Js();
        const int je = grid.Je();
        const int nk = ke - ks;
        const int nj = je - js;

        const SpeciesManager* species_manager = eos.get_species_manager();
        const SpeciesHostView species = species_manager
            ? species_manager->get_host_view() : SpeciesHostView{};
        const DiffusionConfigView diffusion_config =
            make_diffusion_config_view(config);
        const DiffusionGeometry geometry =
            diffusion_geometry_from_name(grid.geometry);
        const auto geometry_view = GridMetrics::make_geometry_view(grid);
        if (geometry == DiffusionGeometry::Unsupported) {
            std::cerr << "[FATAL ERROR] Unsupported diffusion geometry: "
                      << grid.geometry << std::endl;
            throw std::runtime_error("Unsupported diffusion geometry");
        }

    #pragma omp parallel
        {
            std::vector<double> Xi_cache(n_species), Xi_neighbour(n_species), Xi_face(n_species);
            std::vector<double> charge(species.size()), inverse_mass(species.size());
            auto local_reduction = arch::reduction::begin_reduction(
                reduction_spec);

    #pragma omp for schedule(static)
            for (int kj = 0; kj < nk * nj; ++kj)
            {
                try {
                    int k = ks + kj / nj;
                    int j = js + kj % nj;
                    for (int i = grid.Is(); i < grid.Ie(); ++i)
                    {
                        int idx = grid.GetIndex(i, j, k);
                        const DiffusionDtCandidate candidate =
                            evaluate_diffusion_dt_candidate(
                                state.get(idx),
                                n_species > 0
                                    ? state.mass_fractions.data() + idx : nullptr,
                                n_species, grid.GetTotalSize(), eos, species,
                                diffusion_config, geometry_view, i, j, k,
                                Xi_cache.data(), Xi_neighbour.data(), Xi_face.data(),
                                charge.data(), inverse_mass.data(), HostDiffusionStateReader{state});
                        if (!candidate.valid) {
                throw std::runtime_error("Invalid diffusion state, heat capacity or transport coefficient");
                        }
                        amr::CellLogicalKey cell_key{};
                        cell_key.logical_i = i;
                        cell_key.logical_j = j;
                        cell_key.logical_k = k;
                        cell_key.component = 2;
                        arch::reduction::combine_candidate(
                            reduction_spec, local_reduction,
                            {candidate.value, cell_key, true});
                    }

                } catch (...) { failure.capture_current(); }
            }
    #pragma omp critical(diffusion_dt_reduction)
            {
                arch::reduction::combine_state(
                    reduction_spec, global_reduction, local_reduction);
            }
        }
        failure.rethrow();
    const auto result = arch::reduction::finalize_reduction(
            reduction_spec, global_reduction);
        if (result.status != arch::reduction::ReductionStatus::Ok)
            throw std::runtime_error("Invalid diffusion dt reduction");
        return cfl_number * finalize_raw_diffusion_dt(result.value);
    }
}
