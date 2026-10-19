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
#include "amr/transfer/RegridTransferMath.h"
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

    // Test-only nondimensional carrier: a unit specific-energy conversion
    // adds Frho to FE. AMR must leave every other flux unchanged and apply its
    // original stage weight exactly once; this owner has no gravity dependency.
    const double receipt_weight = 0.5;
    const double receipt_energy = receipt_weight
        * (field_values[4] + field_values[0]);
    struct ToyEnergyReceipt {
        amr::FluxRegister& target;
        int coarse;
        double expected_energy;
        bool nonfinite = false;
        int reads = 0;
        int commits = 0;

        double registered_energy(const amr::AmrFluxTopologyPlan&,
            const amr::AmrFluxRegistrationRoute& route,
            std::size_t operation, int, double FE, double Frho, double)
        {
            expect(route.plan.operations.at(operation).field == amr::AmrField::Energy,
                   "generic receipt was called for a non-Energy field");
            expect(!target.HasData(coarse, 0),
                   "registration wrote before the complete Energy gather");
            ++reads;
            return nonfinite ? std::numeric_limits<double>::quiet_NaN()
                             : FE + Frho;
        }
        void commit_registration(const amr::AmrFluxRegistrationRoute&)
        {
            expect(target.HasData(coarse, 0)
                       && target.GetSummedFlux(coarse, 0, 0).eng == expected_energy,
                   "generic receipt committed before successful Apply");
            ++commits;
        }
    } receipt{control.flux_register, coarse_id, receipt_energy};
    const auto expect_flux = [&](double energy) {
        const auto actual = control.flux_register.GetSummedFlux(coarse_id, 0, 0);
        expect(actual.rho == receipt_weight * field_values[0]
                   && actual.mom_u == receipt_weight * field_values[1]
                   && actual.mom_v == receipt_weight * field_values[2]
                   && actual.mom_w == receipt_weight * field_values[3]
                   && actual.eng == energy
                   && control.flux_register.GetSummedSpeciesFlux(coarse_id, 0, 0, 0)
                       == receipt_weight * field_values[5]
                   && control.flux_register.GetSummedSpeciesFlux(coarse_id, 0, 0, 1)
                       == receipt_weight * field_values[6],
               "generic Energy receipt changed a non-Energy flux or repeated its weight");
    };
    const auto expect_empty = [&] {
        for (int id : active) for (int face = 0; face < 2; ++face) {
            const auto actual = control.flux_register.GetSummedFlux(id, face, 0);
            expect(!control.flux_register.HasData(id, face)
                       && actual.rho == 0. && actual.mom_u == 0.
                       && actual.mom_v == 0. && actual.mom_w == 0. && actual.eng == 0.
                       && control.flux_register.GetSummedSpeciesFlux(id, face, 0, 0) == 0.
                       && control.flux_register.GetSummedSpeciesFlux(id, face, 0, 1) == 0.,
                   "rejected or unconsumed receipt partially wrote an AMR register");
        }
    };
    control.flux_register.Clear();
    amr::RegisterCoarseFineFluxes(control, fine_id, fine_after.grid, 0,
        hydro_flux, species_flux, 2, receipt_weight,
        &receipt);
    expect(receipt.reads == 1 && receipt.commits == 1,
           "typed Energy receipt was not consumed exactly once");
    expect_flux(receipt_energy);

    control.flux_register.Clear();
    receipt.nonfinite = true;
    receipt.reads = receipt.commits = 0;
    expect_rejected([&] {
        amr::RegisterCoarseFineFluxes(control, fine_id, fine_after.grid, 0,
            hydro_flux, species_flux, 2, receipt_weight,
            &receipt);
    }, "nonfinite generic Energy reached an accepted Apply");
    expect(receipt.reads == 1 && receipt.commits == 0,
           "rejected Energy receipt committed or skipped its gather");
    expect_empty();

    receipt.nonfinite = false;
    receipt.reads = 0;
    amr::RegisterCoarseFineFluxes(control, fine_id, fine_after.grid, 0,
        hydro_flux, species_flux, 2, 0.,
        &receipt);
    expect(receipt.reads == 0 && receipt.commits == 0,
           "zero-weight route consumed a generic receipt");
    expect_empty();

    // Use the actual active left fine block: only its same-level neighbour
    // exists, so the authentic topology has no coarse-fine route on this axis.
    const auto& flux_topology = control.RequireFluxTopologyPlan(2);
    int no_route_id = -1;
    for (int id : active) if (!flux_topology.find(id, 0)) no_route_id = id;
    expect(no_route_id >= 0, "mixed fixture lost its genuine no-route block");
    amr::RegisterCoarseFineFluxes(control, flux_topology, no_route_id,
        pool->GetBlock(no_route_id).grid, 0, hydro_flux, species_flux,
        2, receipt_weight, &receipt);
    expect(receipt.reads == 0 && receipt.commits == 0,
           "genuine absent route consumed a generic receipt");
    expect_empty();

    for (int null_path = 0; null_path < 3; ++null_path) {
        control.flux_register.Clear();
        if (null_path == 0)
            amr::RegisterCoarseFineFluxes(control, fine_id, fine_after.grid, 0,
                hydro_flux, species_flux, 2, receipt_weight);
        else if (null_path == 1)
            amr::RegisterCoarseFineFluxes(control, fine_id, fine_after.grid, 0,
                hydro_flux, species_flux, 2, receipt_weight,
                nullptr);
        else {
            ToyEnergyReceipt* typed_null = nullptr;
            amr::RegisterCoarseFineFluxes(control, fine_id, fine_after.grid, 0,
                hydro_flux, species_flux, 2, receipt_weight,
                typed_null);
        }
        expect_flux(receipt_weight * field_values[4]);
    }
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
    expect(!plan.transfers.empty(), "retained singular geometry generated no seam ghosts: geometry="
        + config.grid.geometry + " dimension=" + std::to_string(dimension)
        + " mixed=" + std::to_string(mixed));
    control.ghost_exchange.ExecuteExchange(
        pool, tree, dimension, &amr::Block::fluid_state, handles);
    double largest_momentum_error = 0.;
    bool crossed_block = false;
    bool crossed_level = false;
    bool saw_origin = false, saw_north = false, saw_south = false;
    for (const auto& transfer : plan.transfers) {
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






/** Exercise the ordinary donor plan on axisymmetric native block geometry.
 * The same package also initializes the ordinary AMR root natively; this
 * donor check independently prescribes the odd radial/azimuthal and even
 * axial continuation on both geometry levels.
 */
void test_axisymmetric_coordinate_seam(bool mixed)
{
    Grid root(amr::MAX_NG,0.,1.,-4.,6.,0.,0.,1,2,0);
    root.dim=2;root.geometry="cylindrical";root.InitializeTopology();
    auto pool=std::make_shared<amr::MemoryPool>(8,2);
    std::vector<int> active;
    const auto append=[&](int level,int radial,int axial) {
        const int id=pool->AllocateBlock();active.push_back(id);
        auto& block=pool->GetBlock(id);
        block.level=level;block.logical_x1=radial;block.logical_x2=axial;
        block.logical_x3=0;
        block.morton_code=amr::encodeMorton(level,radial,axial,0);
        block.InitGeometry(root,1./amr::BLOCK_NX,10./(2*amr::BLOCK_NY),0.);
        block.RequireLogicalGeometryIdentity(root);
        block.fluid_state.InitSpecies(2);
        const Grid& grid=block.grid;
        for(int j=grid.Js();j<grid.Je();++j)for(int i=grid.Is();i<grid.Ie();++i) {
            const int cell=grid.GetIndex(i,j,0);
            block.fluid_state.rho[cell]=2.;
            block.fluid_state.mom_u[cell]=.1*grid.GetCellCenterX(i);
            block.fluid_state.mom_v[cell]=.3+.01*grid.GetCellCenterY(j);
            block.fluid_state.mom_w[cell]=.2*grid.GetCellCenterX(i);
            block.fluid_state.eng[cell]=10.;
            block.fluid_state.enuc_rate[cell]=.125;
            block.fluid_state.X(0,cell)=.6;block.fluid_state.X(1,cell)=.4;
        }
    };
    if(mixed)for(int j=0;j<2;++j)for(int i=0;i<2;++i)append(1,i,j);
    else append(0,0,0);
    append(0,0,1);
    const auto plan=amr::make_coordinate_seam_plan(pool,active,2);
    expect(!plan.transfers.empty(),"axisymmetric radial seam missing on non-angular z domain");
    amr::execute_coordinate_seam_plan(plan,pool,&amr::Block::fluid_state);
    std::set<int> levels;
    for(const auto& transfer:plan.transfers) {
        const auto& block=pool->GetBlock(transfer.destination_id);
        const auto& donor=pool->GetBlock(transfer.source_id);
        const auto& grid=block.grid;
        const int j=transfer.destination_cell/grid.stride_y;
        const int i=transfer.destination_cell-j*grid.stride_y;
        const int source_j=transfer.source_center/donor.grid.stride_y;
        const double r=grid.GetCellCenterX(i),z=grid.GetCellCenterY(j);
        expect(r<0. && transfer.source_id==transfer.destination_id &&
            donor.grid.GetCellCenterY(source_j)==z &&
            transfer.momentum_sign==std::array<std::int8_t,3>{-1,1,-1},
            "axisymmetric donor must mirror r without moving/wrapping z");
        const int cell=transfer.destination_cell;
        expect(std::abs(block.fluid_state.mom_u[cell]-.1*r)<2e-12 &&
            std::abs(block.fluid_state.mom_v[cell]-(.3+.01*z))<2e-12 &&
            std::abs(block.fluid_state.mom_w[cell]-.2*r)<2e-12,
            "axisymmetric three-component parity");
        expect(block.fluid_state.rho[cell]==2. && block.fluid_state.eng[cell]==10. &&
            block.fluid_state.enuc_rate[cell]==.125 && block.fluid_state.X(0,cell)==.6 &&
            block.fluid_state.X(1,cell)==.4,"axisymmetric even scalar/composition continuation");
        levels.insert(block.level);
    }
    expect(levels.size()==(mixed?2:1),"axisymmetric fixture lost a native geometry level");
    SimConfig config{};
    config.grid.geometry="cylindrical";config.grid.dim=2;
    config.grid.nblockx1=1;config.grid.nblockx2=1;config.grid.nblockx3=0;
    config.grid.x1_min=0.;config.grid.x1_max=1.;
    config.grid.x2_min=-4.;config.grid.x2_max=6.;
    config.grid.amr_max_blocks=8;
    config.amr.lrefinemin=0;config.amr.lrefinemax=0;
    amr::AMRControl qualified(8,2);
    qualified.tree->InitRootGrid(config,2);
    expect(!qualified.tree->GetActiveBlocks().empty(),
        "native axisymmetric InitRootGrid did not publish the root block");
    std::cout<<"AXISYMMETRIC_SEAM mixed="<<mixed<<" transfers="<<plan.transfers.size()<<" PASS\n";
}

void test_coordinate_seam_mapping()
{
    // Spherical 2D polar Hydro may use a partial wedge with physical side
    // boundaries.
    SimConfig wedge{};
    wedge.grid.dim = 2;
    wedge.grid.geometry = "spherical";
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
    test_coordinate_seam_case(2, true, false);
    test_coordinate_seam_case(2, true, true);
    test_coordinate_seam_case(3, false, false);
    test_coordinate_seam_case(3, false, true);
    test_coordinate_seam_case(3, true, false);
    test_coordinate_seam_case(3, true, true);
    test_axisymmetric_coordinate_seam(false);
    test_axisymmetric_coordinate_seam(true);
}


/** Exact actual Block ordinary root geometry, including inactive dimensions.
 * No boundary flags/Tree authority are granted here. Expected root spacings
 * follow the existing InitRootGrid inputs; one-ULP drift must be rejected.
 */
void test_ordinary_logical_geometry_identity() {
    for(int dim=1;dim<=3;++dim)for(const char* chart:{"cartesian","cylindrical","spherical"}) {
        // Native block identity does not grant Tree/transfer qualification.
        Grid root(amr::MAX_NG,1.,4.,.2,1.2,.1,1.1,3,dim>=2?2:0,dim==3?2:0);
        root.dim=dim;root.geometry=chart;
        amr::Block block;block.Reset();block.level=2;
        block.logical_x1=7;block.logical_x2=dim>=2?3:0;block.logical_x3=dim==3?4:0;
        block.morton_code=amr::encodeMorton(block.level,block.logical_x1,block.logical_x2,block.logical_x3);
        block.InitGeometry(root,(root.x1_max-root.x1_min)/(root.nblockx1*amr::BLOCK_NX),
            root.nblockx2>0?(root.x2_max-root.x2_min)/(root.nblockx2*amr::BLOCK_NY):0.,
            root.nblockx3>0?(root.x3_max-root.x3_min)/(root.nblockx3*amr::BLOCK_NZ):0.);
        block.RequireLogicalGeometryIdentity(root);
        const auto rejects=[&](const amr::Block& candidate,const Grid& candidate_root) {
            bool rejected=false;
            try {candidate.RequireLogicalGeometryIdentity(candidate_root);}
            catch(const std::invalid_argument&) {rejected=true;}
            expect(rejected,"ordinary geometry drift/foreign logical root must reject exactly");
        };
        auto candidate=block;candidate.grid.x1_min=std::nextafter(candidate.grid.x1_min,0.);rejects(candidate,root);
        candidate=block;candidate.grid.dx1=std::nextafter(candidate.grid.dx1,0.);rejects(candidate,root);
        candidate=block;candidate.grid.ng=0;rejects(candidate,root);
        candidate=block;candidate.grid.stride_y+=1;rejects(candidate,root);
        candidate=block;candidate.logical_x1+=1;rejects(candidate,root);
        candidate=block;candidate.morton_code^=1;rejects(candidate,root);
        candidate=block;candidate.level=amr::kMaxRefinementLevel+1;rejects(candidate,root);
        candidate=block;candidate.logical_x1=amr::kMortonCoordinateMask+1;rejects(candidate,root);
        auto changed_root=root;changed_root.x1_max=std::nextafter(changed_root.x1_max,5.);rejects(block,changed_root);
        changed_root=root;changed_root.nblockx1+=1;rejects(block,changed_root);
        if(dim<3) {
            candidate=block;candidate.logical_x3=1;rejects(candidate,root);
            changed_root=root;changed_root.nblockx3=2;rejects(block,changed_root);
        }
    }
}

Grid make_cyl2_root(const char* geometry)
{
    Grid root(amr::MAX_NG,0.,2.,0.,1.,0.,0.,1,1,0);
    root.dim=2;root.geometry=geometry;root.InitializeTopology();
    return root;
}

void init_logical_block(amr::Block& block, const Grid& root, int level,
                        int logical_x1, int logical_x2, int species)
{
    block.Reset();
    block.level=level;
    block.logical_x1=logical_x1;
    block.logical_x2=logical_x2;
    block.logical_x3=0;
    block.morton_code=amr::encodeMorton(level,logical_x1,logical_x2,0);
    const double root_dx1=(root.x1_max-root.x1_min)
        /(root.nblockx1*amr::BLOCK_NX);
    const double root_dx2=(root.x2_max-root.x2_min)
        /(root.nblockx2*amr::BLOCK_NY);
    block.InitGeometry(root,root_dx1,root_dx2,0.);
    block.fluid_state.Preallocate(block.grid.GetTotalSize());
    block.fluid_state.InitSpecies(species);
}

void fill_axisymmetric_state(amr::Block& block)
{
    const auto view=GridMetrics::make_geometry_view(block.grid);
    for(int j=block.grid.Js();j<block.grid.Je();++j)
        for(int i=block.grid.Is();i<block.grid.Ie();++i) {
            const int cell=block.grid.GetIndex(i,j,0);
            const double r=view.GetCellCenterX(i);
            const double z=view.GetCellCenterY(j);
            const double rho=1.+0.2*r+0.3*z;
            block.fluid_state.rho[cell]=rho;
            block.fluid_state.mom_u[cell]=0.1*rho*r;
            block.fluid_state.mom_v[cell]=-0.05*rho*z;
            block.fluid_state.mom_w[cell]=rho*(2.+0.5*r);
            block.fluid_state.eng[cell]=rho*10.;
            block.fluid_state.enuc_rate[cell]=0.25+0.05*r;
            const double x0=0.7+0.05*r;
            block.fluid_state.X(0,cell)=x0;
            block.fluid_state.X(1,cell)=1.-x0;
        }
}

double block_rotation_integral(const amr::Block& block)
{
    const auto view=GridMetrics::make_geometry_view(block.grid);
    double total=0.;
    for(int j=block.grid.Js();j<block.grid.Je();++j)
        for(int i=block.grid.Is();i<block.grid.Ie();++i) {
            const int cell=block.grid.GetIndex(i,j,0);
            total+=block.fluid_state.mom_w[cell]
                *GridMetrics::AngularMomentumVolumeMoment(view,i,j,0);
        }
    return total;
}

struct FineCellAverages {
    double moment=0.;
    double volume=0.;
};

FineCellAverages fine_cell_averages(
    const std::array<amr::Block,4>& children,int i,int j)
{
    FineCellAverages result{};
    const int nx=amr::BLOCK_NX,ny=amr::BLOCK_NY;
    for(int cell=0;cell<4;++cell) {
        const int child_index=(i>=nx/2?1:0)|((j>=ny/2?1:0)<<1);
        const amr::Block& child=children[child_index];
        const auto view=GridMetrics::make_geometry_view(child.grid);
        const int fi=child.grid.Is()+2*(i%(nx/2))+(cell&1);
        const int fj=child.grid.Js()+2*(j%(ny/2))+((cell>>1)&1);
        const int index=child.grid.GetIndex(fi,fj,0);
        const double volume=GridMetrics::CellVolume(view,fi,fj,0);
        const double moment=GridMetrics::AngularMomentumVolumeMoment(
            view,fi,fj,0);
        result.moment+=child.fluid_state.mom_w[index]*moment;
        result.volume+=child.fluid_state.mom_w[index]*volume;
    }
    return result;
}

/** Existing eight retained charts keep volume-weighted Mphi transfer; only
 * native axisymmetric (r,z) enables the W measure. */
void test_axisymmetric_regrid_moments()
{
    constexpr double floor=1.e-12;
    const Grid root=make_cyl2_root("cylindrical");
    amr::Block coarse;
    init_logical_block(coarse,root,0,0,0,2);
    fill_axisymmetric_state(coarse);

    std::array<amr::Block,4> children;
    for(int child=0;child<4;++child) {
        init_logical_block(children[child],root,1,child&1,(child>>1)&1,2);
        children[child].InterpolateFromCoarse(coarse,child,2,floor,floor);
    }

    double coarse_rotation=block_rotation_integral(coarse);
    double fine_rotation=0.;
    for(const auto& child:children) fine_rotation+=block_rotation_integral(child);
    expect(std::abs(fine_rotation-coarse_rotation)
            <=1.e-12*std::max(1.,std::abs(coarse_rotation)),
        "axisymmetric prolongation did not conserve total W-weighted Mphi");

    const std::array<std::pair<int,int>,4> samples{
        {{0,0},{4,0},{0,amr::BLOCK_NY/2},{4,amr::BLOCK_NY/2}}};
    for(const auto& sample:samples) {
        const int i=sample.first,j=sample.second;
        const auto averages=fine_cell_averages(children,i,j);
        const int nx=amr::BLOCK_NX,ny=amr::BLOCK_NY;
        int cells=0;
        double moment_sum=0.,volume_sum=0.;
        for(int cell=0;cell<4;++cell) {
            const int child_index=(i>=nx/2?1:0)|((j>=ny/2?1:0)<<1);
            const amr::Block& child=children[child_index];
            const auto view=GridMetrics::make_geometry_view(child.grid);
            const int fi=child.grid.Is()+2*(i%(nx/2))+(cell&1);
            const int fj=child.grid.Js()+2*(j%(ny/2))+((cell>>1)&1);
            moment_sum+=GridMetrics::AngularMomentumVolumeMoment(view,fi,fj,0);
            volume_sum+=GridMetrics::CellVolume(view,fi,fj,0);
            ++cells;
        }
        const double moment_average=averages.moment/moment_sum;
        const double volume_average=averages.volume/volume_sum;
        const int coarse_cell=coarse.grid.GetIndex(
            coarse.grid.Is()+i,coarse.grid.Js()+j,0);
        expect(std::abs(moment_average-coarse.fluid_state.mom_w[coarse_cell])
                <=2.e-12*std::max(1.,std::abs(moment_average)),
            "prolongated Mphi W-mean did not match the coarse coefficient");
        expect(std::abs(moment_average-volume_average)
                >1.e-12*std::max(1.,std::abs(moment_average)),
            "axisymmetric moment test could not distinguish W from V weighting");
        expect(cells==4,"axisymmetric cell family lost a fine cell");
    }

    amr::Block recovered;
    init_logical_block(recovered,root,0,0,0,2);
    const amr::Block* child_ptrs[4]{
        &children[0],&children[1],&children[2],&children[3]};
    expect(recovered.TryAverageToCoarse(child_ptrs,2,floor,floor)
            ==amr::regrid_math::Status::Ok,
        "axisymmetric restriction rejected an admissible family");
    for(int j=recovered.grid.Js();j<recovered.grid.Je();++j)
        for(int i=recovered.grid.Is();i<recovered.grid.Ie();++i) {
            const int cell=recovered.grid.GetIndex(i,j,0);
            expect(std::abs(recovered.fluid_state.mom_w[cell]
                        -coarse.fluid_state.mom_w[cell])
                    <=2.e-12*std::max(1.,std::abs(coarse.fluid_state.mom_w[cell])),
                "axisymmetric refine-only coarsen changed the W-weighted Mphi");
            expect(std::abs(recovered.fluid_state.rho[cell]
                        -coarse.fluid_state.rho[cell])
                    <=2.e-12*std::max(1.,std::abs(coarse.fluid_state.rho[cell])),
                "axisymmetric refine-only coarsen changed density");
            expect(std::abs(recovered.fluid_state.X(0,cell)
                        -coarse.fluid_state.X(0,cell))
                    <=2.e-12,"axisymmetric composition restriction drifted");
        }

    std::array<amr::Block,4> inadmissible;
    for(auto& child:inadmissible) {
        init_logical_block(child,root,1,0,0,2);
        for(std::size_t cell=0;cell<child.fluid_state.rho.size();++cell) {
            child.fluid_state.rho[cell]=1.;
            child.fluid_state.mom_u[cell]=0.;
            child.fluid_state.mom_v[cell]=0.;
            child.fluid_state.mom_w[cell]=0.;
            child.fluid_state.eng[cell]=-1.;
            child.fluid_state.X(0,cell)=1.;
            child.fluid_state.X(1,cell)=0.;
        }
    }
    const amr::Block* inadmissible_ptrs[4]{
        &inadmissible[0],&inadmissible[1],&inadmissible[2],&inadmissible[3]};
    amr::Block veto;
    init_logical_block(veto,root,0,0,0,2);
    expect(veto.TryAverageToCoarse(inadmissible_ptrs,2,floor,floor)
            ==amr::regrid_math::Status::CoarseFluid,
        "inadmissible axisymmetric coarsen was not vetoed");
}

/** Actual coarse/fine ghost executor on native CYL-2 leaf blocks. The fine
 * sources are deliberately nonconstant and span the axis and an annulus, so
 * W-weighted Mphi cannot be mistaken for the ordinary V average. */
void test_axisymmetric_host_ghost_moments()
{
    SimConfig config{};
    config.grid.dim=2;
    config.grid.geometry="cylindrical";
    config.grid.nblockx1=1;
    config.grid.nblockx2=2;
    config.grid.nblockx3=0;
    config.grid.x1_min=0.;config.grid.x1_max=2.;
    config.grid.x2_min=0.;config.grid.x2_max=2.;
    config.grid.amr_max_blocks=16;
    config.amr.lrefinemin=0;config.amr.lrefinemax=1;

    amr::AMRControl control(16,2);
    auto pool=control.pool;
    auto tree=control.tree;
    tree->LoadLeafGrid(config,2,
        std::vector<int>{1,1,1,1,0},
        std::vector<std::uint32_t>{0,1,0,1,0},
        std::vector<std::uint32_t>{0,0,1,1,1},
        std::vector<std::uint32_t>{0,0,0,0,0});
    const auto& active=tree->GetActiveBlocks();
    std::vector<amr::BlockHandle> handles;
    handles.reserve(active.size());
    for(std::size_t index=0;index<active.size();++index) {
        handles.push_back({{6000+index},{81}});
        amr::Block& block=pool->GetBlock(active[index]);
        const auto view=GridMetrics::make_geometry_view(block.grid);
        for(int j=block.grid.Js();j<block.grid.Je();++j)
            for(int i=block.grid.Is();i<block.grid.Ie();++i) {
                const int cell=block.grid.GetIndex(i,j,0);
                const double r=view.GetCellCenterX(i);
                block.fluid_state.rho[cell]=1.+0.5*r;
                block.fluid_state.mom_u[cell]=0.2*r;
                block.fluid_state.mom_v[cell]=-0.1;
                block.fluid_state.mom_w[cell]=1.+3.*r;
                block.fluid_state.eng[cell]=5.;
                block.fluid_state.enuc_rate[cell]=-0.25;
                block.fluid_state.X(0,cell)=0.4;
                block.fluid_state.X(1,cell)=0.6;
            }
    }
    control.BindActiveHandles(handles);

    const auto plan=control.ghost_exchange.BuildCoarseFinePlan(
        pool,tree,2,handles);
    const auto cell_plan=amr::compile_coarse_fine_cell_plan(plan,2);
    const amr::CoarseFineCellTransfer* axis=nullptr;
    const amr::CoarseFineCellTransfer* annulus=nullptr;
    for(const auto& transfer:cell_plan.transfers) {
        if(transfer.rule!=amr::RefinementRule::FineGhostAverage) continue;
        if(transfer.destination_cell[0]==0) axis=&transfer;
        if(transfer.destination_cell[0]==amr::BLOCK_NX/2) annulus=&transfer;
    }
    expect(axis!=nullptr&&annulus!=nullptr,
        "axisymmetric ghost fixture lacks axis/annulus average transfers");

    const auto block_for=[&](const amr::BlockHandle handle) -> const amr::Block& {
        for(std::size_t index=0;index<handles.size();++index)
            if(handles[index]==handle) return pool->GetBlock(active[index]);
        throw std::runtime_error("axisymmetric ghost handle is missing");
    };
    const auto expected_average=[&](const amr::CoarseFineCellTransfer& transfer,
                                    bool moment,bool density) {
        const amr::Block& source=block_for(transfer.source.handle);
        const auto view=GridMetrics::make_geometry_view(source.grid);
        double integral=0.,measure_sum=0.;
        for(std::size_t cell=0;cell<transfer.source_count;++cell) {
            const auto& logical=transfer.source_cells[cell];
            const int i=source.grid.Is()+logical[0];
            const int j=source.grid.Js()+logical[1];
            const int index=source.grid.GetIndex(i,j,0);
            const double measure=moment
                ? GridMetrics::AngularMomentumVolumeMoment(view,i,j,0)
                : GridMetrics::CellVolume(view,i,j,0);
            integral+=(density?source.fluid_state.rho[index]
                              :source.fluid_state.mom_w[index])*measure;
            measure_sum+=measure;
        }
        return integral/measure_sum;
    };
    struct Expectation {
        const amr::CoarseFineCellTransfer* transfer;
        double moment;
        double volume;
        double density;
        int cell;
    } expectations[2]{
        {axis,expected_average(*axis,true,false),
         expected_average(*axis,false,false),
         expected_average(*axis,false,true),-1},
        {annulus,expected_average(*annulus,true,false),
         expected_average(*annulus,false,false),
         expected_average(*annulus,false,true),-1}};
    for(auto& expectation:expectations) {
        const amr::Block& destination=block_for(expectation.transfer->destination.handle);
        const auto& logical=expectation.transfer->destination_cell;
        expectation.cell=destination.grid.GetIndex(
            destination.grid.Is()+logical[0],
            destination.grid.Js()+logical[1],
            destination.grid.Ks()+logical[2]);
        expect(std::abs(expectation.moment-expectation.volume)
                >1.e-12*std::max(1.,std::abs(expectation.moment)),
            "axisymmetric ghost moment control cannot distinguish W from V");
    }

    control.ghost_exchange.ExecuteCoarseFinePlan(
        plan,pool,tree,2,&amr::Block::fluid_state,handles);
    for(const auto& expectation:expectations) {
        const amr::Block& destination=block_for(expectation.transfer->destination.handle);
        expect(destination.fluid_state.mom_w[expectation.cell]==expectation.moment,
            "fine-to-coarse ghost Mphi did not use the exact W average");
        expect(destination.fluid_state.rho[expectation.cell]==expectation.density,
            "fine-to-coarse ghost density stopped using the ordinary V average");
    }
}

bool same_flux_group(const amr::AmrTransferOperation& left,
                     const amr::AmrTransferOperation& right)
{
    return left.source==right.source && left.destination==right.destination
        && left.source_box==right.source_box
        && left.destination_box==right.destination_box
        && left.axis==right.axis && left.side==right.side
        && left.rule==right.rule;
}

/** Actual CYL-2 face/T registration and coarse W reflux, plus the ordinary
 * area/volume controls that must remain distinct on an axial fragment. */
void test_axisymmetric_flux_moments()
{
    SimConfig config{};
    config.grid.dim=2;
    config.grid.geometry="cylindrical";
    config.grid.nblockx1=2;
    config.grid.nblockx2=2;
    config.grid.nblockx3=0;
    config.grid.x1_min=0.;config.grid.x1_max=2.;
    config.grid.x2_min=0.;config.grid.x2_max=2.;
    config.grid.amr_max_blocks=32;
    config.amr.lrefinemin=0;config.amr.lrefinemax=1;

    amr::AMRControl control(32,2);
    auto pool=control.pool;
    auto tree=control.tree;
    tree->LoadLeafGrid(config,0,
        std::vector<int>{1,1,1,1,1,1,1,1,0,0},
        std::vector<std::uint32_t>{0,1,0,1,2,3,2,3,1,0},
        std::vector<std::uint32_t>{2,2,3,3,0,0,1,1,1,0},
        std::vector<std::uint32_t>{0,0,0,0,0,0,0,0,0,0});
    const auto& active=tree->GetActiveBlocks();
    std::vector<amr::BlockHandle> handles;
    for(std::size_t index=0;index<active.size();++index)
        handles.push_back({{3000+index},{91}});
    control.BindActiveHandles(handles);
    control.flux_register.EnsureSpecies(0);

    const auto topology=amr::build_amr_flux_topology_plan(
        *pool,active,handles,2,0);
    bool saw_registration_moment=false;
    for(const auto& route:topology.routes) {
        for(const auto& operation:route.plan.operations) {
            if(operation.field!=amr::AmrField::MomW) continue;
            const auto rho=std::find_if(
                route.plan.operations.begin(),route.plan.operations.end(),
                [&](const amr::AmrTransferOperation& candidate) {
                    return candidate.field==amr::AmrField::Rho
                        && same_flux_group(candidate,operation);
                });
            expect(rho!=route.plan.operations.end(),
                "axisymmetric flux plan lost its ordinary Mphi control");
            if(operation.rule==amr::RefinementRule::FineFluxContribution
                && std::abs(operation.weight-rho->weight)
                    >1.e-13*std::max(1.,std::abs(rho->weight))) {
                saw_registration_moment=true;
                const amr::Block& fine=pool->GetBlock(
                    topology.pool_lowering.at(operation.source));
                const amr::Block& coarse=pool->GetBlock(
                    topology.pool_lowering.at(operation.destination));
                const double fine_moment=GridMetrics::AngularMomentumFaceMoment(
                    GridMetrics::make_geometry_view(fine.grid),
                    amr::axis_value(operation.axis),
                    fine.grid.Is()+operation.source_box.first[0],
                    fine.grid.Js()+operation.source_box.first[1],
                    fine.grid.Ks()+operation.source_box.first[2],
                    operation.side==amr::AmrSide::Lower);
                const double coarse_moment=GridMetrics::AngularMomentumFaceMoment(
                    GridMetrics::make_geometry_view(coarse.grid),
                    amr::axis_value(operation.axis),
                    coarse.grid.Is()+operation.destination_box.first[0],
                    coarse.grid.Js()+operation.destination_box.first[1],
                    coarse.grid.Ks()+operation.destination_box.first[2],
                    operation.side==amr::AmrSide::Upper);
                expect(std::bit_cast<std::uint64_t>(operation.weight)
                        ==std::bit_cast<std::uint64_t>(
                            fine_moment/coarse_moment),
                    "axisymmetric registration moment weight is not the exact T/T ratio");
            }
        }
    }
    expect(saw_registration_moment,
        "axisymmetric fine registration reused ordinary area weights for Mphi");

    const auto reflux=amr::build_amr_reflux_topology_plan(*pool,topology);
    bool saw_reflux_moment=false;
    for(const auto& operation:reflux.operations) {
        if(operation.field!=amr::AmrField::MomW) continue;
        const auto rho=std::find_if(
            reflux.operations.begin(),reflux.operations.end(),
            [&](const amr::AmrTransferOperation& candidate) {
                return candidate.field==amr::AmrField::Rho
                    && same_flux_group(candidate,operation);
            });
        expect(rho!=reflux.operations.end(),
            "axisymmetric reflux plan lost its ordinary Mphi control");
        const auto block_id=topology.pool_lowering.at(operation.destination);
        const amr::Block& block=pool->GetBlock(block_id);
        const auto view=GridMetrics::make_geometry_view(block.grid);
        const int i=block.grid.Is()+operation.destination_box.first[0];
        const int j=block.grid.Js()+operation.destination_box.first[1];
        const int k=block.grid.Ks()+operation.destination_box.first[2];
        const double moment=GridMetrics::AngularMomentumFaceMoment(
            view,amr::axis_value(operation.axis),i,j,k,
            operation.side==amr::AmrSide::Upper);
        const double volume_moment=GridMetrics::AngularMomentumVolumeMoment(
            view,i,j,k);
        const double area=GridMetrics::FaceArea(
            view,amr::axis_value(operation.axis),i,j,k,
            operation.side==amr::AmrSide::Upper);
        const double volume=GridMetrics::CellVolume(view,i,j,k);
        expect(std::abs(operation.weight-moment/volume_moment)
                <=2.e-14*std::max(1.,std::abs(operation.weight)),
            "axisymmetric reflux did not use the exact T/W moment");
        if(operation.axis==amr::AmrAxis::X) {
            expect(std::abs(operation.weight-area/volume)
                    >1.e-13*std::max(1.,std::abs(operation.weight)),
                "axisymmetric radial reflux moment collapsed to ordinary A/V");
            saw_reflux_moment=true;
        }
    }
    expect(saw_reflux_moment,
        "axisymmetric reflux plan emitted no Mphi correction");

    int coarse_id=-1;
    for(const int id:active) {
        const auto& block=pool->GetBlock(id);
        if(block.level==0 && block.face_neighbors[0].count>0
            && block.face_neighbors[0].level_diff==1
            && block.face_neighbors[2].count>0
            && block.face_neighbors[2].level_diff==1) {
            coarse_id=id;
            break;
        }
    }
    expect(coarse_id>=0,"axisymmetric flux fixture has no axial coarse face");
    const amr::Block& coarse=pool->GetBlock(coarse_id);
    const auto coarse_view=GridMetrics::make_geometry_view(coarse.grid);
    const auto logical=amr::flux_plan_detail::face_cell_coordinates(
        0,amr::AmrAxis::X,amr::AmrSide::Lower,2);
    const int i=coarse.grid.Is()+logical[0];
    const int j=coarse.grid.Js()+logical[1];
    const int k=coarse.grid.Ks()+logical[2];
    const int state_index=coarse.grid.GetIndex(i,j,k);
    FluidState& state=pool->GetBlock(coarse_id).fluid_state;
    state.rho[state_index]=1.;
    state.mom_u[state_index]=0.;
    state.mom_v[state_index]=0.;
    state.mom_w[state_index]=0.;
    state.eng[state_index]=1.;

    control.flux_register.Clear();
    FluidVector delta{};
    delta.mom_w=1.;
    control.flux_register.AddRegisteredFlux(
        coarse_id,0,0,delta,1.0);
    const auto reflux_plan=control.flux_register.BuildRefluxPlan(
        pool,active,handles,2,1.0);
    control.flux_register.ExecuteRefluxPlan(
        reflux_plan,pool,active,handles,&amr::Block::fluid_state,1.0);
    const double moment=GridMetrics::AngularMomentumFaceMoment(
        coarse_view,0,i,j,k,false);
    const double volume_moment=GridMetrics::AngularMomentumVolumeMoment(
        coarse_view,i,j,k);
    const double area=GridMetrics::FaceArea(coarse_view,0,i,j,k,false);
    const double volume=GridMetrics::CellVolume(coarse_view,i,j,k);
    expect(std::abs(state.mom_w[state_index]-moment/volume_moment)
            <=2.e-13*std::max(1.,moment/volume_moment),
        "Host reflux did not correct Mphi with the coarse T/W ratio");
    expect(std::abs(moment/volume_moment-area/volume)
            >1.e-12*std::max(1.,moment/volume_moment),
        "axisymmetric reflux negative control could not detect wrong V");

    const amr::AmrFluxRegistrationRoute* selected=nullptr;
    for(const auto& route:topology.routes)
        if(route.key.axis==amr::AmrAxis::Y) {
            const bool has_fine=std::any_of(
                route.plan.operations.begin(),route.plan.operations.end(),
                [](const amr::AmrTransferOperation& operation) {
                    return operation.rule
                        ==amr::RefinementRule::FineFluxContribution
                        && operation.field==amr::AmrField::MomW;
                });
            if(has_fine) {selected=&route;break;}
        }
    expect(selected!=nullptr,
        "axisymmetric flux fixture has no fine axial moment route");
    control.flux_register.Clear();
    std::vector<double> values(selected->plan.operations.size());
    for(std::size_t index=0;index<values.size();++index) {
        const auto& operation=selected->plan.operations[index];
        values[index]=1.+operation.source_box.first[0];
    }
    control.flux_register.ApplyRegistrationPlan(
        selected->plan,values,topology.pool_lowering,1.0);
    struct RegisterExpectation { double rho=0.; double moment=0.; };
    std::map<std::tuple<int,int,int>,RegisterExpectation> expected;
    for(std::size_t index=0;index<selected->plan.operations.size();++index) {
        const auto& operation=selected->plan.operations[index];
        if(operation.field!=amr::AmrField::Rho
            && operation.field!=amr::AmrField::MomW)
            continue;
        const int destination=topology.pool_lowering.at(operation.destination);
        const int face=2*amr::axis_value(operation.axis)
            +amr::side_value(operation.side);
        const int cell=amr::flux_plan_detail::face_cell_index(
            operation.destination_box,operation.axis);
        auto& entry=expected[std::tuple{destination,face,cell}];
        const double coefficient=amr::flux_math::registration_coefficient(
            operation.rule,operation.weight,1.0);
        if(operation.field==amr::AmrField::MomW)
            entry.moment+=coefficient*values[index];
        else
            entry.rho+=coefficient*values[index];
    }
    for(const auto& [key,entry]:expected) {
        const auto [destination,face,cell]=key;
        const auto flux=control.flux_register.GetSummedFlux(
            destination,face,cell);
        expect(std::abs(flux.rho-entry.rho)
                <=2.e-13*std::max(1.,std::abs(entry.rho))
                && std::abs(flux.mom_w-entry.moment)
                    <=2.e-13*std::max(1.,std::abs(entry.moment)),
            "Host flux registration did not apply the per-field Mphi weight");
    }
}

} // namespace

int main()
{
    try {
        test_ordinary_logical_geometry_identity();
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
        test_axisymmetric_regrid_moments();
        test_axisymmetric_host_ghost_moments();
        test_axisymmetric_flux_moments();
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
