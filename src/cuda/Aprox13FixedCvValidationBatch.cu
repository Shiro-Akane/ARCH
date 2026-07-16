#include "Aprox13FixedCvValidationBatch.h"

#include <cuda_runtime.h>

#include <cmath>
#include <cstdint>
#include <limits>

#include "physics/network/aprox13/NetAprox13.h"

namespace arch::cuda {
namespace {

constexpr int kNumSpec = NetAprox13::NUM_SPECIES;
constexpr int kNumEq = NetAprox13::ODE_NEQ;

__constant__ double aprox13_energy_weights[kNumSpec];

__device__ bool all_finite(const double* values, int count)
{
    for (int i = 0; i < count; ++i) {
        if (!isfinite(values[i])) return false;
    }
    return true;
}

__device__ void evaluate_current_aprox13(
    const double* state, double density, double cv,
    double small_x, double* rhs, double* jacobian)
{
    double molar[kNumSpec];
    double molar_rhs[kNumSpec];
    double molar_jacobian[kNumSpec * kNumSpec];
    for (int i = 0; i < kNumSpec; ++i) {
        molar[i] = fmax(small_x,
                        fmin(1.0, state[i] / NetAprox13::aion(i)));
    }

    // Current translated Timmes RHS plus its explicit analytic composition
    // Jacobian.  Screening is held fixed exactly as in the CPU Timmes path.
    NetAprox13::molar_rhs_jacobian_frozen_screening(
        molar, density, state[kNumSpec], molar_rhs, molar_jacobian);

    double enuc = 0.0;
    double denuc_dX[kNumSpec]{};
    for (int i = 0; i < kNumSpec; ++i) {
        rhs[i] = molar_rhs[i] * NetAprox13::aion(i);
        enuc += molar_rhs[i] * aprox13_energy_weights[i];
        for (int j = 0; j < kNumSpec; ++j) {
            const double derivative = molar_jacobian[i * kNumSpec + j];
            jacobian[i * kNumEq + j] =
                derivative * NetAprox13::aion(i) / NetAprox13::aion(j);
            denuc_dX[j] += derivative * aprox13_energy_weights[i]
                         / NetAprox13::aion(j);
        }
    }
    enuc *= NetAprox13::ENERGY_CONVERSION;
    for (int j = 0; j < kNumSpec; ++j) {
        denuc_dX[j] *= NetAprox13::ENERGY_CONVERSION;
    }

    // Analytic Timmes temperature derivative; temperature is never perturbed.
    using TemperatureAD = timmes::Dual<1>;
    TemperatureAD molar_ad[kNumSpec];
    TemperatureAD molar_rhs_ad[kNumSpec];
    for (int i = 0; i < kNumSpec; ++i) {
        molar_ad[i] = TemperatureAD(molar[i]);
    }
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
        denuc_dT += derivative * aprox13_energy_weights[i];
    }
    denuc_dT *= NetAprox13::ENERGY_CONVERSION;

    const double inv_cv = 1.0 / cv;
    rhs[kNumSpec] = enuc * inv_cv;
    for (int j = 0; j < kNumSpec; ++j) {
        jacobian[kNumSpec * kNumEq + j] = denuc_dX[j] * inv_cv;
    }
    jacobian[kNumSpec * kNumEq + kNumSpec] = denuc_dT * inv_cv;
}

__device__ bool dense_lu(double* matrix, double* rhs)
{
    int permutation[kNumEq];
    for (int i = 0; i < kNumEq; ++i) permutation[i] = i;

    for (int i = 0; i < kNumEq; ++i) {
        double max_value = 0.0;
        int pivot_row = i;
        for (int row = i; row < kNumEq; ++row) {
            const double value = fabs(matrix[permutation[row] * kNumEq + i]);
            if (value > max_value) {
                max_value = value;
                pivot_row = row;
            }
        }
        if (max_value < 1.0e-20) return false;

        const int temporary = permutation[i];
        permutation[i] = permutation[pivot_row];
        permutation[pivot_row] = temporary;

        const double pivot_inverse =
            1.0 / matrix[permutation[i] * kNumEq + i];
        for (int row = i + 1; row < kNumEq; ++row) {
            matrix[permutation[row] * kNumEq + i] *= pivot_inverse;
            for (int column = i + 1; column < kNumEq; ++column) {
                matrix[permutation[row] * kNumEq + column] -=
                    matrix[permutation[row] * kNumEq + i]
                  * matrix[permutation[i] * kNumEq + column];
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

__device__ double wrms(const double* correction, const double* state,
                       double rtol, double atol)
{
    double sum = 0.0;
    for (int i = 0; i < kNumEq; ++i) {
        const double weight = rtol * fabs(state[i]) + atol;
        const double scaled = correction[i] / weight;
        sum += scaled * scaled;
    }
    return sqrt(sum / kNumEq);
}

__device__ std::uint32_t integrate_fixed_cv(
    const double* initial, double density, double cv, double dt_target,
    int substeps, const Aprox13FixedCvValidationOptions& options,
    double* accepted_state, double& dt_recommended)
{
    for (int i = 0; i < kNumEq; ++i) accepted_state[i] = initial[i];
    dt_recommended = 0.0;

    if (!(density > 0.0) || !(cv > 0.0) || !(dt_target > 0.0)
        || substeps <= 0 || !all_finite(initial, kNumEq)) {
        return BurnCellInvalidInput;
    }

    const double step = dt_target / substeps;
    dt_recommended = step;
    for (int substep = 0; substep < substeps; ++substep) {
        double old[kNumEq];
        for (int i = 0; i < kNumEq; ++i) old[i] = accepted_state[i];
        bool converged = false;

        for (int iteration = 0;
             iteration < options.max_newton_iterations; ++iteration) {
            double rhs[kNumEq]{};
            double matrix[kNumEq * kNumEq]{};
            evaluate_current_aprox13(
                accepted_state, density, cv, options.small_x, rhs, matrix);
            if (!all_finite(rhs, kNumEq)
                || !all_finite(matrix, kNumEq * kNumEq)) {
                dt_recommended = 0.25 * step;
                return BurnCellNonFinite;
            }

            double correction[kNumEq]{};
            for (int i = 0; i < kNumEq; ++i) {
                correction[i] = old[i] - accepted_state[i] + step * rhs[i];
                for (int j = 0; j < kNumEq; ++j) {
                    const int index = i * kNumEq + j;
                    matrix[index] = (i == j ? 1.0 : 0.0)
                                  - step * matrix[index];
                }
            }
            if (!dense_lu(matrix, correction)) {
                dt_recommended = 0.25 * step;
                return BurnCellSingularMatrix;
            }
            if (!all_finite(correction, kNumEq)) {
                dt_recommended = 0.25 * step;
                return BurnCellNonFinite;
            }

            double trial[kNumEq];
            double mass_sum = 0.0;
            bool admissible = true;
            for (int i = 0; i < kNumSpec; ++i) {
                trial[i] = accepted_state[i] + correction[i];
                admissible = admissible && isfinite(trial[i])
                           && trial[i] >= -10.0 * options.atol
                           && trial[i] <= 1.0 + 10.0 * options.atol;
                mass_sum += trial[i];
            }
            trial[kNumSpec] =
                accepted_state[kNumSpec] + correction[kNumSpec];
            admissible = admissible && isfinite(trial[kNumSpec])
                       && trial[kNumSpec] >= options.small_temperature
                       && trial[kNumSpec] <= options.maximum_temperature
                       && isfinite(mass_sum) && mass_sum > 0.0
                       && fabs(mass_sum - 1.0) <= 100.0 * options.rtol;
            if (!admissible) {
                dt_recommended = 0.25 * step;
                return BurnCellInadmissible;
            }

            const double update_norm =
                wrms(correction, accepted_state, options.rtol, options.atol);
            for (int i = 0; i < kNumEq; ++i) accepted_state[i] = trial[i];
            if (update_norm < 1.0) {
                double projected_sum = 0.0;
                for (int i = 0; i < kNumSpec; ++i) {
                    accepted_state[i] =
                        fmax(accepted_state[i], options.small_x);
                    projected_sum += accepted_state[i];
                }
                for (int i = 0; i < kNumSpec; ++i) {
                    accepted_state[i] /= projected_sum;
                }
                converged = true;
                break;
            }
        }
        if (!converged) {
            dt_recommended = 0.25 * step;
            return BurnCellMaxIterations;
        }
    }

    double nuclear = 0.0;
    for (int i = 0; i < kNumSpec; ++i) {
        nuclear += (accepted_state[i] - initial[i])
                 / NetAprox13::aion(i) * aprox13_energy_weights[i];
    }
    nuclear *= NetAprox13::ENERGY_CONVERSION;
    const double thermal =
        cv * (accepted_state[kNumSpec] - initial[kNumSpec]);
    const double scale = fmax(fmax(fabs(nuclear), fabs(thermal)), 1.0);
    const double closure = fabs(thermal - nuclear) / scale;
    if (!isfinite(closure)) {
        dt_recommended = 0.25 * step;
        return BurnCellNonFinite;
    }
    if (closure > options.energy_closure_tolerance) {
        dt_recommended = 0.25 * step;
        return BurnCellEnergyClosure;
    }
    return BurnCellSuccess;
}

__global__ void aprox13_fixed_cv_validation_kernel(
    BurnBatchInputSoA input, BurnBatchOutputSoA output,
    Aprox13FixedCvValidationOptions options)
{
    const std::size_t cell =
        static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (cell >= input.cell_count) return;

    double initial[kNumEq];
    for (int species = 0; species < kNumSpec; ++species) {
        initial[species] =
            input.mass_fractions[species * input.species_stride + cell];
    }
    initial[kNumSpec] = input.temperature[cell];

    double accepted[kNumEq];
    double dt_recommended = 0.0;
    const std::uint32_t status = integrate_fixed_cv(
        initial, input.density[cell], input.heat_capacity_cv[cell],
        input.dt_target[cell], options.fixed_substeps[cell], options,
        accepted, dt_recommended);

    // A failed cell is transactional: never expose a partially converged state.
    const double* committed = status == BurnCellSuccess ? accepted : initial;
    for (int species = 0; species < kNumSpec; ++species) {
        output.mass_fractions[species * output.species_stride + cell] =
            committed[species];
    }
    output.temperature[cell] = committed[kNumSpec];
    output.status[cell] = status;
    output.dt_recommended[cell] = dt_recommended;
}

bool finite_positive(double value)
{
    return std::isfinite(value) && value > 0.0;
}

} // namespace

BurnBatchLaunchResult launch_aprox13_fixed_cv_validation_batch(
    const BurnBatchInputSoA& input,
    const BurnBatchOutputSoA& output,
    const Aprox13FixedCvValidationOptions& options,
    BurnCudaStream stream) noexcept
{
    BurnBatchLaunchResult result;
    if (input.cell_count == 0 && output.cell_count == 0) {
        result.status = BurnBatchLaunchStatus::EmptyBatch;
        return result;
    }

    const bool view_valid = input.cell_count == output.cell_count
        && input.species_count == kNumSpec
        && output.species_count == kNumSpec
        && input.species_stride >= input.cell_count
        && output.species_stride >= output.cell_count
        && input.density != nullptr
        && input.temperature != nullptr
        && input.mass_fractions != nullptr
        && input.dt_target != nullptr
        && input.heat_capacity_cv != nullptr
        && output.temperature != nullptr
        && output.mass_fractions != nullptr
        && output.status != nullptr
        && output.dt_recommended != nullptr;
    if (!view_valid) {
        result.status = BurnBatchLaunchStatus::InvalidView;
        return result;
    }

    const bool options_valid = options.fixed_substeps != nullptr
        && finite_positive(options.rtol)
        && finite_positive(options.atol)
        && finite_positive(options.small_x)
        && finite_positive(options.small_temperature)
        && std::isfinite(options.maximum_temperature)
        && options.maximum_temperature > options.small_temperature
        && finite_positive(options.energy_closure_tolerance)
        && options.max_newton_iterations > 0
        && options.threads_per_block > 0
        && options.threads_per_block <= 1024;
    if (!options_valid) {
        result.status = BurnBatchLaunchStatus::InvalidOptions;
        return result;
    }

    const std::size_t blocks =
        (input.cell_count + options.threads_per_block - 1)
        / options.threads_per_block;
    if (blocks > static_cast<std::size_t>(
                     std::numeric_limits<std::int32_t>::max())) {
        result.status = BurnBatchLaunchStatus::InvalidView;
        return result;
    }

    cudaStream_t native_stream = reinterpret_cast<cudaStream_t>(stream);
    cudaError_t error = cudaMemcpyToSymbolAsync(
        aprox13_energy_weights, NetAprox13::ENERGY_WEIGHTS.data(),
        sizeof(aprox13_energy_weights), 0, cudaMemcpyHostToDevice,
        native_stream);
    if (error == cudaSuccess) {
        aprox13_fixed_cv_validation_kernel<<<
            static_cast<unsigned int>(blocks), options.threads_per_block,
            0, native_stream>>>(input, output, options);
        error = cudaPeekAtLastError();
    }
    if (error != cudaSuccess) {
        result.status = BurnBatchLaunchStatus::RuntimeError;
        result.cuda_runtime_error = static_cast<std::int32_t>(error);
        return result;
    }

    result.status = BurnBatchLaunchStatus::Enqueued;
    result.enqueued_cells = input.cell_count;
    return result;
}

} // namespace arch::cuda
