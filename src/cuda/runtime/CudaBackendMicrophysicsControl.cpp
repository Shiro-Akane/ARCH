#include "CudaBackendInternal.h"
#include "CudaBackendBurn.h"

namespace arch::cuda {
namespace {

constexpr bool same_block_handle(
    amr::BlockHandle left, amr::BlockHandle right) noexcept
{
    return left.uid.value == right.uid.value
        && left.epoch.value == right.epoch.value;
}

constexpr bool same_storage_generation(
    backend::StorageGeneration left,
    backend::StorageGeneration right) noexcept
{
    return left.value == right.value;
}

bool same_rkl_descriptor(
    const scheduler::RklStageDescriptor& left,
    const scheduler::RklStageDescriptor& right) noexcept
{
    return left.stage == right.stage
        && left.state_n_slot == right.state_n_slot
        && left.previous_slot == right.previous_slot
        && left.older_slot == right.older_slot
        && left.output_slot == right.output_slot
        && left.reflux_before_publish == right.reflux_before_publish
        && left.refresh_ghost_after == right.refresh_ghost_after;
}

} // namespace

double CudaBackend::compute_diffusion_dt(
    backend::BackendStateAccess current)
{
    auto& block = impl_->require_block(current);
    const DeviceStateView selected = block.require_access(current);
    if (current.slot != state::StateSlot::Current)
        throw std::invalid_argument("diffusion dt requires Current");
    const CudaBackendDiffusionWorkspace workspace{
        block.face_flux.view(), block.diffusion_dt_candidates.get(),
        block.diffusion_dt_result.get(), block.diffusion_status.get()};
    CudaBackendLaunchResult result{};
    visit_eos(impl_->eos, [&](const auto& eos) {
        result = launch_cuda_backend_diffusion_dt(
            selected, eos, impl_->species_view, block.grid,
            impl_->launch.diffusion, workspace, impl_->stream.get());
    });
    check_cuda(result.error, "launch diffusion dt");
    int status = 0;
    double dt = 0.0;
    check_cuda(cudaMemcpyAsync(
                   &status, block.diffusion_status.get(), sizeof(int),
                   cudaMemcpyDeviceToHost, impl_->stream.get()),
               "download diffusion status");
    check_cuda(cudaMemcpyAsync(
                   &dt, block.diffusion_dt_result.get(), sizeof(double),
                   cudaMemcpyDeviceToHost, impl_->stream.get()),
               "download diffusion dt");
    quiesce();
    impl_->runtime_counters.kernel_count += result.kernels_launched;
    impl_->runtime_counters.bytes_d2h += sizeof(int) + sizeof(double);
    if (status != 0) throw std::runtime_error("diffusion dt candidate failed");
    return dt;
}

void CudaBackend::copy_state_slot(
    backend::BackendStateAccess source,
    backend::BackendStateAccess destination)
{
    if (!same_block_handle(source.block, destination.block)
        || !same_storage_generation(source.storage, destination.storage))
        throw std::invalid_argument("state copy crosses CUDA blocks");
    auto& block = impl_->require_block(source);
    const DeviceStateView from = block.require_access(source);
    const DeviceStateView to = block.require_access(destination);
    if (source.slot == destination.slot)
        throw std::invalid_argument("state copy aliases one logical slot");
    check_cuda(copy_cuda_backend_state_slot(from, to, impl_->stream.get()),
               "copy state slot");
    quiesce();
}

state::CompletionToken CudaBackend::execute_diffusion_stage(
    backend::BackendStateAccess current, const scheduler::RklPlan& plan,
    const scheduler::RklStageDescriptor& descriptor,
    double dt, double dt_fe, state::CompletionToken expected)
{
    auto& block = impl_->require_block(current);
    static_cast<void>(block.require_access(current));
    const bool frozen_rkl1 = impl_->launch.plan.diffusion_integrator
        == dispatch::DiffusionIntegratorId::Rkl1;
    const bool frozen_rkl2 = impl_->launch.plan.diffusion_integrator
        == dispatch::DiffusionIntegratorId::Rkl2;
    if (current.slot != state::StateSlot::Current || !complete_token(expected)
        || !impl_->launch.diffusion.use_diffusion
        || (!frozen_rkl1 && !frozen_rkl2)
        || plan.second_order != frozen_rkl2
        || descriptor.stage <= 0
        || descriptor.stage > static_cast<int>(plan.stages.size())
        || !same_rkl_descriptor(
            descriptor, plan.stages[descriptor.stage - 1])
        || !(dt > 0.0) || !(dt_fe > 0.0))
        throw std::invalid_argument("invalid diffusion stage contract");
    const DeviceStateView state_n =
        block.slots[slot_index(descriptor.state_n_slot)];
    const DeviceStateView previous =
        block.slots[slot_index(descriptor.previous_slot)];
    const DeviceStateView older =
        block.slots[slot_index(descriptor.older_slot)];
    const DeviceStateView output =
        block.slots[slot_index(descriptor.output_slot)];
    const CudaBackendDiffusionWorkspace workspace{
        block.face_flux.view(), block.diffusion_dt_candidates.get(),
        block.diffusion_dt_result.get(), block.diffusion_status.get()};
    const auto amr_routes = make_cuda_amr_route_views(
        impl_->active_amr_flux.get(), current.block);
    CudaBackendLaunchResult operation{};
    visit_eos(impl_->eos, [&](const auto& eos) {
        operation = launch_cuda_backend_diffusion_stage(
            plan, descriptor, state_n, previous, older, output,
            block.diffusion_delta.view(),
            block.diffusion_initial_delta.view(), eos, impl_->species_view,
            block.grid, impl_->launch.diffusion, workspace,
            amr_routes.data(), dt,
            impl_->stream.get());
    });
    check_cuda(operation.error, "launch diffusion operator");
    int status = 0;
    check_cuda(cudaMemcpyAsync(
                   &status, block.diffusion_status.get(), sizeof(int),
                   cudaMemcpyDeviceToHost, impl_->stream.get()),
               "download diffusion stage status");
    quiesce();
    impl_->runtime_counters.kernel_count +=
        operation.kernels_launched;
    impl_->runtime_counters.bytes_d2h += sizeof(int);
    if (status != 0) throw std::runtime_error("diffusion stage failed");
    return expected;
}

backend::BurnExecutionResult CudaBackend::execute_burn(
    backend::BackendStateAccess current, double dt,
    state::CompletionToken expected)
{
    auto& block = impl_->require_block(current);
    static_cast<void>(block.require_access(current));
    if (current.slot != state::StateSlot::Current || !complete_token(expected)
        || !(dt > 0.0))
        throw std::invalid_argument("invalid burn contract");
    if (!impl_->launch.burn.use_burn) {
        check_cuda(cudaMemsetAsync(
                       block.slots[slot_index(state::StateSlot::Current)].enuc_rate,
                       0,
                       static_cast<std::size_t>(block.grid.total_size)
                           * sizeof(double),
                       impl_->stream.get()),
                   "clear disabled burn diagnostic");
        quiesce();
        return {DriverBurn::INACTIVE_LIMITER_CANDIDATE, 0, 0, expected};
    }
    DeviceStateView selected =
        block.slots[slot_index(state::StateSlot::Current)];
    check_cuda(cudaMemsetAsync(
                   selected.enuc_rate, 0,
                   static_cast<std::size_t>(block.grid.total_size)
                       * sizeof(double),
                   impl_->stream.get()),
               "clear burn diagnostic");
    cudaError_t launch_error = cudaErrorInvalidValue;
    visit_eos(impl_->eos, [&](const auto& eos) {
        launch_error = launch_cuda_burn_route(
            impl_->launch.plan, selected, block.grid,
            block.burn_workspace_storage.get(), block.burn_candidates.get(),
            block.burn_statuses.get(), block.burn_summary.get(), dt, eos,
            impl_->launch.burn, impl_->stream.get());
    });
    check_cuda(launch_error, "launch burn route");
    DeviceBurnSummary summary{};
    check_cuda(cudaMemcpyAsync(
                   &summary, block.burn_summary.get(), sizeof(summary),
                   cudaMemcpyDeviceToHost, impl_->stream.get()),
               "download burn summary");
    quiesce();
    impl_->runtime_counters.kernel_count += 2;
    impl_->runtime_counters.bytes_d2h += sizeof(summary);
    return {summary.limiter, summary.failed_cells, summary.status, expected};
}


} // namespace arch::cuda
