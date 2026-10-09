/**
 * @file NativeActiveAmrWitness.h
 * @brief Two true warm four-module macros separated by a dynamic native regrid.
 *
 * Workflow:
 * 1. Use the maintained activity owner with two actual roots and the frozen
 *    density bump. M0 executes the unchanged two burn/two thermal/RK2 split.
 * 2. Flush its accepted journal, invalidate its source and destroy the old
 *    borrowed context before the genuine Runtime DENS topology transaction.
 * 3. Verify conserved V/W totals, the new UID/epoch/logical domain and actual
 *    CF plan. Rebind BC/EOS/accounting/RKL callbacks to the published ledger.
 * 4. Solve AcceptedCurrent once at the real controller time, with no fictitious
 *    dt=0 Hydro stage; record its separate stage0 journal and actual proof.
 * 5. M1 consumes two newly prepared RK2 fields and every original CF Energy
 *    registration. Each macro uses differences of cumulative boundary budgets.
 *
 * This private activity/identity/accounting witness is not a continuum field,
 * gravitational total-energy, restart, long-time or Device qualification.
 */
#pragma once

#include <chrono>
#include <set>

#include "host/gravity/NativeActiveFourModuleWitness.h"

#include "amr/elliptic/EllipticMeshAdapter.h"

namespace native_active_four_module {

/** Locate a real selected slot; an unknown enum never falls back to Current. */
inline const FluidState& selected_fluid(const amr::Block& block,state::StateSlot slot) {
    switch(slot) {
    case state::StateSlot::Current:return block.fluid_state;
    case state::StateSlot::Next:return block.state_next;
    case state::StateSlot::Scratch:return block.state_scratch;
    default:throw std::logic_error("Dynamic activity source has an unknown state slot");
    }
}

/** Copy all three actual solve phase times without another field solve.
 * Their sum omits any work outside the producer's existing timed regions.
 */
inline std::array<double,3> solve_times(const Physical::Gravity::SelfGravity& gravity) {
    const auto& t=gravity.timings(Physical::Gravity::GravityFieldScope::NativeRzCandidate);const std::array<double,3> result{t.source_boundary,t.poisson,t.force};
    for(double value:result)require(std::isfinite(value)&&value>=0.,"Actual solve timing is invalid");
    return result;
}

/** Authenticate every observer against the actual immutable stage input domain.
 * Before/after work and reserved/applied Energy rows are separate obligations;
 * copied counters do not stand in for the production receipt's own acceptance.
 */
inline void Owner::observe_dynamic_source(const Physical::Gravity::NativeSelfStageObservation& event) {
    using Kind=Physical::Gravity::NativeSelfStageObservation::Kind;
    require(event.kind==Kind::AxisBefore||event.kind==Kind::AxisAfter
        ||event.kind==Kind::EnergyReserved||event.kind==Kind::EnergyApplied,
        "Dynamic source observer received an unknown operation");
    require(event.grid&&event.input&&event.descriptor&&event.source&&event.axis>=0&&event.axis<2
        &&runtime->active_runtime_state_transaction()&&bits(event.dt,context->step_dt)
        &&event.generation>0&&event.source_generation>0&&event.field_generation>0,
        "Dynamic source lost its real field/transaction/dt owner");
    const auto plan=scheduler::make_hydro_plan(scheduler::HydroMethod::RK2);
    const int stage=event.descriptor->stage;
    require(stage>=1&&stage<=2&&scheduler::same_stage_descriptor(*event.descriptor,plan.stages[stage-1])
        &&bits(event.stage_weight,plan.stages[stage-1].flux_register_weight)
        &&bits(event.source->input_time,context->step_start_time+event.descriptor->input_time_fraction*context->step_dt)
        &&event.source->topology==context->ledger.active_epoch()
        &&event.source->inputs.size()==runtime->handles().size(),
        "Dynamic source changed RK2 weights, input time or topology");
    Physical::Gravity::validate_gravity_solve_identity(*event.source);
    const auto& active=control.tree->GetActiveBlocks();
    const auto found=std::find(active.begin(),active.end(),event.block_id);
    require(found!=active.end()&&active.size()==runtime->handles().size(),
        "Dynamic source used a nonactive patch or incomplete handle domain");
    const auto& block=control.pool->GetBlock(event.block_id);block.RequireNativeGeometryIdentity();
    require(&block.grid==event.grid&&&selected_fluid(block,event.descriptor->input_slot)==event.input,
        "Dynamic source observer crossed actual patch/slot storage");
    for(std::size_t n=0;n<event.source->inputs.size();++n) {
        const auto& input=event.source->inputs[n];
        const auto stamp=context->ledger.inspect({runtime->handles()[n],event.descriptor->input_slot});
        require(input.block==runtime->handles()[n]&&input.slot==event.descriptor->input_slot
            &&input.version==stamp.interior.version&&input.version.value>0
            &&input.storage_generation==event.generation,
            "Dynamic source did not consume the true selected slot publication");
        context->ledger.require_readable({input.block,input.slot},
            {state::ExecutionSide::Host,input.version,true,true});
    }
    const auto topology=control.FluxTopologyPlanLease();
    require(topology&&topology->epoch==event.source->topology&&topology->semantics==native
        &&topology->angular_transport&&topology->species_count==species_count,
        "Dynamic source lost the actual native flux topology lease");
    const auto times=solve_times(*gravity);
    const auto ring=gravity->ring_memo_observations(Physical::Gravity::GravityFieldScope::NativeRzCandidate);
    require(ring.epoch==event.source->topology.value&&ring.source_generation==event.source_generation
        &&ring.field_generation==event.field_generation,"Dynamic ring observations changed actual field identity");
    std::lock_guard<std::mutex> lock(source_mutex);
    auto [it,inserted]=sources.try_emplace(stage);auto& visit=it->second;
    if(inserted) {
        visit.descriptor=*event.descriptor;visit.field_generation=event.field_generation;
        visit.source_generation=event.source_generation;visit.input_time=event.source->input_time;
        visit.source=*event.source;visit.lease=event.generation;
        visit.topology_fingerprint=topology->fingerprint;visit.solver_seconds=times;visit.ring_memo=ring;
    }
    require(visit.source==*event.source&&visit.lease==event.generation
        &&visit.field_generation==event.field_generation&&visit.source_generation==event.source_generation
        &&visit.topology_fingerprint==topology->fingerprint,
        "Dynamic operation crossed field/source/topology generations");
    require(visit.ring_memo==ring,"Repeated same-field observer changed actual ring observations");
    for(int k=0;k<3;++k)require(bits(visit.solver_seconds[k],times[k]),
        "Repeated same-field observer changed its actual solve timing");
    if(event.kind==Kind::AxisBefore||event.kind==Kind::AxisAfter) {
        require(event.delta&&event.flux&&event.delta->size()==static_cast<std::size_t>(event.grid->GetTotalSize())
            &&event.flux->size()==event.delta->size(),"Dynamic work observer has invalid real arrays");
        auto& counts=event.kind==Kind::AxisBefore?visit.patch_before[event.block_id]:visit.patch_axes[event.block_id];
        require(++counts[event.axis]==1,"Dynamic patch work axis was skipped/repeated");
        if(event.kind==Kind::AxisAfter) {
            ++visit.axes[event.axis];
            for(const auto& value:*event.delta)
                require(std::isfinite(value.rho)&&std::isfinite(value.mom_u)&&std::isfinite(value.mom_v)
                    &&std::isfinite(value.mom_w)&&std::isfinite(value.eng),"Dynamic source work is nonfinite");
        }
        return;
    }
    const auto* route=topology->find(event.block_id,event.axis);
    require(event.row&&event.route&&route==event.route&&event.operation_index<route->plan.operations.size(),
        "Dynamic Energy observation is not an actual registration route");
    const auto& operation=route->plan.operations[event.operation_index];const auto& row=*event.row;
    require(operation.field==amr::AmrField::Energy&&row.operation==operation&&row.route==route->key
        &&row.epoch==topology->epoch&&row.topology_fingerprint==topology->fingerprint
        &&row.route_fingerprint==route->plan.fingerprint&&row.operation_index==event.operation_index
        &&row.source_flux_offset==event.flux_index&&row.coarse_cell_index>=0&&event.flux_index>=0,
        "Dynamic Energy observation changed the original operation or fragment row");
    for(double value:{event.FE,event.Frho,event.psi,event.result})
        require(std::isfinite(value),"Dynamic Energy observation has nonfinite carrier data");
    const auto key=std::tuple{event.block_id,event.axis,event.operation_index};
    auto& registration=visit.registration[key];
    if(event.kind==Kind::EnergyReserved) {
        require(++registration.reserved==1&&registration.applied==0,"Dynamic Energy reservation was repeated/out of order");
        registration.row=row;registration.flux_index=event.flux_index;
        registration.FE=event.FE;registration.Frho=event.Frho;registration.psi=event.psi;registration.result=event.result;
    } else {
        require(registration.reserved==1&&++registration.applied==1&&registration.row.operation==row.operation
            &&registration.flux_index==event.flux_index&&bits(registration.FE,event.FE)
            &&bits(registration.Frho,event.Frho)&&bits(registration.psi,event.psi)&&bits(registration.result,event.result),
            "Dynamic Energy application did not match its actual reservation");
    }
}

/** Join only after the true macro accepts, using every actual topology operation.
 * No required CF operation is inferred from a theoretical fragment count.
 */
inline void Owner::complete_dynamic_sources() {
    require(sources.size()==2&&sources.at(1).field_generation!=sources.at(2).field_generation,
        "Dynamic RK2 omitted a distinct actual solved field");
    const auto topology=control.FluxTopologyPlanLease();
    require(bool(topology),"Dynamic accepted macro lost its real topology plan");
    const auto& active=control.tree->GetActiveBlocks();
    std::size_t cells=0;for(int id:active) {
        const auto& g=control.pool->GetBlock(id).grid;
        cells+=static_cast<std::size_t>(g.Ie()-g.Is())*(g.Je()-g.Js());
    }
    for(int stage=1;stage<=2;++stage) {
        const auto& visit=sources.at(stage);
        require(visit.patch_axes.size()==active.size()&&visit.patch_before.size()==active.size()
            &&visit.axes==std::array<int,2>{static_cast<int>(active.size()),static_cast<int>(active.size())},
            "Dynamic actual source patch/axis coverage is incomplete");
        for(int id:active)require(visit.patch_axes.at(id)==std::array<int,2>{1,1}
            &&visit.patch_before.at(id)==std::array<int,2>{1,1},"Dynamic patch skipped/repeated real work");
        std::size_t energy_count=0;
        for(const auto& route:topology->routes)for(std::size_t n=0;n<route.plan.operations.size();++n) {
            const auto& operation=route.plan.operations[n];if(operation.field!=amr::AmrField::Energy)continue;
            const auto key=std::tuple{route.key.source_block,static_cast<int>(route.key.axis),n};
            const auto& record=visit.registration.at(key);
            require(record.reserved==1&&record.applied==1&&record.row.operation==operation,
                "Dynamic macro omitted an original CF Energy operation");++energy_count;
        }
        require(visit.registration.size()==energy_count,"Dynamic macro published a foreign CF Energy operation");
        accepted_journal.push_back({stage,visit.input_time,visit.source.topology.value,visit.lease,
            visit.source_generation,cells});
    }
}

/** Verify the actual retained journal, including the distinct AcceptedCurrent row.
 * The Stage object is retained across regrid so no previous accepted row is lost.
 */
inline void Owner::check_dynamic_journal() {
    std::ifstream file(config.io.out_dir+"/native_rz_candidates.tsv");std::string line;
    require(bool(std::getline(file,line)),"Dynamic journal header is missing");std::size_t count=0;
    while(std::getline(file,line))if(!line.empty()) {
        require(count<accepted_journal.size(),"Dynamic journal has an invented stage");
        const auto& expected=accepted_journal[count++];std::istringstream row(line);
        double time=0.,residual=0.,safe=0.;int stage=-1,physical=-1;
        std::uint64_t epoch=0,lease=0,source=0;std::size_t cells=0;
        require(bool(row>>time>>stage>>epoch>>lease>>cells>>source>>residual>>safe>>physical)
            &&stage==expected.stage&&bits(time,expected.time)&&epoch==expected.epoch&&lease==expected.lease
            &&cells==expected.cells&&source==expected.source_generation&&std::isfinite(residual)&&residual>=0.
            &&std::isfinite(safe)&&safe>0.&&residual<=safe&&physical==0,
            "Dynamic journal changed actual purpose/time/domain/error semantics");
    }
    require(count==accepted_journal.size(),"Dynamic journal omitted an accepted field");
}

/** Immutable copies of already accepted boundary accumulators; never reset them. */
struct BoundaryBefore {std::vector<double> hydro,diffusion;};
/** Freeze the actual cumulative accounting used to form this macro's differences. */
inline BoundaryBefore boundary_before(const Owner& owner) {
    BoundaryBefore result{owner.runtime->hydro_boundary_budget(),owner.runtime->diffusion_boundary_budget()};
    require(result.hydro.size()==6+species_count&&result.diffusion.size()==result.hydro.size(),
        "Dynamic boundary accounting has invalid dimensions");
    for(double value:result.hydro)require(std::isfinite(value),"Dynamic initial Hydro budget is nonfinite");
    for(double value:result.diffusion)require(std::isfinite(value),"Dynamic initial thermal budget is nonfinite");
    return result;
}
/** Original normalized macro M/J/species balances, with actual half ENUC and rhoX.
 * E is reported only; no new total self-gravity energy scientific tolerance exists.
 */
inline void check_macro_balance(Owner& owner,const Totals& before,const BoundaryBefore& initial,const Totals& after) {
    const auto final=boundary_before(owner);
    const auto flux=[&](int f){return (static_cast<long double>(final.hydro[f])-initial.hydro[f])
        +(static_cast<long double>(final.diffusion[f])-initial.diffusion[f]);};
    const long double allowance=integral_budget*std::max(1.L,std::abs(before.mass));
    require(std::isfinite(allowance)&&std::isfinite(after.mass)&&std::isfinite(after.angular)
        &&std::isfinite(after.energy),"Dynamic balance produced a nonfinite scale/state");
    require(std::abs(after.mass-before.mass+flux(0))<=allowance
        &&std::abs(after.angular-before.angular+flux(3))<=allowance,
        "Dynamic warm macro violated the original normalized M/J balance");
    for(int s=0;s<species_count;++s) {
        const long double burn=owner.burns[0].species_delta[s]+owner.burns[1].species_delta[s];
        require(std::isfinite(after.species[s])&&std::abs(after.species[s]-before.species[s]-burn+flux(5+s))<=allowance,
            "Dynamic warm macro lost actual burn/transport species balance");
    }
}
/** Check regrid's actual native conserved totals without an energy source/transaction. */
inline void check_regrid_balance(const Totals& before,const Totals& after) {
    const auto check=[](long double a,long double b) {
        require(std::isfinite(a)&&std::isfinite(b),"Regrid integral is nonfinite");
        const long double allowance=integral_budget*std::max(1.L,std::abs(a));
        require(std::isfinite(allowance)&&std::abs(b-a)<=allowance,
            "Dynamic regrid violated the original conserved integral budget");
    };
    check(before.mass,after.mass);check(before.angular,after.angular);check(before.energy,after.energy);
    for(int s=0;s<species_count;++s)check(before.species[s],after.species[s]);
}

/** Preserve the true active seven-array values across the diagnostic Current solve.
 * Ghost refresh/ledger completions are separately authenticated by the Runtime.
 */
inline void require_active_unchanged(Owner& owner,const std::vector<CellBefore>& before) {
    for(const auto& old:before) {
        const auto& fluid=owner.recorded_block(old).fluid_state;const auto value=fluid.get(old.index);
        require(bits(value.rho,old.fluid.rho)&&bits(value.mom_u,old.fluid.mom_u)&&bits(value.mom_v,old.fluid.mom_v)
            &&bits(value.mom_w,old.fluid.mom_w)&&bits(value.eng,old.fluid.eng)&&bits(fluid.enuc_rate[old.index],old.enuc),
            "AcceptedCurrent diagnostic changed the actual fluid/ENUC state");
        for(int s=0;s<species_count;++s)require(bits(fluid.X(s,old.index),old.fractions[s]),
            "AcceptedCurrent diagnostic changed the actual composition");
    }
}

/** Print only copied scalar observations associated with a genuinely accepted field.
 * Workflow: authentic preparation/consumers capture -> original macro/Current
 * checks accept -> publish one compact line and flush it before later work.
 * The ring counter counts certified kernel enclosures, not CUDA/backend launches.
 * This function performs no scientific work, interval lookup or field transfer.
 */
inline void print_ring_observations(const Physical::Gravity::SelfGravity::RingMemoObservations& ring,
    int stage,double time,const char* purpose) {
    std::cout<<"NATIVE_ACTIVE_RING_MEMO purpose="<<purpose<<" stage="<<stage<<" epoch="<<ring.epoch
        <<" source_generation="<<ring.source_generation<<" field_generation="<<ring.field_generation
        <<" time="<<time<<" cells="<<ring.cells<<" boundary_faces="<<ring.boundary_faces
        <<" memo_hits="<<ring.memo_hits<<" memo_misses="<<ring.memo_misses
        <<" memo_admissions="<<ring.memo_admissions<<" entries="<<ring.entries<<" capacity="<<ring.capacity
        <<" current_call_kernel_enclosures="<<ring.current_call_kernel_enclosures
        <<" current_call_range_evaluations="<<ring.current_call_range_evaluations
        <<" current_call_agm_iterations="<<ring.current_call_agm_iterations
        <<" accepted_field_observation=1 physical_qualified=0"<<std::endl;
}

/** Publish compact actual half/stage evidence only after that macro's checks.
 * Actual RKL work remains distinct from the immutable thermal input flux probe.
 */
inline void report_macro(const Owner& owner,int macro,double seconds) {
    for(int h=0;h<2;++h) {
        const auto& b=owner.burns[h];const auto& d=owner.diffusions[h];
        std::cout<<"NATIVE_ACTIVE_AMR_HALF macro="<<macro<<" half="<<h+1<<" burn_calls="<<b.calls
            <<" changed_composition_cells="<<b.changed_composition<<" fresh_enuc_cells="<<b.nonzero_enuc
            <<" max_abs_enuc="<<b.maximum_enuc<<" Q_actual_half="<<b.nuclear_integral
            <<" delta_E_burn="<<b.delta_energy<<" rhoX_change_L1="<<b.composition_l1
            <<" thermal_probe_max="<<d.maximum_flux_probe<<" diffusion_changed_E_cells="<<d.changed_energy
            <<" rkl2_begin="<<d.rkl_begin<<" rkl2_accept="<<d.rkl_accept<<'\n';
    }
    for(const auto& [stage,visit]:owner.sources) {
        print_ring_observations(visit.ring_memo,stage,visit.input_time,"HydroStage");
        std::cout<<"NATIVE_ACTIVE_AMR_SOURCE macro="<<macro<<" stage="<<stage<<" epoch="<<visit.source.topology.value
            <<" lease="<<visit.lease<<" field_generation="<<visit.field_generation
            <<" source_generation="<<visit.source_generation<<" time="<<visit.input_time
            <<" patches="<<visit.patch_axes.size()<<" radial_axes="<<visit.axes[0]<<" axial_axes="<<visit.axes[1]
            <<" energy_operations="<<visit.registration.size()<<" topology_fingerprint="<<visit.topology_fingerprint
            <<" source_boundary_seconds="<<visit.solver_seconds[0]<<" poisson_seconds="<<visit.solver_seconds[1]
            <<" force_seconds="<<visit.solver_seconds[2]<<std::endl;
    }
    std::cout<<"NATIVE_ACTIVE_AMR_MACRO macro="<<macro<<" wall_seconds="<<seconds
        <<" accepted_time="<<owner.controller->t_current<<" accepted_steps="<<owner.controller->step_count<<std::endl;
}

/** Genuine five-field dynamic activity entry; no checkpoint or public science grant. */
inline void advance_dynamic_to_m1(Owner& owner) {
    using Clock=std::chrono::steady_clock;
    require(owner.dynamic&&!owner.config.io.restart,"Dynamic M0 requires a real new activity owner");
    std::cout<<std::setprecision(17);
    require(owner.runtime->handles().size()==2&&owner.freeze_active().size()==512,
        "Dynamic warm M0 has an incorrect real root domain");
    const auto m0=owner.totals();const auto b0=boundary_before(owner);const auto started=Clock::now();
    owner.advance();const auto m0_after=owner.totals();
    check_macro_balance(owner,m0,b0,m0_after);
    report_macro(owner,0,std::chrono::duration<double>(Clock::now()-started).count());
    const auto boundaries_after_m0=boundary_before(owner);
    const auto old_handles=owner.runtime->handles();const auto old_epoch=owner.context->ledger.active_epoch();
    require(!owner.runtime->active_runtime_state_transaction()&&!owner.gravity->prepared_native_self(),
        "Dynamic regrid overlaps a real source or fluid transaction");
    owner.gravity_stage->flush_committed_diagnostics();owner.gravity_stage->invalidate();
    // A context borrows its ledger: destroy it before the true regrid replaces it.
    owner.context.reset();owner.bc->configure_stage(owner.controller->t_current,boundary::BoundaryPurpose::Hydro);
    owner.runtime->ensure_fluid_ghosts(state::StateSlot::Current);
    owner.control.tree->EvaluateRefinement(owner.config);
    for(int id:owner.control.tree->GetActiveBlocks()) {
        const auto& b=owner.control.pool->GetBlock(id);
        require(b.level==0&&b.logical_x2==0&&b.logical_x3==0
            &&((b.logical_x1==0&&b.refine_flag==1)||(b.logical_x1==1&&b.refine_flag==0)),
            "Actual post-M0 DENS selector differs from the prospectively frozen topology");
        std::cout<<"NATIVE_ACTIVE_AMR_SELECTION block="<<id<<" logical_r="<<b.logical_x1
            <<" level="<<b.level<<" flag="<<b.refine_flag<<'\n';
    }
    const auto regrid_start=Clock::now();
    owner.runtime->regrid_native_rz_candidate(owner.controller->step_count,owner.controller->t_current);
    owner.runtime->bind_native_rz_eos(*owner.eos);owner.bind_context_at_current();
    const auto& measurement=owner.runtime->regrid_records().back();
    require(measurement.changed&&measurement.old_blocks==2&&measurement.new_blocks==5
        &&measurement.step==owner.controller->step_count&&bits(measurement.time,owner.controller->t_current)
        &&std::isfinite(measurement.elapsed_seconds)&&measurement.elapsed_seconds>=0.,
        "True dynamic regrid did not publish its actual measured 2-to-5 topology");
    const auto epoch=owner.context->ledger.active_epoch();
    require(epoch!=old_epoch&&owner.runtime->handles().size()==5&&owner.freeze_active().size()==1280,
        "Dynamic regrid did not publish new complete UID/epoch/cell ownership");
    for(const auto handle:old_handles) {
        bool rejected=false;try{(void)owner.context->ledger.inspect({handle,state::StateSlot::Current});}
        catch(const std::logic_error&){rejected=true;}
        require(rejected,"New regrid ledger accepted a stale old-epoch handle");
    }
    std::set<std::tuple<int,std::uint32_t,std::uint32_t>> actual_keys;
    for(int id:owner.control.tree->GetActiveBlocks()) {
        const auto& b=owner.control.pool->GetBlock(id);b.RequireNativeGeometryIdentity();
        require(b.logical_x3==0,"Dynamic native topology published a third logical axis");
        actual_keys.emplace(b.level,b.logical_x1,b.logical_x2);
        std::cout<<"NATIVE_ACTIVE_AMR_PATCH block="<<id<<" level="<<b.level
            <<" logical_r="<<b.logical_x1<<" logical_z="<<b.logical_x2<<" cells=256\n";
    }
    const std::set<std::tuple<int,std::uint32_t,std::uint32_t>> expected{{1,0,0},{1,0,1},{1,1,0},{1,1,1},{0,1,0}};
    require(actual_keys==expected,"Published dynamic AMR leaf family differs from the actual frozen selection");
    const auto regrid_totals=owner.totals();check_regrid_balance(m0_after,regrid_totals);
    const auto& topology=owner.control.RequireFluxTopologyPlan(species_count,native,-1,true);
    const auto topology_lease=owner.control.FluxTopologyPlanLease();
    require(topology_lease&&topology_lease.get()==&topology,"Dynamic CF plan lost its owning lease");
    require(!topology.routes.empty()&&topology.epoch==epoch,"Dynamic regrid has no real CF registration plan");
    std::size_t energy_operations=0;
    for(const auto& route:topology.routes)for(const auto& operation:route.plan.operations)
        energy_operations+=operation.field==amr::AmrField::Energy;
    require(energy_operations>0,"Real CF routes omit all Energy registration operations");
    const auto boundaries_after_regrid=boundary_before(owner);
    require(boundaries_after_regrid.hydro==boundaries_after_m0.hydro
        &&boundaries_after_regrid.diffusion==boundaries_after_m0.diffusion,"Regrid changed cumulative accepted boundary accounting");
    std::cout<<"NATIVE_ACTIVE_AMR_REGRID old_epoch="<<old_epoch.value<<" epoch="<<epoch.value
        <<" old_leaves=2 leaves=5 cells=1280 routes="<<topology.routes.size()<<" energy_operations="<<energy_operations
        <<" runtime_seconds="<<measurement.elapsed_seconds<<" wall_seconds="
        <<std::chrono::duration<double>(Clock::now()-regrid_start).count()
        <<" delta_M="<<regrid_totals.mass-m0_after.mass<<" delta_J="<<regrid_totals.angular-m0_after.angular
        <<" delta_E="<<regrid_totals.energy-m0_after.energy<<'\n';
    const auto current_boundary_before=boundary_before(owner);
    const auto before_current=owner.freeze_active();const auto accepted_time=owner.controller->t_current;
    const int accepted_step=owner.controller->step_count;const auto current_start=Clock::now();
    // Regrid rebind itself rebuilds the geometry/operator history. There is no
    // durable restart here, so do not gratuitously reset the solver's history.
    owner.gravity_stage->prepare_current(accepted_time,false);
    const auto current=owner.gravity_stage->native_current_field();const auto current_times=solve_times(*owner.gravity);
    const auto binding=amr::bind_elliptic_mesh(owner.control,owner.config.grid,owner.runtime->handles());
    require(current.purpose==Physical::Gravity::GravityFieldPurpose::AcceptedCurrent
        &&current.runtime_lease_authenticated&&current.runtime_lease_generation>0
        &&current.source.topology==epoch&&bits(current.source.input_time,accepted_time)
        &&current.source.inputs.size()==owner.runtime->handles().size()
        &&current.source_generation>0&&current.field_generation>0&&current.potential.size()==binding.cells.size()
        &&binding.cells.size()==1280&&current.faces.size()==current.face_gradient.size()
        &&current.boundary_values.size()==current.faces.size()
        &&current.conditional_residual.status==elliptic::BoundaryResidualStatus::Accepted
        &&current.physical_status==elliptic::BoundaryResidualStatus::UncertifiedInput,
        "Regrid Current field lacks its actual purpose, full source domain or native discrete proof");
    Physical::Gravity::validate_gravity_solve_identity(current.source);
    for(std::size_t n=0;n<current.source.inputs.size();++n) {
        const auto& input=current.source.inputs[n];const auto handle=owner.runtime->handles()[n];
        const auto coherence=owner.context->ledger.inspect({handle,state::StateSlot::Current});
        require(input.block==handle&&input.slot==state::StateSlot::Current&&input.version==coherence.interior.version
            &&input.storage_generation==current.runtime_lease_generation,
            "Regrid Current field consumed a stale/fake selected input");
        owner.context->ledger.require_readable({handle,state::StateSlot::Current},
            {state::ExecutionSide::Host,input.version,true,true});
    }
    for(const auto* values:{&current.potential,&current.face_gradient,&current.boundary_values,&current.side_acceleration})
        for(double value:*values)require(std::isfinite(value),"Regrid Current inspection has a nonfinite actual field");
    for(const auto& values:current.acceleration)for(double value:values)
        require(std::isfinite(value),"Regrid Current inspection has nonfinite acceleration");
    require_active_unchanged(owner,before_current);
    const auto current_boundary_after=boundary_before(owner);
    require(current_boundary_after.hydro==current_boundary_before.hydro
        &&current_boundary_after.diffusion==current_boundary_before.diffusion,
        "AcceptedCurrent diagnostic changed cumulative boundary accounting");
    require(bits(owner.controller->t_current,accepted_time)&&owner.controller->step_count==accepted_step
        &&!owner.runtime->active_runtime_state_transaction()&&!owner.gravity->prepared_native_self(),
        "Current diagnostic changed time or manufactured a Hydro source transaction");
    const double gravity_dt=owner.gravity_stage->native_current_timestep();
    require(std::isfinite(gravity_dt)&&gravity_dt>0.&&macro_dt<=gravity_dt,
        "Actual AcceptedCurrent private gravity stability proposal rejects the frozen M1 interval");
    bool public_refused=false;try{(void)owner.gravity->potential();}
    catch(const std::logic_error&){public_refused=true;}
    require(public_refused,"Private Current candidate unexpectedly granted ordinary public field access");
    owner.accepted_journal.push_back({0,accepted_time,epoch.value,current.runtime_lease_generation,
        current.source_generation,binding.cells.size()});owner.check_dynamic_journal();
    std::cout<<"NATIVE_ACTIVE_AMR_CURRENT purpose=AcceptedCurrent stage=0 epoch="<<epoch.value
        <<" lease="<<current.runtime_lease_generation<<" time="<<accepted_time
        <<" source_generation="<<current.source_generation<<" field_generation="<<current.field_generation
        <<" cells="<<binding.cells.size()<<" conditional_residual_upper="<<current.conditional_residual.total_residual_upper
        <<" tolerance_safe="<<current.conditional_residual.tolerance_safe<<" wall_seconds="<<std::chrono::duration<double>(Clock::now()-current_start).count()
        <<" source_boundary_seconds="<<current_times[0]<<" poisson_seconds="<<current_times[1]
        <<" force_seconds="<<current_times[2]<<" private_gravity_dt="<<gravity_dt
        <<" discrete_proof=accepted physical_qualified=0"<<std::endl;
    print_ring_observations(owner.gravity->ring_memo_observations(
        Physical::Gravity::GravityFieldScope::NativeRzCandidate),0,accepted_time,"AcceptedCurrent");
    const auto m1_before=owner.totals();const auto b1=boundary_before(owner);const auto m1_start=Clock::now();
    owner.advance();const auto m1_after=owner.totals();check_macro_balance(owner,m1_before,b1,m1_after);
    report_macro(owner,1,std::chrono::duration<double>(Clock::now()-m1_start).count());
    bool stale_current_refused=false;try{(void)owner.gravity_stage->native_current_field();}
    catch(const std::logic_error&){stale_current_refused=true;}
    require(stale_current_refused,"Accepted M1 revived the pre-M1 Current purpose/field lease");
    require(owner.accepted_journal.size()==5&&bits(owner.controller->t_current,2.*macro_dt)
        &&owner.controller->step_count==owner.start.step+2,
        "Dynamic activity did not retain exactly five real fields and two accepted physical times");
    std::cout<<"PRIVATE_NATIVE_ACTIVE_FOUR_MODULE_AMR candidate_only=1 genuine_regrid=1 actual_fields=5"
        <<" macros=2 leaves=5 cells=1280 real_CF_routes="<<topology.routes.size()
        <<" accepted_time="<<owner.controller->t_current<<" accepted_steps="<<owner.controller->step_count
        <<" activity_accounting_checked=1 restart_qualified=0 total_energy_qualified=0 physical_qualified=0\n";
}
/** Original two-macro entry retains its original frozen endpoint and assertions. */
inline void run_amr(const std::string& table) {
    Owner owner(table,true);advance_dynamic_to_m1(owner);
}

/** Recreate the unsaved iterative history independently on each resumed branch.
 * This is actual AcceptedCurrent prepare with clear-history=true, not copying
 * a potential or relabelling a Hydro frame. Its actual fluid remains unchanged.
 */
inline void prepare_continuation_current(Owner& owner) {
    const auto before=owner.freeze_active();const auto accounting=boundary_before(owner);
    const double time=owner.controller->t_current;const int step=owner.controller->step_count;
    require(!owner.runtime->active_runtime_state_transaction()&&!owner.gravity->prepared_native_self(),
        "Restart Current overlaps a tentative numerical owner");
    owner.gravity_stage->prepare_current(time,true);
    const auto view=owner.gravity_stage->native_current_field();
    require(view.purpose==Physical::Gravity::GravityFieldPurpose::AcceptedCurrent
        &&view.runtime_lease_authenticated&&view.source.inputs.size()==owner.runtime->handles().size()
        &&view.source.topology==owner.context->ledger.active_epoch()&&bits(view.source.input_time,time)
        &&view.conditional_residual.status==elliptic::BoundaryResidualStatus::Accepted
        &&view.physical_status==elliptic::BoundaryResidualStatus::UncertifiedInput,
        "Fresh Current lacks actual selected-source identity or discrete proof");
    for(std::size_t n=0;n<view.source.inputs.size();++n) {
        const auto& input=view.source.inputs[n];const auto stamp=owner.context->ledger.inspect(
            {owner.runtime->handles()[n],state::StateSlot::Current});
        require(input.block==owner.runtime->handles()[n]&&input.slot==state::StateSlot::Current
            &&input.version==stamp.interior.version&&input.storage_generation==view.runtime_lease_generation,
            "Fresh Current consumed a foreign slot or ledger publication");
        owner.context->ledger.require_readable({input.block,input.slot},
            {state::ExecutionSide::Host,input.version,true,true});
    }
    require_active_unchanged(owner,before);const auto after=boundary_before(owner);
    require(after.hydro==accounting.hydro&&after.diffusion==accounting.diffusion
        &&bits(owner.controller->t_current,time)&&owner.controller->step_count==step,
        "Restart Current changed numerical state/time/accounting");
    require(std::isfinite(owner.gravity_stage->native_current_timestep())
        &&macro_dt<=owner.gravity_stage->native_current_timestep(),
        "Actual restart Current gravity timestep rejects frozen M2");
    owner.accepted_journal.push_back({0,time,view.source.topology.value,view.runtime_lease_generation,
        view.source_generation,view.potential.size()});owner.check_dynamic_journal();
    print_ring_observations(owner.gravity->ring_memo_observations(
        Physical::Gravity::GravityFieldScope::NativeRzCandidate),0,time,"AcceptedCurrent");
}
} // namespace native_active_four_module
