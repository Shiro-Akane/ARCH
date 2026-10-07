/**
 * @file CheckedHydroEos.cuh
 * @brief Transport shared EOS failures through a borrowed device status latch.
 *
 * Queries delegate to the physical EOS without changing their returned values.
 * The launch owner keeps status storage alive until all consumers complete.
 * Workflow:
 * 1. Borrow immutable EOS data and the launch-owned required-query latch.
 * 2. For pure Helm queries, match complete inputs in the active warp and
 *    delegate one query per identical group to the original shared EOS.
 * 3. Reuse stage mean results only for exactly matching face query inputs.
 * 4. Validate each returned result; optional probes retain their local fallback.
 * Other EOS views delegate directly without grouping or a different formula.
 */

#pragma once

#include <cmath>
#include <limits>
#include <cuda_runtime.h>

#include "cuda/common/CudaCommon.cuh"
#include "cuda/common/DeviceEosStatus.h"
#include "cuda/common/ExactWarpGroup.cuh"
#include "data/FluidState.h"

namespace arch::cuda {

// Backend execution/error transport: identical pure Helm inputs may share one
// call within the active warp; every result comes from the original shared EOS.  No pressure/energy
// floor, recovery, derivative or interpolation formula belongs in this adapter.
// The launch owns status and keeps it alive until all kernels have quiesced.
template <class Eos>
class CheckedHydroEosView {
public:
    ARCH_INLINE CheckedHydroEosView(Eos eos, int* status)
        : eos_(bind_device_eos_status(eos, status)), status_(status) {}

    ARCH_INLINE CheckedHydroEosView candidate_view() const
    {
        auto candidate = *this;
        candidate.eos_ = bind_device_eos_status(eos_, nullptr);
        candidate.status_ = nullptr;
        return candidate;
    }

    /** Borrow required mean results only for the current face/stage input.
     * Their EOS owner is this launch's immutable policy. A query may reuse one
     * result only when rho, specific energy and every species input agree.
     * Explicit Native means always decline point reuse, matching the shared
     * MeanThermoView rule; chart identity never substitutes for point EOS.
     */
    ARCH_INLINE void bind_mean_thermodynamics(const DeviceStateView& state,
        int left, int right, const double* pressure, const double* sound_speed,
        GridMetrics::GeometrySemantics semantics = GridMetrics::GeometrySemantics::Existing)
    {
        // Native cache entries describe closure effective means, not physical
        // point states. Retire any earlier ordinary borrowing before returning.
        // Unknown charts additionally poison the required-query latch; no point
        // query can obtain an authorization from unrecognized metadata.
        if (semantics != GridMetrics::GeometrySemantics::Existing) {
            means_ = nullptr; mean_pressure_ = nullptr; mean_sound_speed_ = nullptr;
            if (semantics != GridMetrics::GeometrySemantics::AxisymmetricRz)
                static_cast<void>(checked(std::numeric_limits<double>::quiet_NaN()));
            return;
        }
        if constexpr (requires { typename Eos::HostHydroScope; }) {
            means_ = &state; mean_cells_[0] = left; mean_cells_[1] = right;
            mean_pressure_ = pressure; mean_sound_speed_ = sound_speed;
        }
    }

    ARCH_INLINE double get_pressure(const FluidVector& state, const double* composition) const
    { return checked_pressure(evaluate_once(composition, [&] {
        return eos_.get_pressure(state, composition);
    }, state.rho, state.mom_u, state.mom_v, state.mom_w, state.eng)); }

    ARCH_INLINE double get_sound_speed(
        const FluidVector& state, double pressure, const double* composition) const
    { return checked(evaluate_once(composition, [&] {
        return eos_.get_sound_speed(state, pressure, composition);
    }, state.rho, state.mom_u, state.mom_v, state.mom_w, state.eng, pressure)); }

    template <class T = Eos>
    requires requires(const T& value, const FluidVector& state, const double* composition) {
        value.get_sound_speed(state, composition);
    }
    ARCH_INLINE double get_sound_speed(
        const FluidVector& state, const double* composition) const
    { return checked(evaluate_once(composition, [&] {
        return eos_.get_sound_speed(state, composition);
    }, state.rho, state.mom_u, state.mom_v, state.mom_w, state.eng)); }

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
        if (reuse_mean_thermodynamics(rho, energy, composition, pressure, speed)) {
            pressure = checked_pressure(pressure); speed = checked(speed);
            return;
        }
        if constexpr (requires { typename Eos::HostHydroScope; }) {
            const auto group = input_group(composition, rho, energy);
            pressure = speed = 0.0;
            if (group.leader()) eos_.get_pressure_and_sound_speed(
                rho, energy, composition, pressure, speed);
            pressure = group.broadcast(pressure); speed = group.broadcast(speed);
        } else {
            eos_.get_pressure_and_sound_speed(rho, energy, composition, pressure, speed);
        }
        pressure = checked_pressure(pressure);
        speed = checked(speed);
    }

    ARCH_INLINE double get_gamma(const double* composition) const
    { return checked(eos_.get_gamma(composition)); }

    /** Preserve the shared ideal-gas Roe identity through the checked adapter.
     * It is an auxiliary Roe coefficient; the shared flux retains its own
     * admissibility/fallback test and required endpoints are checked separately.
     */
    template<class T = Eos>
    requires requires(const T& value, const double* x) { value.roe_gamma_minus_one(x); }
    ARCH_INLINE double roe_gamma_minus_one(const double* composition) const
    { return eos_.roe_gamma_minus_one(composition); }

    /** Forward the shared Roe-only derivative pair with optional-probe status.
     * Both scalars consume the same original EOS jet. A bad auxiliary state
     * selects the flux fallback and never poisons the required-query latch.
     */
    template<class T = Eos>
    requires requires(const T& value, double rho, double energy, const double* x,
                      double& chi, double& kappa) {
        value.get_dp_drho_e_and_dp_de_rho(rho, energy, x, chi, kappa);
    }
    ARCH_INLINE void get_dp_drho_e_and_dp_de_rho(double rho, double energy,
        const double* composition, double& chi, double& kappa) const
    {
        if constexpr (requires { typename Eos::HostHydroScope; }) {
            const auto group = input_group(composition, rho, energy);
            chi = kappa = 0.0;
            if (group.leader()) bind_device_eos_status(eos_, nullptr)
                .get_dp_drho_e_and_dp_de_rho(rho, energy, composition, chi, kappa);
            chi = group.broadcast(chi); kappa = group.broadcast(kappa);
        } else {
            bind_device_eos_status(eos_, nullptr)
                .get_dp_drho_e_and_dp_de_rho(rho, energy, composition, chi, kappa);
        }
    }

    ARCH_INLINE double get_pressure_from_rho_e(
        double rho, double energy, const double* composition) const
    { return checked_pressure(evaluate_once(composition, [&] {
        return eos_.get_pressure_from_rho_e(rho, energy, composition);
    }, rho, energy)); }

    // Roe rectangular states are optional probes. A failed probe selects the
    // endpoint-speed or low-order fallback; it must not poison the launch.
    // Endpoint and final-state queries still use the checked interface.
    ARCH_INLINE double probe_pressure_from_rho_e(
        double rho, double energy, const double* composition) const
    { return evaluate_once(composition, [&] {
        return bind_device_eos_status(eos_, nullptr).get_pressure_from_rho_e(rho, energy, composition);
    }, rho, energy); }

    ARCH_INLINE double probe_dp_drho_e(
        double rho, double energy, const double* composition) const
    { return evaluate_once(composition, [&] {
        return bind_device_eos_status(eos_, nullptr).get_dp_drho_e(rho, energy, composition);
    }, rho, energy); }

    ARCH_INLINE double probe_dp_de_rho(
        double rho, double energy, const double* composition) const
    { return evaluate_once(composition, [&] {
        return bind_device_eos_status(eos_, nullptr).get_dp_de_rho(rho, energy, composition);
    }, rho, energy); }

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
    { return checked(evaluate_once(composition, [&] {
        return eos_.get_total_energy_primitive(rho, u, v, w, pressure, composition);
    }, rho, u, v, w, pressure)); }

    // PPM may reject this reconstructed face and publish the unchanged owning
    // mean. Only that final mean or an accepted face is a required EOS state.
    ARCH_INLINE double probe_total_energy_primitive(
        double rho, double u, double v, double w, double pressure,
        const double* composition) const
    { return bind_device_eos_status(eos_, nullptr).get_total_energy_primitive(
        rho, u, v, w, pressure, composition); }

private:
    // These borrowed fields never escape hydro_face_kernel_work. They are not
    // a temperature hint, persistent cache, or approximation of a face state.
    const DeviceStateView* means_ = nullptr;
    const double* mean_pressure_ = nullptr;
    const double* mean_sound_speed_ = nullptr;
    int mean_cells_[2]{};

    /** Match the exact arguments of the original required-mean EOS query. */
    ARCH_INLINE bool reuse_mean_thermodynamics(double rho, double energy,
        const double* composition, double& pressure, double& speed) const
    {
        if constexpr (requires { typename Eos::HostHydroScope; }) {
            if (!means_ || !mean_pressure_ || !mean_sound_speed_ || !composition) return false;
            if (means_->n_species != eos_.specs.count) return false;
            for (int side = 0; side < 2; ++side) {
                const int cell = mean_cells_[side];
                if (cell < 0 || cell >= means_->total_size) continue;
                const auto mean = means_->load(cell);
                if (rho != mean.rho || energy != arch::state::recover(mean).internal) continue;
                bool same = true;
                for (int species = 0; species < means_->n_species; ++species)
                    same = same && composition[species] == means_->species(species, cell)
                        && (composition[species] != 0.0
                            || std::signbit(composition[species])
                               == std::signbit(means_->species(species, cell)));
                if (same) {
                    pressure = mean_pressure_[cell]; speed = mean_sound_speed_[cell];
                    return true;
                }
            }
        }
        return false;
    }

    /** Match complete inputs only for the pure, expensive Helm query family. */
    template<class... Scalars>
    ARCH_INLINE ExactWarpGroup input_group(const double* composition, Scalars... inputs) const
    {
        constexpr bool reusable = requires { typename Eos::HostHydroScope; };
        ExactWarpGroup group(reusable);
        if constexpr (reusable) {
            group.match_word(reinterpret_cast<unsigned long long>(status_));
            (group.match(inputs), ...);
            // Preserve the shared EOS's invalid-input handling for null X.
            group.match_word(composition != nullptr);
            if (composition)
                for (int species = 0; species < eos_.specs.count; ++species)
                    group.match(composition[species]);
        }
        return group;
    }

    /** Call the shared EOS once per identical group and check each lane later. */
    template<class Evaluate, class... Scalars>
    ARCH_INLINE double evaluate_once(const double* composition, Evaluate evaluate,
                                     Scalars... inputs) const
    {
        if constexpr (requires { typename Eos::HostHydroScope; }) {
            const auto group = input_group(composition, inputs...);
            double result = 0.0;
            if (group.leader()) result = evaluate();
            return group.broadcast(result);
        } else {
            return evaluate();
        }
    }

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
