/**
 * @file DriverRuntime.cpp
 * @brief Own live mesh, state residency and backend resources for the integration loop.
 *
 * Workflow:
 * 1. Borrow resolved configuration, species and the actual run EOS.
 * 2. Stage topology/ledger identity before real physical BC and halo exchange.
 * 3. Bootstrap the actual resident AMR flux/reflux chart before publishing it
 *    to the next numerical stage.
 */

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <string_view>
#include <stdexcept>
#include <unordered_set>
#include <utility>

#include "driver/runtime/DriverRuntime.h"

#include "amr/AMRControl.h"
#include "driver/DriverUtils.h"
#include "driver/schedule/DriverControl.h"
#include "numerics/diffusion/DiffFunction.h"
#include "numerics/diffusion/DiffusionTypes.h"
#include "physics/boundary/PhysicalBoundaryHandler.h"
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
/** Return a context bound to the live ledger. */
StageExecutionContext DriverRuntime::stage_context()
{
    if (!residency_ledger || stage_handles.empty())
        throw std::logic_error("state residency is not initialized");
    StageExecutionContext context{
        compute_backend ? ExecutionSide::Device : ExecutionSide::Host,
        *residency_ledger, scheduler_clock};
    return context;
}
/** Borrow core run owners and initialize state/version bookkeeping. */
DriverRuntime::DriverRuntime(amr::AMRControl& control, BCHandler& boundaries,
    const SimConfig& settings, const SpeciesManager& species, SimulationController& controller)
    : amr_ctrl(control), bc_handler(boundaries), config(settings), specs(species), ctrl(controller),
      topology_registry(topology::TopologyDomainBounds{
          config.grid.dim,
          {static_cast<std::uint32_t>(std::max(1, config.grid.nblockx1)),
           static_cast<std::uint32_t>(std::max(1, config.grid.nblockx2)),
           static_cast<std::uint32_t>(std::max(1, config.grid.nblockx3))},
          config.amr.lrefinemax}) {
    if(const char* value=std::getenv("ARCH_TRACE_DIFFUSION_ACTIVITY")) {
        const std::string_view option(value);
        if(option!="0"&&option!="1")
            throw std::invalid_argument("ARCH_TRACE_DIFFUSION_ACTIVITY must be exactly 0 or 1");
        diffusion_activity_enabled_=option=="1";
    }
    diffusion_activity_totals_.process_start_time=ctrl.t_current;
    diffusion_activity_totals_.process_start_step=ctrl.step_count;
}
DriverRuntime::~DriverRuntime() = default;

/** Read two existing energy planes after the real final BC/EOS gate returned.
 * This records endpoint redistribution, not time-integrated heat or intermediate
 * RKL excursions. Existing repairs/viscous work remain part of accepted E.
 */
void DriverRuntime::observe_completed_diffusion_activity(const scheduler::RklPlan& plan)
{
    if(!diffusion_activity_enabled_)return;
    if(plan.stages.empty())throw std::logic_error("Diffusion activity lacks its actual RKL plan");
    const auto& rotation=plan.final_rotation;
    std::optional<StateSlot> retained_seed;
    for(const auto mapping:{std::pair{StateSlot::Current,rotation.current_from},
            std::pair{StateSlot::Next,rotation.next_from},
            std::pair{StateSlot::Scratch,rotation.scratch_from}}) {
        if(mapping.second==StateSlot::Current) {
            if(retained_seed)throw std::logic_error("RKL rotation duplicated its seed");
            retained_seed=mapping.first;
        }
    }
    if(!retained_seed||*retained_seed==StateSlot::Current)
        throw std::logic_error("RKL rotation did not retain a distinct input Current");
    topology_registry.validate_committed_snapshot(observe_topology());
    const auto& active=amr_ctrl.tree->GetActiveBlocks();
    if(!residency_ledger||active.empty()||active.size()!=stage_handles.size())
        throw std::logic_error("Diffusion activity lost actual leaf/ledger ownership");
    const auto side=compute_backend?ExecutionSide::Device:ExecutionSide::Host;
    for(const auto handle:stage_handles) {
        for(const auto slot:{StateSlot::Current,*retained_seed}) {
            const state::StateKey key{handle,slot};
            const auto coherence=residency_ledger->inspect(key);
            scheduler::detail::require_settled_destination(coherence);
            residency_ledger->require_readable(key,
                {side,coherence.interior.version,true,slot==StateSlot::Current});
        }
    }
    backend::DiffusionActivityReceipt receipt{};
    if(compute_backend) {
        std::vector<backend::BackendStateAccess> accesses;
        accesses.reserve(stage_handles.size());
        for(std::size_t b=0;b<stage_handles.size();++b)
            accesses.push_back(backend_access(b,StateSlot::Current));
        const auto before=compute_backend->counters();
        receipt=compute_backend->reduce_diffusion_energy_activity_batch(accesses,*retained_seed);
        const auto after=compute_backend->counters();
        diffusion_activity_operations_.bytes_h2d+=after.bytes_h2d-before.bytes_h2d;
        diffusion_activity_operations_.bytes_d2h+=after.bytes_d2h-before.bytes_d2h;
        diffusion_activity_operations_.kernel_count+=after.kernel_count-before.kernel_count;
        diffusion_activity_operations_.stream_sync_count+=after.stream_sync_count-before.stream_sync_count;
    } else {
        const auto seed_member=TimeIntegration::hydro_boundary_state_member(*retained_seed);
        long double signed_change=0.,absolute_change=0.;
        for(const int id:active) {
            const auto& block=amr_ctrl.pool->GetBlock(id);
            const auto& grid=block.grid;
            const auto& after=block.fluid_state.eng;
            const auto& before=(block.*seed_member).eng;
            const auto extent=static_cast<std::size_t>(grid.GetTotalSize());
            if(!block.active||after.size()!=extent||before.size()!=extent)
                throw std::logic_error("Diffusion activity energy plane/leaf mismatch");
            const auto geometry=GridMetrics::make_geometry_view(grid);
            for(int k=grid.Ks();k<grid.Ke();++k)
                for(int j=grid.Js();j<grid.Je();++j)
                    for(int i=grid.Is();i<grid.Ie();++i) {
                        const int cell=grid.GetIndex(i,j,k);
                        const double volume=GridMetrics::CellVolume(geometry,i,j,k);
                        const auto term=DiffFlux::diffusion_energy_activity_term(volume,after[cell],before[cell]);
                        if(!term.valid)
                            throw std::runtime_error("Diffusion activity requires a valid shared endpoint energy term");
                        signed_change+=term.signed_energy_change;
                        absolute_change+=term.absolute_energy_change;
                        if(receipt.cells==std::numeric_limits<std::uint64_t>::max())
                            throw std::overflow_error("Diffusion activity cell count exhausted");
                        ++receipt.cells;
                    }
        }
        const auto maximum=static_cast<long double>(std::numeric_limits<double>::max());
        if(!std::isfinite(signed_change)||!std::isfinite(absolute_change)
            ||std::abs(signed_change)>maximum||absolute_change>maximum)
            throw std::overflow_error("Diffusion activity half receipt is not representable");
        receipt.signed_energy_change=static_cast<double>(signed_change);
        receipt.absolute_energy_change=static_cast<double>(absolute_change);
    }
    if(!std::isfinite(receipt.signed_energy_change)||!std::isfinite(receipt.absolute_energy_change)
        ||receipt.absolute_energy_change<0.||receipt.cells==0)
        throw std::runtime_error("Diffusion activity returned an invalid half receipt");
    diffusion_activity_half_=receipt;
}

DiffusionActivityTotals DriverRuntime::prepare_diffusion_activity_promotion(
    std::span<const std::optional<backend::DiffusionActivityReceipt>> halves) const
{
    auto next=diffusion_activity_totals_;
    if(!diffusion_activity_enabled_)return next;
    if(halves.size()!=2)throw std::logic_error("Diffusion activity requires the original two macro halves");
    if(!halves[0]&&!halves[1])return next; // Disabled diffusion contributes nothing.
    if(!halves[0]||!halves[1])throw std::logic_error("Diffusion activity macro lost a half receipt");
    if(next.accepted_macros==std::numeric_limits<std::uint64_t>::max()
        ||next.accepted_halves>std::numeric_limits<std::uint64_t>::max()-2)
        throw std::overflow_error("Diffusion activity accepted count exhausted");
    for(const auto& half:halves) {
        if(!std::isfinite(half->signed_energy_change)||!std::isfinite(half->absolute_energy_change)
            ||half->absolute_energy_change<0.||half->cells==0
            ||next.cells>std::numeric_limits<std::uint64_t>::max()-half->cells)
            throw std::overflow_error("Diffusion activity promotion has an invalid receipt/count");
        next.signed_energy_change+=half->signed_energy_change;
        next.absolute_energy_change+=half->absolute_energy_change;
        next.cells+=half->cells;
    }
    if(!std::isfinite(next.signed_energy_change)||!std::isfinite(next.absolute_energy_change))
        throw std::overflow_error("Diffusion activity process total exhausted");
    next.accepted_halves+=2;++next.accepted_macros;return next;
}
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
    // Require published interior identity across the whole domain before any
    // EOS evaluation; this reader never configures/fills BC or guesses a time.
    for (std::size_t index = 0; index < active.size(); ++index) {
        residency_ledger->require_readable(
            {stage_handles[index],StateSlot::Current},
            {side,version,true,false});
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
            TimeIntegration::synchronize_domain_boundary(amr_ctrl, bc_handler,
                &amr::Block::fluid_state, proposed.handles_in_observation_order);
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
            storage, &bc_handler.logical_plan(amr_ctrl.pool->GetBlock(active[index]).grid)});
    }
    return bindings;
}

/** Install a validated compute backend and its resident block views. */
void DriverRuntime::install_backend(std::unique_ptr<backend::ComputeBackend> backend)
{
    compute_backend = std::move(backend);
}

/** Upload only accepted Current and bind the original immutable transport plans. */
void DriverRuntime::upload_initial_state()
{
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
