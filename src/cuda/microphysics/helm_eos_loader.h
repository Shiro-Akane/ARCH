#pragma once

#include "physics/eos/HelmEos.h"
#include "physics/eos/IdealGas.h"
#include "physics/eos/Tabular3DEOS.h"
#include "physics/eos/Tabular4DEOS.h"

#include <cuda_runtime_api.h>

#include <array>
#include <vector>

namespace arch::cuda
{

// Lifetime contract for every owner below: upload and every consumer of view()
// use the stream passed to the constructor. Destruction synchronizes that
// stream before freeing; cross-stream consumers require external synchronization.
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

class Tabular3DEOSDeviceOwner
{
public:
    Tabular3DEOSDeviceOwner(const Tabular3DEOSHostView &host, cudaStream_t stream);
    ~Tabular3DEOSDeviceOwner();

    Tabular3DEOSDeviceOwner(const Tabular3DEOSDeviceOwner &) = delete;
    Tabular3DEOSDeviceOwner &operator=(const Tabular3DEOSDeviceOwner &) = delete;
    Tabular3DEOSDeviceOwner(Tabular3DEOSDeviceOwner &&other) noexcept;
    Tabular3DEOSDeviceOwner &operator=(Tabular3DEOSDeviceOwner &&other) noexcept;

    Tabular3DEOSView view() const noexcept { return device_view_; }
    bool empty() const noexcept;

private:
    void release_after_sync() noexcept;

    cudaStream_t stream_ = nullptr;
    DeviceSpeciesOwner species_;
    Tabular3DEOSView device_view_{};
    std::array<double *, 6> device_{};
    std::array<std::vector<double>, 6> staging_{};
};

class Tabular4DEOSDeviceOwner
{
public:
    Tabular4DEOSDeviceOwner(const Tabular4DEOSHostView &host, cudaStream_t stream);
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
    std::array<std::vector<double>, 6> staging_{};
};

} // namespace arch::cuda
