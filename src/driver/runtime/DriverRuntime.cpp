/**
 * @file DriverRuntime.cpp
 * @brief Own live mesh, state residency and backend resources for the integration loop.
 *
 * Workflow:
 * 1. Borrow resolved configuration, species and the actual run EOS.
 * 2. Stage topology/ledger identity before real physical BC and halo exchange.
 * 3. For native RZ, preflight the whole Host domain and validate the shared
 *    thermodynamic closure against the actual EOS before ghost publication.
 * 4. Publish completed topology/state identities to the next scheduled stage.
 */

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>
#include <utility>

#include "driver/runtime/DriverRuntime.h"

#include "amr/AMRControl.h"
#include "driver/DriverUtils.h"
#include "driver/schedule/DriverControl.h"
#include "numerics/integrator/TimeIntegratorHelper.h"

namespace arch::driver {
/** Return the shared floor-repair budget for the active run. */
state::RepairBudget& DriverRuntime::repair_budget() { return ctrl.repairs; }
using scheduler::StageExecutionContext;
using state::ExecutionSide;
using state::StateResidencyLedger;
using state::StateSlot;
using topology::LogicalBlockIdentity;
using topology::TopologyObservation;
/** Freeze only layout/geometry identity, never a second copy of the fluid arrays. */
namespace {
struct NativeBoundaryPatch {
    int pool_index;
    const amr::Block* block;
    const Grid* grid;
    int level;
    std::array<std::uint32_t, 3> logical;
    std::array<int, 8> layout;
    std::array<double, 9> coordinates;
};

/** Capture the actual active grid layout used by one candidate handle. */
NativeBoundaryPatch native_boundary_patch(int id, const amr::Block& block)
{
    const auto& grid = block.grid;
    return {id, &block, &grid, block.level,
        {block.logical_x1, block.logical_x2, block.logical_x3},
        {grid.dim, grid.ng, grid.stride_y, grid.stride_z, grid.total_size,
         grid.nblockx1, grid.nblockx2, grid.nblockx3},
        {grid.x1_min, grid.x1_max, grid.x2_min, grid.x2_max,
         grid.x3_min, grid.x3_max, grid.dx1, grid.dx2, grid.dx3}};
}

/** Compare real pool/grid identity and geometry before touching the EOS. */
bool native_boundary_patch_matches(const NativeBoundaryPatch& before,
    int id, const amr::Block& block)
{
    const auto after = native_boundary_patch(id, block);
    return before.pool_index == after.pool_index && before.block == after.block
        && before.grid == after.grid && before.level == after.level
        && before.logical == after.logical && before.layout == after.layout
        && before.coordinates == after.coordinates
        && block.id == id && block.active
        && block.grid.dim == 2 && block.grid.geometry == "cylindrical";
}

/** Preflight all selected vectors without recovering a single thermodynamic cell. */
void require_native_boundary_layout(const FluidState& fluid,
    const Grid& grid, int species)
{
    const int signed_cells = grid.GetTotalSize();
    if (signed_cells <= 0 || species < 0
        || fluid.block_total_size_ != signed_cells || fluid.GetNumSpecies() != species)
        throw std::logic_error("Native RZ boundary slot/species extent mismatch");
    const auto cells = static_cast<std::size_t>(signed_cells);
    for (const auto* values : {&fluid.rho, &fluid.mom_u, &fluid.mom_v,
                              &fluid.mom_w, &fluid.eng, &fluid.enuc_rate})
        if (values->size() != cells)
            throw std::logic_error("Native RZ boundary vector extent mismatch");
    if (fluid.mass_fractions.size() != cells * static_cast<std::size_t>(species))
        throw std::logic_error("Native RZ boundary composition extent mismatch");
}
} // namespace

/** Lower existing physical controls once when the actual EOS is borrowed. */
state::Bounds DriverRuntime::native_rz_eos_bounds() const
{
    const state::Bounds bounds{config.numerics.sml_rho,
        config.numerics.min_eint, config.numerics.max_eint};
    if (!state::valid_bounds(bounds))
        throw std::invalid_argument("Native RZ EOS binding requires valid physical bounds");
    return bounds;
}

/** Borrow the existing immutable species layout; no new parameter is introduced. */
int DriverRuntime::native_rz_species_count() const { return specs.count(); }

/** Attach the real EOS gate to the exact staged ledger and borrowed handle order.
 * The callback takes its actual calling context explicitly: stage_context()
 * returns by value, so capturing a reference to its local return object would
 * dangle. Only owned Runtime state and bounded immutable identity snapshots are
 * captured. Initial/regrid candidates may have an uncommitted replacement
 * ledger; neither is substituted with the accepted Runtime ledger.
 */
void DriverRuntime::bind_native_boundary_acceptance(StageExecutionContext& context,
    std::span<const amr::BlockHandle> handles)
{
    if (geometry_semantics_ != GridMetrics::GeometrySemantics::AxisymmetricRz)
        return;
    if (!native_rz_eos_acceptance_)
        throw std::logic_error("Native RZ boundary acceptance requires an explicitly bound EOS");
    if (compute_backend || context.side != ExecutionSide::Host
        || &context.clock != &scheduler_clock)
        throw std::logic_error("Native RZ EOS boundary acceptance requires the real Host clock/side");
    const auto& active = amr_ctrl.tree->GetActiveBlocks();
    if (handles.empty() || handles.size() != active.size())
        throw std::logic_error("Native RZ EOS boundary domain extent mismatch");
    auto* const ledger = &context.ledger;
    const bool committed_domain = ledger == residency_ledger.get();
    if (committed_domain)
        topology_registry.validate_committed_snapshot(observe_topology());
    const auto epoch = ledger->active_epoch();
    const int species = specs.count();
    std::vector<amr::BlockHandle> frozen_handles(handles.begin(), handles.end());
    std::unordered_set<std::uint64_t> unique_uids;
    unique_uids.reserve(handles.size());
    std::vector<NativeBoundaryPatch> patches;
    patches.reserve(active.size());
    for (std::size_t index = 0; index < active.size(); ++index) {
        if (!amr::is_valid(handles[index]) || handles[index].epoch != epoch
            || !unique_uids.insert(handles[index].uid.value).second)
            throw std::logic_error("Native RZ EOS boundary handles are invalid or repeated");
        (void)ledger->inspect({handles[index], StateSlot::Current});
        if (committed_domain
            && handles[index] != topology_registry.handle_for_pool(active[index]))
            throw std::logic_error("Native RZ EOS boundary handle/pool correspondence changed");
        const auto& block = amr_ctrl.pool->GetBlock(active[index]);
        patches.push_back(native_boundary_patch(active[index], block));
    }
    auto* const pool = amr_ctrl.pool.get();
    auto* const tree = amr_ctrl.tree.get();
    const auto boundary_snapshot = bc_handler.snapshot_stage_context();
    const auto eos_acceptance = native_rz_eos_acceptance_;
    context.post_boundary_acceptance = [this, ledger, committed_domain, epoch, species, pool, tree,
        handles, frozen_handles = std::move(frozen_handles), patches = std::move(patches),
        boundary_snapshot, eos_acceptance](const StageExecutionContext& actual,
            StateSlot slot, state::StateVersion version) {
        const auto member = TimeIntegration::hydro_boundary_state_member(slot);
        const auto require_frame = [&] {
            if (actual.side != ExecutionSide::Host || compute_backend
                || &actual.ledger != ledger || &actual.clock != &scheduler_clock
                || (committed_domain && (residency_ledger.get() != ledger
                    || stage_handles.data() != handles.data()
                    || stage_handles.size() != handles.size()))
                || ledger->active_epoch() != epoch || specs.count() != species
                || amr_ctrl.pool.get() != pool || amr_ctrl.tree.get() != tree
                || !bc_handler.stage_context_matches(boundary_snapshot))
                throw std::logic_error("Native RZ EOS boundary context/owner changed");
            // Gate before ghost publication: only accepted exact-version Host
            // INTERIORS are required. A pending ghost transfer also rejects.
            for (const auto handle : frozen_handles) {
                ledger->require_readable({handle, slot},
                    {ExecutionSide::Host, version, true, false});
                const auto coherence = ledger->inspect({handle, slot});
                if (coherence.interior.pending_transfer != state::PendingTransferPhase::None
                    || coherence.ghost.pending_transfer != state::PendingTransferPhase::None)
                    throw std::logic_error("Native RZ EOS boundary has pending state transfer");
            }
            const auto& current_active = amr_ctrl.tree->GetActiveBlocks();
            if (current_active.size() != patches.size()
                || handles.size() != frozen_handles.size()
                || !std::equal(handles.begin(), handles.end(), frozen_handles.begin()))
                throw std::logic_error("Native RZ EOS boundary borrowed domain changed");
            // Complete domain/layout preflight precedes the first EOS call;
            // a late invalid handle/slot cannot partly accept earlier patches.
            for (std::size_t index = 0; index < patches.size(); ++index) {
                const auto& block = amr_ctrl.pool->GetBlock(current_active[index]);
                if (!native_boundary_patch_matches(patches[index], current_active[index], block))
                    throw std::logic_error("Native RZ EOS boundary active pool/grid order changed");
                if (committed_domain && frozen_handles[index]
                    != topology_registry.handle_for_pool(current_active[index]))
                    throw std::logic_error("Native RZ EOS boundary handle/pool correspondence changed");
                require_native_boundary_layout(block.*member, block.grid, species);
            }
        };
        require_frame();
        for (std::size_t index=0;index<patches.size();++index) {
            const auto& patch=patches[index];
            const auto& block = amr_ctrl.pool->GetBlock(patch.pool_index);
            try {
                eos_acceptance(block.*member, block.grid);
            } catch(const RzThermodynamics::AcceptanceError& error) {
                // Frame/owner drift is fatal before attaching provenance. The
                // original phase/status survives; mean/physical EOS faults are
                // not converted into an effective-thermal failure.
                require_frame();
                throw NativeBoundaryAcceptanceError(error.what(),patch.pool_index,
                    frozen_handles[index],slot,version,error.diagnostic());
            }
        }
        // EOS and callbacks must not change the BC time/purpose/revision or
        // any publication/layout owner during the real whole-domain gate.
        require_frame();
    };
}

/** Return a context bound to the live ledger, with mandatory native-RZ EOS gate. */
StageExecutionContext DriverRuntime::stage_context()
{
    if (!residency_ledger || stage_handles.empty())
        throw std::logic_error("state residency is not initialized");
    StageExecutionContext context{
        compute_backend ? ExecutionSide::Device : ExecutionSide::Host,
        *residency_ledger, scheduler_clock};
    bind_native_boundary_acceptance(context, stage_handles);
    return context;
}
/** Borrow core run owners and initialize state/version bookkeeping. */
DriverRuntime::DriverRuntime(amr::AMRControl& control, BCHandler& boundaries,
    const SimConfig& settings, const SpeciesManager& species, SimulationController& controller)
    : geometry_semantics_(boundaries.geometry_semantics()), amr_ctrl(control), bc_handler(boundaries), config(settings), specs(species), ctrl(controller),
      topology_registry(topology::TopologyDomainBounds{
          config.grid.dim,
          {static_cast<std::uint32_t>(std::max(1, config.grid.nblockx1)),
           static_cast<std::uint32_t>(std::max(1, config.grid.nblockx2)),
           static_cast<std::uint32_t>(std::max(1, config.grid.nblockx3))},
          config.amr.lrefinemax}) {
    ctrl.repairs.bind_semantics(geometry_semantics_==GridMetrics::GeometrySemantics::AxisymmetricRz
        ? state::RepairSemantics::RzVolumeAngular : state::RepairSemantics::ExistingVolume);
}
DriverRuntime::~DriverRuntime() = default;
/** Snapshot logical identities for the requested active block order. */
std::vector<TopologyObservation> DriverRuntime::observe_blocks(std::span<const int> active) const
{
    std::vector<TopologyObservation> observations;
    observations.reserve(active.size());
    for (const int pool_index : active) {
        const amr::Block& block = amr_ctrl.pool->GetBlock(pool_index);
        observations.push_back({
            pool_index,
            LogicalBlockIdentity{
                config.grid.dim, block.level, block.logical_x1,
                block.logical_x2, block.logical_x3}});
    }
    return observations;
}

/** Snapshot the complete current AMR topology for reconciliation. */
std::vector<TopologyObservation> DriverRuntime::observe_topology() const
{
    const auto& active = amr_ctrl.tree->GetActiveBlocks();
    return observe_blocks(active);
}

/** Return the committed interior version of the current state. */
state::StateVersion DriverRuntime::current_interior_version() const
{
    if (!residency_ledger || stage_handles.empty())
        throw std::logic_error("state residency is not initialized");
    const arch::state::StateVersion version = residency_ledger->inspect(
        {stage_handles.front(), StateSlot::Current}).interior.version;
    for (const amr::BlockHandle handle : stage_handles) {
        if (residency_ledger->inspect({handle, StateSlot::Current})
                .interior.version != version) {
            throw std::logic_error(
                "active Current interiors do not share one version");
        }
    }
    return version;
}

/** Consume accepted interiors only after whole-domain publication preflight.
 * Current slot/storage identity alone is not a publication lease. Keep the
 * physical EOS/Jeans computation in the existing tree/backend owners.
 */
std::vector<double> DriverRuntime::evaluate_current_jeans_resolution()
{
    topology_registry.validate_committed_snapshot(observe_topology());
    const auto version = current_interior_version();
    const auto& active = amr_ctrl.tree->GetActiveBlocks();
    if (active.size() != stage_handles.size()
        || (compute_backend && backend_storage.size() != active.size()))
        throw std::logic_error("JENS Current topology/storage extent mismatch");
    const auto side = compute_backend ? ExecutionSide::Device : ExecutionSide::Host;
    std::vector<backend::BackendStateAccess> accesses;
    if (compute_backend) accesses.reserve(active.size());
    // Do not evaluate earlier blocks before a later publication fails.
    for (std::size_t index = 0; index < active.size(); ++index) {
        residency_ledger->require_readable(
            {stage_handles[index], StateSlot::Current}, {side, version, true, false});
        if (compute_backend) {
            const auto access = backend_access(index, StateSlot::Current);
            if (!compute_backend->contains(access))
                throw std::logic_error("JENS Current backend storage is unavailable");
            accesses.push_back(access);
        }
    }
    std::vector<double> result;
    if (compute_backend) result = compute_backend->evaluate_jeans_resolution(accesses);
    else {
        result.reserve(active.size());
        for (int id : active)
            result.push_back(amr_ctrl.tree->MinimumJeansCells(amr_ctrl.pool->GetBlock(id)));
    }
    if (result.size() != active.size())
        throw std::logic_error("JENS Current summary extent mismatch");
    for (double value : result)
        if (!std::isfinite(value) || value <= 0.)
            throw std::runtime_error("JENS Current summary is invalid");
    return result;
}

/** Lower one host fluid state to the backend transfer view. */
backend::HostStateTransferView DriverRuntime::host_transfer_view(FluidState& state)
{
    const std::size_t cells = state.rho.size();
    const std::size_t species = static_cast<std::size_t>(
        state.GetNumSpecies());
    return arch::backend::HostStateTransferView{
        state.rho.data(), state.mom_u.data(), state.mom_v.data(),
        state.mom_w.data(), state.eng.data(), state.enuc_rate.data(),
        species == 0 ? nullptr : state.mass_fractions.data(), cells,
        species, species == 0 ? 0 : cells};
}

/** Lease one backend block and state slot with generation identity. */
backend::BackendStateAccess DriverRuntime::backend_access(std::size_t block_index, StateSlot slot) const
{
    if (!compute_backend)
        throw std::logic_error("CUDA backend is not constructed");
    if (block_index >= stage_handles.size()
        || block_index >= backend_storage.size())
        throw std::out_of_range("CUDA backend block index is invalid");
    return arch::backend::BackendStateAccess{
        stage_handles[block_index], backend_storage[block_index], slot};
}

/** Record backend transfer/launch deltas for the requested stage. */
void DriverRuntime::trace_backend_operation(backend::BackendOperation operation, StateSlot slot, const backend::BackendCounters& before)
{
    const auto after = compute_backend->counters();
    for (std::size_t index = 0; index < stage_handles.size(); ++index) {
        compute_backend->append_trace({
            static_cast<std::uint64_t>(ctrl.step_count), operation,
            stage_handles[index], backend_storage[index], slot,
            residency_ledger->inspect({stage_handles[index], slot}),
            index == 0 ? after.bytes_h2d - before.bytes_h2d : 0,
            index == 0 ? after.bytes_d2h - before.bytes_d2h : 0,
            index == 0 ? after.kernel_count - before.kernel_count : 0,
            index == 0
                ? after.stream_sync_count - before.stream_sync_count : 0});
    }
}

/** Register initial block identities and their state residency. */
void DriverRuntime::initialize_topology()
{
    if(host_hydro_transaction_)throw std::logic_error("Active Host Hydro owner excludes topology/backend mutation");
    if (geometry_semantics_ == GridMetrics::GeometrySemantics::AxisymmetricRz
        && !native_rz_eos_acceptance_)
        throw std::logic_error("Native RZ initialization requires an explicitly bound EOS");
    for (int id:amr_ctrl.tree->GetActiveBlocks())
        (void)bc_handler.logical_plan(amr_ctrl.pool->GetBlock(id).grid);
    auto initial_candidate =
        topology_registry.stage_adoption(observe_topology());
    std::unique_ptr<StateResidencyLedger> staged_initial_ledger;
    std::vector<amr::BlockHandle> staged_initial_handles;
    const auto initial_topology = topology_registry.commit_after_success(
        std::move(initial_candidate), [&](const auto& proposed) {
            auto replacement =
                std::make_unique<StateResidencyLedger>(proposed.epoch);
            const arch::scheduler::PublicationWitness initial_witness =
                scheduler_clock.next_publication();
            for (const amr::BlockHandle handle
                 : proposed.handles_in_observation_order) {
                replacement->register_block(handle, initial_witness.version,
                                            initial_witness.completion);
            }
            // A restart can deliberately skip the first regrid and initial
            // output. Establish real host ghost data and publish it here so
            // diffusion/hydro never starts from an interior-only Current slot.
            StageExecutionContext staged_context{
                ExecutionSide::Host, *replacement, scheduler_clock};
            bind_native_boundary_acceptance(
                staged_context, proposed.handles_in_observation_order);
            TimeIntegration::synchronize_domain_boundary(amr_ctrl, bc_handler,
                &amr::Block::fluid_state, proposed.handles_in_observation_order,
                geometry_semantics_,
                {config.numerics.sml_rho,config.numerics.min_eint,config.numerics.max_eint});
            (void)arch::scheduler::complete_boundary(
                staged_context, proposed.handles_in_observation_order,
                StateSlot::Current, initial_witness.version,
                [](StateSlot, arch::state::StateVersion,
                   arch::state::CompletionToken token) { return token; });
            staged_initial_handles = proposed.handles_in_observation_order;
            staged_initial_ledger = std::move(replacement);
        });
    stage_handles = std::move(staged_initial_handles);
    residency_ledger = std::move(staged_initial_ledger);
    if (initial_topology.handles_in_observation_order != stage_handles)
        throw std::logic_error("initial topology commit result mismatch");
    amr_ctrl.BindActiveHandles(stage_handles);

}

/** Build topology bindings for backend storage allocation. */
std::vector<backend::BackendTopologyBinding> DriverRuntime::prepare_backend_bindings()
{
    if(host_hydro_transaction_)throw std::logic_error("Active Host Hydro owner excludes topology/backend mutation");
    if (geometry_semantics_==GridMetrics::GeometrySemantics::AxisymmetricRz)
        throw std::logic_error("RZ device runtime is not yet migrated");
    const auto& active = amr_ctrl.tree->GetActiveBlocks();
    if (active.empty() || stage_handles.size() != active.size()) {
        throw std::logic_error(
            "CUDA backend requires a complete active topology");
    }
    bool needs_host_ghosts = false;
    for (const auto handle : stage_handles) {
        const auto coherence = residency_ledger->inspect(
            {handle, StateSlot::Current});
        needs_host_ghosts = needs_host_ghosts
            || !arch::state::side_can_read(
                coherence.ghost.residency, ExecutionSide::Host)
            || coherence.ghost.version != coherence.interior.version
            || coherence.ghost_source_version != coherence.interior.version;
    }
    if (needs_host_ghosts)
        materialize_current_for_host();

    std::vector<backend::BackendTopologyBinding> bindings;
    bindings.reserve(active.size());
    backend_storage.clear();
    backend_storage.reserve(active.size());
    for (std::size_t index = 0; index < active.size(); ++index) {
        const auto storage = storage_generation_issuer.issue();
        backend_storage.push_back(storage);
        bindings.push_back({
            &amr_ctrl.pool->GetBlock(active[index]), stage_handles[index],
            storage, &bc_handler.logical_plan()});
    }
    return bindings;
}

/** Install a validated compute backend and its resident block views. */
void DriverRuntime::install_backend(std::unique_ptr<backend::ComputeBackend> backend)
{
    if(host_hydro_transaction_)throw std::logic_error("Active Host Hydro owner excludes topology/backend mutation");
    if (geometry_semantics_==GridMetrics::GeometrySemantics::AxisymmetricRz)
        throw std::logic_error("RZ device runtime is not yet migrated");
    if (compute_backend || !backend) throw std::logic_error("invalid backend installation");
    compute_backend = std::move(backend);
}

/** Upload accepted case initial state before device stepping. */
void DriverRuntime::upload_initial_state()
{
    if(host_hydro_transaction_)throw std::logic_error("Active Host Hydro owner excludes topology/backend mutation");
    const auto& active = amr_ctrl.tree->GetActiveBlocks();
    for (std::size_t index = 0; index < active.size(); ++index) {
        amr::Block& block = amr_ctrl.pool->GetBlock(active[index]);
        (void)arch::backend::transfer_state_regions(
            *compute_backend, *residency_ledger, scheduler_clock,
            backend_access(index, StateSlot::Current),
            host_transfer_view(block.fluid_state),
            arch::state::PendingTransferPhase::PendingH2D, 0,
            arch::backend::BackendOperation::InitialUpload);
    }
    compute_backend->prepare_amr_flux_plan(
        amr_ctrl.RequireFluxTopologyPlan(specs.count()),
        amr_ctrl.RequireRefluxTopologyPlan(specs.count()));
}
} // namespace arch::driver
