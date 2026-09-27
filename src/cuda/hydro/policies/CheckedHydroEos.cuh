/**
 * @file CheckedHydroEos.cuh
 * @brief Transport shared EOS failures through a borrowed device status latch.
 *
 * Queries delegate to the physical EOS without changing their returned values.
 * The launch owner keeps status storage alive until all consumers complete.
 * Workflow:
 * 1. Receive a shared flux, reconstruction or integrator policy.
 * 2. Bind CUDA-compatible state views to the same mathematical policy.
 * 3. Return device work descriptors without a separate physics formula.
 */

#pragma once

#include "data/FluidState.h"
#include "cuda/common/DeviceEosStatus.h"

#include <cmath>
#include <cuda_runtime.h>

namespace arch::cuda {

// Backend error transport only: each duck-method delegates exactly once to the
// original shared EOS and returns its unmodified value.  No pressure/energy
// floor, recovery, derivative or interpolation formula belongs in this adapter.
// The launch owns status and keeps it alive until all kernels have quiesced.
template <class Eos>
class CheckedHydroEosView {
public:
    ARCH_INLINE CheckedHydroEosView(Eos eos, int* status)
        : eos_(bind_device_eos_status(eos, status)), status_(status) {}

    ARCH_INLINE CheckedHydroEosView candidate_view() const
    { return CheckedHydroEosView(eos_, nullptr); }

    ARCH_INLINE double get_pressure(const FluidVector& state, const double* composition) const
    { return checked_pressure(eos_.get_pressure(state, composition)); }

    ARCH_INLINE double get_sound_speed(
        const FluidVector& state, double pressure, const double* composition) const
    { return checked(eos_.get_sound_speed(state, pressure, composition)); }

    template <class T = Eos>
    requires requires(const T& value, double rho, double energy,
                      const double* composition, double& pressure, double& speed) {
        value.get_pressure_and_sound_speed(
            rho, energy, composition, pressure, speed);
    }
    ARCH_INLINE void get_pressure_and_sound_speed(
        double rho, double energy, const double* composition,
        double& pressure, double& speed) const
    {
        eos_.get_pressure_and_sound_speed(
            rho, energy, composition, pressure, speed);
        pressure = checked_pressure(pressure);
        speed = checked(speed);
    }

    ARCH_INLINE double get_gamma(const double* composition) const
    { return checked(eos_.get_gamma(composition)); }

    ARCH_INLINE double get_pressure_from_rho_e(
        double rho, double energy, const double* composition) const
    { return checked_pressure(eos_.get_pressure_from_rho_e(rho, energy, composition)); }

    // Roe rectangular states are optional probes. A failed probe selects the
    // endpoint-speed or low-order fallback; it must not poison the launch.
    // Endpoint and final-state queries still use the checked interface.
    ARCH_INLINE double probe_pressure_from_rho_e(
        double rho, double energy, const double* composition) const
    { return bind_device_eos_status(eos_, nullptr).get_pressure_from_rho_e(rho, energy, composition); }

    ARCH_INLINE double probe_dp_drho_e(
        double rho, double energy, const double* composition) const
    { return bind_device_eos_status(eos_, nullptr).get_dp_drho_e(rho, energy, composition); }

    ARCH_INLINE double probe_dp_de_rho(
        double rho, double energy, const double* composition) const
    { return bind_device_eos_status(eos_, nullptr).get_dp_de_rho(rho, energy, composition); }

    ARCH_INLINE double get_pressure_from_rho_T(
        double rho, double temperature, const double* composition) const
    { return checked_pressure(eos_.get_pressure_from_rho_T(rho, temperature, composition)); }

    ARCH_INLINE double get_dp_drho_e(
        double rho, double energy, const double* composition) const
    { return checked(eos_.get_dp_drho_e(rho, energy, composition)); }

    ARCH_INLINE double get_dp_de_rho(
        double rho, double energy, const double* composition) const
    { return checked(eos_.get_dp_de_rho(rho, energy, composition)); }

    ARCH_INLINE double get_total_energy_primitive(
        double rho, double u, double v, double w, double pressure,
        const double* composition) const
    { return checked(eos_.get_total_energy_primitive(rho, u, v, w, pressure, composition)); }

    // PPM may reject this reconstructed face and publish the unchanged owning
    // mean. Only that final mean or an accepted face is a required EOS state.
    ARCH_INLINE double probe_total_energy_primitive(
        double rho, double u, double v, double w, double pressure,
        const double* composition) const
    { return bind_device_eos_status(eos_, nullptr).get_total_energy_primitive(
        rho, u, v, w, pressure, composition); }

private:
    ARCH_INLINE double checked_pressure(double value) const
    {
        // A finite nonpositive hydro pressure is no more admissible than NaN.
        // Do not change the EOS value; the launch owner rejects the stage.
        if (!(value > 0.0) && status_ != nullptr) {
#if defined(__CUDA_ARCH__)
            atomicExch(status_, 1);
#else
            *status_ = 1;
#endif
        }
        return value;
    }

    ARCH_INLINE double checked(double value) const
    {
        if (!std::isfinite(value) && status_ != nullptr) {
#if defined(__CUDA_ARCH__)
            atomicExch(status_, 1);
#else
            *status_ = 1;
#endif
        }
        return value;
    }

    Eos eos_;
    int* status_;
};

template <class Eos>
ARCH_INLINE CheckedHydroEosView<Eos> make_checked_hydro_eos(Eos eos, int* status)
{
    return CheckedHydroEosView<Eos>(eos, status);
}

} // namespace arch::cuda
