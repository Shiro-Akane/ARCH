/**
 * @file DriverBoundaryDiagnostics.cpp
 * @brief Integrate actual domain-face fluxes without changing evolved fields.
 *
 * Workflow:
 * 1. Allocate actual physical-surface observers for native RZ or selected callbacks.
 * 2. Set weights from the existing RK/RKL descriptors before face evaluation.
 * 3. Integrate ordinary fields with actual face area, and native phi with torque measure.
 * 4. Accumulate Hydro quadrature and the unchanged RKL accounting recurrence.
 *
 * B_out = integral(dt * sum_faces A * F_out); native phi uses integral r dA
 * instead of A. Its captured F_W is already a normalized face average, so
 * this owner applies the sole torque measure once. Positive entries leave the
 * domain. Momentum entries are native components; curved-basis source terms
 * must be accounted for separately. Totals start at this process, including
 * when the simulation continues a checkpoint. No field repair occurs here.
 */
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <string_view>

#include "amr/AMRControl.h"
#include "driver/runtime/DriverRuntime.h"
#include "grid/GridMetrics.h"
#include "numerics/diffusion/DiffFunction.h"
#include "physics/boundary/BoundaryDiagnostics.h"
#include "physics/boundary/UserBoundary.h"
#include "physics/species/Species.h"

namespace arch::driver {
namespace {
/** Test a block edge against the corresponding immutable domain edge. */
bool domain_edge(double block, double domain) {
    const double scale = std::max(std::abs(block), std::abs(domain));
    return std::abs(block-domain) <= 32.*std::numeric_limits<double>::epsilon()*scale;
}
/** Accumulate measured device observer traffic; these are real counter differences. */
void add_counters(backend::BackendCounters& total,const backend::BackendCounters& old,
                  const backend::BackendCounters& next) {
    total.kernel_count+=next.kernel_count-old.kernel_count;
    total.bytes_h2d+=next.bytes_h2d-old.bytes_h2d;
    total.bytes_d2h+=next.bytes_d2h-old.bytes_d2h;
    total.stream_sync_count+=next.stream_sync_count-old.stream_sync_count;
    total.getter_count+=next.getter_count-old.getter_count;
}

}

/** Rebuild surface ownership on AMR epoch changes; stage slots share the observer. */
void DriverRuntime::prepare_boundary_capture(double weight, double initial_weight, bool save_initial) {
    const auto& active = amr_ctrl.tree->GetActiveBlocks();
    if (active.size()!=stage_handles.size() || active.empty())
        throw std::logic_error("Boundary accounting requires a complete active topology");
    const int fields=6+specs.count();
    if (boundary_budget_epoch_!=stage_handles.front().epoch) {
        boundary_surface_layout_.clear();
        const double domain_lower[]{config.grid.x1_min,config.grid.x2_min,config.grid.x3_min};
        const double domain_upper[]{config.grid.x1_max,config.grid.x2_max,config.grid.x3_max};
        const std::string_view names[]{config.grid.x1l_boundary_type,config.grid.x1r_boundary_type,
            config.grid.x2l_boundary_type,config.grid.x2r_boundary_type,
            config.grid.x3l_boundary_type,config.grid.x3r_boundary_type};
        for (std::size_t b=0;b<active.size();++b) {
            auto& block=amr_ctrl.pool->GetBlock(active[b]);
            const auto& grid=block.grid;
            auto observer=std::make_shared<boundary::BoundaryFluxCaptureStorage>();
            backend::BoundaryFluxPlanes layout; layout.block=stage_handles[b];
            const int size[]{grid.Ie()-grid.Is(),grid.Je()-grid.Js(),grid.Ke()-grid.Ks()};
            const double lower[]{grid.x1_min,grid.x2_min,grid.x3_min};
            const double upper[]{grid.x1_max,grid.x2_max,grid.x3_max};
            for (int axis=0;axis<grid.dim;++axis) for (int side=0;side<2;++side) {
                const int face=2*axis+side;
                if (names[face]=="periodic" || !domain_edge(side?upper[axis]:lower[axis],
                    side?domain_upper[axis]:domain_lower[axis])) continue;
                const auto count=static_cast<std::size_t>(size[(axis+1)%3])*size[(axis+2)%3]*fields;
                observer->stage[face].assign(count,0.); observer->initial[face].assign(count,0.);
                layout.stage[face].resize(count); layout.initial[face].resize(count);
            }
            block.fluid_state.boundary_flux_capture=observer;
            block.state_next.boundary_flux_capture=observer;
            block.state_scratch.boundary_flux_capture=observer;
            boundary_surface_layout_.push_back(std::move(layout));
        }
        boundary_budget_epoch_=stage_handles.front().epoch;
    }
    for (const int id:active) {
        auto& observer=*amr_ctrl.pool->GetBlock(id).fluid_state.boundary_flux_capture;
        observer.weight=weight; observer.initial_weight=initial_weight; observer.save_initial=save_initial;
        for (auto& plane:observer.stage) std::fill(plane.begin(),plane.end(),0.);
        if (save_initial) for (auto& plane:observer.initial) std::fill(plane.begin(),plane.end(),0.);
    }
    if (compute_backend) {
        const auto before=compute_backend->counters();
        compute_backend->configure_boundary_flux_capture(boundary_surface_layout_,weight,initial_weight,save_initial);
        add_counters(boundary_observer_operations_,before,compute_backend->counters());
    }
}

/** Sum physical-surface rows in fixed block/axis/side/tangential-cell order. */
std::vector<double> DriverRuntime::integrate_boundary_capture() {
    const int fields=6+specs.count();
    const bool native_rz=geometry_semantics()==GridMetrics::GeometrySemantics::AxisymmetricRz;
    std::vector<double> result(fields,0.);
    std::vector<backend::BoundaryFluxPlanes> device_planes;
    std::map<amr::BlockHandle,const backend::BoundaryFluxPlanes*> device_by_block;
    if (compute_backend) {
        const auto before=compute_backend->counters();
        device_planes=compute_backend->download_boundary_flux_capture();
        add_counters(boundary_observer_operations_,before,compute_backend->counters());
        for (const auto& planes:device_planes)
            if (!device_by_block.emplace(planes.block,&planes).second)
                throw std::logic_error("Duplicate CUDA boundary accounting block");
        if (device_by_block.size()!=stage_handles.size())
            throw std::logic_error("Incomplete CUDA boundary accounting topology");
    }
    const auto& active=amr_ctrl.tree->GetActiveBlocks();
    for (std::size_t b=0;b<active.size();++b) {
        const auto& block=amr_ctrl.pool->GetBlock(active[b]); const auto& grid=block.grid;
        // Explicit Runtime semantics match the producer; a cylindrical geometry
        // string alone must never switch the ordinary polar-plane observer.
        GridMetrics::GeometryView native_geometry{};
        if(native_rz)native_geometry=GridMetrics::make_geometry_view(grid,geometry_semantics());
        const auto& planes=compute_backend ? device_by_block.at(stage_handles[b])->stage
            : block.fluid_state.boundary_flux_capture->stage;
        const int lower[]{grid.Is(),grid.Js(),grid.Ks()}, upper[]{grid.Ie(),grid.Je(),grid.Ke()};
        for (int axis=0;axis<grid.dim;++axis) for (int side=0;side<2;++side) {
            const int face=2*axis+side, a=(axis+1)%3, c=(axis+2)%3;
            if (planes[face].size()!=boundary_surface_layout_[b].stage[face].size())
                throw std::logic_error("Boundary accounting face shape differs from topology");
            if (planes[face].empty()) continue;
            int cell[]{lower[0],lower[1],lower[2]}; cell[axis]=side?upper[axis]-1:lower[axis];
            std::size_t row=0;
            for (cell[c]=lower[c];cell[c]<upper[c];++cell[c])
                for (cell[a]=lower[a];cell[a]<upper[a];++cell[a],++row) {
                    const double area=native_rz
                        ?GridMetrics::FaceArea(native_geometry,axis,cell[0],cell[1],cell[2],side!=0)
                        :GridMetrics::FaceArea(grid,axis,cell[0],cell[1],cell[2],side!=0);
                    if (!std::isfinite(area) || area<0.) throw std::logic_error("Invalid boundary accounting face area");
                    const double measure=(side?1.:-1.)*area;
                    // F_phi is physical on r faces and W-normalized on z
                    // faces. J_out=sum integral(r dA)*F_phi, not A*F_phi;
                    // energy/species/heat retain their actual V face measure.
                    const double torque=native_rz
                        ?GridMetrics::Rz::FaceTorqueMeasure(native_geometry,axis,cell[0],cell[1],side!=0)
                        :0.;
                    if(native_rz&&(!std::isfinite(torque)||torque<0.))
                        throw std::logic_error("Invalid boundary accounting torque measure");
                    const double signed_torque=(side?1.:-1.)*torque;
                    for (int field=0;field<fields;++field) {
                        const double value=planes[face][row*fields+field];
                        if (!std::isfinite(value)) throw std::runtime_error("Nonfinite actual boundary flux");
                        if(native_rz&&field==3)result[field]+=signed_torque*value;
                        else result[field]+=measure*value;
                    }
                }
        }
    }
    return result;
}

/** Attach actual surface accounting: native RZ always observes built-in faces;
 * ordinary cases retain the existing selected-callback activation condition. */
void DriverRuntime::bind_boundary_accounting(scheduler::StageExecutionContext& context) {
    if(runtime_state_transaction_)throw std::logic_error("Boundary accounting must bind before Host Hydro transaction");
    const auto* selected=boundary::CurrentUserBoundaries();
    const bool native_rz=geometry_semantics()==GridMetrics::GeometrySemantics::AxisymmetricRz;
    if (!native_rz && (!selected || (!selected->callbacks.physical && !selected->callbacks.gravity))) return;
    const int fields=6+specs.count();
    if (hydro_boundary_budget_.empty()) {
        hydro_boundary_budget_.assign(fields,0.); diffusion_boundary_budget_.assign(fields,0.);
    }
    context.hydro_flux_capture_begin=[this](const scheduler::StageDescriptor& stage) {
        prepare_boundary_capture(stage.flux_register_weight,0.,false);
    };
    context.hydro_flux_capture_accept=[this,&context](const scheduler::StageDescriptor&) {
        const auto rate=integrate_boundary_capture();
        auto& budget=tentative_hydro_boundary_budget_ ? *tentative_hydro_boundary_budget_ : hydro_boundary_budget_;
        if(budget.size()!=rate.size())throw std::logic_error("Hydro boundary receipt extent changed");
        for (std::size_t k=0;k<rate.size();++k) budget[k]+=context.step_dt*rate[k];
    };
    context.rkl_flux_capture_begin=[this](const scheduler::RklStageDescriptor& stage,const scheduler::RklPlan& plan) {
        if (stage.stage==1) {
            boundary_rkl_previous_.assign(6+specs.count(),0.); boundary_rkl_older_=boundary_rkl_previous_;
        }
        const auto coefficients=DiffFunction::get_rkl_coeffs(plan.second_order ? DiffFunction::RKLOrder::Second
            : DiffFunction::RKLOrder::First,stage.stage,static_cast<int>(plan.stages.size()));
        prepare_boundary_capture(coefficients.tilde_mu,plan.second_order && stage.stage>1 ? coefficients.gamma : 0.,
            plan.second_order && stage.stage==1);
    };
    context.rkl_flux_capture_accept=[this,&context](const scheduler::RklStageDescriptor& stage,const scheduler::RklPlan& plan) {
        auto budget=integrate_boundary_capture();
        const auto coefficients=DiffFunction::get_rkl_coeffs(plan.second_order ? DiffFunction::RKLOrder::Second
            : DiffFunction::RKLOrder::First,stage.stage,static_cast<int>(plan.stages.size()));
        // Bj = mu*B(j-1)+nu*B(j-2)+dt*Rj; B0=0. Rj already
        // contains tilde_mu*F(Yprev)+gamma*F(Y0). This only observes the
        // existing recurrence; it never changes an STS state or coefficient.
        for (std::size_t k=0;k<budget.size();++k) budget[k]=context.boundary_step_dt*budget[k]
            +(stage.stage>1 ? coefficients.mu*boundary_rkl_previous_[k]+coefficients.nu*boundary_rkl_older_[k] : 0.);
        boundary_rkl_older_=boundary_rkl_previous_; boundary_rkl_previous_=budget;
        if (stage.stage==static_cast<int>(plan.stages.size())) {
            // Both RKL halves remain tentative inside the native macro owner;
            // the unchanged recurrence above still integrates each dt/2.
            auto& accepted=tentative_diffusion_boundary_budget_
                ?*tentative_diffusion_boundary_budget_:diffusion_boundary_budget_;
            if(accepted.size()!=budget.size())
                throw std::logic_error("RKL boundary receipt owner layout mismatch");
            for (std::size_t k=0;k<budget.size();++k) accepted[k]+=budget[k];
        }
    };
}
} // namespace arch::driver
