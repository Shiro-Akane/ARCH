/**
 * @file cuda_aprox13_be_validation.cu
 * @brief Bounded CPU/GPU validation of one-thread-per-cell BE/Newton/dense-LU.
 *
 * This is deliberately an ODE/LU validation layer, not a production burner:
 * it uses the current translated Timmes NetAprox13 with its analytic isotope
 * Jacobian and analytic temperature column, but a fixed positive cv and
 * e=cv*T to isolate the integrator from the future device Helmholtz EOS.
 * No legacy pynucastro network, finite-difference temperature derivative,
 * NSE shortcut, or high-temperature composition freeze is present.
 */

#include <cuda_runtime.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "numerics/linalg/DenseWrap.h"
#include "physics/network/aprox13/NetAprox13.h"

namespace {

constexpr int kNumSpec = NetAprox13::NUM_SPECIES;
constexpr int kNumEq = NetAprox13::ODE_NEQ;
constexpr int kMaxEq = BurnLimits::MAX_ODE_NEQ;
constexpr int kNumCases = 8;
constexpr int kMaxNewton = 24;
constexpr double kRtol = 1.0e-11;
constexpr double kAtol = 1.0e-18;
constexpr double kSmallX = 1.0e-30;
constexpr double kSmallT = 1.0e6;
constexpr double kMaxT = 1.0e11;
constexpr double kParityTolerance = 1.0e-10;
// The production BE-NR guard is 5e-2.  This isolated fixed-cv validation uses
// a 500x tighter guard while retaining the production mass projection.
constexpr double kEnergyClosureTolerance = 1.0e-4;

enum Failure : int {
    kSuccess = 0,
    kInvalidInput = 1 << 0,
    kSingular = 1 << 1,
    kNonFinite = 1 << 2,
    kInadmissible = 1 << 3,
    kMaxIterations = 1 << 4,
    kEnergyClosure = 1 << 5
};

struct TestInput {
    double density = 0.0;
    double temperature = 0.0;
    double cv = 0.0;
    double dt_total = 0.0;
    int substeps = 0;
    int expected_failure = 0;
    double mass_fractions[kNumSpec]{};
};

struct TestOutput {
    double state[kNumEq]{};
    double nuclear_energy = 0.0;
    double thermal_energy = 0.0;
    double energy_closure_rel = 0.0;
    double max_newton_residual = 0.0;
    double max_mass_error = 0.0;
    double stiffness_proxy = 0.0;
    int total_newton_iterations = 0;
    int completed_substeps = 0;
    int failure = kSuccess;
};

__constant__ double device_energy_weights[kNumSpec];

struct HostJacobian {
    double data[kNumEq][kNumEq]{};
    void set(int row, int column, double value) noexcept
    {
        data[row - 1][column - 1] = value;
    }
};

bool finite_vector_host(const double* values, int count)
{
    for (int i = 0; i < count; ++i) {
        if (!std::isfinite(values[i])) return false;
    }
    return true;
}

void evaluate_cpu(const double* state, double density, double cv,
                  double* rhs, double* jacobian)
{
    HostJacobian matrix;
    double enuc = 0.0;
    double denuc_dX[kNumSpec]{};
    double drhs_dT[kNumSpec]{};
    double denuc_dT = 0.0;

    NetAprox13::eval_rhs(state, density, rhs, enuc);
    NetAprox13::eval_jacobian(state, density, matrix, denuc_dX);
    NetAprox13::eval_temperature_derivative(
        state, density, drhs_dT, denuc_dT);

    rhs[kNumSpec] = enuc / cv;
    for (int i = 0; i < kNumSpec; ++i) {
        matrix.set(i + 1, kNumEq, drhs_dT[i]);
        matrix.set(kNumEq, i + 1, denuc_dX[i] / cv);
    }
    matrix.set(kNumEq, kNumEq, denuc_dT / cv);

    for (int i = 0; i < kNumEq; ++i) {
        for (int j = 0; j < kNumEq; ++j) {
            jacobian[i * kNumEq + j] = matrix.data[i][j];
        }
    }
}

__device__ bool finite_vector_device(const double* values, int count)
{
    for (int i = 0; i < count; ++i) {
        if (!isfinite(values[i])) return false;
    }
    return true;
}

__device__ void evaluate_gpu(const double* state, double density, double cv,
                             double* rhs, double* jacobian)
{
    double molar[kNumSpec];
    double molar_rhs[kNumSpec];
    double molar_jacobian[kNumSpec * kNumSpec];
    for (int i = 0; i < kNumSpec; ++i) {
        molar[i] = fmax(kSmallX,
                        fmin(1.0, state[i] / NetAprox13::aion(i)));
    }

    NetAprox13::molar_rhs_jacobian_frozen_screening(
        molar, density, state[kNumSpec], molar_rhs, molar_jacobian);

    double enuc = 0.0;
    double denuc_dX[kNumSpec]{};
    for (int i = 0; i < kNumSpec; ++i) {
        rhs[i] = molar_rhs[i] * NetAprox13::aion(i);
        enuc += molar_rhs[i] * device_energy_weights[i];
        for (int j = 0; j < kNumSpec; ++j) {
            const double derivative = molar_jacobian[i * kNumSpec + j];
            jacobian[i * kNumEq + j] =
                derivative * NetAprox13::aion(i) / NetAprox13::aion(j);
            denuc_dX[j] += derivative * device_energy_weights[i]
                         / NetAprox13::aion(j);
        }
    }
    enuc *= NetAprox13::ENERGY_CONVERSION;
    for (int j = 0; j < kNumSpec; ++j) {
        denuc_dX[j] *= NetAprox13::ENERGY_CONVERSION;
    }

    using TemperatureAD = timmes::Dual<1>;
    TemperatureAD molar_ad[kNumSpec];
    TemperatureAD molar_rhs_ad[kNumSpec];
    for (int i = 0; i < kNumSpec; ++i) molar_ad[i] = TemperatureAD(molar[i]);
    const TemperatureAD temperature =
        TemperatureAD::variable(state[kNumSpec], 0);
    NetAprox13::molar_rhs_impl<TemperatureAD,
                               timmes::RateTemperatureAccessor>(
        molar_ad, density, state[kNumSpec], temperature, molar_rhs_ad);

    double denuc_dT = 0.0;
    for (int i = 0; i < kNumSpec; ++i) {
        const double derivative = molar_rhs_ad[i].deriv[0];
        jacobian[i * kNumEq + kNumSpec] =
            derivative * NetAprox13::aion(i);
        denuc_dT += derivative * device_energy_weights[i];
    }
    denuc_dT *= NetAprox13::ENERGY_CONVERSION;

    rhs[kNumSpec] = enuc / cv;
    for (int j = 0; j < kNumSpec; ++j) {
        jacobian[kNumSpec * kNumEq + j] = denuc_dX[j] / cv;
    }
    jacobian[kNumSpec * kNumEq + kNumSpec] = denuc_dT / cv;
}

__device__ bool dense_lu_gpu(double* matrix, double* rhs)
{
    int permutation[kNumEq];
    for (int i = 0; i < kNumEq; ++i) permutation[i] = i;

    // Same logical-row partial-pivot algorithm and 1e-20 singular threshold
    // as the current DenseLUSolver.
    for (int i = 0; i < kNumEq; ++i) {
        double max_value = 0.0;
        int pivot_row = i;
        for (int j = i; j < kNumEq; ++j) {
            const double value = fabs(matrix[permutation[j] * kNumEq + i]);
            if (value > max_value) {
                max_value = value;
                pivot_row = j;
            }
        }
        if (max_value < 1.0e-20) return false;
        const int temporary = permutation[i];
        permutation[i] = permutation[pivot_row];
        permutation[pivot_row] = temporary;

        const double pivot_inverse =
            1.0 / matrix[permutation[i] * kNumEq + i];
        for (int j = i + 1; j < kNumEq; ++j) {
            matrix[permutation[j] * kNumEq + i] *= pivot_inverse;
            for (int k = i + 1; k < kNumEq; ++k) {
                matrix[permutation[j] * kNumEq + k] -=
                    matrix[permutation[j] * kNumEq + i]
                  * matrix[permutation[i] * kNumEq + k];
            }
        }
    }

    double forward[kNumEq];
    for (int i = 0; i < kNumEq; ++i) {
        forward[i] = rhs[permutation[i]];
        for (int j = 0; j < i; ++j) {
            forward[i] -= matrix[permutation[i] * kNumEq + j] * forward[j];
        }
    }
    for (int i = kNumEq - 1; i >= 0; --i) {
        rhs[i] = forward[i];
        for (int j = i + 1; j < kNumEq; ++j) {
            rhs[i] -= matrix[permutation[i] * kNumEq + j] * rhs[j];
        }
        rhs[i] /= matrix[permutation[i] * kNumEq + i];
    }
    return true;
}

double wrms_host(const double* values, const double* state)
{
    double sum = 0.0;
    for (int i = 0; i < kNumEq; ++i) {
        const double weight = kRtol * std::abs(state[i]) + kAtol;
        const double scaled = values[i] / weight;
        sum += scaled * scaled;
    }
    return std::sqrt(sum / kNumEq);
}

__device__ double wrms_device(const double* values, const double* state)
{
    double sum = 0.0;
    for (int i = 0; i < kNumEq; ++i) {
        const double weight = kRtol * fabs(state[i]) + kAtol;
        const double scaled = values[i] / weight;
        sum += scaled * scaled;
    }
    return sqrt(sum / kNumEq);
}

double max_relative_be_residual_host(const double* residual,
                                     const double* old_state,
                                     const double* new_state)
{
    double result = 0.0;
    for (int i = 0; i < kNumEq; ++i) {
        const double scale = std::max(
            {std::abs(old_state[i]), std::abs(new_state[i]), 1.0});
        result = std::max(result, std::abs(residual[i]) / scale);
    }
    return result;
}

__device__ double max_relative_be_residual_device(
    const double* residual, const double* old_state, const double* new_state)
{
    double result = 0.0;
    for (int i = 0; i < kNumEq; ++i) {
        const double scale = fmax(
            fmax(fabs(old_state[i]), fabs(new_state[i])), 1.0);
        result = fmax(result, fabs(residual[i]) / scale);
    }
    return result;
}

void finalize_diagnostics_cpu(const TestInput& input, const double* initial,
                              TestOutput& output)
{
    long double nuclear = 0.0L;
    for (int i = 0; i < kNumSpec; ++i) {
        nuclear += static_cast<long double>(output.state[i] - initial[i])
                 / NetAprox13::AION[i] * NetAprox13::ENERGY_WEIGHTS[i];
    }
    output.nuclear_energy = NetAprox13::ENERGY_CONVERSION
                          * static_cast<double>(nuclear);
    output.thermal_energy = input.cv
                          * (output.state[kNumSpec] - initial[kNumSpec]);
    const double scale = std::max(
        {std::abs(output.nuclear_energy), std::abs(output.thermal_energy), 1.0});
    output.energy_closure_rel =
        std::abs(output.thermal_energy - output.nuclear_energy) / scale;
}

__device__ void finalize_diagnostics_gpu(const TestInput& input,
                                         const double* initial,
                                         TestOutput& output)
{
    double nuclear = 0.0;
    for (int i = 0; i < kNumSpec; ++i) {
        nuclear += (output.state[i] - initial[i])
                 / NetAprox13::aion(i) * device_energy_weights[i];
    }
    output.nuclear_energy = NetAprox13::ENERGY_CONVERSION * nuclear;
    output.thermal_energy = input.cv
                          * (output.state[kNumSpec] - initial[kNumSpec]);
    const double scale = fmax(fmax(fabs(output.nuclear_energy),
                                   fabs(output.thermal_energy)), 1.0);
    output.energy_closure_rel =
        fabs(output.thermal_energy - output.nuclear_energy) / scale;
}

void integrate_cpu(const TestInput& input, TestOutput& output)
{
    double initial[kNumEq];
    for (int i = 0; i < kNumSpec; ++i) {
        initial[i] = input.mass_fractions[i];
        output.state[i] = input.mass_fractions[i];
    }
    initial[kNumSpec] = input.temperature;
    output.state[kNumSpec] = input.temperature;

    if (!(input.density > 0.0) || !(input.cv > 0.0)
        || !(input.dt_total > 0.0) || input.substeps <= 0
        || !finite_vector_host(initial, kNumEq)) {
        output.failure = kInvalidInput;
        return;
    }

    const double step = input.dt_total / input.substeps;
    for (int substep = 0; substep < input.substeps; ++substep) {
        double old[kNumEq];
        for (int i = 0; i < kNumEq; ++i) old[i] = output.state[i];
        bool converged = false;

        for (int iteration = 0; iteration < kMaxNewton; ++iteration) {
            double rhs[kNumEq]{};
            double jacobian[kNumEq * kNumEq]{};
            evaluate_cpu(output.state, input.density, input.cv, rhs, jacobian);
            if (!finite_vector_host(rhs, kNumEq)
                || !finite_vector_host(jacobian, kNumEq * kNumEq)) {
                output.failure = kNonFinite;
                return;
            }

            DenseMatrixData matrix;
            matrix.zero();
            double correction[kMaxEq]{};
            for (int i = 0; i < kNumEq; ++i) {
                correction[i] = old[i] - output.state[i] + step * rhs[i];
                for (int j = 0; j < kNumEq; ++j) {
                    const double value = (i == j ? 1.0 : 0.0)
                                       - step * jacobian[i * kNumEq + j];
                    matrix.data[i][j] = value;
                    output.stiffness_proxy = std::max(
                        output.stiffness_proxy,
                        std::abs(step * jacobian[i * kNumEq + j]));
                }
            }
            if (!DenseLUSolver::solve<kNumEq, kMaxEq>(matrix, correction)) {
                output.failure = kSingular;
                return;
            }
            if (!finite_vector_host(correction, kNumEq)) {
                output.failure = kNonFinite;
                return;
            }

            double trial[kNumEq];
            double mass_sum = 0.0;
            bool admissible = true;
            for (int i = 0; i < kNumSpec; ++i) {
                trial[i] = output.state[i] + correction[i];
                admissible = admissible && std::isfinite(trial[i])
                           && trial[i] >= -10.0 * kAtol
                           && trial[i] <= 1.0 + 10.0 * kAtol;
                mass_sum += trial[i];
            }
            trial[kNumSpec] = output.state[kNumSpec] + correction[kNumSpec];
            admissible = admissible && std::isfinite(trial[kNumSpec])
                       && trial[kNumSpec] >= kSmallT
                       && trial[kNumSpec] <= kMaxT
                       && std::isfinite(mass_sum) && mass_sum > 0.0
                       && std::abs(mass_sum - 1.0) <= 100.0 * kRtol;
            if (!admissible) {
                output.failure = kInadmissible;
                return;
            }

            const double update_norm = wrms_host(correction, output.state);
            for (int i = 0; i < kNumEq; ++i) output.state[i] = trial[i];
            ++output.total_newton_iterations;
            if (update_norm < 1.0) {
                double projected_sum = 0.0;
                for (int i = 0; i < kNumSpec; ++i) {
                    output.state[i] = std::max(output.state[i], kSmallX);
                    projected_sum += output.state[i];
                }
                for (int i = 0; i < kNumSpec; ++i) {
                    output.state[i] /= projected_sum;
                }
                converged = true;
                break;
            }
        }
        if (!converged) {
            output.failure = kMaxIterations;
            return;
        }

        double rhs[kNumEq]{};
        double jacobian[kNumEq * kNumEq]{};
        double residual[kNumEq]{};
        evaluate_cpu(output.state, input.density, input.cv, rhs, jacobian);
        for (int i = 0; i < kNumEq; ++i) {
            residual[i] = output.state[i] - old[i] - step * rhs[i];
        }
        output.max_newton_residual = std::max(
            output.max_newton_residual,
            max_relative_be_residual_host(residual, old, output.state));
        double mass_sum = 0.0;
        for (int i = 0; i < kNumSpec; ++i) mass_sum += output.state[i];
        output.max_mass_error = std::max(
            output.max_mass_error, std::abs(mass_sum - 1.0));
        ++output.completed_substeps;
    }

    finalize_diagnostics_cpu(input, initial, output);
    if (!std::isfinite(output.energy_closure_rel)) {
        output.failure = kNonFinite;
    } else if (output.energy_closure_rel > kEnergyClosureTolerance) {
        output.failure = kEnergyClosure;
    }
}

__device__ void integrate_gpu(const TestInput& input, TestOutput& output)
{
    double initial[kNumEq];
    for (int i = 0; i < kNumSpec; ++i) {
        initial[i] = input.mass_fractions[i];
        output.state[i] = input.mass_fractions[i];
    }
    initial[kNumSpec] = input.temperature;
    output.state[kNumSpec] = input.temperature;

    if (!(input.density > 0.0) || !(input.cv > 0.0)
        || !(input.dt_total > 0.0) || input.substeps <= 0
        || !finite_vector_device(initial, kNumEq)) {
        output.failure = kInvalidInput;
        return;
    }

    const double step = input.dt_total / input.substeps;
    for (int substep = 0; substep < input.substeps; ++substep) {
        double old[kNumEq];
        for (int i = 0; i < kNumEq; ++i) old[i] = output.state[i];
        bool converged = false;

        for (int iteration = 0; iteration < kMaxNewton; ++iteration) {
            double rhs[kNumEq]{};
            double matrix[kNumEq * kNumEq]{};
            evaluate_gpu(output.state, input.density, input.cv, rhs, matrix);
            if (!finite_vector_device(rhs, kNumEq)
                || !finite_vector_device(matrix, kNumEq * kNumEq)) {
                output.failure = kNonFinite;
                return;
            }

            double correction[kNumEq]{};
            for (int i = 0; i < kNumEq; ++i) {
                correction[i] = old[i] - output.state[i] + step * rhs[i];
                for (int j = 0; j < kNumEq; ++j) {
                    const int index = i * kNumEq + j;
                    output.stiffness_proxy = fmax(
                        output.stiffness_proxy, fabs(step * matrix[index]));
                    matrix[index] = (i == j ? 1.0 : 0.0)
                                  - step * matrix[index];
                }
            }
            if (!dense_lu_gpu(matrix, correction)) {
                output.failure = kSingular;
                return;
            }
            if (!finite_vector_device(correction, kNumEq)) {
                output.failure = kNonFinite;
                return;
            }

            double trial[kNumEq];
            double mass_sum = 0.0;
            bool admissible = true;
            for (int i = 0; i < kNumSpec; ++i) {
                trial[i] = output.state[i] + correction[i];
                admissible = admissible && isfinite(trial[i])
                           && trial[i] >= -10.0 * kAtol
                           && trial[i] <= 1.0 + 10.0 * kAtol;
                mass_sum += trial[i];
            }
            trial[kNumSpec] = output.state[kNumSpec] + correction[kNumSpec];
            admissible = admissible && isfinite(trial[kNumSpec])
                       && trial[kNumSpec] >= kSmallT
                       && trial[kNumSpec] <= kMaxT
                       && isfinite(mass_sum) && mass_sum > 0.0
                       && fabs(mass_sum - 1.0) <= 100.0 * kRtol;
            if (!admissible) {
                output.failure = kInadmissible;
                return;
            }

            const double update_norm = wrms_device(correction, output.state);
            for (int i = 0; i < kNumEq; ++i) output.state[i] = trial[i];
            ++output.total_newton_iterations;
            if (update_norm < 1.0) {
                double projected_sum = 0.0;
                for (int i = 0; i < kNumSpec; ++i) {
                    output.state[i] = fmax(output.state[i], kSmallX);
                    projected_sum += output.state[i];
                }
                for (int i = 0; i < kNumSpec; ++i) {
                    output.state[i] /= projected_sum;
                }
                converged = true;
                break;
            }
        }
        if (!converged) {
            output.failure = kMaxIterations;
            return;
        }

        double rhs[kNumEq]{};
        double jacobian[kNumEq * kNumEq]{};
        double residual[kNumEq]{};
        evaluate_gpu(output.state, input.density, input.cv, rhs, jacobian);
        for (int i = 0; i < kNumEq; ++i) {
            residual[i] = output.state[i] - old[i] - step * rhs[i];
        }
        output.max_newton_residual = fmax(
            output.max_newton_residual,
            max_relative_be_residual_device(residual, old, output.state));
        double mass_sum = 0.0;
        for (int i = 0; i < kNumSpec; ++i) mass_sum += output.state[i];
        output.max_mass_error = fmax(
            output.max_mass_error, fabs(mass_sum - 1.0));
        ++output.completed_substeps;
    }

    finalize_diagnostics_gpu(input, initial, output);
    if (!isfinite(output.energy_closure_rel)) {
        output.failure = kNonFinite;
    } else if (output.energy_closure_rel > kEnergyClosureTolerance) {
        output.failure = kEnergyClosure;
    }
}

__global__ void integrate_kernel(const TestInput* inputs, TestOutput* outputs,
                                 int count)
{
    const int index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index < count) integrate_gpu(inputs[index], outputs[index]);
}

std::array<TestInput, kNumCases> make_inputs()
{
    std::array<TestInput, kNumCases> inputs{};
    constexpr double temperatures[kNumCases]{
        8.0e8, 1.0e9, 1.5e9, 2.0e9,
        2.5e9, 3.0e9, 5.0e9, 2.0e9};
    constexpr double densities[kNumCases]{
        1.0e6, 1.0e7, 1.0e7, 1.0e8,
        1.0e8, 1.0e8, 1.0e8, 1.0e8};
    constexpr double durations[kNumCases]{
        1.0e-8, 1.0e-9, 1.0e-10, 1.0e-12,
        1.0e-13, 1.0e-14, 1.0e-16, 1.0e-12};
    constexpr int substeps[kNumCases]{1, 1, 4, 4, 8, 16, 32, 4};

    for (int c = 0; c < kNumCases; ++c) {
        inputs[c].density = densities[c];
        inputs[c].temperature = temperatures[c];
        inputs[c].cv = 1.0e8 + 0.5e7 * c;
        inputs[c].dt_total = durations[c];
        inputs[c].substeps = substeps[c];

        double sum = 0.0;
        for (int i = 0; i < kNumSpec; ++i) {
            double value = 1.0e-12;
            if (i == 0) value = 0.05 + 0.01 * (c % 3);
            if (i == 1) value = 0.45 - 0.02 * (c % 2);
            if (i == 2) value = 0.45 + 0.01 * (c % 2);
            if (i == 5) value = 0.03;
            if (i == 12) value = 0.02;
            inputs[c].mass_fractions[i] = value;
            sum += value;
        }
        for (double& value : inputs[c].mass_fractions) value /= sum;
    }

    // Exercise the explicit invalid-input flag without asking either backend
    // to silently replace a non-positive heat capacity.
    inputs[kNumCases - 1].cv = 0.0;
    inputs[kNumCases - 1].expected_failure = kInvalidInput;
    return inputs;
}

bool cuda_ok(cudaError_t error, const char* operation)
{
    if (error == cudaSuccess) return true;
    std::fprintf(stderr, "%s: %s\n", operation, cudaGetErrorString(error));
    return false;
}

double scaled_error(const double* reference, const double* candidate, int count)
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

double scalar_scaled_error(double reference, double candidate)
{
    return std::abs(reference - candidate)
         / std::max(std::abs(reference), 1.0);
}

} // namespace

int main()
{
    const auto inputs = make_inputs();
    std::array<TestOutput, kNumCases> cpu{};
    std::array<TestOutput, kNumCases> gpu{};
    for (int i = 0; i < kNumCases; ++i) integrate_cpu(inputs[i], cpu[i]);

    if (!cuda_ok(cudaMemcpyToSymbol(
            device_energy_weights, NetAprox13::ENERGY_WEIGHTS.data(),
            sizeof(device_energy_weights)), "cudaMemcpyToSymbol(weights)")) {
        return EXIT_FAILURE;
    }

    TestInput* device_inputs = nullptr;
    TestOutput* device_outputs = nullptr;
    bool success = cuda_ok(cudaMalloc(&device_inputs, sizeof(inputs)),
                           "cudaMalloc(inputs)")
                && cuda_ok(cudaMalloc(&device_outputs, sizeof(gpu)),
                           "cudaMalloc(outputs)");
    if (success) {
        success = cuda_ok(cudaMemcpy(device_inputs, inputs.data(), sizeof(inputs),
                                     cudaMemcpyHostToDevice),
                          "cudaMemcpy(inputs)")
               && cuda_ok(cudaMemset(device_outputs, 0, sizeof(gpu)),
                          "cudaMemset(outputs)");
    }
    if (success) {
        integrate_kernel<<<1, 32>>>(device_inputs, device_outputs, kNumCases);
        success = cuda_ok(cudaGetLastError(), "integrate_kernel launch")
               && cuda_ok(cudaDeviceSynchronize(), "integrate_kernel sync")
               && cuda_ok(cudaMemcpy(gpu.data(), device_outputs, sizeof(gpu),
                                     cudaMemcpyDeviceToHost),
                          "cudaMemcpy(outputs)");
    }
    cudaFree(device_inputs);
    cudaFree(device_outputs);
    if (!success) return EXIT_FAILURE;

    cudaDeviceProp property{};
    int device = 0;
    int active_blocks = 0;
    cudaGetDevice(&device);
    cudaGetDeviceProperties(&property, device);
    cudaOccupancyMaxActiveBlocksPerMultiprocessor(
        &active_blocks, integrate_kernel, 32, 0);
    const double occupancy = static_cast<double>(active_blocks * 32)
                           / property.maxThreadsPerMultiProcessor;

    double x_error = 0.0;
    double temperature_error = 0.0;
    double nuclear_energy_error = 0.0;
    double thermal_energy_error = 0.0;
    double residual_error = 0.0;
    double closure_error = 0.0;
    bool flags_match = true;
    bool expected_flags = true;
    bool diagnostics_finite = true;
    for (int c = 0; c < kNumCases; ++c) {
        flags_match = flags_match && cpu[c].failure == gpu[c].failure;
        expected_flags = expected_flags
                      && cpu[c].failure == inputs[c].expected_failure;
        if (cpu[c].failure == kSuccess && gpu[c].failure == kSuccess) {
            x_error = std::max(x_error,
                scaled_error(cpu[c].state, gpu[c].state, kNumSpec));
            temperature_error = std::max(temperature_error,
                scalar_scaled_error(cpu[c].state[kNumSpec],
                                    gpu[c].state[kNumSpec]));
            nuclear_energy_error = std::max(nuclear_energy_error,
                scalar_scaled_error(cpu[c].nuclear_energy,
                                    gpu[c].nuclear_energy));
            thermal_energy_error = std::max(thermal_energy_error,
                scalar_scaled_error(cpu[c].thermal_energy,
                                    gpu[c].thermal_energy));
            residual_error = std::max(residual_error,
                scalar_scaled_error(cpu[c].max_newton_residual,
                                    gpu[c].max_newton_residual));
            closure_error = std::max(
                {closure_error, cpu[c].energy_closure_rel,
                 gpu[c].energy_closure_rel});
            diagnostics_finite = diagnostics_finite
                              && std::isfinite(cpu[c].max_newton_residual)
                              && std::isfinite(gpu[c].max_newton_residual)
                              && std::isfinite(cpu[c].energy_closure_rel)
                              && std::isfinite(gpu[c].energy_closure_rel);
        }
        std::printf(
            "case=%d rho=%.3e T0=%.3e dt=%.3e sub=%d "
            "cpu_flag=%d gpu_flag=%d cpu_it=%d gpu_it=%d "
            "cpu_res=%.3e gpu_res=%.3e stiffness=%.3e "
            "cpu_enuc=%.3e gpu_enuc=%.3e "
            "cpu_closure=%.3e gpu_closure=%.3e\n",
            c, inputs[c].density, inputs[c].temperature,
            inputs[c].dt_total, inputs[c].substeps,
            cpu[c].failure, gpu[c].failure,
            cpu[c].total_newton_iterations,
            gpu[c].total_newton_iterations,
            cpu[c].max_newton_residual,
            gpu[c].max_newton_residual,
            cpu[c].stiffness_proxy,
            cpu[c].nuclear_energy, gpu[c].nuclear_energy,
            cpu[c].energy_closure_rel, gpu[c].energy_closure_rel);
    }

    const bool pass = flags_match && expected_flags && diagnostics_finite
                   && x_error <= kParityTolerance
                   && temperature_error <= kParityTolerance
                   && nuclear_energy_error <= kParityTolerance
                   && thermal_energy_error <= kParityTolerance
                   && residual_error <= kParityTolerance
                   && closure_error <= kEnergyClosureTolerance;
    std::printf(
        "aprox13 BE/Newton/LU CPU/GPU states=%d x_rel=%.12e "
        "T_rel=%.12e enuc_rel=%.12e etherm_rel=%.12e "
        "residual_rel=%.12e "
        "closure_max=%.12e flags_match=%s expected_flags=%s finite=%s "
        "gpu=%s cc=%d.%d threads=32 active_blocks_per_sm=%d "
        "occupancy=%.3f status=%s\n",
        kNumCases, x_error, temperature_error, nuclear_energy_error,
        thermal_energy_error, residual_error, closure_error,
        flags_match ? "yes" : "no", expected_flags ? "yes" : "no",
        diagnostics_finite ? "yes" : "no", property.name,
        property.major, property.minor, active_blocks, occupancy,
        pass ? "PASS" : "FAIL");
    return pass ? EXIT_SUCCESS : EXIT_FAILURE;
}
