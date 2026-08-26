#include "amr/BoundaryPlan.h"
#include "driver/DriverUtils.h"
#include "driver/StageScheduler.h"
#include "driver/StateResidency.h"

#include <array>
#include <bit>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace
{
using namespace arch::boundary;

void require(bool condition, std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string(message));
}

template <typename Function>
void require_rejected(Function&& function, std::string_view message)
{
    bool rejected = false;
    try {
        function();
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, message);
}

BoundaryPlanInput one_dimensional_input()
{
    BoundaryPlanInput input{};
    input.dimension = 1;
    input.active_extent = {16, 1, 1};
    input.ghost_depth = 4;
    input.faces.fill(BoundaryType::Inactive);
    input.faces[face_index(BoundaryAxis::X1, BoundarySide::Lower)] =
        BoundaryType::Periodic;
    input.faces[face_index(BoundaryAxis::X1, BoundarySide::Upper)] =
        BoundaryType::Reflecting;
    return input;
}

BoundaryPlanInput two_dimensional_input()
{
    BoundaryPlanInput input{};
    input.dimension = 2;
    input.active_extent = {16, 16, 1};
    input.ghost_depth = 4;
    input.faces.fill(BoundaryType::Inactive);
    input.faces[face_index(BoundaryAxis::X1, BoundarySide::Lower)] =
        BoundaryType::Periodic;
    input.faces[face_index(BoundaryAxis::X1, BoundarySide::Upper)] =
        BoundaryType::Outflow;
    input.faces[face_index(BoundaryAxis::X2, BoundarySide::Lower)] =
        BoundaryType::Reflecting;
    input.faces[face_index(BoundaryAxis::X2, BoundarySide::Upper)] =
        BoundaryType::Periodic;
    return input;
}

BoundaryPlanInput three_dimensional_input()
{
    BoundaryPlanInput input{};
    input.dimension = 3;
    input.active_extent = {16, 16, 16};
    input.ghost_depth = 4;
    input.faces = {
        BoundaryType::Periodic, BoundaryType::Reflecting,
        BoundaryType::Outflow, BoundaryType::Periodic,
        BoundaryType::Reflecting, BoundaryType::Outflow};
    return input;
}

void test_public_numeric_contract()
{
    static_assert(std::is_same_v<std::underlying_type_t<BoundaryType>,
                                 std::uint8_t>);
    static_assert(static_cast<int>(BoundaryType::Periodic) == 0);
    static_assert(static_cast<int>(BoundaryType::Outflow) == 1);
    static_assert(static_cast<int>(BoundaryType::Reflecting) == 2);
    static_assert(static_cast<int>(BoundaryType::Inactive) == 3);
    static_assert(static_cast<int>(BoundaryAxis::X1) == 0);
    static_assert(static_cast<int>(BoundaryAxis::X2) == 1);
    static_assert(static_cast<int>(BoundaryAxis::X3) == 2);
    static_assert(static_cast<int>(BoundarySide::Lower) == 0);
    static_assert(static_cast<int>(BoundarySide::Upper) == 1);
    static_assert(face_index(BoundaryAxis::X1, BoundarySide::Lower) == 0);
    static_assert(face_index(BoundaryAxis::X1, BoundarySide::Upper) == 1);
    static_assert(face_index(BoundaryAxis::X2, BoundarySide::Lower) == 2);
    static_assert(face_index(BoundaryAxis::X2, BoundarySide::Upper) == 3);
    static_assert(face_index(BoundaryAxis::X3, BoundarySide::Lower) == 4);
    static_assert(face_index(BoundaryAxis::X3, BoundarySide::Upper) == 5);
}

void test_frozen_plan_fingerprints()
{
    struct Fixture {
        BoundaryPlanInput input;
        std::array<std::size_t, 3> phase_counts;
        std::uint64_t operation_count;
        std::uint64_t fingerprint;
    };
    const std::array<Fixture, 3> fixtures{{
        {one_dimensional_input(), {8, 0, 0}, 8,
         UINT64_C(0x47898461178fb7ca)},
        {two_dimensional_input(), {128, 192, 0}, 320,
         UINT64_C(0x900448e8727693a0)},
        {three_dimensional_input(), {2048, 3072, 4608}, 9728,
         UINT64_C(0x559c7d610a5606b6)},
    }};

    for (const auto& fixture : fixtures) {
        const auto plan = make_boundary_plan(fixture.input);
        static_assert(std::is_same_v<
            decltype(plan.operations()), std::span<const BoundaryOperation>>);
        static_assert(std::is_same_v<
            decltype(plan.phases()), std::span<const BoundaryPhase>>);
        require(plan.phases().size() == 3, "plan does not expose three phases");
        require(plan.operations().size() == fixture.operation_count,
                "operation count drifted");
        require(plan.fingerprint() == fixture.fingerprint,
                "independently frozen plan fingerprint drifted");
        std::size_t first = 0;
        for (std::size_t phase = 0; phase < 3; ++phase) {
            require(static_cast<std::size_t>(plan.phases()[phase].id) == phase,
                    "phase id order drifted");
            require(plan.phases()[phase].first == first,
                    "phase first ordinal drifted");
            require(plan.phases()[phase].count == fixture.phase_counts[phase],
                    "phase count drifted");
            first += fixture.phase_counts[phase];
        }
        for (std::size_t ordinal = 0; ordinal < plan.operations().size(); ++ordinal)
            require(plan.operations()[ordinal].ordinal == ordinal,
                    "operation ordinals are not contiguous");
    }
}

void test_sources_and_components()
{
    const auto plan = make_boundary_plan(one_dimensional_input());
    const auto operations = plan.operations();
    require(operations[0].source.i == 15 && operations[0].destination.i == -1,
            "periodic lower source mapping drifted");
    require(operations[1].source.i == 15 && operations[1].destination.i == 16,
            "reflecting upper source mapping drifted");
    require(operations[6].source.i == 12 && operations[6].destination.i == -4,
            "periodic lower depth mapping drifted");
    require(operations[7].source.i == 12 && operations[7].destination.i == 19,
            "reflecting upper depth mapping drifted");

    const auto normal = component_mapping(
        operations[1], BoundaryFieldClass::MomentumX);
    const auto tangent = component_mapping(
        operations[1], BoundaryFieldClass::MomentumY);
    require(normal.destination == BoundaryFieldClass::MomentumX
            && normal.source == BoundaryFieldClass::MomentumX
            && normal.sign == -1,
            "normal reflection sign drifted");
    require(tangent.sign == 1, "tangential reflection sign drifted");
}

void test_invalid_inputs()
{
    auto input = one_dimensional_input();
    input.dimension = 0;
    require_rejected([&] { (void)make_boundary_plan(input); },
                     "dimension zero was accepted");
    input = one_dimensional_input();
    input.active_extent[1] = 2;
    require_rejected([&] { (void)make_boundary_plan(input); },
                     "noncanonical inactive extent was accepted");
    input = one_dimensional_input();
    input.faces[face_index(BoundaryAxis::X2, BoundarySide::Lower)] =
        BoundaryType::Outflow;
    require_rejected([&] { (void)make_boundary_plan(input); },
                     "noncanonical inactive face was accepted");
    input = one_dimensional_input();
    input.faces[face_index(BoundaryAxis::X1, BoundarySide::Lower)] =
        BoundaryType::Inactive;
    require_rejected([&] { (void)make_boundary_plan(input); },
                     "inactive active-axis face was accepted");
    input = one_dimensional_input();
    input.ghost_depth = 0;
    require_rejected([&] { (void)make_boundary_plan(input); },
                     "zero ghost depth was accepted");
    input = one_dimensional_input();
    input.faces[face_index(BoundaryAxis::X1, BoundarySide::Lower)] =
        static_cast<BoundaryType>(255);
    require_rejected([&] { (void)make_boundary_plan(input); },
                     "unknown boundary enum was accepted");
    input = one_dimensional_input();
    input.dimension = 4;
    require_rejected([&] { (void)make_boundary_plan(input); },
                     "dimension four was accepted");
    input = one_dimensional_input();
    input.active_extent[0] = 0;
    require_rejected([&] { (void)make_boundary_plan(input); },
                     "zero active extent was accepted");
    input = one_dimensional_input();
    input.ghost_depth = 17;
    require_rejected([&] { (void)make_boundary_plan(input); },
                     "periodic/reflecting ghost depth beyond extent was accepted");
    input = one_dimensional_input();
    input.faces[face_index(BoundaryAxis::X1, BoundarySide::Lower)] =
        BoundaryType::Outflow;
    input.faces[face_index(BoundaryAxis::X1, BoundarySide::Upper)] =
        BoundaryType::Outflow;
    input.ghost_depth = std::numeric_limits<std::uint32_t>::max();
    require_rejected([&] { (void)make_boundary_plan(input); },
                     "overflowing operation/coordinate input was accepted");
    require_rejected(
        [&] {
            (void)arch::boundary::detail::checked_destination_coordinate(
                16, std::numeric_limits<std::uint32_t>::max(),
                BoundarySide::Upper);
        },
        "checked logical coordinate overflow was accepted");
    input = one_dimensional_input();
    require_rejected(
        [&] {
            (void)source_logical_cell(
                input, BoundaryAxis::X1, BoundarySide::Lower,
                input.ghost_depth + 1, {-5, 0, 0});
        },
        "out-of-range logical source depth was accepted");
    input = one_dimensional_input();
    input.active_extent[0] = std::numeric_limits<std::int32_t>::min();
    input.ghost_depth = 1;
    require_rejected(
        [&] {
            (void)source_logical_cell(
                input, BoundaryAxis::X1, BoundarySide::Lower, 1,
                {-1, 0, 0});
        },
        "logical source mapping overflowed an unvalidated extent");
    input = one_dimensional_input();
    input.active_extent[0] = 2;
    require_rejected(
        [&] {
            (void)source_logical_cell(
                input, BoundaryAxis::X1, BoundarySide::Lower, 3,
                {-3, 0, 0});
        },
        "periodic source depth beyond the active extent was accepted");
}

std::uint64_t update_state_hash(std::uint64_t hash, std::uint64_t value)
{
    constexpr std::uint64_t prime = UINT64_C(1099511628211);
    for (int byte = 0; byte < 8; ++byte) {
        hash ^= (value >> (8 * byte)) & UINT64_C(0xff);
        hash *= prime;
    }
    return hash;
}

std::uint64_t state_hash(const FluidState& state, bool enuc_only)
{
    std::uint64_t hash = UINT64_C(14695981039346656037);
    const auto add = [&](const std::vector<double>& values) {
        for (const double value : values)
            hash = update_state_hash(hash, std::bit_cast<std::uint64_t>(value));
    };
    if (enuc_only) {
        add(state.enuc_rate);
        return hash;
    }
    add(state.rho);
    add(state.mom_u);
    add(state.mom_v);
    add(state.mom_w);
    add(state.eng);
    add(state.mass_fractions);
    return hash;
}

std::uint64_t values_hash(const std::vector<double>& values)
{
    std::uint64_t hash = UINT64_C(14695981039346656037);
    for (const double value : values)
        hash = update_state_hash(hash, std::bit_cast<std::uint64_t>(value));
    return hash;
}

void seed_state(FluidState& state, const Grid& grid, int species)
{
    state.Preallocate(grid.GetTotalSize());
    state.InitSpecies(species);
    for (int cell = 0; cell < grid.GetTotalSize(); ++cell) {
        state.rho[cell] = 1000.0 + 0.25 * cell;
        state.mom_u[cell] = -2000.0 - 0.5 * cell;
        state.mom_v[cell] = 3000.0 + 0.75 * cell;
        state.mom_w[cell] = -4000.0 - 1.25 * cell;
        state.eng[cell] = 5000.0 + 1.5 * cell;
        state.enuc_rate[cell] = -6000.0 - 1.75 * cell;
        for (int s = 0; s < species; ++s) {
            const auto raw = UINT64_C(0x3f80000000000000)
                + (static_cast<std::uint64_t>(s) << 40)
                + static_cast<std::uint64_t>(cell);
            state.X(s, cell) = std::bit_cast<double>(raw);
        }
    }
    const int first = grid.GetIndex(grid.Is(), grid.Js(), grid.Ks());
    const int second = grid.GetIndex(grid.Is() + 1, grid.Js(), grid.Ks());
    const int last = grid.GetIndex(grid.Ie() - 1, grid.Je() - 1, grid.Ke() - 1);
    state.mom_u[first] = std::bit_cast<double>(UINT64_C(0x0000000000000000));
    state.mom_v[first] = std::bit_cast<double>(UINT64_C(0x8000000000000000));
    state.eng[first] = std::bit_cast<double>(UINT64_C(0x7ff8000000000042));
    state.rho[second] = std::numeric_limits<double>::infinity();
    state.mom_w[last] = -std::numeric_limits<double>::infinity();
    if (species > 0)
        state.X(0, first) = std::bit_cast<double>(UINT64_C(0x7ff8000000001234));
}

Grid make_grid(int dimension)
{
    Grid grid(amr::MAX_NG, 0.0, 1.0, 0.0, 1.0, 0.0, 1.0);
    grid.dim = dimension;
    grid.InitializeTopology();
    return grid;
}

SimConfig make_mixed_config(int dimension, bool uppercase)
{
    SimConfig config{};
    config.grid.dim = dimension;
    config.grid.x1l_boundary_type = uppercase ? "Periodic" : "periodic";
    config.grid.x1r_boundary_type = uppercase ? "Reflecting" : "reflect";
    config.grid.x2l_boundary_type = uppercase ? "Reflecting" : "reflect";
    config.grid.x2r_boundary_type = uppercase ? "Periodic" : "periodic";
    config.grid.x3l_boundary_type = uppercase ? "Outflow" : "outflow";
    config.grid.x3r_boundary_type = uppercase ? "Reflecting" : "reflect";
    return config;
}

void test_host_lowering_and_executor()
{
    auto input = three_dimensional_input();
    input.faces = {
        BoundaryType::Periodic, BoundaryType::Reflecting,
        BoundaryType::Reflecting, BoundaryType::Periodic,
        BoundaryType::Outflow, BoundaryType::Reflecting};
    const auto plan = make_boundary_plan(input);
    const auto grid = make_grid(3);
    const auto layout = arch::boundary::host::make_layout(grid);
    const auto compiled = arch::boundary::host::compile(plan, layout);
    require(compiled.logical_fingerprint == plan.fingerprint(),
            "Host lowerer changed logical fingerprint");
    require(compiled.transfers.size() == plan.operations().size(),
            "Host lowerer changed operation count");
    require(compiled.phases == std::array<BoundaryPhase, 3>{
                plan.phases()[0], plan.phases()[1], plan.phases()[2]},
            "Host lowerer changed phase partition");
    for (std::size_t index = 0; index < compiled.transfers.size(); ++index) {
        const auto& transfer = compiled.transfers[index];
        require(transfer.logical_ordinal == index,
                "Host lowerer changed logical ordinal");
        require(transfer.source_index >= 0 && transfer.source_index < layout.total_size
                && transfer.destination_index >= 0
                && transfer.destination_index < layout.total_size,
                "Host lowerer produced an invalid flattened index");
    }

    FluidState state;
    seed_state(state, grid, 2);
    const auto enuc_before = state_hash(state, true);
    arch::boundary::host::execute(compiled, state);
    require(state_hash(state, false) == UINT64_C(0x5602b3e48fc0055b),
            "Host executor drifted from frozen latest-main state");
    require(state_hash(state, true) == enuc_before,
            "Host executor changed enuc_rate");

    FluidState exact_copy;
    seed_state(exact_copy, grid, 0);
    const auto& exact_transfer = compiled.transfers.front();
    exact_copy.mom_v[exact_transfer.source_index] =
        std::bit_cast<double>(UINT64_C(0x8000000000000000));
    exact_copy.eng[exact_transfer.source_index] =
        std::bit_cast<double>(UINT64_C(0x7ff800000000beef));
    arch::boundary::host::execute(compiled, exact_copy);
    require(std::bit_cast<std::uint64_t>(
                exact_copy.mom_v[exact_transfer.destination_index])
                == UINT64_C(0x8000000000000000),
            "positive-sign compiled copy canonicalized negative zero");
    require(std::bit_cast<std::uint64_t>(
                exact_copy.eng[exact_transfer.destination_index])
                == UINT64_C(0x7ff800000000beef),
            "positive-sign compiled copy changed NaN payload");

    FluidState invalid;
    invalid.Preallocate(grid.GetTotalSize() - 1);
    const auto before = invalid.rho;
    require_rejected([&] { arch::boundary::host::execute(compiled, invalid); },
                     "Host executor accepted a mismatched state layout");
    require(invalid.rho == before, "Host validation failure wrote state");

    FluidState invalid_conserved;
    seed_state(invalid_conserved, grid, 2);
    invalid_conserved.rho.pop_back();
    const auto invalid_conserved_before = invalid_conserved.rho;
    require_rejected(
        [&] { arch::boundary::host::execute(compiled, invalid_conserved); },
        "Host executor accepted one undersized conserved array");
    require(invalid_conserved.rho == invalid_conserved_before,
            "conserved validation failure wrote state");

    auto invalid_layout = layout;
    invalid_layout.stride_y = invalid_layout.total_x - 1;
    require_rejected([&] { (void)arch::boundary::host::compile(
                              plan, invalid_layout); },
                     "Host lowerer accepted a packed/undersized stride");
    invalid_layout = layout;
    invalid_layout.active_origin_i = 3;
    require_rejected([&] { (void)arch::boundary::host::compile(
                              plan, invalid_layout); },
                     "Host lowerer accepted an insufficient ghost origin");
    const auto overflow_plan = make_boundary_plan(one_dimensional_input());
    arch::boundary::host::HostBoundaryLayout overflow_layout{
        1, 16, 1, 1, 4,
        std::numeric_limits<int>::max(), 0, 0,
        std::numeric_limits<int>::max(), 1, 1,
        std::numeric_limits<int>::max(), std::numeric_limits<int>::max(),
        std::numeric_limits<int>::max()};
    require_rejected(
        [&] { arch::boundary::host::validate_layout(
                  overflow_plan, overflow_layout); },
        "Host lowerer accepted an overflowing active-region bound");
    require(!arch::boundary::detail::contains_active_region(
                std::numeric_limits<int>::max(), 16, 4,
                std::numeric_limits<int>::max()),
            "checked active-region bound overflowed before comparison");
    require_rejected(
        [&] {
            (void)arch::boundary::host::flatten_checked(
                layout, {-5, 0, 0});
        },
        "Host lowerer accepted an out-of-range logical coordinate");

    FluidState invalid_species;
    invalid_species.Preallocate(grid.GetTotalSize());
    invalid_species.InitSpecies(2);
    invalid_species.mass_fractions.pop_back();
    const auto invalid_species_before = invalid_species.mass_fractions;
    require_rejected(
        [&] { arch::boundary::host::execute(compiled, invalid_species); },
        "Host executor accepted an invalid species layout");
    require(invalid_species.mass_fractions == invalid_species_before,
            "species validation failure wrote state");

    auto corrupt_first = compiled;
    corrupt_first.phases[1].first += 1;
    FluidState phase_untouched;
    seed_state(phase_untouched, grid, 2);
    const auto phase_before = state_hash(phase_untouched, false);
    require_rejected(
        [&] { arch::boundary::host::execute(corrupt_first, phase_untouched); },
        "Host executor accepted a non-contiguous phase range");
    require(state_hash(phase_untouched, false) == phase_before,
            "Host phase validation failure wrote state");

    auto corrupt_count = compiled;
    corrupt_count.phases[0].count =
        std::numeric_limits<std::size_t>::max();
    require_rejected(
        [&] { arch::boundary::host::execute(corrupt_count, phase_untouched); },
        "Host executor accepted an overflowing phase count");
    require(state_hash(phase_untouched, false) == phase_before,
            "Host phase-count validation failure wrote state");

    const auto require_invalid_compiled_is_pure =
        [&](const auto& invalid_compiled, std::string_view message) {
            require_rejected(
                [&] {
                    arch::boundary::host::execute(
                        invalid_compiled, phase_untouched);
                }, message);
            require(state_hash(phase_untouched, false) == phase_before,
                    "Host transfer validation failure wrote state");
        };
    auto corrupt_ordinal = compiled;
    corrupt_ordinal.transfers.front().logical_ordinal += 1;
    require_invalid_compiled_is_pure(
        corrupt_ordinal, "Host executor accepted a corrupt logical ordinal");
    auto corrupt_index = compiled;
    corrupt_index.transfers.front().source_index = compiled.layout.total_size;
    require_invalid_compiled_is_pure(
        corrupt_index, "Host executor accepted an out-of-range source index");
    auto corrupt_sign = compiled;
    corrupt_sign.transfers.front().conserved_signs[0] = 0;
    require_invalid_compiled_is_pure(
        corrupt_sign, "Host executor accepted a non-unit compiled sign");
}

void test_production_bc_handler()
{
    constexpr std::array<std::array<std::uint64_t, 2>, 3> frozen{{
        {UINT64_C(0xf1cf9284505d47aa), UINT64_C(0xaa7564cad19b5acc)},
        {UINT64_C(0x7a17c61df2923342), UINT64_C(0xf222f8dce68e29e4)},
        {UINT64_C(0xb22bc20001e0fbed), UINT64_C(0x5602b3e48fc0055b)},
    }};
    for (int dimension = 1; dimension <= 3; ++dimension) {
        const auto config = make_mixed_config(dimension, true);
        for (int species_case = 0; species_case < 2; ++species_case) {
            const int species = species_case == 0 ? 0 : 2;
            Grid grid(amr::MAX_NG, 0.0, 1.0, 0.0, 1.0, 0.0, 1.0);
            grid.dim = dimension;
            grid.InitializeTopology();
            FluidState state;
            seed_state(state, grid, species);
            const auto enuc_before = state_hash(state, true);
            const auto species_before = values_hash(state.mass_fractions);
            const BCHandler boundary{config};
            boundary.apply(state, grid);
            const auto actual = state_hash(state, false);
            if (actual != frozen[dimension - 1][species_case]) {
                std::cerr << "BCHandler hash mismatch dim=" << dimension
                          << " species=" << species << " actual=0x"
                          << std::hex << actual << " expected=0x"
                          << frozen[dimension - 1][species_case]
                          << " rho=0x" << values_hash(state.rho)
                          << " u=0x" << values_hash(state.mom_u)
                          << " v=0x" << values_hash(state.mom_v)
                          << " w=0x" << values_hash(state.mom_w)
                          << " e=0x" << values_hash(state.eng)
                          << " species_before=0x" << species_before
                          << " species_hash=0x" << values_hash(state.mass_fractions)
                          << std::dec << '\n';
            }
            require(actual == frozen[dimension - 1][species_case],
                    "BCHandler drifted from pre-edit latest-main authority");
            require(state_hash(state, true) == enuc_before,
                    "BCHandler changed enuc_rate");
        }
    }

    auto one_dimensional = make_mixed_config(1, false);
    one_dimensional.grid.x2l_boundary_type = "ignored-invalid-inactive-face";
    one_dimensional.grid.x2r_boundary_type = "also-ignored";
    const BCHandler ignores_inactive_strings{one_dimensional};
    require(ignores_inactive_strings.logical_plan().fingerprint()
                == UINT64_C(0x47898461178fb7ca),
            "BCHandler parsed an inactive axis");

    auto invalid = make_mixed_config(3, false);
    invalid.grid.x1l_boundary_type = "unknown";
    require_rejected([&] { (void)BCHandler{invalid}; },
                     "BCHandler silently accepted an unknown active face");

    const auto config = make_mixed_config(3, false);
    const BCHandler boundary{config};
    auto invalid_grid = make_grid(3);
    FluidState untouched;
    seed_state(untouched, invalid_grid, 2);
    const auto untouched_before = state_hash(untouched, false);
    invalid_grid.stride_y -= 1;
    require_rejected([&] { boundary.apply(untouched, invalid_grid); },
                     "BCHandler accepted a runtime Grid layout drift");
    require(state_hash(untouched, false) == untouched_before,
            "BCHandler validation failure wrote state");
    invalid_grid = make_grid(3);
    invalid_grid.ng = std::numeric_limits<int>::max();
    require_rejected(
        [&] { (void)arch::boundary::host::make_layout(invalid_grid); },
        "Host layout construction overflowed a drifted Grid ghost depth");
}

bool same_slot(const arch::state::SlotCoherence& left,
               const arch::state::SlotCoherence& right)
{
    return left.interior.residency == right.interior.residency
        && left.interior.version == right.interior.version
        && left.interior.completion == right.interior.completion
        && left.interior.pending_transfer == right.interior.pending_transfer
        && left.ghost.residency == right.ghost.residency
        && left.ghost.version == right.ghost.version
        && left.ghost.completion == right.ghost.completion
        && left.ghost.pending_transfer == right.ghost.pending_transfer
        && left.ghost_source_version == right.ghost_source_version;
}

void test_e0_e1_boundary_completion_contract()
{
    using namespace arch::scheduler;
    using namespace arch::state;

    const amr::BlockHandle handle{{1}, {1}};
    const std::array handles{handle};
    const StateKey current{handle, StateSlot::Current};
    const auto config = make_mixed_config(3, false);
    const BCHandler boundary{config};
    auto grid = make_grid(3);

    StateResidencyLedger host_ledger{{1}};
    host_ledger.register_block(handle, {1}, {1, CompletionState::Complete});
    host_ledger.publish_interior(
        current, ExecutionSide::Host, {2}, {2, CompletionState::Complete});
    MonotonicSchedulerClock host_clock{2, 2};
    StageExecutionContext host_context{
        ExecutionSide::Host, host_ledger, host_clock};
    FluidState host_state;
    seed_state(host_state, grid, 2);

    const auto before_executor = host_ledger.inspect(current);
    boundary.apply(host_state, grid);
    require(same_slot(host_ledger.inspect(current), before_executor),
            "E2 Host executor directly changed E0 residency");

    const auto completed = complete_boundary(
        host_context, handles, StateSlot::Current, {2},
        [&](StateSlot slot, StateVersion version, CompletionToken token) {
            require(slot == StateSlot::Current && version == StateVersion{2},
                    "E1 boundary callback received the wrong state identity");
            boundary.apply(host_state, grid);
            return token;
        });
    const auto host_after = host_ledger.inspect(current);
    require(is_complete(completed)
                && host_after.ghost.residency == StateResidency::HostValid
                && host_after.ghost.version == StateVersion{2}
                && host_after.ghost_source_version == StateVersion{2},
            "exact Host boundary completion did not publish matching ghost");

    StateResidencyLedger device_ledger{{1}};
    device_ledger.register_block(handle, {1}, {1, CompletionState::Complete});
    device_ledger.publish_interior(
        current, ExecutionSide::Device, {2}, {2, CompletionState::Complete});
    MonotonicSchedulerClock device_clock{2, 2};
    StageExecutionContext device_context{
        ExecutionSide::Device, device_ledger, device_clock};
    FluidState fake_device_state;
    seed_state(fake_device_state, grid, 2);
    const auto before_fake_device_executor = device_ledger.inspect(current);
    int fake_cuda_calls = 0;
    (void)complete_boundary(
        device_context, handles, StateSlot::Current, {2},
        [&](StateSlot, StateVersion, CompletionToken token) {
            boundary.apply(fake_device_state, grid);
            require(same_slot(
                        device_ledger.inspect(current),
                        before_fake_device_executor),
                    "fake Device executor directly changed E0 residency");
            ++fake_cuda_calls;
            return token;
        });
    const auto device_after = device_ledger.inspect(current);
    require(fake_cuda_calls == 1
                && device_after.ghost.residency == StateResidency::DeviceValid
                && device_after.ghost.version == StateVersion{2}
                && device_after.ghost_source_version == StateVersion{2},
            "fake Device completion did not publish DeviceValid ghost");

    const auto require_failed_completion_is_pure =
        [&](auto&& prepare, auto&& callback, StateVersion version,
            std::string_view message) {
            StateResidencyLedger ledger{{1}};
            ledger.register_block(
                handle, {1}, {1, CompletionState::Complete});
            ledger.publish_interior(
                current, ExecutionSide::Host, {2},
                {2, CompletionState::Complete});
            const std::uint64_t last_token = prepare(ledger);
            MonotonicSchedulerClock clock{last_token, 2};
            StageExecutionContext context{ExecutionSide::Host, ledger, clock};
            const auto before = ledger.inspect(current);
            require_rejected(
                [&] {
                    (void)complete_boundary(
                        context, handles, StateSlot::Current, version,
                        std::forward<decltype(callback)>(callback));
                }, message);
            require(same_slot(ledger.inspect(current), before),
                    "failed boundary completion changed E0 state");
        };

    require_failed_completion_is_pure(
        [](auto&) { return UINT64_C(2); },
        [](StateSlot, StateVersion, CompletionToken) -> CompletionToken {
            throw std::runtime_error("physical boundary failure");
        },
        {2}, "boundary exception was not propagated");
    require_failed_completion_is_pure(
        [](auto&) { return UINT64_C(2); },
        [](StateSlot, StateVersion, CompletionToken token) {
            return CompletionToken{token.value + 1, CompletionState::Complete};
        },
        {2}, "wrong boundary completion token was accepted");
    require_failed_completion_is_pure(
        [](auto&) { return UINT64_C(2); },
        [](StateSlot, StateVersion, CompletionToken token) {
            return CompletionToken{token.value, CompletionState::Pending};
        },
        {2}, "incomplete boundary result was accepted");
    require_failed_completion_is_pure(
        [](auto&) { return UINT64_C(2); },
        [](StateSlot, StateVersion, CompletionToken token) { return token; },
        {3}, "wrong ghost source version was accepted");
    require_failed_completion_is_pure(
        [&](StateResidencyLedger& ledger) {
            ledger.begin_transfer(
                current, StateRegion::Interior,
                PendingTransferPhase::PendingH2D,
                {3, CompletionState::Pending});
            return UINT64_C(3);
        },
        [](StateSlot, StateVersion, CompletionToken token) { return token; },
        {2}, "pending transfer allowed ghost publication");
}
} // namespace

int main()
{
    try {
        test_public_numeric_contract();
        test_frozen_plan_fingerprints();
        test_sources_and_components();
        test_invalid_inputs();
        test_host_lowering_and_executor();
        test_production_bc_handler();
        test_e0_e1_boundary_completion_contract();
        std::cout << "boundary plan contract passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
