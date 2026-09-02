#include "cuda/microphysics/tabular3_eos_device_owner.h"

#include "cuda/microphysics/device_eos_owner_utils.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace arch::cuda
{
namespace
{

SpeciesHostView validate_tabular3_upload(const Tabular3DEOSHostView &host)
{
    const std::size_t expected = owner_detail::checked_extent(
        {host.n_rho, host.n_T, host.n_X});
    const double *source[6]{
        host.table_P, host.table_E, host.table_cs, host.table_cv,
        host.table_dP_drho, host.table_dP_dT};
    if (host.uses_free_energy) {
        for (int index = 0; index < 6; ++index)
            owner_detail::validate_absent_upload(
                source[index], host.table_extents[index],
                "Tabular3 direct table");
        for (int field = 0; field < tabular_eos::FieldCount; ++field)
            owner_detail::validate_required_upload(
                host.free_energy_fields[field],
                host.free_energy_extents[field], expected,
                "Tabular3 free-energy derivative field");
    } else {
        for (int index = 0; index < 4; ++index)
            owner_detail::validate_required_upload(
                source[index], host.table_extents[index], expected,
                "Tabular3 required direct table");
        for (int index = 4; index < 6; ++index)
            owner_detail::validate_optional_upload(
                source[index], host.table_extents[index], expected,
                "Tabular3 optional direct table");
        for (int field = 0; field < tabular_eos::FieldCount; ++field)
            owner_detail::validate_absent_upload(
                host.free_energy_fields[field],
                host.free_energy_extents[field],
                "Tabular3 free-energy derivative field");
    }
    owner_detail::validate_species_upload(host.specs);
    return host.specs;
}

} // namespace

Tabular3DEOSDeviceOwner::Tabular3DEOSDeviceOwner(
    const Tabular3DEOSHostView &host, cudaStream_t stream)
    : stream_(stream), species_(validate_tabular3_upload(host), stream)
{
    const std::size_t extent = owner_detail::checked_extent(
        {host.n_rho, host.n_T, host.n_X});
    device_view_.n_rho = host.n_rho;
    device_view_.n_T = host.n_T;
    device_view_.n_X = host.n_X;
    device_view_.log_rho_min = host.log_rho_min;
    device_view_.log_rho_max = host.log_rho_max;
    device_view_.dlog_rho = host.dlog_rho;
    device_view_.log_T_min = host.log_T_min;
    device_view_.log_T_max = host.log_T_max;
    device_view_.dlog_T = host.dlog_T;
    device_view_.X_min = host.X_min;
    device_view_.X_max = host.X_max;
    device_view_.dX = host.dX;
    device_view_.target_species_id = host.target_species_id;
    device_view_.uses_free_energy = host.uses_free_energy;
    const double *source[6]{
        host.table_P, host.table_E, host.table_cs, host.table_cv,
        host.table_dP_drho, host.table_dP_dT};
    try {
        if (host.uses_free_energy) {
            for (int field = 0; field < tabular_eos::FieldCount; ++field) {
                owner_detail::stage_required(
                    staging_free_energy_[field],
                    host.free_energy_fields[field], extent,
                    "Tabular3 free-energy derivative field");
                owner_detail::allocate_and_copy(
                    device_free_energy_[field], staging_free_energy_[field],
                    stream_, "upload Tabular3 free-energy field");
                device_view_.free_energy_fields[field] =
                    device_free_energy_[field];
            }
        } else {
            for (int index = 0; index < 4; ++index)
                owner_detail::stage_required(
                    staging_[index], source[index], extent,
                    "Tabular3 direct table");
            owner_detail::stage_optional(staging_[4], source[4], extent);
            owner_detail::stage_optional(staging_[5], source[5], extent);
            for (int index = 0; index < 6; ++index)
                owner_detail::allocate_and_copy(
                    device_[index], staging_[index], stream_,
                    "upload Tabular3 direct table");
            device_view_.table_P = device_[0];
            device_view_.table_E = device_[1];
            device_view_.table_cs = device_[2];
            device_view_.table_cv = device_[3];
            device_view_.table_dP_drho = device_[4];
            device_view_.table_dP_dT = device_[5];
        }
        device_view_.specs = species_.view();
    } catch (...) {
        release_after_sync();
        throw;
    }
}

Tabular3DEOSDeviceOwner::~Tabular3DEOSDeviceOwner()
{
    release_after_sync();
}

Tabular3DEOSDeviceOwner::Tabular3DEOSDeviceOwner(
    Tabular3DEOSDeviceOwner &&other) noexcept
    : stream_(std::exchange(other.stream_, nullptr)),
      species_(std::move(other.species_)), device_view_(other.device_view_),
      device_(other.device_), device_free_energy_(other.device_free_energy_),
      staging_(std::move(other.staging_)),
      staging_free_energy_(std::move(other.staging_free_energy_))
{
    other.device_view_ = {};
    other.device_.fill(nullptr);
    other.device_free_energy_.fill(nullptr);
}

Tabular3DEOSDeviceOwner &Tabular3DEOSDeviceOwner::operator=(
    Tabular3DEOSDeviceOwner &&other) noexcept
{
    if (this == &other) return *this;
    release_after_sync();
    species_ = std::move(other.species_);
    stream_ = std::exchange(other.stream_, nullptr);
    device_view_ = other.device_view_;
    other.device_view_ = {};
    device_ = other.device_;
    other.device_.fill(nullptr);
    device_free_energy_ = other.device_free_energy_;
    other.device_free_energy_.fill(nullptr);
    staging_ = std::move(other.staging_);
    staging_free_energy_ = std::move(other.staging_free_energy_);
    return *this;
}

void Tabular3DEOSDeviceOwner::release_after_sync() noexcept
{
    bool has_storage = false;
    for (double *pointer : device_)
        has_storage = has_storage || pointer != nullptr;
    for (double *pointer : device_free_energy_)
        has_storage = has_storage || pointer != nullptr;
    if (has_storage) cudaStreamSynchronize(stream_);
    for (double *&pointer : device_) {
        if (pointer != nullptr) cudaFree(pointer);
        pointer = nullptr;
    }
    for (double *&pointer : device_free_energy_) {
        if (pointer != nullptr) cudaFree(pointer);
        pointer = nullptr;
    }
    device_view_ = {};
}

bool Tabular3DEOSDeviceOwner::empty() const noexcept
{
    return std::all_of(device_.begin(), device_.end(),
                       [](const double *pointer) {
                           return pointer == nullptr;
                       })
        && std::all_of(device_free_energy_.begin(),
                       device_free_energy_.end(),
                       [](const double *pointer) {
                           return pointer == nullptr;
                       })
        && species_.empty();
}

} // namespace arch::cuda
