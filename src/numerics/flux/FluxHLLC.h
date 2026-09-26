/**
 * @file FluxHLLC.h
 * @brief HLLC Flux Scheme (Toro's Algorithm).
 * Restores the contact discontinuity (Star Wave S*).
 */

/**
 * Workflow:
 * 1. Reconstruct left and right face states using the configured limiter policy.
 * 2. Evaluate the named Riemann flux consistently in every active dimension.
 * 3. Store fluid and species face fluxes for the caller's divergence update.
 * Time integration and coarse-fine flux registration belong to the caller,
 * not to this flux policy; the per-face mathematics is shared by backends.
 */

#pragma once

#include <algorithm>
#include <vector>
#ifdef _OPENMP
#include <omp.h>
#endif

#include "numerics/flux/FluxFunctions.h"
#include "numerics/flux/InvariantDomainFlux.h"

#include "numerics/reconstruction/AMRInterfaceReconstruction.h"

template <typename ReconstructPolicy>
struct FluxHLLC
{
    static std::string name() { return "HLLC + " + ReconstructPolicy::name(); }
    static constexpr int NG = ReconstructPolicy::NG;

    // Construct the flux in an HLLC star region from its contact speed and
    // pressure. This is algebraically F_K + S_K*(U*_K - U_K), but avoids
    // subtracting large terms to obtain a stationary contact's zero mass and
    // energy flux. The same Rankine-Hugoniot state serves every backend.
    // Ref: Toro, "Riemann Solvers and Numerical Methods for Fluid Dynamics"
    // U_K^* = rho_K * ((S_K - u_K) / (S_K - S_*)) * [1, S_*, E_K/rho_K + ...]
    static ARCH_INLINE FluidVector calc_star_flux(
        const FluidVector &U_K, double rho_K, double un_K, double ut1_K, double ut2_K,
        double p_K, double E_K,
        double S_K, double S_star, int dir)
    {
        // scaling factor: omega = (S_K - u_K) / (S_K - S_*)
        double denom = S_K - S_star;
        if (std::abs(denom) <= 16.0 * std::numeric_limits<double>::epsilon() * std::max(std::abs(S_K), std::abs(S_star)))
            return get_flux(U_K, p_K, dir); // Existing degenerate-state fallback.

        double omega = (S_K - un_K) / denom;

        // 1. Density*
        double rho_star = rho_K * omega;

        // The star state uses normal speed S_star and preserves tangential velocity.
        double un_star = S_star;

        // 3. Energy*
        double specific_E_K = E_K / rho_K;

        double sk_minus_u = S_K - un_K;
        double p_term = 0.0;
        if (sk_minus_u != 0.0)
        {
            p_term = p_K / (rho_K * sk_minus_u);
        }
        double term = (S_star - un_K) * (S_star + p_term);

        double eng_star = rho_star * (specific_E_K + term);

        if (sk_minus_u == 0.0) {
            // Retain the existing guarded pressure-term limit; that modified
            // state need not satisfy the unmodified contact-pressure identity.
            const auto star = set_flux_vector(rho_star, rho_star * un_star,
                rho_star * ut1_K, rho_star * ut2_K, eng_star, dir);
            return get_flux(U_K, p_K, dir) + S_K * (star - U_K);
        }
        const double pressure_star = p_K + rho_K * sk_minus_u * (S_star - un_K);
        const double mass_flux = rho_star * S_star;
        return set_flux_vector(mass_flux, mass_flux * S_star + pressure_star,
            mass_flux * ut1_K, mass_flux * ut2_K, S_star * (eng_star + pressure_star), dir);
    }

    template <typename EosType>
    static ARCH_INLINE void compute_face_flux(
        const FluidVector& U_L, const FluidVector& U_R,
        const double* Xi_L, const double* Xi_R, int n_spec,
        const EosType& eos, int dir, double /* coefficient */,
        FluidVector& flux_out, double* species_flux_out,
        const FluxAdmissibility::MeanThermoCache* mean_cache = nullptr,
        const FluidState* mean_state = nullptr, int left_cell = -1,
        int right_cell = -1)
    {
        // 2. Thermodynamics Preparation
        // Left
        double rho_L = U_L.rho;
        double un_L = get_un(U_L, dir);
        double ut1_L = get_ut1(U_L, dir);
        double ut2_L = get_ut2(U_L, dir);
        double v2_L = un_L * un_L + ut1_L * ut1_L + ut2_L * ut2_L; // Full 3D kinetic energy

        double e_L = (U_L.eng / rho_L) - 0.5 * v2_L;

        // Consistency of the Riemann problem: F_HLLC(U,U) = F(U). Compare
        // every conservative component and species before using this exact
        // identity. Still evaluate the required endpoint EOS (including its
        // acoustic validity); invalid states retain the ordinary failure path.
        bool identical = U_L.rho == U_R.rho && U_L.mom_u == U_R.mom_u
            && U_L.mom_v == U_R.mom_v && U_L.mom_w == U_R.mom_w
            && U_L.eng == U_R.eng;
        if (identical) {
            for (int s = 0; s < n_spec; ++s)
                identical = identical && Xi_L[s] == Xi_R[s];
        }
        if (identical && rho_L > 0.0 && e_L > 0.0
            && std::isfinite(e_L)) {
            double pressure = 0.0, sound_speed = 0.0;
            bool reused_mean = false;
#if !defined(__CUDA_ARCH__)
            // CUDA calls the same face formula without a host patch cache.
            // Keep FluidState/vector cache access out of device compilation.
            if (mean_cache && mean_state) {
                const auto matches_mean = [&](int cell) {
                    if (cell < 0 || !mean_cache->ready[cell]) return false;
                    const auto mean = mean_state->get(cell);
                    if (mean.rho != U_L.rho || mean.mom_u != U_L.mom_u
                        || mean.mom_v != U_L.mom_v || mean.mom_w != U_L.mom_w
                        || mean.eng != U_L.eng) return false;
                    for (int s = 0; s < n_spec; ++s)
                        if (mean_state->X(s, cell) != Xi_L[s]) return false;
                    return true;
                };
                const int matched = matches_mean(left_cell) ? left_cell
                    : matches_mean(right_cell) ? right_cell : -1;
                if (matched >= 0) {
                    // The patch-stage mean cache has already validated this
                    // exact (rho,e,X); no interpolation or approximation.
                    pressure = mean_cache->pressure[matched];
                    sound_speed = mean_cache->sound_speed[matched];
                    reused_mean = true;
                }
            }
#endif
            if (!reused_mean)
                calc_endpoint_thermo(U_L, e_L, Xi_L, eos,
                                     pressure, sound_speed);
            if (pressure > 0.0 && sound_speed > 0.0
                && std::isfinite(pressure) && std::isfinite(sound_speed)) {
                flux_out = get_flux(U_L, pressure, dir);
                for (int s = 0; s < n_spec; ++s)
                    species_flux_out[s] = flux_out.rho * Xi_L[s];
                return;
            }
        }

        double p_L, c_L;
        calc_endpoint_thermo(U_L, e_L, Xi_L, eos, p_L, c_L);

        // Right
        double rho_R = U_R.rho;
        double un_R = get_un(U_R, dir);
        double ut1_R = get_ut1(U_R, dir);
        double ut2_R = get_ut2(U_R, dir);
        double v2_R = un_R * un_R + ut1_R * ut1_R + ut2_R * ut2_R;

        double e_R = (U_R.eng / rho_R) - 0.5 * v2_R;
        double p_R, c_R;
        calc_endpoint_thermo(U_R, e_R, Xi_R, eos, p_R, c_R);

        // 3. Physical Fluxes (F_L, F_R)
        FluidVector F_L = get_flux(U_L, p_L, dir);
        FluidVector F_R = get_flux(U_R, p_R, dir);

        // 4. Wave Speed Estimates (S_L, S_R, S_*)
        double H_L = (U_L.eng + p_L) / rho_L;
        double H_R = (U_R.eng + p_R) / rho_R;

        // Roe Average (Used for S_L, S_R estimates)
        RoeGlaisterState roe_state = calc_glaister_state(
            U_L, U_R, p_L, p_R, e_L, e_R, H_L, H_R,
            Xi_L, Xi_R, n_spec, species_flux_out, eos);

        double S_L, S_R;
        calc_hll_wave_speeds(un_L, c_L, un_R, c_R, roe_state, dir, S_L, S_R);

        // Contact-wave speed.
        double S_star = calc_hllc_star_speed(un_L, rho_L, p_L, S_L,
                                             un_R, rho_R, p_R, S_R);

        // 5. HLLC Flux Assembly & Species Logic
        FluidVector hllc_flux;
        const double *chosen_Xi = nullptr; // Upwind composition associated with the selected region.

        // Branch 1: Supersonic L (Flow is all L)
        if (S_L >= 0.0)
        {
            hllc_flux = F_L;
            chosen_Xi = Xi_L;
        }
        // Branch 4: Supersonic R (Flow is all R)
        else if (S_R <= 0.0)
        {
            hllc_flux = F_R;
            chosen_Xi = Xi_R;
        }
        // Subsonic Region (Need Star Fluxes)
        else
        {
            // Branch 2: Left Star Region (S_L < 0 <= S_*)
            if (S_star >= 0.0)
            {
                hllc_flux = calc_star_flux(U_L, rho_L, un_L, ut1_L, ut2_L,
                    p_L, U_L.eng, S_L, S_star, dir);

                // The left star region carries the left composition.
                chosen_Xi = Xi_L;
            }
            // Branch 3: Right Star Region (S_* < 0 < S_R)
            else
            {
                hllc_flux = calc_star_flux(U_R, rho_R, un_R, ut1_R, ut2_R,
                    p_R, U_R.eng, S_R, S_star, dir);

                // The right star region carries the right composition.
                chosen_Xi = Xi_R;
            }
        }

        // Output Hydro Flux
        flux_out = hllc_flux;

        // 6. Species Flux (Upwind based on Star Region)
        // HLLC mass flux already includes the contact-wave correction.
        double mass_flux = hllc_flux.rho;

        for (int s = 0; s < n_spec; ++s)
        {
            // Species fractions remain constant across each outer
            // star region, so advect the composition selected by
            // the same side test used for the hydrodynamic flux.
            species_flux_out[s] = mass_flux * chosen_Xi[s];
        }
    }

    template <typename EosType>
    static void compute_fluxes(const FluidState &state, const EosType &eos, const Grid &grid,
                               std::vector<FluidVector> &flux_out,
                               std::vector<double> &spec_flux_out,
                               int dir, // 0=x, 1=y, 2=z
                               double /* unused_entropy_coeff */ = 0.0,
                               FluxAdmissibility::MeanThermoCache* mean_cache = nullptr)
    {
        arch::state::HostFailure failure;
        int n_spec = state.GetNumSpecies();
        int total_size = grid.GetTotalSize();

        int stride = (dir == 0) ? 1 : ((dir == 1) ? grid.stride_y : grid.stride_z);

        int i_start = grid.Is();
        int i_end = grid.Ie();
        int j_start = grid.Js();
        int j_end = grid.Je();
        int k_start = grid.Ks();
        int k_end = grid.Ke();

        if (dir == 0)
            i_start -= 1;
        else if (dir == 1)
            j_start -= 1;
        else if (dir == 2)
            k_start -= 1;

        const int nk = k_end - k_start;
        const int nj = j_end - j_start;

        // Exact cell-mean EOS results are reused by all faces and dimensions
        // of this patch-stage. Only the host execution schedule owns the
        // cache; reconstructed face states and the shared limiter are unchanged.
        if (mean_cache) {
            if (mean_cache->ready.size() != static_cast<std::size_t>(total_size))
                throw std::logic_error("HLLC mean EOS cache has wrong patch size");
            std::vector<double> mean_species(n_spec);
            const auto ensure_mean = [&](int cell) {
                if (mean_cache->ready[cell]) return;
                state.get_species_to_buffer(cell, mean_species.data());
                FluxAdmissibility::required_mean_thermo(
                    state.get(cell), mean_species.data(), eos,
                    mean_cache->pressure[cell], mean_cache->sound_speed[cell]);
                mean_cache->ready[cell] = 1;
            };
            for (int kj = 0; kj < nk * nj; ++kj) {
                const int k = k_start + kj / nj;
                const int j = j_start + kj % nj;
                for (int i = i_start; i < i_end; ++i) {
                    const int left = grid.GetIndex(i, j, k);
                    ensure_mean(left);
                    ensure_mean(left + stride);
                }
            }
        }

        const auto process_row = [&](int kj, std::vector<double>& Xi_L,
                                     std::vector<double>& Xi_R,
                                     std::vector<double>& Xi_cell,
                                     std::vector<double>& face_species_flux) {
                try {
                    int k = k_start + kj / nj;
                    int j = j_start + kj % nj;
                    for (int i = i_start; i < i_end; ++i)
                    {
                        int idx = grid.GetIndex(i, j, k);
                        // 1. Reconstruction
                        FluidVector U_L, U_R;
                        AMRInterfaceReconstruction::reconstruct_face<ReconstructPolicy>(state, eos, grid, dir, i, j, k, idx, stride, n_spec, Xi_L.data(), Xi_R.data(), Xi_cell.data(), U_L, U_R);

                        FluxAdmissibility::compute_candidate([&] {
                            compute_face_flux(
                                U_L, U_R, Xi_L.data(), Xi_R.data(), n_spec, eos, dir,
                                0.0, flux_out[idx + stride], face_species_flux.data(),
                                mean_cache, &state, idx, idx + stride);
                        }, flux_out[idx + stride], face_species_flux.data(), n_spec);
                        state.get_species_to_buffer(idx, Xi_L.data());
                        state.get_species_to_buffer(idx + stride, Xi_R.data());
                        if (mean_cache) {
                            FluxAdmissibility::limit_face_with_thermo(
                                state.get(idx), state.get(idx + stride),
                                Xi_L.data(), Xi_R.data(), n_spec,
                                mean_cache->pressure[idx], mean_cache->sound_speed[idx],
                                mean_cache->pressure[idx + stride],
                                mean_cache->sound_speed[idx + stride], dir,
                                flux_out[idx + stride], face_species_flux.data());
                        } else {
                            FluxAdmissibility::limit_face(
                                state.get(idx), state.get(idx + stride),
                                Xi_L.data(), Xi_R.data(), n_spec, eos, dir,
                                flux_out[idx + stride], face_species_flux.data());
                        }
                        for (int s = 0; s < n_spec; ++s)
                        {
                            spec_flux_out[s * total_size + (idx + stride)] = face_species_flux[s];
                        }
                    }

                } catch (...) { failure.capture_current(); }
        };

        // RK stages already distribute independent blocks among workers.
        // A nested team for one small patch repeats launch costs without
        // additional useful parallelism. Keep the same face mathematics.
        bool parallel_rows = nk * nj > 1;
#ifdef _OPENMP
        parallel_rows = parallel_rows && !omp_in_parallel();
#endif
        if (parallel_rows) {
#pragma omp parallel
            {
                std::vector<double> Xi_L(n_spec), Xi_R(n_spec);
                std::vector<double> Xi_cell(n_spec), face_species_flux(n_spec);
#pragma omp for schedule(static)
                for (int kj = 0; kj < nk * nj; ++kj)
                    process_row(kj, Xi_L, Xi_R, Xi_cell, face_species_flux);
            }
        } else {
            std::vector<double> Xi_L(n_spec), Xi_R(n_spec);
            std::vector<double> Xi_cell(n_spec), face_species_flux(n_spec);
            for (int kj = 0; kj < nk * nj; ++kj)
                process_row(kj, Xi_L, Xi_R, Xi_cell, face_species_flux);
        }

        failure.rethrow();
    }
};
