/** Stream-owner-managed CUDA allocation and the backend error boundary.
 * This is the existing runtime allocation owner, shared with typed sparse
 * pools. It must not import a block store, EOS catalogue or kernel templates.
 * The enclosing owner still establishes the device and completion witness
 * before destruction; this class does not introduce an implicit stream fence.
 */
#pragma once

#include <cuda_runtime.h>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace arch::cuda {

[[noreturn]] inline void throw_cuda(cudaError_t error, const char* operation)
{
    throw std::runtime_error(std::string(operation) + ": " + cudaGetErrorString(error));
}
inline void check_cuda(cudaError_t error, const char* operation)
{
    if (error != cudaSuccess) throw_cuda(error, operation);
}
inline void require_cuda_success_or_terminate(cudaError_t error) noexcept
{
    if (error != cudaSuccess) std::terminate();
}
inline void set_device_and_quiesce_or_terminate(int device_ordinal, cudaStream_t stream) noexcept
{
    require_cuda_success_or_terminate(cudaSetDevice(device_ordinal));
    // nullptr denotes CUDA's valid default stream, not absence of a consumer.
    require_cuda_success_or_terminate(cudaStreamSynchronize(stream));
}

template <class T>
class DeviceAllocation {
public:
    DeviceAllocation() = default;
    ~DeviceAllocation() { release(); }
    DeviceAllocation(const DeviceAllocation&) = delete;
    DeviceAllocation& operator=(const DeviceAllocation&) = delete;
    DeviceAllocation(DeviceAllocation&& other) noexcept
        : pointer_(std::exchange(other.pointer_, nullptr)),
          count_(std::exchange(other.count_, 0))
    {
    }
    DeviceAllocation& operator=(DeviceAllocation&&) = delete;

    void allocate(std::size_t count)
    {
        if (pointer_ != nullptr || count == 0
            || count > std::numeric_limits<std::size_t>::max() / sizeof(T))
            throw std::invalid_argument("invalid CUDA allocation extent");
        check_cuda(cudaMalloc(reinterpret_cast<void**>(&pointer_),
                              count * sizeof(T)),
                   "cudaMalloc");
        count_ = count;
    }

    T* get() const noexcept { return pointer_; }
    std::size_t size() const noexcept { return count_; }

private:
    void release() noexcept
    {
        // The enclosing owner establishes the device and a completion witness.
        if (pointer_ != nullptr)
            require_cuda_success_or_terminate(cudaFree(pointer_));
        pointer_ = nullptr;
        count_ = 0;
    }

    T* pointer_ = nullptr;
    std::size_t count_ = 0;
};

} // namespace arch::cuda
