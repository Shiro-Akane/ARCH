/**
 * @file test_amr_flux_surface_plan.cpp
 * @brief Check AMR face grouping and conservative flux correction.
 *
 * Exercise shared reflux arithmetic, surface-to-cell mapping and topology
 * partitioning, including signed and zero-weight stage contributions.
 */
#include "physics/boundary/BoundaryFlux.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <numbers>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "amr/AMRControl.h"
#include "amr/flux/AmrFluxExecutionPlan.h"
#include "amr/flux/FluxRegister.h"
#include "amr/flux/AMRFluxRegistering.h"

#include "driver/runtime/DriverRuntime.h"
#include "driver/schedule/DriverControl.h"
#include "driver/stages/DriverStages.h"
#include "physics/boundary/PhysicalBoundaryHandler.h"
#include "physics/boundary/UserBoundary.h"
#include "physics/eos/IdealGas.h"

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

/** Check provenance against original Energy entries, without using group_fields.
 * Workflow: match each existing compiled term/target to its original operands,
 * then require its exact vector position and one-to-one Energy coverage.
 */
void expect_energy_operation_indices(
    const amr::AmrFluxTopologyPlan& topology,
    const std::vector<amr::AmrFluxEndpointBinding>& bindings,
    const amr::AmrCompiledFluxTopologyPlan& compiled)
{
    expect(compiled.routes.size() == topology.routes.size(),
           "compiled Energy provenance changed route alignment");
    for (std::size_t route_index = 0; route_index < topology.routes.size(); ++route_index) {
        const auto& original = topology.routes[route_index];
        const auto& route = compiled.routes[route_index];
        const auto source = std::find_if(bindings.begin(), bindings.end(),
            [&](const auto& binding) { return binding.endpoint == original.source; });
        expect(source != bindings.end() && route.source_block == source->runtime_index,
               "compiled Energy provenance changed source binding");
        std::vector<int> visits(original.plan.operations.size(), 0);
        std::size_t checked_terms = 0;
        for (const auto& target : route.targets) {
            const auto destination = std::find_if(bindings.begin(), bindings.end(),
                [&](const auto& binding) { return binding.runtime_index == target.destination_block; });
            expect(destination != bindings.end(), "compiled Energy provenance lost destination binding");
            for (int offset = 0; offset < target.term_count; ++offset) {
                const auto& term = route.terms.at(static_cast<std::size_t>(target.first_term + offset));
                std::size_t expected = std::numeric_limits<std::size_t>::max();
                for (std::size_t index = 0; index < original.plan.operations.size(); ++index) {
                    const auto& operation = original.plan.operations[index];
                    if (operation.field != amr::AmrField::Energy
                        || operation.destination != destination->endpoint
                        || operation.rule != target.rule
                        || 2 * amr::axis_value(operation.axis) + amr::side_value(operation.side)
                            != target.destination_face
                        || operation.weight != term.geometric_weight
                        || amr::flux_execution_detail::checked_full_cell(
                            source->grid, operation.source_box, operation.axis, true) != term.source_cell
                        || amr::flux_execution_detail::checked_register_cell(
                            destination->grid, operation.destination_box, operation.axis, operation.side)
                            != target.destination_cell)
                        continue;
                    expect(expected == std::numeric_limits<std::size_t>::max(),
                           "compiled Energy provenance matched multiple original operations");
                    expected = index;
                }
                expect(expected != std::numeric_limits<std::size_t>::max()
                           && term.energy_operation_index == expected,
                       "compiled term lost original Energy vector index");
                ++visits[expected];
                ++checked_terms;
            }
        }
        expect(checked_terms == route.terms.size(), "compiled Energy provenance omitted a term");
        for (std::size_t index = 0; index < visits.size(); ++index)
            expect(visits[index] == (original.plan.operations[index].field == amr::AmrField::Energy ? 1 : 0),
                   "compiled Energy provenance duplicated or omitted an original group");
    }
}

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
    expect_energy_operation_indices(fixture.topology, fixture.bindings, compiled);
    expect(amr::AmrFluxRegistrationTerm{}.energy_operation_index
               == std::numeric_limits<std::size_t>::max(),
           "unbound Energy operation index lost its sentinel");
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

    // Valid plans require ordinal == vector index. Preserve that original gate;
    // a reordered operation vector must never acquire compiled provenance.
    auto reordered = fixture.topology;
    auto& operations = reordered.routes.front().plan.operations;
    std::reverse(operations.begin(), operations.end());
    reordered.routes.front().plan.fingerprint =
        amr::compute_amr_plan_fingerprint(reordered.routes.front().plan);
    reordered.fingerprint = amr::flux_plan_detail::compute_fingerprint(reordered);
    bool rejected = false;
    try { (void)amr::compile_amr_flux_topology_plan(reordered, fixture.bindings); }
    catch (const std::invalid_argument&) { rejected = true; }
    expect(rejected, "reordered operations escaped original canonical plan validation");
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
    expect_energy_operation_indices(topology, bindings, compiled);
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




// Independent finite-ring integral reference. These tests qualify scalar
// normalization only, not the still-unmigrated runtime register consumers.



/** Actual ordinary polar-plane Stokes/RKL2 consumer on five mixed AMR leaves.
 * Workflow: seed the existing Spherical2D (r,phi) chart away from r=0, bind
 * real no-slip user walls plus periodic phi, run the sole production composite
 * RKL owner twice, and observe its existing physical-work and CF registers.
 * rho=1, e=12, ur=uz=0, uphi=.05*sin(pi*(r-1)/2); ordinary point-state
 * EOS conventions apply. Full energy is checked with the genuine measured
 * outward mechanical work, never assumed zero from a thermal wall law.
 * This finite conservation/activity witness has no continuous PDE/order claim.
 */
void test_spherical_polar_real_mixed_rkl2()
{
    using namespace arch;
    using state::StateSlot;
    constexpr double pi=std::numbers::pi_v<double>;
    constexpr long double conservation_budget=2.e-12L;
    SimConfig config{};
    config.grid.dim=2;config.grid.geometry="spherical";
    config.grid.nblockx1=2;config.grid.nblockx2=1;config.grid.nblockx3=0;
    config.grid.x1_min=1.;config.grid.x1_max=3.;
    config.grid.x2_min=0.;config.grid.x2_max=2.*pi;config.grid.amr_max_blocks=32;
    config.grid.x1l_boundary_type=config.grid.x1r_boundary_type="user";
    config.grid.x2l_boundary_type=config.grid.x2r_boundary_type="periodic";
    config.amr.lrefinemin=0;config.amr.lrefinemax=1;
    config.numerics.sml_rho=1.e-24;config.numerics.min_eint=1.e-14;
    config.numerics.max_eint=1.e10;config.numerics.cfl=.4;config.io.tmax=1.;
    config.physics.burn.use_burn=false;config.physics.gravity.type="none";
    auto& diffusion=config.physics.diffusion;
    diffusion.use_diffusion=diffusion.use_viscous_diffusion=true;
    // A temperature prescription requires its real channel to be enabled.
    // alpha=0 is legal: this witness qualifies nonzero viscosity, not heating by conduction.
    diffusion.use_thermal_diffusion=true;diffusion.alpha_therm=0.;
    diffusion.use_species_diffusion=false;diffusion.nu_visc=.03;
    diffusion.diff_cfl=.8;diffusion.max_stages=17;diffusion.integrator="RKL2";
    SpeciesManager species;species.add_species("polar-gas",1.,1.,1.4,3.);
    IdealGas eos(1.4,species);
    amr::AMRControl control(32,2);
    control.tree->LoadLeafGrid(config,1,{1,1,1,1,0},
        {0,1,0,1,1},{0,0,1,1,0},{0,0,0,0,0});
    control.flux_register.EnsureSpecies(1);
    const auto& active=control.tree->GetActiveBlocks();
    expect(active.size()==5,"ordinary polar RKL2 lost its five actual mixed leaves");
    for(int id:active) {
        auto& block=control.pool->GetBlock(id);const auto& grid=block.grid;
        for(auto* field:std::array<FluidState*,3>{
            &block.fluid_state,&block.state_next,&block.state_scratch}) {
            field->stage_repairs.reset(1);
            for(int cell=0;cell<grid.GetTotalSize();++cell) {
                field->set(cell,{1.,0.,0.,0.,12.});field->X(0,cell)=1.;
            }
            for(int j=0;j<grid.GetTotalY();++j)for(int i=0;i<grid.GetTotalX();++i) {
                const double tangential=.05*std::sin(pi*(grid.GetCellCenterX(i)-1.)/2.);
                field->set(grid.GetIndex(i,j,0),{1.,0.,tangential,0.,12.+.5*tangential*tangential});
            }
        }
    }
    std::array<std::atomic<std::size_t>,2> diffusion_calls{};
    boundary::ResolvedUserBoundaries callbacks;
    callbacks.identity="ordinary-polar-no-slip-zero-temperature-gradient";
    /** The original pure point law owns velocity reflection and EOS conversion. */
    callbacks.physical=[&](const boundary::PhysicalBoundaryContext& request) {
        expect(request.axis==boundary::BoundaryAxis::X1&&request.dimension==2,
            "ordinary polar physical callback escaped the two radial faces");
        boundary::PhysicalBoundaryData data;
        if(request.purpose==boundary::BoundaryPurpose::Hydro) {
            auto wall=request.interior;wall.u=wall.v=wall.w=0.;data.hydro=std::move(wall);
        } else {
            expect(request.purpose==boundary::BoundaryPurpose::Diffusion,
                "ordinary polar wall received a nonphysical purpose");
            diffusion_calls[static_cast<std::size_t>(request.side)].fetch_add(1,std::memory_order_relaxed);
            data.temperature={boundary::ScalarBoundaryKind::NormalGradient,0.};
            for(auto& velocity:data.velocity)velocity={boundary::ScalarBoundaryKind::Value,0.};
        }
        return data;
    };
    boundary::ScopedUserBoundarySelection selection(std::move(callbacks),config,species);
    RunState start{};start.repairs.reset(1);
    SimulationController counters(config,start);
    BCHandler boundary(config);boundary.bind(eos,species);
    boundary.configure_stage(0.,boundary::BoundaryPurpose::Diffusion);
    driver::DriverRuntime runtime(control,boundary,config,species,counters);
    runtime.initialize_topology();auto context=runtime.stage_context();
    driver::DriverStageWorkspace workspace;
    context.configure_boundary_context=[&](double time,boundary::BoundaryPurpose purpose) {
        boundary.configure_stage(time,purpose);
    };
    context.physical_boundary_preparation=[&](StateSlot slot,double time,boundary::BoundaryPurpose purpose) {
        context.configure_boundary_context(time,purpose);runtime.ensure_fluid_ghosts(slot);
    };
    /** Validate real completed logical cells with the same selected point EOS. */
    const auto validate_eos=[&](StateSlot slot) {
        for(int id:active) {
            const auto& block=control.pool->GetBlock(id);const auto& grid=block.grid;
            const auto& field=slot==StateSlot::Current?block.fluid_state:
                slot==StateSlot::Next?block.state_next:block.state_scratch;
            for(int j=0;j<grid.GetTotalY();++j)for(int i=0;i<grid.GetTotalX();++i) {
                const int cell=grid.GetIndex(i,j,0);const auto value=field.get(cell);
                const double fraction=field.X(0,cell);
                const auto recovered=state::recover(value);
                const double pressure=eos.get_pressure(value,&fraction);
                const double temperature=eos.get_temperature(value.rho,recovered.internal,&fraction);
                expect(state::accepted(recovered.status)&&std::isfinite(pressure)&&pressure>0.
                    &&std::isfinite(temperature)&&temperature>0.,
                    "ordinary polar completed cell failed its real selected EOS");
            }
        }
    };
    std::size_t observed_stages=0,nonzero_cf_stages=0;
    validate_eos(StateSlot::Current);
    runtime.bind_boundary_accounting(context);
    expect(bool(context.rkl_flux_capture_accept),"ordinary polar user walls omitted real mechanical-work accounting");
    const auto accept_surface=context.rkl_flux_capture_accept;
    double cf_momentum_linf=0.,cf_energy_linf=0.;
    /** Observe the existing completed stage; retain its sole receipt producer once. */
    context.rkl_flux_capture_accept=[&](const scheduler::RklStageDescriptor& stage,const scheduler::RklPlan& plan) {
        accept_surface(stage,plan);++observed_stages;
        bool active_momentum=false,active_energy=false;
        for(int id:active)for(int face=0;face<4;++face) {
            const auto& block=control.pool->GetBlock(id);
            if(block.face_neighbors[face].level_diff<=0||!control.flux_register.HasData(id,face))continue;
            const int face_cells=face<2?amr::BLOCK_NY:amr::BLOCK_NX;
            for(int cell=0;cell<face_cells;++cell) {
                const auto actual=control.flux_register.GetSummedFlux(id,face,cell);
                expect(std::isfinite(actual.mom_v)&&std::isfinite(actual.eng),"ordinary polar actual CF residual is nonfinite");
                cf_momentum_linf=std::max(cf_momentum_linf,std::abs(actual.mom_v));
                cf_energy_linf=std::max(cf_energy_linf,std::abs(actual.eng));
                active_momentum=active_momentum||actual.mom_v!=0.;active_energy=active_energy||actual.eng!=0.;
            }
        }
        if(active_momentum&&active_energy)++nonzero_cf_stages;
    };
    expect(!control.RequireRefluxTopologyPlan(1).operations.empty(),"ordinary polar real RKL2 has no CF correction plan");
    dispatch::ResolvedExecutionPlan plan{};plan.diffusion_integrator=dispatch::DiffusionIntegratorId::Rkl2;
    const auto candidate=driver::calculate_timestep_candidates(runtime,workspace,eos,&plan);
    const double dt=8.*candidate.diffusion_forward_euler*diffusion.diff_cfl;
    const int stages=DiffFunction::compute_stages(DiffFunction::RKLOrder::Second,dt,
        candidate.diffusion_forward_euler,diffusion.diff_cfl,diffusion.max_stages);
    expect(std::isfinite(dt)&&dt>0.&&stages>=2,"ordinary polar RKL2 skipped its real recurrence");
    /** Integrate actual cell measures; Spherical2D is the r dr dphi plane. */
    const auto totals=[&]() {
        std::array<long double,3> result{};
        for(int id:active) {
            const auto& block=control.pool->GetBlock(id);const auto& grid=block.grid;
            for(int j=grid.Js();j<grid.Je();++j)for(int i=grid.Is();i<grid.Ie();++i) {
                const long double volume=GridMetrics::CellVolume(grid,i,j,0);
                const auto value=block.fluid_state.get(grid.GetIndex(i,j,0));
                expect(std::isfinite(volume)&&volume>0.,"ordinary polar actual cell volume is invalid");
                result[0]+=volume*value.rho;result[1]+=volume*value.eng;
                result[2]+=volume*((long double)value.mom_u*value.mom_u
                    +(long double)value.mom_v*value.mom_v+(long double)value.mom_w*value.mom_w)/(2.L*value.rho);
            }
        }
        return result;
    };
    const auto initial=totals();long double previous_kinetic=initial[2];
    for(int macro=0;macro<2;++macro) {
        context.step_start_time=context.boundary_start_time=macro*dt;
        context.step_dt=context.boundary_step_dt=dt;
        const auto before=context.ledger.inspect({runtime.handles().front(),StateSlot::Current}).interior.version;
        const auto previous_cf=nonzero_cf_stages;
        std::vector<double> previous_energy;
        for(int id:active) {
            const auto& block=control.pool->GetBlock(id);const auto& grid=block.grid;
            for(int j=grid.Js();j<grid.Je();++j)for(int i=grid.Is();i<grid.Ie();++i)
                previous_energy.push_back(block.fluid_state.eng[grid.GetIndex(i,j,0)]);
        }
        {
            scheduler::ScopedStageBinding binding(context,runtime.handles());
            driver::advance_diffusion(runtime,workspace,context,eos,&plan,macro,dt,candidate.diffusion_forward_euler);
        }
        validate_eos(StateSlot::Current);
        const auto after=totals();const auto& outward=runtime.diffusion_boundary_budget();
        expect(outward.size()==static_cast<std::size_t>(6+species.count()),"ordinary polar RKL2 lost actual surface receipt");
        const long double mass_error=(after[0]-initial[0]+outward[0])/initial[0];
        const long double energy_error=(after[1]-initial[1]+outward[4])/initial[1];
        expect(std::isfinite(mass_error)&&std::isfinite(energy_error)
            &&std::abs(mass_error)<=conservation_budget&&std::abs(energy_error)<=conservation_budget,
            "ordinary polar mixed RKL2 lost mass/energy plus actual mechanical surface work");
        expect(after[2]<previous_kinetic,"ordinary polar mixed RKL2 failed finite kinetic dissipation");
        previous_kinetic=after[2];
        expect(nonzero_cf_stages>previous_cf,"ordinary polar macrostep bypassed active momentum/energy CF correction");
        std::size_t cell_count=0,changed_energy=0;
        for(std::size_t n=0;n<active.size();++n) {
            const auto& block=control.pool->GetBlock(active[n]);const auto& grid=block.grid;
            const auto coherence=context.ledger.inspect({runtime.handles()[n],StateSlot::Current});
            expect(coherence.interior.version.value==before.value+stages,"ordinary polar RKL2 missed real stage publication");
            context.ledger.require_readable({runtime.handles()[n],StateSlot::Current},
                {state::ExecutionSide::Host,coherence.interior.version,true,true});
            for(double value:block.fluid_state.stage_repairs.values)expect(value==0.,"ordinary polar current has a floor repair");
            for(int j=grid.Js();j<grid.Je();++j)for(int i=grid.Is();i<grid.Ie();++i) {
                const int cell=grid.GetIndex(i,j,0);
                expect(std::bit_cast<std::uint64_t>(block.fluid_state.rho[cell])==std::bit_cast<std::uint64_t>(1.)
                    &&std::bit_cast<std::uint64_t>(block.fluid_state.X(0,cell))==std::bit_cast<std::uint64_t>(1.),
                    "ordinary polar RKL2 changed stationary rho/X bits");
                changed_energy+=block.fluid_state.eng[cell]!=previous_energy[cell_count++];
            }
        }
        expect(changed_energy>0,"ordinary polar RKL2 skipped its nonlinear paired-energy consumer");
        for(double value:runtime.repair_budget().values)expect(value==0.,"ordinary polar RKL2 concealed repair accounting");
        std::cout<<"SPHERICAL_POLAR_MIXED_REAL_RKL2_SURFACE macro="<<macro
            <<" mass_error="<<double(mass_error)<<" energy_error="<<double(energy_error)
            <<" actual_outward_work="<<outward[4]<<" changed_energy="<<changed_energy
            <<" nonzero_cf_stages="<<nonzero_cf_stages-previous_cf<<'\n';
    }
    expect(observed_stages==static_cast<std::size_t>(2*stages),
        "ordinary polar RKL2 missed actual stage receipts");
    expect(diffusion_calls[0].load()>0&&diffusion_calls[1].load()>0,"ordinary polar RKL2 skipped an actual radial wall callback");
    std::cout<<"SPHERICAL_POLAR_MIXED_REAL_RKL2_PASS leaves=5 macro_steps=2 stages="<<stages
        <<" dt="<<dt<<" kinetic_ratio="<<double(previous_kinetic/initial[2])
        <<" cf_momentum_linf="<<cf_momentum_linf<<" cf_energy_linf="<<cf_energy_linf
        <<" real_EOS=1 zero_repairs=1 thermal_activity=0 continuous_PDE_reference=0\n";
}

} // namespace

int main()
{
    try {
        test_shared_math();
        test_zero_transport_species_identity();
        test_canonical_surface_lowering();
        test_zero_activation_and_signed_execution();
        test_corner_reflux_grouping();
        test_real_2d_topology_surface_partition();
        for(int direction:{0,1})for(double inner:{0.,1.})
        for(int direction:{0,1})for(double inner:{0.,1.})for(double stage:{1.,-.375})
        test_spherical_polar_real_mixed_rkl2();
        std::cout << "AMR_FLUX_SURFACE_PLAN_PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
