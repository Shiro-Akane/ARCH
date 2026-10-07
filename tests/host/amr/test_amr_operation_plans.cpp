/**
 * @file test_amr_operation_plans.cpp
 * @brief Verify conservative AMR transfer plans and their host execution.
 *
 * The cases cover restriction, limited prolongation, species closure and
 * coarse-fine stencils across dimensions and curved-grid measures.
 */
#include "amr/transfer/AmrTransferPlans.h"
#include "amr/exchange/CoarseFineCellPlan.h"
#include "amr/transfer/ConservativeRestriction.h"
#include "amr/flux/AMRFluxRegistering.h"
#include "amr/flux/FluxRegister.h"
#include "amr/exchange/GhostExchange.h"
#include "amr/transfer/LimitedLinearProlongation.h"
#include "amr/transfer/NativeRzRegridTransfer.h"
#include "numerics/reconstruction/AMRInterfaceStencil.h"
#include "numerics/reconstruction/Reconstruction.h"
#include "fixtures/amr/amr_composition_test_cases.h"
#include "physics/eos/IdealGas.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <iomanip>
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

void test_muscl_face_composition_closure()
{
    FluidState state;
    state.Preallocate(4);
    state.InitSpecies(4);
    for (int cell=0; cell<4; ++cell)
        for (int species=0; species<4; ++species)
            state.X(species,cell)=amr::test::muscl_composition_cells[cell][species];
    double faces[8]{};
    MusclReconstruction<McLimiter>::run_species(state,1,4,faces,faces+4);
    for (int i=0; i<8; ++i) {
        const double expected=amr::test::muscl_composition_faces[i];
        expect(std::abs(faces[i]-expected)<=8*std::numeric_limits<double>::epsilon()*expected,
               "MUSCL face differs from independent normalized composition");
    }
    for (int side=0; side<2; ++side) {
        double sum=0;
        for (int i=0; i<4; ++i) sum+=faces[4*side+i];
        expect(std::abs(sum-1.0)<=4*std::numeric_limits<double>::epsilon(),
               "MUSCL species flux would not sum to the mass flux");
    }
    MusclReconstruction<McLimiter>::normalize_species_faces(0,nullptr,nullptr);
}

void test_shared_composition_prolongation()
{
    using namespace amr::prolongation_math;
    for (int dimension = 1; dimension <= 3; ++dimension) {
        for (const auto& example : amr::test::composition_cases()) {
            const auto stencil = example.stencil(dimension);
            const auto family = classify_composition_family(stencil);
            expect(family == example.family[dimension-1], "composition family decision drifted");
            std::array<double, amr::test::CompositionCase::species> integrals{};
            for (int child = 0; child < (1 << dimension); ++child) {
                double position[3]{};
                for (int axis = 0; axis < dimension; ++axis)
                    position[axis] = (child & (1 << axis)) != 0 ? 0.25 : -0.25;
                const double density = reconstruct_field(
                    stencil, stencil.density, position);
                double sum = 0.0;
                for (int species = 0; species < stencil.species_count; ++species) {
                    const double fraction = reconstruct_mass_fraction(
                        stencil, family, density, species, position);
                    expect(std::isfinite(fraction) && fraction >= 0.0
                               && fraction <= 1.0,
                           "prolongation produced a negative/invalid species");
                    if (example.preserve_last_trace && species+1==stencil.species_count) {
                        const double trace=example.fractions[species*amr::test::CompositionCase::cells];
                        expect(std::abs(fraction-trace) <= 16.0*std::numeric_limits<double>::epsilon()*trace,
                               "closure invented or erased a zero/1e-20 trace species");
                    }
                    if (family == CompositionFamily::Constant)
                        expect(fraction == example.fractions[
                                   species * amr::test::CompositionCase::cells],
                               "fallback did not preserve parent composition");
                    sum += fraction;
                    integrals[species] += density * fraction;
                }
                expect(std::abs(sum - 1.0) <= 4.0e-16,
                       "prolongation failed composition closure");
            }
            for (int species = 0; species < stencil.species_count; ++species) {
                const double average = integrals[species] / (1 << dimension);
                const double parent = example.density[0] * example.fractions[
                    species * amr::test::CompositionCase::cells];
                expect(std::abs(average - parent) <= 4.0e-16,
                       "linear/fallback siblings did not conserve parent rhoX");
            }
        }
    }
    auto invalid = amr::test::composition_cases()[0];
    expect(composition_closure_species(invalid.stencil(1))==1,
           "closure did not select the dominant parent species");
    expect(composition_closure_species(amr::test::composition_cases()[2].stencil(1))==0,
           "equal parent fractions lost deterministic first-index tie break");
    for (const double density : {0.0, -1.0,
                                std::numeric_limits<double>::quiet_NaN(),
                                std::numeric_limits<double>::infinity()}) {
        invalid.density.fill(density);
        expect(classify_composition_family(invalid.stencil(3))
                   == CompositionFamily::InvalidDensity,
               "invalid density was silently treated as composition fallback");
        auto no_species = invalid.stencil(1);
        no_species.species_count = 0;
        no_species.mass_fractions = nullptr;
        expect(classify_composition_family(no_species)
                   == CompositionFamily::InvalidDensity,
               "zero-species prolongation accepted invalid density");
    }
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

void test_host_exchange_cache_rebinding()
{
    SimConfig config{};
    config.grid.dim = 1;
    config.grid.nblockx1 = 2;
    config.grid.nblockx2 = config.grid.nblockx3 = 0;
    config.grid.amr_max_blocks = 8;
    config.amr.lrefinemin = config.amr.lrefinemax = 0;
    amr::AMRControl control(8, 1);
    auto pool = control.pool;
    auto tree = control.tree;
    tree->LoadLeafGrid(config, 2, {0, 0}, {0, 1}, {0, 0}, {0, 0});
    const auto& active = tree->GetActiveBlocks();
    std::vector<amr::BlockHandle> handles{{{41}, {9}}, {{42}, {9}}};
    for (const auto id : active) fill_block(pool->GetBlock(id));
    auto& exchange = control.ghost_exchange;
    auto& left = pool->GetBlock(active[0]);
    auto& right = pool->GetBlock(active[1]);
    const int destination = left.grid.GetIndex(left.grid.Ie()); // exclusive active end
    const int source = right.grid.GetIndex(right.grid.Is());
    for (const auto slot : {&amr::Block::fluid_state, &amr::Block::state_next,
                            &amr::Block::state_scratch}) {
        const auto expected = (right.*slot).rho[source];
        exchange.ExecuteExchange(pool, tree, 1, slot, handles);
        expect((left.*slot).rho[destination] == expected,
            "cached Host plan used a previous slot view");
    }
    expect(exchange.PlanCacheBuilds() == 1 && exchange.HostPlanCacheBuilds() == 1,
        "slot changes recompiled pointer-free Host plans");
    FluidState replacement = right.state_next;
    replacement.rho[source] = 7654321.0;
    right.state_next = std::move(replacement);
    exchange.ExecuteExchange(pool, tree, 1, &amr::Block::state_next, handles);
    expect(left.state_next.rho[destination] == 7654321.0
        && exchange.HostPlanCacheBuilds() == 1,
        "storage replacement used a dangling Host pointer");
    const auto stride = left.grid.stride_y;
    left.grid.stride_y = 0;
    expect_rejected([&] { exchange.ExecuteExchange(
        pool, tree, 1, &amr::Block::state_next, handles); },
        "cache hit bypassed current layout validation");
    left.grid.stride_y = stride;
    exchange.ExecuteExchange(pool, tree, 1, &amr::Block::state_next, handles);
    expect(exchange.HostPlanCacheBuilds() == 1, "invalid layout replaced Host cache");
    for (const auto id : active) {
        auto& block = pool->GetBlock(id);
        block.fluid_state.InitSpecies(1);
        block.state_next.InitSpecies(1);
        block.state_scratch.InitSpecies(1);
    }
    exchange.ExecuteExchange(pool, tree, 1, &amr::Block::state_next, handles);
    expect(exchange.PlanCacheBuilds() == 2 && exchange.HostPlanCacheBuilds() == 2,
        "species change reused old Host lowering");
    for (auto& handle : handles) ++handle.epoch.value;
    exchange.ExecuteExchange(pool, tree, 1, &amr::Block::state_next, handles);
    expect(exchange.PlanCacheBuilds() == 3 && exchange.HostPlanCacheBuilds() == 3,
        "topology epoch change reused old Host lowering");
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
    const auto& cached = exchange.GetPlans(pool, tree, 1, handles);
    expect(cached.same_level.size() == 2
        && cached.coarse_fine.fingerprint
            == exchange.BuildCoarseFinePlan(pool, tree, 1, handles).fingerprint,
        "cached plans differ from fresh mixed-level plans");
    const auto fresh = exchange.BuildSameLevelPlans(pool, tree, 1, handles);
    for (std::size_t group = 0; group < fresh.size(); ++group) {
        expect(cached.same_level[group].fingerprint == fresh[group].fingerprint,
            "cached same-level fingerprint differs");
        for (std::size_t local = 0; local < fresh[group].blocks.size(); ++local)
            expect(handles[cached.level_indices[group][local]]
                    == fresh[group].blocks[local].handle,
                "cached endpoint index is stale");
    }
    (void)exchange.GetPlans(pool, tree, 1, handles);
    expect(exchange.PlanCacheBuilds() == 1 && exchange.PlanCacheHits() == 1,
        "unchanged topology rebuilt exchange plans");
    // Field and slot contents are not part of a logical plan cache.
    pool->GetBlock(active.front()).state_next = pool->GetBlock(active.front()).fluid_state;
    (void)exchange.GetPlans(pool, tree, 1, handles);
    expect(exchange.PlanCacheBuilds() == 1, "field binding invalidated logical cache");
    auto next_handles = handles;
    for (auto& handle : next_handles) ++handle.epoch.value;
    (void)exchange.GetPlans(pool, tree, 1, next_handles);
    expect(exchange.PlanCacheBuilds() == 2, "epoch change did not invalidate cache");
    (void)exchange.GetPlans(pool, tree, 1, handles);
    const auto before_failure = exchange.PlanCacheBuilds();
    auto& neighbor = pool->GetBlock(active.front()).face_neighbors[0];
    const auto saved_neighbor = neighbor;
    neighbor.count = 5;
    expect_rejected([&] { (void)exchange.GetPlans(pool, tree, 1, handles); },
        "cached plan hid an invalid new topology");
    neighbor = saved_neighbor;
    (void)exchange.GetPlans(pool, tree, 1, handles);
    expect(exchange.PlanCacheBuilds() == before_failure,
        "failed candidate destroyed the previous cache entry");
    auto invalid_cached_handles = handles;
    invalid_cached_handles.back() = invalid_cached_handles.front();
    expect_rejected([&] { (void)exchange.GetPlans(pool, tree, 1, invalid_cached_handles); },
        "cached plan accepted duplicate handles");
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

    // Invalid coarse density must reject the complete gather before either
    // direction scatters, matching the CUDA status/abort transaction.
    const auto original_density = coarse_before.fluid_state.rho;
    std::fill(coarse_before.fluid_state.rho.begin(),
              coarse_before.fluid_state.rho.end(), 0.0);
    std::vector<FluidState> rejection_snapshot;
    for (const int block_id : active)
        rejection_snapshot.push_back(pool->GetBlock(block_id).fluid_state);
    bool rejected_density = false;
    try {
        exchange.ExecuteCoarseFinePlan(
            plan, pool, tree, 1, &amr::Block::fluid_state, handles);
    } catch (const std::runtime_error& error) {
        rejected_density = std::string(error.what())
            == amr::prolongation_math::invalid_prolongation_density_message();
    }
    expect(rejected_density, "Host prolongation silently accepted invalid density");
    for (std::size_t block = 0; block < active.size(); ++block) {
        const auto& before = rejection_snapshot[block];
        const auto& after = pool->GetBlock(active[block]).fluid_state;
        expect(before.rho == after.rho && before.mom_u == after.mom_u
                   && before.mom_v == after.mom_v && before.mom_w == after.mom_w
                   && before.eng == after.eng && before.enuc_rate == after.enuc_rate
                   && before.mass_fractions == after.mass_fractions,
               "rejected Host AMR plan partially scattered");
    }
    coarse_before.fluid_state.rho = original_density;

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


/** Independently project a constant Cartesian momentum into a native basis. */
std::array<double, 3> projected_momentum(
    const Grid& grid, double theta, double phi)
{
    const double c = std::cos(phi), s = std::sin(phi);
    if (grid.dim == 2 || grid.geometry == "cylindrical")
        return {c + 2. * s,
                grid.dim == 3 ? 3. : -s + 2. * c,
                grid.dim == 3 ? -s + 2. * c : 0.};
    return {
        std::sin(theta) * (c + 2. * s) + 3. * std::cos(theta),
        std::cos(theta) * (c + 2. * s) - 3. * std::sin(theta),
        -s + 2. * c};
}

/** Check true vector-basis continuation across each supported singular face. */
void test_coordinate_seam_case(int dimension, bool spherical, bool mixed)
{
    SimConfig config{};
    config.grid.dim = dimension;
    config.grid.geometry = spherical ? "spherical" : "cylindrical";
    config.grid.nblockx1 = 1;
    config.grid.nblockx2 = spherical && dimension == 3 ? 2 :
                            dimension == 2 ? 2 : 1;
    config.grid.nblockx3 = dimension == 3 ? 2 : 0;
    config.grid.x1_min = 0.;
    config.grid.x1_max = 1.;
    config.grid.x2_min = 0.;
    config.grid.x2_max = spherical && dimension == 3
        ? std::acos(-1.) : dimension == 2 ? 2. * std::acos(-1.) : 1.;
    config.grid.x3_min = 0.;
    config.grid.x3_max = dimension == 3 ? 2. * std::acos(-1.) : 0.;
    config.grid.x1l_boundary_type = "reflecting";
    if (dimension == 2) {
        config.grid.x2l_boundary_type = "periodic";
        config.grid.x2r_boundary_type = "periodic";
    } else {
        config.grid.x3l_boundary_type = "periodic";
        config.grid.x3r_boundary_type = "periodic";
        if (spherical) {
            config.grid.x2l_boundary_type = "reflecting";
            config.grid.x2r_boundary_type = "reflecting";
        }
    }
    config.grid.amr_max_blocks = 24;
    config.amr.lrefinemin = 0;
    config.amr.lrefinemax = mixed ? 1 : 0;
    amr::AMRControl control(24, dimension);
    auto pool = control.pool;
    auto tree = control.tree;
    if (mixed && dimension == 2) {
        tree->LoadLeafGrid(config, 2,
            {1, 1, 1, 1, 0}, {0, 1, 0, 1, 0},
            {0, 0, 1, 1, 1}, {0, 0, 0, 0, 0});
    } else if (mixed) {
        // Refine the lower-azimuth root. Spherical geometry retains the
        // second coarse theta root so both pole seams cross AMR levels.
        std::vector<int> levels;
        std::vector<std::uint32_t> x1, x2, x3;
        for (int k = 0; k < 2; ++k)
            for (int j = 0; j < 2; ++j)
                for (int i = 0; i < 2; ++i) {
                    levels.push_back(1);
                    x1.push_back(i);
                    x2.push_back(j);
                    x3.push_back(k);
                }
        levels.push_back(0);
        x1.push_back(0);
        x2.push_back(0);
        x3.push_back(1);
        if (spherical) {
            levels.push_back(0);
            x1.push_back(0);
            x2.push_back(1);
            x3.push_back(0);
            levels.push_back(0);
            x1.push_back(0);
            x2.push_back(1);
            x3.push_back(1);
        }
        tree->LoadLeafGrid(config, 2, levels, x1, x2, x3);
    } else {
        tree->InitRootGrid(config, 2);
    }
    const auto& active = tree->GetActiveBlocks();
    std::vector<amr::BlockHandle> handles;
    for (std::size_t block = 0; block < active.size(); ++block) {
        handles.push_back({{1000 + block}, {51}});
        auto& b = pool->GetBlock(active[block]);
        const Grid& grid = b.grid;
        for (int k = grid.Ks(); k < grid.Ke(); ++k)
            for (int j = grid.Js(); j < grid.Je(); ++j)
                for (int i = grid.Is(); i < grid.Ie(); ++i) {
                    const int cell = grid.GetIndex(i, j, k);
                    const double theta = dimension == 3 && spherical
                        ? grid.GetCellCenterY(j) : 0.;
                    const double phi = dimension == 2
                        ? grid.GetCellCenterY(j) : grid.GetCellCenterZ(k);
                    const auto momentum = projected_momentum(grid, theta, phi);
                    b.fluid_state.rho[cell] = 2.;
                    b.fluid_state.mom_u[cell] = momentum[0];
                    b.fluid_state.mom_v[cell] = momentum[1];
                    b.fluid_state.mom_w[cell] = momentum[2];
                    b.fluid_state.eng[cell] = 10.;
                    b.fluid_state.enuc_rate[cell] = 0.125;
                    b.fluid_state.X(0, cell) = 0.6;
                    b.fluid_state.X(1, cell) = 0.4;
                }
    }
    const auto& plan = control.ghost_exchange.GetPlans(
        pool, tree, dimension, handles).coordinate_seam;
    expect(!plan.transfers.empty(), "singular geometry generated no seam ghosts");
    control.ghost_exchange.ExecuteExchange(
        pool, tree, dimension, &amr::Block::fluid_state, handles);
    double largest_momentum_error = 0.;
    bool crossed_block = false;
    bool crossed_level = false;
    bool saw_origin = false, saw_north = false, saw_south = false;
    for (const auto& transfer : plan.transfers) {
        expect(transfer.geometry_semantics==GridMetrics::GeometrySemantics::Existing,
            "existing coordinate seam lost its original chart semantics");
        const auto& destination = pool->GetBlock(transfer.destination_id);
        const auto& source = pool->GetBlock(transfer.source_id);
        const Grid& grid = destination.grid;
        const int cell = transfer.destination_cell;
        const int k = cell / grid.stride_z;
        const int j = (cell - k * grid.stride_z) / grid.stride_y;
        const int i = cell - k * grid.stride_z - j * grid.stride_y;
        saw_origin |= i < grid.Is();
        if (spherical && dimension == 3) {
            saw_north |= j < grid.Js();
            saw_south |= j >= grid.Je();
        }
        const double theta = spherical && dimension == 3
            ? grid.GetCellCenterY(j) : 0.;
        const double phi = dimension == 2
            ? grid.GetCellCenterY(j) : grid.GetCellCenterZ(k);
        const auto expected = projected_momentum(grid, theta, phi);
        const std::array<double, 3> actual{
            destination.fluid_state.mom_u[cell],
            destination.fluid_state.mom_v[cell],
            destination.fluid_state.mom_w[cell]};
        for (int axis = 0; axis < dimension; ++axis)
            largest_momentum_error = std::max(largest_momentum_error,
                std::abs(actual[axis] - expected[axis]));
        expect(destination.fluid_state.rho[cell] == 2.
                && destination.fluid_state.eng[cell] == 10.
                && destination.fluid_state.enuc_rate[cell] == 0.125
                && destination.fluid_state.X(0, cell) == 0.6
                && destination.fluid_state.X(1, cell) == 0.4,
            "singular seam did not preserve scalar and species state");
        crossed_block |= transfer.source_id != transfer.destination_id;
        crossed_level |= source.level != destination.level;
    }
    expect(crossed_block, "singular seam never crossed an AMR block");
    expect(saw_origin, "singular seam never filled the radial origin");
    if (spherical && dimension == 3)
        expect(saw_north && saw_south,
            "spherical seam did not fill both polar faces");
    if (mixed) expect(crossed_level,
        "mixed AMR seam did not exercise a coarse/fine donor");
    const double budget = mixed ? 0.035 : 2e-12;
    expect(largest_momentum_error < budget,
        "singular seam Cartesian-vector projection exceeded error budget: "
        + std::to_string(largest_momentum_error));
    if (dimension == 2 && mixed) {
        // An AMR one-sided slope can cross a sharp density jump. The mapped
        // ghost must fall back to the valid donor center, including its basis
        // transform, instead of publishing a negative reconstructed density.
        auto abrupt = plan.transfers.front();
        expect(abrupt.source_neighbor[0] != abrupt.source_center,
            "seam fallback fixture lacks a radial donor neighbor");
        abrupt.neighbor_weight = {-0.5, 0., 0.};
        auto& donor = pool->GetBlock(abrupt.source_id).fluid_state;
        donor.rho[abrupt.source_neighbor[0]] = 100.;
        donor.eng[abrupt.source_neighbor[0]] = 1000.;
        amr::CoordinateSeamPlan fallback;
        fallback.transfers.push_back(abrupt);
        amr::execute_coordinate_seam_plan(fallback, pool,
            &amr::Block::fluid_state);
        const auto& result = pool->GetBlock(abrupt.destination_id).fluid_state;
        expect(result.rho[abrupt.destination_cell]
                == donor.rho[abrupt.source_center]
                && result.eng[abrupt.destination_cell]
                    == donor.eng[abrupt.source_center]
                && result.mom_u[abrupt.destination_cell]
                    == abrupt.momentum_sign[0]
                        * donor.mom_u[abrupt.source_center]
                && result.X(0, abrupt.destination_cell)
                    == donor.X(0, abrupt.source_center)
                && result.X(1, abrupt.destination_cell)
                    == donor.X(1, abrupt.source_center),
            "singular seam did not use admissible donor fallback");
        donor.rho[abrupt.source_neighbor[0]] = 2.;
        donor.eng[abrupt.source_neighbor[0]] = 10.;
        donor.X(0, abrupt.source_center) = 0.1;
        donor.X(1, abrupt.source_center) = 0.9;
        donor.X(0, abrupt.source_neighbor[0]) = 1.;
        donor.X(1, abrupt.source_neighbor[0]) = 0.;
        amr::execute_coordinate_seam_plan(fallback, pool,
            &amr::Block::fluid_state);
        expect(result.X(0, abrupt.destination_cell) == 0.1
                && result.X(1, abrupt.destination_cell) == 0.9,
            "singular seam published an invalid reconstructed composition");
    }
}

void test_rz_coarse_fine_ghost_angular() {
    const auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    const auto chart=amr::CoordinateSeamGeometry::RzAxisymmetric;
    const arch::state::Bounds bounds{1.e-14,1.e-14,1.e6};
    std::size_t injections=0,averages=0;
    for(int direction:{0,1})for(double inner:{0.,1.}) {
        SimConfig config{};
        config.grid.dim=2;config.grid.geometry="cylindrical";
        config.grid.nblockx1=direction==0?2:1;
        config.grid.nblockx2=direction==1?2:1;config.grid.nblockx3=0;
        config.grid.x1_min=inner;config.grid.x1_max=inner+config.grid.nblockx1;
        config.grid.x2_min=0.;config.grid.x2_max=config.grid.nblockx2;
        config.amr.lrefinemin=0;config.amr.lrefinemax=1;
        amr::AMRControl control(16,2);
        control.tree->LoadLeafGrid(config,2,{1,1,1,1,0},
            {0,1,0,1,direction==0?1u:0u},{0,0,1,1,direction==1?1u:0u},
            {0,0,0,0,0},rz);
        const auto& active=control.tree->GetActiveBlocks();
        std::vector<amr::BlockHandle> handles;
        for(std::size_t b=0;b<active.size();++b)handles.push_back({{70000+b},{111}});
        const auto& plans=control.ghost_exchange.GetPlans(
            control.pool,control.tree,2,handles,chart);
        const auto cells=amr::compile_coarse_fine_cell_plan(plans.coarse_fine,2);
        expect(!cells.transfers.empty(),"RZ mixed ghost fixture has no coarse/fine transfer");
        const auto block_for=[&](amr::BlockHandle h)->amr::Block& {
            for(std::size_t b=0;b<handles.size();++b)
                if(handles[b]==h)return control.pool->GetBlock(active[b]);
            throw std::runtime_error("RZ ghost handle missing");
        };
        const auto cell_index=[](const Grid& g,const amr::LogicalAmrCell& c) {
            return g.GetIndex(g.Is()+c[0],g.Js()+c[1],g.Ks()+c[2]);
        };
        int slot_number=0;
        for(auto member:{&amr::Block::fluid_state,&amr::Block::state_next,&amr::Block::state_scratch}) {
            const double omega=++slot_number;
            for(int id:active) {
                auto& block=control.pool->GetBlock(id);const auto& g=block.grid;
                auto& state=block.*member;
                for(int j=0;j<g.GetTotalY();++j)for(int i=0;i<g.GetTotalX();++i) {
                    const int cell=g.GetIndex(i,j,0);
                    const double lo=g.GetFacePosL(i),hi=g.GetFacePosR(i);
                    const double radius=hi<=0.
                        ? -.75*(std::pow(-lo,4)-std::pow(-hi,4))/(std::pow(-lo,3)-std::pow(-hi,3))
                        : .75*(std::pow(hi,4)-std::pow(lo,4))/(std::pow(hi,3)-std::pow(lo,3));
                    // e0 is constant physical specific internal energy.  The native
                    // total energy is its V average plus the independently integrated
                    // rotational kinetic energy: <E>_V=e0+Omega^2 int(r^3)dr/(2 int(r)dr).
                    // The same signed antiderivatives make E even across the axis.
                    const long double l=lo,h=hi;
                    const long double energy=100.L+static_cast<long double>(omega)*omega
                        *(h*h*h*h-l*l*l*l)/(4.L*(h*h-l*l));
                    state.set(cell,{1.,0.,0.,omega*radius,static_cast<double>(energy)});
                    state.enuc_rate[cell]=.25*omega;
                    state.X(0,cell)=.6;state.X(1,cell)=.4;
                }
            }
            std::vector<FluidState> before;
            for(int id:active)before.push_back(control.pool->GetBlock(id).*member);
            control.ghost_exchange.ExecuteCoarseFinePlan(plans.coarse_fine,
                control.pool,control.tree,2,member,handles,chart,bounds);
            for(const auto& transfer:cells.transfers) {
                auto& block=block_for(transfer.destination.handle);
                const auto& g=block.grid;const auto& state=block.*member;
                const int i=g.Is()+transfer.destination_cell[0];
                const int destination=cell_index(g,transfer.destination_cell);
                const long double lo=g.GetFacePosL(i),hi=g.GetFacePosR(i);
                const long double expected=omega*.75L*(hi*hi*hi*hi-lo*lo*lo*lo)
                    /(hi*hi*hi-lo*lo*lo);
                expect(std::abs(state.mom_w[destination]-expected)<1.e-12*std::max(1.L,std::abs(expected)),
                    "RZ ghost did not preserve analytic rigid rotation W average");
                const long double expected_energy=100.L+static_cast<long double>(omega)*omega
                    *(hi*hi*hi*hi-lo*lo*lo*lo)/(4.L*(hi*hi-lo*lo));
                expect(std::abs(state.rho[destination]-1.)<1.e-12
                    && std::abs(state.eng[destination]-expected_energy)<1.e-12
                    && std::abs(state.X(0,destination)-.6)<1.e-12
                    && std::abs(state.X(1,destination)-.4)<1.e-12,
                    "RZ ghost changed independent V fields or composition");
                if(transfer.rule==amr::RefinementRule::CoarseGhostInjection)++injections;
                else ++averages;
            }
            for(std::size_t b=0;b<active.size();++b) {
                const auto& block=control.pool->GetBlock(active[b]);
                const auto& g=block.grid;const auto& state=block.*member;
                for(int j=g.Js();j<g.Je();++j)for(int i=g.Is();i<g.Ie();++i) {
                    const int cell=g.GetIndex(i,j,0);
                    expect(state.rho[cell]==before[b].rho[cell]
                        && state.mom_w[cell]==before[b].mom_w[cell]
                        && state.eng[cell]==before[b].eng[cell]
                        && state.X(0,cell)==before[b].X(0,cell),
                        "RZ ghost exchange changed active scientific state");
                }
            }
        }
        // Bounds are borrowed on every call, independent of the cached topology.
        std::vector<FluidState> snapshot;
        for(int id:active)snapshot.push_back(control.pool->GetBlock(id).fluid_state);
        bool rejected=false;
        try {
            control.ghost_exchange.ExecuteCoarseFinePlan(plans.coarse_fine,control.pool,
                control.tree,2,&amr::Block::fluid_state,handles,chart,{1.e-14,1.e-14,.1});
        } catch(const std::runtime_error&) {rejected=true;}
        expect(rejected,"RZ ghost ignored configured internal energy ceiling");
        const auto unchanged=[&] {
            for(std::size_t b=0;b<active.size();++b) {
                const auto& state=control.pool->GetBlock(active[b]).fluid_state;
                expect(state.rho==snapshot[b].rho && state.mom_u==snapshot[b].mom_u
                    && state.mom_v==snapshot[b].mom_v && state.mom_w==snapshot[b].mom_w
                    && state.eng==snapshot[b].eng && state.enuc_rate==snapshot[b].enuc_rate
                    && state.mass_fractions==snapshot[b].mass_fractions,
                    "RZ failed coarse/fine plan partially scattered");
            }
        };
        unchanged();
        // The former raw -9/128 parent veto is not a native thermal oracle:
        // its independent kappa=8/9 closure has e=85/8>0.  Actual thermal
        // rejection is owned by the post-ghost Runtime quartic-density test.
    }
    expect(injections>0 && averages>0,"RZ ghost did not cover both directions");
    std::cout<<"RZ_GHOST_W_PASS injections="<<injections<<" averages="<<averages<<'\n';
}

void test_rz_rigid_rotation_transfer() {
    const auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    constexpr long double pi=3.141592653589793238462643383279502884L;
    constexpr double omega=2.;
    for(double inner:{0.,1.}) {
        amr::Block parent{};std::array<amr::Block,4> fine;
        const auto initialize=[](amr::Block& block,double lo,double hi,double zlo,double zhi) {
            block.grid=Grid(amr::MAX_NG,lo,hi,zlo,zhi,0.,1.);
            block.grid.geometry="cylindrical";block.grid.dim=2;
            block.grid.InitializeTopology();
            block.fluid_state.Preallocate(block.grid.GetTotalSize());
            block.fluid_state.InitSpecies(1);
        };
        initialize(parent,inner,inner+1.,0.,1.);
        const auto& g=parent.grid;
        for(int j=0;j<g.GetTotalY();++j)for(int i=0;i<g.GetTotalX();++i) {
            const int cell=g.GetIndex(i,j,0);
            const double x=GridMetrics::Rz::AngularReconstructionCoordinate(
                g.GetFacePosL(i),g.GetFacePosR(i));
            parent.fluid_state.set(cell,{1.,0.,0.,omega*x,100.});
            parent.fluid_state.X(0,cell)=1.;
        }
        long double angular=0.;
        for(int c=0;c<4;++c) {
            const double lo=inner+.5*(c&1),z=.5*((c>>1)&1);
            initialize(fine[c],lo,lo+.5,z,z+.5);
            fine[c].InterpolateFromCoarse(parent,c,2,1.e-14,1.e-14,rz);
            const auto& fg=fine[c].grid;const auto& state=fine[c].fluid_state;
            for(int j=fg.Js();j<fg.Je();++j)for(int i=fg.Is();i<fg.Ie();++i) {
                const int cell=fg.GetIndex(i,j,0);
                const long double l=fg.GetFacePosL(i),h=fg.GetFacePosR(i);
                const long double reference=.75L*(h*h*h*h-l*l*l*l)/(h*h*h-l*l*l);
                expect(std::abs(state.mom_w[cell]-omega*reference)<1.e-12,
                       "RZ solid rotation did not reconstruct at W centroid");
                angular+=state.mom_w[cell]*(2.L*pi/3.L)*(h*h*h-l*l*l)*fg.dx2;
            }
        }
        const long double lo=inner,hi=inner+1.;
        const long double expected=omega*pi*(hi*hi*hi*hi-lo*lo*lo*lo)/2.L;
        expect(std::abs((angular-expected)/expected)<1.e-12,
               "RZ rigid rotation transfer lost independent analytic angular momentum");
    }
    std::cout<<"RZ_RIGID_ROTATION_TRANSFER_PASS\n";
}


void test_rz_candidate_parent_veto() {
    SimConfig config{};
    config.grid.dim=2;config.grid.geometry="cylindrical";
    config.grid.nblockx1=1;config.grid.nblockx2=1;config.grid.nblockx3=0;
    config.grid.x1_min=0.;config.grid.x1_max=1.;
    config.grid.x2_min=0.;config.grid.x2_max=1.;
    config.amr.lrefinemin=0;config.amr.lrefinemax=1;
    config.amr.refine_on_rho=false;
    config.numerics.sml_rho=1.e-14;config.numerics.min_eint=1.e-14;
    auto pool=std::make_shared<amr::MemoryPool>(8,2);
    amr::AmrTree tree(pool);
    const auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    tree.LoadLeafGrid(config,1,{1,1,1,1},{0,1,0,1},{0,0,1,1},{0,0,0,0},rz);
    const auto ids=tree.GetActiveBlocks();
    std::vector<FluidState> states;
    for(int id:ids) {
        auto& block=pool->GetBlock(id);const auto& g=block.grid;
        for(int j=g.Js();j<g.Je();++j)for(int i=g.Is();i<g.Ie();++i) {
            const bool high=(i-g.Is())&1;const int cell=g.GetIndex(i,j,0);
            block.fluid_state.set(cell,{1.,0.,0.,high?-16.:1.,high?2049./16.:9./16.});
            block.fluid_state.X(0,cell)=1.;
        }
        states.push_back(block.fluid_state);
    }
    int eos_calls=0;
    tree.SetJeansEvaluator([&](const FluidVector&,const double*,
        const GridMetrics::GeometryView&,int,int) {
        ++eos_calls;return JeansDiagnostics::Resolution{800.,JeansDiagnostics::Status::valid};
    });
    for(bool jeans:{false,true}) {
        config.amr.refine_on_jeans=jeans;config.amr.jeans_cells=160.;
        auto prepared=tree.PrepareRegrid(config,{}, {},[&] {
            for(int id:tree.GetActiveBlocks())pool->GetBlock(id).refine_flag=-1;
        });
        expect(prepared.topology_changed()&&prepared.proposed_active_blocks().size()==1,
            "native finite/simplex parent was rejected by a false raw thermal oracle");
        expect(tree.GetActiveBlocks()==ids,"provisional prepare published accepted topology");
        std::vector<amr::BlockHandle> old_handles;
        for(std::size_t n=0;n<ids.size();++n)old_handles.push_back({{81000+n},{1}});
        const std::vector<amr::BlockHandle> proposed_handles{{{82000},{2}}};
        prepared.BuildMigrationPlans(old_handles,proposed_handles,{91001,{1},{2}});
        const auto& plan=prepared.restriction_plan();
        expect(!plan.operations.empty(),"native prepared parent has no real restriction plan");
        for(const auto& op:plan.operations) {
            expect(op.destination.handle==proposed_handles.front()
                &&op.destination.logical.level==0&&op.destination.logical.logical_x1==0
                &&op.destination.logical.logical_x2==0&&op.destination.logical.logical_x3==0,
                "native parent endpoint differs from staged logical/handle identity");
            bool source_found=false;for(const auto& handle:old_handles)source_found|=op.source.handle==handle;
            expect(source_found,"native restriction plan refers to an unrelated source frame");
        }
        expect(eos_calls==0,"ghostless native preparation called EOS/JENS");
        prepared.AbortNoexcept();
        expect(tree.GetActiveBlocks()==ids&&pool->GetNumActiveBlocks()==4,
            "native provisional abort changed source tree/pool ownership");
        for(std::size_t n=0;n<ids.size();++n) {
            const auto& u=pool->GetBlock(ids[n]).fluid_state;
            expect(u.rho==states[n].rho&&u.mom_w==states[n].mom_w&&u.eng==states[n].eng
                &&u.mass_fractions==states[n].mass_fractions,
                "native provisional abort changed source E/J/rhoX");
        }
    }
    std::cout<<"RZ_CANDIDATE_PARENT_PROVISIONAL_ABORT_PASS runtime_thermal_gate=false\n";
}

void test_rz_angular_restriction_counterexample() {
    using namespace amr::regrid_math;
    const double rho[]{1.,1.,1.,1.};
    const double zero[]{0.,0.,0.,0.};
    const double angular[]{1.,-16.,1.,-16.};
    const double energy[]{9./16.,2049./16.,9./16.,2049./16.};
    const double x[]{1.,1.,1.,1.};
    ConstStateView source{{rho,zero,zero,angular,energy,zero},x,4};
    RestrictionGeometry geometry{};
    geometry.count=4;geometry.coarse_volume=4.;
    geometry.angular_momentum=true;geometry.coarse_angular_measure=8.;
    for(int i=0;i<4;++i) {
        geometry.source_cells[i]=i;
        geometry.volumes[i]=(i&1)?1.5:.5;
        geometry.angular_measures[i]=(i&1)?3.5:.5;
        expect(is_admissible_conserved_state(source.fluid(i),1.e-14,1.e-14),
               "RZ veto reference child not admissible");
    }
    RestrictionResult result{};double workspace[1]{};
    expect(restrict_family(source,geometry,1,1.e-14,1.e-14,workspace,result)==Status::Ok,
           "RZ provisional restriction applied an invalid Cartesian thermal veto");
    expect(result.fluid.mom_w==-111./8. && result.fluid.eng==1539./16.,
           "RZ W parent differs from independent frozen rational reference");
    expect(result.fluid.eng-.5*result.fluid.mom_w*result.fluid.mom_w==-9./128.,
           "RZ parent counterexample internal energy changed");
    // Constant rho on [0,1]: V=1/2, W=1/3, I=1/4, hence kappa=8/9.
    // True numerical native e=1539/16-(8/9)*(111/8)^2/2=85/8 > 0.
    // These exact rational values are independent of the closure implementation.
    const long double independent_e=1539.L/16.L-(8.L/9.L)*(111.L/8.L)*(111.L/8.L)/2.L;
    expect(std::abs(independent_e-85.L/8.L)<2.e-12L,"independent native thermal rational changed");
    Grid native_grid(amr::MAX_NG,0.,16.,-.5,.5,0.,1.);
    native_grid.geometry="cylindrical";native_grid.dim=2;native_grid.InitializeTopology();
    const auto native_geometry=GridMetrics::make_geometry_view(native_grid,
        GridMetrics::GeometrySemantics::AxisymmetricRz);
    const auto read=[&](int){return result.fluid;};
    const auto closure=RzThermodynamics::make_cell(read,
        native_grid.GetIndex(native_grid.Is(),native_grid.Js(),0),native_geometry,
        native_grid.Is(),arch::state::Bounds{1.e-14,1.e-14,100.});
    expect(closure.valid()&&std::abs(closure.internal-static_cast<double>(independent_e))<2.e-12,
        "actual density stencil native closure disagrees with independent kappa/e");
    geometry.angular_momentum=false;
    expect(restrict_family(source,geometry,1,1.e-14,1.e-14,workspace,result)==Status::Ok,
           "legacy V parent reference should resolve; test does not distinguish W");
    geometry.angular_momentum=true;geometry.angular_measures[0]=0.;
    expect(restrict_family(source,geometry,1,1.e-14,1.e-14,workspace,result)==Status::InvalidGeometry,
           "RZ accepted zero angular measure");
    std::cout<<"RZ_ANGULAR_RESTRICTION_COUNTEREXAMPLE_PASS\n";
}

/** Independent antiderivatives for rho=Omega=1, e=1/64 on an actual cell.
 * V~int r dr, W~int r^2 dr, I~int r^3 dr. Signed axis ghosts inherit even
 * rho/E and odd m_phi. These expectations never call the production quadrature.
 */
FluidVector rz_cold_cell_reference(const Grid& grid,int i)
{
    long double low=grid.GetFacePosL(i),high=grid.GetFacePosR(i),sign=1.;
    if(high<=0.) {const long double saved=low;low=-high;high=-saved;sign=-1.;}
    const long double v=(high*high-low*low)/2.L;
    const long double w=(high*high*high-low*low*low)/3.L;
    const long double inertia=(high*high*high*high-low*low*low*low)/4.L;
    return {1.,0.,0.,static_cast<double>(sign*inertia/w),
        static_cast<double>(1.L/64.L+inertia/(2.L*v))};
}

/** Build one actual Block geometry/layout, without any scheduler/EOS witness. */
void rz_test_block_geometry(amr::Block& block,double low,double high,
    double axial_low,double axial_high,int species)
{
    block.grid=Grid(amr::MAX_NG,low,high,axial_low,axial_high,0.,1.);
    block.grid.geometry="cylindrical";block.grid.dim=2;block.grid.InitializeTopology();
    block.fluid_state.Preallocate(block.grid.GetTotalSize());block.fluid_state.InitSpecies(species);
}

/** Populate real logical cold-spin cells; optional ghosts-only mode preserves
 * actual transferred interiors. Analytic fixture ghosts are explicitly not a
 * Runtime BC/exchange publication or topology/clock qualification.
 */
void rz_test_fill_cold(amr::Block& block,bool ghosts_only=false)
{
    const auto& grid=block.grid;
    for(int j=0;j<grid.GetTotalY();++j)for(int i=0;i<grid.GetTotalX();++i) {
        if(ghosts_only&&i>=grid.Is()&&i<grid.Ie()&&j>=grid.Js()&&j<grid.Je())continue;
        const int index=grid.GetIndex(i,j,0);
        block.fluid_state.set(index,rz_cold_cell_reference(grid,i));
        block.fluid_state.enuc_rate[index]=.125;
        for(int s=0;s<block.fluid_state.GetNumSpecies();++s)block.fluid_state.X(s,index)=s==0?1.:0.;
    }
}

/** Actual Block prolongation/restriction with independent cold V/W/E references.
 * No copied-parent baseline is accepted as an outer fine-cell thermal oracle.
 * Source/fine ghosts are real analytic fixture cells; final Runtime science is
 * separately owned. Original 2e-12 numeric budget remains unchanged.
 */
void test_rz_cold_block_family_roundtrip()
{
    const auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    constexpr long double pi=3.141592653589793238462643383279502884L;
    const arch::state::Bounds bounds{1.e-14,1./128.,1./32.};
    amr::Block parent{},restored{};std::array<amr::Block,4> fine;
    rz_test_block_geometry(parent,0.,1.,-.5,.5,1);
    rz_test_block_geometry(restored,0.,1.,-.5,.5,1);
    rz_test_fill_cold(parent);rz_test_fill_cold(restored);
    const FluidState original=parent.fluid_state;
    const amr::Block* children[4]{};
    SpeciesManager species;species.add_species("gas",1.,1.,1.4,1.);
    IdealGas eos(1.4,species);
    std::array<long double,4> before{},after{},refined{};
    const auto accumulate=[&](const amr::Block& block,std::array<long double,4>& totals) {
        const auto& g=block.grid;const auto& u=block.fluid_state;
        for(int j=g.Js();j<g.Je();++j)for(int i=g.Is();i<g.Ie();++i) {
            const int index=g.GetIndex(i,j,0);const long double l=g.GetFacePosL(i),h=g.GetFacePosR(i);
            const long double v=pi*(h*h-l*l)*g.dx2;
            const long double w=(2.L*pi/3.L)*(h*h*h-l*l*l)*g.dx2;
            totals[0]+=u.rho[index]*v;totals[1]+=u.mom_w[index]*w;
            totals[2]+=u.eng[index]*v;totals[3]+=u.rho[index]*u.X(0,index)*v;
        }
    };
    accumulate(parent,before);
    for(int c=0;c<4;++c) {
        const double r=.5*(c&1),z=-.5+.5*((c>>1)&1);
        rz_test_block_geometry(fine[c],r,r+.5,z,z+.5,1);
        fine[c].InterpolateFromCoarse(parent,c,2,bounds.density,bounds.internal_min,rz);
        children[c]=&fine[c];rz_test_fill_cold(fine[c],true);
        const auto& g=fine[c].grid;const auto& u=fine[c].fluid_state;
        const auto geometry=GridMetrics::make_geometry_view(g,rz);
        const auto read=[&](int index){return u.get(index);};
        for(int j=g.Js();j<g.Je();++j)for(int i=g.Is();i<g.Ie();++i) {
            const int index=g.GetIndex(i,j,0);const auto expected=rz_cold_cell_reference(g,i),actual=u.get(index);
            expect(std::abs(actual.rho-expected.rho)<2.e-12&&std::abs(actual.mom_u)<2.e-12
                &&std::abs(actual.mom_v)<2.e-12&&std::abs(actual.mom_w-expected.mom_w)<2.e-12
                &&std::abs(actual.eng-expected.eng)<2.e-12,
                "actual cold Block prolongation differs from independent V/W antiderivatives");
            const auto closure=RzThermodynamics::make_cell(read,index,geometry,i,bounds);
            expect(closure.valid()&&std::abs(closure.internal-1./64.)<2.e-12,
                "actual fine density/rotation closure lost independent cold thermal energy");
            const double x=u.X(0,index);
            expect(std::abs(x-1.)<2.e-12,"cold Block species mass did not follow real rho_V");
            for(int node=0;node<RzThermodynamics::physical_node_count;++node) {
                const auto point=RzThermodynamics::base_point(closure,
                    RzThermodynamics::physical_node_radius(closure,node));
                expect(arch::state::validate_eos(point,&x,1,bounds,eos)==arch::state::Status::valid,
                    "cold transferred numerical baseline failed actual point IdealGas");
                const double pressure=eos.get_pressure(point,&x);
                expect(std::abs(pressure-(1.4-1.)/64.)<2.e-12,
                    "cold baseline pressure differs from independent IdealGas reference");
            }
        }
        accumulate(fine[c],refined);
    }
    expect(restored.TryAverageToCoarse(children,2,bounds.density,bounds.internal_min,rz)
        ==amr::regrid_math::Status::Ok,"cold actual Block restriction used a raw kinetic veto");
    accumulate(restored,after);
    for(int n=0;n<4;++n)
        expect(std::abs(refined[n]-before[n])<2.e-12L*std::max(1.L,std::abs(before[n]))
            &&std::abs(after[n]-before[n])<2.e-12L*std::max(1.L,std::abs(before[n])),
            "cold actual Block roundtrip lost independent mass/J/E/species integral");
    for(int j=parent.grid.Js();j<parent.grid.Je();++j)for(int i=parent.grid.Is();i<parent.grid.Ie();++i) {
        const int index=parent.grid.GetIndex(i,j,0);
        expect(std::abs(restored.fluid_state.rho[index]-original.rho[index])<2.e-12
            &&std::abs(restored.fluid_state.mom_w[index]-original.mom_w[index])<2.e-12
            &&std::abs(restored.fluid_state.eng[index]-original.eng[index])<2.e-12,
            "cold actual restricted cell differs from original native moments");
    }
    expect(parent.fluid_state.rho==original.rho&&parent.fluid_state.mom_w==original.mom_w
        &&parent.fluid_state.eng==original.eng&&parent.fluid_state.mass_fractions==original.mass_fractions,
        "actual Block transfer mutated immutable source native fields");
    // Whole native [0,1] means: mphi=3/4 and E=17/64. Copied into [.5,1],
    // kappa=392/405 gives e=17/64-(392/405)*(3/4)^2/2=-19/2880.
    Grid outer(amr::MAX_NG,.5,8.5,-.5,.5,0.,1.);
    outer.geometry="cylindrical";outer.dim=2;outer.InitializeTopology();
    const FluidVector copied{1.,0.,0.,3./4.,17./64.};
    const long double bad_e=17.L/64.L-(392.L/405.L)*(3.L/4.L)*(3.L/4.L)/2.L;
    expect(std::abs(bad_e+19.L/2880.L)<2.e-12L,"independent copied cold reference changed");
    const auto copy_closure=RzThermodynamics::make_cell([&](int){return copied;},
        outer.GetIndex(outer.Is(),outer.Js(),0),GridMetrics::make_geometry_view(outer,rz),outer.Is(),bounds);
    expect(!copy_closure.valid(),"copied parent moments were mistaken for an admissible outer fine baseline");
    std::cout<<"RZ_COLD_BLOCK_FAMILY_PASS exact_native_integrals=true runtime_bc_qualification=false\n";
}

/** Thirteen-species public family math: independent minmod gradients need not
 * sum to zero. Test actual family row/column conservation and a constant trace;
 * do not call the limited profile an exact analytic physical solution.
 */
void test_rz_thirteen_species_family_simplex()
{
    using namespace amr::regrid_math;
    constexpr int count=13;constexpr double trace=1.e-20;
    const auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    amr::Block source{};rz_test_block_geometry(source,0.,1.,-.5,.5,count);rz_test_fill_cold(source);
    const auto& g=source.grid;const int i=g.Is()+2,j=g.Js()+2,index=g.GetIndex(i,j,0);
    for(int y=0;y<g.GetTotalY();++y)for(int x=0;x<g.GetTotalX();++x) {
        const int cell=g.GetIndex(x,y,0);
        const std::array<double,3> values=x<i?std::array<double,3>{.1,.2,.7}
            :x==i?std::array<double,3>{.2,.3,.5}:std::array<double,3>{.25,.45,.3};
        for(int s=0;s<count;++s)source.fluid_state.X(s,cell)=s<3?values[s]:s==12?trace:0.;
    }
    // Independent raw minmod increments: .05 + .10 - .20 = -.05.
    // Finite row correction of that defect is forbidden; dependent largest
    // Xi must make the physical profile simplex-preserving before integration.
    expect(std::abs((.05L+.10L-.20L)+.05L)<2.e-12L,
        "three-species nonclosure witness changed");
    NativeRzProlongationContext context{};
    context.source_geometry=GridMetrics::make_geometry_view(g,rz);
    context.logical_nx=g.GetTotalX();context.logical_ny=g.GetTotalY();context.radial_i=i;context.axial_j=j;
    const double rl=g.GetFacePosL(i),rr=g.GetFacePosR(i),rm=rl+.5*(rr-rl);
    const double zl=g.x2_min+(j-g.ng)*g.dx2,zr=g.x2_min+(j-g.ng+1)*g.dx2,zm=zl+.5*(zr-zl);
    for(int c=0;c<4;++c)context.children[c]={c&1?rm:rl,c&1?rr:rm,c&2?zm:zl,c&2?zr:zm};
    std::array<double,count*prolongation_workspace_per_species> workspace{};
    ProlongationResult result{};
    const auto state=[&](int k){return source.fluid_state.get(k);};
    const auto enuc=[&](int k){return source.fluid_state.enuc_rate[k];};
    const auto fraction=[&](int s,int k){return source.fluid_state.X(s,k);};
    expect(prolong_native_family(context,state,enuc,fraction,count,{1.e-14,1./128.,1./32.},
        workspace.data(),result)==Status::Ok,"dependent thirteen-species native family failed finite/simplex transfer");
    std::array<long double,count> columns{};long double volume=0.;
    for(int c=0;c<4;++c) {
        const auto& b=context.children[c];const long double lo=b.radial_lower,hi=b.radial_upper;
        const long double v=(hi*hi-lo*lo)/2.L*(b.axial_upper-b.axial_lower);volume+=v;
        double row=0.;
        for(int s=0;s<count;++s) {
            const double mass=result.rhoX[s*maximum_children+c];
            expect(std::isfinite(mass)&&mass>=0.,"thirteen-species child contains a nonfinite/negative mass");
            row+=mass;columns[s]+=mass*v;
        }
        expect(std::abs(row-result.fluid[c].rho)<2.e-12,
            "thirteen-species rhoX row fails to close to actual native density");
        expect(std::abs(result.rhoX[12*maximum_children+c]/result.fluid[c].rho-trace)
            <=16.*std::numeric_limits<double>::epsilon()*trace,
            "thirteen-species closure invented or erased the constant positive trace");
    }
    for(int s=0;s<count;++s) {
        const long double reference=source.fluid_state.rho[index]*source.fluid_state.X(s,index)*volume;
        expect(std::abs(columns[s]-reference)<2.e-12L*volume,
            "thirteen-species own-V column mass differs from immutable parent target");
    }
    std::cout<<"RZ_THIRTEEN_SPECIES_FAMILY_PASS independent_limited_plus_dependent_simplex=true\n";
}

/** Strict FP64 counterexamples use actual logical cells and a valid source
 * closure. Neither unrepresentable positive rhoX nor loss of a represented
 * constant momentum may publish a pending result or introduce a physical floor.
 */
void test_rz_native_family_representability()
{
    using namespace amr::regrid_math;
    const auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    const arch::state::Bounds bounds{0.,0.,2.};
    for(const int count:{0,2,13}) {
        amr::Block source{};rz_test_block_geometry(source,1.,2.,0.,1.,count);
        const auto& grid=source.grid;
        const double mr=count==0?std::numeric_limits<double>::denorm_min():0.;
        for(int j=0;j<grid.GetTotalY();++j)for(int i=0;i<grid.GetTotalX();++i) {
            const int cell=grid.GetIndex(i,j,0);
            source.fluid_state.set(cell,{1.e-300,mr,0.,0.,1.e-300});
            for(int sp=0;sp<count;++sp)source.fluid_state.X(sp,cell)=sp==0?1.:1.e-30;
        }
        NativeRzProlongationContext context{};
        context.source_geometry=GridMetrics::make_geometry_view(grid,rz);
        context.logical_nx=grid.GetTotalX();context.logical_ny=grid.GetTotalY();
        context.radial_i=grid.Is()+5;context.axial_j=grid.Js()+5;
        const int i=context.radial_i,j=context.axial_j,index=grid.GetIndex(i,j,0);
        const double rl=grid.GetFacePosL(i),rr=grid.GetFacePosR(i),rm=rl+.5*(rr-rl);
        const double zl=grid.x2_min+(j-grid.ng)*grid.dx2;
        const double zr=grid.x2_min+(j-grid.ng+1)*grid.dx2,zm=zl+.5*(zr-zl);
        for(int child=0;child<4;++child)context.children[child]={
            (child&1)?rm:rl,(child&1)?rr:rm,(child&2)?zm:zl,(child&2)?zr:zm};
        const auto read=[&](int cell){return source.fluid_state.get(cell);};
        const auto enuc=[&](int cell){return source.fluid_state.enuc_rate[cell];};
        const auto fraction=[&](int sp,int cell){return source.fluid_state.X(sp,cell);};
        expect(RzThermodynamics::make_cell(read,index,context.source_geometry,i,bounds).valid(),
            "subnormal counterexample source closure is not admissible");
        ProlongationResult sentinel{};sentinel.fluid[0]={123.,124.,125.,126.,127.};
        std::vector<double> workspace(static_cast<std::size_t>(count)*prolongation_workspace_per_species);
        const auto status=prolong_native_family(context,read,enuc,fraction,count,bounds,
            workspace.data(),sentinel);
        expect(status==(count==0?Status::ParentFluid:Status::SpeciesIntegral),
            "native family silently erased represented momentum or an unrepresentable positive trace");
        expect(sentinel.fluid[0].rho==123.&&sentinel.fluid[0].mom_u==124.
            &&sentinel.fluid[0].mom_v==125.&&sentinel.fluid[0].mom_w==126.
            &&sentinel.fluid[0].eng==127.&&sentinel.rhoX==nullptr,
            "failed native arithmetic published its pending result");
    }
    std::cout<<"RZ_NATIVE_REPRESENTABILITY_PASS cases=3 explicit_failure=1 no_result_publication=1\n";
}


/** Real deep native AMR fixtures use the production Block geometry generator.
 * Root spacings are the actual Tree inputs; no face-generation formula is
 * repeated here. Five Blocks suffice regardless of their logical level.
 */
void rz_canonical_actual_block(amr::Block& block,const Grid& root,int level,
    std::uint32_t radial,std::uint32_t axial,int id)
{
    block.Reset();block.id=id;block.level=level;
    block.logical_x1=radial;block.logical_x2=axial;block.logical_x3=0;
    block.InitGeometry(root,(root.x1_max-root.x1_min)/(root.nblockx1*amr::BLOCK_NX),
        (root.x2_max-root.x2_min)/(root.nblockx2*amr::BLOCK_NY),1.,
        GridMetrics::GeometrySemantics::AxisymmetricRz);
    block.fluid_state.Preallocate(block.grid.GetTotalSize());
    block.fluid_state.InitSpecies(2);
}

/** Independent factored antiderivatives for rho=5/4, physical e=100.
 * For rigid rotation, m_phi,W=rho*Omega*int(r^3)dr/int(r^2)dr;
 * E_V=rho*(100+Omega^2*(r_hi^2+r_lo^2)/4). Actual endpoints
 * come from the production Grid; these are not copied-parent or point means.
 */
FluidVector rz_canonical_uniform_reference(const Grid& grid,int i,double omega)
{
    constexpr long double rho=1.25L;
    const long double l=grid.GetFacePosL(i),h=grid.GetFacePosR(i);
    const long double angular_radius=.75L*(h+l)*(h*h+l*l)/(h*h+h*l+l*l);
    const long double energy=rho*(100.L+omega*omega*(h*h+l*l)/4.L);
    return {static_cast<double>(rho),0.,0.,static_cast<double>(rho*omega*angular_radius),
        static_cast<double>(energy)};
}

/** Fill every actual source/fine ghost and allocated pad with admissible data.
 * Analytic test ghosts are not a Runtime BC/regrid/restart qualification.
 */
void rz_canonical_fill_uniform(amr::Block& block,double omega)
{
    auto& u=block.fluid_state;const auto& g=block.grid;
    for(int cell=0;cell<g.GetTotalSize();++cell) {
        u.set(cell,{1.25,0.,0.,0.,125.});u.enuc_rate[cell]=.125;
        u.X(0,cell)=.3;u.X(1,cell)=.7;
    }
    for(int j=0;j<g.GetTotalY();++j)for(int i=0;i<g.GetTotalX();++i)
        u.set(g.GetIndex(i,j,0),rz_canonical_uniform_reference(g,i,omega));
}

/** Independent full-ring V/W integrals on actual physical endpoints.
 * Factoring differences retains the information of deep outer thin cells.
 * No production CellVolume/angular-measure helper supplies the reference.
 */
std::array<long double,2> rz_canonical_true_measures(const Grid& grid,int i,int j)
{
    constexpr long double pi=3.141592653589793238462643383279502884L;
    const long double l=grid.GetFacePosL(i),h=grid.GetFacePosR(i);
    const long double zl=grid.GetAxialFacePosL(j),zh=grid.GetAxialFacePosR(j);
    const long double width=h-l,height=zh-zl;
    expect(std::isfinite(l)&&std::isfinite(h)&&l>=0.&&width>0.
        &&std::isfinite(zl)&&std::isfinite(zh)&&height>0.,
        "deep actual native cell has invalid physical endpoints");
    return {pi*width*(h+l)*height,
        (2.L*pi/3.L)*width*(h*h+h*l+l*l)*height};
}

/** Relative integral check at the unchanged family 64-epsilon budget.
 * Positive mass/E/species/measure targets never receive a max(1,scale)
 * allowance, which would hide errors in tiny deep-cell integrals.
 */
void rz_canonical_integral_equal(long double actual,long double reference,
    const char* label)
{
    constexpr long double roundoff=64.L*std::numeric_limits<double>::epsilon();
    expect(std::isfinite(actual)&&std::isfinite(reference)&&reference!=0.,
        std::string(label)+" lacks a nonzero finite independent target");
    expect(std::abs(actual-reference)<=roundoff*std::abs(reference),
        std::string(label)+" violated original 64-epsilon actual-measure conservation");
}

/** Production InitGeometry -> Block native prolongation -> physical integrals.
 * Six deep nonbinary geometries and two real physical profiles cover each
 * parent's 256 cell families, including thin outer cells at L14 -> L15.
 * Actual point EOS is checked after the shared density/rotation closure.
 * This is transfer-owner science only, not whole Runtime/AMR qualification.
 */
void test_rz_canonical_deep_actual_block_families()
{
    constexpr auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    const arch::state::Bounds bounds{1.e-14,1.e-14,1.e6};
    SpeciesManager species;species.add_species("a",1.,1.,1.4,1.);
    species.add_species("b",1.,1.,1.4,1.);IdealGas eos(1.4,species);
    std::size_t families=0,physical_points=0;
    for(const std::array<int,2> roots:{std::array<int,2>{1,1},std::array<int,2>{3,5}})
      for(int level:{3,10,14})for(double omega:{0.,1.}) {
        Grid root(amr::MAX_NG,.1,1.3,-.3,.7,0.,1.,roots[0],roots[1],1);
        root.dim=2;root.geometry="cylindrical";root.InitializeTopology(rz);
        const auto factor=std::uint32_t{1}<<level;
        const std::uint32_t lx=static_cast<std::uint32_t>(roots[0])*factor-1;
        const std::uint32_t ly=static_cast<std::uint32_t>(roots[1])*factor-1;
        amr::Block parent{};std::array<amr::Block,4> fine;
        rz_canonical_actual_block(parent,root,level,lx,ly,0);
        rz_canonical_fill_uniform(parent,omega);const FluidState immutable_source=parent.fluid_state;
        for(int child=0;child<4;++child) {
            rz_canonical_actual_block(fine[child],root,level+1,
                2*lx+(child&1),2*ly+((child>>1)&1),child+1);
            rz_canonical_fill_uniform(fine[child],omega);
            fine[child].InterpolateFromCoarse(parent,child,2,bounds.density,
                bounds.internal_min,rz,bounds.internal_max);
        }
        const auto& pg=parent.grid;
        for(int j=0;j<amr::BLOCK_NY;++j)for(int i=0;i<amr::BLOCK_NX;++i) {
            const int parent_index=pg.GetIndex(pg.Is()+i,pg.Js()+j,0);
            const auto source=parent.fluid_state.get(parent_index);
            const auto measure=rz_canonical_true_measures(pg,pg.Is()+i,pg.Js()+j);
            const int quadrant=(i>=amr::BLOCK_NX/2?1:0)|(j>=amr::BLOCK_NY/2?2:0);
            const auto& child=fine[quadrant];const auto& fg=child.grid;const auto& u=child.fluid_state;
            const int fi=fg.Is()+2*(i%(amr::BLOCK_NX/2));
            const int fj=fg.Js()+2*(j%(amr::BLOCK_NY/2));
            std::array<long double,7> sum{};
            for(int member=0;member<4;++member) {
                const int ci=fi+(member&1),cj=fj+((member>>1)&1),cell=fg.GetIndex(ci,cj,0);
                const auto value=u.get(cell);
                const auto m=rz_canonical_true_measures(fg,ci,cj);
                expect(value.rho>0.&&value.eng>0.&&std::isfinite(value.rho)&&std::isfinite(value.eng),
                    "deep actual native child has invalid mass/energy");
                expect(value.mom_u==0.&&value.mom_v==0.,"uniform native transfer invented r/z motion");
                if(omega==0.)expect(value.mom_w==0.,"nonrotating native transfer invented angular momentum");
                sum[0]+=m[0];sum[1]+=m[1];sum[2]+=value.rho*m[0];sum[3]+=value.eng*m[0];
                sum[4]+=value.mom_w*m[1];sum[5]+=value.rho*u.X(0,cell)*m[0];
                sum[6]+=value.rho*u.X(1,cell)*m[0];
                const auto geometry=GridMetrics::make_geometry_view(fg,rz);
                const auto closure=RzThermodynamics::make_cell([&](int index){return u.get(index);},
                    cell,geometry,ci,bounds);
                expect(closure.valid(),"deep actual child density/rotation closure is invalid");
                const double x[]{u.X(0,cell),u.X(1,cell)};
                for(int node=0;node<RzThermodynamics::physical_node_count;++node) {
                    const auto point=RzThermodynamics::base_point(closure,
                        RzThermodynamics::physical_node_radius(closure,node));
                    const auto thermal=arch::state::recover(point);
                    expect(thermal.status==arch::state::Status::valid,
                        "deep actual native physical point has invalid recovered thermal state");
                    // A uniform nonrotating profile is point-exact. The rotated
                    // finite-order profile is qualified by actual conservation
                    // and EOS/bounds, not an unproved point-exact e=100 oracle.
                    if(omega==0.)
                        expect(std::abs(thermal.internal-100.)
                            <=64.*std::numeric_limits<double>::epsilon()*100.,
                            "nonrotating actual native physical point lost independent e=100");
                    expect(thermal.internal>=bounds.internal_min
                        &&thermal.internal<=bounds.internal_max,
                        "deep actual native physical point exceeded original thermal bounds");
                    expect(arch::state::validate_eos(point,x,2,bounds,eos)==arch::state::Status::valid,
                        "deep actual native physical point failed actual IdealGas");
                    ++physical_points;
                }
            }
            rz_canonical_integral_equal(sum[0],measure[0],"positive V partition");
            rz_canonical_integral_equal(sum[1],measure[1],"positive W partition");
            rz_canonical_integral_equal(sum[2],source.rho*measure[0],"native mass");
            rz_canonical_integral_equal(sum[3],source.eng*measure[0],"native total energy");
            if(omega!=0.)rz_canonical_integral_equal(sum[4],source.mom_w*measure[1],"nonzero native angular momentum");
            else expect(sum[4]==0.,"zero native angular integral drifted");
            rz_canonical_integral_equal(sum[5],source.rho*.3L*measure[0],"species a mass");
            rz_canonical_integral_equal(sum[6],source.rho*.7L*measure[0],"species b mass");
            ++families;
        }
        expect(parent.fluid_state.rho==immutable_source.rho&&parent.fluid_state.mom_u==immutable_source.mom_u
            &&parent.fluid_state.mom_v==immutable_source.mom_v&&parent.fluid_state.mom_w==immutable_source.mom_w
            &&parent.fluid_state.eng==immutable_source.eng&&parent.fluid_state.enuc_rate==immutable_source.enuc_rate
            &&parent.fluid_state.mass_fractions==immutable_source.mass_fractions,
            "deep actual Block transfer mutated immutable source data");
        const FluidState destination_before=fine[0].fluid_state;
        const auto unchanged_destination=[&] {
            const auto& u=fine[0].fluid_state;
            return u.rho==destination_before.rho&&u.mom_u==destination_before.mom_u
                &&u.mom_v==destination_before.mom_v&&u.mom_w==destination_before.mom_w
                &&u.eng==destination_before.eng&&u.enuc_rate==destination_before.enuc_rate
                &&u.mass_fractions==destination_before.mass_fractions;
        };
        const auto saved_logical=parent.logical_x1;++parent.logical_x1;
        expect_rejected([&]{fine[0].InterpolateFromCoarse(parent,0,2,bounds.density,
            bounds.internal_min,rz,bounds.internal_max);},"native transfer accepted foreign Block logical identity");
        expect(unchanged_destination(),"foreign Block identity wrote destination before rejecting");
        parent.logical_x1=saved_logical;
        const double saved_upper=parent.grid.dyadic_identity.root_upper[0];
        parent.grid.dyadic_identity.root_upper[0]=std::nextafter(saved_upper,std::numeric_limits<double>::infinity());
        expect_rejected([&]{fine[0].InterpolateFromCoarse(parent,0,2,bounds.density,
            bounds.internal_min,rz,bounds.internal_max);},"native transfer accepted changed root endpoint identity");
        expect(unchanged_destination(),"changed root identity wrote destination before rejecting");
        parent.grid.dyadic_identity.root_upper[0]=saved_upper;
      }
    expect(families==12u*amr::BLOCK_NX*amr::BLOCK_NY&&physical_points>0,
        "deep actual Block fixture skipped an expected geometry/profile family");
    std::cout<<"RZ_CANONICAL_DEEP_BLOCK_FAMILY_PASS families="<<families
        <<" point_eos="<<physical_points<<" runtime_bc_qualification=false\n";
}

void test_rz_regrid_roundtrip() {
    const auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    for(double inner:{0.,1.}) {
        amr::Block parent{},restored{};
        std::array<amr::Block,4> fine;
        const auto initialize=[&](amr::Block& block,double left,double right,double low,double high) {
            block.grid=Grid(amr::MAX_NG,left,right,low,high,0.,1.);
            block.grid.geometry="cylindrical";block.grid.dim=2;block.grid.InitializeTopology();
            block.fluid_state.Preallocate(block.grid.GetTotalSize());
            block.fluid_state.InitSpecies(2);
        };
        initialize(parent,inner,inner+1.,-.5,.5);
        initialize(restored,inner,inner+1.,-.5,.5);
        for(int c=0;c<4;++c) {
            const double x=inner+.5*(c&1),z=-.5+.5*((c>>1)&1);
            initialize(fine[c],x,x+.5,z,z+.5);
        }
        auto& g=parent.grid;
        for(int j=0;j<g.GetTotalY();++j)for(int i=0;i<g.GetTotalX();++i) {
            const int cell=g.GetIndex(i,j,0);
            const double radius=g.GetCellCenterX(i),z=g.GetCellCenterY(j);
            const double rho=2.+.1*radius+.2*z;
            parent.fluid_state.set(cell,{rho,.1*rho,.2*rho,2.*rho*radius,100.*rho});
            parent.fluid_state.enuc_rate[cell]=.3*rho;
            parent.fluid_state.X(0,cell)=.6+.01*radius;
            parent.fluid_state.X(1,cell)=1.-parent.fluid_state.X(0,cell);
        }
        const amr::Block* children[4];
        for(int c=0;c<4;++c) {
            fine[c].InterpolateFromCoarse(parent,c,2,1.e-14,1.e-14,rz);
            children[c]=&fine[c];
        }
        restored.AverageToCoarse(children,2,1.e-14,1.e-14,rz);
        const auto integrals=[](const amr::Block& block) {
            std::array<long double,9> sum{};
            const auto& g=block.grid;const auto& u=block.fluid_state;
            for(int j=g.Js();j<g.Je();++j)for(int i=g.Is();i<g.Ie();++i) {
                const int cell=g.GetIndex(i,j,0);
                const long double lo=g.GetFacePosL(i),hi=g.GetFacePosR(i);
                const long double volume=arch::constants::math::pi*(hi*hi-lo*lo)*g.dx2;
                const double data[]{u.rho[cell],u.mom_u[cell],u.mom_v[cell],
                    u.mom_w[cell],u.eng[cell],u.enuc_rate[cell],
                    u.rho[cell]*u.X(0,cell),u.rho[cell]*u.X(1,cell)};
                for(int a=0;a<8;++a)sum[a]+=volume*data[a];
                // Integral r*dV for piecewise-constant momentum_phi, not r_mid*V.
                const long double radial_moment=(2.L/3.L)*arch::constants::math::pi
                    *(hi*hi*hi-lo*lo*lo)*g.dx2;
                sum[8]+=radial_moment*u.mom_w[cell];
            }
            return sum;
        };
        const auto before=integrals(parent),after=integrals(restored);
        std::array<long double,9> refined{};
        for(const auto& block:fine) {
            const auto sum=integrals(block);
            for(int a=0;a<9;++a)refined[a]+=sum[a];
        }
        double max_error=0.;
        for(int a=0;a<9;++a) {
            if(a==3)continue; // m_phi is W-averaged; its V integral is not conserved.
            const double scale=std::max(1.,std::abs(static_cast<double>(before[a])));
            max_error=std::max(max_error,static_cast<double>(
                std::max(std::abs(after[a]-before[a]),std::abs(refined[a]-before[a])))/scale);
        }
        expect(max_error<1.e-12,"RZ Block migration lost V conserved fields or W angular integral");
        std::cout<<std::setprecision(17)<<"RZ_REGRID inner="<<inner<<" conserved_error="<<max_error
            <<" angular_before="<<static_cast<double>(before[8])
            <<" angular_refined="<<static_cast<double>(refined[8])
            <<" angular_restored="<<static_cast<double>(after[8])
            <<" angular_relative_change="<<static_cast<double>((refined[8]-before[8])/before[8])<<'\n';
        // J itself is an asserted invariant, independent of ordinary V fields.
    }
}

/** Axis donor/parity and topology-cache ownership only. The fixture initializes
 * active cells; it does not provide the completed physical halo needed by the
 * separate native coarse/fine thermodynamic transfer owner.
 */
void test_rz_axis_seam(bool mixed, double inner_radius)
{
    SimConfig config{};
    config.grid.dim=2;config.grid.geometry="cylindrical";
    config.grid.nblockx1=1;config.grid.nblockx2=2;config.grid.nblockx3=0;
    config.grid.x1_min=inner_radius;config.grid.x1_max=inner_radius+1.;
    config.grid.x2_min=-1.;config.grid.x2_max=1.;
    config.grid.x1l_boundary_type="reflecting";
    config.grid.x2l_boundary_type="outflow";config.grid.x2r_boundary_type="outflow";
    config.grid.amr_max_blocks=24;
    config.amr.lrefinemin=0;config.amr.lrefinemax=mixed?1:0;
    amr::AMRControl control(24,2);
    if (mixed) {
        control.tree->LoadLeafGrid(config,2,
            {1,1,1,1,0},{0,1,0,1,0},{0,0,1,1,1},{0,0,0,0,0});
    } else control.tree->InitRootGrid(config,2);
    const auto& active=control.tree->GetActiveBlocks();
    for (int id:active) {
        auto& block=control.pool->GetBlock(id);
        const auto& grid=block.grid;
        for (int j=grid.Js();j<grid.Je();++j)
            for (int i=grid.Is();i<grid.Ie();++i) {
                const int cell=grid.GetIndex(i,j,grid.Ks());
                const double radius=grid.GetCellCenterX(i),z=grid.GetCellCenterY(j);
                auto& state=block.fluid_state;
                state.rho[cell]=2.;state.mom_u[cell]=radius;
                state.mom_v[cell]=2.+z;state.mom_w[cell]=3.*radius;
                state.eng[cell]=100.;state.enuc_rate[cell]=.125;
                state.X(0,cell)=.6;state.X(1,cell)=.4;
            }
    }
    const auto plan=amr::make_coordinate_seam_plan(control.pool,active,2,
        amr::CoordinateSeamGeometry::RzAxisymmetric);
    if (inner_radius!=0.) {
        expect(plan.transfers.empty(),"Nonzero RZ inner boundary treated as axis");
        return;
    }
    expect(!plan.transfers.empty(),"RZ axis lacks ghost transfers");
    std::vector<amr::BlockHandle> handles;
    for (std::size_t index=0;index<active.size();++index)
        handles.push_back({{5000+index},{97}});
    auto& exchange=control.ghost_exchange;
    const auto rz=amr::CoordinateSeamGeometry::RzAxisymmetric;
    expect(exchange.GetPlans(control.pool,control.tree,2,handles)
        .coordinate_seam.transfers.empty(),"Default chart silently enabled RZ");
    expect(!exchange.GetPlans(control.pool,control.tree,2,handles,rz)
        .coordinate_seam.transfers.empty(),"Chart identity reused cached polar plan");
    const auto builds=exchange.PlanCacheBuilds();
    (void)exchange.GetPlans(control.pool,control.tree,2,handles,rz);
    expect(exchange.PlanCacheBuilds()==builds,"Unchanged RZ chart missed cache");
    for (int id:active) {
        auto& grid=control.pool->GetBlock(id).grid;
        grid.x1_min+=.25;grid.x1_max+=.25;
    }
    expect(exchange.GetPlans(control.pool,control.tree,2,handles,rz)
        .coordinate_seam.transfers.empty(),"Changed geometry reused axis plan");
    expect(exchange.PlanCacheBuilds()==builds+1,"Native bounds did not invalidate cache");
    for (int id:active) {
        auto& grid=control.pool->GetBlock(id).grid;
        grid.x1_min-=.25;grid.x1_max-=.25;
    }
    const auto& restored=exchange.GetPlans(control.pool,control.tree,2,handles,rz);
    amr::execute_coordinate_seam_plan(restored.coordinate_seam,control.pool,
        &amr::Block::fluid_state);
    expect(exchange.PlanCacheBuilds()==builds+2,"Restored axis retained shifted plan");
    expect(exchange.GetPlans(control.pool,control.tree,2,handles)
        .coordinate_seam.transfers.empty(),"Returning to polar retained RZ plan");
    std::set<int> levels;
    for (const auto& transfer:plan.transfers) {
        expect(transfer.geometry_semantics==GridMetrics::GeometrySemantics::AxisymmetricRz,
            "native RZ seam stencil lost its actual chart semantics");
        const auto& donor=control.pool->GetBlock(transfer.source_id);
        const auto& destination=control.pool->GetBlock(transfer.destination_id);
        // Mirrored active cell is in the same physical boundary block, even
        // on the mixed hierarchy. A half-turn lookup would cross z blocks.
        expect(transfer.source_id==transfer.destination_id,"RZ axis used angular donor");
        expect(transfer.momentum_sign==std::array<std::int8_t,3>{-1,1,-1},
            "RZ basis parity changed");
        levels.insert(destination.level);
        const int cell=transfer.destination_cell,source=transfer.source_center;
        const auto& a=destination.fluid_state;
        const auto& b=donor.fluid_state;
        expect(a.rho[cell]==b.rho[source] && a.eng[cell]==b.eng[source]
            && a.enuc_rate[cell]==b.enuc_rate[source]
            && a.X(0,cell)==b.X(0,source) && a.X(1,cell)==b.X(1,source),
            "RZ axis changed scalar/species parity");
        expect(a.mom_u[cell]==-b.mom_u[source]
            && a.mom_v[cell]==b.mom_v[source]
            && a.mom_w[cell]==-b.mom_w[source],"RZ axis momentum parity changed");
    }
    if (mixed) expect(levels==std::set<int>{0,1},"RZ mixed axis did not cover both levels");
    expect_rejected([&] {amr::make_coordinate_seam_plan(control.pool,active,3,
        amr::CoordinateSeamGeometry::RzAxisymmetric);},"RZ seam accepted 3D chart");
}

/** Real native axis plan using independent cold-spin V/W antiderivatives.
 * This verifies a pre-ghost transfer only: no density stencil inertia, EOS
 * acceptance token, or whole Runtime scientific qualification is fabricated.
 */
void test_rz_cold_coordinate_seam()
{
    static_assert(std::is_standard_layout_v<amr::CoordinateSeamTransfer>);
    static_assert(std::is_trivially_copyable_v<amr::CoordinateSeamTransfer>);
    constexpr auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    constexpr auto chart=amr::CoordinateSeamGeometry::RzAxisymmetric;
    constexpr long double internal=1.L/64.L;
    SimConfig config{};
    config.grid.dim=2;config.grid.geometry="cylindrical";
    config.grid.nblockx1=1;config.grid.nblockx2=1;config.grid.nblockx3=0;
    config.grid.x1_min=0.;config.grid.x1_max=amr::BLOCK_NX;
    config.grid.x2_min=-1.;config.grid.x2_max=1.;
    config.grid.x1l_boundary_type="reflecting";
    config.grid.amr_max_blocks=4;
    config.amr.lrefinemin=0;config.amr.lrefinemax=0;
    amr::AMRControl control(4,2);
    control.tree->InitRootGrid(config,2,rz);
    const auto& active=control.tree->GetActiveBlocks();
    for(int id:active) {
        auto& block=control.pool->GetBlock(id);
        const auto& grid=block.grid;
        for(int j=grid.Js();j<grid.Je();++j)for(int i=grid.Is();i<grid.Ie();++i) {
            const long double lo=grid.GetFacePosL(i),hi=grid.GetFacePosR(i);
            const long double volume=(hi*hi-lo*lo)/2.L;
            const long double angular=(hi*hi*hi-lo*lo*lo)/3.L;
            const long double inertia=(hi*hi*hi*hi-lo*lo*lo*lo)/4.L;
            const int cell=grid.GetIndex(i,j,0);
            block.fluid_state.set(cell,{1.,0.,0.,static_cast<double>(inertia/angular),
                static_cast<double>(internal+.5L*inertia/volume)});
            block.fluid_state.enuc_rate[cell]=.125;
            block.fluid_state.X(0,cell)=.25;block.fluid_state.X(1,cell)=.75;
        }
    }
    const auto plan=amr::make_coordinate_seam_plan(control.pool,active,2,chart);
    expect(!plan.transfers.empty(),"cold native RZ axis produced no actual seam operations");
    const auto first=plan.transfers.front();
    auto& state=control.pool->GetBlock(first.source_id).fluid_state;
    const auto native=state.get(first.source_center);
    expect(native.rho==1.&&native.mom_w==.75&&native.eng==17./64.
        &&arch::state::recover(native).status==arch::state::Status::unresolved_energy,
        "independent first-cell cold spin no longer witnesses raw point recovery failure");
    const auto before=state;
    amr::execute_coordinate_seam_plan(plan,control.pool,&amr::Block::fluid_state);
    for(const auto& transfer:plan.transfers) {
        expect(transfer.geometry_semantics==rz
            &&transfer.source_id==transfer.destination_id
            &&transfer.momentum_sign==std::array<std::int8_t,3>{-1,1,-1},
            "cold native axis changed its chart, actual donor, or basis parity");
        const auto& donor=control.pool->GetBlock(transfer.source_id).fluid_state;
        const auto& destination=control.pool->GetBlock(transfer.destination_id).fluid_state;
        const int source=transfer.source_center,cell=transfer.destination_cell;
        expect(destination.rho[cell]==donor.rho[source]
            &&destination.mom_u[cell]==-donor.mom_u[source]
            &&destination.mom_v[cell]==donor.mom_v[source]
            &&destination.mom_w[cell]==-donor.mom_w[source]
            &&destination.eng[cell]==donor.eng[source]
            &&destination.enuc_rate[cell]==donor.enuc_rate[source]
            &&destination.X(0,cell)==.25&&destination.X(1,cell)==.75,
            "cold native seam changed independent V/W means or strided species");
        expect(RzThermodynamics::provisional_native_state(destination.get(cell),
            destination.mass_fractions.data()+cell,2,destination.block_total_size_)
                ==arch::state::Status::valid,
            "cold native seam failed the shared provisional ghost check");
    }
    const auto& grid=control.pool->GetBlock(first.source_id).grid;
    for(int j=grid.Js();j<grid.Je();++j)for(int i=grid.Is();i<grid.Ie();++i) {
        const int cell=grid.GetIndex(i,j,0);
        expect(state.rho[cell]==before.rho[cell]&&state.mom_u[cell]==before.mom_u[cell]
            &&state.mom_v[cell]==before.mom_v[cell]&&state.mom_w[cell]==before.mom_w[cell]
            &&state.eng[cell]==before.eng[cell]&&state.enuc_rate[cell]==before.enuc_rate[cell]
            &&state.X(0,cell)==before.X(0,cell)&&state.X(1,cell)==before.X(1,cell),
            "native seam changed an active conservative donor");
    }

    // A deliberately negative interpolated rho must use the legal cold native
    // center, although generic point recovery of that center is unresolved.
    auto abrupt=first;abrupt.neighbor_weight={-.5,0.,0.};
    expect(abrupt.source_neighbor[0]!=abrupt.source_center,
        "native cold fallback lacks an actual radial donor neighbor");
    const double neighbor_rho=state.rho[abrupt.source_neighbor[0]];
    state.rho[abrupt.source_neighbor[0]]=100.;
    amr::CoordinateSeamPlan fallback;fallback.transfers.push_back(abrupt);
    amr::execute_coordinate_seam_plan(fallback,control.pool,&amr::Block::fluid_state);
    expect(state.rho[first.destination_cell]==1.&&state.mom_w[first.destination_cell]==-.75
        &&state.eng[first.destination_cell]==17./64.
        &&state.X(0,first.destination_cell)==.25&&state.X(1,first.destination_cell)==.75,
        "native cold seam fallback used a raw Cartesian energy veto");
    state.rho[abrupt.source_neighbor[0]]=neighbor_rho;

    expect_rejected([&] {amr::make_coordinate_seam_plan(control.pool,active,2,
        static_cast<amr::CoordinateSeamGeometry>(255));},
        "unknown coordinate seam chart selected a valid plan");
    auto unknown=first;
    unknown.geometry_semantics=static_cast<GridMetrics::GeometrySemantics>(255);
    amr::CoordinateSeamPlan unsupported;unsupported.transfers.push_back(unknown);
    const auto untouched=state;
    expect_rejected([&] {amr::execute_coordinate_seam_plan(unsupported,control.pool,
        &amr::Block::fluid_state);},"unknown seam semantics reached shared transfer execution");
    expect(state.rho==untouched.rho&&state.mom_u==untouched.mom_u
        &&state.mom_v==untouched.mom_v&&state.mom_w==untouched.mom_w
        &&state.eng==untouched.eng&&state.enuc_rate==untouched.enuc_rate
        &&state.mass_fractions==untouched.mass_fractions,
        "unknown seam semantics changed ghost scratch before rejection");

    const amr::CoordinateSeamFields<const double> donor{
        state.rho.data(),state.mom_u.data(),state.mom_v.data(),state.mom_w.data(),
        state.eng.data(),state.enuc_rate.data(),state.mass_fractions.data(),
        state.block_total_size_,state.GetNumSpecies()};
    const amr::CoordinateSeamFields<double> destination{
        state.rho.data(),state.mom_u.data(),state.mom_v.data(),state.mom_w.data(),
        state.eng.data(),state.enuc_rate.data(),state.mass_fractions.data(),
        state.block_total_size_,state.GetNumSpecies()};
    for(int bad=0;bad<4;++bad) {
        state.set(first.source_center,native);
        state.X(0,first.source_center)=.25;state.X(1,first.source_center)=.75;
        state.enuc_rate[first.source_center]=.125;
        if(bad==0)state.rho[first.source_center]=0.;
        if(bad==1)state.eng[first.source_center]=std::numeric_limits<double>::quiet_NaN();
        if(bad==2)state.X(1,first.source_center)=.25;
        if(bad==3)state.enuc_rate[first.source_center]=std::numeric_limits<double>::infinity();
        const auto retained=state.get(first.destination_cell);
        const double retained_enuc=state.enuc_rate[first.destination_cell];
        expect(!amr::apply_coordinate_seam_transfer(first,donor,destination),
            "native seam accepted an invalid donor before full Runtime acceptance");
        expect(state.rho[first.destination_cell]==retained.rho
            &&state.mom_u[first.destination_cell]==retained.mom_u
            &&state.mom_v[first.destination_cell]==retained.mom_v
            &&state.mom_w[first.destination_cell]==retained.mom_w
            &&state.eng[first.destination_cell]==retained.eng
            &&state.enuc_rate[first.destination_cell]==retained_enuc,
            "failed native seam wrote conservative fields from an invalid donor");
        // Candidate Xi may have been written to ghost scratch. No byte-atomic
        // whole-state or accepted-ghost publication guarantee is inferred.
    }
    state.set(first.source_center,native);
    state.X(0,first.source_center)=.25;state.X(1,first.source_center)=.75;
    state.enuc_rate[first.source_center]=.125;
    std::cout<<"RZ_COLD_SEAM_PROVISIONAL transfers="<<plan.transfers.size()
        <<" raw_point_unresolved=true shared_host_device_math=true runtime_eos_qualification=false\n";
}

void test_coordinate_seam_mapping()
{
    // Ordinary curved Hydro may use a partial wedge with physical side
    // boundaries. The new chart plan must not reject that existing topology.
    SimConfig wedge{};
    wedge.grid.dim = 2;
    wedge.grid.geometry = "cylindrical";
    wedge.grid.nblockx1 = 1;
    wedge.grid.nblockx2 = 1;
    wedge.grid.nblockx3 = 0;
    wedge.grid.x1_min = 0.;
    wedge.grid.x1_max = 1.;
    wedge.grid.x2_min = 0.;
    wedge.grid.x2_max = std::acos(-1.);
    wedge.grid.x1l_boundary_type = "reflecting";
    wedge.grid.x2l_boundary_type = "outflow";
    wedge.grid.x2r_boundary_type = "outflow";
    wedge.grid.amr_max_blocks = 4;
    amr::AMRControl wedge_control(4, 2);
    wedge_control.tree->InitRootGrid(wedge, 1);
    const std::vector<amr::BlockHandle> wedge_handles{{{7}, {3}}};
    expect(wedge_control.ghost_exchange.GetPlans(wedge_control.pool,
            wedge_control.tree, 2, wedge_handles).coordinate_seam.transfers.empty(),
        "partial-azimuth Hydro unexpectedly requires a coordinate seam");
    test_coordinate_seam_case(2, false, false);
    test_coordinate_seam_case(2, false, true);
    test_coordinate_seam_case(2, true, false);
    test_coordinate_seam_case(2, true, true);
    test_coordinate_seam_case(3, false, false);
    test_coordinate_seam_case(3, false, true);
    test_coordinate_seam_case(3, true, false);
    test_coordinate_seam_case(3, true, true);
    test_rz_cold_coordinate_seam();
}


/** Actual paired-periodic coarse/fine Host plan on sparse, genuine native
 * Blocks. Two borrowed active Blocks and one independent alias geometry are
 * sufficient: this is a transfer-owner test, not a full hierarchy/Runtime BC
 * qualification. Every logical source halo has positive analytic rho/e.
 */
void test_rz_periodic_actual_coarse_fine_alias()
{
    constexpr auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    constexpr auto chart=amr::CoordinateSeamGeometry::RzAxisymmetric;
    const arch::state::Bounds bounds{1.e-14,1.e-14,1.e6};
    SpeciesManager species;species.add_species("a",1.,1.,1.4,1.);
    species.add_species("b",1.,1.,1.4,1.);IdealGas eos(1.4,species);
    const auto same_state=[](const FluidState& a,const FluidState& b) {
        return a.rho==b.rho&&a.mom_u==b.mom_u&&a.mom_v==b.mom_v
            &&a.mom_w==b.mom_w&&a.eng==b.eng&&a.enuc_rate==b.enuc_rate
            &&a.mass_fractions==b.mass_fractions;
    };
    const auto same_bits=[](double a,double b) {
        return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);
    };
    std::size_t routes=0,cells=0;
    for(int level:{10,14})for(double omega:{0.,1.})for(bool lower:{false,true}) {
        SimConfig config{};config.grid.dim=2;config.grid.geometry="cylindrical";
        config.grid.nblockx1=1;config.grid.nblockx2=1;config.grid.nblockx3=0;
        config.grid.x1_min=.1;config.grid.x1_max=1.3;
        config.grid.x2_min=-.3;config.grid.x2_max=.8;
        config.grid.x2l_boundary_type="periodic";config.grid.x2r_boundary_type="periodic";
        config.grid.amr_max_blocks=4;config.amr.lrefinemin=0;config.amr.lrefinemax=level+1;
        amr::AMRControl control(4,2);
        const std::uint32_t coarse_z=lower?(std::uint32_t{1}<<level)-1:0;
        const std::uint32_t fine_z=lower?0:(std::uint32_t{1}<<(level+1))-1;
        control.tree->LoadLeafGrid(config,2,{level,level+1},{0,0},{coarse_z,fine_z},{0,0},rz);
        const auto& active=control.tree->GetActiveBlocks();
        expect(active.size()==2,"periodic transfer fixture allocated a large hierarchy");
        std::vector<amr::BlockHandle> handles;int coarse_id=-1,fine_id=-1;
        for(std::size_t n=0;n<active.size();++n) {
            auto& block=control.pool->GetBlock(active[n]);handles.push_back({{7000+n},{9}});
            rz_canonical_fill_uniform(block,omega);
            (block.level==level?coarse_id:fine_id)=active[n];
        }
        expect(coarse_id>=0&&fine_id>=0,"periodic actual source/destination missing");
        auto& coarse=control.pool->GetBlock(coarse_id);auto& fine=control.pool->GetBlock(fine_id);
        const auto& cg=coarse.grid;const auto& fg=fine.grid;
        expect(cg.dyadic_identity.periodic_axial&&fg.dyadic_identity.periodic_axial,
            "paired real config did not authorize actual native geometry");
        amr::Block alias{};
        rz_canonical_actual_block(alias,control.tree->GetRootGrid(),level+1,0,
            lower?(std::uint32_t{1}<<(level+1))-1:0,3);
        const int source_j=lower?amr::BLOCK_NY-1:0;
        const int destination_j=lower?-2:amr::BLOCK_NY;
        const int alias_j=lower?amr::BLOCK_NY-2:0;
        expect(same_bits(lower?cg.x2_max:fg.x2_max,config.grid.x2_max),
            "periodic cell aliases wrapped a real upper Block descriptor");
        const auto endpoint_for=[&](const amr::Block& block) {
            return amr::AmrEndpoint{{2,block.level,block.logical_x1,block.logical_x2,block.logical_x3},
                handles[block.active_index]};
        };
        amr::CoarseFineTransferPlan plan{};plan.dimension=2;plan.scope={0,{9},{9}};
        for(int field=0;field<8;++field)plan.operations.push_back({0,
            endpoint_for(coarse),endpoint_for(fine),{{2,source_j,0},{1,1,1}},
            {{4,destination_j,0},{2,2,1}},amr::AmrAxis::Y,
            lower?amr::AmrSide::Lower:amr::AmrSide::Upper,
            field<6?static_cast<amr::AmrField>(field):amr::AmrField::Species,
            field<6?-1:field-6,amr::RefinementRule::CoarseGhostInjection,1.,1.});
        amr::finalize_amr_plan(plan);
        const auto lowered=amr::compile_coarse_fine_cell_plan(plan,2);
        expect(lowered.transfers.size()==4,"periodic plan skipped real four-member family");
        const FluidState source_before=coarse.fluid_state;
        const auto source=cg.GetIndex(cg.Is()+2,cg.Js()+source_j,0);
        const auto parent_measure=rz_canonical_true_measures(cg,cg.Is()+2,cg.Js()+source_j);
        const auto fv=GridMetrics::make_geometry_view(fg,rz);
        const auto av=GridMetrics::make_geometry_view(alias.grid,rz);
        // Compare with the actual in-domain fine Block, not a independently
        // rounded extended-coordinate subtraction or manufactured exact face.
        for(int n=0;n<4;++n) {
            const int i=fg.Is()+4+(n&1),j=fg.Js()+destination_j+((n>>1)&1);
            const int ai=alias.grid.Is()+4+(n&1),aj=alias.grid.Js()+alias_j+((n>>1)&1);
            expect(same_bits(fg.GetAxialFacePosL(j),alias.grid.GetAxialFacePosL(aj))
                &&same_bits(fg.GetAxialFacePosR(j),alias.grid.GetAxialFacePosR(aj))
                &&same_bits(fv.GetAxialFacePosL(j),av.GetAxialFacePosL(aj))
                &&same_bits(fv.GetAxialFacePosR(j),av.GetAxialFacePosR(aj))
                &&same_bits(fg.CellWidth(1,j),alias.grid.CellWidth(1,aj))
                &&same_bits(GridMetrics::CellVolume(fv,i,j,0),GridMetrics::CellVolume(av,ai,aj,0))
                &&same_bits(GridMetrics::Rz::AngularMomentumMeasure(fv,i,j),
                    GridMetrics::Rz::AngularMomentumMeasure(av,ai,aj)),
                "real periodic ghost endpoints/height/V/W do not share actual domain alias");
        }
        control.ghost_exchange.ExecuteCoarseFinePlan(plan,control.pool,control.tree,2,
            &amr::Block::fluid_state,handles,chart,bounds);
        std::array<long double,7> sum{};
        for(int n=0;n<4;++n) {
            const int i=fg.Is()+4+(n&1),j=fg.Js()+destination_j+((n>>1)&1);
            const int index=fg.GetIndex(i,j,0);const auto u=fine.fluid_state.get(index);
            const auto m=rz_canonical_true_measures(fg,i,j);
            sum[0]+=m[0];sum[1]+=m[1];sum[2]+=u.rho*m[0];sum[3]+=u.eng*m[0];
            sum[4]+=u.mom_w*m[1];sum[5]+=u.rho*fine.fluid_state.X(0,index)*m[0];
            sum[6]+=u.rho*fine.fluid_state.X(1,index)*m[0];
            const auto closure=RzThermodynamics::make_cell([&](int cell){return fine.fluid_state.get(cell);},
                index,fv,i,bounds);
            expect(closure.valid(),"actual periodic transferred cell closure rejected");
            const double x[]{fine.fluid_state.X(0,index),fine.fluid_state.X(1,index)};
            for(int q=0;q<RzThermodynamics::physical_node_count;++q)
                expect(arch::state::validate_eos(RzThermodynamics::base_point(closure,
                    RzThermodynamics::physical_node_radius(closure,q)),x,2,bounds,eos)==arch::state::Status::valid,
                    "actual periodic ghost physical point failed true IdealGas");
            ++cells;
        }
        const auto parent=coarse.fluid_state.get(source);
        rz_canonical_integral_equal(sum[0],parent_measure[0],"periodic V partition");
        rz_canonical_integral_equal(sum[1],parent_measure[1],"periodic W partition");
        rz_canonical_integral_equal(sum[2],parent.rho*parent_measure[0],"periodic native mass");
        rz_canonical_integral_equal(sum[3],parent.eng*parent_measure[0],"periodic native E");
        if(omega!=0.)rz_canonical_integral_equal(sum[4],parent.mom_w*parent_measure[1],"periodic native J");
        else expect(sum[4]==0.,"periodic zero J changed");
        rz_canonical_integral_equal(sum[5],parent.rho*.3L*parent_measure[0],"periodic species a");
        rz_canonical_integral_equal(sum[6],parent.rho*.7L*parent_measure[0],"periodic species b");
        expect(same_state(coarse.fluid_state,source_before),"actual periodic plan mutated source arrays");
        // Unauthorized image/root provenance must reject the complete gather
        // before writing any destination component, including species/ENUC.
        const FluidState destination_before=fine.fluid_state;
        for(int fault=0;fault<2;++fault) {
            const auto coarse_identity=cg.dyadic_identity,fine_identity=fg.dyadic_identity;
            if(fault==0) {
                coarse.grid.dyadic_identity.periodic_axial=false;
                fine.grid.dyadic_identity.periodic_axial=false;
            } else coarse.grid.dyadic_identity.root_upper[1]=std::nextafter(config.grid.x2_max,
                std::numeric_limits<double>::infinity());
            bool rejected=false;
            try {control.ghost_exchange.ExecuteCoarseFinePlan(plan,control.pool,control.tree,2,
                &amr::Block::fluid_state,handles,chart,bounds);}
            catch(const std::invalid_argument&){rejected=true;}
            catch(const std::runtime_error&){rejected=true;}
            coarse.grid.dyadic_identity=coarse_identity;fine.grid.dyadic_identity=fine_identity;
            expect(rejected,"unauthorized periodic/root mapping accepted");
            expect(same_state(fine.fluid_state,destination_before)&&same_state(coarse.fluid_state,source_before),
                "rejected periodic/root mapping wrote arrays before failure");
        }
        ++routes;
    }
    expect(routes==8&&cells==32,"periodic test skipped direction/level/rotation");
    std::cout<<"RZ_ACTUAL_PERIODIC_COARSE_FINE_ALIAS_PASS routes="<<routes<<" cells="<<cells
        <<" runtime_bc_qualification=false\n";
}


/**
 * Workflow: load a genuine root-bound mixed-level T junction -> seed physical
 * native V/W means and finite diagnostic halos -> retain the old public
 * same-level/CF ordering counterexample -> reset -> execute the real exchange
 * owner -> compare every same-level axial halo against the actual neighbour.
 *
 * The reference is an exact field-copy identity, U_dst(i,jghost)=U_src(i,jactive),
 * including radial ghost columns supplied by the real coarse/fine producer.
 * No exchange-plan operation supplies the expected neighbour or cell mapping.
 * This verifies exchange ordering, not Runtime/EOS or whole RZ evolution.
 */
void test_rz_native_tjunction_corner_sync()
{
    constexpr auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    constexpr auto chart=amr::CoordinateSeamGeometry::RzAxisymmetric;
    const arch::state::Bounds bounds{1.e-14,1.e-14,1.e6};
    SimConfig config{};
    config.grid.dim=2;config.grid.geometry="cylindrical";
    config.grid.nblockx1=2;config.grid.nblockx2=1;config.grid.nblockx3=0;
    config.grid.x1_min=1.;config.grid.x1_max=3.;
    config.grid.x2_min=0.;config.grid.x2_max=1.;
    config.amr.lrefinemin=0;config.amr.lrefinemax=1;
    amr::AMRControl control(16,2);
    control.tree->LoadLeafGrid(config,2,{1,1,1,1,0},
        {0,1,0,1,1},{0,0,1,1,0},{0,0,0,0,0},rz);
    const auto& active=control.tree->GetActiveBlocks();
    expect(active.size()==5,"native corner fixture lost its real five-leaf T junction");
    std::vector<amr::BlockHandle> handles;
    std::vector<FluidState> before;
    for(std::size_t b=0;b<active.size();++b) {
        auto& block=control.pool->GetBlock(active[b]);
        block.RequireNativeGeometryIdentity();
        rz_canonical_fill_uniform(block,1.);
        auto& u=block.fluid_state;const auto& g=block.grid;
        // Only the diagnostic scalar distinguishes stale radial halos. Every
        // evolved native mean and species remains physically admissible.
        for(int j=0;j<g.GetTotalY();++j)for(int i=0;i<g.GetTotalX();++i)
            if(i<g.Is()||i>=g.Ie())u.enuc_rate[g.GetIndex(i,j,0)]=10.+active[b];
        handles.push_back({{91000+b},{143}});
        before.push_back(u);
    }
    const auto bits=[](double value){return std::bit_cast<std::uint64_t>(value);};
    const auto active_position=[&](int id) {
        for(std::size_t b=0;b<active.size();++b)if(active[b]==id)return b;
        throw std::runtime_error("actual same-level neighbour is not an active leaf");
    };
    // Independent neighbour/coordinate oracle. J_s is an active donor row;
    // the entire radial extent is copied, including its newly completed CF halo.
    const auto count_mismatches=[&](bool strict) {
        std::size_t mismatches=0,compared=0,radial_columns=0;
        for(int id:active) {
            const auto& dst=control.pool->GetBlock(id);const auto& dg=dst.grid;
            for(int face:{2,3}) {
                const auto& n=dst.face_neighbors[face];
                if(n.count!=1||n.level_diff!=0)continue;
                const auto& src=control.pool->GetBlock(n.ids[0]);const auto& sg=src.grid;
                expect(dst.level==src.level&&dst.logical_x1==src.logical_x1
                    &&dst.logical_x3==src.logical_x3,"same-level Y neighbour has a foreign radial chart");
                expect(bits(dg.x1_min)==bits(sg.x1_min)&&bits(dg.x1_max)==bits(sg.x1_max)
                    &&dg.GetTotalX()==sg.GetTotalX()&&dg.ng==sg.ng,
                    "same-level Y neighbour radial coordinates are not aligned");
                expect(face==2?dst.logical_x2==src.logical_x2+1:src.logical_x2==dst.logical_x2+1,
                    "actual neighbour IDs do not match the independent axial cell mapping");
                for(int depth=1;depth<=dg.ng;++depth) {
                    const int jd=face==2?dg.Js()-depth:dg.Je()+depth-1;
                    const int js=face==2?sg.Je()-depth:sg.Js()+depth-1;
                    for(int i=0;i<dg.GetTotalX();++i) {
                        const int di=dg.GetIndex(i,jd,0),si=sg.GetIndex(i,js,0);
                        const auto& d=dst.fluid_state;const auto& s=src.fluid_state;
                        bool equal=true;
                        for(auto field:{&FluidState::rho,&FluidState::mom_u,&FluidState::mom_v,
                            &FluidState::mom_w,&FluidState::eng,&FluidState::enuc_rate})
                            equal=equal&&bits((d.*field)[di])==bits((s.*field)[si]);
                        for(int species=0;species<d.GetNumSpecies();++species) {
                            equal=equal&&bits(d.X(species,di))==bits(s.X(species,si));
                            equal=equal&&bits(d.rho[di]*d.X(species,di))
                                ==bits(s.rho[si]*s.X(species,si));
                        }
                        if(!equal)++mismatches;
                        ++compared;if(i<dg.Is()||i>=dg.Ie())++radial_columns;
                    }
                }
            }
        }
        expect(compared>0&&radial_columns>0,"native corner oracle did not visit radial halo columns");
        if(strict)expect(mismatches==0,"native same-level Y halo is stale after actual coarse/fine exchange");
        return mismatches;
    };
    const auto& plans=control.ghost_exchange.GetPlans(control.pool,control.tree,2,handles,chart);
    // Retain an executable old-order counterexample using the existing public
    // producer APIs. No field-copy implementation is duplicated by the test.
    for(const auto& plan:plans.same_level) {
        std::vector<amr::HostExchangeBlockView> views;
        for(const auto& endpoint:plan.blocks) {
            std::size_t b=0;while(b<handles.size()&&handles[b]!=endpoint.handle)++b;
            expect(b<handles.size(),"public old-order plan lost a committed test handle");
            auto& block=control.pool->GetBlock(active[b]);auto& u=block.fluid_state;const auto& g=block.grid;
            amr::HostExchangeBlockView v{};
            v.logical={2,block.level,block.logical_x1,block.logical_x2,block.logical_x3};v.handle=handles[b];
            v.layout={2,{g.Is(),g.Js(),g.Ks()},{g.GetTotalX(),g.GetTotalY(),g.GetTotalZ()},
                {1,g.stride_y,g.stride_z},g.GetTotalSize()};
            v.conserved={u.rho.data(),u.mom_u.data(),u.mom_v.data(),u.mom_w.data(),u.eng.data(),u.enuc_rate.data()};
            v.species=u.mass_fractions.data();v.species_count=u.GetNumSpecies();v.species_stride=g.GetTotalSize();
            views.push_back(v);
        }
        const auto compiled=amr::compile_host_exchange_plan(plan,views);
        amr::execute_host_exchange_plan(compiled,views);
    }
    control.ghost_exchange.ExecuteCoarseFinePlan(plans.coarse_fine,control.pool,control.tree,2,
        &amr::Block::fluid_state,handles,chart,bounds);
    const auto old_mismatches=count_mismatches(false);
    expect(old_mismatches>0,"native T junction did not reproduce the old ordering counterexample");
    // Reset only this test's arrays in place; this is fixture reset, not a claim
    // about numerical Runtime rollback or acceptance of the legacy ordering.
    for(std::size_t b=0;b<active.size();++b) {
        auto& u=control.pool->GetBlock(active[b]).fluid_state;
        for(auto field:{&FluidState::rho,&FluidState::mom_u,&FluidState::mom_v,
            &FluidState::mom_w,&FluidState::eng,&FluidState::enuc_rate,&FluidState::mass_fractions})
            std::copy((before[b].*field).begin(),(before[b].*field).end(),(u.*field).begin());
    }
    control.ghost_exchange.ExecuteExchange(control.pool,control.tree,2,
        &amr::Block::fluid_state,handles,chart,bounds);
    count_mismatches(true);
    std::size_t completed_cf_cells=0,active_cells=0;
    for(std::size_t b=0;b<active.size();++b) {
        const auto& block=control.pool->GetBlock(active[b]);const auto& g=block.grid;const auto& u=block.fluid_state;
        for(int j=g.Js();j<g.Je();++j)for(int i=g.Is();i<g.Ie();++i) {
            const int cell=g.GetIndex(i,j,0);
            for(auto field:{&FluidState::rho,&FluidState::mom_u,&FluidState::mom_v,
                &FluidState::mom_w,&FluidState::eng,&FluidState::enuc_rate})
                expect(bits((u.*field)[cell])==bits((before[b].*field)[cell]),
                    "native corner synchronization mutated an active interior scalar");
            for(int species=0;species<u.GetNumSpecies();++species)
                expect(bits(u.X(species,cell))==bits(before[b].X(species,cell)),
                    "native corner synchronization mutated an active interior species");
            ++active_cells;
        }
        for(int face:{0,1})if(block.face_neighbors[face].count>0
            &&block.face_neighbors[face].level_diff==-1) {
            for(int depth=1;depth<=g.ng;++depth)for(int j=g.Js();j<g.Je();++j) {
                const int i=face==0?g.Is()-depth:g.Ie()+depth-1,cell=g.GetIndex(i,j,0);
                expect(std::isfinite(before[b].enuc_rate[cell])&&std::isfinite(u.enuc_rate[cell]),
                    "native corner stale/CF diagnostic value is not finite");
                if(bits(u.enuc_rate[cell])!=bits(before[b].enuc_rate[cell]))++completed_cf_cells;
            }
        }
    }
    expect(completed_cf_cells>0&&active_cells==5u*16u*16u,
        "native T junction did not exercise a real overwritten CF radial halo or all active cells");
    std::cout<<"RZ_NATIVE_TJUNCTION_CORNER_SYNC_PASS old_order_mismatches="<<old_mismatches
        <<" completed_cf_radial_cells="<<completed_cf_cells<<" active_cells="<<active_cells
        <<" runtime_science_qualification=false\n";
}

} // namespace

int main()
{
    try {
        test_public_contract();
        test_validation();
        test_conservative_restriction_math();
        test_limited_linear_prolongation_math();
        test_shared_composition_prolongation();
        test_muscl_face_composition_closure();
        test_amr_interface_stencil_predicate();
        test_multidimensional_cell_lowering();
        test_curvilinear_host_restriction();
        test_host_exchange_cache_rebinding();
        test_mixed_level_and_coarse_fine_execution();
        test_coordinate_seam_mapping();
        test_rz_coarse_fine_ghost_angular();
    test_rz_rigid_rotation_transfer();
    test_rz_candidate_parent_veto();
    test_rz_angular_restriction_counterexample();
    test_rz_cold_block_family_roundtrip();
    test_rz_thirteen_species_family_simplex();
    test_rz_native_family_representability();
    test_rz_regrid_roundtrip();
    test_rz_canonical_deep_actual_block_families();
    test_rz_periodic_actual_coarse_fine_alias();
    test_rz_native_tjunction_corner_sync();
        test_rz_axis_seam(false,0.);
        test_rz_axis_seam(true,0.);
        test_rz_axis_seam(false,.25);
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
