/**
 * @file cuda_aprox13_be_warp_validation.cu
 * @brief One-warp-per-cell BE/Newton/cooperative-LU validation and benchmark.
 *
 * This prototype reuses the bounded one-thread baseline in
 * cuda_aprox13_be_validation.cu.  Network rate/Jacobian evaluation remains a
 * lane-0 call to the current translated Timmes NetAprox13; matrix assembly,
 * partial-pivot LU, substitutions, Newton updates, normalization, and
 * residual reductions are cooperative across the warp.  The split is
 * intentional: it measures the resource benefit of cooperative ODE/LU while
 * exposing the still-serial rate/Jacobian stage.
 */

#define main cuda_aprox13_be_baseline_embedded_main
#include "cuda_aprox13_be_validation.cu"
#undef main

#include <vector>

namespace {

constexpr unsigned kFullWarpMask = 0xffffffffu;
constexpr int kWarpSize = 32;
constexpr int kWarpsPerBlock = 4;
constexpr int kWarpBlockSize = kWarpSize * kWarpsPerBlock;
constexpr int kBenchmarkCells = 8192;
constexpr int kBenchmarkRepeats = 5;

struct alignas(16) WarpWorkspace {
    double state[kNumEq];
    double old_state[kNumEq];
    double rhs[kNumEq];
    double matrix[kNumEq * kNumEq];
    double correction[kNumEq];
    double forward[kNumEq];
    int permutation[kNumEq];
    int failure;
    int converged;
};

__device__ double warp_sum(double value)
{
    for (int offset = kWarpSize / 2; offset > 0; offset /= 2) {
        value += __shfl_down_sync(kFullWarpMask, value, offset);
    }
    return __shfl_sync(kFullWarpMask, value, 0);
}

__device__ double warp_max(double value)
{
    for (int offset = kWarpSize / 2; offset > 0; offset /= 2) {
        value = fmax(value,
                     __shfl_down_sync(kFullWarpMask, value, offset));
    }
    return __shfl_sync(kFullWarpMask, value, 0);
}

__device__ bool cooperative_lu_solve(WarpWorkspace& workspace, int lane)
{
    for (int i = lane; i < kNumEq; i += kWarpSize) {
        workspace.permutation[i] = i;
    }
    __syncwarp();

    for (int column = 0; column < kNumEq; ++column) {
        const int candidate = column + lane;
        double pivot_value = -1.0;
        int pivot_index = kNumEq;
        if (candidate < kNumEq) {
            pivot_value = fabs(workspace.matrix[
                workspace.permutation[candidate] * kNumEq + column]);
            pivot_index = candidate;
        }

        // Deterministic maximum with the lower logical-row index breaking
        // exact ties, matching the serial solver's first-maximum behavior.
        for (int offset = kWarpSize / 2; offset > 0; offset /= 2) {
            const double other_value = __shfl_down_sync(
                kFullWarpMask, pivot_value, offset);
            const int other_index = __shfl_down_sync(
                kFullWarpMask, pivot_index, offset);
            if (other_value > pivot_value
                || (other_value == pivot_value && other_index < pivot_index)) {
                pivot_value = other_value;
                pivot_index = other_index;
            }
        }

        if (lane == 0) {
            if (pivot_value < 1.0e-20) {
                workspace.failure = kSingular;
            } else {
                const int temporary = workspace.permutation[column];
                workspace.permutation[column] =
                    workspace.permutation[pivot_index];
                workspace.permutation[pivot_index] = temporary;
            }
        }
        __syncwarp();
        if (workspace.failure != kSuccess) return false;

        const double pivot_inverse = 1.0 / workspace.matrix[
            workspace.permutation[column] * kNumEq + column];
        const int elimination_row = column + 1 + lane;
        if (elimination_row < kNumEq) {
            workspace.matrix[
                workspace.permutation[elimination_row] * kNumEq + column]
                *= pivot_inverse;
        }
        __syncwarp();

        const int remaining = kNumEq - column - 1;
        const int update_count = remaining * remaining;
        for (int task = lane; task < update_count; task += kWarpSize) {
            const int logical_row = column + 1 + task / remaining;
            const int update_column = column + 1 + task % remaining;
            const int physical_row = workspace.permutation[logical_row];
            workspace.matrix[physical_row * kNumEq + update_column] -=
                workspace.matrix[physical_row * kNumEq + column]
              * workspace.matrix[
                    workspace.permutation[column] * kNumEq + update_column];
        }
        __syncwarp();
    }

    // Preserve DenseLUSolver's serial accumulation order in substitutions.
    // The O(N^3) elimination above remains cooperative; keeping these O(N^2)
    // sums ordered avoids amplifying roundoff in the binding-energy delta.
    if (lane == 0) {
        for (int row = 0; row < kNumEq; ++row) {
            workspace.forward[row] =
                workspace.correction[workspace.permutation[row]];
            for (int column = 0; column < row; ++column) {
                workspace.forward[row] -= workspace.matrix[
                    workspace.permutation[row] * kNumEq + column]
                    * workspace.forward[column];
            }
        }
        for (int row = kNumEq - 1; row >= 0; --row) {
            workspace.correction[row] = workspace.forward[row];
            for (int column = row + 1; column < kNumEq; ++column) {
                workspace.correction[row] -= workspace.matrix[
                    workspace.permutation[row] * kNumEq + column]
                    * workspace.correction[column];
            }
            workspace.correction[row] /= workspace.matrix[
                workspace.permutation[row] * kNumEq + row];
        }
    }
    __syncwarp();
    return true;
}

__device__ void integrate_warp(const TestInput& input, TestOutput& output,
                               WarpWorkspace& workspace, int lane)
{
    for (int i = lane; i < kNumSpec; i += kWarpSize) {
        workspace.state[i] = input.mass_fractions[i];
    }
    if (lane == kNumSpec) workspace.state[kNumSpec] = input.temperature;
    if (lane == 0) {
        workspace.failure = kSuccess;
        workspace.converged = 0;
        if (!(input.density > 0.0) || !(input.cv > 0.0)
            || !(input.dt_total > 0.0) || input.substeps <= 0
            || !isfinite(input.temperature)) {
            workspace.failure = kInvalidInput;
        }
    }
    __syncwarp();

    if (workspace.failure == kSuccess) {
        const double step = input.dt_total / input.substeps;
        for (int substep = 0; substep < input.substeps; ++substep) {
            for (int i = lane; i < kNumEq; i += kWarpSize) {
                workspace.old_state[i] = workspace.state[i];
            }
            if (lane == 0) workspace.converged = 0;
            __syncwarp();

            for (int iteration = 0; iteration < kMaxNewton; ++iteration) {
                if (lane == 0) {
                    evaluate_gpu(workspace.state, input.density, input.cv,
                                 workspace.rhs, workspace.matrix);
                }
                __syncwarp();

                bool local_finite = true;
                for (int i = lane; i < kNumEq; i += kWarpSize) {
                    local_finite = local_finite && isfinite(workspace.rhs[i]);
                }
                for (int i = lane; i < kNumEq * kNumEq; i += kWarpSize) {
                    local_finite = local_finite
                                && isfinite(workspace.matrix[i]);
                }
                if (__ballot_sync(kFullWarpMask, !local_finite) != 0) {
                    if (lane == 0) workspace.failure = kNonFinite;
                    __syncwarp();
                    break;
                }

                double local_stiffness = 0.0;
                for (int i = lane; i < kNumEq; i += kWarpSize) {
                    workspace.correction[i] = workspace.old_state[i]
                                            - workspace.state[i]
                                            + step * workspace.rhs[i];
                }
                for (int index = lane; index < kNumEq * kNumEq;
                     index += kWarpSize) {
                    const double scaled = step * workspace.matrix[index];
                    local_stiffness = fmax(local_stiffness, fabs(scaled));
                    const int row = index / kNumEq;
                    const int column = index % kNumEq;
                    workspace.matrix[index] =
                        (row == column ? 1.0 : 0.0) - scaled;
                }
                local_stiffness = warp_max(local_stiffness);
                if (lane == 0) {
                    output.stiffness_proxy = fmax(
                        output.stiffness_proxy, local_stiffness);
                }
                __syncwarp();

                if (!cooperative_lu_solve(workspace, lane)) break;

                bool admissible = true;
                double local_mass = 0.0;
                double local_update_square = 0.0;
                if (lane < kNumEq) {
                    const double trial = workspace.state[lane]
                                       + workspace.correction[lane];
                    workspace.forward[lane] = trial;
                    admissible = isfinite(trial);
                    if (lane < kNumSpec) {
                        admissible = admissible
                                  && trial >= -10.0 * kAtol
                                  && trial <= 1.0 + 10.0 * kAtol;
                        local_mass = trial;
                    } else {
                        admissible = admissible
                                  && trial >= kSmallT && trial <= kMaxT;
                    }
                    const double weight =
                        kRtol * fabs(workspace.state[lane]) + kAtol;
                    const double scaled = workspace.correction[lane] / weight;
                    local_update_square = scaled * scaled;
                }
                const double mass_sum = warp_sum(local_mass);
                const double update_sum = warp_sum(local_update_square);
                if (lane == 0) {
                    admissible = admissible && isfinite(mass_sum)
                              && mass_sum > 0.0
                              && fabs(mass_sum - 1.0) <= 100.0 * kRtol;
                }
                const unsigned invalid = __ballot_sync(
                    kFullWarpMask, !admissible);
                if (invalid != 0) {
                    if (lane == 0) workspace.failure = kInadmissible;
                    __syncwarp();
                    break;
                }

                for (int i = lane; i < kNumEq; i += kWarpSize) {
                    workspace.state[i] = workspace.forward[i];
                }
                if (lane == 0) ++output.total_newton_iterations;
                __syncwarp();

                const double update_norm = sqrt(update_sum / kNumEq);
                if (update_norm < 1.0) {
                    if (lane < kNumSpec) {
                        workspace.state[lane] =
                            fmax(workspace.state[lane], kSmallX);
                    }
                    __syncwarp();
                    if (lane == 0) {
                        double projected_sum = 0.0;
                        for (int i = 0; i < kNumSpec; ++i) {
                            projected_sum += workspace.state[i];
                        }
                        workspace.forward[0] = projected_sum;
                    }
                    __syncwarp();
                    if (lane < kNumSpec) {
                        workspace.state[lane] /= workspace.forward[0];
                    }
                    if (lane == 0) workspace.converged = 1;
                    __syncwarp();
                    break;
                }
            }

            if (workspace.failure != kSuccess) break;
            if (!workspace.converged) {
                if (lane == 0) workspace.failure = kMaxIterations;
                __syncwarp();
                break;
            }

            if (lane == 0) {
                evaluate_gpu(workspace.state, input.density, input.cv,
                             workspace.rhs, workspace.matrix);
            }
            __syncwarp();
            double local_residual = 0.0;
            if (lane < kNumEq) {
                const double residual = workspace.state[lane]
                                      - workspace.old_state[lane]
                                      - step * workspace.rhs[lane];
                const double scale = fmax(
                    fmax(fabs(workspace.old_state[lane]),
                         fabs(workspace.state[lane])), 1.0);
                local_residual = fabs(residual) / scale;
            }
            const double residual_max = warp_max(local_residual);
            double mass_value = lane < kNumSpec ? workspace.state[lane] : 0.0;
            const double mass_sum = warp_sum(mass_value);
            if (lane == 0) {
                output.max_newton_residual = fmax(
                    output.max_newton_residual, residual_max);
                output.max_mass_error = fmax(
                    output.max_mass_error, fabs(mass_sum - 1.0));
                ++output.completed_substeps;
            }
            __syncwarp();
        }
    }

    for (int i = lane; i < kNumEq; i += kWarpSize) {
        output.state[i] = workspace.state[i];
    }
    __syncwarp();
    if (lane == 0) {
        output.failure = workspace.failure;
        if (workspace.failure == kSuccess) {
            double nuclear = 0.0;
            for (int i = 0; i < kNumSpec; ++i) {
                nuclear += (workspace.state[i] - input.mass_fractions[i])
                         / NetAprox13::aion(i) * device_energy_weights[i];
            }
            output.nuclear_energy = NetAprox13::ENERGY_CONVERSION * nuclear;
            output.thermal_energy = input.cv
                * (workspace.state[kNumSpec] - input.temperature);
            const double scale = fmax(
                fmax(fabs(output.nuclear_energy),
                     fabs(output.thermal_energy)), 1.0);
            output.energy_closure_rel =
                fabs(output.thermal_energy - output.nuclear_energy) / scale;
            if (!isfinite(output.energy_closure_rel)) {
                output.failure = kNonFinite;
            } else if (output.energy_closure_rel
                       > kEnergyClosureTolerance) {
                output.failure = kEnergyClosure;
            }
        }
    }
}

__global__ __launch_bounds__(kWarpBlockSize, 4)
void integrate_warp_kernel(const TestInput* inputs,
                                      TestOutput* outputs, int count)
{
    extern __shared__ unsigned char shared_storage[];
    auto* workspaces = reinterpret_cast<WarpWorkspace*>(shared_storage);
    const int lane = threadIdx.x % kWarpSize;
    const int warp_in_block = threadIdx.x / kWarpSize;
    const int cell = blockIdx.x * kWarpsPerBlock + warp_in_block;
    if (cell < count) {
        integrate_warp(inputs[cell], outputs[cell],
                       workspaces[warp_in_block], lane);
    }
}

struct Comparison {
    double x = 0.0;
    double temperature = 0.0;
    double nuclear_energy = 0.0;
    double thermal_energy = 0.0;
    double residual = 0.0;
    bool flags = true;
};

Comparison compare_outputs(const TestOutput* reference,
                           const TestOutput* candidate, int count)
{
    Comparison result;
    for (int i = 0; i < count; ++i) {
        result.flags = result.flags
                    && reference[i].failure == candidate[i].failure;
        if (reference[i].failure == kSuccess
            && candidate[i].failure == kSuccess) {
            result.x = std::max(result.x, scaled_error(
                reference[i].state, candidate[i].state, kNumSpec));
            result.temperature = std::max(result.temperature,
                scalar_scaled_error(reference[i].state[kNumSpec],
                                    candidate[i].state[kNumSpec]));
            result.nuclear_energy = std::max(result.nuclear_energy,
                scalar_scaled_error(reference[i].nuclear_energy,
                                    candidate[i].nuclear_energy));
            result.thermal_energy = std::max(result.thermal_energy,
                scalar_scaled_error(reference[i].thermal_energy,
                                    candidate[i].thermal_energy));
            result.residual = std::max(result.residual,
                scalar_scaled_error(reference[i].max_newton_residual,
                                    candidate[i].max_newton_residual));
        }
    }
    return result;
}

float benchmark_one_thread(TestInput* inputs, TestOutput* outputs, int count)
{
    cudaEvent_t start{}, stop{};
    cudaEventCreate(&start);
    cudaEventCreate(&stop);
    std::array<float, kBenchmarkRepeats> samples{};
    for (int warmup = 0; warmup < 2; ++warmup) {
        cudaMemset(outputs, 0, sizeof(TestOutput) * count);
        integrate_kernel<<<(count + 127) / 128, 128>>>(
            inputs, outputs, count);
    }
    cudaDeviceSynchronize();
    for (int repeat = 0; repeat < kBenchmarkRepeats; ++repeat) {
        cudaMemset(outputs, 0, sizeof(TestOutput) * count);
        cudaEventRecord(start);
        integrate_kernel<<<(count + 127) / 128, 128>>>(
            inputs, outputs, count);
        cudaEventRecord(stop);
        cudaEventSynchronize(stop);
        float elapsed = 0.0f;
        cudaEventElapsedTime(&elapsed, start, stop);
        samples[repeat] = elapsed;
    }
    cudaEventDestroy(start);
    cudaEventDestroy(stop);
    std::sort(samples.begin(), samples.end());
    return samples[kBenchmarkRepeats / 2];
}

float benchmark_warp(TestInput* inputs, TestOutput* outputs, int count)
{
    cudaEvent_t start{}, stop{};
    cudaEventCreate(&start);
    cudaEventCreate(&stop);
    const int blocks = (count + kWarpsPerBlock - 1) / kWarpsPerBlock;
    const std::size_t shared_bytes =
        kWarpsPerBlock * sizeof(WarpWorkspace);
    std::array<float, kBenchmarkRepeats> samples{};
    for (int warmup = 0; warmup < 2; ++warmup) {
        cudaMemset(outputs, 0, sizeof(TestOutput) * count);
        integrate_warp_kernel<<<blocks, kWarpBlockSize, shared_bytes>>>(
            inputs, outputs, count);
    }
    cudaDeviceSynchronize();
    for (int repeat = 0; repeat < kBenchmarkRepeats; ++repeat) {
        cudaMemset(outputs, 0, sizeof(TestOutput) * count);
        cudaEventRecord(start);
        integrate_warp_kernel<<<blocks, kWarpBlockSize, shared_bytes>>>(
            inputs, outputs, count);
        cudaEventRecord(stop);
        cudaEventSynchronize(stop);
        float elapsed = 0.0f;
        cudaEventElapsedTime(&elapsed, start, stop);
        samples[repeat] = elapsed;
    }
    cudaEventDestroy(start);
    cudaEventDestroy(stop);
    std::sort(samples.begin(), samples.end());
    return samples[kBenchmarkRepeats / 2];
}

} // namespace

int main()
{
    const auto inputs = make_inputs();
    std::array<TestOutput, kNumCases> cpu{};
    std::array<TestOutput, kNumCases> baseline{};
    std::array<TestOutput, kNumCases> warp{};
    for (int i = 0; i < kNumCases; ++i) integrate_cpu(inputs[i], cpu[i]);

    if (!cuda_ok(cudaMemcpyToSymbol(
            device_energy_weights, NetAprox13::ENERGY_WEIGHTS.data(),
            sizeof(device_energy_weights)), "cudaMemcpyToSymbol(weights)")) {
        return EXIT_FAILURE;
    }

    TestInput* device_inputs = nullptr;
    TestOutput* device_baseline = nullptr;
    TestOutput* device_warp = nullptr;
    bool success = cuda_ok(cudaMalloc(&device_inputs, sizeof(inputs)),
                           "cudaMalloc(inputs)")
                && cuda_ok(cudaMalloc(&device_baseline, sizeof(baseline)),
                           "cudaMalloc(baseline)")
                && cuda_ok(cudaMalloc(&device_warp, sizeof(warp)),
                           "cudaMalloc(warp)");
    if (success) {
        cudaMemcpy(device_inputs, inputs.data(), sizeof(inputs),
                   cudaMemcpyHostToDevice);
        cudaMemset(device_baseline, 0, sizeof(baseline));
        cudaMemset(device_warp, 0, sizeof(warp));
        integrate_kernel<<<1, 32>>>(device_inputs, device_baseline, kNumCases);
        integrate_warp_kernel<<<
            (kNumCases + kWarpsPerBlock - 1) / kWarpsPerBlock,
            kWarpBlockSize,
            kWarpsPerBlock * sizeof(WarpWorkspace)>>>(
                device_inputs, device_warp, kNumCases);
        success = cuda_ok(cudaGetLastError(), "validation kernels")
               && cuda_ok(cudaDeviceSynchronize(), "validation sync")
               && cuda_ok(cudaMemcpy(baseline.data(), device_baseline,
                                     sizeof(baseline), cudaMemcpyDeviceToHost),
                          "copy baseline")
               && cuda_ok(cudaMemcpy(warp.data(), device_warp,
                                     sizeof(warp), cudaMemcpyDeviceToHost),
                          "copy warp");
    }
    cudaFree(device_inputs);
    cudaFree(device_baseline);
    cudaFree(device_warp);
    if (!success) return EXIT_FAILURE;

    const Comparison cpu_to_baseline = compare_outputs(
        cpu.data(), baseline.data(), kNumCases);
    const Comparison cpu_to_warp = compare_outputs(
        cpu.data(), warp.data(), kNumCases);
    const Comparison baseline_to_warp = compare_outputs(
        baseline.data(), warp.data(), kNumCases);
    for (int i = 0; i < kNumCases; ++i) {
        std::printf(
            "case=%d cpu_flag=%d thread_flag=%d warp_flag=%d "
            "cpu_it=%d thread_it=%d warp_it=%d "
            "cpu_res=%.3e thread_res=%.3e warp_res=%.3e "
            "cpu_enuc=%.17e warp_enuc=%.17e enuc_rel=%.3e\n",
            i, cpu[i].failure, baseline[i].failure, warp[i].failure,
            cpu[i].total_newton_iterations,
            baseline[i].total_newton_iterations,
            warp[i].total_newton_iterations,
            cpu[i].max_newton_residual,
            baseline[i].max_newton_residual,
            warp[i].max_newton_residual,
            cpu[i].nuclear_energy, warp[i].nuclear_energy,
            scalar_scaled_error(cpu[i].nuclear_energy,
                                warp[i].nuclear_energy));
    }

    std::vector<TestInput> benchmark_inputs(kBenchmarkCells);
    for (int i = 0; i < kBenchmarkCells; ++i) {
        benchmark_inputs[i] = inputs[i % (kNumCases - 1)];
    }
    TestInput* benchmark_device_inputs = nullptr;
    TestOutput* benchmark_thread_outputs = nullptr;
    TestOutput* benchmark_warp_outputs = nullptr;
    cudaMalloc(&benchmark_device_inputs,
               sizeof(TestInput) * kBenchmarkCells);
    cudaMalloc(&benchmark_thread_outputs,
               sizeof(TestOutput) * kBenchmarkCells);
    cudaMalloc(&benchmark_warp_outputs,
               sizeof(TestOutput) * kBenchmarkCells);
    cudaMemcpy(benchmark_device_inputs, benchmark_inputs.data(),
               sizeof(TestInput) * kBenchmarkCells, cudaMemcpyHostToDevice);

    const float thread_ms = benchmark_one_thread(
        benchmark_device_inputs, benchmark_thread_outputs, kBenchmarkCells);
    const float warp_ms = benchmark_warp(
        benchmark_device_inputs, benchmark_warp_outputs, kBenchmarkCells);
    std::vector<TestOutput> benchmark_thread(kBenchmarkCells);
    std::vector<TestOutput> benchmark_warp_values(kBenchmarkCells);
    cudaMemcpy(benchmark_thread.data(), benchmark_thread_outputs,
               sizeof(TestOutput) * kBenchmarkCells, cudaMemcpyDeviceToHost);
    cudaMemcpy(benchmark_warp_values.data(), benchmark_warp_outputs,
               sizeof(TestOutput) * kBenchmarkCells, cudaMemcpyDeviceToHost);
    const Comparison benchmark_comparison = compare_outputs(
        benchmark_thread.data(), benchmark_warp_values.data(),
        kBenchmarkCells);
    cudaFree(benchmark_device_inputs);
    cudaFree(benchmark_thread_outputs);
    cudaFree(benchmark_warp_outputs);

    cudaDeviceProp property{};
    int device = 0;
    int thread_blocks = 0;
    int warp_blocks = 0;
    cudaGetDevice(&device);
    cudaGetDeviceProperties(&property, device);
    cudaOccupancyMaxActiveBlocksPerMultiprocessor(
        &thread_blocks, integrate_kernel, 128, 0);
    cudaOccupancyMaxActiveBlocksPerMultiprocessor(
        &warp_blocks, integrate_warp_kernel, kWarpBlockSize,
        kWarpsPerBlock * sizeof(WarpWorkspace));
    const double thread_occupancy =
        static_cast<double>(thread_blocks * 128)
        / property.maxThreadsPerMultiProcessor;
    const double warp_occupancy =
        static_cast<double>(warp_blocks * kWarpBlockSize)
        / property.maxThreadsPerMultiProcessor;

    const auto within_tolerance = [](const Comparison& comparison) {
        return comparison.flags
            && comparison.x <= kParityTolerance
            && comparison.temperature <= kParityTolerance
            && comparison.nuclear_energy <= kParityTolerance
            && comparison.thermal_energy <= kParityTolerance
            && comparison.residual <= kParityTolerance;
    };
    const bool pass = within_tolerance(cpu_to_baseline)
                   && within_tolerance(cpu_to_warp)
                   && within_tolerance(baseline_to_warp)
                   && within_tolerance(benchmark_comparison);

    std::printf(
        "cpu_vs_warp x=%.12e T=%.12e enuc=%.12e etherm=%.12e "
        "residual=%.12e flags=%s\n",
        cpu_to_warp.x, cpu_to_warp.temperature,
        cpu_to_warp.nuclear_energy, cpu_to_warp.thermal_energy,
        cpu_to_warp.residual, cpu_to_warp.flags ? "yes" : "no");
    std::printf(
        "thread_vs_warp x=%.12e T=%.12e enuc=%.12e etherm=%.12e "
        "residual=%.12e flags=%s\n",
        baseline_to_warp.x, baseline_to_warp.temperature,
        baseline_to_warp.nuclear_energy,
        baseline_to_warp.thermal_energy,
        baseline_to_warp.residual,
        baseline_to_warp.flags ? "yes" : "no");
    std::printf(
        "batch=%d repeats=%d thread_median_ms=%.6f warp_median_ms=%.6f "
        "thread_cells_per_s=%.3e warp_cells_per_s=%.3e speedup=%.3f "
        "batch_flags=%s batch_x=%.3e\n",
        kBenchmarkCells, kBenchmarkRepeats, thread_ms, warp_ms,
        1000.0 * kBenchmarkCells / thread_ms,
        1000.0 * kBenchmarkCells / warp_ms,
        thread_ms / warp_ms,
        benchmark_comparison.flags ? "yes" : "no",
        benchmark_comparison.x);
    std::printf(
        "resource gpu=%s cc=%d.%d shared_per_block=%zuB "
        "thread_blocks_per_sm=%d thread_occupancy=%.3f "
        "warp_blocks_per_sm=%d warp_occupancy=%.3f "
        "rate_jac_lane0=yes production_ready=no status=%s\n",
        property.name, property.major, property.minor,
        kWarpsPerBlock * sizeof(WarpWorkspace),
        thread_blocks, thread_occupancy,
        warp_blocks, warp_occupancy, pass ? "PASS" : "FAIL");
    return pass ? EXIT_SUCCESS : EXIT_FAILURE;
}
