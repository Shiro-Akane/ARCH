/**
 * @file DiffFlux.h
 * @brief Computes diffusion fluxes and operators for explicit time integration.
 * *
 * * Workflow:
 * * 1. Calculate diffusion coefficients (viscosity, thermal conductivity, species diffusivity).
 * * 2. Compute face-centered gradients for the requested diffusion dimension.
 * * 3. Generate diffusion fluxes and geometric source terms (for momentum).
 * * 4. Combine multi-dimensional fluxes into a generic L(U) operator.
 */

#pragma once

#include "../../data/FluidState.h"
#include "../../grid/Grid.h"
#include "../../data/GlobalDefs.h"
#include "../integrator/TimeIntegratorHelper.h"
#include <type_traits>
#include <cmath>
#include <vector>
#include <algorithm>
#include "../../physics/eos/eos_state.h"
#include "../../physics/diffusionCoe/diffusion_math.hpp"

// =========================================================
// ==================== DiffFlux Namespace =================
// =========================================================

namespace DiffFlux
{
    // =========================================================
    // 1. SFINAE Checks and Coefficient Extraction
    // =========================================================
    /**
     * @brief Retrieves diffusion coefficients. Uses eos_state_t and diffusion_math directly.
     */
    template <typename EosType>
    inline void get_coeffs(const EosType& eos, const SimConfig& config,
                           double rho, double T, const double* Xi,
                           double& nu_visc, double& alpha_therm, double& D_spec)
    {
        // =========================================================
        // 1. Initialize and Evaluate State
        // =========================================================
        eos_state_t state;
        state.rho = rho;
        state.T = T;
        state.Xi = Xi;
        
        eos.evaluate_state(state);

        const SpeciesManager* specs = eos.get_species_manager();
        bool is_stellar_eos = (specs && specs->count() > 0 && state.xne > 0.0);

        // =========================================================
        // 2. Process Override Config Parameters
        // =========================================================
        if (config.physics.diffusion.nu_visc > 0 || config.physics.diffusion.alpha_therm > 0) {
            if (is_stellar_eos) {
                std::cerr << "[FATAL ERROR] Unexpected override values (nu_visc/alpha_therm) found in .par file while using an astrophysical EOS (e.g., HelmEos). Please remove them to enable autonomous stellar diffusion, or disable the stellar network." << std::endl;
                std::abort();
            }
            nu_visc = config.physics.diffusion.nu_visc;
            alpha_therm = config.physics.diffusion.alpha_therm;
            D_spec = config.physics.diffusion.D_spec;
            return;
        }

        // =========================================================
        // 3. Compute Stellar Transport Coefficients
        // =========================================================
        if (is_stellar_eos) {
            std::vector<double> zion(specs->count());
            std::vector<double> aion_inv(specs->count());
            for (int k = 0; k < specs->count(); ++k) {
                zion[k] = specs->get_Z(k);
                aion_inv[k] = 1.0 / specs->get_A(k);
            }

            double cond = ConductivityMath::compute_stellar_conductivity(
                state.T, state.rho, state.pele, state.xne, state.eta, 
                state.Xi, specs->count(),
                zion.data(), aion_inv.data()
            );

            double cv_val = std::max(state.cv, 1e-12);
            alpha_therm = cond / (state.rho * cv_val);
            nu_visc = 0.0;
            D_spec = 0.0;
        } else {
            nu_visc = config.physics.diffusion.nu_visc;
            alpha_therm = config.physics.diffusion.alpha_therm;
            D_spec = config.physics.diffusion.D_spec;
        }
    }

    // =========================================================
    // 2. Flux Computation
    // =========================================================

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

        if (!do_thermal && !do_viscous && !do_species) return;

        #pragma omp parallel
        {
            std::vector<double> Xi_L(n_species), Xi_R(n_species), Xi_face(n_species);
            
            #pragma omp for schedule(static)
            for (int k = grid.Ks(); k < k_end; ++k) {
                for (int j = grid.Js(); j < j_end; ++j) {
                    for (int i = grid.Is(); i < i_end; ++i) {
                        int idx_R = grid.GetIndex(i, j, k);
                        int idx_L = idx_R - stride;

                        double rho_L = state.rho[idx_L];
                        double rho_R = state.rho[idx_R];
                        if (rho_L < 1e-12 || rho_R < 1e-12) continue;

                        FluidVector U_L = state.get(idx_L);
                        FluidVector U_R = state.get(idx_R);
                        
                        double uL_sq = (U_L.mom_u * U_L.mom_u + U_L.mom_v * U_L.mom_v + U_L.mom_w * U_L.mom_w) / (rho_L * rho_L);
                        double uR_sq = (U_R.mom_u * U_R.mom_u + U_R.mom_v * U_R.mom_v + U_R.mom_w * U_R.mom_w) / (rho_R * rho_R);
                        
                        double e_int_L = (U_L.eng - 0.5 * rho_L * uL_sq) / rho_L;
                        double e_int_R = (U_R.eng - 0.5 * rho_R * uR_sq) / rho_R;

                        state.get_species_to_buffer(idx_L, Xi_L.data());
                        state.get_species_to_buffer(idx_R, Xi_R.data());

                        double T_L = eos.get_temperature(rho_L, e_int_L, Xi_L.data());
                        double T_R = eos.get_temperature(rho_R, e_int_R, Xi_R.data());

                        double rho_f = 0.5 * (rho_L + rho_R);
                        double T_f = 0.5 * (T_L + T_R);
                        for(int s=0; s<n_species; ++s) Xi_face[s] = 0.5*(Xi_L[s] + Xi_R[s]);

                        double nu, alpha, D;
                        get_coeffs(eos, config, rho_f, T_f, Xi_face.data(), nu, alpha, D);

                        // Calculate physical distance between cell centers
                        double dx1 = 1.0;
                        if (grid.geometry == "cartesian") {
                            dx1 = (dir == 0) ? grid.dx1 : ((dir == 1) ? grid.dx2 : grid.dx3);
                        } else if (grid.geometry == "cylindrical") {
                            if (dir == 0) dx1 = grid.dx1;
                            else if (dir == 1) {
                                if (grid.dim == 2) dx1 = grid.GetCellCenterX(i) * grid.dx2;
                                else dx1 = grid.dx2;
                            }
                            else if (dir == 2) dx1 = grid.GetCellCenterX(i) * grid.dx3;
                        } else if (grid.geometry == "spherical") {
                            if (dir == 0) dx1 = grid.dx1;
                            else if (dir == 1) {
                                dx1 = grid.GetCellCenterX(i) * grid.dx2; // theta direction uses cell radius
                            }
                            else if (dir == 2) {
                                double theta_c = grid.GetCellCenterY(j);
                                dx1 = grid.GetCellCenterX(i) * std::sin(theta_c) * grid.dx3;
                            }
                        }

                        // Gradient calculations
                        double dT_dx = (T_R - T_L) / dx1;
                        double q_therm = do_thermal ? (-alpha * rho_f * std::max(eos.get_cv(rho_f, T_f, Xi_face.data()), 1e-12) * dT_dx) : 0.0;
                        
                        FluidVector F_diff;
                        F_diff.rho = 0.0;
                        
                        double v_face_x = 0.5 * (U_L.mom_u / rho_L + U_R.mom_u / rho_R);
                        double v_face_y = 0.5 * (U_L.mom_v / rho_L + U_R.mom_v / rho_R);
                        double v_face_z = 0.5 * (U_L.mom_w / rho_L + U_R.mom_w / rho_R);
                        
                        if (do_viscous) {
                            double dvx_dx = (U_R.mom_u / rho_R - U_L.mom_u / rho_L) / dx1;
                            double dvy_dx = (U_R.mom_v / rho_R - U_L.mom_v / rho_L) / dx1;
                            double dvz_dx = (U_R.mom_w / rho_R - U_L.mom_w / rho_L) / dx1;
                            
                            F_diff.mom_u = -nu * rho_f * dvx_dx;
                            F_diff.mom_v = -nu * rho_f * dvy_dx;
                            F_diff.mom_w = -nu * rho_f * dvz_dx;
                        }

                        // Energy flux = thermal conduction + viscous dissipation work
                        F_diff.eng = q_therm + (F_diff.mom_u * v_face_x + F_diff.mom_v * v_face_y + F_diff.mom_w * v_face_z);
                        
                        flux_out[idx_R] = F_diff;
                        
                        if (do_species) {
                            for (int s = 0; s < n_species; ++s) {
                                double dX_dx = (Xi_R[s] - Xi_L[s]) / dx1;
                                spec_flux_out[s * grid.GetTotalSize() + idx_R] = -rho_f * D * dX_dx;
                            }
                        }
                    }
                }
            }
        }
    }

    // =========================================================
    // 3. Geometric Source Terms
    // =========================================================

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

    // =========================================================
    // 4. Operator Evaluation (L_U)
    // =========================================================

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

        if (!do_thermal && !do_viscous && !do_species) return;

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

    // =========================================================
    // 5. Adaptive Time Stepping
    // =========================================================

    /**
     * @brief Computes explicit time step limit for diffusion (dt = dx1^2 / (2 * max_coeff))
     */
    template <typename EosType>
    inline double adaptive_dt_diff(const FluidState &state, const EosType &eos, const Grid &grid, const SimConfig &config, double cfl_number)
    {
        if (!config.physics.diffusion.use_diffusion) return 1e10; 
        
        int n_species = state.GetNumSpecies();
        double min_dt = 1e10;

        const int ks = grid.Ks();
        const int ke = grid.Ke();
        const int js = grid.Js();
        const int je = grid.Je();
        const int nk = ke - ks;
        const int nj = je - js;

        const bool do_thermal = config.physics.diffusion.use_thermal_diffusion;
        const bool do_viscous = config.physics.diffusion.use_viscous_diffusion;
        const bool do_species = config.physics.diffusion.use_species_diffusion;

    #pragma omp parallel
        {
            std::vector<double> Xi_cache(n_species);
            double local_min_dt = 1e10;

    #pragma omp for schedule(static)
            for (int kj = 0; kj < nk * nj; ++kj)
            {
                int k = ks + kj / nj;
                int j = js + kj % nj;
                for (int i = grid.Is(); i < grid.Ie(); ++i)
                {
                    int idx = grid.GetIndex(i, j, k);
                    FluidVector U = state.get(idx);
                    double rho = U.rho;

                    if (rho < 1e-12)
                        continue;

                    for (int s = 0; s < n_species; ++s)
                        Xi_cache[s] = state.X(s, idx);

                    double e_int = (U.eng - 0.5 * rho * (std::pow(U.mom_u/rho, 2) + std::pow(U.mom_v/rho, 2) + std::pow(U.mom_w/rho, 2))) / rho;
                    double T = eos.get_temperature(rho, e_int, Xi_cache.data());

                    double nu = 0.0, alpha = 0.0, D = 0.0;
                    get_coeffs(eos, config, rho, T, Xi_cache.data(), nu, alpha, D);

                    double max_coeff = 0.0;
                    if (do_thermal) max_coeff = std::max(max_coeff, alpha);
                    if (do_viscous) max_coeff = std::max(max_coeff, nu);
                    if (do_species) max_coeff = std::max(max_coeff, D);

                    if (max_coeff > 1e-12) {
                        double inv_dt_sum = 0.0;
                        for (int dir = 0; dir < grid.dim; ++dir) {
                            double dx1 = 1.0;
                            if (grid.geometry == "cartesian") {
                                dx1 = (dir == 0) ? grid.dx1 : ((dir == 1) ? grid.dx2 : grid.dx3);
                            } else if (grid.geometry == "cylindrical") {
                                if (dir == 0) dx1 = grid.dx1;
                                else if (dir == 1) dx1 = (grid.dim == 2) ? grid.GetCellCenterX(i) * grid.dx2 : grid.dx2;
                                else if (dir == 2) dx1 = grid.GetCellCenterX(i) * grid.dx3;
                            } else if (grid.geometry == "spherical") {
                                if (dir == 0) dx1 = grid.dx1;
                                else if (dir == 1) dx1 = grid.GetCellCenterX(i) * grid.dx2;
                                else if (dir == 2) {
                                    double theta_c = grid.GetCellCenterY(j);
                                    dx1 = grid.GetCellCenterX(i) * std::sin(theta_c) * grid.dx3;
                                }
                            }
                            inv_dt_sum += 2.0 * max_coeff / (dx1 * dx1);
                        }
                        
                        double cell_dt = 1.0 / std::max(inv_dt_sum, 1e-20);
                        local_min_dt = std::min(local_min_dt, cell_dt);
                    }
                }
            }
    #pragma omp critical
            {
                min_dt = std::min(min_dt, local_min_dt);
            }
        }
        return cfl_number * min_dt;
    }
}
