/**
 * @file test_amr_flux_surface_plan.cpp
 * @brief Check AMR face grouping and conservative flux correction.
 *
 * Exercise shared reflux arithmetic, surface-to-cell mapping and topology
 * partitioning, including signed and zero-weight stage contributions.
 */
#include "amr/AmrFluxExecutionPlan.h"
#include "amr/AMRControl.h"
#include "amr/FluxRegister.h"

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

} // namespace

int main()
{
    try {
        test_shared_math();
        test_canonical_surface_lowering();
        test_zero_activation_and_signed_execution();
        test_corner_reflux_grouping();
        test_real_2d_topology_surface_partition();
        std::cout << "AMR_FLUX_SURFACE_PLAN_PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
