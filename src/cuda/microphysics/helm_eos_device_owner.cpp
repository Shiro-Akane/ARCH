#include "cuda/microphysics/helm_eos_device_owner.h"

#include "cuda/microphysics/device_eos_owner_utils.h"

#include <cstddef>
#include <stdexcept>
#include <utility>

namespace arch::cuda
{
namespace
{

SpeciesHostView validate_helm_upload(const HelmEosHostView &host)
{
    constexpr std::size_t expected =
        static_cast<std::size_t>(HelmEosView::imax) * HelmEosView::jmax;
    for (int index = 0; index < 9; ++index)
        owner_detail::validate_required_upload(
            host.f[index], host.f_extents[index], expected, "Helm f table");
    for (int index = 0; index < 4; ++index)
        owner_detail::validate_required_upload(
            host.ef_table[index], host.ef_extents[index], expected,
            "Helm ef table");
    if (host.specs.host_owner == nullptr)
        throw std::invalid_argument(
            "Helm upload requires species metadata ownership.");
    owner_detail::validate_species_upload(host.specs);
    return host.specs;
}

} // namespace

HelmEosDeviceOwner::HelmEosDeviceOwner(const HelmEos &host,
                                       cudaStream_t stream)
    : HelmEosDeviceOwner(host.get_view(), stream)
{
}

HelmEosDeviceOwner::HelmEosDeviceOwner(const HelmEosHostView &source,
                                       cudaStream_t stream)
    : stream_(stream), species_(validate_helm_upload(source), stream)
{
    constexpr std::size_t extent =
        static_cast<std::size_t>(HelmEosView::imax) * HelmEosView::jmax;
    try {
        for (int index = 0; index < 9; ++index) {
            owner_detail::stage_required(
                staging_f_[index], source.f[index], extent, "Helm f table");
            owner_detail::allocate_and_copy(
                device_f_[index], staging_f_[index], stream_,
                "upload Helm f");
            device_view_.f[index] = device_f_[index];
        }
        for (int index = 0; index < 4; ++index) {
            owner_detail::stage_required(
                staging_ef_[index], source.ef_table[index], extent,
                "Helm ef table");
            owner_detail::allocate_and_copy(
                device_ef_[index], staging_ef_[index], stream_,
                "upload Helm ef");
            device_view_.ef_table[index] = device_ef_[index];
        }
        device_view_.specs = species_.view();
    } catch (...) {
        release_after_sync();
        throw;
    }
}

HelmEosDeviceOwner::~HelmEosDeviceOwner()
{
    release_after_sync();
}

HelmEosDeviceOwner::HelmEosDeviceOwner(HelmEosDeviceOwner &&other) noexcept
    : stream_(std::exchange(other.stream_, nullptr)),
      species_(std::move(other.species_)), device_view_(other.device_view_),
      device_f_(other.device_f_), device_ef_(other.device_ef_),
      staging_f_(std::move(other.staging_f_)),
      staging_ef_(std::move(other.staging_ef_))
{
    other.device_view_ = {};
    other.device_f_.fill(nullptr);
    other.device_ef_.fill(nullptr);
}

HelmEosDeviceOwner &HelmEosDeviceOwner::operator=(
    HelmEosDeviceOwner &&other) noexcept
{
    if (this == &other) return *this;
    release_after_sync();
    species_ = std::move(other.species_);
    stream_ = std::exchange(other.stream_, nullptr);
    device_view_ = other.device_view_;
    other.device_view_ = {};
    device_f_ = other.device_f_;
    other.device_f_.fill(nullptr);
    device_ef_ = other.device_ef_;
    other.device_ef_.fill(nullptr);
    staging_f_ = std::move(other.staging_f_);
    staging_ef_ = std::move(other.staging_ef_);
    return *this;
}

void HelmEosDeviceOwner::release_after_sync() noexcept
{
    bool has_storage = false;
    for (double *pointer : device_f_)
        has_storage = has_storage || pointer != nullptr;
    for (double *pointer : device_ef_)
        has_storage = has_storage || pointer != nullptr;
    if (has_storage) cudaStreamSynchronize(stream_);
    for (double *&pointer : device_f_) {
        if (pointer != nullptr) cudaFree(pointer);
        pointer = nullptr;
    }
    for (double *&pointer : device_ef_) {
        if (pointer != nullptr) cudaFree(pointer);
        pointer = nullptr;
    }
    device_view_ = {};
}

bool HelmEosDeviceOwner::empty() const noexcept
{
    return device_view_.f[0] == nullptr
        && device_view_.ef_table[0] == nullptr && species_.empty();
}

} // namespace arch::cuda
