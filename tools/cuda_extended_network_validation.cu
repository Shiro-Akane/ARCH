/**
 * @file cuda_extended_network_validation.cu
 * @brief CPU/GPU parity test for current translated Timmes aprox19/aprox21.
 *
 * Compile once with TIMMES_VALIDATE_APROX19 and once with
 * TIMMES_VALIDATE_APROX21.  This test uses the current C++ translation only:
 * no pynucastro mirror, finite-difference temperature column, NSE shortcut,
 * or high-temperature freeze/bypass is present.
 *
 * The composition block uses a generated explicit fixed-rate Jacobian.  The
 * four columns that enter Timmes' abundance-dependent equilibrium closures
 * are overwritten by bounded Dual<1> closure derivatives, while the screened
 * base rates stay frozen exactly as in dfdy_isotopes.  The temperature column
 * is analytic and uses RateTemperatureAccessor, including rate and screening
 * derivatives.
 * A fixed positive cv on both CPU and GPU isolates network/ODE Jacobian
 * assembly from the future device Helmholtz implementation.
 */

#include <cuda_runtime.h>

#include <algorithm>
#include <array>
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

constexpr int kNumSpec = Network::NUM_SPECIES;
constexpr int kNumEq = Network::ODE_NEQ;
constexpr int kNumCases = 6;
constexpr double kTolerance = 1.0e-12;

struct TestInput
{
    double density = 0.0;
    double temperature = 0.0;
    double cv = 0.0;
    double dt = 0.0;
    double mass_fractions[kNumSpec]{};
};

struct TestOutput
{
    double rhs[kNumEq]{};
    double jacobian[kNumEq * kNumEq]{};
    double lhs[kNumEq * kNumEq]{};
    // Global scratch keeps the molar matrix out of the per-thread CUDA stack,
    // matching the intended batched workspace/warp-cooperative ODE layout.
    double molar_jacobian[kNumSpec * kNumSpec]{};
};

__constant__ double device_energy_weights[kNumSpec];

struct HostMatrix
{
    double data[kNumEq * kNumEq]{};

    void set(int row, int column, double value) noexcept
    {
        data[(row - 1) * kNumEq + column - 1] = value;
    }
};

void evaluate_cpu(const TestInput& input, TestOutput& output)
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

    Network::eval_rhs(state, input.density, output.rhs, enuc);
    Network::eval_jacobian(state, input.density, matrix, denuc_dX);
    Network::eval_temperature_derivative(
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
            const double value = matrix.data[index];
            output.jacobian[index] = value;
            output.lhs[index] = (i == j ? 1.0 : 0.0) - input.dt * value;
        }
    }
}

__device__ void evaluate_gpu(const TestInput& input, TestOutput& output)
{
    double molar[kNumSpec];
    double molar_rhs[kNumSpec];
    for (int i = 0; i < kNumSpec; ++i) {
        molar[i] = fmax(1.0e-30,
                       fmin(1.0, input.mass_fractions[i]
                                     / Network::aion(i)));
    }
    Network::molar_rhs_jacobian_frozen_screening(
        molar, input.density, input.temperature,
        molar_rhs, output.molar_jacobian);

    double enuc = 0.0;
    double denuc_dX[kNumSpec]{};
    for (int i = 0; i < kNumSpec; ++i) {
        output.rhs[i] = molar_rhs[i] * Network::aion(i);
        enuc += molar_rhs[i] * device_energy_weights[i];
        for (int j = 0; j < kNumSpec; ++j) {
            const double derivative =
                output.molar_jacobian[i * kNumSpec + j];
            output.jacobian[i * kNumEq + j] =
                derivative * Network::aion(i) / Network::aion(j);
            denuc_dX[j] += derivative * device_energy_weights[i]
                         / Network::aion(j);
        }
    }
    enuc *= Network::ENERGY_CONVERSION;
    output.rhs[kNumSpec] = enuc / input.cv;
    for (int column = 0; column < kNumSpec; ++column) {
        output.jacobian[kNumSpec * kNumEq + column] =
            denuc_dX[column] * Network::ENERGY_CONVERSION / input.cv;
    }

    // Analytic Timmes temperature column, including rate/screening d/dT.
    using TemperatureAD = timmes::Dual<1>;
    TemperatureAD molar_temperature_ad[kNumSpec];
    TemperatureAD molar_rhs_temperature_ad[kNumSpec];
    for (int i = 0; i < kNumSpec; ++i) {
        molar_temperature_ad[i] = TemperatureAD(molar[i]);
    }
    const TemperatureAD temperature =
        TemperatureAD::variable(input.temperature, 0);
    Network::template molar_rhs_impl<TemperatureAD,
                                     timmes::RateTemperatureAccessor>(
        molar_temperature_ad, input.density, input.temperature,
        temperature, molar_rhs_temperature_ad);

    double denuc_dT = 0.0;
    for (int row = 0; row < kNumSpec; ++row) {
        const double derivative = molar_rhs_temperature_ad[row].deriv[0];
        output.jacobian[row * kNumEq + kNumSpec] =
            derivative * Network::aion(row);
        denuc_dT += derivative * device_energy_weights[row];
    }
    output.jacobian[kNumSpec * kNumEq + kNumSpec] =
        denuc_dT * Network::ENERGY_CONVERSION / input.cv;

    for (int i = 0; i < kNumEq; ++i) {
        for (int j = 0; j < kNumEq; ++j) {
            const int index = i * kNumEq + j;
            output.lhs[index] = (i == j ? 1.0 : 0.0)
                              - input.dt * output.jacobian[index];
        }
    }
}

__global__ void evaluate_kernel(const TestInput* inputs, TestOutput* outputs,
                                int count)
{
    const int index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index < count) {
        evaluate_gpu(inputs[index], outputs[index]);
    }
}

bool cuda_ok(cudaError_t error, const char* operation)
{
    if (error == cudaSuccess) return true;
    std::fprintf(stderr, "%s: %s\n", operation, cudaGetErrorString(error));
    return false;
}

bool all_finite(const double* values, int count)
{
    for (int i = 0; i < count; ++i) {
        if (!std::isfinite(values[i])) return false;
    }
    return true;
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

std::array<TestInput, kNumCases> make_inputs()
{
    constexpr double temperatures[kNumCases]{
        1.0e9, 1.0e9, 2.0e9, 2.0e9, 5.0e9, 5.0e9};
    constexpr double densities[kNumCases]{
        1.0e6, 1.0e8, 1.0e6, 1.0e8, 1.0e6, 1.0e8};

    std::array<TestInput, kNumCases> inputs{};
    for (int c = 0; c < kNumCases; ++c) {
        inputs[c].density = densities[c];
        inputs[c].temperature = temperatures[c];
        inputs[c].cv = 1.0e8 + c * 1.0e7;
        inputs[c].dt = 1.0e-12;

        double normalization = 0.0;
        for (int i = 0; i < kNumSpec; ++i) {
            const double value = 1.0 + 0.05 * ((i + 3 * c) % 11);
            inputs[c].mass_fractions[i] = value;
            normalization += value;
        }
        for (double& value : inputs[c].mass_fractions) {
            value /= normalization;
        }
    }
    return inputs;
}

} // namespace

int main()
{
    const auto inputs = make_inputs();
    std::array<TestOutput, kNumCases> cpu_outputs{};
    std::array<TestOutput, kNumCases> gpu_outputs{};
    for (int i = 0; i < kNumCases; ++i) {
        evaluate_cpu(inputs[i], cpu_outputs[i]);
    }

    if (!cuda_ok(cudaMemcpyToSymbol(
            device_energy_weights, Network::ENERGY_WEIGHTS.data(),
            sizeof(device_energy_weights)),
            "cudaMemcpyToSymbol(device_energy_weights)")) {
        return EXIT_FAILURE;
    }

    TestInput* device_inputs = nullptr;
    TestOutput* device_outputs = nullptr;
    if (!cuda_ok(cudaMalloc(&device_inputs, sizeof(inputs)),
                 "cudaMalloc(inputs)")
        || !cuda_ok(cudaMalloc(&device_outputs, sizeof(gpu_outputs)),
                    "cudaMalloc(outputs)")) {
        cudaFree(device_inputs);
        cudaFree(device_outputs);
        return EXIT_FAILURE;
    }

    bool success = cuda_ok(cudaMemcpy(
        device_inputs, inputs.data(), sizeof(inputs), cudaMemcpyHostToDevice),
        "cudaMemcpy(inputs)");
    if (success) {
        evaluate_kernel<<<1, 32>>>(device_inputs, device_outputs, kNumCases);
        success = cuda_ok(cudaGetLastError(), "evaluate_kernel launch")
               && cuda_ok(cudaDeviceSynchronize(),
                          "evaluate_kernel synchronize")
               && cuda_ok(cudaMemcpy(
                      gpu_outputs.data(), device_outputs, sizeof(gpu_outputs),
                      cudaMemcpyDeviceToHost),
                      "cudaMemcpy(outputs)");
    }

    cudaFree(device_inputs);
    cudaFree(device_outputs);
    if (!success) return EXIT_FAILURE;

    double rhs_error = 0.0;
    double jacobian_error = 0.0;
    double lhs_error = 0.0;
    double composition_column_error = 0.0;
    int worst_composition_case = -1;
    int worst_composition_column = -1;
    double temperature_column_error = 0.0;
    double temperature_row_error = 0.0;
    bool finite = true;
    for (int i = 0; i < kNumCases; ++i) {
        finite = finite
              && all_finite(cpu_outputs[i].rhs, kNumEq)
              && all_finite(cpu_outputs[i].jacobian, kNumEq * kNumEq)
              && all_finite(cpu_outputs[i].lhs, kNumEq * kNumEq)
              && all_finite(gpu_outputs[i].rhs, kNumEq)
              && all_finite(gpu_outputs[i].jacobian, kNumEq * kNumEq)
              && all_finite(gpu_outputs[i].lhs, kNumEq * kNumEq);
        rhs_error = std::max(rhs_error, scaled_max_relative_error(
            cpu_outputs[i].rhs, gpu_outputs[i].rhs, kNumEq));
        jacobian_error = std::max(jacobian_error,
            scaled_max_relative_error(
                cpu_outputs[i].jacobian, gpu_outputs[i].jacobian,
                kNumEq * kNumEq));
        lhs_error = std::max(lhs_error, scaled_max_relative_error(
            cpu_outputs[i].lhs, gpu_outputs[i].lhs, kNumEq * kNumEq));
        for (int column = 0; column < kNumSpec; ++column) {
            const double error = scaled_column_relative_error(
                cpu_outputs[i].jacobian, gpu_outputs[i].jacobian,
                column);
            if (error > composition_column_error) {
                composition_column_error = error;
                worst_composition_case = i;
                worst_composition_column = column;
            }
        }
        temperature_column_error = std::max(
            temperature_column_error,
            scaled_column_relative_error(
                cpu_outputs[i].jacobian, gpu_outputs[i].jacobian,
                kNumSpec));
        temperature_row_error = std::max(
            temperature_row_error,
            scaled_max_relative_error(
                cpu_outputs[i].jacobian + kNumSpec * kNumEq,
                gpu_outputs[i].jacobian + kNumSpec * kNumEq,
                kNumEq));
    }

    // Acceptance uses the same whole-system scaled max norm as the aprox13
    // validator.  The per-composition-column value remains diagnostic: a
    // nearly null/cancelling column can have a large relative ratio while its
    // absolute discrepancy is many orders below the full Jacobian scale.
    const bool pass = finite
                   && rhs_error <= kTolerance
                   && jacobian_error <= kTolerance
                   && lhs_error <= kTolerance
                   && temperature_column_error <= kTolerance
                   && temperature_row_error <= kTolerance;
    std::printf(
        "%s current-network CPU/GPU states=%d "
        "rhs_rel=%.12e jac_rel=%.12e lhs_rel=%.12e "
        "comp_col_rel_diag=%.12e temp_col_rel=%.12e temp_row_rel=%.12e "
        "finite=%s status=%s\n",
        Network::NETWORK_NAME, kNumCases, rhs_error, jacobian_error,
        lhs_error, composition_column_error, temperature_column_error,
        temperature_row_error,
        finite ? "yes" : "no", pass ? "PASS" : "FAIL");

    if (worst_composition_case >= 0) {
        double max_difference = 0.0;
        double max_reference = 0.0;
        for (int row = 0; row < kNumEq; ++row) {
            const int index = row * kNumEq + worst_composition_column;
            max_difference = std::max(
                max_difference,
                std::abs(cpu_outputs[worst_composition_case].jacobian[index]
                       - gpu_outputs[worst_composition_case].jacobian[index]));
            max_reference = std::max(
                max_reference,
                std::abs(cpu_outputs[worst_composition_case].jacobian[index]));
        }
        std::printf(
            "%s diagnostic worst_comp_case=%d column=%d "
            "column_max_abs_diff=%.12e column_max_abs_ref=%.12e\n",
            Network::NETWORK_NAME, worst_composition_case,
            worst_composition_column, max_difference, max_reference);
    }
    return pass ? EXIT_SUCCESS : EXIT_FAILURE;
}
