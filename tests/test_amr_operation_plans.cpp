#include "amr/AmrTransferPlans.h"
#include "amr/CoarseFineCellPlan.h"
#include "amr/ConservativeRestriction.h"
#include "amr/AMRFluxRegistering.h"
#include "amr/FluxRegister.h"
#include "amr/GhostExchange.h"
#include "amr/LimitedLinearProlongation.h"
#include "numerics/reconstruction/AMRInterfaceStencil.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>

static_assert(std::is_trivially_copyable_v<amr::LogicalAmrBox>);
static_assert(std::is_standard_layout_v<amr::AmrTransferOperation>);
static_assert(sizeof(amr::AmrTransferOperation) <= 256);

namespace {

void expect(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

template <typename Function>
void expect_rejected(Function&& function, const std::string& message)
{
    bool rejected = false;
    try {
        function();
    } catch (const std::invalid_argument&) {
        rejected = true;
    } catch (const std::overflow_error&) {
        rejected = true;
    }
    expect(rejected, message);
}

void test_conservative_restriction_math()
{
    const double density_integral =
        amr::restriction_math::weighted_conserved_value(1.0, 1.0)
        + amr::restriction_math::weighted_conserved_value(3.0, 1.0);
    const double species_density_integral =
        amr::restriction_math::weighted_species_density(1.0, 1.0, 1.0)
        + amr::restriction_math::weighted_species_density(3.0, 0.0, 1.0);
    expect(amr::restriction_math::restricted_mass_fraction(
               species_density_integral, density_integral) == 0.25,
           "shared Host/device rho-X restriction drifted");

    const double weighted_density_integral =
        amr::restriction_math::weighted_conserved_value(1.0, 1.0)
        + amr::restriction_math::weighted_conserved_value(3.0, 3.0);
    const double weighted_species_density_integral =
        amr::restriction_math::weighted_species_density(1.0, 1.0, 1.0)
        + amr::restriction_math::weighted_species_density(3.0, 0.0, 3.0);
    expect(amr::restriction_math::restricted_mass_fraction(
               weighted_species_density_integral,
               weighted_density_integral) == 0.1,
           "shared physical-volume rho-X restriction drifted");
}

void test_limited_linear_prolongation_math()
{
    const double lower[3]{8.0, 8.0, 0.0};
    const double upper[3]{12.0, 12.0, 0.0};
    double sum = 0.0;
    for (int child = 0; child < 4; ++child) {
        const double position[3]{
            (child & 1) != 0 ? 0.25 : -0.25,
            (child & 2) != 0 ? 0.25 : -0.25, 0.0};
        const double value = amr::prolongation_math::limited_linear_value(
            10.0, lower, upper, position, 2);
        expect(value >= 8.0 && value <= 12.0,
               "limited-linear prolongation exceeded its stencil bounds");
        sum += value;
    }
    expect(sum == 40.0,
           "symmetric limited-linear children did not conserve the parent");

    const double extremum_lower[3]{9.0, 0.0, 0.0};
    const double extremum_upper[3]{8.0, 0.0, 0.0};
    const double position[3]{0.25, 0.0, 0.0};
    expect(amr::prolongation_math::limited_linear_value(
               10.0, extremum_lower, extremum_upper, position, 1) == 10.0,
           "limited-linear prolongation did not flatten an extremum");
}

amr::AmrEndpoint endpoint(int level, std::uint32_t x,
                          std::uint64_t uid, std::uint64_t epoch)
{
    return {{1, level, x, 0, 0}, {{uid}, {epoch}}};
}

amr::AmrTransferOperation operation(amr::AmrField field,
                                    std::int32_t component = -1)
{
    return {
        0,
        endpoint(0, 0, 10, 7),
        endpoint(1, 1, 20, 7),
        {{0, 0, 0}, {2, 1, 1}},
        {{-2, 0, 0}, {2, 1, 1}},
        amr::AmrAxis::X,
        amr::AmrSide::Lower,
        field,
        component,
        amr::RefinementRule::CoarseGhostInjection,
        1.0,
        1.0};
}

amr::CoarseFineTransferPlan ordinary_plan()
{
    amr::CoarseFineTransferPlan plan{};
    plan.dimension = 1;
    plan.scope = {0, {7}, {7}};
    plan.operations.push_back(operation(amr::AmrField::Species, 1));
    plan.operations.push_back(operation(amr::AmrField::Rho));
    amr::finalize_amr_plan(plan);
    return plan;
}

amr::ProlongationPlan migration_plan()
{
    amr::ProlongationPlan plan{};
    plan.dimension = 1;
    plan.scope = {91, {7}, {8}};
    auto value = operation(amr::AmrField::Energy);
    value.destination.handle.epoch = {8};
    value.rule = amr::RefinementRule::ConservativeMinmodProlongation;
    value.weight = 0.5;
    plan.operations.push_back(value);
    amr::finalize_amr_plan(plan);
    return plan;
}

void test_public_contract()
{
    expect(static_cast<int>(amr::AmrAxis::X) == 0
               && static_cast<int>(amr::AmrAxis::Y) == 1
               && static_cast<int>(amr::AmrAxis::Z) == 2,
           "AMR axis values drifted");
    expect(static_cast<int>(amr::AmrSide::Lower) == 0
               && static_cast<int>(amr::AmrSide::Upper) == 1,
           "AMR side values drifted");
    expect(static_cast<int>(amr::AmrField::Rho) == 0
               && static_cast<int>(amr::AmrField::Species) == 6,
           "AMR field order drifted");

    const auto ordinary = ordinary_plan();
    expect(ordinary.operations.size() == 2,
           "ordinary AMR operation count drifted");
    expect(ordinary.operations[0].field == amr::AmrField::Rho
               && ordinary.operations[1].field == amr::AmrField::Species,
           "AMR canonical field order drifted");
    expect(ordinary.operations[0].ordinal == 0
               && ordinary.operations[1].ordinal == 1,
           "AMR ordinals drifted");
    expect(ordinary.fingerprint == UINT64_C(0x534e24ea68b3f6bf),
           "ordinary AMR literal fingerprint drifted");

    const auto migration = migration_plan();
    expect(migration.scope.transaction_id == 91
               && migration.operations[0].source.handle.epoch.value == 7
               && migration.operations[0].destination.handle.epoch.value == 8,
           "AMR migration scope drifted");
    expect(migration.fingerprint == UINT64_C(0xf6c7dabb6721000d),
           "migration AMR literal fingerprint drifted");
}

void test_validation()
{
    {
        auto plan = ordinary_plan();
        plan.operations[0].ordinal = 7;
        expect_rejected([&] { amr::validate_amr_plan(plan); },
                        "noncontiguous ordinal accepted");
    }
    {
        auto plan = ordinary_plan();
        plan.operations[0].source.handle.epoch = {8};
        plan.fingerprint = amr::compute_amr_plan_fingerprint(plan);
        expect_rejected([&] { amr::validate_amr_plan(plan); },
                        "mixed ordinary epoch accepted");
    }
    {
        auto plan = migration_plan();
        plan.scope.transaction_id = 0;
        plan.fingerprint = amr::compute_amr_plan_fingerprint(plan);
        expect_rejected([&] { amr::validate_amr_plan(plan); },
                        "zero migration transaction accepted");
    }
    {
        auto plan = ordinary_plan();
        plan.operations[0].field = amr::AmrField::Species;
        plan.operations[0].component = -1;
        plan.fingerprint = amr::compute_amr_plan_fingerprint(plan);
        expect_rejected([&] { amr::validate_amr_plan(plan); },
                        "negative species component accepted");
    }
    {
        auto plan = ordinary_plan();
        plan.operations[0].weight = std::numeric_limits<double>::quiet_NaN();
        plan.fingerprint = amr::compute_amr_plan_fingerprint(plan);
        expect_rejected([&] { amr::validate_amr_plan(plan); },
                        "nonfinite AMR weight accepted");
    }
    {
        auto plan = ordinary_plan();
        plan.operations[0].weight = std::numeric_limits<double>::infinity();
        plan.fingerprint = amr::compute_amr_plan_fingerprint(plan);
        expect_rejected([&] { amr::validate_amr_plan(plan); },
                        "positive-infinite AMR weight accepted");
    }
    {
        auto plan = ordinary_plan();
        plan.operations[0].weight = -std::numeric_limits<double>::infinity();
        plan.fingerprint = amr::compute_amr_plan_fingerprint(plan);
        expect_rejected([&] { amr::validate_amr_plan(plan); },
                        "negative-infinite AMR weight accepted");
    }
    {
        auto plan = ordinary_plan();
        plan.operations[0].sign = std::numeric_limits<double>::quiet_NaN();
        plan.fingerprint = amr::compute_amr_plan_fingerprint(plan);
        expect_rejected([&] { amr::validate_amr_plan(plan); },
                        "nonfinite AMR sign accepted");
    }
    {
        auto plan = ordinary_plan();
        plan.operations[0].destination_box.extent[0] = 0;
        plan.fingerprint = amr::compute_amr_plan_fingerprint(plan);
        expect_rejected([&] { amr::validate_amr_plan(plan); },
                        "zero AMR box extent accepted");
    }
    {
        auto plan = ordinary_plan();
        plan.operations.push_back(plan.operations.back());
        plan.operations.back().ordinal = 2;
        plan.fingerprint = amr::compute_amr_plan_fingerprint(plan);
        expect_rejected([&] { amr::validate_amr_plan(plan); },
                        "duplicate AMR logical operation accepted");
    }
    {
        auto plan = ordinary_plan();
        plan.operations[0].field = static_cast<amr::AmrField>(255);
        plan.fingerprint = amr::compute_amr_plan_fingerprint(plan);
        expect_rejected([&] { amr::validate_amr_plan(plan); },
                        "invalid AMR field accepted");
    }
    {
        auto plan = ordinary_plan();
        plan.fingerprint ^= UINT64_C(1);
        expect_rejected([&] { amr::validate_amr_plan(plan); },
                        "AMR fingerprint mismatch accepted");
    }
}

void fill_block(amr::Block& block)
{
    const int total = block.grid.GetTotalSize();
    const int species_count = block.fluid_state.GetNumSpecies();
    const double species_weight_sum = 0.5 * species_count
        * (species_count + 1.0);
    for (int index = 0; index < total; ++index) {
        const double value = 1000.0 * block.level
            + 100.0 * block.logical_x1 + index;
        block.fluid_state.rho[index] = value + 1.0;
        block.fluid_state.mom_u[index] = value + 2.0;
        block.fluid_state.mom_v[index] = value + 3.0;
        block.fluid_state.mom_w[index] = value + 4.0;
        block.fluid_state.eng[index] = value + 5.0;
        block.fluid_state.enuc_rate[index] = value + 6.0;
        for (int species = 0; species < species_count; ++species)
            block.fluid_state.X(species, index) = species_count == 0
                ? 0.0 : (species + 1.0) / species_weight_sum;
    }
    block.state_next = block.fluid_state;
    block.state_scratch = block.fluid_state;
    for (int index = 0; index < total; ++index) {
        block.state_next.rho[index] += 10000.0;
        block.state_next.mom_u[index] += 10000.0;
        block.state_next.mom_v[index] += 10000.0;
        block.state_next.mom_w[index] += 10000.0;
        block.state_next.eng[index] += 10000.0;
        block.state_next.enuc_rate[index] += 10000.0;
        block.state_scratch.rho[index] += 20000.0;
        block.state_scratch.mom_u[index] += 20000.0;
        block.state_scratch.mom_v[index] += 20000.0;
        block.state_scratch.mom_w[index] += 20000.0;
        block.state_scratch.eng[index] += 20000.0;
        block.state_scratch.enuc_rate[index] += 20000.0;
    }
}

void test_amr_interface_stencil_predicate()
{
    bool host_flags[6]{false, false, false, false, false, false};
    expect(!AMRInterfaceReconstruction::needs_tvd_interface_stencil(
               3, host_flags, 0, 7, 4, 20),
           "unmarked PPM face was lowered");
    host_flags[0] = true;
    expect(AMRInterfaceReconstruction::needs_tvd_interface_stencil(
               3, host_flags, 0, 5, 4, 20)
               && !AMRInterfaceReconstruction::needs_tvd_interface_stencil(
                   3, host_flags, 0, 6, 4, 20),
           "lower coarse-fine PPM threshold drifted");
    expect(!AMRInterfaceReconstruction::needs_tvd_interface_stencil(
               2, host_flags, 0, 5, 4, 20),
           "native MUSCL stencil was unnecessarily lowered");

    std::uint8_t device_flags[6]{0, 0, 0, 1, 0, 0};
    expect(AMRInterfaceReconstruction::needs_tvd_interface_stencil(
               3, device_flags, 1, 17, 4, 20)
               && !AMRInterfaceReconstruction::needs_tvd_interface_stencil(
                   3, device_flags, 1, 16, 4, 20),
           "upper device coarse-fine PPM threshold drifted");
    expect(AMRInterfaceReconstruction::needs_tvd_interface_stencil(
               3, device_flags, 3, 10, 4, 20),
           "invalid wide-stencil metadata did not take the narrow path");
}

amr::CoarseFineTransferPlan geometric_plan(
    int dimension, amr::RefinementRule rule,
    const amr::LogicalAmrBox& source_box,
    const amr::LogicalAmrBox& destination_box)
{
    const bool injection =
        rule == amr::RefinementRule::CoarseGhostInjection;
    amr::CoarseFineTransferPlan plan{};
    plan.dimension = dimension;
    plan.scope = {0, {9}, {9}};
    const amr::AmrEndpoint source{
        {dimension, injection ? 0 : 1, 0, 0, 0}, {{41}, {9}}};
    const amr::AmrEndpoint destination{
        {dimension, injection ? 1 : 0, 0, 0, 0}, {{42}, {9}}};
    for (int field = 0; field < 6; ++field) {
        plan.operations.push_back({
            0, source, destination, source_box, destination_box,
            amr::AmrAxis::X, amr::AmrSide::Lower,
            static_cast<amr::AmrField>(field), -1, rule, 1.0, 1.0});
    }
    amr::finalize_amr_plan(plan);
    return plan;
}

void test_multidimensional_cell_lowering()
{
    const auto injection = geometric_plan(
        2, amr::RefinementRule::CoarseGhostInjection,
        {{14, 0, 0}, {2, 8, 1}}, {{-4, 0, 0}, {4, 16, 1}});
    const auto injected = amr::compile_coarse_fine_cell_plan(injection, 0);
    expect(injected.transfers.size() == 64
               && injected.transfers.front().source_count == 1
               && injected.transfers.front().destination_cell
                   == amr::LogicalAmrCell{-4, 0, 0}
               && injected.transfers.front().source_cells[0]
                   == amr::LogicalAmrCell{14, 0, 0}
               && injected.transfers.front().slope_cells[0]
                   == amr::LogicalAmrCell{13, 0, 0}
               && injected.transfers.front().slope_cells[1]
                   == amr::LogicalAmrCell{15, 0, 0}
               && injected.transfers.front().slope_cells[2]
                   == amr::LogicalAmrCell{14, -1, 0}
               && injected.transfers.front().slope_cells[3]
                   == amr::LogicalAmrCell{14, 1, 0}
               && injected.transfers.front().fine_position
                   == std::array<double, 3>{-0.25, -0.25, 0.0},
           "2D coarse injection cell mathematics drifted");

    const auto average = geometric_plan(
        3, amr::RefinementRule::FineGhostAverage,
        {{8, 0, 0}, {8, 16, 16}}, {{-4, 0, 0}, {4, 8, 8}});
    const auto averaged = amr::compile_coarse_fine_cell_plan(average, 0);
    expect(averaged.transfers.size() == 256
               && averaged.transfers.front().source_count == 8
               && averaged.transfers.front().destination_cell
                   == amr::LogicalAmrCell{-4, 0, 0}
               && averaged.transfers.front().source_cells.front()
                   == amr::LogicalAmrCell{8, 0, 0}
               && averaged.transfers.front().source_cells[7]
                   == amr::LogicalAmrCell{9, 1, 1},
           "3D fine average cell mathematics drifted");

    auto weighted = injection;
    weighted.operations.front().weight = 0.5;
    weighted.fingerprint = amr::compute_amr_plan_fingerprint(weighted);
    expect_rejected(
        [&] { (void)amr::compile_coarse_fine_cell_plan(weighted, 0); },
        "weighted logical transfer was silently treated as a copy");
}

void test_curvilinear_host_restriction()
{
    SimConfig config{};
    config.grid.dim = 1;
    config.grid.nblockx1 = 2;
    config.grid.nblockx2 = 0;
    config.grid.nblockx3 = 0;
    config.grid.geometry = "cylindrical";
    config.grid.amr_max_blocks = 16;
    config.amr.lrefinemin = 0;
    config.amr.lrefinemax = 1;

    amr::AMRControl control(16, 1);
    auto pool = control.pool;
    auto tree = control.tree;
    tree->LoadLeafGrid(
        config, 1, std::vector<int>{1, 1, 0},
        std::vector<std::uint32_t>{0, 1, 1},
        std::vector<std::uint32_t>{0, 0, 0},
        std::vector<std::uint32_t>{0, 0, 0});

    const auto& active = tree->GetActiveBlocks();
    std::vector<amr::BlockHandle> handles;
    handles.reserve(active.size());
    for (std::size_t index = 0; index < active.size(); ++index) {
        handles.push_back({{200 + index}, {47}});
        fill_block(pool->GetBlock(active[index]));
    }
    control.BindActiveHandles(handles);
    const auto plan = control.ghost_exchange.BuildCoarseFinePlan(
        pool, tree, 1, handles);
    const auto cell_plan = amr::compile_coarse_fine_cell_plan(plan, 1);
    const auto selected = std::find_if(
        cell_plan.transfers.begin(), cell_plan.transfers.end(),
        [](const amr::CoarseFineCellTransfer& transfer) {
            return transfer.rule == amr::RefinementRule::FineGhostAverage;
        });
    expect(selected != cell_plan.transfers.end()
               && selected->source_count == 2,
           "curvilinear fixture has no fine restriction route");

    const auto block_for = [&](const amr::BlockHandle handle) -> amr::Block& {
        for (std::size_t index = 0; index < handles.size(); ++index)
            if (handles[index] == handle)
                return pool->GetBlock(active[index]);
        throw std::runtime_error("curvilinear fixture handle is missing");
    };
    amr::Block& source = block_for(selected->source.handle);
    amr::Block& destination = block_for(selected->destination.handle);
    const auto cell_index = [](const Grid& grid,
                               const amr::LogicalAmrCell& logical) {
        return grid.GetIndex(
            grid.Is() + logical[0], grid.Js() + logical[1],
            grid.Ks() + logical[2]);
    };
    const int source_a = cell_index(source.grid, selected->source_cells[0]);
    const int source_b = cell_index(source.grid, selected->source_cells[1]);
    const int destination_cell = cell_index(
        destination.grid, selected->destination_cell);
    source.fluid_state.rho[source_a] = 1.0;
    source.fluid_state.rho[source_b] = 3.0;
    source.fluid_state.mom_u[source_a] = 2.0;
    source.fluid_state.mom_u[source_b] = 6.0;
    source.fluid_state.enuc_rate[source_a] = -4.0;
    source.fluid_state.enuc_rate[source_b] = 8.0;
    source.fluid_state.X(0, source_a) = 1.0;
    source.fluid_state.X(0, source_b) = 0.0;

    const auto measure = [&](const amr::LogicalAmrCell& logical) {
        return GridMetrics::CellVolume(
            source.grid, source.grid.Is() + logical[0],
            source.grid.Js() + logical[1],
            source.grid.Ks() + logical[2]);
    };
    const double measure_a = measure(selected->source_cells[0]);
    const double measure_b = measure(selected->source_cells[1]);
    const double measure_sum = measure_a + measure_b;
    const double density_integral =
        amr::restriction_math::weighted_conserved_value(1.0, measure_a)
        + amr::restriction_math::weighted_conserved_value(3.0, measure_b);
    const double expected_rho = amr::restriction_math::restricted_average(
        density_integral, measure_sum);
    const double expected_mom_u =
        amr::restriction_math::restricted_average(
            amr::restriction_math::weighted_conserved_value(2.0, measure_a)
                + amr::restriction_math::weighted_conserved_value(
                    6.0, measure_b),
            measure_sum);
    const double expected_enuc =
        amr::restriction_math::restricted_average(
            amr::restriction_math::weighted_conserved_value(-4.0, measure_a)
                + amr::restriction_math::weighted_conserved_value(
                    8.0, measure_b),
            measure_sum);
    const double expected_x =
        amr::restriction_math::restricted_mass_fraction(
            amr::restriction_math::weighted_species_density(
                1.0, 1.0, measure_a)
                + amr::restriction_math::weighted_species_density(
                    3.0, 0.0, measure_b),
            density_integral);

    control.ghost_exchange.ExecuteCoarseFinePlan(
        plan, pool, tree, 1, &amr::Block::fluid_state, handles);
    expect(destination.fluid_state.rho[destination_cell] == expected_rho
               && destination.fluid_state.mom_u[destination_cell]
                   == expected_mom_u
               && destination.fluid_state.enuc_rate[destination_cell]
                   == expected_enuc
               && destination.fluid_state.X(0, destination_cell)
                   == expected_x,
           "curvilinear Host restriction ignored physical cell volume");
    expect(expected_rho != 2.0 && expected_x != 0.25,
           "curvilinear test did not distinguish volume weighting");
}

void test_mixed_level_and_coarse_fine_execution()
{
    SimConfig config{};
    config.grid.dim = 1;
    config.grid.nblockx1 = 2;
    config.grid.nblockx2 = 0;
    config.grid.nblockx3 = 0;
    config.grid.amr_max_blocks = 16;
    config.amr.lrefinemin = 0;
    config.amr.lrefinemax = 1;

    amr::AMRControl control(16, 1);
    auto pool = control.pool;
    auto tree = control.tree;
    tree->LoadLeafGrid(
        config, 2,
        std::vector<int>{1, 1, 0},
        std::vector<std::uint32_t>{0, 1, 1},
        std::vector<std::uint32_t>{0, 0, 0},
        std::vector<std::uint32_t>{0, 0, 0});

    const auto& active = tree->GetActiveBlocks();
    std::vector<amr::BlockHandle> handles;
    handles.reserve(active.size());
    for (std::size_t index = 0; index < active.size(); ++index) {
        handles.push_back({{100 + index}, {31}});
        fill_block(pool->GetBlock(active[index]));
    }
    control.BindActiveHandles(handles);
    control.flux_register.EnsureSpecies(2);

    amr::GhostExchange& exchange = control.ghost_exchange;
    const double fail_closed_witness =
        pool->GetBlock(active.front()).fluid_state.rho.front();
    expect_rejected(
        [&] { (void)exchange.BuildSameLevelPlans(pool, tree, 1); },
        "same-level planning accepted empty handles");
    expect_rejected(
        [&] { exchange.ExecuteExchange(
            pool, tree, 1, &amr::Block::fluid_state); },
        "Host exchange accepted empty handles");
    const std::span<const amr::BlockHandle> short_handles(
        handles.data(), handles.size() - 1);
    expect_rejected(
        [&] { exchange.ExecuteExchange(
            pool, tree, 1, &amr::Block::fluid_state, short_handles); },
        "Host exchange accepted an incomplete identity view");
    expect(pool->GetBlock(active.front()).fluid_state.rho.front()
               == fail_closed_witness,
           "rejected Host exchange modified state");

    const auto same_level = exchange.BuildSameLevelPlans(
        pool, tree, 1, handles);
    expect(same_level.size() == 2,
           "mixed AMR hierarchy did not produce one plan per level");
    expect_rejected(
        [&] { (void)exchange.BuildSameLevelPlan(pool, tree, 1, handles); },
        "mixed AMR hierarchy was accepted as one same-level plan");

    const auto plan = exchange.BuildCoarseFinePlan(pool, tree, 1, handles);
    expect(plan.operations.size() == 16,
           "coarse-fine plan did not cover two routes and eight fields");
    expect(plan.operations.front().rule
               == amr::RefinementRule::FineGhostAverage
               || plan.operations.front().rule
               == amr::RefinementRule::CoarseGhostInjection,
           "coarse-fine plan has an invalid first rule");
    const auto cell_plan = amr::compile_coarse_fine_cell_plan(plan, 2);
    expect(cell_plan.dimension == 1 && cell_plan.species_count == 2
               && cell_plan.scope == plan.scope
               && cell_plan.logical_fingerprint == plan.fingerprint
               && cell_plan.transfers.size() == 2 * amr::MAX_NG,
           "coarse-fine CPU-only cell lowering metadata drifted");
    int injection_cells = 0;
    int average_cells = 0;
    std::set<std::pair<amr::AmrEndpoint, amr::LogicalAmrCell>> destinations;
    for (const auto& transfer : cell_plan.transfers) {
        expect(destinations.emplace(
                   transfer.destination, transfer.destination_cell).second,
               "coarse-fine cell lowering produced overlapping writes");
        if (transfer.rule
            == amr::RefinementRule::CoarseGhostInjection) {
            expect(transfer.source_count == 1,
                   "coarse injection did not lower to one source");
            ++injection_cells;
        } else {
            expect(transfer.rule == amr::RefinementRule::FineGhostAverage
                       && transfer.source_count == 2,
                   "1D fine average did not lower to two sources");
            ++average_cells;
        }
    }
    expect(injection_cells == amr::MAX_NG
               && average_cells == amr::MAX_NG,
           "coarse-fine cell lowering route coverage drifted");

    int fine_id = -1;
    int coarse_id = -1;
    for (const int block_id : active) {
        const auto& block = pool->GetBlock(block_id);
        if (block.level == 1 && block.logical_x1 == 1) fine_id = block_id;
        if (block.level == 0 && block.logical_x1 == 1) coarse_id = block_id;
    }
    expect(fine_id >= 0 && coarse_id >= 0,
           "mixed AMR fixture endpoints are missing");
    amr::Block& fine_before = pool->GetBlock(fine_id);
    amr::Block& coarse_before = pool->GetBlock(coarse_id);
    const int coarse_source = coarse_before.grid.GetIndex(
        coarse_before.grid.Is(), coarse_before.grid.Js(),
        coarse_before.grid.Ks());
    const int fine_upper_ghost = fine_before.grid.GetIndex(
        fine_before.grid.Ie() + 1, fine_before.grid.Js(),
        fine_before.grid.Ks());
    const int fine_average_a = fine_before.grid.GetIndex(
        fine_before.grid.Is() + amr::BLOCK_NX - 2 * amr::MAX_NG,
        fine_before.grid.Js(), fine_before.grid.Ks());
    const int fine_average_b = fine_average_a + 1;
    const int coarse_lower_ghost = coarse_before.grid.GetIndex(
        coarse_before.grid.Is() - amr::MAX_NG, coarse_before.grid.Js(),
        coarse_before.grid.Ks());

    // A direct arithmetic average would produce X0=0.5 here.  Restriction
    // must instead conserve rho*X and recover X0=(1*1+3*0)/(1+3)=0.25.
    fine_before.fluid_state.rho[fine_average_a] = 1.0;
    fine_before.fluid_state.rho[fine_average_b] = 3.0;
    fine_before.fluid_state.mom_u[fine_average_a] = 2.0;
    fine_before.fluid_state.mom_u[fine_average_b] = 6.0;
    fine_before.fluid_state.mom_v[fine_average_a] = 4.0;
    fine_before.fluid_state.mom_v[fine_average_b] = 8.0;
    fine_before.fluid_state.mom_w[fine_average_a] = 6.0;
    fine_before.fluid_state.mom_w[fine_average_b] = 10.0;
    fine_before.fluid_state.eng[fine_average_a] = 8.0;
    fine_before.fluid_state.eng[fine_average_b] = 12.0;
    fine_before.fluid_state.enuc_rate[fine_average_a] = 10.0;
    fine_before.fluid_state.enuc_rate[fine_average_b] = 14.0;
    fine_before.fluid_state.X(0, fine_average_a) = 1.0;
    fine_before.fluid_state.X(0, fine_average_b) = 0.0;
    fine_before.fluid_state.X(1, fine_average_a) = 0.0;
    fine_before.fluid_state.X(1, fine_average_b) = 1.0;
    const auto fine_index = static_cast<std::size_t>(
        std::find(active.begin(), active.end(), fine_id) - active.begin());
    const amr::LogicalAmrCell fine_ghost_logical{
        amr::BLOCK_NX + 1, 0, 0};
    const auto injection = std::find_if(
        cell_plan.transfers.begin(), cell_plan.transfers.end(),
        [&](const amr::CoarseFineCellTransfer& transfer) {
            return transfer.rule
                    == amr::RefinementRule::CoarseGhostInjection
                && transfer.destination.handle == handles[fine_index]
                && transfer.destination_cell == fine_ghost_logical;
        });
    expect(injection != cell_plan.transfers.end()
               && injection->source.handle
                   != injection->destination.handle,
           "fine ghost has no lowered prolongation transfer");
    const auto prolonged = [&](const std::vector<double>& field) {
        const auto source_index = [&](const amr::LogicalAmrCell& logical) {
            return coarse_before.grid.GetIndex(
                coarse_before.grid.Is() + logical[0],
                coarse_before.grid.Js() + logical[1],
                coarse_before.grid.Ks() + logical[2]);
        };
        double lower[3]{};
        double upper[3]{};
        for (int axis = 0; axis < 1; ++axis) {
            lower[axis] = field[source_index(
                injection->slope_cells[2 * axis])];
            upper[axis] = field[source_index(
                injection->slope_cells[2 * axis + 1])];
        }
        return amr::prolongation_math::limited_linear_value(
            field[source_index(injection->source_cells[0])],
            lower, upper, injection->fine_position.data(), 1);
    };
    const double expected_fine_ghost = prolonged(
        coarse_before.fluid_state.rho);
    const double expected_fine_enuc = prolonged(
        coarse_before.fluid_state.enuc_rate);
    std::vector<double> coarse_rho_x0(
        coarse_before.fluid_state.rho.size());
    for (std::size_t index = 0; index < coarse_rho_x0.size(); ++index)
        coarse_rho_x0[index] = coarse_before.fluid_state.rho[index]
            * coarse_before.fluid_state.X(0, static_cast<int>(index));
    const double expected_fine_x0 = prolonged(coarse_rho_x0)
        / expected_fine_ghost;
    const double expected_coarse_ghost = 0.5
        * (fine_before.fluid_state.rho[fine_average_a]
           + fine_before.fluid_state.rho[fine_average_b]);
    const double expected_enuc = 0.5
        * (fine_before.fluid_state.enuc_rate[fine_average_a]
           + fine_before.fluid_state.enuc_rate[fine_average_b]);
    const auto shared_restriction = [&](const FluidState& state, int species) {
        const double density_integral =
            amr::restriction_math::weighted_conserved_value(
                state.rho[fine_average_a], 1.0)
            + amr::restriction_math::weighted_conserved_value(
                state.rho[fine_average_b], 1.0);
        const double species_density_integral =
            amr::restriction_math::weighted_species_density(
                state.rho[fine_average_a],
                state.X(species, fine_average_a), 1.0)
            + amr::restriction_math::weighted_species_density(
                state.rho[fine_average_b],
                state.X(species, fine_average_b), 1.0);
        return amr::restriction_math::restricted_mass_fraction(
            species_density_integral, density_integral);
    };
    const double expected_ghost_x0 =
        shared_restriction(fine_before.fluid_state, 0);
    const double expected_ghost_x1 =
        shared_restriction(fine_before.fluid_state, 1);
    const double expected_next_fine_ghost = prolonged(
        coarse_before.state_next.rho);
    const double expected_next_coarse_ghost = 0.5
        * (fine_before.state_next.rho[fine_average_a]
           + fine_before.state_next.rho[fine_average_b]);
    const double expected_next_x0 =
        shared_restriction(fine_before.state_next, 0);
    const double expected_next_x1 =
        shared_restriction(fine_before.state_next, 1);
    const double expected_scratch_fine_ghost = prolonged(
        coarse_before.state_scratch.rho);
    const double expected_scratch_coarse_ghost = 0.5
        * (fine_before.state_scratch.rho[fine_average_a]
           + fine_before.state_scratch.rho[fine_average_b]);
    const double expected_scratch_x0 =
        shared_restriction(fine_before.state_scratch, 0);
    const double expected_scratch_x1 =
        shared_restriction(fine_before.state_scratch, 1);

    exchange.ExecuteExchange(
        pool, tree, 1, &amr::Block::fluid_state, handles);
    exchange.ExecuteCoarseFinePlan(
        plan, pool, tree, 1, &amr::Block::state_next, handles);
    exchange.ExecuteCoarseFinePlan(
        plan, pool, tree, 1, &amr::Block::state_scratch, handles);
    const amr::Block& fine_after = pool->GetBlock(fine_id);
    const amr::Block& coarse_after = pool->GetBlock(coarse_id);
    expect(fine_after.fluid_state.rho[fine_upper_ghost]
               == expected_fine_ghost,
           "coarse-to-fine ghost route drifted");
    expect(fine_after.fluid_state.enuc_rate[fine_upper_ghost]
                   == expected_fine_enuc
               && fine_after.fluid_state.X(0, fine_upper_ghost)
                   == expected_fine_x0
               && fine_after.fluid_state.X(0, fine_upper_ghost)
                       + fine_after.fluid_state.X(1, fine_upper_ghost)
                   == 1.0,
           "coarse-to-fine shared limited-linear reconstruction drifted");
    expect(coarse_after.fluid_state.rho[coarse_lower_ghost]
               == expected_coarse_ghost,
           "fine-to-coarse ghost average drifted");
    expect(coarse_after.fluid_state.enuc_rate[coarse_lower_ghost]
               == expected_enuc,
           "fine-to-coarse ENUC ghost average drifted");
    expect(coarse_after.fluid_state.mom_u[coarse_lower_ghost] == 4.0
               && coarse_after.fluid_state.mom_v[coarse_lower_ghost] == 6.0
               && coarse_after.fluid_state.mom_w[coarse_lower_ghost] == 8.0
               && coarse_after.fluid_state.eng[coarse_lower_ghost] == 10.0,
           "fine-to-coarse conserved ghost restriction drifted");
    expect(coarse_after.fluid_state.X(0, coarse_lower_ghost)
                   == expected_ghost_x0
               && coarse_after.fluid_state.X(1, coarse_lower_ghost)
                   == expected_ghost_x1
               && expected_ghost_x0 == 0.25
               && expected_ghost_x1 == 0.75,
           "Host lowering no longer matches shared CUDA rho-X math");
    expect(fine_after.state_next.rho[fine_upper_ghost]
               == expected_next_fine_ghost
               && coarse_after.state_next.rho[coarse_lower_ghost]
                   == expected_next_coarse_ghost
               && coarse_after.state_next.X(0, coarse_lower_ghost)
                   == expected_next_x0
               && coarse_after.state_next.X(1, coarse_lower_ghost)
                   == expected_next_x1,
           "Next coarse-fine state-slot execution drifted");
    expect(fine_after.state_scratch.rho[fine_upper_ghost]
               == expected_scratch_fine_ghost
               && coarse_after.state_scratch.rho[coarse_lower_ghost]
                   == expected_scratch_coarse_ghost
               && coarse_after.state_scratch.X(0, coarse_lower_ghost)
                   == expected_scratch_x0
               && coarse_after.state_scratch.X(1, coarse_lower_ghost)
                   == expected_scratch_x1,
           "Scratch coarse-fine state-slot execution drifted");

    std::map<amr::AmrEndpoint, int> lowering;
    amr::AmrEndpoint fine_endpoint{};
    amr::AmrEndpoint coarse_endpoint{};
    for (std::size_t index = 0; index < active.size(); ++index) {
        const auto& active_block = pool->GetBlock(active[index]);
        const amr::AmrEndpoint value{
            {1, active_block.level, active_block.logical_x1, 0, 0},
            handles[index]};
        lowering.emplace(value, active[index]);
        if (active[index] == fine_id) fine_endpoint = value;
        if (active[index] == coarse_id) coarse_endpoint = value;
    }

    amr::FluxRegister invalid_register;
    invalid_register.EnsureSpecies(2);
    invalid_register.Resize(16, 1);
    amr::FluxRegistrationPlan registration{};
    registration.dimension = 1;
    registration.scope = {0, {31}, {31}};
    const std::array<double, 7> field_values{
        2.0, 3.0, 4.0, 5.0, 6.0, 0.4, 0.6};
    for (int field = 0; field < 7; ++field) {
        registration.operations.push_back({
            0, fine_endpoint, coarse_endpoint,
            {{amr::BLOCK_NX - 1, 0, 0}, {1, 1, 1}},
            {{0, 0, 0}, {1, 1, 1}},
            amr::AmrAxis::X, amr::AmrSide::Lower,
            field < 5 ? static_cast<amr::AmrField>(field)
                      : amr::AmrField::Species,
            field < 5 ? -1 : field - 5,
            amr::RefinementRule::FineFluxContribution, 0.5, 1.0});
    }
    amr::finalize_amr_plan(registration);

    std::vector<double> nonfinite_values(
        field_values.begin(), field_values.end());
    nonfinite_values[0] = std::numeric_limits<double>::quiet_NaN();
    expect_rejected(
        [&] { invalid_register.ApplyRegistrationPlan(
            registration, nonfinite_values, lowering); },
        "nonfinite flux contribution was accepted");
    expect(!invalid_register.HasData(coarse_id, 0),
           "rejected flux registration performed a partial write");

    const int fine_total = fine_after.grid.GetTotalSize();
    std::vector<FluidVector> hydro_flux(
        static_cast<std::size_t>(fine_total),
        {field_values[0], field_values[1], field_values[2],
         field_values[3], field_values[4]});
    std::vector<double> species_flux(
        static_cast<std::size_t>(2 * fine_total));
    std::fill_n(species_flux.begin(), fine_total, field_values[5]);
    std::fill_n(species_flux.begin() + fine_total,
                fine_total, field_values[6]);
    amr::RegisterCoarseFineFluxes(
        control, fine_id, fine_after.grid, 0, hydro_flux,
        species_flux, 2, 0.5);
    expect(control.flux_register.HasData(coarse_id, 0),
           "production flux registration did not activate its coarse face");
    expect(control.flux_register.GetSummedFlux(coarse_id, 0, 0).rho == 1.0
               && control.flux_register.GetSummedFlux(coarse_id, 0, 0).eng == 3.0
               && control.flux_register.GetSummedSpeciesFlux(coarse_id, 0, 0, 0)
                   == 0.2
               && control.flux_register.GetSummedSpeciesFlux(coarse_id, 0, 0, 1)
                   == 0.3,
           "logical flux registration arithmetic drifted");

    const double reflux_dt = 0.25;
    const auto reflux = control.flux_register.BuildRefluxPlan(
        pool, active, handles, 1, reflux_dt);
    expect(reflux.operations.size() == 7,
           "reflux plan did not cover five conserved and two species fields");
    const int coarse_cell = coarse_after.grid.GetIndex(
        coarse_after.grid.Is(), coarse_after.grid.Js(),
        coarse_after.grid.Ks());
    const double rho_before = coarse_after.fluid_state.rho[coarse_cell];
    const double x0_before = coarse_after.fluid_state.X(0, coarse_cell);
    const double area = GridMetrics::FaceArea(
        coarse_after.grid, 0, coarse_after.grid.Is(),
        coarse_after.grid.Js(), coarse_after.grid.Ks(), false);
    const double volume = GridMetrics::CellVolume(
        coarse_after.grid, coarse_after.grid.Is(),
        coarse_after.grid.Js(), coarse_after.grid.Ks());
    const double correction = reflux_dt * area / volume;
    const double expected_rho = rho_before + correction;
    const double expected_x0 =
        (rho_before * x0_before + correction * 0.2) / expected_rho;
    control.flux_register.ExecuteRefluxPlan(
        reflux, pool, active, handles, &amr::Block::fluid_state);
    const auto& refluxed = pool->GetBlock(coarse_id).fluid_state;
    expect(refluxed.rho[coarse_cell] == expected_rho
               && refluxed.X(0, coarse_cell) == expected_x0,
           "logical reflux execution arithmetic drifted: rho="
               + std::to_string(refluxed.rho[coarse_cell])
               + " expected_rho=" + std::to_string(expected_rho)
               + " x0=" + std::to_string(refluxed.X(0, coarse_cell))
               + " expected_x0=" + std::to_string(expected_x0));
}

} // namespace

int main()
{
    try {
        test_public_contract();
        test_validation();
        test_conservative_restriction_math();
        test_limited_linear_prolongation_math();
        test_amr_interface_stencil_predicate();
        test_multidimensional_cell_lowering();
        test_curvilinear_host_restriction();
        test_mixed_level_and_coarse_fine_execution();
        const auto ordinary = ordinary_plan();
        const auto migration = migration_plan();
        std::cout << "AMR_PLAN_CONTRACT_PASS ordinary="
                  << std::hex << ordinary.fingerprint
                  << " migration=" << migration.fingerprint << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
