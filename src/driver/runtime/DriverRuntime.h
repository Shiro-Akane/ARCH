/** @file DriverRuntime.h
 * @brief Own run topology, residency, scheduler clock and backend storage.
 * AMR, configuration and output counters are borrowed for the duration of the run.
 * Physical algorithms and output serialization remain with their own modules.
 * Workflow:
 * 1. Receive a resolved configuration, stage request and current state identity.
 * 2. Expose the smallest runtime operations required by split Driver owners.
 * 3. Hand completed state and diagnostics to the next scheduled stage.
 */

#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "amr/exchange/CoordinateSeamPlan.h"
#include "amr/transfer/AmrTransferPlans.h"
#include "data/GlobalDefs.h"
#include "driver/runtime/ComputeBackend.h"

#include "driver/runtime/TopologyIdentityRegistry.h"
#include "driver/schedule/StageScheduler.h"
#include "numerics/curvilinear/GridMetrics.h"

#include <algorithm>
#include <type_traits>


struct BCHandler;
struct SimulationController;
struct SimConfig;
struct FluidState;
struct Grid;
struct SpeciesManager;
namespace amr { struct AMRControl; }
namespace Numerics { class IHydroSolver; }
namespace arch::driver {
struct RegridMeasurement {
    int step;
    double time;
    std::size_t old_blocks, new_blocks;
    bool changed;
    double elapsed_seconds;
    backend::BackendCounters operations;
};
/** Accepted diagnostic segment; never a checkpoint/evolved state quantity. */
struct DiffusionActivityTotals {
    long double signed_energy_change=0., absolute_energy_change=0.;
    std::uint64_t cells=0, accepted_halves=0, accepted_macros=0;
    double process_start_time=0.;
    int process_start_step=0;
};
class DriverRuntime {
public:
    DriverRuntime(amr::AMRControl&, BCHandler&, const SimConfig&,
                  const SpeciesManager&, SimulationController&);
    ~DriverRuntime();
    DriverRuntime(const DriverRuntime&) = delete;
    DriverRuntime& operator=(const DriverRuntime&) = delete;

    void initialize_topology();
    enum class RegridPhase { IndicatorsEvaluated, BalancePrepared, BalanceRound, Balanced, Selected, Constructed, Migrated, TopologyActivated, GhostsSynchronized, ReadyToPublish };
    bool perform_regrid(int step, double time, bool jeans_repair_only = false,
        const std::function<void(RegridPhase)>& observer = {});
    /** Collective owner-only transfer at a quiescent accepted boundary.
     * Observers see completed local phases and may reject before publication.
     * This narrow native consumer does not enable application MPI-AMR.
     */
    enum class LeafTransferPhase { Reserved, FieldReceived, TopologyActivated, DevicePrepared, ReadyToPublish };
    void migrate_leaf(amr::AmrEndpoint source, int destination_rank,
        const std::function<void(LeafTransferPhase)>& observer = {});
    /** Atomically colocate a complete qualified sibling family. Call perform_regrid
     * separately to apply the original indicators and restriction. Returns false
     * for an already colocated family, preserving its epoch and accepted state.
     */
    bool coalesce_siblings(amr::LogicalBlockKey parent, int destination_rank,
        const std::function<void(LeafTransferPhase)>& observer = {});
    void ensure_jeans_resolution(int step, double time);
    // Explicit diagnostic/enforcement request; never invoked for disabled JENS.
    std::vector<double> evaluate_current_jeans_resolution();
    // Backend-local ghosts never request Host materialization.
    void ensure_fluid_ghosts(state::StateSlot slot = state::StateSlot::Current);
    void materialize_current_for_host();
    state::CompletionToken execute_device_boundary(state::StateSlot,
        state::StateVersion, state::CompletionToken);
    backend::BackendStateAccess backend_access(std::size_t, state::StateSlot) const;
    void trace_backend_operation(backend::BackendOperation, state::StateSlot,
                                 const backend::BackendCounters&);
    std::vector<backend::BackendTopologyBinding> prepare_backend_bindings();
    void install_backend(std::unique_ptr<backend::ComputeBackend>);
    void upload_initial_state();
    /** Bind numerical-stage flux/reflux resources after storage upload succeeds. */
    void prepare_device_flux_plans();

    scheduler::StageExecutionContext stage_context();
    /** Native distributed Hydro transaction; application preflight remains separate. */
    bool distributed_hydro_topology() const;
    void begin_distributed_hydro_attempt(const Numerics::IHydroSolver*, const SimulationController&);
    void agree_distributed_hydro_interval(double dt) const;
    void commit_distributed_hydro_attempt(double dt);
    void discard_distributed_hydro_attempt() noexcept;
    void synchronize_distributed_clock();

    const std::vector<amr::BlockHandle>& handles() const { return stage_handles; }
    backend::ComputeBackend* backend() const { return compute_backend.get(); }
    amr::AMRControl& control() const { return amr_ctrl; }
    BCHandler& boundaries() const { return bc_handler; }
    const SimConfig& configuration() const { return config; }
    const SpeciesManager& species() const { return specs; }
    state::RepairBudget& repair_budget();
    const std::vector<RegridMeasurement>& regrid_records() const { return regrid_measurements; }
    /** Observe actual surface fluxes only for selected case boundary callbacks. */
    void bind_boundary_accounting(scheduler::StageExecutionContext&);
    const std::vector<double>& hydro_boundary_budget() const { return hydro_boundary_budget_; }
    const std::vector<double>& diffusion_boundary_budget() const { return diffusion_boundary_budget_; }
    const backend::BackendCounters& boundary_observer_operations() const { return boundary_observer_operations_; }
    bool diffusion_activity_enabled() const noexcept { return diffusion_activity_enabled_; }
    const DiffusionActivityTotals& diffusion_activity_totals() const noexcept { return diffusion_activity_totals_; }
    const backend::BackendCounters& diffusion_activity_operations() const noexcept { return diffusion_activity_operations_; }
    /** Observe accepted half-step endpoints only, through actual retained slots. */
    void observe_completed_diffusion_activity(const scheduler::RklPlan&);
    void clear_diffusion_activity_half() noexcept { diffusion_activity_half_.reset(); }
    std::optional<backend::DiffusionActivityReceipt> take_diffusion_activity_half() noexcept {
        auto result=diffusion_activity_half_;diffusion_activity_half_.reset();return result;
    }
    /** Prepare scalar accumulation before final physical commit; no publication. */
    DiffusionActivityTotals prepare_diffusion_activity_promotion(
        std::span<const std::optional<backend::DiffusionActivityReceipt>>) const;
    /** Scalar assignment after the complete macro. */
    void promote_diffusion_activity(const DiffusionActivityTotals& value) noexcept {
        diffusion_activity_totals_=value;
    }
private:
    struct HydroAttempt;
    std::unique_ptr<HydroAttempt> hydro_attempt_;
    bool diffusion_activity_enabled_=false;
    std::optional<backend::DiffusionActivityReceipt> diffusion_activity_half_;
    DiffusionActivityTotals diffusion_activity_totals_{};
    backend::BackendCounters diffusion_activity_operations_{};
    // GravityStage borrows the exact Current/Hydro owners; friendship does not
    // grant scientific capability.
    friend class GravityStage;
    std::vector<topology::TopologyObservation> observe_blocks(std::span<const int>) const;
    std::vector<topology::TopologyObservation> observe_topology() const;
    state::StateVersion current_interior_version() const;
    void complete_device_boundary(state::StateSlot);
    // One original boundary recipe; the caller owns storage/ledger validation.
    state::CompletionToken execute_device_boundary_domain(
        std::span<const backend::BackendStateAccess>,
        std::span<const amr::BlockHandle>, state::StateSlot,
        state::StateVersion, state::CompletionToken, state::StateResidencyLedger&,
        backend::BackendTopologyStoreTransaction* staged = nullptr);
    std::unique_ptr<backend::BackendTopologyStoreTransaction> begin_device_topology_store(
        amr::AmrPlanScope, std::span<const amr::BlockHandle>,
        std::vector<backend::StorageGeneration>&);
    void migrate_leaves(std::span<const amr::AmrEndpoint>, int destination_rank,
        const std::function<void(LeafTransferPhase)>& observer);
    bool execute_regrid(bool jeans_repair_only);
    bool perform_regrid_impl(int step,double time,bool jeans_repair_only,
        const std::function<void(RegridPhase)>& observer = {});
    void balance_distributed_refinement(const std::function<void(RegridPhase)>& observer);
    bool execute_distributed_regrid(int step,double time,bool jeans_repair_only,
        const std::function<void(RegridPhase)>& observer);
    // Original Device indicator arithmetic.
    void evaluate_device_regrid_indicators(bool jeans_repair_only);
    bool device_jeans_parent_resolved(const amr::Block&,std::span<const int>);
    static backend::HostStateTransferView host_transfer_view(FluidState&);

    amr::AMRControl& amr_ctrl;
    BCHandler& bc_handler;
    const SimConfig& config;
    const SpeciesManager& specs;
    SimulationController& ctrl;
    topology::TopologyIdentityRegistry topology_registry;
    scheduler::MonotonicSchedulerClock scheduler_clock;
    std::uint64_t next_amr_transaction_id = 1;
    std::unique_ptr<state::StateResidencyLedger> residency_ledger;
    std::vector<amr::BlockHandle> stage_handles;
    std::unique_ptr<backend::ComputeBackend> compute_backend;
    backend::StorageGenerationIssuer storage_generation_issuer;
    std::vector<backend::StorageGeneration> backend_storage;
    std::vector<backend::BackendStateAccess> boundary_accesses, boundary_level_accesses;
    // Ghost field versions remain in the residency ledger. This extra stamp
    // identifies only the immutable callback time/purpose and topology.
    struct UserBoundaryStamp {
        amr::TopologyEpoch epoch{};
        std::uint64_t revision = 0;
    };
    std::array<UserBoundaryStamp, 3> user_boundary_stamps_{};
    std::vector<RegridMeasurement> regrid_measurements;
    // Surface records are rebuilt after a topology epoch changes. Integrated
    // budgets retain their since-process-start scope across AMR regrids.
    amr::TopologyEpoch boundary_budget_epoch_{};
    std::vector<backend::BoundaryFluxPlanes> boundary_surface_layout_;
    std::vector<double> hydro_boundary_budget_, diffusion_boundary_budget_;
    std::vector<double> boundary_rkl_previous_, boundary_rkl_older_;
    backend::BackendCounters boundary_observer_operations_{};
    void prepare_boundary_capture(double weight, double initial_weight, bool save_initial);
    std::vector<double> integrate_boundary_capture();
};
} // namespace arch::driver
