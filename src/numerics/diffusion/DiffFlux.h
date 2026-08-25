/**
 * @file DiffFlux.h
 * @brief Computes diffusion fluxes and operators for explicit time integration.
 *
 * Workflow:
 * 1. Calculate diffusion coefficients (viscosity, thermal conductivity, species diffusivity).
 * 2. Compute face-centered gradients for the requested diffusion dimension.
 * 3. Generate diffusion fluxes and geometric source terms (for momentum).
 * 4. Combine multi-dimensional fluxes into a generic L(U) operator.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "../integrator/TimeIntegratorHelper.h"

#include "../../data/FluidState.h"
#include "../../data/GlobalDefs.h"
#include "../../driver/ReductionSpec.h"
#include "../../grid/Grid.h"
#include "../../physics/diffusionCoe/diffusion_math.hpp"
#include "../../physics/eos/eos_state.h"
#include "../../physics/species/Species.h"

// Diffusive flux and operator assembly.

namespace DiffFlux
{
    enum class DiffusionGeometry : int
    {
        Cartesian = 0,
        Cylindrical = 1,
        Spherical = 2,
        Unsupported = 3,
    };

    struct DiffusionConfigView
    {
        bool use_diffusion = false;
        bool use_thermal_diffusion = false;
        bool use_viscous_diffusion = false;
        bool use_species_diffusion = false;
        double nu_visc = 0.0;
        double alpha_therm = 0.0;
        double D_spec = 0.0;
    };

    struct DiffusionCoefficients
    {
        double nu_visc = 0.0;
        double alpha_therm = 0.0;
        double D_spec = 0.0;
        bool valid = true;
    };

    struct DiffusionFaceStatus
    {
        bool active = false;
        bool valid = true;
    };

    struct DiffusionDtCandidate
    {
        double value = 1.0e10;
        bool valid = true;
    };

    static_assert(std::is_standard_layout_v<DiffusionConfigView>);
    static_assert(std::is_trivially_copyable_v<DiffusionConfigView>);
    static_assert(std::is_standard_layout_v<DiffusionCoefficients>);
    static_assert(std::is_trivially_copyable_v<DiffusionCoefficients>);
    static_assert(std::is_standard_layout_v<DiffusionFaceStatus>);
    static_assert(std::is_trivially_copyable_v<DiffusionFaceStatus>);
    static_assert(std::is_standard_layout_v<DiffusionDtCandidate>);
    static_assert(std::is_trivially_copyable_v<DiffusionDtCandidate>);

    inline DiffusionConfigView make_diffusion_config_view(const SimConfig& config)
    {
        return {
            config.physics.diffusion.use_diffusion,
            config.physics.diffusion.use_thermal_diffusion,
            config.physics.diffusion.use_viscous_diffusion,
            config.physics.diffusion.use_species_diffusion,
            config.physics.diffusion.nu_visc,
            config.physics.diffusion.alpha_therm,
            config.physics.diffusion.D_spec};
    }

    inline DiffusionGeometry diffusion_geometry_from_name(const std::string& geometry)
    {
        if (geometry == "cartesian") return DiffusionGeometry::Cartesian;
        if (geometry == "cylindrical") return DiffusionGeometry::Cylindrical;
        if (geometry == "spherical") return DiffusionGeometry::Spherical;
        return DiffusionGeometry::Unsupported;
    }

    ARCH_INLINE constexpr double diffusion_dt_sentinel()
    {
        return 1.0e10;
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
        return !(rho_left < 1.0e-12 || rho_right < 1.0e-12);
    }

    ARCH_INLINE double diffusion_face_spacing(
        DiffusionGeometry geometry, int dim, int direction,
        double dx1, double dx2, double dx3, double radius, double theta)
    {
        if (geometry == DiffusionGeometry::Cartesian)
            return direction == 0 ? dx1 : (direction == 1 ? dx2 : dx3);
        if (geometry == DiffusionGeometry::Cylindrical) {
            if (direction == 0) return dx1;
            if (direction == 1) return dim == 2 ? radius * dx2 : dx2;
            return radius * dx3;
        }
        if (geometry == DiffusionGeometry::Spherical) {
            if (direction == 0) return dx1;
            if (direction == 1) return radius * dx2;
            return radius * std::sin(theta) * dx3;
        }
        return 0.0;
    }

    ARCH_INLINE DiffusionCoefficients evaluate_diffusion_coefficients(
        const eos_state_t& state, const DiffusionConfigView& config,
        int species_count, const double* charge, const double* inverse_mass)
    {
        DiffusionCoefficients coefficients{};
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
            const double cv_value = std::max(state.cv, 1.0e-12);
            coefficients.alpha_therm =
                conductivity / (state.rho * cv_value);
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

    ARCH_INLINE void assemble_diffusion_face_flux(
        const FluidVector& left, const FluidVector& right,
        double temperature_left, double temperature_right,
        double face_density, const double* species_left,
        const double* species_right, int species_count, double spacing,
        double heat_capacity, const DiffusionCoefficients& coefficients,
        const DiffusionConfigView& config, FluidVector& flux,
        double* species_flux, int species_flux_stride)
    {
        const double temperature_gradient =
            (temperature_right - temperature_left) / spacing;
        const double thermal_flux = config.use_thermal_diffusion
            ? (-coefficients.alpha_therm * face_density
               * std::max(heat_capacity, 1.0e-12) * temperature_gradient)
            : 0.0;

        flux.rho = 0.0;
        const double velocity_face_x =
            0.5 * (left.mom_u / left.rho + right.mom_u / right.rho);
        const double velocity_face_y =
            0.5 * (left.mom_v / left.rho + right.mom_v / right.rho);
        const double velocity_face_z =
            0.5 * (left.mom_w / left.rho + right.mom_w / right.rho);

        if (config.use_viscous_diffusion) {
            const double velocity_gradient_x =
                (right.mom_u / right.rho - left.mom_u / left.rho) / spacing;
            const double velocity_gradient_y =
                (right.mom_v / right.rho - left.mom_v / left.rho) / spacing;
            const double velocity_gradient_z =
                (right.mom_w / right.rho - left.mom_w / left.rho) / spacing;
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

    template <typename EosType, typename SpeciesAccessor>
    ARCH_INLINE DiffusionFaceStatus evaluate_diffusion_face(
        const FluidVector& left, const FluidVector& right,
        const double* species_left_source, const double* species_right_source,
        int species_count, int species_source_stride, double spacing,
        const EosType& eos, const SpeciesAccessor& species,
        const DiffusionConfigView& config,
        double* species_left, double* species_right, double* species_face,
        double* charge, double* inverse_mass,
        FluidVector& flux, double* species_flux, int species_flux_stride)
    {
        if (!diffusion_face_is_active(left.rho, right.rho))
            return {false, true};

        const double velocity_sq_left =
            (left.mom_u * left.mom_u + left.mom_v * left.mom_v
             + left.mom_w * left.mom_w) / (left.rho * left.rho);
        const double velocity_sq_right =
            (right.mom_u * right.mom_u + right.mom_v * right.mom_v
             + right.mom_w * right.mom_w) / (right.rho * right.rho);
        const double internal_left =
            (left.eng - 0.5 * left.rho * velocity_sq_left) / left.rho;
        const double internal_right =
            (right.eng - 0.5 * right.rho * velocity_sq_right) / right.rho;

        for (int index = 0; index < species_count; ++index) {
            species_left[index] = species_left_source[index * species_source_stride];
            species_right[index] = species_right_source[index * species_source_stride];
        }
        const double temperature_left =
            eos.get_temperature(left.rho, internal_left, species_left);
        const double temperature_right =
            eos.get_temperature(right.rho, internal_right, species_right);
        const double face_density = 0.5 * (left.rho + right.rho);
        const double face_temperature =
            0.5 * (temperature_left + temperature_right);
        for (int index = 0; index < species_count; ++index)
            species_face[index] = 0.5 * (species_left[index] + species_right[index]);

        const DiffusionCoefficients coefficients =
            evaluate_diffusion_coefficients_from_eos(
                eos, species, config, face_density, face_temperature,
                species_face, charge, inverse_mass);
        if (!coefficients.valid) return {true, false};
        const double heat_capacity = config.use_thermal_diffusion
            ? eos.get_cv(face_density, face_temperature, species_face) : 0.0;
        assemble_diffusion_face_flux(
            left, right, temperature_left, temperature_right, face_density,
            species_left, species_right, species_count, spacing,
            heat_capacity, coefficients, config, flux,
            species_flux, species_flux_stride);
        return {true, true};
    }

    ARCH_INLINE double raw_forward_euler_candidate(
        double maximum_coefficient, const double* spacing, int dimension)
    {
        if (!(maximum_coefficient > 1.0e-12))
            return diffusion_dt_sentinel();
        double inverse_dt = 0.0;
        for (int direction = 0; direction < dimension; ++direction)
            inverse_dt += 2.0 * maximum_coefficient
                / (spacing[direction] * spacing[direction]);
        return 1.0 / std::max(inverse_dt, 1.0e-20);
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

    template <typename EosType, typename SpeciesAccessor>
    ARCH_INLINE DiffusionDtCandidate evaluate_diffusion_dt_candidate(
        const FluidVector& value, const double* species_source,
        int species_count, int species_source_stride,
        const EosType& eos, const SpeciesAccessor& species,
        const DiffusionConfigView& config, int dimension,
        DiffusionGeometry geometry, double dx1, double dx2, double dx3,
        double radius, double theta, double* composition,
        double* charge, double* inverse_mass)
    {
        if (geometry == DiffusionGeometry::Unsupported)
            return {diffusion_dt_sentinel(), false};
        if (value.rho < 1.0e-12) return {};
        for (int index = 0; index < species_count; ++index)
            composition[index] = species_source[index * species_source_stride];
        const double internal =
            (value.eng - 0.5 * value.rho
                 * (std::pow(value.mom_u / value.rho, 2)
                    + std::pow(value.mom_v / value.rho, 2)
                    + std::pow(value.mom_w / value.rho, 2))) / value.rho;
        const double temperature =
            eos.get_temperature(value.rho, internal, composition);
        const DiffusionCoefficients coefficients =
            evaluate_diffusion_coefficients_from_eos(
                eos, species, config, value.rho, temperature,
                composition, charge, inverse_mass);
        if (!coefficients.valid) return {diffusion_dt_sentinel(), false};

        double maximum = 0.0;
        if (config.use_thermal_diffusion)
            maximum = std::max(maximum, coefficients.alpha_therm);
        if (config.use_viscous_diffusion)
            maximum = std::max(maximum, coefficients.nu_visc);
        if (config.use_species_diffusion)
            maximum = std::max(maximum, coefficients.D_spec);
        double spacing[3] = {1.0, 1.0, 1.0};
        for (int direction = 0; direction < dimension; ++direction) {
            spacing[direction] = diffusion_face_spacing(
                geometry, dimension, direction, dx1, dx2, dx3,
                radius, theta);
            if (!(spacing[direction] > 0.0))
                return {diffusion_dt_sentinel(), false};
        }
        return {raw_forward_euler_candidate(maximum, spacing, dimension), true};
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
            std::cerr << "[FATAL ERROR] Unexpected override values (nu_visc/alpha_therm) found in .par file while using an astrophysical EOS (e.g., HelmEos). Please remove them to enable autonomous stellar diffusion, or disable the stellar network." << std::endl;
            std::abort();
        }
        nu_visc = coefficients.nu_visc;
        alpha_therm = coefficients.alpha_therm;
        D_spec = coefficients.D_spec;
    }

    // 2. Flux Computation

    /**
     * @brief Computes physical flux density for 1D diffusion along 'dir'
     */
    template <typename EosType>
    static void compute_fluxes(const FluidState& state, const EosType& eos, const Grid& grid, const SimConfig& config,
                               std::vector<FluidVector>& flux_out,
                               std::vector<double>& spec_flux_out,
                               int dir)
    {
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
        const DiffusionGeometry geometry =
            diffusion_geometry_from_name(grid.geometry);
        if (geometry == DiffusionGeometry::Unsupported) {
            std::cerr << "[FATAL ERROR] Unsupported diffusion geometry: "
                      << grid.geometry << std::endl;
            std::abort();
        }

        #pragma omp parallel
        {
            std::vector<double> Xi_L(n_species), Xi_R(n_species), Xi_face(n_species);
            std::vector<double> charge(species.size()), inverse_mass(species.size());

            #pragma omp for schedule(static)
            for (int k = grid.Ks(); k < k_end; ++k) {
                for (int j = grid.Js(); j < j_end; ++j) {
                    for (int i = grid.Is(); i < i_end; ++i) {
                        int idx_R = grid.GetIndex(i, j, k);
                        int idx_L = idx_R - stride;

                        FluidVector U_L = state.get(idx_L);
                        FluidVector U_R = state.get(idx_R);
                        FluidVector F_diff;
                        const double spacing = diffusion_face_spacing(
                            geometry, grid.dim, dir, grid.dx1, grid.dx2, grid.dx3,
                            grid.GetCellCenterX(i), grid.GetCellCenterY(j));
                        const DiffusionFaceStatus status = evaluate_diffusion_face(
                            U_L, U_R,
                            n_species > 0 ? state.mass_fractions.data() + idx_L : nullptr,
                            n_species > 0 ? state.mass_fractions.data() + idx_R : nullptr,
                            n_species, grid.GetTotalSize(), spacing,
                            eos, species, diffusion_config,
                            Xi_L.data(), Xi_R.data(), Xi_face.data(),
                            charge.data(), inverse_mass.data(), F_diff,
                            n_species > 0 ? spec_flux_out.data() + idx_R : nullptr,
                            grid.GetTotalSize());
                        if (!status.active) continue;
                        if (!status.valid) {
                            std::cerr << "[FATAL ERROR] Unexpected override values (nu_visc/alpha_therm) found in .par file while using an astrophysical EOS (e.g., HelmEos). Please remove them to enable autonomous stellar diffusion, or disable the stellar network." << std::endl;
                            std::abort();
                        }
                        flux_out[idx_R] = F_diff;
                    }
                }
            }
        }
    }

    // 3. Geometric Source Terms

    /**
     * @brief Adds vector-Laplacian geometric source terms for momentum diffusion.
     */
    template <typename EosType>
    inline void add_geometric_sources(std::vector<FluidVector>& dU, const FluidState& state,
                                      const EosType& eos, const Grid& grid, const SimConfig& config, double dt)
    {
        if (!config.physics.diffusion.use_viscous_diffusion) return;
        if (grid.geometry == "cartesian") return;

        int n_species = state.GetNumSpecies();

        #pragma omp parallel
        {
            std::vector<double> Xi(n_species);
            #pragma omp for schedule(static)
            for (int k = grid.Ks(); k < grid.Ke(); ++k) {
                for (int j = grid.Js(); j < grid.Je(); ++j) {
                    for (int i = grid.Is(); i < grid.Ie(); ++i) {
                        int idx = grid.GetIndex(i, j, k);
                        double rho = state.rho[idx];
                        if (rho < 1e-12) continue;

                        PointCoords coords = grid.GetPhysicalCoords(i, j, k);
                        double r = coords.r;
                        if (grid.geometry == "cylindrical") r = coords.r_cy;

                        if (r < 1e-14) continue;

                        state.get_species_to_buffer(idx, Xi.data());
                        FluidVector U = state.get(idx);

                        double u_sq = (U.mom_u * U.mom_u + U.mom_v * U.mom_v + U.mom_w * U.mom_w) / (rho * rho);
                        double e_int = (U.eng - 0.5 * rho * u_sq) / rho;
                        double T = eos.get_temperature(rho, e_int, Xi.data());

                        double nu, alpha, D;
                        get_coeffs(eos, config, rho, T, Xi.data(), nu, alpha, D);

                        double v_r = U.mom_u / rho;

                        if (grid.geometry == "cylindrical") {
                            dU[idx].mom_u += dt * (-nu * rho * v_r) / (r * r);
                            if (grid.dim >= 2) {
                                double v_phi = (grid.dim == 2) ? (U.mom_v / rho) : (U.mom_w / rho);
                                double dU_phi = dt * (-nu * rho * v_phi) / (r * r);
                                if (grid.dim == 2) dU[idx].mom_v += dU_phi;
                                else dU[idx].mom_w += dU_phi;
                            }
                        } else if (grid.geometry == "spherical") {
                            dU[idx].mom_u += dt * (-2.0 * nu * rho * v_r) / (r * r);
                            if (grid.dim >= 2) {
                                double v_theta = U.mom_v / rho;
                                double sin_theta = std::max(std::sin(coords.theta), 1e-14);
                                dU[idx].mom_v += dt * (-nu * rho * v_theta) / (r * r * sin_theta * sin_theta);

                                if (grid.dim == 3) {
                                    double v_phi = U.mom_w / rho;
                                    dU[idx].mom_w += dt * (-nu * rho * v_phi) / (r * r * sin_theta * sin_theta);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // 4. Operator Evaluation (L_U)

    /**
     * @brief Computes L(U) = div( D grad U ) as a generic wrapper for time integrators
     */
    template <typename EosType>
    void compute_diffusion_operator(const FluidState& state, FluidState& L_U,
                                    const EosType& eos, const Grid& grid, const SimConfig& config)
    {
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

            compute_fluxes(state, eos, grid, config, flux_buffer, spec_flux_buffer, dir);
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

    /**
     * @brief Computes explicit time step limit for diffusion (dt = dx1^2 / (2 * max_coeff))
     */
    template <typename EosType>
    inline double adaptive_dt_diff(const FluidState &state, const EosType &eos, const Grid &grid, const SimConfig &config, double cfl_number)
    {
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
        if (geometry == DiffusionGeometry::Unsupported) {
            std::cerr << "[FATAL ERROR] Unsupported diffusion geometry: "
                      << grid.geometry << std::endl;
            std::abort();
        }

    #pragma omp parallel
        {
            std::vector<double> Xi_cache(n_species);
            std::vector<double> charge(species.size()), inverse_mass(species.size());
            auto local_reduction = arch::reduction::begin_reduction(
                reduction_spec);

    #pragma omp for schedule(static)
            for (int kj = 0; kj < nk * nj; ++kj)
            {
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
                            diffusion_config, grid.dim, geometry,
                            grid.dx1, grid.dx2, grid.dx3,
                            grid.GetCellCenterX(i), grid.GetCellCenterY(j),
                            Xi_cache.data(), charge.data(), inverse_mass.data());
                    if (!candidate.valid) {
                        std::cerr << "[FATAL ERROR] Unexpected override values (nu_visc/alpha_therm) found in .par file while using an astrophysical EOS (e.g., HelmEos). Please remove them to enable autonomous stellar diffusion, or disable the stellar network." << std::endl;
                        std::abort();
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
            }
    #pragma omp critical(diffusion_dt_reduction)
            {
                arch::reduction::combine_state(
                    reduction_spec, global_reduction, local_reduction);
            }
        }
        const auto result = arch::reduction::finalize_reduction(
            reduction_spec, global_reduction);
        if (result.status != arch::reduction::ReductionStatus::Ok)
            throw std::runtime_error("Invalid diffusion dt reduction");
        return cfl_number * finalize_raw_diffusion_dt(result.value);
    }
}
