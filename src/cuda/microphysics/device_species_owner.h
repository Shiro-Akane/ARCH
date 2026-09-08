/**
 * @file device_species_owner.h
 * @brief Own immutable device species arrays and expose borrowed shared views.
 *
 * The constructor borrows a stream and retains host staging for uploads.
 * Consumers must obey the stream/lifetime contract below; view() does not
 * transfer ownership or introduce a separate mixture model.
 */

#pragma once

#include "physics/eos/IdealGas.h"

#include <cuda_runtime_api.h>

#include <array>
#include <vector>

namespace arch::cuda
{

// Lifetime contract for every immutable device owner: upload and every
// consumer of view() use the constructor stream. Destruction synchronizes
// that stream before freeing; cross-stream consumers require external
// synchronization.
class DeviceSpeciesOwner
{
public:
    DeviceSpeciesOwner(const SpeciesManager &species, cudaStream_t stream);
    DeviceSpeciesOwner(SpeciesHostView species, cudaStream_t stream);
    ~DeviceSpeciesOwner();

    DeviceSpeciesOwner(const DeviceSpeciesOwner &) = delete;
    DeviceSpeciesOwner &operator=(const DeviceSpeciesOwner &) = delete;
    DeviceSpeciesOwner(DeviceSpeciesOwner &&other) noexcept;
    DeviceSpeciesOwner &operator=(DeviceSpeciesOwner &&other) noexcept;

    SpeciesPODView view() const noexcept;
    IdealGasView ideal_gas_view(double global_gamma) const noexcept;
    bool empty() const noexcept;

private:
    void release_after_sync() noexcept;

    cudaStream_t stream_ = nullptr;
    int count_ = 0;
    std::array<double *, 4> device_{};
    std::array<std::vector<double>, 4> staging_{};
};

} // namespace arch::cuda
