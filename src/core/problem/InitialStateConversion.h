/**
 * @file InitialStateConversion.h
 * @brief Convert case primitive fields to solver conserved state using the shared EOS.
 *
 * Workflow:
 * 1. Read validated configuration or a registered problem request.
 * 2. Convert case primitive fields to solver conserved state using the shared EOS.
 * 3. Return a single resolved value or state with explicit failure on invalid input.
 */

#pragma once

#include <cmath>
#include <stdexcept>

#include "data/FluidState.h"
#include "data/GlobalDefs.h"
#include "data/UserTypes.h"
#include "grid/GridMetrics.h"
#include "numerics/state/StateAdmissibility.h"

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
        const double kinetic_energy = 0.5 * data.rho *
            (data.u * data.u + data.v * data.v + data.w * data.w);
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
 * Callback supplies physical primitive components at four real cell samples.
 * rho/mom_r/mom_z/E/rhoX are V averages; m_phi is the r*dV average of rho*u_phi.
 * The returned view owns no second evolved J/ell state. No live state is changed.
 *
 * This strict path refuses repaired samples and unresolved representative
 * closures. PopulateState's legacy midpoint/repair/ghost publication is not yet
 * switched: its whole repair ledger must migrate with this conversion.
 */
struct InitialCellState {
    FluidVector conserved{};
    std::vector<double> mass_fractions;
};

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
    PrimitiveData data;
    for (const auto& q:GridMetrics::Rz::CellAverageSamples(r_lower,r_upper,z_lower,z_upper)) {
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
        output.conserved.rho+=q.volume_weight*sample.rho;
        output.conserved.mom_u+=q.volume_weight*sample.mom_u;
        output.conserved.mom_v+=q.volume_weight*sample.mom_v;
        output.conserved.mom_w+=q.angular_weight*sample.mom_w;
        output.conserved.eng+=q.volume_weight*sample.eng;
        for(int sp=0;sp<species;++sp)
            output.mass_fractions[sp]+=q.volume_weight*sample.rho*data.mass_fractions[sp];
    }
    for (double& x:output.mass_fractions) x/=output.conserved.rho;
    if (arch::state::validate(output.conserved,output.mass_fractions.data(),species,1,
        limits.sml_rho,limits.min_eint,limits.max_eint)!=arch::state::Status::valid)
        throw std::runtime_error("RZ cell average has unresolved representative state");
    const auto recovered=arch::state::recover(output.conserved);
    const double temperature=eos.get_temperature(output.conserved.rho,recovered.internal,
        output.mass_fractions.data());
    const double pressure=eos.get_pressure(output.conserved,output.mass_fractions.data());
    const double sound=eos.get_sound_speed(output.conserved,pressure,output.mass_fractions.data());
    if (!(temperature>0.) || !std::isfinite(temperature) || !(pressure>0.)
        || !std::isfinite(pressure) || !(sound>0.) || !std::isfinite(sound))
        throw std::runtime_error("RZ cell representative state outside EOS domain");
    return output;
}

} // namespace ProblemHelper::detail
