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
#include "grid/GridMetrics.h"
#include "numerics/state/RzNativeClosure.h"

class BCHandler;
struct SimulationController;
struct SimConfig;
struct FluidState;
struct Grid;
struct SpeciesManager;
namespace amr { class AMRControl; }
namespace arch::driver {
struct RegridMeasurement {
    int step;
    double time;
    std::size_t old_blocks, new_blocks;
    bool changed;
    double elapsed_seconds;
    backend::BackendCounters operations;
};
/** Authentic failed whole-patch gate with its frozen Runtime publication owner.
 * This envelope alone is never permission to retry; the live prepared
 * restriction relation and active-interior thermal phase must also match.
 */
class NativeBoundaryAcceptanceError final : public std::runtime_error {
public:
    const int pool_index;
    const amr::BlockHandle handle;
    const state::StateSlot slot;
    const state::StateVersion version;
    const RzThermodynamics::AcceptanceDiagnostic diagnostic;
    NativeBoundaryAcceptanceError(const std::string& message,int pool_id,
        amr::BlockHandle identity,state::StateSlot selected,state::StateVersion value,
        RzThermodynamics::AcceptanceDiagnostic evidence)
        :std::runtime_error(message),pool_index(pool_id),handle(identity),
          slot(selected),version(value),diagnostic(evidence) {}
};
class DriverRuntime;
class NativeMacroRetryAttempt;
/** A private Runtime-qualified refusal, never a bool from user BC or input.
 * Values survive rollback; no candidate array/context reference is retained. */
class NativeThermalStepRejection final : public std::runtime_error {
    friend class DriverRuntime;
    const DriverRuntime* runtime_;
    const NativeBoundaryAcceptanceError evidence_;
    std::uint64_t attempt_;
    double start_,dt_;
    NativeThermalStepRejection(const NativeBoundaryAcceptanceError& error,
        const DriverRuntime* runtime,std::uint64_t attempt,double start,double dt)
        :std::runtime_error(error.what()),runtime_(runtime),evidence_(error),attempt_(attempt),start_(start),dt_(dt) {}
public:
    /** Match immutable attempt provenance after its real transaction unwound. */
    bool belongs_to(const DriverRuntime& runtime,std::uint64_t attempt,double start,double dt) const noexcept {
        return runtime_==&runtime&&attempt_==attempt
            &&std::bit_cast<std::uint64_t>(start_)==std::bit_cast<std::uint64_t>(start)
            &&std::bit_cast<std::uint64_t>(dt_)==std::bit_cast<std::uint64_t>(dt);
    }
    double failed_aligned_dt() const noexcept {return dt_;}
    const NativeBoundaryAcceptanceError& evidence() const noexcept {return evidence_;}
};
enum class NativeCoarseningVetoKind { EffectiveThermal, JeansResolution };
/** Compact per-call evidence; no fluid arrays/raw output or scientific receipt. */
struct NativeCoarseningVetoRecord {
    amr::LogicalBlockKey parent;
    NativeCoarseningVetoKind kind;
    std::optional<RzThermodynamics::AcceptanceDiagnostic> diagnostic;
    amr::AmrPlanScope scope;
    amr::BlockHandle target;
    state::StateVersion version{}; // zero for fail-only prepublication checks
    double jeans_minimum=0.; // meaningful only for JeansResolution
};
/** Accepted diagnostic segment; never a checkpoint/evolved state quantity. */
struct DiffusionActivityTotals {
    long double signed_energy_change=0., absolute_energy_change=0.;
    std::uint64_t cells=0, accepted_halves=0, accepted_macros=0;
    double process_start_time=0.;
    int process_start_step=0;
};
class HostHydroTransaction;
class DriverRuntime {
public:
    DriverRuntime(amr::AMRControl&, BCHandler&, const SimConfig&,
                  const SpeciesManager&, SimulationController&);
    ~DriverRuntime();
    DriverRuntime(const DriverRuntime&) = delete;
    DriverRuntime& operator=(const DriverRuntime&) = delete;

    void initialize_topology();
    /** Borrow the actual run EOS for strict native-RZ post-ghost acceptance.
     * The EOS must outlive this Runtime. Species count and physical bounds are
     * frozen at binding; no constant-pressure substitute or repair is used.
     */
    template<class Eos> void bind_native_rz_eos(const Eos& eos) {
        if (geometry_semantics_ != GridMetrics::GeometrySemantics::AxisymmetricRz)
            return;
        if (host_hydro_transaction_)
            throw std::logic_error("Active Host Hydro owner excludes EOS rebinding");
        const int species = native_rz_species_count();
        const auto bounds = native_rz_eos_bounds();
        if(native_rz_eos_binding_revision_==std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("Native RZ EOS binding revision exhausted");
        // Build both real-EOS borrowers before publication. A failed allocation
        // cannot leave a new acceptance callable paired with an old classifier.
        std::function<void(const FluidState&,const Grid&)> acceptance=[&eos,species,bounds](
            const FluidState& state,const Grid& grid) {
            RzThermodynamics::validate_completed_patch_eos(state,grid,species,bounds,eos);
        };
        std::function<RzThermodynamics::ActiveThermalClassification(const FluidState&,const Grid&,int)>
            classification=[&eos,species,bounds](const FluidState& state,const Grid& grid,int requested) {
                return RzThermodynamics::classify_completed_active_thermal(state,grid,species,bounds,eos,requested);
            };
        native_rz_eos_acceptance_.swap(acceptance);
        native_rz_active_thermal_classification_.swap(classification);
        ++native_rz_eos_binding_revision_;
        native_rz_eos_binding_=NativeRzEosBindingWitness{
            std::addressof(eos),species,bounds,native_rz_eos_binding_revision_};
        // Every successful rebind invalidates any previous attempt witness,
        // even if the same object address, bounds and species are reused.
        native_host_current_boundary_stamp_.reset();
    }
    /** Attach the exact borrowed Host candidate domain and its real BC context.
     * Bind before actual BC/exchange starts; refresh after a BC time/purpose
     * change. This internal gate does not open the public RZ science capability.
     */
    void bind_native_boundary_acceptance(scheduler::StageExecutionContext&,
        std::span<const amr::BlockHandle>);
    bool perform_regrid(int step, double time, bool jeans_repair_only = false);
    // Explicit internal CPU transaction verification; not reachable from
    // SimConfig/API/Driver evolution and never enables Device or production RZ.
    bool regrid_native_rz_candidate(int step,double time,
        const std::function<void()>& after_host_finalization = {});
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

    scheduler::StageExecutionContext stage_context();
    const std::vector<amr::BlockHandle>& handles() const { return stage_handles; }
    backend::ComputeBackend* backend() const { return compute_backend.get(); }
    amr::AMRControl& control() const { return amr_ctrl; }
    BCHandler& boundaries() const { return bc_handler; }
    const SimConfig& configuration() const { return config; }
    const SpeciesManager& species() const { return specs; }
    GridMetrics::GeometrySemantics geometry_semantics() const noexcept { return geometry_semantics_; }
    state::RepairBudget& repair_budget();
    /** Borrow the one internal Host/native-RZ macro owner; never begin a nested scope. */
    HostHydroTransaction* active_host_hydro_transaction() const noexcept {
        return host_hydro_transaction_;
    }
    /** Borrow only the current internal retry attempt, never a new transaction. */
    NativeMacroRetryAttempt* native_macro_retry_attempt() const noexcept {return native_macro_retry_attempt_;}
    const std::vector<RegridMeasurement>& regrid_records() const { return regrid_measurements; }
    /** Exact parent vetoes from the latest internal native call, not EOS PASS. */
    const std::vector<NativeCoarseningVetoRecord>& native_coarsening_veto_records() const noexcept {
        return native_coarsening_veto_records_;
    }
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
    /** Scalar assignment after the complete macro and its real native commit. */
    void promote_diffusion_activity(const DiffusionActivityTotals& value) noexcept {
        diffusion_activity_totals_=value;
    }
private:
    friend class HostHydroTransaction;
    friend class NativeMacroRetryAttempt;
    bool diffusion_activity_enabled_=false;
    std::optional<backend::DiffusionActivityReceipt> diffusion_activity_half_;
    DiffusionActivityTotals diffusion_activity_totals_{};
    backend::BackendCounters diffusion_activity_operations_{};
    NativeMacroRetryAttempt* native_macro_retry_attempt_=nullptr;
    void qualify_native_thermal_rejection(const scheduler::StageExecutionContext&,
        const NativeBoundaryAcceptanceError&);
    // GravityStage borrows the actual EOS-binding witness and exact Current/
    // Hydro owners; friendship does not grant Native scientific capability.
    friend class GravityStage;
    // Non-owning exact token for one explicit internal Host/RZ transaction.
    HostHydroTransaction* host_hydro_transaction_=nullptr;
    std::vector<double>* tentative_hydro_boundary_budget_=nullptr;
    std::vector<double>* tentative_diffusion_boundary_budget_=nullptr;
    std::vector<topology::TopologyObservation> observe_blocks(std::span<const int>) const;
    std::vector<topology::TopologyObservation> observe_topology() const;
    state::StateVersion current_interior_version() const;
    int native_rz_species_count() const;
    state::Bounds native_rz_eos_bounds() const;
    void complete_device_boundary(state::StateSlot);
    bool execute_regrid(bool jeans_repair_only,bool native_rz_candidate=false,
        const std::function<void()>& after_host_finalization = {});
    bool execute_regrid_attempt(bool jeans_repair_only,bool native_rz_candidate,
        const std::function<void()>& after_host_finalization,
        std::span<const amr::LogicalBlockKey> vetoed_coarsenings = {},
        const std::function<void()>& evaluate_native_indicators = {});
    bool perform_regrid_impl(int step,double time,bool jeans_repair_only,
        bool native_rz_candidate,const std::function<void()>& after_host_finalization = {});
    bool device_jeans_parent_resolved(const amr::Block&,std::span<const int>);
    static backend::HostStateTransferView host_transfer_view(FluidState&);

    const GridMetrics::GeometrySemantics geometry_semantics_;
    amr::AMRControl& amr_ctrl;
    BCHandler& bc_handler;
    const SimConfig& config;
    const SpeciesManager& specs;
    SimulationController& ctrl;
    /** Exact actual EOS binding metadata; absence grants no retry eligibility.
     * The opaque pointer is borrowed identity, never an EOS implementation or
     * acceptance callback. A new binding revision invalidates old attempts.
     */
    struct NativeRzEosBindingWitness {
        const void* owner;
        int species;
        state::Bounds bounds;
        std::uint64_t revision;
    };
    std::uint64_t native_rz_eos_binding_revision_=0;
    std::optional<NativeRzEosBindingWitness> native_rz_eos_binding_;
    bool native_rz_eos_binding_matches(const NativeRzEosBindingWitness&) const;
    // A borrowed real EOS, bound explicitly before any native candidate BC work.
    std::function<void(const FluidState&, const Grid&)> native_rz_eos_acceptance_;
    std::function<RzThermodynamics::ActiveThermalClassification(
        const FluidState&,const Grid&,int)> native_rz_active_thermal_classification_;
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
    /** Completed native Host Current Hydro identity, with empty opaque BC
     * storage leases. It owns metadata only; actual EOS validation is always
     * repeated. Defined with the boundary implementation to avoid exposing
     * boundary candidate types through the Runtime interface.
     */
    struct NativeHostCurrentBoundaryStamp;
    std::shared_ptr<NativeHostCurrentBoundaryStamp> native_host_current_boundary_stamp_;
    std::vector<RegridMeasurement> regrid_measurements;
    std::vector<NativeCoarseningVetoRecord> native_coarsening_veto_records_;
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
/** One nonmoving synchronous macro-attempt qualification lease.
 * Workflow: verify accepted Current/EOS -> borrow the real context/handles ->
 * mark each original diffusion half -> Runtime qualifies only live active
 * completed-RKL thermal refusal -> release AFTER complete macro rollback.
 * No U backup, scientific grant, callback registry or configured parameter.
 */
class NativeMacroRetryAttempt final {
    friend class DriverRuntime;
    DriverRuntime& runtime_;
    scheduler::StageExecutionContext& context_;
    std::uint64_t attempt_;
    double start_,dt_,dt_fe_;
    std::optional<scheduler::RklMethod> method_;
    NumericsConfig numerics_;
    DiffusionConfig diffusion_;
    std::optional<DriverRuntime::NativeRzEosBindingWitness> eos_binding_;
    std::span<const amr::BlockHandle> handles_;
    int half_=0,stages_=0;
    bool enabled_=false;
public:
    NativeMacroRetryAttempt(DriverRuntime&,scheduler::StageExecutionContext&,
        std::uint64_t,std::optional<scheduler::RklMethod>,double dt_fe);
    NativeMacroRetryAttempt(const NativeMacroRetryAttempt&)=delete;
    NativeMacroRetryAttempt(NativeMacroRetryAttempt&&)=delete;
    ~NativeMacroRetryAttempt() noexcept;
    /** Name the existing D1/D2 work; stage count is the same selected function. */
    void begin_diffusion_half(int half,double interval);
    /** Successful diffusion ends its qualification window; no receipt is accepted. */
    void end_diffusion_half() noexcept {half_=0;stages_=0;}
};
} // namespace arch::driver
