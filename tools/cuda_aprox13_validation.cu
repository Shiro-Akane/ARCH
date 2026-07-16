/**
 * @file cuda_aprox13_validation.cu
 * @brief CPU/GPU parity smoke test for the current translated Timmes aprox13.
 *
 * This test intentionally exercises only the current NetAprox13 implementation.
 * It does not include the legacy pynucastro CUDA mirror, the standalone
 * arch_gpu experiment, an NSE shortcut, or a high-temperature freeze path.
 *
 * A fixed positive heat capacity is used on both sides to isolate the network
 * and the ODE Jacobian assembly from a future device Helmholtz-EOS port.  The
 * resulting full system is the production Timmes convention:
 *
 *   dT/dt       = enuc / cv
 *   J(T, X_j)   = d(enuc)/dX_j / cv
 *   J(T, T)     = d(enuc)/dT / cv
 *
 * cv derivatives are deliberately absent, matching Solver_BE_NR.
 */

#include <cuda_runtime.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "physics/network/aprox13/NetAprox13.h"

namespace {

constexpr int kNumSpec = NetAprox13::NUM_SPECIES;
constexpr int kNumEq = NetAprox13::ODE_NEQ;
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
};

// Populated at startup from the current NetAprox13::ENERGY_WEIGHTS.  This
// avoids a second hard-coded copy of nuclear masses in the CUDA test.
__constant__ double device_energy_weights[kNumSpec];

struct HostMatrix
{
    double data[kNumEq][kNumEq]{};

    void set(int row, int column, double value) noexcept
    {
        data[row - 1][column - 1] = value;
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
            const double value = matrix.data[i][j];
            const int index = i * kNumEq + j;
            output.jacobian[index] = value;
            output.lhs[index] = (i == j ? 1.0 : 0.0) - input.dt * value;
        }
    }
}

__device__ void evaluate_gpu(const TestInput& input, TestOutput& output)
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
    if (error == cudaSuccess) {
        return true;
    }
    std::fprintf(stderr, "%s: %s\n", operation, cudaGetErrorString(error));
    return false;
}

bool all_finite(const double* values, int count)
{
    for (int i = 0; i < count; ++i) {
        if (!std::isfinite(values[i])) {
            return false;
        }
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
                                    const double* candidate, int dimension,
                                    int column)
{
    double reference_column[kNumEq]{};
    double candidate_column[kNumEq]{};
    for (int row = 0; row < dimension; ++row) {
        reference_column[row] = reference[row * dimension + column];
        candidate_column[row] = candidate[row * dimension + column];
    }
    return scaled_max_relative_error(
        reference_column, candidate_column, dimension);
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
            device_energy_weights, NetAprox13::ENERGY_WEIGHTS.data(),
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
    if (!success) {
        return EXIT_FAILURE;
    }

    double rhs_error = 0.0;
    double jacobian_error = 0.0;
    double lhs_error = 0.0;
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
            cpu_outputs[i].lhs, gpu_outputs[i].lhs,
            kNumEq * kNumEq));
        temperature_column_error = std::max(
            temperature_column_error,
            scaled_column_relative_error(
                cpu_outputs[i].jacobian, gpu_outputs[i].jacobian,
                kNumEq, kNumSpec));
        temperature_row_error = std::max(
            temperature_row_error,
            scaled_max_relative_error(
                cpu_outputs[i].jacobian + kNumSpec * kNumEq,
                gpu_outputs[i].jacobian + kNumSpec * kNumEq,
                kNumEq));
    }

    const bool pass = finite
                   && rhs_error <= kTolerance
                   && jacobian_error <= kTolerance
                   && lhs_error <= kTolerance
                   && temperature_column_error <= kTolerance
                   && temperature_row_error <= kTolerance;
    std::printf(
        "aprox13 current-network CPU/GPU states=%d "
        "rhs_rel=%.12e jac_rel=%.12e lhs_rel=%.12e "
        "temp_col_rel=%.12e temp_row_rel=%.12e finite=%s status=%s\n",
        kNumCases, rhs_error, jacobian_error, lhs_error,
        temperature_column_error, temperature_row_error,
        finite ? "yes" : "no", pass ? "PASS" : "FAIL");
    return pass ? EXIT_SUCCESS : EXIT_FAILURE;
}
