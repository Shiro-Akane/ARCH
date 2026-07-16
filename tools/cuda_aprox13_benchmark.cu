/**
 * @file cuda_aprox13_benchmark.cu
 * @brief Batched CPU/OpenMP versus H100 CUDA microbenchmark for current aprox13.
 *
 * Scope is deliberately narrow and honest: one independent network/Jacobian
 * evaluation per cell.  This is not a hydro, ODE-step, NSE, or 2-D application
 * benchmark.  Both paths use the translated Timmes aprox13 RHS, the complete
 * analytic abundance Jacobian, the analytic temperature column and energy row,
 * and form I - dt J.  No legacy pynucastro mirror, standalone archgpu solver,
 * or high-temperature bypass is referenced.
 */

#include <cuda_runtime.h>
#include <omp.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <string>
#include <vector>

#include "physics/network/aprox13/NetAprox13.h"

namespace {

constexpr int kNumSpec = NetAprox13::NUM_SPECIES;
constexpr int kNumEq = NetAprox13::ODE_NEQ;
constexpr int kBlockSize = 128;
constexpr int kWarmupLaunches = 10;
constexpr int kRepeats = 5;
constexpr double kTolerance = 1.0e-12;

struct CellInput
{
    double density;
    double temperature;
    double cv;
    double dt;
    double mass_fractions[kNumSpec];
};

struct FullOutput
{
    double rhs[kNumEq]{};
    double jacobian[kNumEq * kNumEq]{};
    double lhs[kNumEq * kNumEq]{};
};

// Filled from NetAprox13::ENERGY_WEIGHTS at startup.  Keeping this in constant
// memory avoids creating a second, independently maintained nuclear-mass table.
__constant__ double device_energy_weights[kNumSpec];

struct HostMatrix
{
    double data[kNumEq][kNumEq]{};

    void set(int row, int column, double value) noexcept
    {
        data[row - 1][column - 1] = value;
    }
};

bool cuda_ok(cudaError_t error, const char* operation)
{
    if (error == cudaSuccess) {
        return true;
    }
    std::fprintf(stderr, "%s: %s\n", operation, cudaGetErrorString(error));
    return false;
}

CellInput fixed_input()
{
    CellInput input{};
    input.density = 1.0e8;
    input.temperature = 2.0e9;
    input.cv = 1.3e8;
    input.dt = 1.0e-12;

    double normalization = 0.0;
    for (int i = 0; i < kNumSpec; ++i) {
        input.mass_fractions[i] = 1.0 + 0.05 * ((3 * i + 2) % 11);
        normalization += input.mass_fractions[i];
    }
    for (double& value : input.mass_fractions) {
        value /= normalization;
    }
    return input;
}

void evaluate_cpu_full(const CellInput& input, FullOutput& output)
{
    double state[kNumEq]{};
    for (int i = 0; i < kNumSpec; ++i) {
        state[i] = input.mass_fractions[i];
    }
    state[kNumSpec] = input.temperature;

    double enuc = 0.0;
    double denuc_dX[kNumSpec]{};
    double drhs_dT[kNumSpec]{};
    double denuc_dT = 0.0;
    HostMatrix matrix;

    NetAprox13::eval_rhs(state, input.density, output.rhs, enuc);
    NetAprox13::eval_jacobian(state, input.density, matrix, denuc_dX);
    NetAprox13::eval_temperature_derivative(
        state, input.density, drhs_dT, denuc_dT);

    output.rhs[kNumSpec] = enuc / input.cv;
    for (int i = 0; i < kNumSpec; ++i) {
        matrix.set(i + 1, kNumEq, drhs_dT[i]);
        matrix.set(kNumEq, i + 1, denuc_dX[i] / input.cv);
    }
    matrix.set(kNumEq, kNumEq, denuc_dT / input.cv);

    for (int i = 0; i < kNumEq; ++i) {
        for (int j = 0; j < kNumEq; ++j) {
            const int index = i * kNumEq + j;
            output.jacobian[index] = matrix.data[i][j];
            output.lhs[index] = (i == j ? 1.0 : 0.0)
                              - input.dt * matrix.data[i][j];
        }
    }
}

__device__ void evaluate_gpu_full(const CellInput& input, FullOutput& output)
{
    double molar[kNumSpec];
    double molar_rhs[kNumSpec];
    double molar_jacobian[kNumSpec * kNumSpec];
    for (int i = 0; i < kNumSpec; ++i) {
        molar[i] = fmax(1.0e-30,
                        fmin(1.0, input.mass_fractions[i]
                                      / NetAprox13::aion(i)));
    }

    NetAprox13::molar_rhs_jacobian_frozen_screening(
        molar, input.density, input.temperature,
        molar_rhs, molar_jacobian);

    double enuc = 0.0;
    double denuc_dX[kNumSpec]{};
    for (int i = 0; i < kNumSpec; ++i) {
        output.rhs[i] = molar_rhs[i] * NetAprox13::aion(i);
        enuc += molar_rhs[i] * device_energy_weights[i];
        for (int j = 0; j < kNumSpec; ++j) {
            const double derivative =
                molar_jacobian[i * kNumSpec + j];
            output.jacobian[i * kNumEq + j] =
                derivative * NetAprox13::aion(i) / NetAprox13::aion(j);
            denuc_dX[j] += derivative * device_energy_weights[i]
                         / NetAprox13::aion(j);
        }
    }
    enuc *= NetAprox13::ENERGY_CONVERSION;
    for (double& derivative : denuc_dX) {
        derivative *= NetAprox13::ENERGY_CONVERSION;
    }

    using TemperatureAD = timmes::Dual<1>;
    TemperatureAD molar_ad[kNumSpec];
    TemperatureAD molar_rhs_ad[kNumSpec];
    for (int i = 0; i < kNumSpec; ++i) {
        molar_ad[i] = TemperatureAD(molar[i]);
    }
    const TemperatureAD temperature =
        TemperatureAD::variable(input.temperature, 0);
    NetAprox13::molar_rhs_impl<TemperatureAD,
                               timmes::RateTemperatureAccessor>(
        molar_ad, input.density, input.temperature,
        temperature, molar_rhs_ad);

    double denuc_dT = 0.0;
    for (int i = 0; i < kNumSpec; ++i) {
        const double derivative = molar_rhs_ad[i].deriv[0];
        output.jacobian[i * kNumEq + kNumSpec] =
            derivative * NetAprox13::aion(i);
        denuc_dT += derivative * device_energy_weights[i];
    }
    denuc_dT *= NetAprox13::ENERGY_CONVERSION;

    output.rhs[kNumSpec] = enuc / input.cv;
    for (int j = 0; j < kNumSpec; ++j) {
        output.jacobian[kNumSpec * kNumEq + j] =
            denuc_dX[j] / input.cv;
    }
    output.jacobian[kNumSpec * kNumEq + kNumSpec] =
        denuc_dT / input.cv;

    for (int i = 0; i < kNumEq; ++i) {
        for (int j = 0; j < kNumEq; ++j) {
            const int index = i * kNumEq + j;
            output.lhs[index] = (i == j ? 1.0 : 0.0)
                              - input.dt * output.jacobian[index];
        }
    }
}

__global__ void validate_kernel(const CellInput* input, FullOutput* output)
{
    if (blockIdx.x == 0 && threadIdx.x == 0) {
        evaluate_gpu_full(*input, *output);
    }
}

// This bounded transform consumes every RHS/J/LHS element without overflowing
// at stiff states.  The result is stored per cell and reduced on the host so the
// optimizer cannot discard the evaluated operators.  Its cost is intentionally
// present in both CPU and GPU timings.
TIMMES_HD inline double checksum_term(double value, int index)
{
    const double bounded = value / (1.0 + fabs(value));
    return bounded * (1.0 + 0.0009765625 * (index + 1));
}

double evaluate_cpu_checksum(const CellInput& input)
{
    FullOutput output;
    evaluate_cpu_full(input, output);

    double checksum = 0.0;
    for (int i = 0; i < kNumEq; ++i) {
        checksum += checksum_term(output.rhs[i], i);
    }
    for (int i = 0; i < kNumEq * kNumEq; ++i) {
        checksum += checksum_term(output.jacobian[i], kNumEq + i);
        checksum += checksum_term(output.lhs[i], kNumEq + kNumEq * kNumEq + i);
    }
    return checksum;
}

__device__ double evaluate_gpu_checksum(const CellInput& input)
{
    FullOutput output;
    evaluate_gpu_full(input, output);

    double checksum = 0.0;
    for (int i = 0; i < kNumEq; ++i) {
        checksum += checksum_term(output.rhs[i], i);
    }
    for (int i = 0; i < kNumEq * kNumEq; ++i) {
        checksum += checksum_term(output.jacobian[i], kNumEq + i);
        checksum += checksum_term(output.lhs[i], kNumEq + kNumEq * kNumEq + i);
    }
    return checksum;
}

__global__ void benchmark_kernel(const CellInput* inputs, double* checksums,
                                 std::size_t count)
{
    const std::size_t index =
        static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (index < count) {
        checksums[index] = evaluate_gpu_checksum(inputs[index]);
    }
}

double scaled_max_relative_error(const double* reference,
                                 const double* candidate, int count)
{
    double max_difference = 0.0;
    double max_reference = 0.0;
    for (int i = 0; i < count; ++i) {
        max_difference = std::max(
            max_difference, std::abs(reference[i] - candidate[i]));
        max_reference = std::max(max_reference, std::abs(reference[i]));
    }
    return max_difference / std::max(max_reference, 1.0e-300);
}

double scaled_column_relative_error(const double* reference,
                                    const double* candidate, int column)
{
    double reference_column[kNumEq]{};
    double candidate_column[kNumEq]{};
    for (int row = 0; row < kNumEq; ++row) {
        reference_column[row] = reference[row * kNumEq + column];
        candidate_column[row] = candidate[row * kNumEq + column];
    }
    return scaled_max_relative_error(
        reference_column, candidate_column, kNumEq);
}

bool validate_current_operator()
{
    const CellInput input = fixed_input();
    FullOutput cpu_output;
    FullOutput gpu_output;
    evaluate_cpu_full(input, cpu_output);

    CellInput* device_input = nullptr;
    FullOutput* device_output = nullptr;
    bool ok = cuda_ok(cudaMalloc(&device_input, sizeof(CellInput)),
                      "cudaMalloc(validation input)")
           && cuda_ok(cudaMalloc(&device_output, sizeof(FullOutput)),
                      "cudaMalloc(validation output)");
    if (ok) {
        ok = cuda_ok(cudaMemcpy(device_input, &input, sizeof(CellInput),
                                cudaMemcpyHostToDevice),
                     "cudaMemcpy(validation input)");
    }
    if (ok) {
        validate_kernel<<<1, 1>>>(device_input, device_output);
        ok = cuda_ok(cudaGetLastError(), "validate_kernel launch")
          && cuda_ok(cudaDeviceSynchronize(), "validate_kernel sync")
          && cuda_ok(cudaMemcpy(&gpu_output, device_output,
                                sizeof(FullOutput), cudaMemcpyDeviceToHost),
                     "cudaMemcpy(validation output)");
    }
    cudaFree(device_input);
    cudaFree(device_output);
    if (!ok) {
        return false;
    }

    const double rhs_error = scaled_max_relative_error(
        cpu_output.rhs, gpu_output.rhs, kNumEq);
    const double jacobian_error = scaled_max_relative_error(
        cpu_output.jacobian, gpu_output.jacobian, kNumEq * kNumEq);
    const double lhs_error = scaled_max_relative_error(
        cpu_output.lhs, gpu_output.lhs, kNumEq * kNumEq);
    const double temperature_column_error = scaled_column_relative_error(
        cpu_output.jacobian, gpu_output.jacobian, kNumSpec);
    const double temperature_row_error = scaled_max_relative_error(
        cpu_output.jacobian + kNumSpec * kNumEq,
        gpu_output.jacobian + kNumSpec * kNumEq, kNumEq);
    const bool pass = rhs_error <= kTolerance
                   && jacobian_error <= kTolerance
                   && lhs_error <= kTolerance
                   && temperature_column_error <= kTolerance
                   && temperature_row_error <= kTolerance;

    std::printf(
        "validation,rhs_rel=%.12e,jac_rel=%.12e,lhs_rel=%.12e,"
        "temp_col_rel=%.12e,temp_row_rel=%.12e,status=%s\n",
        rhs_error, jacobian_error, lhs_error,
        temperature_column_error, temperature_row_error,
        pass ? "PASS" : "FAIL");
    return pass;
}

double median(std::vector<double> values)
{
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

double checksum_sum(const std::vector<double>& checksums)
{
    return std::accumulate(checksums.begin(), checksums.end(), 0.0);
}

double time_cpu(const std::vector<CellInput>& inputs,
                std::vector<double>& checksums, int threads)
{
    omp_set_dynamic(0);
    omp_set_num_threads(threads);

    // One untimed full-batch CPU warmup is enough to fault pages and initialize
    // OpenMP.  The required ten-launch warmup below is applied to CUDA.
#pragma omp parallel for schedule(static)
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        checksums[i] = evaluate_cpu_checksum(inputs[i]);
    }

    std::vector<double> seconds;
    seconds.reserve(kRepeats);
    for (int repeat = 0; repeat < kRepeats; ++repeat) {
        const auto start = std::chrono::steady_clock::now();
#pragma omp parallel for schedule(static)
        for (std::size_t i = 0; i < inputs.size(); ++i) {
            checksums[i] = evaluate_cpu_checksum(inputs[i]);
        }
        const auto stop = std::chrono::steady_clock::now();
        seconds.push_back(
            std::chrono::duration<double>(stop - start).count());
    }
    return median(seconds);
}

struct GpuTimes
{
    double kernel_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    double checksum = 0.0;
};

bool time_gpu(const std::vector<CellInput>& inputs,
              std::vector<double>& host_checksums, GpuTimes& result)
{
    const std::size_t count = inputs.size();
    const std::size_t input_bytes = count * sizeof(CellInput);
    const std::size_t output_bytes = count * sizeof(double);
    CellInput* device_inputs = nullptr;
    double* device_checksums = nullptr;
    cudaEvent_t start_event = nullptr;
    cudaEvent_t stop_event = nullptr;

    bool ok = cuda_ok(cudaMalloc(&device_inputs, input_bytes),
                      "cudaMalloc(benchmark inputs)")
           && cuda_ok(cudaMalloc(&device_checksums, output_bytes),
                      "cudaMalloc(benchmark checksums)")
           && cuda_ok(cudaEventCreate(&start_event), "cudaEventCreate(start)")
           && cuda_ok(cudaEventCreate(&stop_event), "cudaEventCreate(stop)");
    if (!ok) {
        cudaFree(device_inputs);
        cudaFree(device_checksums);
        if (start_event) cudaEventDestroy(start_event);
        if (stop_event) cudaEventDestroy(stop_event);
        return false;
    }

    const unsigned int blocks = static_cast<unsigned int>(
        (count + kBlockSize - 1) / kBlockSize);
    ok = cuda_ok(cudaMemcpy(device_inputs, inputs.data(), input_bytes,
                            cudaMemcpyHostToDevice),
                 "cudaMemcpy(warmup inputs)");

    for (int warmup = 0; ok && warmup < kWarmupLaunches; ++warmup) {
        benchmark_kernel<<<blocks, kBlockSize>>>(
            device_inputs, device_checksums, count);
        ok = cuda_ok(cudaGetLastError(), "benchmark warmup launch");
    }
    if (ok) {
        ok = cuda_ok(cudaDeviceSynchronize(), "benchmark warmup sync");
    }

    std::vector<double> kernel_seconds;
    kernel_seconds.reserve(kRepeats);
    for (int repeat = 0; ok && repeat < kRepeats; ++repeat) {
        ok = cuda_ok(cudaEventRecord(start_event), "cudaEventRecord(start)");
        benchmark_kernel<<<blocks, kBlockSize>>>(
            device_inputs, device_checksums, count);
        ok = ok && cuda_ok(cudaGetLastError(), "benchmark kernel launch")
             && cuda_ok(cudaEventRecord(stop_event), "cudaEventRecord(stop)")
             && cuda_ok(cudaEventSynchronize(stop_event),
                        "cudaEventSynchronize(stop)");
        float elapsed_ms = 0.0F;
        if (ok) {
            ok = cuda_ok(cudaEventElapsedTime(
                             &elapsed_ms, start_event, stop_event),
                         "cudaEventElapsedTime");
        }
        if (ok) {
            kernel_seconds.push_back(1.0e-3 * elapsed_ms);
        }
    }

    std::vector<double> end_to_end_seconds;
    end_to_end_seconds.reserve(kRepeats);
    for (int repeat = 0; ok && repeat < kRepeats; ++repeat) {
        const auto start = std::chrono::steady_clock::now();
        ok = cuda_ok(cudaMemcpy(device_inputs, inputs.data(), input_bytes,
                                cudaMemcpyHostToDevice),
                     "cudaMemcpy(timed H2D)");
        if (ok) {
            benchmark_kernel<<<blocks, kBlockSize>>>(
                device_inputs, device_checksums, count);
            ok = cuda_ok(cudaGetLastError(), "timed kernel launch")
              && cuda_ok(cudaMemcpy(host_checksums.data(), device_checksums,
                                    output_bytes, cudaMemcpyDeviceToHost),
                         "cudaMemcpy(timed D2H)");
        }
        const auto stop = std::chrono::steady_clock::now();
        if (ok) {
            end_to_end_seconds.push_back(
                std::chrono::duration<double>(stop - start).count());
        }
    }

    if (ok) {
        result.kernel_seconds = median(kernel_seconds);
        result.end_to_end_seconds = median(end_to_end_seconds);
        result.checksum = checksum_sum(host_checksums);
    }

    cudaEventDestroy(start_event);
    cudaEventDestroy(stop_event);
    cudaFree(device_inputs);
    cudaFree(device_checksums);
    return ok;
}

void print_hardware()
{
    int device = 0;
    cudaDeviceProp properties{};
    cudaGetDevice(&device);
    cudaGetDeviceProperties(&properties, device);
    cudaFuncAttributes attributes{};
    cudaFuncGetAttributes(&attributes, benchmark_kernel);
    std::printf(
        "hardware,gpu=%s,cc=%d.%d,sm=%d,cpu_max_threads=%d,"
        "kernel_registers=%d,kernel_local_bytes=%zu,block=%d\n",
        properties.name, properties.major, properties.minor,
        properties.multiProcessorCount, omp_get_num_procs(),
        attributes.numRegs, attributes.localSizeBytes, kBlockSize);
    std::printf(
        "protocol,network=aprox13,operators=RHS+complete_J+analytic_dT+LHS,"
        "fixed_rho=1e8,fixed_T=2e9,warmup_cuda=%d,repeats=%d,"
        "timing=median,scope=microphysics_only\n",
        kWarmupLaunches, kRepeats);
}

} // namespace

int main()
{
    if (!cuda_ok(cudaMemcpyToSymbol(
            device_energy_weights, NetAprox13::ENERGY_WEIGHTS.data(),
            sizeof(device_energy_weights)),
            "cudaMemcpyToSymbol(device_energy_weights)")) {
        return EXIT_FAILURE;
    }
    if (!validate_current_operator()) {
        return EXIT_FAILURE;
    }

    print_hardware();
    const int cpu_threads = std::min(16, omp_get_num_procs());
    constexpr std::array<std::size_t, 4> batch_sizes{
        1024, 16384, 65536, 262144};
    const CellInput input = fixed_input();

    std::printf(
        "columns,cells,cpu1_cells_s,cpuN_threads,cpuN_cells_s,"
        "gpu_kernel_cells_s,gpu_e2e_cells_s,"
        "kernel_speedup_vs_cpu1,kernel_speedup_vs_cpuN,"
        "e2e_speedup_vs_cpu1,e2e_speedup_vs_cpuN,"
        "cpu1_checksum,cpuN_checksum,gpu_checksum\n");

    volatile double checksum_sink = 0.0;
    for (const std::size_t count : batch_sizes) {
        std::vector<CellInput> inputs(count, input);
        std::vector<double> cpu_checksums(count);
        std::vector<double> gpu_checksums(count);

        const double cpu1_seconds = time_cpu(inputs, cpu_checksums, 1);
        const double cpu1_checksum = checksum_sum(cpu_checksums);
        const double cpuN_seconds =
            time_cpu(inputs, cpu_checksums, cpu_threads);
        const double cpuN_checksum = checksum_sum(cpu_checksums);
        GpuTimes gpu_times;
        if (!time_gpu(inputs, gpu_checksums, gpu_times)) {
            return EXIT_FAILURE;
        }

        checksum_sink += cpu1_checksum + cpuN_checksum + gpu_times.checksum;
        const double cells = static_cast<double>(count);
        const double cpu1_rate = cells / cpu1_seconds;
        const double cpuN_rate = cells / cpuN_seconds;
        const double gpu_kernel_rate = cells / gpu_times.kernel_seconds;
        const double gpu_e2e_rate = cells / gpu_times.end_to_end_seconds;
        std::printf(
            "result,%zu,%.6e,%d,%.6e,%.6e,%.6e,"
            "%.6f,%.6f,%.6f,%.6f,%.12e,%.12e,%.12e\n",
            count, cpu1_rate, cpu_threads, cpuN_rate,
            gpu_kernel_rate, gpu_e2e_rate,
            gpu_kernel_rate / cpu1_rate,
            gpu_kernel_rate / cpuN_rate,
            gpu_e2e_rate / cpu1_rate,
            gpu_e2e_rate / cpuN_rate,
            cpu1_checksum, cpuN_checksum, gpu_times.checksum);
        std::fflush(stdout);
    }

    std::printf(
        "scope_note,this is an aprox13 network/Jacobian microbenchmark; "
        "it is not an end-to-end ODE hydro NSE or 2-D speedup,"
        "checksum_sink=%.12e\n",
        static_cast<double>(checksum_sink));
    return EXIT_SUCCESS;
}
