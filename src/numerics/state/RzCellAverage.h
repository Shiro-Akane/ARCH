/**
 * @file RzCellAverage.h
 * @brief Shared positive-cell V/W integration and density-weighted fractions.
 *
 * Workflow:
 * 1. Borrow the actual GridMetrics radial-Gauss4/axial-Gauss2 samples and
 *    immutable, already point-EOS-validated conserved states/composition.
 * 2. Multiply/accumulate rho/m_r/m_z/E with V weights and m_phi with
 *    W weights, rejecting nonfinite or completely lost nonzero products.
 * 3. Form Xi means with separate exponent-scaled species and total masses.
 * 4. Integrate signed source components through the same V/W product sequence.
 * 5. Return explicit mathematical status; the Host owner handles exceptions
 *    and the actual stage/ghost/EOS publication contract.
 *
 * No quadrature, EOS, floor, normalization or evolved field is duplicated.
 * Validity below certifies finite numerical integration and detects a nonzero
 * point contribution completely rounded to zero. It does not certify exact
 * signed summation, cancellation accuracy or all subnormal precision. The
 * caller supplies the native sample rule, support and scientific checks.
 */
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

#include "core/ArchPortability.h"
#include "data/FluidState.h"
#include "grid/GridMetrics.h"

namespace RzCellAverage {

/** Mathematical failure category, distinct from a thermodynamic certificate. */
enum class Status : unsigned char {
    valid, invalid_coordinate, invalid_weight, invalid_density,
    invalid_fraction, nonfinite_state, unrepresentable
};

/** State/source component mean or five NaNs; failure supplies no default U. */
struct ConservedMean {
    FluidVector value;
    Status status;
    ARCH_INLINE explicit ConservedMean(Status failure=Status::nonfinite_state)
        :value(std::numeric_limits<double>::quiet_NaN(),
               std::numeric_limits<double>::quiet_NaN(),
               std::numeric_limits<double>::quiet_NaN(),
               std::numeric_limits<double>::quiet_NaN(),
               std::numeric_limits<double>::quiet_NaN()),status(failure) {}
    /** Report mathematical integration success, not point EOS validity. */
    ARCH_INLINE bool valid() const {return status==Status::valid;}
};

/** One mass fraction or an unusable NaN on invalid/unrepresentable input. */
struct FractionMean {
    double value;
    Status status;
    ARCH_INLINE explicit FractionMean(Status failure=Status::invalid_fraction)
        :value(std::numeric_limits<double>::quiet_NaN()),status(failure) {}
    /** Report the finite nonnegative weighted-ratio contract. */
    ARCH_INLINE bool valid() const {return status==Status::valid;}
};

/** Check one already-computed original weighted component without correcting it.
 * For w>0 and finite u, a nonzero u with fl(w*u)==0 has lost the entire
 * contribution. Nonzero subnormal products and genuine u=0 remain allowed;
 * exact cancellation of representable contributions belongs to accumulation.
 */
ARCH_INLINE bool representable_contribution(double point,double weighted) {
    return std::isfinite(weighted)&&(point==0.||weighted!=0.);
}

namespace detail {

/** Integrate the sole guarded V/W component sequence in its original order.
 * State mode requires positive point and final mean rho. Source mode accepts
 * finite signed components, including a zero mass source. Both modes retain
 * the same positive native weights, finite checks and each original product
 * immediately followed by its original addition. No cancellation, exponent
 * rescaling, density surrogate or new scientific tolerance is introduced.
 * The immutable component reader must be device-callable and must not throw.
 */
template<bool RequirePositiveDensity,std::size_t Samples,class ComponentReader>
ARCH_INLINE ConservedMean components_mean(
    const std::array<GridMetrics::Rz::CellAverageSample,Samples>& samples,
    const ComponentReader& component_reader)
{
    static_assert(Samples>0);
    FluidVector mean{};
    for(std::size_t k=0;k<Samples;++k) {
        const auto& q=samples[k];
        if(!std::isfinite(q.radius)||q.radius<0.||!std::isfinite(q.axial))
            return ConservedMean(Status::invalid_coordinate);
        if(!std::isfinite(q.volume_weight)||!(q.volume_weight>0.)
           ||!std::isfinite(q.angular_weight)||!(q.angular_weight>0.))
            return ConservedMean(Status::invalid_weight);
        const auto point=component_reader(k);
        if(!std::isfinite(point.rho)||!std::isfinite(point.mom_u)
           ||!std::isfinite(point.mom_v)||!std::isfinite(point.mom_w)
           ||!std::isfinite(point.eng))
            return ConservedMean(Status::nonfinite_state);
        if constexpr(RequirePositiveDensity) {
            if(!(point.rho>0.))return ConservedMean(Status::invalid_density);
        }
        const double weighted_rho=q.volume_weight*point.rho;
        if(!representable_contribution(point.rho,weighted_rho))
            return ConservedMean(Status::unrepresentable);
        mean.rho+=weighted_rho;
        const double weighted_mom_u=q.volume_weight*point.mom_u;
        if(!representable_contribution(point.mom_u,weighted_mom_u))
            return ConservedMean(Status::unrepresentable);
        mean.mom_u+=weighted_mom_u;
        const double weighted_mom_v=q.volume_weight*point.mom_v;
        if(!representable_contribution(point.mom_v,weighted_mom_v))
            return ConservedMean(Status::unrepresentable);
        mean.mom_v+=weighted_mom_v;
        const double weighted_mom_w=q.angular_weight*point.mom_w;
        if(!representable_contribution(point.mom_w,weighted_mom_w))
            return ConservedMean(Status::unrepresentable);
        mean.mom_w+=weighted_mom_w;
        const double weighted_eng=q.volume_weight*point.eng;
        if(!representable_contribution(point.eng,weighted_eng))
            return ConservedMean(Status::unrepresentable);
        mean.eng+=weighted_eng;
    }
    if(!std::isfinite(mean.rho)||(RequirePositiveDensity&&!(mean.rho>0.))
       ||!std::isfinite(mean.mom_u)||!std::isfinite(mean.mom_v)
       ||!std::isfinite(mean.mom_w)||!std::isfinite(mean.eng))
        return ConservedMean(Status::unrepresentable);
    ConservedMean result;
    result.value=mean;result.status=Status::valid;
    return result;
}

} // namespace detail

/** Integrate validated physical point states in the existing sample order.
 * <U>_V=sum(w_V U), except m_phi=<rho*u_phi>_W=sum(w_W rho*u_phi).
 * Actual radii/weights come from the caller's positive native cell rule;
 * finite positive point/final rho and all original failure gates are retained.
 * This is integration only: actual point EOS and stage identity belong to
 * the caller. A failed result contains five NaNs, never a default state.
 */
template<std::size_t Samples,class StateReader>
ARCH_INLINE ConservedMean conserved_mean(
    const std::array<GridMetrics::Rz::CellAverageSample,Samples>& samples,
    const StateReader& state_reader)
{
    return detail::components_mean<true>(samples,state_reader);
}

/** Integrate signed conserved source components without treating them as U.
 * Mass, radial/axial momentum and energy use V; the azimuthal source uses W.
 * A source integrand can have rho=0 or a finite signed mass component. It is
 * not an EOS state, and this entry neither invents a positive rho nor applies
 * a point thermal test. All sample/weight/finite/product guards are identical
 * to the state entry; only its point/final positive-density gates are absent.
 * Physical input-state/EOS checks and source origin remain the caller's duty.
 */
template<std::size_t Samples,class SourceReader>
ARCH_INLINE ConservedMean source_components_mean(
    const std::array<GridMetrics::Rz::CellAverageSample,Samples>& samples,
    const SourceReader& source_reader)
{
    return detail::components_mean<false>(samples,source_reader);
}

/** Density-weighted Xi without premature w*rho*Xi overflow/underflow.
 * Xi_bar=sum(w*rho*Xi)/sum(w*rho). Separate binary exponents scale positive
 * numerator and denominator sums; only the final quotient is rescaled.
 * All inputs are checked before constant Xi is reproduced exactly, including
 * positive subnormals. Complete simplex/EOS checks remain the caller's job.
 * Tiny varying contributions and a final ratio rounded to zero retain IEEE
 * rounding loss; there is no trace-species floor or conservation certificate.
 * The immutable index reader must be device-callable and must not throw.
 */
template<std::size_t Samples,class FractionReader>
ARCH_INLINE FractionMean fraction_mean(const std::array<double,Samples>& weights,
    const std::array<double,Samples>& densities,const FractionReader& fraction_reader)
{
    static_assert(Samples>0);
    std::array<double,Samples> denominator{},numerator{};
    std::array<int,Samples> denominator_power{},numerator_power{};
    int dmax=std::numeric_limits<int>::lowest(),nmax=dmax;
    double first=0.;bool constant=true;
    for(std::size_t k=0;k<Samples;++k) {
        if(!std::isfinite(weights[k])||!(weights[k]>0.))
            return FractionMean(Status::invalid_weight);
        if(!std::isfinite(densities[k])||!(densities[k]>0.))
            return FractionMean(Status::invalid_density);
        const double fraction=fraction_reader(k);
        if(!std::isfinite(fraction)||fraction<0.)
            return FractionMean(Status::invalid_fraction);
        if(k==0)first=fraction;
        int ew=0,er=0,ex=0;
        denominator[k]=std::frexp(weights[k],&ew)*std::frexp(densities[k],&er);
        denominator_power[k]=ew+er;
        if(denominator_power[k]>dmax)dmax=denominator_power[k];
        constant=constant&&fraction==first;
        if(fraction>0.) {
            numerator[k]=denominator[k]*std::frexp(fraction,&ex);
            numerator_power[k]=denominator_power[k]+ex;
            if(numerator_power[k]>nmax)nmax=numerator_power[k];
        }
    }
    FractionMean result;
    if(constant) {result.value=first;result.status=Status::valid;return result;}
    if(nmax==std::numeric_limits<int>::lowest()) {
        result.value=0.;result.status=Status::valid;return result;
    }
    double mass=0.,species_mass=0.;
    for(std::size_t k=0;k<Samples;++k) {
        mass+=std::scalbn(denominator[k],denominator_power[k]-dmax);
        if(numerator[k]>0.)
            species_mass+=std::scalbn(numerator[k],numerator_power[k]-nmax);
    }
    const double value=std::scalbn(species_mass/mass,nmax-dmax);
    if(!std::isfinite(value)||value<0.)return FractionMean(Status::unrepresentable);
    result.value=value;result.status=Status::valid;
    return result;
}

} // namespace RzCellAverage
