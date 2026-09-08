/** Backend storage for one immutable generated network; no network catalogue.
 * Construction completes the upload before returning, so borrowed Host data
 * need only outlive construction. Device views borrow this owner and its stream.
 */
#pragma once
#include "cuda/common/DeviceAllocation.h"
#include "physics/network/WeakTableView.h"
#include <cmath>
#include <cstdint>

namespace arch::cuda {
class DeviceNetworkOwner {
public:
    DeviceNetworkOwner(network::WeakTableStorageView host, cudaStream_t stream)
        : stream_(stream), source_(host)
    {
        if (!host.data || host.size == 0
            || host.size > std::numeric_limits<std::size_t>::max() / sizeof(double))
            throw std::invalid_argument("Immutable network upload is empty or overflows its extent");
        for (std::size_t i = 0; i < host.size; ++i)
            if (!std::isfinite(host.data[i]))
                throw std::invalid_argument("Immutable network upload contains nonfinite data");
        check_cuda(cudaGetDevice(&device_), "network owner device");
        try {
            values_.allocate(host.size);
            check_cuda(cudaMemcpyAsync(values_.get(), host.data, host.size * sizeof(double),
                cudaMemcpyHostToDevice, stream_), "immutable network upload");
            check_cuda(cudaStreamSynchronize(stream_), "immutable network upload completion");
            ++synchronizations_;
        } catch (...) {
            set_device_and_quiesce_or_terminate(device_, stream_);
            throw;
        }
    }
    ~DeviceNetworkOwner() { set_device_and_quiesce_or_terminate(device_, stream_); }
    DeviceNetworkOwner(const DeviceNetworkOwner&) = delete;
    DeviceNetworkOwner& operator=(const DeviceNetworkOwner&) = delete;
    network::WeakTableStorageView view() const noexcept { return {values_.get(), values_.size()}; }
    std::size_t bytes() const noexcept { return values_.size() * sizeof(double); }
    std::uint64_t synchronization_count() const noexcept { return synchronizations_; }
    bool bound_to(network::WeakTableStorageView host, cudaStream_t stream) const noexcept {
        int current = -1;
        return host.data == source_.data && host.size == source_.size && stream == stream_
            && cudaGetDevice(&current) == cudaSuccess && current == device_;
    }
private:
    int device_ = -1;
    cudaStream_t stream_ = nullptr;
    network::WeakTableStorageView source_; // Identity only; never dereferenced after construction.
    DeviceAllocation<double> values_;
    std::uint64_t synchronizations_ = 0;
};
} // namespace arch::cuda
