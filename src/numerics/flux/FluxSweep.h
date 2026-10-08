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
#include "numerics/flux/RzNativeFaceFlux.h"
#include "numerics/flux/StationarySlipWallFlux.h"
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
    const auto geometry=GridMetrics::make_geometry_view(grid,mean_cache->geometry_semantics);
    const bool native_rz=GridMetrics::is_axisymmetric_rz(geometry);
    const int logical_nx=grid.Ie()+grid.ng,logical_ny=grid.Je()+grid.ng;
    if(native_rz&&!arch::state::valid_bounds(mean_cache->physical_bounds))
        throw std::invalid_argument("Native face requires actual physical bounds");
    if(static_cast<std::size_t>(n_spec)>std::numeric_limits<std::size_t>::max()/35)
        throw std::length_error("Native face species workspace is not representable");

    // Flat flags require real upstream authority; this is only dimensional
    // preflight, never a grant from a bare bool or inferred endpoint/velocity.
    if(!native_rz&&!mean_cache->hydro_boundary.empty()) {
        if(grid.dim<1||grid.dim>3||dir<0||dir>=grid.dim)
            throw std::invalid_argument("Ordinary wall view has an inactive direction");
        for(int axis=grid.dim;axis<3;++axis)
            if(mean_cache->hydro_boundary.reflecting[2*axis]
                ||mean_cache->hydro_boundary.reflecting[2*axis+1])
                throw std::invalid_argument("Ordinary wall view marks an inactive axis");
    }

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
                const int radial=cell % grid.stride_y;
                const int begin=std::clamp(radial-1,0,logical_nx-3);
                const auto closure = RzThermodynamics::make_cell_supported(
                    read,cell,geometry,radial,begin,mean_cache->physical_bounds);
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
                                 std::vector<double>& face_species_flux,
                                 std::vector<double>& native_workspace,
                                 std::vector<double>& native_high_sum,
                                 std::vector<double>& native_low_sum) {
            try {
                int k = k_start + kj / nj;
                int j = j_start + kj % nj;
                for (int i = i_start; i < i_end; ++i)
                {
                    int idx = grid.GetIndex(i, j, k);
                    if(native_rz) {
                        // Actual interface policy precedes the selected bundle:
                        // all configured limiters/PPM remain selected elsewhere.
                        const RzSelectedReconstruction::Context context{
                            geometry,logical_nx,logical_ny,i,j,dir,n_spec,mean_cache->physical_bounds};
                        const auto count=static_cast<std::size_t>(n_spec);
                        RzNativeFaceFlux::Scratch scratch{
                            count?native_workspace.data():nullptr,
                            count?native_workspace.data()+16*count:nullptr,19*count,
                            Xi_L.data(),Xi_R.data(),Xi_cell.data(),
                            native_high_sum.data(),native_low_sum.data()};
                        const auto read=[&state](int cell){return state.get(cell);};
                        const auto fraction=[&state](int species,int cell){return state.X(species,cell);};
                        FluidVector native_flux;
                        const auto status=AMRInterfaceReconstruction::needs_tvd_interface_reconstruction<ReconstructPolicy>(grid,dir,i,j,k)
                            ?RzNativeFaceFlux::compute<FluxPolicy,MusclReconstruction<MinMod>>(
                                read,fraction,context,eos,coefficient,&mean_view,idx,idx+stride,
                                scratch,native_flux,face_species_flux.data(),mean_cache->hydro_boundary)
                            :RzNativeFaceFlux::compute<FluxPolicy,ReconstructPolicy>(
                                read,fraction,context,eos,coefficient,&mean_view,idx,idx+stride,
                                scratch,native_flux,face_species_flux.data(),mean_cache->hydro_boundary);
                        if(status!=arch::state::Status::valid)
                            throw std::runtime_error("Native selected physical face failed required geometry/state/EOS acceptance");
                        flux_out[idx+stride]=native_flux;
                        for(int species=0;species<n_spec;++species)
                            spec_flux_out[species*total_size+idx+stride]=face_species_flux[species];
                        continue;
                    }
                    // 1. Reconstruction
                    FluidVector U_L, U_R;
                    AMRInterfaceReconstruction::reconstruct_face<ReconstructPolicy>(
                        state,eos,grid,dir,i,j,k,idx,stride,n_spec,
                        Xi_L.data(),Xi_R.data(),Xi_cell.data(),U_L,U_R,mean_cache->geometry_semantics);

                    // Actual traversal coordinates name lower/upper faces;
                    // internal faces remain dormant without geometric guessing.
                    int wall_side=-1;
                    if(!mean_cache->hydro_boundary.empty()) {
                        const int coordinate=dir==0?i+1:dir==1?j+1:k+1;
                        const int lower=dir==0?grid.Is():dir==1?grid.Js():grid.Ks();
                        const int upper=dir==0?grid.Ie():dir==1?grid.Je():grid.Ke();
                        if(!mean_cache->hydro_boundary.reflecting_side(dir,coordinate,lower,upper,wall_side))
                            throw std::invalid_argument("Ordinary wall view has an invalid actual face");
                    }
                    if(wall_side>=0) {
                        // High and immutable point means borrow the same real
                        // interior separately; composition is mirrored with it.
                        auto base_left=state.get(idx),base_right=state.get(idx+stride);
                        if(wall_side==0) {
                            U_L=StationarySlipWallFlux::reflected_point(U_R,dir);
                            base_left=StationarySlipWallFlux::reflected_point(base_right,dir);
                            for(int s=0;s<n_spec;++s)Xi_L[s]=Xi_R[s];
                        } else {
                            U_R=StationarySlipWallFlux::reflected_point(U_L,dir);
                            base_right=StationarySlipWallFlux::reflected_point(base_left,dir);
                            for(int s=0;s<n_spec;++s)Xi_R[s]=Xi_L[s];
                        }
                        bool gamma_wall=false;
                        if constexpr(requires {eos.roe_gamma_minus_one(Xi_L.data());}) {
                            const auto& h=wall_side==0?U_R:U_L;
                            const auto& base=wall_side==0?base_right:base_left;
                            gamma_wall=(dir==0?h.mom_u:dir==1?h.mom_v:h.mom_w)!=0.
                                ||(dir==0?base.mom_u:dir==1?base.mom_v:base.mom_w)!=0.;
                        }
                        FluidVector high;
                        const auto& trial_eos=arch::state::candidate_eos(eos);
                        FluxAdmissibility::compute_candidate([&] {
                            if constexpr(requires {eos.roe_gamma_minus_one(Xi_L.data());}) {
                                if(gamma_wall) {
                                    const auto& interior=wall_side==0?U_R:U_L;
                                    const double* xi=wall_side==0?Xi_R.data():Xi_L.data();
                                    double pressure=0.,speed=0.;
                                    FluxAdmissibility::required_mean_thermo(interior,xi,trial_eos,pressure,speed);
                                    if(StationarySlipWallFlux::gamma_wall_flux(interior,xi,trial_eos,
                                        dir,wall_side,pressure,speed,high)) {
                                        for(int s=0;s<n_spec;++s)face_species_flux[s]=0.;
                                    } else {
                                        high=FluidVector(arch::state::invalid(),0.,0.,0.,arch::state::invalid());
                                        for(int s=0;s<n_spec;++s)face_species_flux[s]=arch::state::invalid();
                                    }
                                    return;
                                }
                            }
                            FluxPolicy::compute_face_flux(U_L,U_R,Xi_L.data(),Xi_R.data(),n_spec,
                                trial_eos,dir,coefficient,high,face_species_flux.data(),&mean_view,idx,idx+stride);
                        },high,face_species_flux.data(),n_spec);
                        if(StationarySlipWallFlux::stationary_candidate(
                            dir,high,face_species_flux.data(),n_spec)
                                ==StationarySlipWallFlux::StationaryCandidateStatus::invalid)
                            throw std::runtime_error("Ordinary wall high has invalid pressure traction");
                        const int interior_cell=wall_side==0?idx+stride:idx;
                        state.get_species_to_buffer(interior_cell,Xi_L.data());
                        for(int s=0;s<n_spec;++s)Xi_R[s]=Xi_L[s];
                        const double pressure=mean_cache->pressure[interior_cell];
                        const double speed=mean_cache->sound_speed[interior_cell];
                        FluxAdmissibility::PointFaceBlend factor;
                        if(gamma_wall) {
                            if constexpr(requires {eos.roe_gamma_minus_one(Xi_L.data());})
                                factor=StationarySlipWallFlux::gamma_selected_point_blend(
                                    base_left,base_right,Xi_L.data(),Xi_R.data(),n_spec,
                                    pressure,speed,pressure,speed,eos,dir,wall_side,
                                    high,face_species_flux.data(),Xi_cell.data());
                        } else {
                            factor=FluxAdmissibility::point_face_blend_with_thermo(
                                base_left,base_right,Xi_L.data(),Xi_R.data(),n_spec,
                                pressure,speed,pressure,speed,dir,high,face_species_flux.data());
                        }
                        if(!factor.valid||!StationarySlipWallFlux::finite_flux(factor.low)
                            ||!std::isfinite(factor.theta)||factor.theta<0.||factor.theta>1.)
                            throw std::runtime_error("Ordinary selected wall has an inadmissible baseline");
                        const double hp=dir==0?high.mom_u:dir==1?high.mom_v:high.mom_w;
                        const double lp=dir==0?factor.low.mom_u:dir==1?factor.low.mom_v:factor.low.mom_w;
                        if(hp<0.||(!std::isfinite(hp)&&factor.theta!=0.)||!std::isfinite(lp)||lp<0.)
                            throw std::runtime_error("Ordinary wall has invalid pressure traction");
                        const auto blended=factor.theta==0.?factor.low:factor.theta==1.?high:
                            factor.low+factor.theta*(high-factor.low);
                        if(!StationarySlipWallFlux::finite_flux(blended))
                            throw std::runtime_error("Ordinary wall flux is not finite");
                        // Publish exactly the H/low blend verified above. No
                        // post-factor projection may change the verified object.
                        const double flrho=dir==0?base_left.mom_u:dir==1?base_left.mom_v:base_left.mom_w;
                        const double frrho=dir==0?base_right.mom_u:dir==1?base_right.mom_v:base_right.mom_w;
                        for(int s=0;s<n_spec;++s) {
                            const double low_species=gamma_wall?0.:
                                .5*flrho*Xi_L[s]+.5*frrho*Xi_R[s]
                                -(.5*factor.wave_speed)*(base_right.rho*Xi_R[s]-base_left.rho*Xi_L[s]);
                            const double value=factor.theta==0.?low_species:
                                factor.theta==1.?face_species_flux[s]:
                                low_species+factor.theta*(face_species_flux[s]-low_species);
                            if(!std::isfinite(value))
                                throw std::runtime_error("Ordinary wall species flux is not finite");
                            face_species_flux[s]=value;
                        }
                        flux_out[idx+stride]=blended;
                        for(int s=0;s<n_spec;++s)
                            spec_flux_out[s*total_size+idx+stride]=face_species_flux[s];
                        continue;
                    }

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
            std::vector<double> native_workspace(native_rz?35*static_cast<std::size_t>(n_spec):0);
            std::vector<double> native_high_sum(native_rz?n_spec:0),native_low_sum(native_rz?n_spec:0);
#pragma omp for schedule(static)
            for (int kj = 0; kj < nk * nj; ++kj)
                process_row(kj, Xi_L, Xi_R, Xi_cell, face_species_flux,
                    native_workspace,native_high_sum,native_low_sum);
        }
    } else {
        std::vector<double> Xi_L(n_spec), Xi_R(n_spec);
        std::vector<double> Xi_cell(n_spec), face_species_flux(n_spec);
        std::vector<double> native_workspace(native_rz?35*static_cast<std::size_t>(n_spec):0);
        std::vector<double> native_high_sum(native_rz?n_spec:0),native_low_sum(native_rz?n_spec:0);
        for (int kj = 0; kj < nk * nj; ++kj)
            process_row(kj, Xi_L, Xi_R, Xi_cell, face_species_flux,
                    native_workspace,native_high_sum,native_low_sum);
    }

    failure.rethrow();
}
} // namespace FluxTraversal
