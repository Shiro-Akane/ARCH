/**
 * @file HydroCellUpdate.h
 * @brief Shared Host/device cell-update leaves for hydro divergence and RK.
 *
 * Single cell-update owner shared by Host traversal and CUDA state kernels:
 * cell V divergence evaluates delta U = dt*(A_left*F_left - A_right*F_right)/V,
 * and the normalized RK combination evaluates
 * U_new = U_old + w_flux*((U_current - U_old) + delta U), w_n+w_flux=1.
 * Ordinary cells apply the standard admissibility bounds and repair receipt.
 * The actual final EOS gate after whole-domain BC/exchange remains an external
 * Runtime owner and is not reimplemented by this leaf.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

#include "data/FluidState.h"
#include "numerics/curvilinear/GridMetrics.h"
#include "numerics/state/StateAdmissibility.h"

namespace TimeIntegration
{
    /** Add physical flux divergence, using the actual chart's conserved measure.
     * Axisymmetric azimuthal momentum stores a PWC coefficient with J=W*Mphi:
     * its divergence is (Tl*Fl-Tr*Fr)/W. Other fields/species use A/V. The
     * angular curvature is already absorbed here, so no second phi source is
     * permitted. Geometry supplies moments; no angular state/cache is owned.
     */
    ARCH_INLINE void accumulate_cell_divergence(
        const FluidVector& lower_flux, const FluidVector& upper_flux,
        const double* lower_species_flux, const double* upper_species_flux,
        int n_spec, int species_stride,
        double area_l, double area_r, double volume, double dt,
        FluidVector& dU, double* d_spec,
        const GridMetrics::GeometryView& geometry, int direction, int i, int j, int k)
    {
        double dt_over_vol = dt / volume;
        if (geometry.geometry==GridMetrics::Geometry::Cylindrical && geometry.dim==2) {
            // Suppress the ordinary azimuthal term before arithmetic, rather
            // than computing an irrelevant A/V term and cancelling it later.
            FluidVector lower=lower_flux,upper=upper_flux;
            lower.mom_w=upper.mom_w=0.;
            FluidVector increment=(lower*area_l-upper*area_r)*dt_over_vol;
            const double moment=GridMetrics::AngularMomentumVolumeMoment(geometry,i,j,k);
            const double torque_l=GridMetrics::AngularMomentumFaceMoment(geometry,direction,i,j,k,false);
            const double torque_r=GridMetrics::AngularMomentumFaceMoment(geometry,direction,i,j,k,true);
            increment.mom_w=(lower_flux.mom_w*torque_l-upper_flux.mom_w*torque_r)*(dt/moment);
            dU=dU+increment;
        } else {
            // Retained charts preserve their exact operation order.
            dU = dU + (lower_flux * area_l - upper_flux * area_r) * dt_over_vol;
        }
        for (int s = 0; s < n_spec; ++s)
        {
            int off = s * species_stride;
            d_spec[off] += (lower_species_flux[off] * area_l - upper_species_flux[off] * area_r) * dt_over_vol;
        }
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
     * The ordinary-state bounds and repair accounting are retained exactly.
     */
    ARCH_INLINE arch::state::Status update_stage_cell(
        const FluidVector& U_old, const FluidVector& U_curr, const FluidVector& delta,
        const double* Xi_old, const double* Xi_curr, const double* d_spec,
        int n_spec, int species_stride,
        double weight_n, double weight_flux,
        double sml_rho, double min_eint, double max_eint,
        FluidVector& U_new, double* Xi_new, arch::state::RepairView repairs = {},
        double cell_volume = 1.0, int cell = 0)
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
        const arch::state::Repair repair =
            arch::state::apply_bounds(U_new,sml_rho,min_eint,max_eint);
        if (!arch::state::accepted(repair.status)) {
            U_new.eng = arch::state::invalid();
            return repair.status;
        }
        double sum = 0.0;
        bool composition_repaired = false;
        for (int s = 0; s < n_spec; ++s) {
            const int off = s * species_stride;
            const double density = normalized_stage_component(
                U_old.rho * Xi_old[off], U_curr.rho * Xi_curr[off],
                d_spec[off], weight_flux);
            double fraction = density / raw_density;
            if (!std::isfinite(fraction) || fraction < -arch::state::composition_roundoff_limit) {
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
        if (repair.status == arch::state::Status::repaired || composition_repaired) {
            if(!repairs.conserved_density(repair.delta.rho,repair.delta.mom_u,
                repair.delta.mom_v,repair.delta.mom_w,repair.delta.eng,cell_volume)) {
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
