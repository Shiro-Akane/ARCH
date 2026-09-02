#include "cuda/microphysics/device_eos_owner_utils.h"

#include <limits>
#include <stdexcept>
#include <string>

namespace arch::cuda::owner_detail
{

void check_cuda(cudaError_t status, const char *operation)
{
    if (status != cudaSuccess)
        throw std::runtime_error(
            std::string(operation) + ": " + cudaGetErrorString(status));
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
    check_cuda(cudaMemcpyAsync(device, staging.data(),
                               staging.size() * sizeof(double),
                               cudaMemcpyHostToDevice, stream), label);
}

std::size_t checked_extent(std::initializer_list<int> dimensions)
{
    std::size_t extent = 1;
    for (int dimension : dimensions) {
        if (dimension <= 0)
            throw std::invalid_argument(
                "EOS table dimensions must be positive.");
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
    if (host == nullptr)
        throw std::invalid_argument(std::string(label) + " is null.");
    staging.assign(host, host + extent);
}

void stage_optional(std::vector<double> &staging, const double *host,
                    std::size_t extent)
{
    if (host == nullptr) staging.clear();
    else staging.assign(host, host + extent);
}

void validate_species_upload(SpeciesHostView species)
{
    if (species.count < 0
        || species.extent != static_cast<std::size_t>(species.count))
        throw std::invalid_argument(
            "Species upload extent does not match count.");
    if (species.count == 0) {
        if (species.host_data != nullptr)
            throw std::invalid_argument(
                "Empty species upload has non-null data.");
        if (species.host_owner != nullptr
            && !species.host_owner->species_list.empty())
            throw std::invalid_argument(
                "Empty species upload disagrees with its owner.");
        return;
    }
    if (species.host_data == nullptr || species.host_owner == nullptr)
        throw std::invalid_argument(
            "Species upload must be backed by a host owner.");
    const auto &owned = species.host_owner->species_list;
    if (owned.size() != species.extent || owned.data() != species.host_data)
        throw std::invalid_argument(
            "Species upload data/count disagree with its owner.");
}

void validate_required_upload(const double *host, std::size_t actual,
                              std::size_t expected, const char *label)
{
    if (host == nullptr || actual != expected)
        throw std::invalid_argument(
            std::string(label)
            + " must be non-null with the exact table extent.");
}

void validate_optional_upload(const double *host, std::size_t actual,
                              std::size_t expected, const char *label)
{
    const bool absent = host == nullptr && actual == 0;
    const bool present = host != nullptr && actual == expected;
    if (!absent && !present)
        throw std::invalid_argument(
            std::string(label)
            + " must be (null,0) or (non-null,exact extent).");
}

void validate_absent_upload(const double *host, std::size_t actual,
                            const char *label)
{
    if (host != nullptr || actual != 0)
        throw std::invalid_argument(
            std::string(label)
            + " must be absent for the selected table mode.");
}

} // namespace arch::cuda::owner_detail
