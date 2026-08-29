#include "amr/AmrTransferPlans.h"
#include "amr/AMRFluxRegistering.h"
#include "amr/FluxRegister.h"
#include "amr/GhostExchange.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
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
    for (int index = 0; index < total; ++index) {
        const double value = 1000.0 * block.level
            + 100.0 * block.logical_x1 + index;
        block.fluid_state.rho[index] = value + 1.0;
        block.fluid_state.mom_u[index] = value + 2.0;
        block.fluid_state.mom_v[index] = value + 3.0;
        block.fluid_state.mom_w[index] = value + 4.0;
        block.fluid_state.eng[index] = value + 5.0;
        block.fluid_state.enuc_rate[index] = value + 6.0;
        for (int species = 0;
             species < block.fluid_state.GetNumSpecies(); ++species)
            block.fluid_state.X(species, index) =
                0.1 * (species + 1) + 1.0e-6 * value;
    }
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

    int fine_id = -1;
    int coarse_id = -1;
    for (const int block_id : active) {
        const auto& block = pool->GetBlock(block_id);
        if (block.level == 1 && block.logical_x1 == 1) fine_id = block_id;
        if (block.level == 0 && block.logical_x1 == 1) coarse_id = block_id;
    }
    expect(fine_id >= 0 && coarse_id >= 0,
           "mixed AMR fixture endpoints are missing");
    const amr::Block& fine_before = pool->GetBlock(fine_id);
    const amr::Block& coarse_before = pool->GetBlock(coarse_id);
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
    const double expected_fine_ghost =
        coarse_before.fluid_state.rho[coarse_source];
    const double expected_coarse_ghost = 0.5
        * (fine_before.fluid_state.rho[fine_average_a]
           + fine_before.fluid_state.rho[fine_average_b]);
    const double expected_enuc = 0.5
        * (fine_before.fluid_state.enuc_rate[fine_average_a]
           + fine_before.fluid_state.enuc_rate[fine_average_b]);

    exchange.ExecuteCoarseFinePlan(
        plan, pool, tree, 1, &amr::Block::fluid_state, handles);
    const amr::Block& fine_after = pool->GetBlock(fine_id);
    const amr::Block& coarse_after = pool->GetBlock(coarse_id);
    expect(fine_after.fluid_state.rho[fine_upper_ghost]
               == expected_fine_ghost,
           "coarse-to-fine ghost route drifted");
    expect(coarse_after.fluid_state.rho[coarse_lower_ghost]
               == expected_coarse_ghost,
           "fine-to-coarse ghost average drifted");
    expect(coarse_after.fluid_state.enuc_rate[coarse_lower_ghost]
               == expected_enuc,
           "fine-to-coarse ENUC ghost average drifted");

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
