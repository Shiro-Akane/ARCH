/**
 * @file RzCellAverage.h
 * @brief Shared positive-cell V/W integration and density-weighted fractions.
 *
 * Workflow:
 * 1. Borrow the actual GridMetrics radial-Gauss4/axial-Gauss2 samples and
 *    immutable, already point-EOS-validated conserved states/composition.
 * 2. Accumulate rho/m_r/m_z/E with V weights and m_phi with W weights.
 * 3. Form Xi means with separate exponent-scaled species and total masses.
 * 4. Return explicit mathematical status; the Host owner handles exceptions
 *    and the actual stage/ghost/EOS publication contract.
 *
 * No quadrature, EOS, floor, normalization or evolved field is duplicated.
 * Validity below certifies finite numerical integration only. The caller
 * supplies the native sample rule and owns cell support and scientific checks.
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

/** Conserved mean or five unusable NaNs; failure never supplies default U. */
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

/** Integrate validated point states in the existing sample order.
 * <U>_V=sum(w_V U) except m_phi=<rho*u_phi>_W=sum(w_W rho*u_phi).
 * Radius/weights must belong to the caller's positive native cell rule;
 * this function neither infers bounds nor invents a weight-sum tolerance.
 * The immutable index reader must be device-callable and must not throw.
 */
template<std::size_t Samples,class StateReader>
ARCH_INLINE ConservedMean conserved_mean(
    const std::array<GridMetrics::Rz::CellAverageSample,Samples>& samples,
    const StateReader& state_reader)
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
        const auto point=state_reader(k);
        if(!std::isfinite(point.rho)||!std::isfinite(point.mom_u)
           ||!std::isfinite(point.mom_v)||!std::isfinite(point.mom_w)
           ||!std::isfinite(point.eng))
            return ConservedMean(Status::nonfinite_state);
        if(!(point.rho>0.))return ConservedMean(Status::invalid_density);
        mean.rho+=q.volume_weight*point.rho;
        mean.mom_u+=q.volume_weight*point.mom_u;
        mean.mom_v+=q.volume_weight*point.mom_v;
        mean.mom_w+=q.angular_weight*point.mom_w;
        mean.eng+=q.volume_weight*point.eng;
    }
    if(!std::isfinite(mean.rho)||!(mean.rho>0.)
       ||!std::isfinite(mean.mom_u)||!std::isfinite(mean.mom_v)
       ||!std::isfinite(mean.mom_w)||!std::isfinite(mean.eng))
        return ConservedMean(Status::unrepresentable);
    ConservedMean result;
    result.value=mean;result.status=Status::valid;
    return result;
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
