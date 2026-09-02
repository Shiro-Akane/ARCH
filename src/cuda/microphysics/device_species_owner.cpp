#include "cuda/microphysics/device_species_owner.h"

#include "cuda/microphysics/device_eos_owner_utils.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace arch::cuda
{

DeviceSpeciesOwner::DeviceSpeciesOwner(const SpeciesManager &species,
                                       cudaStream_t stream)
    : DeviceSpeciesOwner(species.get_host_view(), stream)
{
}

DeviceSpeciesOwner::DeviceSpeciesOwner(SpeciesHostView species,
                                       cudaStream_t stream)
    : stream_(stream), count_(species.count)
{
    owner_detail::validate_species_upload(species);
    for (auto &values : staging_)
        values.reserve(static_cast<std::size_t>(count_));
    for (int index = 0; index < count_; ++index) {
        staging_[0].push_back(species.get_A(index));
        staging_[1].push_back(species.get_Z(index));
        staging_[2].push_back(species.get_gamma_ref(index));
        staging_[3].push_back(species.get_Cv_ref(index));
    }
    try {
        owner_detail::allocate_and_copy(
            device_[0], staging_[0], stream_, "upload species A");
        owner_detail::allocate_and_copy(
            device_[1], staging_[1], stream_, "upload species Z");
        owner_detail::allocate_and_copy(
            device_[2], staging_[2], stream_, "upload species gamma");
        owner_detail::allocate_and_copy(
            device_[3], staging_[3], stream_, "upload species Cv");
    } catch (...) {
        release_after_sync();
        throw;
    }
}

DeviceSpeciesOwner::~DeviceSpeciesOwner()
{
    release_after_sync();
}

DeviceSpeciesOwner::DeviceSpeciesOwner(DeviceSpeciesOwner &&other) noexcept
    : stream_(std::exchange(other.stream_, nullptr)),
      count_(std::exchange(other.count_, 0)), device_(other.device_),
      staging_(std::move(other.staging_))
{
    other.device_.fill(nullptr);
}

DeviceSpeciesOwner &DeviceSpeciesOwner::operator=(
    DeviceSpeciesOwner &&other) noexcept
{
    if (this == &other) return *this;
    release_after_sync();
    stream_ = std::exchange(other.stream_, nullptr);
    count_ = std::exchange(other.count_, 0);
    device_ = other.device_;
    other.device_.fill(nullptr);
    staging_ = std::move(other.staging_);
    return *this;
}

void DeviceSpeciesOwner::release_after_sync() noexcept
{
    owner_detail::synchronize_and_free_all(stream_, device_);
    count_ = 0;
}

SpeciesPODView DeviceSpeciesOwner::view() const noexcept
{
    return {device_[0], device_[1], device_[2], device_[3], count_};
}

IdealGasView DeviceSpeciesOwner::ideal_gas_view(
    double global_gamma) const noexcept
{
    return {view(), global_gamma};
}

bool DeviceSpeciesOwner::empty() const noexcept
{
    return count_ == 0
        && std::all_of(device_.begin(), device_.end(),
                       [](const double *pointer) {
                           return pointer == nullptr;
                       });
}

} // namespace arch::cuda
