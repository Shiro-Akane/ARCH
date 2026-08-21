#include "cuda/microphysics/helm_eos_loader.h"

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace arch::cuda
{
namespace
{

void check_cuda(cudaError_t status, const char *operation)
{
    if (status != cudaSuccess)
        throw std::runtime_error(std::string(operation) + ": " + cudaGetErrorString(status));
}

void allocate_and_copy(double *&device, const std::vector<double> &staging,
                       cudaStream_t stream, const char *label)
{
    if (staging.empty()) {
        device = nullptr;
        return;
    }
    check_cuda(cudaMalloc(reinterpret_cast<void **>(&device),
                          staging.size() * sizeof(double)), label);
    check_cuda(cudaMemcpyAsync(device, staging.data(), staging.size() * sizeof(double),
                               cudaMemcpyHostToDevice, stream), label);
}

template <std::size_t N>
void synchronize_and_free_all(cudaStream_t stream,
                              std::array<double *, N> &pointers) noexcept
{
    bool has_storage = false;
    for (double *pointer : pointers) has_storage = has_storage || pointer != nullptr;
    if (has_storage) cudaStreamSynchronize(stream);
    for (double *&pointer : pointers) {
        if (pointer != nullptr) cudaFree(pointer);
        pointer = nullptr;
    }
}

std::size_t checked_extent(std::initializer_list<int> dimensions)
{
    std::size_t extent = 1;
    for (int dimension : dimensions) {
        if (dimension <= 0)
            throw std::invalid_argument("EOS table dimensions must be positive.");
        const std::size_t value = static_cast<std::size_t>(dimension);
        if (extent > std::numeric_limits<std::size_t>::max() / value)
            throw std::overflow_error("EOS table extent overflow.");
        extent *= value;
    }
    return extent;
}

void stage_required(std::vector<double> &staging, const double *host,
                    std::size_t extent, const char *label)
{
    if (host == nullptr) throw std::invalid_argument(std::string(label) + " is null.");
    staging.assign(host, host + extent);
}

void stage_optional(std::vector<double> &staging, const double *host,
                    std::size_t extent)
{
    if (host == nullptr) staging.clear();
    else staging.assign(host, host + extent);
}

const SpeciesManager &require_species(const HelmEos &host)
{
    const SpeciesManager *species = host.get_species_manager();
    if (species == nullptr)
        throw std::invalid_argument("HelmEos CUDA upload requires species metadata.");
    return *species;
}

} // namespace

DeviceSpeciesOwner::DeviceSpeciesOwner(const SpeciesManager &species, cudaStream_t stream)
    : DeviceSpeciesOwner(species.get_host_view(), stream)
{
}

DeviceSpeciesOwner::DeviceSpeciesOwner(SpeciesHostView species, cudaStream_t stream)
    : stream_(stream), count_(species.count)
{
    if (count_ < 0 || (count_ > 0 && species.host_data == nullptr))
        throw std::invalid_argument("Invalid host species view.");
    for (auto &values : staging_) values.reserve(static_cast<std::size_t>(count_));
    for (int index = 0; index < count_; ++index) {
        staging_[0].push_back(species.get_A(index));
        staging_[1].push_back(species.get_Z(index));
        staging_[2].push_back(species.get_gamma_ref(index));
        staging_[3].push_back(species.get_Cv_ref(index));
    }
    try {
        allocate_and_copy(device_[0], staging_[0], stream_, "upload species A");
        allocate_and_copy(device_[1], staging_[1], stream_, "upload species Z");
        allocate_and_copy(device_[2], staging_[2], stream_, "upload species gamma");
        allocate_and_copy(device_[3], staging_[3], stream_, "upload species Cv");
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
      count_(std::exchange(other.count_, 0)),
      device_(other.device_), staging_(std::move(other.staging_))
{
    other.device_.fill(nullptr);
}

DeviceSpeciesOwner &DeviceSpeciesOwner::operator=(DeviceSpeciesOwner &&other) noexcept
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
    synchronize_and_free_all(stream_, device_);
    count_ = 0;
}

SpeciesPODView DeviceSpeciesOwner::view() const noexcept
{
    return {device_[0], device_[1], device_[2], device_[3], count_};
}

IdealGasView DeviceSpeciesOwner::ideal_gas_view(double global_gamma) const noexcept
{
    return {view(), global_gamma};
}

bool DeviceSpeciesOwner::empty() const noexcept
{
    return count_ == 0 && std::all_of(device_.begin(), device_.end(),
                                     [](const double *p) { return p == nullptr; });
}

HelmEosDeviceOwner::HelmEosDeviceOwner(const HelmEos &host, cudaStream_t stream)
    : stream_(stream), species_(require_species(host), stream)
{
    const HelmEosHostView source = host.get_view();
    constexpr std::size_t extent =
        static_cast<std::size_t>(HelmEosView::imax) * HelmEosView::jmax;
    try {
        for (int index = 0; index < 9; ++index) {
            stage_required(staging_f_[index], source.f[index], extent, "Helm f table");
            allocate_and_copy(device_f_[index], staging_f_[index], stream_, "upload Helm f");
            device_view_.f[index] = device_f_[index];
        }
        for (int index = 0; index < 4; ++index) {
            stage_required(staging_ef_[index], source.ef_table[index], extent,
                           "Helm ef table");
            allocate_and_copy(device_ef_[index], staging_ef_[index], stream_,
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
    : stream_(std::exchange(other.stream_, nullptr)), species_(std::move(other.species_)),
      device_view_(other.device_view_), device_f_(other.device_f_),
      device_ef_(other.device_ef_), staging_f_(std::move(other.staging_f_)),
      staging_ef_(std::move(other.staging_ef_))
{
    other.device_view_ = {};
    other.device_f_.fill(nullptr);
    other.device_ef_.fill(nullptr);
}

HelmEosDeviceOwner &HelmEosDeviceOwner::operator=(HelmEosDeviceOwner &&other) noexcept
{
    if (this == &other) return *this;
    release_after_sync();
    species_ = std::move(other.species_);
    stream_ = std::exchange(other.stream_, nullptr);
    device_view_ = other.device_view_;
    other.device_view_ = {};
    device_f_ = other.device_f_; other.device_f_.fill(nullptr);
    device_ef_ = other.device_ef_; other.device_ef_.fill(nullptr);
    staging_f_ = std::move(other.staging_f_);
    staging_ef_ = std::move(other.staging_ef_);
    return *this;
}

void HelmEosDeviceOwner::release_after_sync() noexcept
{
    bool has_storage = false;
    for (double *p : device_f_) has_storage = has_storage || p != nullptr;
    for (double *p : device_ef_) has_storage = has_storage || p != nullptr;
    if (has_storage) cudaStreamSynchronize(stream_);
    for (double *&p : device_f_) { if (p != nullptr) cudaFree(p); p = nullptr; }
    for (double *&p : device_ef_) { if (p != nullptr) cudaFree(p); p = nullptr; }
    device_view_ = {};
}

bool HelmEosDeviceOwner::empty() const noexcept
{
    return device_view_.f[0] == nullptr && device_view_.ef_table[0] == nullptr
        && species_.empty();
}

Tabular3DEOSDeviceOwner::Tabular3DEOSDeviceOwner(
    const Tabular3DEOSHostView &host, cudaStream_t stream)
    : stream_(stream), species_(host.specs, stream)
{
    const std::size_t extent = checked_extent({host.n_rho, host.n_T, host.n_X});
    device_view_.n_rho = host.n_rho; device_view_.n_T = host.n_T;
    device_view_.n_X = host.n_X;
    device_view_.log_rho_min = host.log_rho_min;
    device_view_.log_rho_max = host.log_rho_max;
    device_view_.dlog_rho = host.dlog_rho;
    device_view_.log_T_min = host.log_T_min;
    device_view_.log_T_max = host.log_T_max;
    device_view_.dlog_T = host.dlog_T;
    device_view_.X_min = host.X_min; device_view_.X_max = host.X_max;
    device_view_.dX = host.dX;
    device_view_.target_species_id = host.target_species_id;
    const double *source[6]{host.table_P, host.table_E, host.table_cs, host.table_cv,
                            host.table_dP_drho, host.table_dP_dT};
    try {
        for (int index = 0; index < 4; ++index)
            stage_required(staging_[index], source[index], extent, "Tabular3 table");
        stage_optional(staging_[4], source[4], extent);
        stage_optional(staging_[5], source[5], extent);
        for (int index = 0; index < 6; ++index)
            allocate_and_copy(device_[index], staging_[index], stream_, "upload Tabular3");
        device_view_.table_P = device_[0]; device_view_.table_E = device_[1];
        device_view_.table_cs = device_[2]; device_view_.table_cv = device_[3];
        device_view_.table_dP_drho = device_[4]; device_view_.table_dP_dT = device_[5];
        device_view_.specs = species_.view();
    } catch (...) {
        release_after_sync();
        throw;
    }
}

Tabular3DEOSDeviceOwner::~Tabular3DEOSDeviceOwner() { release_after_sync(); }

Tabular3DEOSDeviceOwner::Tabular3DEOSDeviceOwner(Tabular3DEOSDeviceOwner &&other) noexcept
    : stream_(std::exchange(other.stream_, nullptr)), species_(std::move(other.species_)),
      device_view_(other.device_view_), device_(other.device_),
      staging_(std::move(other.staging_))
{
    other.device_view_ = {};
    other.device_.fill(nullptr);
}

Tabular3DEOSDeviceOwner &Tabular3DEOSDeviceOwner::operator=(
    Tabular3DEOSDeviceOwner &&other) noexcept
{
    if (this == &other) return *this;
    release_after_sync();
    species_ = std::move(other.species_);
    stream_ = std::exchange(other.stream_, nullptr);
    device_view_ = other.device_view_; other.device_view_ = {};
    device_ = other.device_; other.device_.fill(nullptr);
    staging_ = std::move(other.staging_);
    return *this;
}

void Tabular3DEOSDeviceOwner::release_after_sync() noexcept
{
    synchronize_and_free_all(stream_, device_);
    device_view_ = {};
}

bool Tabular3DEOSDeviceOwner::empty() const noexcept
{
    return device_view_.table_P == nullptr && species_.empty();
}

Tabular4DEOSDeviceOwner::Tabular4DEOSDeviceOwner(
    const Tabular4DEOSHostView &host, cudaStream_t stream)
    : stream_(stream), species_(host.specs, stream)
{
    const std::size_t extent = checked_extent({host.n_rho, host.n_T, host.n_A, host.n_Z});
    device_view_.n_rho = host.n_rho; device_view_.n_T = host.n_T;
    device_view_.n_A = host.n_A; device_view_.n_Z = host.n_Z;
    device_view_.log_rho_min = host.log_rho_min;
    device_view_.log_rho_max = host.log_rho_max;
    device_view_.dlog_rho = host.dlog_rho;
    device_view_.log_T_min = host.log_T_min;
    device_view_.log_T_max = host.log_T_max;
    device_view_.dlog_T = host.dlog_T;
    device_view_.A_min = host.A_min; device_view_.A_max = host.A_max;
    device_view_.dA = host.dA;
    device_view_.Z_min = host.Z_min; device_view_.Z_max = host.Z_max;
    device_view_.dZ = host.dZ;
    const double *source[6]{host.table_P, host.table_E, host.table_cs, host.table_cv,
                            host.table_dP_drho, host.table_dP_dT};
    try {
        for (int index = 0; index < 4; ++index)
            stage_required(staging_[index], source[index], extent, "Tabular4 table");
        stage_optional(staging_[4], source[4], extent);
        stage_optional(staging_[5], source[5], extent);
        for (int index = 0; index < 6; ++index)
            allocate_and_copy(device_[index], staging_[index], stream_, "upload Tabular4");
        device_view_.table_P = device_[0]; device_view_.table_E = device_[1];
        device_view_.table_cs = device_[2]; device_view_.table_cv = device_[3];
        device_view_.table_dP_drho = device_[4]; device_view_.table_dP_dT = device_[5];
        device_view_.specs = species_.view();
    } catch (...) {
        release_after_sync();
        throw;
    }
}

Tabular4DEOSDeviceOwner::~Tabular4DEOSDeviceOwner() { release_after_sync(); }

Tabular4DEOSDeviceOwner::Tabular4DEOSDeviceOwner(Tabular4DEOSDeviceOwner &&other) noexcept
    : stream_(std::exchange(other.stream_, nullptr)), species_(std::move(other.species_)),
      device_view_(other.device_view_), device_(other.device_),
      staging_(std::move(other.staging_))
{
    other.device_view_ = {};
    other.device_.fill(nullptr);
}

Tabular4DEOSDeviceOwner &Tabular4DEOSDeviceOwner::operator=(
    Tabular4DEOSDeviceOwner &&other) noexcept
{
    if (this == &other) return *this;
    release_after_sync();
    species_ = std::move(other.species_);
    stream_ = std::exchange(other.stream_, nullptr);
    device_view_ = other.device_view_; other.device_view_ = {};
    device_ = other.device_; other.device_.fill(nullptr);
    staging_ = std::move(other.staging_);
    return *this;
}

void Tabular4DEOSDeviceOwner::release_after_sync() noexcept
{
    synchronize_and_free_all(stream_, device_);
    device_view_ = {};
}

bool Tabular4DEOSDeviceOwner::empty() const noexcept
{
    return device_view_.table_P == nullptr && species_.empty();
}

} // namespace arch::cuda
