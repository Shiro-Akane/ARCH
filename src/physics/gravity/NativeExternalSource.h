/**
 * @file NativeExternalSource.h
 * @brief Host traversal of one genuinely prepared native-RZ body source.
 *
 * Workflow:
 * 1. Authenticate the actual immutable patch through its private stage receipt.
 * 2. Recover the existing physical baseline and validate all eight real Gauss
 *    states with the selected EOS and unchanged composition/physical bounds.
 * 3. Call the shared point source and V/W integrator; reuse dead face scratch.
 * 4. Validate every candidate addition before adding one patch body source,
 *    then publish measured V impulse/work and W angular impulse exactly once.
 *
 * No additional state, density profile, floor, EOS or source formula is created.
 * The macro transaction owns rollback of tentative output and source budgets.
 */
#pragma once

#include <array>
#include <cmath>
#include <span>
#include <stdexcept>

#include "data/FluidState.h"
#include "grid/GridMetrics.h"
#include "numerics/state/RzNativeClosure.h"
#include "physics/gravity/GravitySource.h"
#include "physics/gravity/NativeExternalStage.h"

namespace Physical::Gravity {

/** Integrate dt*rho*g with V/V/W and dt*rho*u dot g with V once per cell.
 * Scratch is the caller's completed face buffer, never a new evolved field.
 * All point EOS calls precede patch dU mutation. Composition remains the same
 * cell fractions; neither normalization nor positivity repair is allowed.
 */
template<class Eos>
void add_native_external_sources(std::span<FluidVector> dU,
    std::span<FluidVector> scratch,std::span<double> fraction_scratch,
    const FluidState& input,const Eos& eos,const Grid& grid,
    const GridMetrics::GeometryView& geometry,double dt,
    const arch::state::Bounds& bounds,NativeExternalStageFrame::PatchReceipt& receipt)
{
    receipt.require_application(input,grid,geometry,dt,bounds);
    const auto external=receipt.external();
    const int extent=grid.GetTotalSize(),species=input.GetNumSpecies();
    if(extent<=0||dU.size()!=static_cast<std::size_t>(extent)
        ||scratch.size()!=dU.size()||species<0
        ||fraction_scratch.size()<static_cast<std::size_t>(species)
        ||input.block_total_size_!=extent
        ||input.mass_fractions.size()!=static_cast<std::size_t>(species)*extent)
        throw std::invalid_argument("Native external source requires its actual patch scratch/layout");
    const auto read=[&](int index){return input.get(index);};
    const auto finite=[](const FluidVector& u) {
        return std::isfinite(u.rho)&&std::isfinite(u.mom_u)&&std::isfinite(u.mom_v)
            &&std::isfinite(u.mom_w)&&std::isfinite(u.eng);
    };
    for(int j=grid.Js();j<grid.Je();++j)for(int i=grid.Is();i<grid.Ie();++i) {
        const int index=grid.GetIndex(i,j,0);
        const auto cell=RzThermodynamics::make_cell(read,index,geometry,i,bounds);
        if(!cell.valid())throw std::runtime_error("Native external source input closure is invalid");
        for(int s=0;s<species;++s)fraction_scratch[s]=input.X(s,index);
        const auto samples=GridMetrics::Rz::CellAverageSamples(
            grid.GetFacePosL(i),grid.GetFacePosR(i),grid.GetAxialFacePosL(j),grid.GetAxialFacePosR(j));
        std::array<FluidVector,8> points;
        for(std::size_t node=0;node<points.size();++node) {
            points[node]=RzThermodynamics::base_point(cell,samples[node].radius);
            if(arch::state::validate_eos(points[node],fraction_scratch.data(),species,bounds,eos)
                !=arch::state::Status::valid)
                throw std::runtime_error("Native external source physical point/EOS is invalid");
        }
        const auto source=native_external_source_mean(samples,
            [&](std::size_t node){return points[node];},external,dt);
        if(!source.valid()||source.value.rho!=0.||!finite(dU[index]))
            throw std::runtime_error("Native external source integral is unrepresentable");
        scratch[index]=source.value;
        auto candidate=dU[index];
        candidate.mom_u+=source.value.mom_u;candidate.mom_v+=source.value.mom_v;
        candidate.mom_w+=source.value.mom_w;candidate.eng+=source.value.eng;
        if(!finite(candidate))throw std::runtime_error("Native external source candidate addition is nonfinite");
    }
    // Revalidate the same actual application after every candidate EOS/source
    // check and before the first patch dU source write. The claim owns failure.
    receipt.require_application(input,grid,geometry,dt,bounds);
    NativeBodySourceBudget budget{};
    for(int j=grid.Js();j<grid.Je();++j)for(int i=grid.Is();i<grid.Ie();++i) {
        const int index=grid.GetIndex(i,j,0);const auto before=dU[index];
        dU[index].mom_u+=scratch[index].mom_u;dU[index].mom_v+=scratch[index].mom_v;
        dU[index].mom_w+=scratch[index].mom_w;dU[index].eng+=scratch[index].eng;
        // Accounting uses actual rounded additions, with no RK weight here:
        // Q_r/z/E = V*(dU_after-dU_before), Q_J = W*delta(m_phi).
        const long double volume=GridMetrics::CellVolume(geometry,i,j,0);
        const long double angular=GridMetrics::Rz::AngularMomentumMeasure(geometry,i,j);
        budget.radial_momentum+=volume*(static_cast<long double>(dU[index].mom_u)-before.mom_u);
        budget.axial_momentum+=volume*(static_cast<long double>(dU[index].mom_v)-before.mom_v);
        budget.torque+=angular*(static_cast<long double>(dU[index].mom_w)-before.mom_w);
        budget.work+=volume*(static_cast<long double>(dU[index].eng)-before.eng);
    }
    receipt.commit(budget);
}

} // namespace Physical::Gravity
