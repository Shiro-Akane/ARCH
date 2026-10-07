/**
 * @file test_amr_flux_surface_plan.cpp
 * @brief Check AMR face grouping and conservative flux correction.
 *
 * Exercise shared reflux arithmetic, surface-to-cell mapping and topology
 * partitioning, including signed and zero-weight stage contributions.
 */
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "amr/AMRControl.h"
#include "amr/flux/AmrFluxExecutionPlan.h"
#include "amr/flux/FluxRegister.h"
#include "amr/flux/AMRFluxRegistering.h"

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

/** Exact no-transport species identity, plus unchanged active/invalid arithmetic.
 * These finite nonbinary rho/X values exhibit the old multiplication/division
 * one-ULP defect. Binary active cases have exact independent fractions; a
 * nonzero underflowing product must retain the original active expression.
 */
void test_zero_transport_species_identity()
{
    constexpr double rho=10543210.12345, x=.1;
    const double nan=std::numeric_limits<double>::quiet_NaN();
    const double inf=std::numeric_limits<double>::infinity();
    struct Case { std::array<double,5> input; double expected; int kind; };
    const std::array<Case,15> cases{{
        {{rho, x, .125, 0., rho}, x, 0},
        {{rho, x, 0., 3., rho}, x, 0},
        {{rho, x, -.375, -0., rho}, x, 0},
        {{2., .25, .5, .5, 2.}, .375, 0},
        {{2., .25, .5, 0., 4.}, .125, 0},
        {{rho, x, .5, std::numeric_limits<double>::denorm_min(), rho},
            0x1.9999999999999p-4, 0},
        {{rho, x, nan, 0., rho}, 0., 1},
        {{rho, x, inf, 0., rho}, 0., 1},
        {{rho, x, 0., inf, rho}, 0., 1},
        {{rho, x, 0., nan, rho}, 0., 1},
        {{rho, x, 1., inf, rho}, inf, 2},
        {{rho, nan, 0., 0., rho}, 0., 1},
        {{rho, inf, 0., 0., rho}, inf, 2},
        {{0., x, 0., 0., 0.}, 0., 1},
        {{inf, x, 0., 0., inf}, 0., 1}
    }};
    const double raw=amr::flux_math::reflux_species_density(rho,x,.125,0.)/rho;
    expect(std::bit_cast<std::uint64_t>(raw)
        ==std::bit_cast<std::uint64_t>(0x1.9999999999999p-4)
        &&std::bit_cast<std::uint64_t>(raw)!=std::bit_cast<std::uint64_t>(x),
        "zero-transport regression no longer witnesses original rhoX/rho one-ULP loss");
    for(std::size_t n=0;n<cases.size();++n) {
        const auto& c=cases[n];
        const double actual=amr::flux_math::reflux_mass_fraction(
            c.input[0],c.input[1],c.input[2],c.input[3],c.input[4]);
        const bool accepted=c.kind==1?std::isnan(actual):c.kind==2?
            std::isinf(actual)&&actual>0.:
            std::bit_cast<std::uint64_t>(actual)==std::bit_cast<std::uint64_t>(c.expected);
        expect(accepted,"shared reflux species identity/active/invalid contract case="+std::to_string(n));
    }
    std::cout<<"AMR_ZERO_TRANSPORT_SPECIES_PASS cases="<<cases.size()<<" exact_identity=1 original_active_math=1\n";
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


void test_rz_registration_reflux(int direction, double inner,
    bool angular=false,double stage=1.)
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
    const auto topology=control.RequireFluxTopologyPlan(species,rz,-1,angular);
    const auto reflux=control.RequireRefluxTopologyPlan(species,rz,angular);
    expect(topology.semantics==rz,"RZ flux plan lost chart");
    expect(&control.RequireFluxTopologyPlan(species,rz,-1,angular)
        ==&control.RequireFluxTopologyPlan(species,rz,-1,angular),"RZ flux cache did not hit");
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
    std::map<Key,long double> expected_register,expected_angular;
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
            expected_register[{face,facecell}]+=stage*coefficient*v;
            const long double rl=g.GetFacePosL(si),rr=g.GetFacePosR(si);
            const long double lever=dir==0?rl:(2.L/3.L)*(rr*rr*rr-rl*rl*rl)/(rr*rr-rl*rl);
            expected_angular[{face,facecell}]+=stage*coefficient*4.L*v*(angular?lever:1.L);
        }
        amr::RegisterCoarseFineFluxes(control,block.id,g,dir,
            flux,species_flux,species,stage,rz,angular);
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
    std::map<int,long double> density_delta,phi_delta;
    long double integrated_delta=0.,boundary_integral=0.,J_delta=0.,torque_delta=0.,initial_abs_J=0.;
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
        const long double W=2.L*arch::constants::math::pi*(hi*hi*hi-lo*lo*lo)*cg.dx2/3.L;
        const long double torque=(upper?-1.L:1.L)*dt*a*expected_angular.at({face,facecell});
        phi_delta[cell]+=torque/(angular?W:volume);torque_delta+=torque;
    }
    for(std::size_t n=0;n<direct.operations.size();++n)
        expect(close(direct.operations[n].weight,dt*reflux.operations[n].weight),
            "RZ direct reflux lost chart/timestep weight");
    control.ApplyReflux(dt,&amr::Block::fluid_state,rz,angular);
    const auto& state=control.pool->GetBlock(coarse).fluid_state;
    for(int j=cg.Js();j<cg.Je();++j)for(int i=cg.Is();i<cg.Ie();++i) {
        const int cell=cg.GetIndex(i,j,0);const double d=static_cast<double>(density_delta[cell]);
        expect(close(state.rho[cell],10.+d),"RZ actual density reflux mismatch");
        expect(close(state.mom_u[cell],20.+2.*d),"RZ actual radial reflux mismatch");
        expect(close(state.mom_v[cell],30.+3.*d),"RZ actual axial reflux mismatch");
        expect(close(state.mom_w[cell],40.+static_cast<double>(phi_delta[cell])),"RZ actual phi reflux mismatch");
        expect(close(state.eng[cell],1000.+5.*d),"RZ actual energy reflux mismatch");
        expect(close(state.X(0,cell),.6)&&close(state.X(1,cell),.4),
            "RZ reflux composition drifted");
        const long double lo=cg.GetFacePosL(i),hi=cg.GetFacePosR(i);
        const long double W=2.L*arch::constants::math::pi*(hi*hi*hi-lo*lo*lo)*cg.dx2/3.L;
        J_delta+=(state.mom_w[cell]-40.)*(angular?W:arch::constants::math::pi*(hi*hi-lo*lo)*cg.dx2);
        initial_abs_J+=40.L*W;
        integrated_delta+=(state.rho[cell]-10.)*arch::constants::math::pi
            *(hi*hi-lo*lo)*cg.dx2;
    }
    expect(std::abs(integrated_delta-boundary_integral)<1.e-13L,
        "RZ integrated correction differs from face imbalance");
    const long double J_error=std::abs(J_delta-torque_delta)/(initial_abs_J+std::abs(torque_delta));
    expect(J_error<=1.e-12L,"RZ torque reflux exceeded original angular budget");
    if(angular) {
        const auto ordinary=control.RequireFluxTopologyPlan(species,rz,-1,false).fingerprint;
        expect(ordinary!=topology.fingerprint,"angular and ordinary register cache identity collided");
        bool rejected=false;
        try{control.ApplyReflux(dt,&amr::Block::fluid_state,rz,false);}
        catch(const std::invalid_argument&){rejected=true;}
        expect(rejected,"ordinary reflux consumed an accumulated torque register");
        std::vector<amr::AmrFluxEndpointBinding> bindings;
        for(std::size_t n=0;n<active.size();++n) {
            const auto& block=control.pool->GetBlock(active[n]);
            bindings.push_back({topology.active_endpoints[n],active[n],species,
                amr::make_amr_flux_grid_layout(block.grid)});
        }
        const auto compiled=amr::compile_amr_flux_topology_plan(topology,bindings);
        for(const auto& route:compiled.routes) for(const auto& term:route.terms) {
            const auto& g=control.pool->GetBlock(route.source_block).grid;
            const int i=term.source_cell%g.stride_y;
            const long double l=g.GetFacePosL(i),h=g.GetFacePosR(i);
            const long double lever=route.source_direction==0?l
                :(2.L/3.L)*(h*h*h-l*l*l)/(h*h-l*l);
            expect(close(term.angular_factor,static_cast<double>(lever)),
                "compiled/device term lost authoritative face lever");
        }
        const auto compiled_reflux=amr::compile_amr_reflux_plan(reflux,bindings,species,&topology);
        for(const auto& target:compiled_reflux.targets) {
            const auto& g=control.pool->GetBlock(target.block).grid;
            const int i=target.state_cell%g.stride_y;
            const long double l=g.GetFacePosL(i),h=g.GetFacePosR(i);
            const long double factor=1.5L*(h*h-l*l)/(h*h*h-l*l*l);
            for(int offset=0;offset<target.contribution_count;++offset)
                expect(close(compiled_reflux.contributions[target.first_contribution+offset].angular_factor,
                    static_cast<double>(factor)),"compiled/device reflux factor lost W identity");
        }
    }
    // Chart and physical geometry are independent of topology epoch.
    const auto original_hash=topology.fingerprint;
    const auto legacy_hash=control.RequireFluxTopologyPlan(species).fingerprint;
    expect(legacy_hash!=original_hash,"AMR cache confused polar and RZ chart");
    bool wrong_chart_rejected=false;
    try { control.ApplyReflux(dt); }
    catch(const std::invalid_argument&) { wrong_chart_rejected=true; }
    expect(wrong_chart_rejected,"legacy chart consumed RZ accumulated flux");

    expect(control.RequireFluxTopologyPlan(species,rz,-1,angular).fingerprint==original_hash,
        "RZ cache rebuild changed original identity");
    const auto original_reflux_hash=control.RequireRefluxTopologyPlan(species,rz,angular).fingerprint;

    // These existing scalar fixtures intentionally have unbound native grids.
    // Seed identity equality is a cache/borrow contract, not permission to
    // authenticate new hierarchy geometry. Exercise the actual cache and both
    // real producers with local bounds/dx and handle epochs left untouched.
    {
        std::vector<GridMetrics::DyadicGridIdentity> original_seeds;
        std::vector<FluidState> original_states;
        for(int id:active) {
            const auto& block=control.pool->GetBlock(id);
            expect(!block.grid.dyadic_identity.bound,
                "scalar cache fixture unexpectedly acquired authenticated native hierarchy identity");
            original_seeds.push_back(block.grid.dyadic_identity);
            original_states.push_back(block.fluid_state);
        }
        const auto register_before=control.flux_register.snapshot_host();
        expect(!topology.routes.empty(),"identity cache fixture has no genuine producer route");
        const auto& producer_route=topology.routes.front();
        const auto& producer_grid=control.pool->GetBlock(producer_route.key.source_block).grid;
        std::vector<FluidVector> unchanged_flux(producer_grid.GetTotalSize());
        std::vector<double> unchanged_species(species*producer_grid.GetTotalSize());
        for(int mutation=0;mutation<7;++mutation) {
            for(int id:active) {
                auto& seed=control.pool->GetBlock(id).grid.dyadic_identity;
                if(mutation==0)seed.root_lower[0]=.125;
                else if(mutation==1)seed.root_upper[1]=1.;
                else if(mutation==2)seed.root_blocks[0]=3;
                else if(mutation==3)seed.level=1;
                else if(mutation==4)seed.logical[1]=1;
                else if(mutation==5)seed.periodic_axial=true;
                else seed.root_lower[1]=-0.; // Same value; distinct binary root identity.
                expect(!seed.bound,"cache mutation granted scientific hierarchy authentication");
            }
            const auto changed=control.RequireFluxTopologyPlan(species,rz,-1,angular);
            expect(changed.epoch==topology.epoch&&changed.fingerprint!=original_hash,
                "actual flux cache ignored root seed identity while epoch stayed fixed");
            const auto changed_reflux=control.RequireRefluxTopologyPlan(species,rz,angular);
            expect(changed_reflux.operations.size()==reflux.operations.size(),
                "identity-only cache rebuild changed unchanged local reflux geometry");
            bool stale_flux=false;
            try {amr::RegisterCoarseFineFluxes(control,topology,producer_route.key.source_block,
                producer_grid,amr::axis_value(producer_route.key.axis),unchanged_flux,
                unchanged_species,species,1.);}
            catch(const std::invalid_argument&) {stale_flux=true;}
            expect(stale_flux,"actual producer consumed stale root-identity flux plan");
            bool stale_reflux_producer=false;
            try {(void)amr::build_amr_reflux_topology_plan(*control.pool,topology);}
            catch(const std::invalid_argument&) {stale_reflux_producer=true;}
            expect(stale_reflux_producer,"actual reflux producer consumed stale root-identity topology");
            bool stale_accumulation=false;
            try {control.ApplyReflux(dt,&amr::Block::fluid_state,rz,angular);}
            catch(const std::invalid_argument&) {stale_accumulation=true;}
            expect(stale_accumulation,"new root identity consumed old accumulated flux fingerprint");
            expect(control.flux_register.host_snapshot_matches(register_before),
                "stale native identity wrote accumulated flux before rejection");
            for(std::size_t n=0;n<active.size();++n) {
                auto& block=control.pool->GetBlock(active[n]);const auto& saved=original_states[n];
                const auto& state=block.fluid_state;
                expect(state.rho==saved.rho&&state.mom_u==saved.mom_u
                    &&state.mom_v==saved.mom_v&&state.mom_w==saved.mom_w
                    &&state.eng==saved.eng&&state.enuc_rate==saved.enuc_rate
                    &&state.mass_fractions==saved.mass_fractions,
                    "stale native identity changed physical/reflux state before rejection");
                // No local coordinate/layout/epoch changed during the mutation.
                const auto& old_grid=topology.native_grids[n];const auto& grid=block.grid;
                expect(grid.x1_min==old_grid.x1_min&&grid.x1_max==old_grid.x1_max
                    &&grid.x2_min==old_grid.x2_min&&grid.x2_max==old_grid.x2_max
                    &&grid.dx1==old_grid.dx1&&grid.dx2==old_grid.dx2
                    &&grid.ng==old_grid.ng&&grid.stride_y==old_grid.stride_y
                    &&grid.stride_z==old_grid.stride_z&&handles[n].epoch==topology.epoch,
                    "root identity cache negative accidentally changed local geometry/epoch");
                block.grid.dyadic_identity=original_seeds[n];
            }
            expect(control.RequireFluxTopologyPlan(species,rz,-1,angular).fingerprint==original_hash,
                "restoring unbound seed did not recover the actual original topology identity");
            expect(control.RequireRefluxTopologyPlan(species,rz,angular).fingerprint==original_reflux_hash,
                "restoring seed changed the original reflux mathematical plan");
        }
    }
    for(int id:active) {
        auto& g=control.pool->GetBlock(id).grid;g.x1_min+=.125;g.x1_max+=.125;
    }
    expect(control.RequireFluxTopologyPlan(species,rz,-1,angular).fingerprint!=original_hash,
        "AMR flux cache retained stale native bounds");
    expect(control.RequireRefluxTopologyPlan(species,rz,angular).fingerprint!=original_reflux_hash
        || direction==1,"AMR reflux cache retained old radial coefficients");

    bool old_flux_rejected=false;
    try { control.ApplyReflux(dt,&amr::Block::fluid_state,rz,angular); }
    catch(const std::invalid_argument&) { old_flux_rejected=true; }
    expect(old_flux_rejected,"new native geometry consumed old accumulated flux");
    control.flux_register.Clear();
    for(int id:active) {
        const auto& block=control.pool->GetBlock(id);
        std::vector<FluidVector> zero_flux(block.grid.GetTotalSize());
        std::vector<double> zero_species(species*block.grid.GetTotalSize());
        for(int dir=0;dir<2;++dir)
            amr::RegisterCoarseFineFluxes(control,id,block.grid,dir,
                zero_flux,zero_species,species,1.,rz,angular);
    }
    control.ApplyReflux(dt,&amr::Block::fluid_state,rz,angular);
    std::cout<<"RZ_REFLUX direction="<<direction<<" inner="<<inner
        <<" angular="<<angular<<" stage="<<stage<<" J_budget_error="<<static_cast<double>(J_error)
        <<" register_error="<<max_register_error<<" conservation_error="
        <<static_cast<double>(std::abs(integrated_delta-boundary_integral))<<'\n';
}


// Independent finite-ring integral reference. These tests qualify scalar
// normalization only, not the still-unmigrated runtime register consumers.
void test_rz_angular_normalization()
{
    const long double pi=std::acos(-1.L);
    long double worst=0.L;
    for(int direction:{0,1}) for(long double lo:{0.L,1.L})
    for(double stage:{1.,-.375,0.}) {
        const long double hi=lo+1.L, mid=(lo+hi)/2.L, dz=.75L;
        const auto area=[&](long double a,long double b) {
            return direction==0 ? 2.L*pi*hi*dz : pi*(b*b-a*a);
        };
        const auto torque=[&](long double a,long double b) {
            return direction==0 ? 2.L*pi*hi*hi*dz
                : 2.L*pi*(b*b*b-a*a*a)/3.L;
        };
        const long double coarse_area=area(lo,hi);
        const long double volume=pi*(hi*hi-lo*lo)*dz;
        const long double W=2.L*pi*(hi*hi*hi-lo*lo*lo)*dz/3.L;
        // Radial refinement splits the tangential z interval; axial
        // refinement splits r and requires different fine lever arms.
        long double registered=0.L, reference_torque=0.L;
        for(int child=0;child<2;++child) {
            const long double a=child==0?lo:mid,b=child==0?mid:hi;
            const long double fine_area=direction==0?coarse_area/2.L:area(a,b);
            const long double fine_torque=direction==0?torque(lo,hi)/2.L:torque(a,b);
            const double flux=child==0?2.25:-.75;
            const double value=amr::flux_math::angular_registered_flux(
                flux,static_cast<double>(fine_torque/fine_area));
            registered+=amr::flux_math::registration_coefficient(
                amr::RefinementRule::FineFluxContribution,
                static_cast<double>(fine_area/coarse_area),stage)*value;
            reference_torque+=stage*fine_torque*flux;
        }
        const double coarse_flux=.625;
        registered+=amr::flux_math::registration_coefficient(
            amr::RefinementRule::CoarseFluxContribution,1.,stage)
            *amr::flux_math::angular_registered_flux(
                coarse_flux,static_cast<double>(torque(lo,hi)/coarse_area));
        reference_torque-=stage*torque(lo,hi)*coarse_flux;
        const double dt=.03125;
        const double correction=amr::flux_math::angular_reflux_coefficient(
            static_cast<double>(dt*coarse_area/volume),
            static_cast<double>(volume),static_cast<double>(W));
        const long double measured=correction*registered*W;
        const long double expected=dt*reference_torque;
        const long double scale=dt*std::abs(static_cast<long double>(stage))
            *(torque(lo,hi)*std::abs(coarse_flux)
              +torque(lo,hi)*2.25L);
        const long double error=scale==0.L?std::abs(measured-expected)
            :std::abs(measured-expected)/scale;
        expect(error<=1.e-12L,"signed AMR torque normalization lost angular budget");
        worst=std::max(worst,error);
    }
    expect(amr::flux_math::angular_registered_flux(7.,0.)==0.,
           "axis torque flux must vanish exactly");
    std::cout<<"RZ_ANGULAR_NORMALIZATION cases=12 max_budget_error="
             <<static_cast<double>(worst)<<'\n';
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
        test_rz_angular_normalization();
        test_rz_uniform_empty_reflux();
        test_shared_math();
        test_zero_transport_species_identity();
        test_canonical_surface_lowering();
        test_zero_activation_and_signed_execution();
        test_corner_reflux_grouping();
        test_real_2d_topology_surface_partition();
        for(int direction:{0,1})for(double inner:{0.,1.})
            test_rz_registration_reflux(direction,inner);
        for(int direction:{0,1})for(double inner:{0.,1.})for(double stage:{1.,-.375})
            test_rz_registration_reflux(direction,inner,true,stage);
        std::cout << "AMR_FLUX_SURFACE_PLAN_PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
