/**
 * @file cuda_aprox13_batch_api_validation.cu
 * @brief SoA/stream/status test for the validation-only aprox13 batch ABI.
 *
 * The mathematical BE/Newton/LU parity remains covered by
 * cuda_aprox13_be_validation.cu.  This test sends the same bounded state family
 * through the reusable batch launcher and checks the device-buffer contract.
 */

#include <cuda_runtime.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "cuda/Aprox13FixedCvValidationBatch.h"

namespace {

constexpr int kNumSpec = 13;
constexpr int kCells = 8;
constexpr int kStride = kCells + 3;

bool cuda_ok(cudaError_t error, const char* operation)
{
    if (error == cudaSuccess) return true;
    std::fprintf(stderr, "%s: %s\n", operation, cudaGetErrorString(error));
    return false;
}

template <typename T>
bool alloc_copy(const std::vector<T>& host, T*& device)
{
    return cuda_ok(cudaMalloc(&device, host.size() * sizeof(T)), "cudaMalloc")
        && cuda_ok(cudaMemcpy(device, host.data(), host.size() * sizeof(T),
                              cudaMemcpyHostToDevice), "cudaMemcpy H2D");
}

} // namespace

int main()
{
    const std::array<double, kCells> temperatures{
        8.0e8, 1.0e9, 1.5e9, 2.0e9,
        2.5e9, 3.0e9, 5.0e9, 2.0e9};
    const std::array<double, kCells> densities{
        1.0e6, 1.0e7, 1.0e7, 1.0e8,
        1.0e8, 1.0e8, 1.0e8, 1.0e8};
    const std::array<double, kCells> durations{
        1.0e-8, 1.0e-9, 1.0e-10, 1.0e-12,
        1.0e-13, 1.0e-14, 1.0e-16, 1.0e-12};
    const std::array<std::int32_t, kCells> substeps{
        1, 1, 4, 4, 8, 16, 32, 4};

    std::vector<double> rho(densities.begin(), densities.end());
    std::vector<double> temperature(temperatures.begin(), temperatures.end());
    std::vector<double> cv(kCells);
    std::vector<double> dt(durations.begin(), durations.end());
    std::vector<std::int32_t> fixed_substeps(substeps.begin(), substeps.end());
    std::vector<double> species(kNumSpec * kStride, -777.0);
    for (int cell = 0; cell < kCells; ++cell) {
        cv[cell] = 1.0e8 + 0.5e7 * cell;
        double sum = 0.0;
        for (int isotope = 0; isotope < kNumSpec; ++isotope) {
            double value = 1.0e-12;
            if (isotope == 0) value = 0.05 + 0.01 * (cell % 3);
            if (isotope == 1) value = 0.45 - 0.02 * (cell % 2);
            if (isotope == 2) value = 0.45 + 0.01 * (cell % 2);
            if (isotope == 5) value = 0.03;
            if (isotope == 12) value = 0.02;
            species[isotope * kStride + cell] = value;
            sum += value;
        }
        for (int isotope = 0; isotope < kNumSpec; ++isotope) {
            species[isotope * kStride + cell] /= sum;
        }
    }
    // Same explicit invalid-cv case as the bounded correctness test.
    cv[kCells - 1] = 0.0;

    double* d_rho = nullptr;
    double* d_temperature = nullptr;
    double* d_cv = nullptr;
    double* d_dt = nullptr;
    double* d_species = nullptr;
    std::int32_t* d_substeps = nullptr;
    double* d_temperature_out = nullptr;
    double* d_species_out = nullptr;
    std::uint32_t* d_status = nullptr;
    double* d_dt_recommended = nullptr;
    cudaStream_t stream = nullptr;

    std::vector<double> output_species(kNumSpec * kStride, -999.0);
    std::vector<double> output_temperature(kCells, -999.0);
    std::vector<std::uint32_t> status(kCells, 0xffffffffu);
    std::vector<double> dt_recommended(kCells, -999.0);

    bool ok = alloc_copy(rho, d_rho)
           && alloc_copy(temperature, d_temperature)
           && alloc_copy(cv, d_cv)
           && alloc_copy(dt, d_dt)
           && alloc_copy(species, d_species)
           && alloc_copy(fixed_substeps, d_substeps)
           && cuda_ok(cudaMalloc(&d_temperature_out,
                                 kCells * sizeof(double)),
                      "cudaMalloc temperature output")
           && cuda_ok(cudaMalloc(&d_species_out,
                                 output_species.size() * sizeof(double)),
                      "cudaMalloc species output")
           && cuda_ok(cudaMalloc(&d_status,
                                 kCells * sizeof(std::uint32_t)),
                      "cudaMalloc status")
           && cuda_ok(cudaMalloc(&d_dt_recommended,
                                 kCells * sizeof(double)),
                      "cudaMalloc dt recommended")
           && cuda_ok(cudaMemcpy(d_species_out, output_species.data(),
                                 output_species.size() * sizeof(double),
                                 cudaMemcpyHostToDevice),
                      "initialize output padding")
           && cuda_ok(cudaStreamCreateWithFlags(
                          &stream, cudaStreamNonBlocking),
                      "cudaStreamCreateWithFlags");

    if (ok) {
        arch::cuda::BurnBatchInputSoA input{
            d_rho, d_temperature, d_species, d_dt, d_cv,
            kCells, kStride, kNumSpec};
        arch::cuda::BurnBatchOutputSoA output{
            d_temperature_out, d_species_out, d_status, d_dt_recommended,
            kCells, kStride, kNumSpec};
        arch::cuda::Aprox13FixedCvValidationOptions options;
        options.fixed_substeps = d_substeps;

        const auto launch =
            arch::cuda::launch_aprox13_fixed_cv_validation_batch(
                input, output, options,
                reinterpret_cast<arch::cuda::BurnCudaStream>(stream));
        ok = launch.status == arch::cuda::BurnBatchLaunchStatus::Enqueued
          && launch.cuda_runtime_error == 0
          && launch.enqueued_cells == kCells
          && cuda_ok(cudaStreamSynchronize(stream),
                     "cudaStreamSynchronize(batch)")
          && cuda_ok(cudaMemcpy(output_temperature.data(), d_temperature_out,
                                kCells * sizeof(double), cudaMemcpyDeviceToHost),
                     "copy temperature output")
          && cuda_ok(cudaMemcpy(output_species.data(), d_species_out,
                                output_species.size() * sizeof(double),
                                cudaMemcpyDeviceToHost),
                     "copy species output")
          && cuda_ok(cudaMemcpy(status.data(), d_status,
                                kCells * sizeof(std::uint32_t),
                                cudaMemcpyDeviceToHost),
                     "copy status output")
          && cuda_ok(cudaMemcpy(dt_recommended.data(), d_dt_recommended,
                                kCells * sizeof(double), cudaMemcpyDeviceToHost),
                     "copy dt recommendation");

        auto invalid_view = input;
        invalid_view.species_count = 12;
        const auto rejected =
            arch::cuda::launch_aprox13_fixed_cv_validation_batch(
                invalid_view, output, options);
        ok = ok
          && rejected.status == arch::cuda::BurnBatchLaunchStatus::InvalidView
          && rejected.enqueued_cells == 0;
    }

    double maximum_mass_error = 0.0;
    bool padding_intact = true;
    bool expected_status = true;
    bool failure_transactional = true;
    bool dt_semantics = true;
    bool finite = true;
    if (ok) {
        for (int cell = 0; cell < kCells; ++cell) {
            const std::uint32_t expected =
                cell == kCells - 1
                    ? arch::cuda::BurnCellInvalidInput
                    : arch::cuda::BurnCellSuccess;
            expected_status = expected_status && status[cell] == expected;
            finite = finite && std::isfinite(output_temperature[cell])
                     && std::isfinite(dt_recommended[cell]);

            double mass_sum = 0.0;
            for (int isotope = 0; isotope < kNumSpec; ++isotope) {
                const double value = output_species[isotope * kStride + cell];
                finite = finite && std::isfinite(value);
                mass_sum += value;
            }
            maximum_mass_error = std::max(
                maximum_mass_error, std::abs(mass_sum - 1.0));

            if (expected == arch::cuda::BurnCellSuccess) {
                const double expected_dt = dt[cell] / fixed_substeps[cell];
                dt_semantics = dt_semantics
                    && std::abs(dt_recommended[cell] - expected_dt)
                        <= 1.0e-15 * expected_dt;
            } else {
                dt_semantics = dt_semantics && dt_recommended[cell] == 0.0;
                failure_transactional = failure_transactional
                    && output_temperature[cell] == temperature[cell];
                for (int isotope = 0; isotope < kNumSpec; ++isotope) {
                    failure_transactional = failure_transactional
                        && output_species[isotope * kStride + cell]
                           == species[isotope * kStride + cell];
                }
            }
        }
        for (int isotope = 0; isotope < kNumSpec; ++isotope) {
            for (int padding = kCells; padding < kStride; ++padding) {
                padding_intact = padding_intact
                    && output_species[isotope * kStride + padding] == -999.0;
            }
        }
    }

    if (stream) cudaStreamDestroy(stream);
    cudaFree(d_rho);
    cudaFree(d_temperature);
    cudaFree(d_cv);
    cudaFree(d_dt);
    cudaFree(d_species);
    cudaFree(d_substeps);
    cudaFree(d_temperature_out);
    cudaFree(d_species_out);
    cudaFree(d_status);
    cudaFree(d_dt_recommended);

    const bool pass = ok && expected_status && failure_transactional
                   && dt_semantics && padding_intact && finite
                   && maximum_mass_error <= 1.0e-12;
    std::printf(
        "aprox13 fixed-cv batch API cells=%d layout=SoA stride=%d "
        "status=%s transactional=%s dt_rec=%s padding=%s finite=%s "
        "mass_error=%.12e status=%s\n",
        kCells, kStride,
        expected_status ? "PASS" : "FAIL",
        failure_transactional ? "PASS" : "FAIL",
        dt_semantics ? "PASS" : "FAIL",
        padding_intact ? "PASS" : "FAIL",
        finite ? "PASS" : "FAIL", maximum_mass_error,
        pass ? "PASS" : "FAIL");
    return pass ? EXIT_SUCCESS : EXIT_FAILURE;
}
