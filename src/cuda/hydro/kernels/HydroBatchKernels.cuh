/**
 * @file HydroBatchKernels.cuh
 * @brief Execute a bounded wave of blocks through the shared hydro operators.
 *
 * Grid y selects a block; grid x and the thread index select its cells/faces.
 * The runtime owns all bindings, state slots and scratch until stream completion.
 * Each wave clears increments, evaluates directional fluxes, registers them in
 * route order, adds sources and performs the common integrator update. Species
 * scratch shared between blocks requires single-block waves to prevent aliasing.
 * These launches do not publish state or alter the scheduler's stage weights.
 * Workflow:
 * 1. Receive stage views, face geometry and device state arrays.
 * 2. Launch the shared hydro face, source or state work on CUDA.
 * 3. Complete original per-direction AMR registration before scratch reuse.
 * 4. Native uses factory walls/Bounds, shared integrated curvature, and strict
 *    V/W provisional updates; completed Runtime BC/EOS owns thermal acceptance.
 * 5. Only a gravity-enabled Native block evaluates the shared external cell
 *    leaf, applies its impulse and reduces the measured compact budget.
 */
#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

#include "cuda/hydro/policies/CheckedHydroEos.cuh"
#include "cuda/hydro/policies/HydroIntegratorPolicies.cuh"
#include "cuda/runtime/hydro/CudaBackendHydro.h"
#include "numerics/state/RzNativeClosure.h"
#include "physics/gravity/NativeExternalSourceMath.h"

namespace arch::cuda {
namespace detail {

/** Reset actual stage increments and initialize the output diagnostic.
 * ENUC is the last burn half's specific rate, not an RK conserved variable:
 * active cells inherit the actual input bits, while unused output storage keeps
 * the original zero initialization. Never borrow uninitialized Next/Scratch or
 * input padding, and do not add transport, weighting or a second kernel.
 */
static __global__ void hydro_batch_clear(const DeviceHydroBatchBlock* blocks)
{
    const auto& b = blocks[blockIdx.y];
    const int cell = blockIdx.x * blockDim.x + threadIdx.x;
    if (cell == 0) *b.eos_status = 0;
    if (b.repairs.values)
        for (int n = cell; n < state::RepairView::fixed_size + 2 * b.repairs.species; n += blockDim.x * gridDim.x) b.repairs.values[n] = 0.0;
    if (cell >= b.delta.total_size) return;
    b.delta.store(cell, {0.0, 0.0, 0.0, 0.0, 0.0});
    for (int s = 0; s < b.delta.n_species; ++s) b.delta.set_species(s, cell, 0.0);
    const int k = cell / b.grid.stride_z;
    const int j = (cell - k * b.grid.stride_z) / b.grid.stride_y;
    const int i = cell - k * b.grid.stride_z - j * b.grid.stride_y;
    const bool active = i >= b.grid.is && i < b.grid.ie
        && j >= b.grid.js && j < b.grid.je && k >= b.grid.ks && k < b.grid.ke;
    b.output.enuc_rate[cell] = active ? b.input.enuc_rate[cell] : 0.0;
}

/** Authenticate Native metadata before any state/fraction reader is invoked.
 * Workflow: check actual logical/padded extents and pointers -> bind the sole
 * shared canonical identity -> validate existing physical Bounds. This is a
 * mathematical borrowing check, not a Runtime slot/ghost/stream certificate.
 */
__device__ inline bool native_mean_binding_valid(const DeviceHydroBatchBlock& b,
    const arch::state::Bounds& bounds)
{
    const auto& g=b.grid;
    if (g.semantics != GridMetrics::GeometrySemantics::AxisymmetricRz
        || g.geometry != static_cast<int>(DeviceGeometry::Cylindrical)
        || g.dim != 2 || !g.dyadic_identity.bound || g.ng < 1
        || g.ng > (std::numeric_limits<int>::max()-amr::BLOCK_NX)/2
        || g.ng > (std::numeric_limits<int>::max()-amr::BLOCK_NY)/2
        || g.total_x != amr::BLOCK_NX+2*g.ng || g.total_y != amr::BLOCK_NY+2*g.ng
        || g.is != g.ng || g.ie != g.ng+amr::BLOCK_NX
        || g.js != g.ng || g.je != g.ng+amr::BLOCK_NY
        || g.total_z != 1 || g.ks != 0 || g.ke != 1
        || g.stride_y < g.total_x || g.stride_z <= 0 || g.total_size <= 0
        || std::int64_t(g.total_y)*g.stride_y > g.stride_z
        || std::int64_t(g.total_y-1)*g.stride_y+g.total_x > g.total_size
        || b.input.total_size != g.total_size || b.input.n_species < 0
        || !b.input.rho || !b.input.mom_u || !b.input.mom_v || !b.input.mom_w
        || !b.input.eng || !b.eos_status
        || (b.input.n_species && (!b.input.mass_fractions
            || g.total_size > std::numeric_limits<int>::max()/b.input.n_species))
        || !arch::state::valid_bounds(bounds)) return false;
    return GridMetrics::matches_identity(make_grid_geometry_view(g));
}

/** Poison a rejected required cache using only its declared allocation extent.
 * Cache storage belongs to the caller and spans input.total_size. No geometry
 * index or source reader is used when chart/bounds/layout preflight failed.
 * Earlier successful cells cannot turn this sticky failure into stage readiness.
 */
__device__ inline void reject_mean_cache(const DeviceHydroBatchBlock& b,int lane)
{
    if (b.eos_status) atomicExch(b.eos_status,1);
    const double bad=std::numeric_limits<double>::quiet_NaN();
    for (int cell=lane;cell<b.input.total_size;cell+=blockDim.x*gridDim.x) {
        if (b.mean_pressure)b.mean_pressure[cell]=bad;
        if (b.mean_sound_speed)b.mean_sound_speed[cell]=bad;
    }
}

/** Evaluate the original required strip using immutable native V/W means.
 * Workflow: reject malformed Native metadata before reads -> gather Xi with its
 * actual stride -> make the shared three-column density/inertia closure -> query
 * the unchanged EOS on effective_mean -> publish complete finite positive P/c.
 * Native Ekin=J^2/(2 I_* V); no evolved U, abundance, floor or law is changed.
 * Existing uses the original raw mean, EOS call, traversal and write order.
 */
template<class Eos>
__device__ inline void hydro_mean_thermo_work(const DeviceHydroBatchBlock& b,
    Eos eos, SpeciesWorkspaceView workspace,const arch::state::Bounds& bounds)
{
    const int lane=blockIdx.x*blockDim.x+threadIdx.x;
    if (!b.mean_pressure || !b.mean_sound_speed) {
        if (b.grid.semantics!=GridMetrics::GeometrySemantics::Existing
            && (b.mean_pressure || b.mean_sound_speed)) reject_mean_cache(b,lane);
        return;
    }
    const bool native=b.grid.semantics==GridMetrics::GeometrySemantics::AxisymmetricRz;
    if (b.grid.semantics!=GridMetrics::GeometrySemantics::Existing
        && (!native || !native_mean_binding_valid(b,bounds))) {
        reject_mean_cache(b,lane);return;
    }
    // Existing launch validation owns its workspace contract. Native direct
    // mean-only calls also authenticate the borrowed scratch before Xi reads.
    if (native) {
        const std::size_t n=static_cast<std::size_t>(b.input.n_species);
        const std::size_t lanes=static_cast<std::size_t>(blockDim.x)*gridDim.x;
        const bool local=!workspace.values && workspace.capacity==0
            && workspace.lanes==0 && workspace.species==0 && workspace.arrays==0
            && n<=kLocalSpeciesScratchCapacity;
        const bool borrowed=workspace.values && n>0 && workspace.species==b.input.n_species
            && workspace.arrays>=1 && workspace.lanes>0
            && static_cast<std::size_t>(workspace.lanes)>=lanes
            && n<=std::numeric_limits<std::size_t>::max()/static_cast<std::size_t>(workspace.lanes)
            && workspace.capacity>=n*static_cast<std::size_t>(workspace.lanes);
        if (!local && !borrowed) {reject_mean_cache(b,lane);return;}
    }
    SpeciesLaneScratch<1> scratch(workspace, lane);
    double* composition = scratch.array(0);
    const auto checked = make_checked_hydro_eos(eos, b.eos_status);
    for (int cell = lane; cell < b.grid.total_size; cell += blockDim.x * gridDim.x) {
        const int k = cell / b.grid.stride_z;
        const int j = (cell - k * b.grid.stride_z) / b.grid.stride_y;
        const int i = cell - k * b.grid.stride_z - j * b.grid.stride_y;
        const int position[]{i,j,k};
        const int begin[]{b.grid.is,b.grid.js,b.grid.ks};
        const int end[]{b.grid.ie,b.grid.je,b.grid.ke};
        int outside = 0;
        bool needed = true;
        for (int axis = 0; axis < 3; ++axis) {
            if (position[axis] >= begin[axis] && position[axis] < end[axis]) continue;
            ++outside;
            needed &= axis < b.grid.dim
                && (position[axis] == begin[axis]-1 || position[axis] == end[axis]);
        }
        if (!needed || outside > 1) continue;
        for (int s = 0; s < b.input.n_species; ++s)
            composition[s] = b.input.species(s, cell);
        if (native) {
            const auto read=[input=b.input](int index){return input.load(index);};
            const int support=i-1<0?0:(i-1>b.grid.total_x-3?b.grid.total_x-3:i-1);
            const auto closure=RzThermodynamics::make_cell_supported(
                read,cell,make_grid_geometry_view(b.grid),i,support,bounds);
            double p=std::numeric_limits<double>::quiet_NaN(),c=p;
            if (!closure.valid() || arch::state::validate_composition(composition,
                    b.input.n_species,1)!=arch::state::Status::valid) {
                atomicExch(b.eos_status,1);
            } else {
                FluxAdmissibility::required_mean_thermo(closure.effective_mean,
                    b.input.n_species>0?composition:nullptr,checked,p,c);
                if (!std::isfinite(p) || !(p>0.) || !std::isfinite(c) || !(c>0.)) {
                    atomicExch(b.eos_status,1);
                    p=c=std::numeric_limits<double>::quiet_NaN();
                }
            }
            b.mean_pressure[cell]=p;b.mean_sound_speed[cell]=c;
        } else {
            FluxAdmissibility::required_mean_thermo(b.input.load(cell),
                b.input.n_species > 0 ? composition : nullptr, checked,
                b.mean_pressure[cell], b.mean_sound_speed[cell]);
        }
    }
}

/** One block per grid-y invokes the same required mean worker. Physical Bounds
 * come from the existing actual launch arguments; the adapter owns no policy.
 */
template<class Eos>
__global__ void hydro_batch_mean_thermo(const DeviceHydroBatchBlock* blocks,
    Eos eos, SpeciesWorkspaceView workspace,arch::state::Bounds bounds)
{
    hydro_mean_thermo_work(blocks[blockIdx.y],eos,workspace,bounds);
}

/** Reset each block's required-query latch before any CFL candidate runs. */
static __global__ void hydro_batch_cfl_clear(const DeviceHydroDtBatchBlock* blocks)
{ if (threadIdx.x == 0) *blocks[blockIdx.y].status = 0; }

/** Map grid y to blocks and reuse the scalar candidate computation verbatim. */
template<class Eos>
__global__ void hydro_batch_cfl_candidates(const DeviceHydroDtBatchBlock* blocks,
    Eos eos, SpeciesWorkspaceView workspace)
{
    const auto& b = blocks[blockIdx.y];
    hydro_cfl_candidates_work(b.input, b.grid,
        make_checked_hydro_eos(eos, b.status), b.candidates, workspace);
}

/** Reduce each block in its original cell order; no cross-block state is shared. */
static __global__ void hydro_batch_cfl_reduce(const DeviceHydroDtBatchBlock* blocks,
    double cfl)
{
    const auto& b = blocks[blockIdx.y];
    hydro_cfl_reduce_work(b.candidates, b.grid.active_cell_count(), cfl, b.result, b.status);
}

template<class Reconstruction, class Flux, class Eos>
__global__ void hydro_batch_faces(const DeviceHydroBatchBlock* blocks, Eos eos,
    int direction, double coefficient, SpeciesWorkspaceView workspace)
{
    const auto& b = blocks[blockIdx.y];
    if (direction >= b.grid.dim) return;
    hydro_face_kernel_work<Reconstruction, Flux, true>(b.input, b.face_flux, b.grid,
        make_checked_hydro_eos(eos, b.eos_status), direction, coefficient, workspace,
        b.mean_pressure, b.mean_sound_speed, b.roe_wave_speed,
        b.native_bounds, b.eos_status, b.native_walls);
}

static __global__ void hydro_batch_divergence(const DeviceHydroBatchBlock* blocks,
    int direction, double dt)
{
    const auto& b = blocks[blockIdx.y];
    if (direction < b.grid.dim)
        hydro_divergence_kernel_work(b.face_flux, b.delta, b.grid, dt, direction,b.self_gravity);
}

template<class Eos>
__global__ void hydro_batch_sources(const DeviceHydroBatchBlock* blocks, Eos eos,
    double dt, SpeciesWorkspaceView workspace, Physical::Gravity::ExternalGravityView gravity)
{
    const auto& b = blocks[blockIdx.y];
    // Native keeps its original gravity-free geometric source: the external
    // body is consumed afterwards by the dedicated external kernels. Every
    // ordinary block still receives the original external gravity view exactly
    // as before, so only the qualified Native branch changes.
    const Physical::Gravity::ExternalGravityView source_gravity =
        b.grid.semantics == GridMetrics::GeometrySemantics::AxisymmetricRz
            ? Physical::Gravity::ExternalGravityView{} : gravity;
    if (b.grid.geometry != static_cast<int>(DeviceGeometry::Cartesian) || source_gravity.enabled)
        hydro_source_kernel_work(b.input, b.delta, b.grid,
            make_checked_hydro_eos(eos, b.eos_status), dt, workspace, source_gravity, b.eos_status);
    const int linear=blockIdx.x*blockDim.x+threadIdx.x;
    if(b.self_gravity.enabled() && linear<b.grid.active_cell_count()) {
        const int c=b.grid.active_cell(linear);FluidVector delta=b.delta.load(c);
        double* momentum[]{&delta.mom_u,&delta.mom_v,&delta.mom_w};
        for(int a=0;a<b.grid.dim;++a)*momentum[a]+=Physical::Gravity::gravity_momentum(
            b.self_gravity.faces[a][c],b.self_gravity.faces[a][c+b.grid.stride(a)],b.input.rho[c],dt);
        b.delta.store(c,delta);
    }
}

/** Evaluate the shared external cell source for every active Native cell.
 * Workflow: bind the same input readers -> reuse the shared eight-node cell
 * leaf with the original checked EOS adapter, input geometry, Native Bounds and the
 * current before-delta -> keep species scratch in the existing Native lane
 * array -> publish every Valid candidate impulse into the dead face_flux plane
 * only after the original directional AMR registrations completed. A non-Valid
 * candidate latches the existing required-query status and leaves delta
 * untouched. No field, RK weight or authority is produced here.
 */
template<class Eos>
__global__ void hydro_batch_native_external_eval(const DeviceHydroBatchBlock* blocks,
    Eos eos, double dt, SpeciesWorkspaceView workspace,
    Physical::Gravity::ExternalGravityView gravity)
{
    const auto& b = blocks[blockIdx.y];
    if (b.grid.semantics != GridMetrics::GeometrySemantics::AxisymmetricRz) return;
    const int lane = blockIdx.x * blockDim.x + threadIdx.x;
    SpeciesLaneScratch<1> scratch(workspace, lane);
    double* composition = scratch.array(0);
    const auto read = [input = b.input](int cell) { return input.load(cell); };
    const auto fraction = [input = b.input](int species, int cell) {
        return input.species(species, cell);
    };
    const auto geometry = make_grid_geometry_view(b.grid);
    const int ni = b.grid.ie - b.grid.is;
    const int nj = b.grid.je - b.grid.js;
    for (int linear = lane; linear < b.grid.active_cell_count();
         linear += blockDim.x * gridDim.x) {
        const int i = b.grid.is + linear % ni;
        const int j = b.grid.js + (linear / ni) % nj;
        const int cell = b.grid.active_cell(linear);
        const auto candidate = Physical::Gravity::evaluate_native_external_source_cell(
            read, fraction, cell, b.input.n_species,
            make_checked_hydro_eos(eos, b.eos_status), geometry, i, j, dt,
            b.native_bounds, composition, gravity, b.delta.load(cell));
        if (candidate.status != Physical::Gravity::NativeExternalCellStatus::Valid) {
            atomicExch(b.eos_status, 1);
            continue;
        }
        b.face_flux.store(cell, {0.0, candidate.source.mom_u, candidate.source.mom_v,
            candidate.source.mom_w, candidate.source.eng});
    }
}

/** Apply the accepted external impulse and measure its shared V/V/W budget.
 * Workflow: read the patch latch once so any candidate failure vetoes the whole
 * application -> reload the before-delta and the saved impulse -> repeat the
 * same four rounded additions (mom_u, mom_v, mom_w, eng; never rho or species)
 * -> store the after-delta -> reuse the shared rounded-add budget leaf with
 * four zero FP64 locals and write the per-cell row back into the dead
 * face_flux. dt is already carried by the evaluated source; no RK weight or new
 * volume/budget law is introduced.
 */
static __global__ void hydro_batch_native_external_apply(const DeviceHydroBatchBlock* blocks)
{
    const auto& b = blocks[blockIdx.y];
    if (b.grid.semantics != GridMetrics::GeometrySemantics::AxisymmetricRz) return;
    // The eval kernel completed earlier on this stream; the atomic read keeps
    // the veto visible to every applying thread without a new latch owner.
    if (atomicAdd(b.eos_status, 0) != 0) return;
    const auto geometry = make_grid_geometry_view(b.grid);
    const int lane = blockIdx.x * blockDim.x + threadIdx.x;
    const int ni = b.grid.ie - b.grid.is;
    const int nj = b.grid.je - b.grid.js;
    for (int linear = lane; linear < b.grid.active_cell_count();
         linear += blockDim.x * gridDim.x) {
        const int i = b.grid.is + linear % ni;
        const int j = b.grid.js + (linear / ni) % nj;
        const int cell = b.grid.active_cell(linear);
        const FluidVector before = b.delta.load(cell);
        const FluidVector source = b.face_flux.load(cell);
        FluidVector after = before;
        after.mom_u += source.mom_u;
        after.mom_v += source.mom_v;
        after.mom_w += source.mom_w;
        after.eng += source.eng;
        b.delta.store(cell, after);
        double radial = 0.0, axial = 0.0, torque = 0.0, work = 0.0;
        Physical::Gravity::accumulate_native_external_source_budget(radial, axial,
            torque, work, before, after, geometry, i, j);
        b.face_flux.store(cell, {0.0, radial, axial, torque, work});
    }
}

/** Reduce the measured external budget deterministically for one Native block.
 * Workflow: one thread owns the compact row and traverses the actual j rows
 * then i, replicating the Host accumulation order in FP64 -> reset the four
 * outputs even when the patch failed -> latch a non-finite measured row. No
 * atomic partial-sum order, new algorithm or controller is introduced.
 */
static __global__ void hydro_batch_native_external_reduce(const DeviceHydroBatchBlock* blocks)
{
    const auto& b = blocks[blockIdx.y];
    if (blockIdx.x != 0 || threadIdx.x != 0) return;
    if (b.grid.semantics != GridMetrics::GeometrySemantics::AxisymmetricRz) return;
    double radial = 0.0, axial = 0.0, torque = 0.0, work = 0.0;
    if (atomicAdd(b.eos_status, 0) == 0) {
        for (int j = b.grid.js; j < b.grid.je; ++j)
            for (int i = b.grid.is; i < b.grid.ie; ++i) {
                const FluidVector row = b.face_flux.load(b.grid.index(i, j, 0));
                radial += row.mom_u;
                axial += row.mom_v;
                torque += row.mom_w;
                work += row.eng;
            }
    }
    b.native_external_budget[0] = radial;
    b.native_external_budget[1] = axial;
    b.native_external_budget[2] = torque;
    b.native_external_budget[3] = work;
    if (!std::isfinite(radial) || !std::isfinite(axial)
        || !std::isfinite(torque) || !std::isfinite(work))
        atomicExch(b.eos_status, 1);
}

static __global__ void hydro_batch_update(const DeviceHydroBatchBlock* blocks,
    double old_weight, double update_weight, double density_floor,
    double minimum_internal_energy, double maximum_internal_energy)
{
    const auto& b = blocks[blockIdx.y];
    hydro_single_stage_update_kernel_work(b.old_state, b.input, b.output, b.delta,
        b.grid, old_weight, update_weight, density_floor,
        minimum_internal_energy, maximum_internal_energy, b.eos_status, b.repairs);
}

/** Validate all POD launch storage before any clear/flux/register write.
 * The actual backend additionally authenticates leases and original plan modes.
 * Native borrows factory Bounds/walls and 41*S real lanes; Existing retains its
 * original view, stencil, four-plane workspace and cached-metric checks.
 */
template<class Reconstruction>
bool valid_hydro_batch_block(const DeviceHydroBatchBlock& b, SpeciesWorkspaceView workspace)
{
    const bool native = b.grid.semantics == GridMetrics::GeometrySemantics::AxisymmetricRz;
    if (!valid_hydro_grid(b.grid) || !b.eos_status
        || (!native && b.grid.semantics != GridMetrics::GeometrySemantics::Existing)
        || !valid_species_workspace(workspace, b.input.n_species, native ? 41 : 4)
        || (native && b.input.n_species > 0 && !workspace.values)
        || b.grid.ng < Reconstruction::ghost_depth || !b.grid.cell_volume
        || make_grid_geometry_view(b.grid).geometry == GridMetrics::Geometry::Unsupported)
        return false;
    if (native) {
        using Selected = typename Reconstruction::shared_policy;
        if (!native_face_storage_valid(b.input, b.face_flux, b.grid, 0,
                b.grid.is - 1, b.grid.js, b.native_bounds, 0.0)
            || (b.self_gravity.enabled() && (b.self_gravity.density != b.input.rho
                || !b.self_gravity.faces[0] || !b.self_gravity.faces[1]
                || !b.self_gravity.work_low[0] || !b.self_gravity.work_low[1]
                || !b.self_gravity.work_high[0] || !b.self_gravity.work_high[1]))
            || b.repairs.semantics != state::RepairSemantics::RzVolumeAngular
            || (b.grid.dyadic_identity.root_lower[0] == 0. && b.grid.ng < 3
                && (RzSelectedReconstruction::PolicyTraits<Selected>::kind == 1
                    || AMRInterfaceReconstruction::needs_tvd_interface_stencil(
                        Reconstruction::ghost_depth, b.grid.amr_coarse_fine_face,
                        0, b.grid.is - 1, b.grid.is, b.grid.ie))))
            return false;
    }
    for (const auto view : {b.old_state, b.input, b.output, b.delta, b.face_flux})
        if (!valid_hydro_view(view) || view.total_size != b.grid.total_size
            || view.n_species != b.input.n_species) return false;
    const int begin[]{b.grid.is, b.grid.js, b.grid.ks};
    const int end[]{b.grid.ie, b.grid.je, b.grid.ke};
    const int total[]{b.grid.total_x, b.grid.total_y, b.grid.total_z};
    for (int axis = 0; axis < b.grid.dim; ++axis)
        if (begin[axis] < Reconstruction::ghost_depth
            || total[axis] - end[axis] < Reconstruction::ghost_depth
            || !b.grid.face_area_lower[axis] || !b.grid.face_area_upper[axis]) return false;
    return true;
}

} // namespace detail

template<class Reconstruction, class Flux, class Eos>
CudaBackendLaunchResult launch_hydro_batch(
    std::span<const DeviceHydroBatchBlock> host, const DeviceHydroBatchBlock* device,
    Eos eos, double coefficient, double density_floor, double min_e, double max_e,
    const scheduler::StageDescriptor& descriptor, double dt, cudaStream_t stream,
    SpeciesWorkspaceView workspace, Physical::Gravity::ExternalGravityView gravity)
{
    CudaBackendLaunchResult result{};
    if (host.empty()) return result;
    if (!device) return {cudaErrorInvalidValue, 0, true};
    const state::Bounds bounds{density_floor, min_e, max_e};
    for (const auto& b : host) {
        if (!detail::valid_hydro_batch_block<Reconstruction>(b, workspace))
            return {cudaErrorInvalidValue, 0, true};
        const bool native =
            b.grid.semantics == GridMetrics::GeometrySemantics::AxisymmetricRz;
        // The compact Native external observer is qualified only for the exact
        // external branch: a gravity-enabled Native block must carry one row
        // pointer, and every other block must carry none. Self borrows the
        // authenticated resident field and its original paired row mapping;
        // simultaneous external and Self sources are invalid.
        if ((gravity.enabled && native) != (b.native_external_budget != nullptr))
            return {cudaErrorInvalidValue, 0, true};
        if (native && gravity.enabled && b.self_gravity.enabled())
            return {cudaErrorInvalidValue, 0, true};
        if (native
            && (!std::isfinite(dt) || !(dt > 0.)
                || !std::isfinite(coefficient) || !state::valid_bounds(bounds)
                || b.native_bounds.density != bounds.density
                || b.native_bounds.internal_min != bounds.internal_min
                || b.native_bounds.internal_max != bounds.internal_max))
            return {cudaErrorInvalidValue, 0, true};
    }
    // Wide species use the existing bounded lane arena. One block per wave
    // avoids cross-block aliasing without multiplying its memory budget.
    // Local per-thread species scratch can execute many blocks concurrently.
    const std::size_t wave_limit = workspace.values ? 1 : 1024;
    for (std::size_t first = 0; first < host.size(); first += wave_limit) {
        const auto count = std::min(wave_limit, host.size() - first);
        const auto wave = host.subspan(first, count);
        int cells = 0, storage = 0, faces = 0, dimension = 0;
        bool sources = gravity.enabled;
        for (const auto& b : wave) {
            cells = std::max(cells, b.grid.active_cell_count());
            storage = std::max(storage, b.grid.total_size);
            dimension = std::max(dimension, b.grid.dim);
            sources |= b.grid.geometry != static_cast<int>(DeviceGeometry::Cartesian) || b.self_gravity.enabled();
            for (int d = 0; d < b.grid.dim; ++d) {
                const int extent[]{b.grid.ie - b.grid.is, b.grid.je - b.grid.js, b.grid.ke - b.grid.ks};
                faces = std::max(faces, (extent[0] + (d == 0))
                    * (extent[1] + (d == 1)) * (extent[2] + (d == 2)));
            }
        }
        const auto record = [&] {
            result.error = cudaGetLastError();
            if (result.error == cudaSuccess) ++result.kernels_launched;
            return result.error == cudaSuccess;
        };
        const dim3 storage_grid(detail::hydro_launch_blocks(storage, 128), static_cast<unsigned>(count));
        const dim3 cell_grid(detail::hydro_launch_blocks(cells, 128), static_cast<unsigned>(count));
        const dim3 face_grid(detail::species_launch_blocks(faces, workspace), static_cast<unsigned>(count));
        const dim3 source_grid(detail::species_launch_blocks(cells, workspace), static_cast<unsigned>(count));
        const int threads = detail::species_launch_threads(workspace);
        const auto* bindings = device + first;
        detail::hydro_batch_clear<<<storage_grid, 128, 0, stream>>>(bindings);
        if (!record()) return result;
        const bool means = std::any_of(wave.begin(), wave.end(),
            [](const auto& b) { return b.mean_pressure && b.mean_sound_speed; });
        if (means) {
            const dim3 mean_grid(detail::species_launch_blocks(storage, workspace),
                                 static_cast<unsigned>(count));
            detail::hydro_batch_mean_thermo<<<mean_grid, threads, 0, stream>>>(
                bindings,eos,workspace,{density_floor,min_e,max_e});
            if (!record()) return result;
        }
        for (int direction = 0; direction < dimension; ++direction) {
            detail::hydro_batch_faces<Reconstruction, Flux><<<face_grid, threads, 0, stream>>>(
                bindings, eos, direction, coefficient, workspace);
            if (!record()) return result;
            detail::hydro_batch_divergence<<<cell_grid, 128, 0, stream>>>(bindings, direction, dt);
            if (!record()) return result;
            // Routes can accumulate into the same surface. Preserve Host route
            // order and complete registration before overwriting this direction.
            for (const auto& b : wave) {
                if (direction >= b.grid.dim) continue;
                const auto registered = launch_cuda_amr_flux_register(b.routes[direction],
                    AmrFluxSource::StageScratch, descriptor.flux_register_weight, stream);
                result.error = registered.error;
                result.kernels_launched += registered.kernels_launched;
                if (result.error != cudaSuccess) return result;
            }
        }
        if (sources) {
            detail::hydro_batch_sources<<<source_grid, threads, 0, stream>>>(bindings, eos, dt, workspace, gravity);
            if (!record()) return result;
        }
        // Only a gravity-enabled Native member consumes the shared external
        // body: after the original directional registration and source pass,
        // evaluate -> apply -> reduce on this same stream. Non-Native members
        // return before any read, and pure ordinary waves launch nothing here.
        const bool native_external = gravity.enabled
            && std::any_of(wave.begin(), wave.end(), [](const auto& b) {
                return b.grid.semantics == GridMetrics::GeometrySemantics::AxisymmetricRz; });
        if (native_external) {
            detail::hydro_batch_native_external_eval<Eos><<<source_grid, threads, 0, stream>>>(
                bindings, eos, dt, workspace, gravity);
            if (!record()) return result;
            detail::hydro_batch_native_external_apply<<<cell_grid, 128, 0, stream>>>(bindings);
            if (!record()) return result;
            const dim3 external_row(1, static_cast<unsigned>(count));
            detail::hydro_batch_native_external_reduce<<<external_row, 1, 0, stream>>>(bindings);
            if (!record()) return result;
        }
        detail::hydro_batch_update<<<cell_grid, 128, 0, stream>>>(bindings,
            descriptor.old_weight, descriptor.update_weight, density_floor, min_e, max_e);
        if (!record()) return result;
    }
    return result;
}

/** Batch CFL work without changing candidate, NaN, reduction or CFL formulas.
 * Global species scratch remains bounded and therefore uses single-block waves.
 * Local per-thread scratch permits independent blocks in the same launch.
 */
template<class Eos>
CudaBackendLaunchResult launch_hydro_dt_batch(
    std::span<const DeviceHydroDtBatchBlock> host, const DeviceHydroDtBatchBlock* device,
    Eos eos, double cfl, SpeciesWorkspaceView workspace, cudaStream_t stream)
{
    CudaBackendLaunchResult result{};
    if (host.empty()) return result;
    if (!device) return {cudaErrorInvalidValue, 0, true};
    for (const auto& b : host)
        if (!valid_hydro_view(b.input) || !valid_hydro_grid(b.grid)
            || b.input.total_size != b.grid.total_size || b.grid.active_cell_count() <= 0
            || !b.candidates || !b.result || !b.status
            || !valid_species_workspace(workspace, b.input.n_species, 1))
            return {cudaErrorInvalidValue, 0, true};
    const std::size_t wave_limit = workspace.values ? 1 : 1024;
    for (std::size_t first = 0; first < host.size(); first += wave_limit) {
        const auto count = std::min(wave_limit, host.size() - first);
        int cells = 0;
        for (const auto& b : host.subspan(first, count))
            cells = std::max(cells, b.grid.active_cell_count());
        const dim3 one(1, static_cast<unsigned>(count));
        const dim3 grid(detail::species_launch_blocks(cells, workspace),
                        static_cast<unsigned>(count));
        const auto record = [&] {
            result.error = cudaGetLastError();
            if (result.error == cudaSuccess) ++result.kernels_launched;
            return result.error == cudaSuccess;
        };
        detail::hydro_batch_cfl_clear<<<one, 1, 0, stream>>>(device + first);
        if (!record()) return result;
        detail::hydro_batch_cfl_candidates<<<grid, detail::species_launch_threads(workspace), 0, stream>>>(
            device + first, eos, workspace);
        if (!record()) return result;
        detail::hydro_batch_cfl_reduce<<<one, 1, 0, stream>>>(device + first, cfl);
        if (!record()) return result;
    }
    return result;
}

} // namespace arch::cuda
