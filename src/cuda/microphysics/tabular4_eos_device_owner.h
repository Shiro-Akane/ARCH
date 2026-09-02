#pragma once

#include "cuda/microphysics/device_species_owner.h"
#include "physics/eos/Tabular4DEOS.h"

#include <cuda_runtime_api.h>

#include <array>
#include <vector>

namespace arch::cuda
{

class Tabular4DEOSDeviceOwner
{
public:
    Tabular4DEOSDeviceOwner(const Tabular4DEOSHostView &host,
                            cudaStream_t stream);
    ~Tabular4DEOSDeviceOwner();

    Tabular4DEOSDeviceOwner(const Tabular4DEOSDeviceOwner &) = delete;
    Tabular4DEOSDeviceOwner &operator=(const Tabular4DEOSDeviceOwner &) = delete;
    Tabular4DEOSDeviceOwner(Tabular4DEOSDeviceOwner &&other) noexcept;
    Tabular4DEOSDeviceOwner &operator=(Tabular4DEOSDeviceOwner &&other) noexcept;

    Tabular4DEOSView view() const noexcept { return device_view_; }
    bool empty() const noexcept;

private:
    void release_after_sync() noexcept;

    cudaStream_t stream_ = nullptr;
    DeviceSpeciesOwner species_;
    Tabular4DEOSView device_view_{};
    std::array<double *, 6> device_{};
    std::array<double *, tabular_eos::FieldCount> device_free_energy_{};
    std::array<std::vector<double>, 6> staging_{};
    std::array<std::vector<double>, tabular_eos::FieldCount>
        staging_free_energy_{};
};

} // namespace arch::cuda
