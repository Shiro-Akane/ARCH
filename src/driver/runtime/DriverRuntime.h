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
#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

#include "driver/runtime/ComputeBackend.h"
#include "driver/runtime/TopologyIdentityRegistry.h"
#include "driver/schedule/StageScheduler.h"
#include "grid/GridMetrics.h"
#include "amr/exchange/CoordinateSeamPlan.h"
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
        native_rz_eos_acceptance_ = [&eos, species, bounds](
            const FluidState& state, const Grid& grid) {
            RzThermodynamics::validate_patch_eos(state, grid, species, bounds, eos);
        };
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
    const std::vector<RegridMeasurement>& regrid_records() const { return regrid_measurements; }
    /** Observe actual surface fluxes only for selected case boundary callbacks. */
    void bind_boundary_accounting(scheduler::StageExecutionContext&);
    const std::vector<double>& hydro_boundary_budget() const { return hydro_boundary_budget_; }
    const std::vector<double>& diffusion_boundary_budget() const { return diffusion_boundary_budget_; }
    const backend::BackendCounters& boundary_observer_operations() const { return boundary_observer_operations_; }
private:
    friend class HostHydroTransaction;
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
    // A borrowed real EOS, bound explicitly before any native candidate BC work.
    std::function<void(const FluidState&, const Grid&)> native_rz_eos_acceptance_;
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
