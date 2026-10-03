/**
 * @file test_amr_flux_surface_plan.cpp
 * @brief Check AMR face grouping and conservative flux correction.
 *
 * Exercise shared reflux arithmetic, surface-to-cell mapping and topology
 * partitioning, including signed and zero-weight stage contributions.
 */
#include "amr/flux/AmrFluxExecutionPlan.h"
#include "amr/AMRControl.h"
#include "amr/flux/FluxRegister.h"
#include "amr/flux/AMRFluxRegistering.h"

#include <cmath>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void expect(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

bool close(double left, double right)
{
    return std::abs(left - right) < 1.0e-13;
}

void append_fields(amr::FluxRegistrationPlan& plan,
                   const amr::AmrTransferOperation& prototype,
                   int species_count)
{
    const auto append = [&](amr::AmrField field, int component) {
        auto operation = prototype;
        operation.field = field;
        operation.component = component;
        plan.operations.push_back(operation);
    };
    append(amr::AmrField::Rho, -1);
    append(amr::AmrField::MomU, -1);
    append(amr::AmrField::MomV, -1);
    append(amr::AmrField::MomW, -1);
    append(amr::AmrField::Energy, -1);
    for (int species = 0; species < species_count; ++species)
        append(amr::AmrField::Species, species);
}

amr::AmrFluxGridLayout layout_2d()
{
    const int ng = amr::MAX_NG;
    const int total_y = amr::BLOCK_NY + 2 * ng;
    return {2, amr::PAD_NX * total_y, amr::PAD_NX,
            amr::PAD_NX * total_y,
            ng, ng + amr::BLOCK_NX,
            ng, ng + amr::BLOCK_NY, 0, 1};
}

struct Fixture {
    static constexpr int species_count = 2;
    static constexpr int coarse_id = 3;
    static constexpr int fine_id = 7;
    amr::TopologyEpoch epoch{41};
    amr::AmrEndpoint coarse{{2, 0, 0, 0, 0}, {{101}, epoch}};
    amr::AmrEndpoint fine{{2, 1, 1, 0, 0}, {{202}, epoch}};
    amr::AmrFluxTopologyPlan topology;
    std::vector<amr::AmrFluxEndpointBinding> bindings;

    Fixture()
    {
        amr::FluxRegistrationPlan route_plan{};
        route_plan.dimension = 2;
        route_plan.scope = {0, epoch, epoch};
        // Insert in reverse tangential order; finalize must canonicalize it.
        for (int source_j : {1, 0}) {
            const amr::AmrTransferOperation prototype{
                0, fine, coarse,
                {{amr::BLOCK_NX, source_j, 0}, {1, 1, 1}},
                {{0, 0, 0}, {1, 1, 1}},
                amr::AmrAxis::X, amr::AmrSide::Lower,
                amr::AmrField::Rho, -1,
                amr::RefinementRule::FineFluxContribution,
                0.5, 1.0};
            append_fields(route_plan, prototype, species_count);
        }
        amr::finalize_amr_plan(route_plan);

        topology.dimension = 2;
        topology.species_count = species_count;
        topology.epoch = epoch;
        topology.active_blocks = {coarse_id, fine_id};
        topology.active_endpoints = {coarse, fine};
        topology.pool_lowering = {{coarse, coarse_id}, {fine, fine_id}};
        topology.routes.push_back({
            {fine_id, amr::AmrAxis::X}, fine, std::move(route_plan)});
        topology.route_index.emplace(
            amr::AmrFluxRouteKey{fine_id, amr::AmrAxis::X}, 0);
        topology.fingerprint =
            amr::flux_plan_detail::compute_fingerprint(topology);
        amr::validate_amr_flux_topology_plan(topology);

        const auto grid = layout_2d();
        bindings = {{coarse, 0, species_count, grid},
                    {fine, 1, species_count, grid}};
    }
};

void test_shared_math()
{
    using amr::RefinementRule;
    using namespace amr::flux_math;
    expect(close(registration_coefficient(
                     RefinementRule::FineFluxContribution, 0.25, 2.0),
                 0.5),
           "fine registration coefficient drifted");
    expect(close(registration_coefficient(
                     RefinementRule::CoarseFluxContribution, 1.0, 2.0),
                 -2.0),
           "coarse registration coefficient drifted");
    expect(close(registration_coefficient(
                     RefinementRule::FineFluxContribution, 0.5, -0.2),
                 -0.1),
           "negative RKL gamma was not preserved");

    const double rho_after = reflux_conserved(2.0, 0.25, 4.0);
    expect(close(rho_after, 3.0), "reflux conserved update drifted");
    expect(close(reflux_mass_fraction(
                     2.0, 0.3, 0.25, 1.2, rho_after),
                 0.3),
           "reflux species-density update drifted");

    const double stage_flux = 3.0;
    const double initial_flux = 5.0;
    const double rkl_register =
        registration_coefficient(
            RefinementRule::FineFluxContribution, 0.5, 0.7)
            * stage_flux
        + registration_coefficient(
            RefinementRule::FineFluxContribution, 0.5, -0.2)
            * initial_flux;
    expect(close(rkl_register, 0.55),
           "signed two-operator RKL registration drifted");
}

void test_canonical_surface_lowering()
{
    Fixture fixture;
    const auto compiled = amr::compile_amr_flux_topology_plan(
        fixture.topology, fixture.bindings);
    expect(compiled.routes.size() == 1,
           "topology did not lower exactly one source route");
    const auto& route = compiled.routes.front();
    expect(route.source_block == 1 && route.source_direction == 0
               && route.targets.size() == 1 && route.terms.size() == 2,
           "fine face terms were not segmented by register destination");
    const auto& target = route.targets.front();
    expect(target.destination_block == 0 && target.destination_face == 0
               && target.destination_cell == 0 && target.source_face == 1
               && target.first_term == 0 && target.term_count == 2,
           "surface target lowering drifted");
    expect(route.terms[0].source_surface_cell == 0
               && route.terms[1].source_surface_cell == 1
               && close(route.terms[0].geometric_weight, 0.5)
               && close(route.terms[1].geometric_weight, 0.5),
           "canonical fine terms or area weights drifted");
    const auto grid = layout_2d();
    expect(route.terms[0].source_cell
               == grid.is + amr::BLOCK_NX + grid.js * grid.stride_y,
           "upper-face right-cell flux convention drifted");

    const auto requirements = amr::build_amr_flux_surface_requirements(
        compiled, true);
    expect(requirements.size() == 2
               && requirements[0].block == 0
               && requirements[0].face == 0
               && requirements[0].roles
                    == static_cast<std::uint8_t>(
                        amr::AmrFluxSurfaceRole::Register)
               && requirements[1].block == 1
               && requirements[1].face == 1
               && requirements[1].roles
                    == static_cast<std::uint8_t>(
                        amr::AmrFluxSurfaceRole::InitialOperatorCache),
           "compact register/cache allocation manifest drifted");

    // The compact allocation is proportional to face cells, not block volume.
    const std::size_t compact = static_cast<std::size_t>(amr::BLOCK_NY)
        * (5 + Fixture::species_count);
    const std::size_t old_full_volume = static_cast<std::size_t>(
        grid.total_size) * (6 + Fixture::species_count);
    expect(compact < old_full_volume,
           "surface storage regressed to a full-volume cache");
}

void test_zero_activation_and_signed_execution()
{
    Fixture fixture;
    amr::FluxRegister flux_register;
    flux_register.EnsureSpecies(Fixture::species_count);
    flux_register.Resize(16, 2);
    const auto& plan = fixture.topology.routes.front().plan;
    std::vector<double> values(plan.operations.size(), 0.0);
    for (std::size_t index = 0; index < plan.operations.size(); ++index) {
        const auto& operation = plan.operations[index];
        if (operation.field == amr::AmrField::Rho)
            values[index] = operation.source_box.first[1] == 0 ? 3.0 : 5.0;
    }
    flux_register.ApplyRegistrationPlan(
        plan, values, fixture.topology.pool_lowering, 0.0);
    expect(!flux_register.HasData(Fixture::coarse_id, 0),
           "zero stage weight manufactured a Host activity witness");

    flux_register.ApplyRegistrationPlan(
        plan, values, fixture.topology.pool_lowering, -0.25);
    expect(flux_register.HasData(Fixture::coarse_id, 0)
               && close(flux_register.GetSummedFlux(
                            Fixture::coarse_id, 0, 0).rho,
                        -1.0),
           "signed execution disagrees with shared route math");
}

void append_reflux_fields(amr::RefluxPlan& plan,
                          const amr::AmrEndpoint& endpoint,
                          amr::AmrAxis axis, amr::AmrSide side,
                          double weight, double sign)
{
    amr::LogicalAmrBox box{{0, 0, 0}, {1, 1, 1}};
    const auto append = [&](amr::AmrField field, int component) {
        plan.operations.push_back({
            0, endpoint, endpoint, box, box, axis, side, field, component,
            amr::RefinementRule::RefluxCorrection, weight, sign});
    };
    append(amr::AmrField::Rho, -1);
    append(amr::AmrField::MomU, -1);
    append(amr::AmrField::MomV, -1);
    append(amr::AmrField::MomW, -1);
    append(amr::AmrField::Energy, -1);
    append(amr::AmrField::Species, 0);
    append(amr::AmrField::Species, 1);
}

void test_corner_reflux_grouping()
{
    Fixture fixture;
    amr::RefluxPlan plan{};
    plan.dimension = 2;
    plan.scope = {0, fixture.epoch, fixture.epoch};
    // Reverse insertion; finalization and lowering must restore X then Y.
    append_reflux_fields(
        plan, fixture.coarse, amr::AmrAxis::Y, amr::AmrSide::Lower,
        3.0, 1.0);
    append_reflux_fields(
        plan, fixture.coarse, amr::AmrAxis::X, amr::AmrSide::Lower,
        2.0, 1.0);
    amr::finalize_amr_plan(plan);
    const auto compiled = amr::compile_amr_reflux_plan(
        plan, fixture.bindings, Fixture::species_count);
    expect(compiled.targets.size() == 1
               && compiled.contributions.size() == 2
               && compiled.targets.front().contribution_count == 2,
           "corner reflux was not grouped into one race-free state target");
    expect(compiled.contributions[0].register_face == 0
               && compiled.contributions[1].register_face == 2,
           "corner reflux contribution order is not canonical");
}

void test_real_2d_topology_surface_partition()
{
    SimConfig config{};
    config.grid.dim = 2;
    config.grid.nblockx1 = 2;
    config.grid.nblockx2 = 1;
    config.grid.nblockx3 = 0;
    config.grid.amr_max_blocks = 32;
    config.amr.lrefinemin = 0;
    config.amr.lrefinemax = 1;

    amr::AMRControl control(32, 2);
    control.tree->LoadLeafGrid(
        config, 0,
        std::vector<int>{1, 1, 1, 1, 0},
        std::vector<std::uint32_t>{0, 1, 0, 1, 1},
        std::vector<std::uint32_t>{0, 0, 1, 1, 0},
        std::vector<std::uint32_t>{0, 0, 0, 0, 0});

    const auto& active = control.tree->GetActiveBlocks();
    std::vector<amr::BlockHandle> handles;
    handles.reserve(active.size());
    for (std::size_t index = 0; index < active.size(); ++index)
        handles.push_back({{1000 + index}, {55}});

    const auto topology = amr::build_amr_flux_topology_plan(
        *control.pool, active, handles, 2, 0);
    const auto reflux = amr::build_amr_reflux_topology_plan(
        *control.pool, topology);

    int coarse_runtime = -1;
    std::vector<amr::AmrFluxEndpointBinding> bindings;
    bindings.reserve(active.size());
    for (std::size_t index = 0; index < active.size(); ++index) {
        const auto& block = control.pool->GetBlock(active[index]);
        if (block.level == 0 && block.logical_x1 == 1
            && block.logical_x2 == 0)
            coarse_runtime = static_cast<int>(index);
        bindings.push_back({
            topology.active_endpoints[index], static_cast<int>(index), 0,
            amr::make_amr_flux_grid_layout(block.grid)});
    }
    expect(coarse_runtime >= 0, "real 2D AMR fixture has no coarse leaf");

    const auto compiled = amr::compile_amr_flux_topology_plan(
        topology, bindings);
    struct CellPartition {
        int coarse_terms = 0;
        int fine_terms = 0;
        double fine_weight = 0.0;
    };
    std::map<int, CellPartition> partition;
    for (const auto& route : compiled.routes) {
        for (const auto& target : route.targets) {
            if (target.destination_block != coarse_runtime
                || target.destination_face != 0)
                continue;
            auto& cell = partition[target.destination_cell];
            for (int offset = 0; offset < target.term_count; ++offset) {
                const auto& term = route.terms[
                    static_cast<std::size_t>(target.first_term + offset)];
                if (target.rule
                    == amr::RefinementRule::CoarseFluxContribution) {
                    ++cell.coarse_terms;
                    expect(close(term.geometric_weight, 1.0),
                           "coarse surface term lost unit weight");
                } else {
                    expect(target.rule
                               == amr::RefinementRule::FineFluxContribution,
                           "real topology emitted an invalid flux rule");
                    ++cell.fine_terms;
                    cell.fine_weight += term.geometric_weight;
                }
            }
        }
    }
    expect(partition.size() == static_cast<std::size_t>(amr::BLOCK_NY),
           "real 2D topology did not cover the complete coarse face");
    for (const auto& [cell_index, cell] : partition) {
        (void)cell_index;
        expect(cell.coarse_terms == 1 && cell.fine_terms == 2
                   && close(cell.fine_weight, 1.0),
               "2:1 face partition is not one coarse term plus a unit fine sum");
    }

    const auto compiled_reflux = amr::compile_amr_reflux_plan(
        reflux, bindings, 0);
    expect(compiled_reflux.targets.size()
               == static_cast<std::size_t>(amr::BLOCK_NY)
               && compiled_reflux.contributions.size()
                   == static_cast<std::size_t>(amr::BLOCK_NY),
           "real 2D reflux did not lower to one race-free correction per cell");
}


void test_rz_registration_reflux(int direction, double inner)
{
    constexpr int species=2;
    const auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    SimConfig config{};
    config.grid.dim=2;config.grid.geometry="cylindrical";
    config.grid.nblockx1=direction==0?2:1;
    config.grid.nblockx2=direction==0?1:2;config.grid.nblockx3=0;
    config.grid.x1_min=inner;config.grid.x1_max=inner+2.;
    config.grid.x2_min=-1.;config.grid.x2_max=1.;
    config.grid.amr_max_blocks=32;config.amr.lrefinemin=0;config.amr.lrefinemax=1;
    amr::AMRControl control(32,2);
    control.tree->LoadLeafGrid(config,0,{1,1,1,1,0},
        {0,1,0,1,static_cast<std::uint32_t>(direction==0?1:0)},
        {0,0,1,1,static_cast<std::uint32_t>(direction==0?0:1)},{0,0,0,0,0});
    const auto& active=control.tree->GetActiveBlocks();
    std::vector<amr::BlockHandle> handles;
    int coarse=-1;
    for(std::size_t n=0;n<active.size();++n) {
        handles.push_back({{2000+n},{83}});
        auto& block=control.pool->GetBlock(active[n]);
        block.fluid_state.InitSpecies(species);
        for(int cell=0;cell<block.grid.GetTotalSize();++cell) {
            block.fluid_state.set(cell,{10.,20.,30.,40.,1000.});
            block.fluid_state.X(0,cell)=.6;block.fluid_state.X(1,cell)=.4;
        }
        if(block.level==0)coarse=active[n];
    }
    control.BindActiveHandles(handles);control.flux_register.EnsureSpecies(species);
    expect(coarse>=0,"RZ reflux fixture has no coarse leaf");
    const auto topology=control.RequireFluxTopologyPlan(species,rz);
    const auto reflux=control.RequireRefluxTopologyPlan(species,rz);
    expect(topology.semantics==rz,"RZ flux plan lost chart");
    expect(&control.RequireFluxTopologyPlan(species,rz)
        ==&control.RequireFluxTopologyPlan(species,rz),"RZ flux cache did not hit");
    const auto value=[](const Grid& g,int dir,int i,int j) {
        const double r=dir==0?g.GetFacePosL(i):g.GetCellCenterX(i);
        const double z=dir==1?g.x2_min+(j-g.Js())*g.dx2:g.GetCellCenterY(j);
        return 1.+.3*r*r+.2*z;
    };
    const auto area=[](const Grid& g,int dir,int i,int j,bool upper) -> long double {
        (void)j;
        const long double lo=g.GetFacePosL(i),hi=g.GetFacePosR(i);
        if(dir==0)return 2.L*arch::constants::math::pi*(upper?hi:lo)*g.dx2;
        return arch::constants::math::pi*(hi*hi-lo*lo);
    };
    using Key=std::pair<int,int>;
    std::map<Key,long double> expected_register;
    bool nonuniform_axial_weight=false;
    for(const auto& route:topology.routes) {
        const auto& block=control.pool->GetBlock(route.key.source_block);
        const auto& g=block.grid;
        const int dir=amr::axis_value(route.key.axis);
        std::vector<FluidVector> flux(g.GetTotalSize());
        std::vector<double> species_flux(species*g.GetTotalSize());
        for(int j=0;j<g.GetTotalY();++j)for(int i=0;i<g.GetTotalX();++i) {
            const int cell=g.GetIndex(i,j,0);const double v=value(g,dir,i,j);
            flux[cell]={v,2.*v,3.*v,4.*v,5.*v};
            species_flux[cell]=.6*v;species_flux[g.GetTotalSize()+cell]=.4*v;
        }
        for(const auto& operation:route.plan.operations) {
            if(operation.field!=amr::AmrField::Rho)continue;
            const int face=2*amr::axis_value(operation.axis)+amr::side_value(operation.side);
            const int facecell=amr::flux_plan_detail::face_cell_index(
                operation.destination_box,operation.axis);
            const int si=g.Is()+operation.source_box.first[0];
            const int sj=g.Js()+operation.source_box.first[1];
            const double v=value(g,dir,si,sj);
            long double coefficient=-1.;
            if(operation.rule==amr::RefinementRule::FineFluxContribution) {
                const auto& cg=control.pool->GetBlock(coarse).grid;
                const int ci=cg.Is()+operation.destination_box.first[0];
                const int cj=cg.Js()+operation.destination_box.first[1];
                // Source index is a face's right cell; its lower face is the
                // stored face even for an upper block boundary.
                const long double fine_area=area(g,dir,si,sj,false);
                const long double coarse_area=area(cg,dir,ci,cj,face%2==1);
                coefficient=fine_area/coarse_area;
                expect(close(operation.weight,static_cast<double>(coefficient)),
                    "RZ fine area weight differs from independent full-ring face");
                if(dir==1 && std::abs(coefficient-.5L)>1.e-4L)
                    nonuniform_axial_weight=true;
            }
            expected_register[{face,facecell}]+=coefficient*v;
        }
        amr::RegisterCoarseFineFluxes(control,block.id,g,dir,
            flux,species_flux,species,1.,rz);
    }
    if(direction==1)expect(nonuniform_axial_weight,
        "RZ axial fine weights incorrectly remained uniform halves");
    const auto& cg=control.pool->GetBlock(coarse).grid;
    double max_register_error=0.;
    for(const auto& [key,reference]:expected_register) {
        const double actual=control.flux_register.GetSummedFlux(coarse,key.first,key.second).rho;
        expect(close(actual,static_cast<double>(reference)),"RZ registration mismatch");
        max_register_error=std::max(max_register_error,std::abs(actual-static_cast<double>(reference)));
    }
    constexpr double dt=.001;
    const auto direct=control.flux_register.BuildRefluxPlan(
        control.pool,active,handles,2,dt,rz);
    expect(direct.operations.size()==reflux.operations.size(),"RZ direct/topology reflux count drifted");
    std::map<int,long double> density_delta;
    long double integrated_delta=0.,boundary_integral=0.;
    for(const auto& operation:reflux.operations) {
        if(operation.field!=amr::AmrField::Rho)continue;
        const int dir=amr::axis_value(operation.axis);
        const bool upper=operation.side==amr::AmrSide::Upper;
        const int i=cg.Is()+operation.destination_box.first[0];
        const int j=cg.Js()+operation.destination_box.first[1];
        const int cell=cg.GetIndex(i,j,0);
        const int face=2*dir+(upper?1:0);
        const int facecell=amr::flux_plan_detail::face_cell_index(operation.destination_box,operation.axis);
        const long double lo=cg.GetFacePosL(i),hi=cg.GetFacePosR(i);
        const long double volume=arch::constants::math::pi*(hi*hi-lo*lo)*cg.dx2;
        const long double a=area(cg,dir,i,j,upper);
        expect(close(operation.weight,static_cast<double>(a/volume)),
            "RZ reflux weight differs from independent full-ring A/V");
        const long double amount=(upper?-1.L:1.L)*dt*a*expected_register.at({face,facecell});
        density_delta[cell]+=amount/volume;boundary_integral+=amount;
    }
    for(std::size_t n=0;n<direct.operations.size();++n)
        expect(close(direct.operations[n].weight,dt*reflux.operations[n].weight),
            "RZ direct reflux lost chart/timestep weight");
    control.ApplyReflux(dt,&amr::Block::fluid_state,rz);
    const auto& state=control.pool->GetBlock(coarse).fluid_state;
    for(int j=cg.Js();j<cg.Je();++j)for(int i=cg.Is();i<cg.Ie();++i) {
        const int cell=cg.GetIndex(i,j,0);const double d=static_cast<double>(density_delta[cell]);
        expect(close(state.rho[cell],10.+d),"RZ actual density reflux mismatch");
        expect(close(state.mom_u[cell],20.+2.*d),"RZ actual radial reflux mismatch");
        expect(close(state.mom_v[cell],30.+3.*d),"RZ actual axial reflux mismatch");
        expect(close(state.mom_w[cell],40.+4.*d),"RZ actual phi reflux mismatch");
        expect(close(state.eng[cell],1000.+5.*d),"RZ actual energy reflux mismatch");
        expect(close(state.X(0,cell),.6)&&close(state.X(1,cell),.4),
            "RZ reflux composition drifted");
        const long double lo=cg.GetFacePosL(i),hi=cg.GetFacePosR(i);
        integrated_delta+=(state.rho[cell]-10.)*arch::constants::math::pi
            *(hi*hi-lo*lo)*cg.dx2;
    }
    expect(std::abs(integrated_delta-boundary_integral)<1.e-13L,
        "RZ integrated correction differs from face imbalance");
    // Chart and physical geometry are independent of topology epoch.
    const auto original_hash=topology.fingerprint;
    const auto legacy_hash=control.RequireFluxTopologyPlan(species).fingerprint;
    expect(legacy_hash!=original_hash,"AMR cache confused polar and RZ chart");
    bool wrong_chart_rejected=false;
    try { control.ApplyReflux(dt); }
    catch(const std::invalid_argument&) { wrong_chart_rejected=true; }
    expect(wrong_chart_rejected,"legacy chart consumed RZ accumulated flux");

    expect(control.RequireFluxTopologyPlan(species,rz).fingerprint==original_hash,
        "RZ cache rebuild changed original identity");
    const auto original_reflux_hash=control.RequireRefluxTopologyPlan(species,rz).fingerprint;
    for(int id:active) {
        auto& g=control.pool->GetBlock(id).grid;g.x1_min+=.125;g.x1_max+=.125;
    }
    expect(control.RequireFluxTopologyPlan(species,rz).fingerprint!=original_hash,
        "AMR flux cache retained stale native bounds");
    expect(control.RequireRefluxTopologyPlan(species,rz).fingerprint!=original_reflux_hash
        || direction==1,"AMR reflux cache retained old radial coefficients");

    bool old_flux_rejected=false;
    try { control.ApplyReflux(dt,&amr::Block::fluid_state,rz); }
    catch(const std::invalid_argument&) { old_flux_rejected=true; }
    expect(old_flux_rejected,"new native geometry consumed old accumulated flux");
    control.flux_register.Clear();
    for(int id:active) {
        const auto& block=control.pool->GetBlock(id);
        std::vector<FluidVector> zero_flux(block.grid.GetTotalSize());
        std::vector<double> zero_species(species*block.grid.GetTotalSize());
        for(int dir=0;dir<2;++dir)
            amr::RegisterCoarseFineFluxes(control,id,block.grid,dir,
                zero_flux,zero_species,species,1.,rz);
    }
    control.ApplyReflux(dt,&amr::Block::fluid_state,rz);
    std::cout<<"RZ_REFLUX direction="<<direction<<" inner="<<inner
        <<" register_error="<<max_register_error<<" conservation_error="
        <<static_cast<double>(std::abs(integrated_delta-boundary_integral))<<'\n';
}


void test_rz_uniform_empty_reflux() {
    SimConfig config{};
    config.grid.dim=2;config.grid.geometry="cylindrical";
    config.grid.nblockx1=2;config.grid.nblockx2=1;config.grid.nblockx3=0;
    config.grid.x1_min=0.;config.grid.x1_max=2.;
    config.grid.x2_min=-1.;config.grid.x2_max=1.;
    config.grid.amr_max_blocks=8;config.amr.lrefinemin=0;config.amr.lrefinemax=0;
    amr::AMRControl control(8,2);
    control.tree->LoadLeafGrid(config,0,{0,0},{0,1},{0,0},{0,0});
    const std::vector<amr::BlockHandle> handles{{{3000},{89}},{{3001},{89}}};
    control.BindActiveHandles(handles);
    const auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    expect(control.RequireRefluxTopologyPlan(0,rz).operations.empty(),
        "uniform RZ manufactured reflux operations");
    control.ApplyReflux(.1,&amr::Block::fluid_state,rz);
}

} // namespace

int main()
{
    try {
        test_rz_uniform_empty_reflux();
        test_shared_math();
        test_canonical_surface_lowering();
        test_zero_activation_and_signed_execution();
        test_corner_reflux_grouping();
        test_real_2d_topology_surface_partition();
        for(int direction:{0,1})for(double inner:{0.,1.})
            test_rz_registration_reflux(direction,inner);
        std::cout << "AMR_FLUX_SURFACE_PLAN_PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
