#pragma once

#include "physics/species/Species.h"

#include <cuda_runtime_api.h>

#include <array>
#include <cstddef>
#include <initializer_list>
#include <vector>

namespace arch::cuda::owner_detail
{

void check_cuda(cudaError_t status, const char *operation);
void allocate_and_copy(double *&device, const std::vector<double> &staging,
                       cudaStream_t stream, const char *label);

template <std::size_t N>
void synchronize_and_free_all(cudaStream_t stream,
                              std::array<double *, N> &pointers) noexcept
{
    bool has_storage = false;
    for (double *pointer : pointers)
        has_storage = has_storage || pointer != nullptr;
    if (has_storage) cudaStreamSynchronize(stream);
    for (double *&pointer : pointers) {
        if (pointer != nullptr) cudaFree(pointer);
        pointer = nullptr;
    }
}

std::size_t checked_extent(std::initializer_list<int> dimensions);
void stage_required(std::vector<double> &staging, const double *host,
                    std::size_t extent, const char *label);
void stage_optional(std::vector<double> &staging, const double *host,
                    std::size_t extent);
void validate_species_upload(SpeciesHostView species);
void validate_required_upload(const double *host, std::size_t actual,
                              std::size_t expected, const char *label);
void validate_optional_upload(const double *host, std::size_t actual,
                              std::size_t expected, const char *label);
void validate_absent_upload(const double *host, std::size_t actual,
                            const char *label);

} // namespace arch::cuda::owner_detail
