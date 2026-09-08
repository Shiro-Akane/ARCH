#include "cuda/runtime/control/CudaBackendInternal.h"

namespace arch::cuda {
CudaBackend::CudaBackend(std::unique_ptr<Impl> implementation)
    : impl_(std::move(implementation))
{
    if (!impl_) throw std::invalid_argument("CUDA backend implementation is null");
}

CudaBackend::~CudaBackend() = default;

state::ExecutionSide CudaBackend::side() const noexcept
{
    return state::ExecutionSide::Device;
}

amr::BlockHandle CudaBackend::block_handle() const noexcept
{
    return impl_->first_block().handle;
}

backend::StorageGeneration CudaBackend::storage_generation() const noexcept
{
    return impl_->first_block().generation;
}

bool CudaBackend::contains(
    backend::BackendStateAccess access) const noexcept
{
    return impl_->store.contains(access);
}

void CudaBackend::enqueue_materialize_host_current(
    backend::BackendStateAccess current, state::StateRegion region,
    backend::HostStateTransferView host)
{
    auto& block = impl_->require_block(current);
    const DeviceStateView selected = block.require_access(current);
    if (current.slot != state::StateSlot::Current)
        throw std::invalid_argument("materialization requires Current");
    impl_->runtime_counters.bytes_d2h += block.copy_host_device_region(
        selected, host, region, cudaMemcpyDeviceToHost, impl_->stream.get());
}

void CudaBackend::enqueue_upload_slot(
    backend::BackendStateAccess access, state::StateRegion region,
    backend::HostStateTransferView host)
{
    auto& block = impl_->require_block(access);
    const DeviceStateView selected = block.require_access(access);
    impl_->runtime_counters.bytes_h2d += block.copy_host_device_region(
        selected, host, region, cudaMemcpyHostToDevice, impl_->stream.get());
}

void CudaBackend::quiesce()
{
    impl_->checked_quiesce("synchronize CUDA backend");
}

backend::BackendCounters CudaBackend::counters() const noexcept
{
    ++impl_->runtime_counters.getter_count;
    return impl_->runtime_counters;
}

void CudaBackend::append_trace(backend::BackendTraceRecord record)
{
    if (!impl_->store.contains({record.block, record.storage, record.slot}))
        throw std::invalid_argument("trace targets stale CUDA storage");
    impl_->runtime_trace.push_back(record);
}

std::span<const backend::BackendTraceRecord>
CudaBackend::trace_snapshot() const noexcept
{
    ++impl_->runtime_counters.getter_count;
    return impl_->runtime_trace;
}


} // namespace arch::cuda
