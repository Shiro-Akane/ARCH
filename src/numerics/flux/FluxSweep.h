/**
 * @file FluxSweep.h
 * @brief Host face traversal shared by every numerical flux policy.
 *
 * Workflow:
 * 1. Prepare required physical mean thermodynamics for one immutable RK stage;
 *    native RZ uses the accepted shared density/inertia closure's EOS input.
 * 2. Reconstruct conservative/species states using the configured shared
 *    PCM/MUSCL/PPM and coarse-fine policy; call the selected shared face flux.
 * 3. Apply the unchanged conservative limiter using physical mean P/c, then
 *    publish one fluid/species flux for both neighbours and AMR registration.
 *
 * This host adapter owns scratch and OpenMP traversal only. Existing CUDA
 * callers retain their original representation; native RZ device migration
 * and the full native-measure flux limiter require separate qualification.
 */
#pragma once

#include <stdexcept>
#include <vector>
#ifdef _OPENMP
#include <omp.h>
#endif

#include "grid/GridMetrics.h"
#include "numerics/flux/InvariantDomainFlux.h"
#include "numerics/reconstruction/AMRInterfaceReconstruction.h"
#include "numerics/state/RzNativeClosure.h"

namespace FluxTraversal {
/** Execute one directional sweep without duplicating a solver's mathematics. */
template<class FluxPolicy, class ReconstructPolicy, class EosType>
void compute_fluxes(const FluidState& state, const EosType& eos, const Grid& grid,
    std::vector<FluidVector>& flux_out, std::vector<double>& spec_flux_out,
    int dir, double coefficient, FluxAdmissibility::MeanThermoCache* mean_cache)
{
    FluxAdmissibility::MeanThermoCache local_cache;
    if (!mean_cache) { local_cache.reset(grid.GetTotalSize()); mean_cache=&local_cache; }
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
    {
        if (mean_cache->ready.size() != static_cast<std::size_t>(total_size))
            throw std::logic_error("Face mean EOS cache has wrong patch size");
        const auto geometry = GridMetrics::make_geometry_view(grid,mean_cache->geometry_semantics);
        const bool native_rz = geometry.semantics == GridMetrics::GeometrySemantics::AxisymmetricRz;
        if (native_rz && grid.stride_y <= 0)
            throw std::logic_error("RZ mean EOS requires a positive native row stride");
        const auto read = [&state](int index) { return state.get(index); };
        std::vector<double> mean_species(n_spec);
        const auto ensure_mean = [&](int cell) {
            if (mean_cache->ready[cell]) return;
            state.get_species_to_buffer(cell, mean_species.data());
            if (native_rz) {
                // m_phi=J/W has a different kinetic mean from an ordinary
                // point momentum. Reuse the accepted shared closure (kappa
                // and effective_mean); do not implement another EOS or floor.
                const auto closure = RzThermodynamics::make_cell(
                    read,cell,geometry,cell % grid.stride_y);
                if (!closure.valid())
                    throw std::runtime_error("RZ face mean EOS requires an admissible native closure");
                FluxAdmissibility::required_mean_thermo(
                    closure.effective_mean, mean_species.data(), eos,
                    mean_cache->pressure[cell], mean_cache->sound_speed[cell]);
            } else {
                FluxAdmissibility::required_mean_thermo(
                    state.get(cell), mean_species.data(), eos,
                    mean_cache->pressure[cell], mean_cache->sound_speed[cell]);
            }
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

    const FluxAdmissibility::MeanThermoView mean_view{
        state.rho.data(), state.mom_u.data(), state.mom_v.data(),
        state.mom_w.data(), state.eng.data(), state.mass_fractions.data(),
        mean_cache->pressure.data(),
        mean_cache->sound_speed.data(),
        mean_cache->ready.data(), total_size, n_spec,
        mean_cache->roe_wave_speed,mean_cache->geometry_semantics};

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
                    AMRInterfaceReconstruction::reconstruct_face<ReconstructPolicy>(
                        state,eos,grid,dir,i,j,k,idx,stride,n_spec,
                        Xi_L.data(),Xi_R.data(),Xi_cell.data(),U_L,U_R,mean_cache->geometry_semantics);

                    FluxAdmissibility::compute_candidate([&] {
                        FluxPolicy::compute_face_flux(
                            U_L, U_R, Xi_L.data(), Xi_R.data(), n_spec, eos, dir,
                            coefficient, flux_out[idx + stride], face_species_flux.data(),
                            &mean_view, idx, idx + stride);
                    }, flux_out[idx + stride], face_species_flux.data(), n_spec);
                    state.get_species_to_buffer(idx, Xi_L.data());
                    state.get_species_to_buffer(idx + stride, Xi_R.data());
                    FluxAdmissibility::limit_face_with_thermo(
                        state.get(idx), state.get(idx + stride),
                        Xi_L.data(), Xi_R.data(), n_spec,
                        mean_cache->pressure[idx], mean_cache->sound_speed[idx],
                        mean_cache->pressure[idx + stride],
                        mean_cache->sound_speed[idx + stride], dir,
                        flux_out[idx + stride], face_species_flux.data());
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
} // namespace FluxTraversal
