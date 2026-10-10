/**
 * @file NativeExternalSource.h
 * @brief Shared native-RZ body-source cell math and actual Host traversal.
 *
 * Workflow:
 * 1. Authenticate the actual immutable patch through its private stage receipt.
 * 2. Evaluate the shared ARCH_INLINE cell leaf for every eligible cell: recover
 *    the existing physical baseline, validate all eight real Gauss states with
 *    the selected EOS and unchanged composition/physical bounds, call the same
 *    point source and V/W integrator, and check the candidate addition.
 * 3. Keep a leaf result only while it is Valid, reuse dead face scratch, and
 *    map the leaf's five statuses to the exact original failure messages, so
 *    the Host traversal consumes the platform-neutral cell evaluation.
 * 4. Revalidate the same actual application, then add one patch body source and
 *    publish measured V impulse/work and W angular impulse exactly once.
 *
 * No additional state, density profile, floor, EOS or source formula is created.
 * The macro transaction owns rollback of tentative output and source budgets.
 */
#pragma once

#include <span>
#include <stdexcept>

#include "physics/gravity/NativeExternalSourceMath.h"
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
    const auto fraction=[&](int s,int index){return input.X(s,index);};
    for(int j=grid.Js();j<grid.Je();++j)for(int i=grid.Is();i<grid.Ie();++i) {
        const int index=grid.GetIndex(i,j,0);
        const auto result=evaluate_native_external_source_cell(read,fraction,index,
            species,eos,geometry,i,j,dt,bounds,fraction_scratch.data(),external,dU[index]);
        if(result.status==NativeExternalCellStatus::InputClosureInvalid)
            throw std::runtime_error("Native external source input closure is invalid");
        if(result.status==NativeExternalCellStatus::PhysicalPointInvalid)
            throw std::runtime_error("Native external source physical point/EOS is invalid");
        if(result.status==NativeExternalCellStatus::IntegralUnrepresentable)
            throw std::runtime_error("Native external source integral is unrepresentable");
        if(result.status==NativeExternalCellStatus::CandidateNonfinite)
            throw std::runtime_error("Native external source candidate addition is nonfinite");
        scratch[index]=result.source;
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
        accumulate_native_external_source_budget(budget.radial_momentum,
            budget.axial_momentum,budget.torque,budget.work,before,dU[index],geometry,i,j);
    }
    receipt.commit(budget);
}

} // namespace Physical::Gravity
