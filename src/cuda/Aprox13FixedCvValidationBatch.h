#pragma once

#include "BurnBatchView.h"

namespace arch::cuda {

/**
 * Controls for the fixed-cv validation-only aprox13 batch integrator.
 *
 * fixed_substeps is a device array with one strictly positive entry per cell.
 * It exists to reproduce the already validated bounded BE/Newton test.  It is
 * not the adaptive production ODE policy and must disappear when the production
 * batch launcher owns the regular PI-controlled substepping implementation.
 */
struct Aprox13FixedCvValidationOptions
{
    const std::int32_t* fixed_substeps = nullptr;
    double rtol = 1.0e-11;
    double atol = 1.0e-18;
    double small_x = 1.0e-30;
    double small_temperature = 1.0e6;
    double maximum_temperature = 1.0e11;
    double energy_closure_tolerance = 1.0e-4;
    std::int32_t max_newton_iterations = 24;
    std::int32_t threads_per_block = 32;
};

/**
 * Enqueue the current translated-Timmes aprox13 fixed-cv validation kernel.
 *
 * The call is asynchronous with respect to stream.  It performs no allocation,
 * no host/device state transfer, and no synchronization.  The caller owns all
 * device buffers and must synchronize before reading status or output.
 *
 * This symbol is deliberately named validation and is not registered in the
 * production runtime dispatch registry.  Fixed cv is not the Helmholtz EOS;
 * NSE is not implemented here.  An explicit CUDA request must therefore not
 * route a production Cellular run to this launcher.
 */
BurnBatchLaunchResult launch_aprox13_fixed_cv_validation_batch(
    const BurnBatchInputSoA& input,
    const BurnBatchOutputSoA& output,
    const Aprox13FixedCvValidationOptions& options,
    BurnCudaStream stream = nullptr) noexcept;

static_assert(std::is_standard_layout_v<Aprox13FixedCvValidationOptions>);
static_assert(std::is_trivially_copyable_v<Aprox13FixedCvValidationOptions>);

} // namespace arch::cuda
