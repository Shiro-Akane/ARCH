/**
 * @file InitialStateConversion.h
 * @brief Convert case primitive fields to solver conserved state using the shared EOS.
 *
 * Workflow:
 * 1. Read validated configuration or a registered problem request.
 * 2. Convert case primitive fields to solver conserved state using the shared EOS.
 * 3. For native RZ cells, validate all eight physical EOS samples and reuse
 *    the shared V/W integration and density-weighted composition leaves.
 * 4. Return a single resolved value or state with explicit failure on invalid input.
 */

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

#include "data/FluidState.h"
#include "data/GlobalDefs.h"
#include "data/UserTypes.h"
#include "grid/GridMetrics.h"
#include "numerics/state/RzCellAverage.h"
#include "numerics/state/RzNativeClosure.h"
#include "numerics/state/StateAdmissibility.h"
#include "physics/eos/eos_Utils.h"

namespace ProblemHelper::detail {

// Shared by mesh population and application preview. Keep the exact energy
// construction here so temperature-based cases have one initialization path.
template <class Eos>
FluidVector InitialConservedState(const PrimitiveData &data, const Eos &eos,
                                 const NumericsConfig& limits = {}, arch::state::Repair* report = nullptr)
{
    if (!(data.rho > 0.0) || !std::isfinite(data.rho))
        throw std::runtime_error("Initial density must be finite and positive; exact vacuum is unsupported");
    double sum = 0.0;
    for (double x : data.mass_fractions) {
        if (!(x >= 0.0) || !std::isfinite(x)) throw std::runtime_error("Invalid initial composition");
        sum += x;
    }
    if (!data.mass_fractions.empty() && std::abs(sum - 1.0) > 512.0 * data.mass_fractions.size() * std::numeric_limits<double>::epsilon())
        throw std::runtime_error("Initial composition must sum to one");
    FluidVector state;
    state.rho = data.rho;
    state.mom_u = data.rho * data.u;
    state.mom_v = data.rho * data.v;
    state.mom_w = data.rho * data.w;
    if (data.has_temperature) {
        const double specific_internal_energy = eos.get_eint_from_T(
            data.rho, data.temperature, data.mass_fractions.data());
        if (!std::isfinite(specific_internal_energy)) {
            throw std::runtime_error("EOS returned non-finite internal energy for temperature-based initialization.");
        }
        const double kinetic_energy = eos_utils::calc_kinetic_energy(
            data.rho,data.u,data.v,data.w);
        state.eng = data.rho * specific_internal_energy + kinetic_energy;
    } else {
        state.eng = eos.get_total_energy_primitive(
            data.rho, data.u, data.v, data.w, data.p,
            data.mass_fractions.data());
    }
    const auto recovery = arch::state::apply_bounds(state,limits.sml_rho,limits.min_eint,limits.max_eint);
    if (!arch::state::accepted(recovery.status))
        throw std::runtime_error("Initial state has invalid or unresolved thermal energy");
    if (recovery.status == arch::state::Status::repaired) {
        // A positive repaired state can still lie outside a tabulated EOS domain.
        // Validate it before either preview or mesh initialization publishes it.
        const auto thermal = arch::state::recover(state);
        const auto* fractions = data.mass_fractions.data();
        const double temperature = eos.get_temperature(state.rho, thermal.internal, fractions);
        const double pressure = eos.get_pressure(state, fractions);
        const double sound_speed = eos.get_sound_speed(state, pressure, fractions);
        if (!(temperature > 0.0) || !std::isfinite(temperature) ||
            !(pressure > 0.0) || !std::isfinite(pressure) ||
            !(sound_speed > 0.0) || !std::isfinite(sound_speed))
            throw std::runtime_error("Initial state repair lies outside the EOS valid domain");
    }
    if (report) *report = recovery;
    return state;
}

/**
 * Candidate native RZ cell conversion for the initialization/BC migration.
 * Callback supplies physical primitive components at eight real cell samples
 * (four radial Gauss nodes crossed with two axial Gauss nodes).
 * rho/mom_r/mom_z/E/rhoX are V averages; m_phi is the r*dV average of rho*u_phi.
 * The returned view owns no second evolved J/ell state. No live state is changed.
 *
 * Each real sample uses the actual EOS and strict point bounds before V/W
 * integration. The resulting mixed-measure mean is provisional: its thermal
 * closure requires the full same-stage density stencil, available only after
 * the Driver has completed the actual whole-domain boundary/exchange.
 * No raw Cartesian kinetic-energy veto, heating or change of J is performed.
 */
struct InitialCellState {
    FluidVector conserved{};
    std::vector<double> mass_fractions;
};

/** Density-weighted quadrature fraction without premature rho*X underflow.
 * Xbar=sum(w*rho*X)/sum(w*rho). Each positive product is represented as a
 * mantissa and binary exponent; numerator and denominator have independent
 * shared exponents before summation. Only the final ratio is rescaled.
 * Identical sample fractions reproduce their constant exactly, including
 * positive subnormals. No floor, normalization or residual-species correction
 * is applied. Terms below the scaled sum's representability are rounding loss;
 * this does not certify later evolved trace-mass conservation.
 */
template<std::size_t Samples>
double InitialMassFraction(const std::array<double,Samples>& weights,
    const std::array<double,Samples>& densities,const double* fractions)
{
    static_assert(Samples>0);
    if(!fractions)
        throw std::runtime_error("Invalid physical mass-fraction quadrature sample");
    const auto result=RzCellAverage::fraction_mean(weights,densities,
        [fractions](std::size_t k) {return fractions[k];});
    if(result.status==RzCellAverage::Status::unrepresentable)
        throw std::runtime_error("Initial mass-fraction ratio is not representable");
    if(!result.valid())
        throw std::runtime_error("Invalid physical mass-fraction quadrature sample");
    return result.value;
}

/** Sample the actual positive physical cell, then borrow the shared math.
 * Point EOS/bounds/repair rejection precedes integration; the returned native
 * mixed-measure candidate still requires the same-stage post-ghost closure.
 */
template <class Eos,class Callback>
InitialCellState InitialRzCellState(double r_lower,double r_upper,
    double z_lower,double z_upper,int species,const Eos& eos,
    const NumericsConfig& limits,Callback&& init_callback)
{
    if (species<0 || !std::isfinite(r_lower) || !std::isfinite(r_upper)
        || !std::isfinite(z_lower) || !std::isfinite(z_upper)
        || r_lower<0. || !(r_upper>r_lower) || !(z_upper>z_lower))
        throw std::invalid_argument("Invalid physical RZ initialization cell");
    const double dz=z_upper-z_lower;
    const double volume=GridMetrics::Rz::CellVolume(r_lower,r_upper,dz);
    const double angular=GridMetrics::Rz::AngularMomentumMeasure(r_lower,r_upper,dz);
    if (!std::isfinite(dz) || !std::isfinite(volume) || !(volume>0.)
        || !std::isfinite(angular) || !(angular>0.))
        throw std::invalid_argument("RZ initialization native V/W is not representable");
    InitialCellState output;
    output.mass_fractions.assign(species,0.);
    constexpr std::size_t sample_count=8;
    const auto samples=GridMetrics::Rz::CellAverageSamples(r_lower,r_upper,z_lower,z_upper);
    std::array<FluidVector,sample_count> point_samples{};
    std::array<double,sample_count> density_samples{},volume_weights{};
    std::vector<double> fraction_samples(static_cast<std::size_t>(species)*sample_count);
    std::size_t sample_index=0;
    PrimitiveData data;
    for (const auto& q:samples) {
        data=PrimitiveData{};
        data.mass_fractions.assign(species,0.);
        const auto p=Grid::PhysicalCoordsFromNative(2,"cylindrical",q.radius,q.axial,0.,
            GridMetrics::GeometrySemantics::AxisymmetricRz);
        init_callback(p,data);
        if (data.mass_fractions.size()!=static_cast<size_t>(species))
            throw std::runtime_error("RZ Init callback changed species layout");
        arch::state::Repair repair;
        const auto sample=InitialConservedState(data,eos,limits,&repair);
        if (repair.status!=arch::state::Status::valid)
            throw std::runtime_error("RZ cell sample requires repair; candidate not published");
        const arch::state::Bounds bounds{limits.sml_rho,limits.min_eint,limits.max_eint};
        if(arch::state::validate_eos(sample,data.mass_fractions.data(),species,bounds,eos)
            !=arch::state::Status::valid)
            throw std::runtime_error("RZ Init sample lies outside the selected EOS domain");
        point_samples[sample_index]=sample;
        density_samples[sample_index]=sample.rho;
        volume_weights[sample_index]=q.volume_weight;
        for(int sp=0;sp<species;++sp)
            fraction_samples[static_cast<std::size_t>(sp)*sample_count+sample_index]
                =data.mass_fractions[sp];
        ++sample_index;
    }
    const auto integrated=RzCellAverage::conserved_mean(samples,
        [&point_samples](std::size_t k) {return point_samples[k];});
    if(!integrated.valid())
        throw std::runtime_error("RZ Init integral has invalid finite fields or density");
    output.conserved=integrated.value;
    const arch::state::Bounds bounds{limits.sml_rho,limits.min_eint,limits.max_eint};
    if(RzThermodynamics::provisional_native_state(output.conserved,nullptr,0,1,bounds)
        !=arch::state::Status::valid)
        throw std::runtime_error("RZ Init integral has invalid finite fields or density");
    for(int sp=0;sp<species;++sp)
        output.mass_fractions[sp]=InitialMassFraction(volume_weights,density_samples,
            fraction_samples.data()+static_cast<std::size_t>(sp)*sample_count);
    if(RzThermodynamics::provisional_native_state(output.conserved,
        output.mass_fractions.data(),species,1,bounds)!=arch::state::Status::valid)
        throw std::runtime_error("RZ Init integral has invalid composition");
    return output;
}

} // namespace ProblemHelper::detail
