/**
 * Composition-Jacobian resource comparison for aprox19/aprox21.
 *
 * This intentionally keeps temperature differentiation and LHS assembly out
 * of both kernels so ptxas reports a like-for-like comparison between the old
 * all-column Dual<1> composition path and the generated explicit path.
 */

#include <cuda_runtime.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#if defined(TIMMES_VALIDATE_APROX19)
#include "physics/network/aprox19/NetAprox19.h"
using Network = NetAprox19;
#elif defined(TIMMES_VALIDATE_APROX21)
#include "physics/network/aprox21/NetAprox21.h"
using Network = NetAprox21;
#else
#error "Define TIMMES_VALIDATE_APROX19 or TIMMES_VALIDATE_APROX21"
#endif

namespace {

constexpr int N = Network::NUM_SPECIES;

struct Input
{
    double density;
    double temperature;
    double molar[N];
};

struct Output
{
    double rhs[N];
    double jacobian[N * N];
};

__global__ void baseline_all_column_dual_kernel(
    const Input* inputs, Output* outputs, int count)
{
    const int index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= count) return;
    const Input* input = &inputs[index];
    Output* output = &outputs[index];
    Network::template molar_rhs<double>(
        input->molar, input->density, input->temperature, output->rhs);
    using AD = timmes::Dual<1>;
    for (int column = 0; column < N; ++column) {
        AD y_ad[N];
        AD dydt_ad[N];
        for (int i = 0; i < N; ++i) {
            y_ad[i] = i == column
                    ? AD::variable(input->molar[i], 0)
                    : AD(input->molar[i]);
        }
        Network::template molar_rhs_frozen_screening<AD>(
            y_ad, input->density, input->temperature, dydt_ad);
        for (int row = 0; row < N; ++row) {
            output->jacobian[row * N + column] = dydt_ad[row].deriv[0];
        }
    }
}

__global__ void generated_explicit_kernel(
    const Input* inputs, Output* outputs, int count)
{
    const int index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= count) return;
    const Input* input = &inputs[index];
    Output* output = &outputs[index];
    Network::molar_rhs_jacobian_frozen_screening(
        input->molar, input->density, input->temperature,
        output->rhs, output->jacobian);
}

bool cuda_ok(cudaError_t error, const char* operation)
{
    if (error == cudaSuccess) return true;
    std::fprintf(stderr, "%s: %s\n", operation, cudaGetErrorString(error));
    return false;
}

double scaled_max_error(const double* reference, const double* candidate,
                        int count)
{
    double difference = 0.0;
    double scale = 0.0;
    for (int i = 0; i < count; ++i) {
        difference = std::max(difference,
                              std::abs(reference[i] - candidate[i]));
        scale = std::max(scale, std::abs(reference[i]));
    }
    return difference / std::max(scale, 1.0e-300);
}

} // namespace

int main()
{
    constexpr int kCells = 8192;
    constexpr int kThreads = 64;
    constexpr int kBlocks = (kCells + kThreads - 1) / kThreads;
    constexpr int kWarmup = 5;
    constexpr int kRepeats = 40;
    Input seed{};
    seed.density = 1.0e8;
    seed.temperature = 5.0e9;
    double normalization = 0.0;
    double mass_fraction[N];
    for (int i = 0; i < N; ++i) {
        mass_fraction[i] = 1.0 + 0.05 * ((i + 7) % 11);
        normalization += mass_fraction[i];
    }
    for (int i = 0; i < N; ++i) {
        seed.molar[i] = mass_fraction[i] / normalization / Network::aion(i);
    }

    Input* host_inputs = new Input[kCells];
    for (int cell = 0; cell < kCells; ++cell) {
        host_inputs[cell] = seed;
        host_inputs[cell].density *= 1.0 + 1.0e-6 * (cell % 17);
        host_inputs[cell].temperature *= 1.0 + 1.0e-6 * (cell % 13);
    }

    Input* device_inputs = nullptr;
    Output* baseline = nullptr;
    Output* generated = nullptr;
    bool ok = cuda_ok(cudaMalloc(&device_inputs, sizeof(Input) * kCells), "cudaMalloc(input)")
           && cuda_ok(cudaMalloc(&baseline, sizeof(Output) * kCells), "cudaMalloc(baseline)")
           && cuda_ok(cudaMalloc(&generated, sizeof(Output) * kCells), "cudaMalloc(generated)")
           && cuda_ok(cudaMemcpy(device_inputs, host_inputs, sizeof(Input) * kCells,
                                 cudaMemcpyHostToDevice), "cudaMemcpy(input)");
    delete[] host_inputs;
    if (ok) {
        baseline_all_column_dual_kernel<<<kBlocks, kThreads>>>(
            device_inputs, baseline, kCells);
        generated_explicit_kernel<<<kBlocks, kThreads>>>(
            device_inputs, generated, kCells);
        ok = cuda_ok(cudaGetLastError(), "kernel launch")
          && cuda_ok(cudaDeviceSynchronize(), "kernel synchronize");
    }

    Output host_baseline{};
    Output host_generated{};
    if (ok) {
        ok = cuda_ok(cudaMemcpy(&host_baseline, baseline, sizeof(Output),
                                cudaMemcpyDeviceToHost), "copy baseline")
          && cuda_ok(cudaMemcpy(&host_generated, generated, sizeof(Output),
                                cudaMemcpyDeviceToHost), "copy generated");
    }
    float baseline_ms = 0.0f;
    float generated_ms = 0.0f;
    cudaEvent_t start = nullptr;
    cudaEvent_t stop = nullptr;
    if (ok) {
        ok = cuda_ok(cudaEventCreate(&start), "cudaEventCreate(start)")
          && cuda_ok(cudaEventCreate(&stop), "cudaEventCreate(stop)");
    }
    if (ok) {
        for (int repeat = 0; repeat < kWarmup; ++repeat) {
            baseline_all_column_dual_kernel<<<kBlocks, kThreads>>>(
                device_inputs, baseline, kCells);
            generated_explicit_kernel<<<kBlocks, kThreads>>>(
                device_inputs, generated, kCells);
        }
        ok = cuda_ok(cudaDeviceSynchronize(), "benchmark warmup");
    }
    if (ok) {
        cudaEventRecord(start);
        for (int repeat = 0; repeat < kRepeats; ++repeat) {
            baseline_all_column_dual_kernel<<<kBlocks, kThreads>>>(
                device_inputs, baseline, kCells);
        }
        cudaEventRecord(stop);
        cudaEventSynchronize(stop);
        cudaEventElapsedTime(&baseline_ms, start, stop);

        cudaEventRecord(start);
        for (int repeat = 0; repeat < kRepeats; ++repeat) {
            generated_explicit_kernel<<<kBlocks, kThreads>>>(
                device_inputs, generated, kCells);
        }
        cudaEventRecord(stop);
        cudaEventSynchronize(stop);
        cudaEventElapsedTime(&generated_ms, start, stop);
    }
    if (start != nullptr) cudaEventDestroy(start);
    if (stop != nullptr) cudaEventDestroy(stop);
    cudaFree(device_inputs);
    cudaFree(baseline);
    cudaFree(generated);
    if (!ok) return EXIT_FAILURE;

    const double rhs_error = scaled_max_error(
        host_baseline.rhs, host_generated.rhs, N);
    const double jacobian_error = scaled_max_error(
        host_baseline.jacobian, host_generated.jacobian, N * N);
    std::printf("%s device composition baseline-vs-explicit "
                "rhs_rel=%.12e jac_rel=%.12e "
                "baseline_ms=%.6f explicit_ms=%.6f speedup=%.3f status=%s\n",
                Network::NETWORK_NAME, rhs_error, jacobian_error,
                baseline_ms / kRepeats, generated_ms / kRepeats,
                generated_ms > 0.0f ? baseline_ms / generated_ms : 0.0f,
                jacobian_error <= 1.0e-12 ? "PASS" : "FAIL");
    return jacobian_error <= 1.0e-12 ? EXIT_SUCCESS : EXIT_FAILURE;
}
