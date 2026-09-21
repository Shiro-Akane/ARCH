/** @file DriverRuntime.h
 * @brief Own run topology, residency, scheduler clock and backend storage.
 * AMR, configuration and output counters are borrowed for the duration of the run.
 * Physical algorithms and output serialization remain with their own modules.
 */
#pragma once
#include "driver/runtime/ComputeBackend.h"
#include "driver/schedule/StageScheduler.h"
#include "driver/runtime/TopologyIdentityRegistry.h"
#include <memory>
#include <span>
#include <vector>
class BCHandler;
struct SimulationController;
struct SimConfig;
struct FluidState;
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
class DriverRuntime {
public:
    DriverRuntime(amr::AMRControl&, BCHandler&, const SimConfig&,
                  const SpeciesManager&, SimulationController&);
    ~DriverRuntime();
    DriverRuntime(const DriverRuntime&) = delete;
    DriverRuntime& operator=(const DriverRuntime&) = delete;

    void initialize_topology();
    bool perform_regrid(int step, double time);
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

    scheduler::StageExecutionContext stage_context() {
        return {compute_backend ? state::ExecutionSide::Device : state::ExecutionSide::Host,
                *residency_ledger, scheduler_clock};
    }
    const std::vector<amr::BlockHandle>& handles() const { return stage_handles; }
    backend::ComputeBackend* backend() const { return compute_backend.get(); }
    amr::AMRControl& control() const { return amr_ctrl; }
    BCHandler& boundaries() const { return bc_handler; }
    const SimConfig& configuration() const { return config; }
    const SpeciesManager& species() const { return specs; }
    const std::vector<RegridMeasurement>& regrid_records() const { return regrid_measurements; }
private:
    std::vector<topology::TopologyObservation> observe_blocks(std::span<const int>) const;
    std::vector<topology::TopologyObservation> observe_topology() const;
    state::StateVersion current_interior_version() const;
    void publish_current_ghost();
    void complete_device_boundary(state::StateSlot);
    bool execute_regrid();
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
    std::vector<RegridMeasurement> regrid_measurements;
};
} // namespace arch::driver
