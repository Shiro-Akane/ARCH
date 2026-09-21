/**
 * @file tabular4_eos_device_owner.cpp
 * @brief Stage and upload Tabular4D tables without changing EOS mathematics.
 *
 * Validate the host table layout, retain copy sources, bind device pointers into
 * the shared view and synchronize before releasing storage. Construction queues
 * uploads on the borrowed stream; runtime control establishes their completion
 * before publishing the owner to numerical consumers.
 */

#include "cuda/microphysics/eos/owners/tabular4_eos_device_owner.h"

#include "cuda/microphysics/eos/device_eos_owner_utils.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace arch::cuda
{
namespace
{

SpeciesHostView validate_tabular4_upload(const Tabular4DEOSHostView &host)
{
    const std::size_t expected = owner_detail::checked_extent(
        {host.n_rho, host.n_T, host.n_A, host.n_Z});
    for (int field = 0; field < tabular_eos::FieldCount; ++field)
        owner_detail::validate_required_upload(host.free_energy_fields[field],
            host.free_energy_extents[field], expected, "Tabular4 free-energy derivative field");
    owner_detail::validate_required_upload(host.table_valid,host.valid_extent,
        expected,"Tabular4 derivative valid-node mask");
    owner_detail::validate_species_upload(host.specs);
    return host.specs;
}

} // namespace

Tabular4DEOSDeviceOwner::Tabular4DEOSDeviceOwner(
    const Tabular4DEOSHostView &host, cudaStream_t stream)
    : stream_(stream), species_(validate_tabular4_upload(host), stream)
{
    const std::size_t extent = owner_detail::checked_extent(
        {host.n_rho, host.n_T, host.n_A, host.n_Z});
    device_view_.n_rho = host.n_rho;
    device_view_.n_T = host.n_T;
    device_view_.n_A = host.n_A;
    device_view_.n_Z = host.n_Z;
    device_view_.log_rho_min = host.log_rho_min;
    device_view_.log_rho_max = host.log_rho_max;
    device_view_.dlog_rho = host.dlog_rho;
    device_view_.log_T_min = host.log_T_min;
    device_view_.log_T_max = host.log_T_max;
    device_view_.dlog_T = host.dlog_T;
    device_view_.A_min = host.A_min;
    device_view_.A_max = host.A_max;
    device_view_.dA = host.dA;
    device_view_.Z_min = host.Z_min;
    device_view_.Z_max = host.Z_max;
    device_view_.dZ = host.dZ;
    device_view_.energy_reference_shift = host.energy_reference_shift;
    try {
            for (int field = 0; field < tabular_eos::FieldCount; ++field) {
                owner_detail::stage_required(
                    staging_free_energy_[field],
                    host.free_energy_fields[field], extent,
                    "Tabular4 free-energy derivative field");
                owner_detail::allocate_and_copy(
                    device_free_energy_[field], staging_free_energy_[field],
                    stream_, "upload Tabular4 free-energy field");
                device_view_.free_energy_fields[field] =
                    device_free_energy_[field];
            }
        {
            owner_detail::stage_required(staging_valid_,host.table_valid,extent,
                "Tabular4 derivative valid-node mask");
            owner_detail::allocate_and_copy(device_valid_,staging_valid_,stream_,
                "upload Tabular4 derivative validity");
            device_view_.table_valid=device_valid_;
        }
        device_view_.specs = species_.view();
    } catch (...) {
        release_after_sync();
        throw;
    }
}

Tabular4DEOSDeviceOwner::~Tabular4DEOSDeviceOwner()
{
    release_after_sync();
}

Tabular4DEOSDeviceOwner::Tabular4DEOSDeviceOwner(
    Tabular4DEOSDeviceOwner &&other) noexcept
    : stream_(std::exchange(other.stream_, nullptr)),
      species_(std::move(other.species_)), device_view_(other.device_view_),
      device_free_energy_(other.device_free_energy_),
      staging_free_energy_(std::move(other.staging_free_energy_)),
      device_valid_(std::exchange(other.device_valid_,nullptr)),
      staging_valid_(std::move(other.staging_valid_))
{
    other.device_view_ = {};
    other.device_free_energy_.fill(nullptr);
}

Tabular4DEOSDeviceOwner &Tabular4DEOSDeviceOwner::operator=(
    Tabular4DEOSDeviceOwner &&other) noexcept
{
    if (this == &other) return *this;
    release_after_sync();
    species_ = std::move(other.species_);
    stream_ = std::exchange(other.stream_, nullptr);
    device_view_ = other.device_view_;
    other.device_view_ = {};
    device_free_energy_ = other.device_free_energy_;
    other.device_free_energy_.fill(nullptr);
    staging_free_energy_ = std::move(other.staging_free_energy_);
    device_valid_=std::exchange(other.device_valid_,nullptr);
    staging_valid_=std::move(other.staging_valid_);
    return *this;
}

void Tabular4DEOSDeviceOwner::release_after_sync() noexcept
{
    bool has_storage = device_valid_!=nullptr;
    for (double *pointer : device_free_energy_)
        has_storage = has_storage || pointer != nullptr;
    if (has_storage) cudaStreamSynchronize(stream_);
    if (device_valid_) cudaFree(device_valid_);
    device_valid_=nullptr;
    for (double *&pointer : device_free_energy_) {
        if (pointer != nullptr) cudaFree(pointer);
        pointer = nullptr;
    }
    device_view_ = {};
}

bool Tabular4DEOSDeviceOwner::empty() const noexcept
{
    return std::all_of(device_free_energy_.begin(),
                       device_free_energy_.end(),
                       [](const double *pointer) {
                           return pointer == nullptr;
                       })
        && device_valid_==nullptr && species_.empty();
}

} // namespace arch::cuda
