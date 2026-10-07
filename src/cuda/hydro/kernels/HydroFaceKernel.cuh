/**
 * @file HydroFaceKernel.cuh
 * @brief Reconstruct device face states and evaluate shared flux policies.
 *
 * The shared AMR stencil predicate chooses the admissible reconstruction near
 * coarse/fine interfaces. Each face writes hydro and species fluxes to borrowed
 * buffers; per-lane composition scratch belongs to the launch owner. Launch
 * success means work was queued, not that the output is host-visible.
 * Workflow:
 * 1. Receive stage views, face geometry and device state arrays.
 * 2. Launch the shared hydro face, source or state work on CUDA.
 * 3. Publish stage output only after the backend stream orders writes.
 * 4. A separate private face-only adapter borrows 41*S lane-major scratch and
 *    invokes shared selected native point/face mathematics. It grants neither
 *    full Native launch capability nor Runtime/BC/AMR/source publication.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

#include "cuda/hydro/GridGeometryAdapter.cuh"
#include "cuda/hydro/policies/CheckedHydroEos.cuh"
#include "cuda/hydro/policies/HydroFluxPolicies.cuh"
#include "cuda/hydro/policies/HydroReconstructionPolicies.cuh"
#include "numerics/flux/InvariantDomainFlux.h"
#include "numerics/flux/RzNativeFaceFlux.h"
#include "numerics/reconstruction/AMRInterfaceStencil.h"

namespace arch::cuda
{
namespace detail
{
/** Explicit private leaf arena: 41*S contiguous doubles PER LANE.
 * Workflow: the test/storage owner allocates the arena; the adapter checks
 * integer capacity, borrows 16*S rhoX + 19*S reconstruction + five S scratch
 * planes, and reserves the last S solely for unpublished output. This is not
 * the array-major SpeciesWorkspaceView and does not change any launch ABI.
 */
struct NativeFaceScratchView {
    double* values=nullptr;
    std::size_t capacity=0;
    int lanes=0;
};

/** Reject malformed POD extents before a shared reader can dereference them.
 * This is a mathematical single-patch storage check, not a Runtime lease or
 * a backend capability grant. Canonical identity uses its existing shared owner.
 */
ARCH_INLINE bool native_face_storage_valid(DeviceStateView state,
    DeviceStateView flux,const DeviceGridView& grid,int direction,int i,int j,
    const arch::state::Bounds& bounds,double coefficient)
{
    if(grid.semantics!=GridMetrics::GeometrySemantics::AxisymmetricRz
        ||grid.geometry!=static_cast<int>(DeviceGeometry::Cylindrical)||grid.dim!=2
        ||grid.ng<1||grid.ng>(std::numeric_limits<int>::max()-16)/2
        ||grid.total_x!=16+2*grid.ng||grid.total_y!=16+2*grid.ng
        ||grid.total_z!=1||grid.ks!=0||grid.ke!=1
        ||grid.is!=grid.ng||grid.ie!=grid.ng+16
        ||grid.js!=grid.ng||grid.je!=grid.ng+16
        ||grid.stride_y<grid.total_x||grid.stride_z<=0||grid.total_size<=0
        ||std::int64_t(grid.total_y)*grid.stride_y>grid.stride_z
        ||std::int64_t(grid.total_y-1)*grid.stride_y+grid.total_x>grid.total_size
        ||state.total_size!=grid.total_size||flux.total_size!=grid.total_size
        ||state.n_species<0||flux.n_species!=state.n_species
        ||(state.n_species>0&&state.total_size>std::numeric_limits<int>::max()/state.n_species)
        ||!state.rho||!state.mom_u||!state.mom_v||!state.mom_w||!state.eng
        ||!flux.rho||!flux.mom_u||!flux.mom_v||!flux.mom_w||!flux.eng
        ||(state.n_species&&(!state.mass_fractions||!flux.mass_fractions))
        ||state.rho==flux.rho||state.mom_u==flux.mom_u||state.mom_v==flux.mom_v
        ||state.mom_w==flux.mom_w||state.eng==flux.eng
        ||(state.n_species&&state.mass_fractions==flux.mass_fractions)
        ||!arch::state::valid_bounds(bounds)||!std::isfinite(coefficient)
        ||(direction!=0&&direction!=1))return false;
    for(int f=0;f<6;++f)if(grid.amr_coarse_fine_face[f]>1)return false;
    if(direction==0) {
        if(i<grid.is-1||i>=grid.ie||j<grid.js||j>=grid.je)return false;
    } else if(i<grid.is||i>=grid.ie||j<grid.js-1||j>=grid.je)return false;
    const auto geometry=make_grid_geometry_view(grid);
    return GridMetrics::matches_identity(geometry);
}

/** Evaluate ONE private resident Native face with the common physical math.
 * Workflow: validate POD/bounds/arena; bind required checked EOS; select the
 * exact configured shared policy (or actual 2:1 MinMod fallback); reconstruct
 * physical H/B, screen one whole-face factor, then publish this face once.
 * Axial averages use V for rho/mr/mz/E/rhoX and W for physical mphi; no second
 * torque lever lives here. Failure latches the required status and leaves all
 * real flux arrays unchanged. Earlier faces remain provisional, not a stage
 * transaction. Full Native launch/Runtime/BC/AMR/self-gravity gates stay held.
 * The caller owns nonoverlapping source, destination and scratch allocations.
 */
template<class Reconstruction,class Flux,class EosView>
__device__ inline arch::state::Status hydro_native_face_math(
    DeviceStateView state,DeviceStateView flux,DeviceGridView grid,EosView eos,
    int direction,int i,int j,double coefficient,arch::state::Bounds bounds,
    NativeFaceScratchView arena,int lane,int* required_status,
    const FluxAdmissibility::MeanThermoView* means=nullptr,
    const arch::boundary::HydroBoundaryView& walls={})
{
    using Status=arch::state::Status;
    const auto reject=[required_status](Status status) {
        if(required_status)atomicExch(required_status,1);
        return status;
    };
    if(!required_status||!native_face_storage_valid(state,flux,grid,direction,i,j,bounds,coefficient)
        ||(means&&means->geometry_semantics!=GridMetrics::GeometrySemantics::AxisymmetricRz))
        return reject(Status::invalid_thermodynamics);
    const std::size_t species=static_cast<std::size_t>(state.n_species);
    if(lane<0||arena.lanes<=0||lane>=arena.lanes
        ||species>std::numeric_limits<std::size_t>::max()/41
        ||(species&&std::size_t(arena.lanes)>std::numeric_limits<std::size_t>::max()/(41*species))
        ||(species&&(!arena.values||arena.capacity<41*species*std::size_t(arena.lanes))))
        return reject(Status::invalid_composition);
    const int normal=direction==0?i:j,begin=direction==0?grid.is:grid.js,end=direction==0?grid.ie:grid.je;
    const bool use_tvd_interface=AMRInterfaceReconstruction::needs_tvd_interface_stencil(
        Reconstruction::ghost_depth,grid.amr_coarse_fine_face,direction,normal,begin,end);
    using Selected=typename Reconstruction::shared_policy;
    if(direction==0&&grid.dyadic_identity.bound&&grid.dyadic_identity.root_lower[0]==0.
        &&(use_tvd_interface||RzSelectedReconstruction::PolicyTraits<Selected>::kind==1)&&grid.ng<3)
        return reject(Status::invalid_thermodynamics);
    double* values=species?arena.values+41*species*std::size_t(lane):nullptr;
    RzNativeFaceFlux::Scratch scratch{};
    scratch.rhoX=values;scratch.reconstruction=species?values+16*species:nullptr;
    scratch.reconstruction_count=19*species;
    scratch.x_left=species?values+35*species:nullptr;scratch.x_right=species?values+36*species:nullptr;
    scratch.candidate_species=species?values+37*species:nullptr;
    scratch.high_sum=species?values+38*species:nullptr;scratch.low_sum=species?values+39*species:nullptr;
    double* output_species=species?values+40*species:nullptr;
    const auto geometry=make_grid_geometry_view(grid);
    const RzSelectedReconstruction::Context context{geometry,grid.total_x,grid.total_y,i,j,
        direction,state.n_species,bounds};
    const int left=grid.index(i,j),right=left+grid.stride(direction);
    const auto read=[state](int cell){return state.load(cell);};
    const auto fraction=[state](int s,int cell){return state.species(s,cell);};
    // If a caller already supplied a checked view, retire its earlier ordinary
    // mean borrowing before nesting the required-query transport. Native point
    // EOS cannot reuse closure-mean P/c from either wrapper layer.
    if constexpr(requires {eos.bind_mean_thermodynamics(state,left,right,
        nullptr,nullptr,grid.semantics);})
        eos.bind_mean_thermodynamics(state,left,right,nullptr,nullptr,grid.semantics);
    const auto checked=make_checked_hydro_eos(eos,required_status);
    FluidVector output{};
    const auto status=use_tvd_interface?RzNativeFaceFlux::compute<typename Flux::shared_policy,
        MusclReconstruction<MinMod>>(read,fraction,context,checked,coefficient,means,left,right,
            scratch,output,output_species,walls):
        RzNativeFaceFlux::compute<typename Flux::shared_policy,Selected>(read,fraction,context,
            checked,coefficient,means,left,right,scratch,output,output_species,walls);
    if(status!=Status::valid)return reject(status);
    if(*required_status)return Status::invalid_thermodynamics;
    flux.store(right,output);
    for(int s=0;s<state.n_species;++s)flux.set_species(s,right,output_species[s]);
    return Status::valid;
}

/** Apply the common coarse-fine stencil rule to resident stage inputs. */
template <typename Reconstruction, typename EosView>
ARCH_INLINE void reconstruct_amr_face(
    DeviceStateView state, DeviceGridView grid, int direction,
    int i, int j, int k, int cell, int stride, const EosView& eos,
    FluidVector& left, FluidVector& right,
    double* species_left, double* species_right, double* species_cell)
{
    const int normal_index = direction == 0 ? i : (direction == 1 ? j : k);
    const int normal_begin = direction == 0 ? grid.is
        : (direction == 1 ? grid.js : grid.ks);
    const int normal_end = direction == 0 ? grid.ie
        : (direction == 1 ? grid.je : grid.ke);
    if (AMRInterfaceReconstruction::needs_tvd_interface_stencil(
            Reconstruction::ghost_depth, grid.amr_coarse_fine_face,
            direction, normal_index, normal_begin, normal_end)) {
        CudaMusclReconstruction<MinMod>::reconstruct(
            state, cell, stride, eos, left, right,
            species_left, species_right, species_cell);
        return;
    }
    Reconstruction::reconstruct(
        state, cell, stride, eos, left, right,
        species_left, species_right, species_cell);
}

template <typename Reconstruction, typename Flux, typename EosView>
__device__ inline void hydro_face_kernel_work(
    DeviceStateView state, DeviceStateView flux, DeviceGridView grid,
    EosView eos, int direction, double coefficient,
    SpeciesWorkspaceView workspace = {}, const double* mean_pressure = nullptr,
    const double* mean_sound_speed = nullptr, bool roe_wave_speed = true)
{
    int i_begin = grid.is;
    int j_begin = grid.js;
    int k_begin = grid.ks;
    if (direction == 0)
        --i_begin;
    else if (direction == 1)
        --j_begin;
    else
        --k_begin;
    const int ni = grid.ie - i_begin;
    const int nj = grid.je - j_begin;
    const int nk = grid.ke - k_begin;
    const int lane = blockIdx.x * blockDim.x + threadIdx.x;
    SpeciesLaneScratch<4> scratch(workspace, lane);
    double* species_left = scratch.array(0);
    double* species_right = scratch.array(1);
    double* species_cell = scratch.array(2);
    double* face_species_flux = scratch.array(3);
    const FluxAdmissibility::MeanThermoView means{
        state.rho, state.mom_u, state.mom_v, state.mom_w, state.eng,
        state.mass_fractions, mean_pressure, mean_sound_speed, nullptr,
        state.total_size, state.n_species, roe_wave_speed, grid.semantics};
    for (int linear = lane; linear < ni * nj * nk;
         linear += blockDim.x * gridDim.x) {
        const int i = i_begin + linear % ni;
        const int j = j_begin + (linear / ni) % nj;
        const int k = k_begin + linear / (ni * nj);
        const int cell = grid.index(i, j, k);
        const int stride = grid.stride(direction);

        FluidVector left;
        FluidVector right;
        reconstruct_amr_face<Reconstruction>(
            state, grid, direction, i, j, k, cell, stride, eos, left, right,
            species_left, species_right, species_cell);
        FluxAdmissibility::limit_reconstruction(state.load(cell), left);
        FluxAdmissibility::limit_reconstruction(state.load(cell + stride), right);
        FluidVector face_flux;
        if constexpr (requires {
            eos.bind_mean_thermodynamics(state, cell, cell + stride,
                                         mean_pressure, mean_sound_speed, grid.semantics);
        }) eos.bind_mean_thermodynamics(state, cell, cell + stride,
                                        mean_pressure, mean_sound_speed, grid.semantics);
        const auto trial_eos = arch::state::candidate_eos(eos);
        // The method choice is valid even without cached thermodynamics (the
        // inexpensive ideal EOS needs no mean buffers). A view with null P/c
        // safely declines reuse and still carries the configured wave bounds.
        Flux::compute(
            left, right, species_left, species_right, state.n_species, trial_eos,
            direction, coefficient, face_flux, face_species_flux,
            &means, cell, cell + stride);
        for (int species = 0; species < state.n_species; ++species) {
            species_left[species] = state.species(species, cell);
            species_right[species] = state.species(species, cell + stride);
        }
        if (mean_pressure && mean_sound_speed) {
            // Same limiter, with the unchanged means evaluated once per stage.
            FluxAdmissibility::limit_face_with_thermo(
                state.load(cell), state.load(cell + stride), species_left,
                species_right, state.n_species, mean_pressure[cell],
                mean_sound_speed[cell], mean_pressure[cell + stride],
                mean_sound_speed[cell + stride], direction, face_flux, face_species_flux);
        } else {
            FluxAdmissibility::limit_face(state.load(cell), state.load(cell + stride),
                species_left, species_right, state.n_species, eos, direction,
                face_flux, face_species_flux);
        }
        const int face = cell + stride;
        flux.store(face, face_flux);
        for (int species = 0; species < state.n_species; ++species)
            flux.set_species(species, face, face_species_flux[species]);
        // Observe the exact flux divergence and reflux consume, after
        // admissibility limiting and physical-boundary ghost filling.
        // Hydro reconstructs from the left cell, while divergence addresses
        // the face by its right-cell index. Shift only the normal coordinate
        // so both domain faces refer to the flux just stored at cell+stride.
        // Hydro heat flux is zero; only owned physical face planes are touched.
        if (state.capture.stage[2 * direction] || state.capture.stage[2 * direction + 1])
            arch::boundary::CaptureBoundaryFlux(state.capture, direction,
                i+(direction==0),j+(direction==1),k+(direction==2),
                grid.is, grid.ie, grid.js, grid.je, grid.ks, grid.ke, face_flux,
                state.n_species ? flux.mass_fractions + face : nullptr,
                state.n_species, flux.total_size, 0.0);
    }
}

template <typename Reconstruction, typename Flux, typename EosView>
__global__ void hydro_face_kernel(
    DeviceStateView state, DeviceStateView flux, DeviceGridView grid,
    EosView eos, int direction, double coefficient,
    SpeciesWorkspaceView workspace = {})
{
    hydro_face_kernel_work<Reconstruction, Flux>(state, flux, grid, eos, direction, coefficient, workspace);
}
} // namespace detail

template <typename Reconstruction, typename Flux, typename EosView>
inline cudaError_t launch_hydro_faces(
    DeviceStateView state, DeviceStateView flux, DeviceGridView grid,
    const EosView& eos, int direction, double coefficient, cudaStream_t stream,
    SpeciesWorkspaceView workspace = {})
{
    const int directional_begin = direction == 0 ? grid.is
        : (direction == 1 ? grid.js : grid.ks);
    const int directional_end = direction == 0 ? grid.ie
        : (direction == 1 ? grid.je : grid.ke);
    const int directional_extent = direction == 0 ? grid.total_x
        : (direction == 1 ? grid.total_y : grid.total_z);
    if (!valid_hydro_view(state) || !valid_hydro_view(flux)
        || !valid_species_workspace(workspace, state.n_species, 4)
        || state.n_species != flux.n_species
        || state.total_size != flux.total_size
        || state.total_size != grid.total_size
        || !valid_hydro_grid(grid)
        // Native selected face/BC/stage publication is a separate migration.
        // A valid Native mean cache must not enable this raw-point traversal.
        || grid.semantics != GridMetrics::GeometrySemantics::Existing
        || grid.ng < Reconstruction::ghost_depth
        || direction < 0 || direction >= grid.dim
        || directional_begin < Reconstruction::ghost_depth
        || directional_extent - directional_end
            < Reconstruction::ghost_depth)
        return cudaErrorInvalidValue;
    int ni = grid.ie - grid.is;
    int nj = grid.je - grid.js;
    int nk = grid.ke - grid.ks;
    if (direction == 0)
        ++ni;
    else if (direction == 1)
        ++nj;
    else
        ++nk;
    const int threads = detail::species_launch_threads(workspace);
    const int count = ni * nj * nk;
    if (count <= 0)
        return cudaSuccess;
    detail::hydro_face_kernel<Reconstruction, Flux>
        <<<detail::species_launch_blocks(count, workspace), threads, 0, stream>>>(
            state, flux, grid, eos, direction, coefficient, workspace);
    return cudaGetLastError();
}
} // namespace arch::cuda
