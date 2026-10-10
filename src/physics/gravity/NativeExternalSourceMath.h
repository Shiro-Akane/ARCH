/**
 * @file NativeExternalSourceMath.h
 * @brief Platform-neutral shared native-RZ external cell math owner.
 *
 * Workflow:
 * 1. This header owns only the shared ARCH_INLINE cell leaf: the exact status
 *    enum, the evaluated-cell candidate and the one-cell external evaluation.
 * 2. It is included by the Host receipt/traversal header and can be reused by
 *    other platforms, so it depends only on state/view/math headers and never
 *    on the Host receipt or control dependency graph.
 * 3. The exact formula, node order and failure statuses are unchanged; the
 *    Host traversal still maps each returned status to its original message.
 *
 * The caller owns fields, receipts and scratch, and supplies the actual EOS
 * and physical bounds. This leaf returns only a source candidate and status.
 */
#pragma once

#include <array>
#include <cmath>

#include "core/ArchPortability.h"
#include "data/FluidState.h"
#include "grid/GridMetrics.h"
#include "numerics/state/RzNativeClosure.h"
#include "physics/gravity/GravitySource.h"

namespace Physical::Gravity {

/** Exact failure stages of the shared one-cell native external evaluation.
 * The candidate explicitly defaults to the original first closure failure;
 * no status normalization is applied.
 */
enum class NativeExternalCellStatus {
    Valid, InputClosureInvalid, PhysicalPointInvalid, IntegralUnrepresentable, CandidateNonfinite
};

/** One evaluated cell: the same native external source increment that the
 * original Host loop stored, plus the status that selected it. Nothing else is
 * published, so the caller still owns fields, receipts and partial sums.
 */
struct NativeExternalCellCandidate {
    FluidVector source{};
    NativeExternalCellStatus status{NativeExternalCellStatus::InputClosureInvalid};
};

/** Evaluate the exact existing native external source of one complete cell.
 * 1. Build the same native closure with RzThermodynamics::make_cell on the
 *    caller's read/index/geometry; an invalid closure returns
 *    InputClosureInvalid before any buffer is touched.
 * 2. Only then fill composition[s]=fraction(s,index) with the same cell
 *    fractions; no normalization or positivity repair is allowed.
 * 3. Sample the original eight Gauss nodes from the actual geometry face
 *    positions, form the same physical baseline base_point at each node radius
 *    and require arch::state::validate_eos in the original node order.
 * 4. Call the same native_external_source_mean on those samples, the external
 *    view and dt. Its source is already dt weighted:
 *    dt*(rho*g_r, rho*g_z, rho*g_phi) and dt*rho*(u_r*g_r+u_z*g_z+u_phi*g_phi),
 *    integrated with the shared V/W weights. A non-valid or nonzero-rho source
 *    is the existing integral representability failure, together with the same
 *    finite(delta) check on the caller's actual dU entry.
 * 5. Apply exactly the same rounded additions to delta and require a finite
 *    candidate, which is the original last check of the first Host loop.
 *
 * Returned: the one cell source and the first exact failure status. No field,
 * scratch, receipt or partial sum is written and composition is the only caller
 * buffer filled. No threshold, EOS approximation, reweighting, status
 * normalization or compiler relaxation is introduced. The caller establishes
 * the valid patch layout and the exact eligible indices.
 */
template<class Read,class Fraction,class Eos>
ARCH_INLINE NativeExternalCellCandidate evaluate_native_external_source_cell(Read read,
    Fraction fraction,int index,int species,const Eos& eos,
    const GridMetrics::GeometryView& geometry,int i,int j,double dt,
    const arch::state::Bounds& bounds,double* composition,ExternalGravityView external,
    const FluidVector& delta)
{
    NativeExternalCellCandidate result{};
    const auto cell=RzThermodynamics::make_cell(read,index,geometry,i,bounds);
    if(!cell.valid()) {
        result.status=NativeExternalCellStatus::InputClosureInvalid;
        return result;
    }
    for(int s=0;s<species;++s)composition[s]=fraction(s,index);
    const auto samples=GridMetrics::Rz::CellAverageSamples(
        geometry.GetFacePosL(i),geometry.GetFacePosR(i),
        geometry.GetAxialFacePosL(j),geometry.GetAxialFacePosR(j));
    std::array<FluidVector,8> points;
    for(std::size_t node=0;node<points.size();++node) {
        points[node]=RzThermodynamics::base_point(cell,samples[node].radius);
        if(arch::state::validate_eos(points[node],composition,species,bounds,eos)
            !=arch::state::Status::valid) {
            result.status=NativeExternalCellStatus::PhysicalPointInvalid;
            return result;
        }
    }
    const auto finite=[](const FluidVector& u) {
        return std::isfinite(u.rho)&&std::isfinite(u.mom_u)&&std::isfinite(u.mom_v)
            &&std::isfinite(u.mom_w)&&std::isfinite(u.eng);
    };
    const auto source=native_external_source_mean(samples,
        [&](std::size_t node){return points[node];},external,dt);
    if(!source.valid()||source.value.rho!=0.||!finite(delta)) {
        result.status=NativeExternalCellStatus::IntegralUnrepresentable;
        return result;
    }
    auto candidate=delta;
    candidate.mom_u+=source.value.mom_u;candidate.mom_v+=source.value.mom_v;
    candidate.mom_w+=source.value.mom_w;candidate.eng+=source.value.eng;
    if(!finite(candidate)) {
        result.status=NativeExternalCellStatus::CandidateNonfinite;
        return result;
    }
    result.source=source.value;
    result.status=NativeExternalCellStatus::Valid;
    return result;
}


/** Accumulate measured rounded body-source additions with the actual V/V/W
 * measures. Host uses its existing longdouble observer; Device uses FP64 compact
 * rows. The conserved update and RK weight remain outside this shared leaf.
 * Formula: (Q_r,Q_z,Q_J,Q_E) += (V*delta Pr,V*delta Pz,W*delta m_phi,V*delta E).
 */
template<class Accumulator>
ARCH_INLINE void accumulate_native_external_source_budget(
    Accumulator& radial,Accumulator& axial,Accumulator& torque,Accumulator& work,
    const FluidVector& before,const FluidVector& after,
    const GridMetrics::GeometryView& geometry,int i,int j)
{
    const Accumulator volume=GridMetrics::CellVolume(geometry,i,j,0);
    const Accumulator angular=GridMetrics::Rz::AngularMomentumMeasure(geometry,i,j);
    radial+=volume*(static_cast<Accumulator>(after.mom_u)-before.mom_u);
    axial+=volume*(static_cast<Accumulator>(after.mom_v)-before.mom_v);
    torque+=angular*(static_cast<Accumulator>(after.mom_w)-before.mom_w);
    work+=volume*(static_cast<Accumulator>(after.eng)-before.eng);
}

} // namespace Physical::Gravity
