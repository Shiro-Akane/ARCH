/**
 * @file HydroCellUpdate.h
 * @brief Shared Host/device cell-update leaves for hydro divergence and RK.
 *
 * Single cell-update owner shared by Host traversal and CUDA state kernels:
 * cell V divergence evaluates delta U = dt*(A_left*F_left - A_right*F_right)/V,
 * the RZ W/angular slot instead takes the conditioned J/W increment from the
 * shared geometry leaf, and the normalized RK combination evaluates
 * U_new = U_old + w_flux*((U_current - U_old) + delta U), w_n+w_flux=1.
 * Ordinary cells apply the standard admissibility bounds and repair receipt;
 * native RZ candidates run only the provisional native check here. The actual
 * final EOS gate after whole-domain BC/exchange remains an external Runtime
 * owner and is not reimplemented by this leaf.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

#include "data/FluidState.h"
#include "grid/GridMetrics.h"
#include "numerics/state/RzNativeClosure.h"
#include "numerics/state/StateAdmissibility.h"

namespace TimeIntegration
{
    /** Borrowed geometry for one native angular cell increment. This
     * mathematical context conveys no Runtime/source/ghost authority.
     */
    struct NativeAngularDivergence {
        const GridMetrics::GeometryView* geometry = nullptr;
        int direction = 0;
        int i = 0;
        int j = 0;
    };

    /** Add V-measure fields/species and, when explicitly supplied, the
     * conditioned native J/W increment from the shared geometry leaf.
     * Validate the native angular increment before any cell/species write.
     * False leaves all outputs untouched; it never falls back to V transport.
     */
    ARCH_INLINE bool accumulate_cell_divergence(
        const FluidVector& lower_flux, const FluidVector& upper_flux,
        const double* lower_species_flux, const double* upper_species_flux,
        int n_spec, int species_stride,
        double area_l, double area_r, double volume, double dt,
        FluidVector& dU, double* d_spec,
        const NativeAngularDivergence* angular = nullptr)
    {
        double increment=0.0;
        if(angular&&(!angular->geometry
            ||!GridMetrics::Rz::AngularFluxIncrement(*angular->geometry,
                angular->direction,angular->i,angular->j,
                lower_flux.mom_w,upper_flux.mom_w,dt,increment))) return false;
        double dt_over_vol = dt / volume;
        auto lower=lower_flux,upper=upper_flux;
        // The unique RZ m_phi slot is J/W. All other fields remain V averages.
        // Do not form an unused m_phi V-divergence before replacing it.
        if(angular) {
            lower.mom_w=upper.mom_w=0.0;
            dU.mom_w += increment;
        }
        dU = dU + (lower * area_l - upper * area_r) * dt_over_vol;
        for (int s = 0; s < n_spec; ++s)
        {
            int off = s * species_stride;
            d_spec[off] += (lower_species_flux[off] * area_l - upper_species_flux[off] * area_r) * dt_over_vol;
        }
        return true;
    }

    /**
     * Evaluate one normalized RK component while preserving stationary states.
     *
     * Workflow: validate the finite constituents and explicit Euler trial;
     * retain Euler's current+delta order; otherwise evaluate
     * old+w_flux*((current-old)+delta). Mathematically this is
     * (1-w_flux)*old+w_flux*(current+delta), the original convex combination.
     * If a difference overflows for finite extreme constituents, use that
     * equivalent range-protected form. Nonfinite input/trial is never repaired.
     * The caller validates normalized weights once for all conserved fields.
     */
    ARCH_INLINE double normalized_stage_component(
        double old, double current, double delta, double weight_flux)
    {
        if (!std::isfinite(old) || !std::isfinite(current) || !std::isfinite(delta))
            return arch::state::invalid();
        const double trial = current + delta;
        if (!std::isfinite(trial)) return arch::state::invalid();
        if (weight_flux == 1.0) return trial;
        if (weight_flux == 0.0) return old;
        const double difference = current - old;
        const double increment = difference + delta;
        if (std::isfinite(difference) && std::isfinite(increment)) {
            const double result = old + weight_flux * increment;
            if (std::isfinite(result)) return result;
        }
        return (1.0 - weight_flux) * old + weight_flux * trial;
    }

    /**
     * Combine the normalized RK state and conserved rho*X with the same leaf.
     * The default path retains its original ordinary-state bounds and repair
     * accounting. strict_conservative selects native RZ provisional checks:
     * J/W is not an ordinary point momentum, so its raw thermal energy is not
     * recovered here. The actual post-ghost Runtime EOS gate owns acceptance.
     */
    ARCH_INLINE arch::state::Status update_stage_cell(
        const FluidVector& U_old, const FluidVector& U_curr,
        const FluidVector& delta,
        const double* Xi_old, const double* Xi_curr, const double* d_spec,
        int n_spec, int species_stride,
        double weight_n, double weight_flux,
        double sml_rho, double min_eint, double max_eint,
        FluidVector& U_new, double* Xi_new, arch::state::RepairView repairs = {},
        double cell_volume = 1.0, int cell = 0, bool strict_conservative = false,
        double angular_measure = 0.0)
    {
        // The supported scheduler supplies convex coefficients with w_n+w_f=1.
        // Reject malformed internal requests rather than silently normalizing
        // arbitrary weights or interpreting them as a different RK method.
        if (!std::isfinite(weight_n) || !std::isfinite(weight_flux)
            || weight_n < 0.0 || weight_n > 1.0
            || weight_flux < 0.0 || weight_flux > 1.0
            || weight_n + weight_flux != 1.0) {
            U_new.eng = arch::state::invalid();
            return arch::state::Status::invalid_thermodynamics;
        }
        U_new = {
            normalized_stage_component(U_old.rho, U_curr.rho, delta.rho, weight_flux),
            normalized_stage_component(U_old.mom_u, U_curr.mom_u, delta.mom_u, weight_flux),
            normalized_stage_component(U_old.mom_v, U_curr.mom_v, delta.mom_v, weight_flux),
            normalized_stage_component(U_old.mom_w, U_curr.mom_w, delta.mom_w, weight_flux),
            normalized_stage_component(U_old.eng, U_curr.eng, delta.eng, weight_flux)};

        const double raw_density = U_new.rho;
        arch::state::Repair repair{};
        if(strict_conservative)
            repair.status=RzThermodynamics::provisional_native_state(U_new,nullptr,0,1,
                {sml_rho,min_eint,max_eint});
        else
            repair=arch::state::apply_bounds(U_new,sml_rho,min_eint,max_eint);
        if (!arch::state::accepted(repair.status)) {
            U_new.eng = arch::state::invalid();
            return repair.status;
        }
        // Native candidates must expose the real species-major layout before
        // any indexed access. Existing callers retain their original contract.
        if(strict_conservative&&(n_spec<0||species_stride<=0
            ||(n_spec>0&&(!Xi_old||!Xi_curr||!d_spec||!Xi_new)))) {
            U_new.eng=arch::state::invalid();
            return arch::state::Status::invalid_composition;
        }
        double sum = 0.0;
        bool composition_repaired = false;
        for (int s = 0; s < n_spec; ++s) {
            const int off = s * species_stride;
            const double density = normalized_stage_component(
                U_old.rho * Xi_old[off], U_curr.rho * Xi_curr[off],
                d_spec[off], weight_flux);
            double fraction = density / raw_density;
            if (!std::isfinite(fraction) || fraction <
                (strict_conservative ? 0.0 : -arch::state::composition_roundoff_limit)) {
                U_new.eng = arch::state::invalid();
                return arch::state::Status::invalid_composition;
            }
            composition_repaired = composition_repaired || fraction < 0.0;
            fraction = std::max(0.0, fraction);
            Xi_new[off] = fraction;
            sum += fraction;
        }
        if (n_spec && (!std::isfinite(sum) || std::abs(sum - 1.0)
                > 512.0 * n_spec * std::numeric_limits<double>::epsilon())) {
            U_new.eng = arch::state::invalid();
            return arch::state::Status::invalid_composition;
        }
        if (composition_repaired && !arch::state::normalize_composition(Xi_new,n_spec,species_stride)) {
            U_new.eng = arch::state::invalid();
            return arch::state::Status::invalid_composition;
        }
        if(strict_conservative) {
            const auto status=RzThermodynamics::provisional_native_state(
                U_new,Xi_new,n_spec,species_stride,{sml_rho,min_eint,max_eint});
            if(status!=arch::state::Status::valid) {
                U_new.eng=arch::state::invalid();
                return status;
            }
        }
        if (repair.status == arch::state::Status::repaired || composition_repaired) {
            if(!repairs.conserved_density(repair.delta.rho,repair.delta.mom_u,
                repair.delta.mom_v,repair.delta.mom_w,repair.delta.eng,cell_volume,angular_measure)) {
                U_new.eng=arch::state::invalid();
                return arch::state::Status::nonfinite;
            }
            repairs.event(cell_volume, cell);
            for (int s = 0; s < n_spec; ++s) {
                const int off = s * species_stride;
                const double before = normalized_stage_component(
                    U_old.rho * Xi_old[off], U_curr.rho * Xi_curr[off],
                    d_spec[off], weight_flux);
                repairs.species_mass(s, cell_volume * (U_new.rho * Xi_new[off] - before));
            }
            return arch::state::Status::repaired;
        }
        return arch::state::Status::valid;
    }
} // namespace TimeIntegration
