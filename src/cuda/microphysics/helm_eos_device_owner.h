#pragma once

#include "cuda/microphysics/device_species_owner.h"
#include "physics/eos/HelmEos.h"

#include <cuda_runtime_api.h>

#include <array>
#include <vector>

namespace arch::cuda
{

class HelmEosDeviceOwner
{
public:
    HelmEosDeviceOwner(const HelmEos &host, cudaStream_t stream);
    HelmEosDeviceOwner(const HelmEosHostView &host, cudaStream_t stream);
    ~HelmEosDeviceOwner();

    HelmEosDeviceOwner(const HelmEosDeviceOwner &) = delete;
    HelmEosDeviceOwner &operator=(const HelmEosDeviceOwner &) = delete;
    HelmEosDeviceOwner(HelmEosDeviceOwner &&other) noexcept;
    HelmEosDeviceOwner &operator=(HelmEosDeviceOwner &&other) noexcept;

    HelmEosView view() const noexcept { return device_view_; }
    bool empty() const noexcept;

private:
    void release_after_sync() noexcept;

    cudaStream_t stream_ = nullptr;
    DeviceSpeciesOwner species_;
    HelmEosView device_view_{};
    std::array<double *, 9> device_f_{};
    std::array<double *, 4> device_ef_{};
    std::array<std::vector<double>, 9> staging_f_{};
    std::array<std::vector<double>, 4> staging_ef_{};
};

} // namespace arch::cuda
