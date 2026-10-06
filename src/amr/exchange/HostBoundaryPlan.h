/** @file HostBoundaryPlan.h
 * @brief Lower shared boundary plans to validated host array offsets.
 * Workflow:
 * 1. Validate block layout against the backend-independent logical plan.
 * 2. Compile signs and donor/destination offsets once.
 * 3. Execute phases in the same deterministic order as CUDA.
 */
#pragma once
#include <bit>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>
#include "amr/exchange/BoundaryPlan.h"
#include "data/FluidState.h"
#include "grid/Grid.h"
namespace arch::boundary::host
{
struct HostBoundaryLayout {
    int dimension;
    int active_i;
    int active_j;
    int active_k;
    int ghost_depth;
    int active_origin_i;
    int active_origin_j;
    int active_origin_k;
    int total_x;
    int total_y;
    int total_z;
    int stride_y;
    int stride_z;
    int total_size;

    friend constexpr bool operator==(
        const HostBoundaryLayout&, const HostBoundaryLayout&) = default;
};

struct HostCompiledBoundaryTransfer {
    std::uint64_t logical_ordinal;
    int source_index;
    int destination_index;
    // rho, three momenta, energy, and scalar ENUC diagnostic.
    std::array<std::int8_t, 6> conserved_signs;
    std::int8_t species_sign;
};

struct HostCompiledBoundaryPlan {
    std::uint64_t logical_fingerprint;
    HostBoundaryLayout layout;
    std::array<BoundaryPhase, 3> phases;
    std::vector<HostCompiledBoundaryTransfer> transfers;
};

inline HostBoundaryLayout make_layout(const Grid& grid)
{
    if (grid.dim < 1 || grid.dim > 3 || grid.ng < 0)
        throw std::invalid_argument("Invalid runtime Grid boundary metadata");
    const auto ghost = static_cast<std::int64_t>(grid.ng);
    const auto expected_total_x = static_cast<std::int64_t>(amr::BLOCK_NX)
        + 2 * ghost;
    const auto expected_total_y = grid.dim >= 2
        ? static_cast<std::int64_t>(amr::BLOCK_NY) + 2 * ghost : 1;
    const auto expected_total_z = grid.dim == 3
        ? static_cast<std::int64_t>(amr::BLOCK_NZ) + 2 * ghost : 1;
    constexpr auto int_max = std::numeric_limits<int>::max();
    if (expected_total_x > int_max || expected_total_y > int_max
        || expected_total_z > int_max
        || grid.GetTotalX() != expected_total_x
        || grid.GetTotalY() != expected_total_y
        || grid.GetTotalZ() != expected_total_z)
        throw std::invalid_argument("Runtime Grid extents are not canonical");
    return {
        grid.dim,
        amr::BLOCK_NX, grid.dim >= 2 ? amr::BLOCK_NY : 1,
        grid.dim == 3 ? amr::BLOCK_NZ : 1,
        grid.ng,
        grid.ng, grid.dim >= 2 ? grid.ng : 0,
        grid.dim == 3 ? grid.ng : 0,
        grid.GetTotalX(), grid.GetTotalY(), grid.GetTotalZ(),
        grid.stride_y, grid.stride_z, grid.GetTotalSize()};
}

inline HostBoundaryLayout make_canonical_layout(int dimension)
{
    if (dimension < 1 || dimension > 3)
        throw std::invalid_argument("Boundary dimension must be in [1,3]");
    const int total_y = dimension >= 2
        ? amr::BLOCK_NY + 2 * amr::MAX_NG : 1;
    const int total_z = dimension == 3
        ? amr::BLOCK_NZ + 2 * amr::MAX_NG : 1;
    return {
        dimension,
        amr::BLOCK_NX, dimension >= 2 ? amr::BLOCK_NY : 1,
        dimension == 3 ? amr::BLOCK_NZ : 1,
        amr::MAX_NG,
        amr::MAX_NG, dimension >= 2 ? amr::MAX_NG : 0,
        dimension == 3 ? amr::MAX_NG : 0,
        amr::BLOCK_NX + 2 * amr::MAX_NG, total_y, total_z,
        amr::PAD_NX, amr::PAD_NX * total_y,
        amr::PAD_NX * total_y * total_z};
}

inline void validate_layout(
    const BoundaryPlan& plan, const HostBoundaryLayout& layout)
{
    const auto& input = plan.input();
    if (layout.dimension != input.dimension
        || layout.active_i != input.active_extent[0]
        || layout.active_j != input.active_extent[1]
        || layout.active_k != input.active_extent[2]
        || layout.ghost_depth != static_cast<int>(input.ghost_depth)
        || layout.total_x <= 0 || layout.total_y <= 0 || layout.total_z <= 0
        || layout.stride_y < layout.total_x || layout.stride_z <= 0
        || layout.total_size <= 0
        || !arch::boundary::detail::contains_active_region(
            layout.active_origin_i, layout.active_i, layout.ghost_depth,
            layout.total_x)
        || !arch::boundary::detail::contains_active_region(
            layout.active_origin_j, layout.active_j,
            layout.dimension >= 2 ? layout.ghost_depth : 0,
            layout.total_y)
        || !arch::boundary::detail::contains_active_region(
            layout.active_origin_k, layout.active_k,
            layout.dimension == 3 ? layout.ghost_depth : 0,
            layout.total_z)
        || static_cast<std::int64_t>(layout.stride_y) * layout.total_y
            > layout.stride_z
        || static_cast<std::int64_t>(layout.stride_z) * layout.total_z
            > layout.total_size)
        throw std::invalid_argument("Host boundary layout does not match logical plan");
}

inline int flatten_checked(
    const HostBoundaryLayout& layout, LogicalCellRef logical)
{
    const std::int64_t i = static_cast<std::int64_t>(layout.active_origin_i)
        + logical.i;
    const std::int64_t j = static_cast<std::int64_t>(layout.active_origin_j)
        + logical.j;
    const std::int64_t k = static_cast<std::int64_t>(layout.active_origin_k)
        + logical.k;
    if (i < 0 || i >= layout.total_x || j < 0 || j >= layout.total_y
        || k < 0 || k >= layout.total_z)
        throw std::out_of_range("Boundary logical coordinate is outside Host layout");
    const std::int64_t index = k * layout.stride_z + j * layout.stride_y + i;
    if (index < 0 || index >= layout.total_size
        || index > std::numeric_limits<int>::max())
        throw std::overflow_error("Boundary Host flattened index is invalid");
    return static_cast<int>(index);
}

/** Compile one original logical mapping into validated Host offsets/signs.
 * Ordinary and RZ-corner callers share the same flattening/component mapping.
 */
inline HostCompiledBoundaryTransfer compile_transfer(
    const BoundaryOperation& operation, const HostBoundaryLayout& layout)
{
    HostCompiledBoundaryTransfer transfer{};
    transfer.logical_ordinal = operation.ordinal;
    transfer.source_index = flatten_checked(layout, operation.source);
    transfer.destination_index = flatten_checked(layout, operation.destination);
    for (std::uint8_t field = 0; field < 5; ++field) {
        transfer.conserved_signs[field] = component_mapping(
            operation, static_cast<BoundaryFieldClass>(field)).sign;
    }
    transfer.conserved_signs[5] = component_mapping(
        operation, BoundaryFieldClass::AllSpecies).sign;
    transfer.species_sign = component_mapping(
        operation, BoundaryFieldClass::AllSpecies).sign;
    return transfer;
}

inline HostCompiledBoundaryPlan compile(
    const BoundaryPlan& plan, const HostBoundaryLayout& layout)
{
    validate_layout(plan, layout);
    HostCompiledBoundaryPlan compiled{};
    compiled.logical_fingerprint = plan.fingerprint();
    compiled.layout = layout;
    for (std::size_t phase = 0; phase < compiled.phases.size(); ++phase)
        compiled.phases[phase] = plan.phases()[phase];
    compiled.transfers.reserve(plan.operations().size());
    for (const auto& operation : plan.operations()) {
        const auto transfer = compile_transfer(operation, layout);
        compiled.transfers.push_back(transfer);
    }
    return compiled;
}

inline void validate_state(
    const HostCompiledBoundaryPlan& compiled, const FluidState& state)
{
    const auto size = static_cast<std::size_t>(compiled.layout.total_size);
    const auto n_species = state.GetNumSpecies();
    if (state.rho.size() != size || state.mom_u.size() != size
        || state.mom_v.size() != size || state.mom_w.size() != size
        || state.eng.size() != size || state.enuc_rate.size() != size
        || n_species < 0
        || (n_species != 0
            && size > std::numeric_limits<std::size_t>::max()
                / static_cast<std::size_t>(n_species))
        || state.mass_fractions.size()
            != size * static_cast<std::size_t>(n_species))
        throw std::invalid_argument("FluidState does not match compiled boundary layout");
}

inline void validate_compiled_plan(const HostCompiledBoundaryPlan& compiled)
{
    if (compiled.transfers.size()
        > static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max()))
        throw std::invalid_argument("Host boundary transfer count is not executable");

    std::size_t expected_first = 0;
    for (std::size_t phase_index = 0;
         phase_index < compiled.phases.size(); ++phase_index) {
        const auto& phase = compiled.phases[phase_index];
        if (phase.id != static_cast<BoundaryPhaseId>(phase_index)
            || phase.first != expected_first
            || phase.first > compiled.transfers.size()
            || phase.count > compiled.transfers.size() - phase.first)
            throw std::invalid_argument("Invalid Host boundary phase metadata");
        expected_first += phase.count;
    }
    if (expected_first != compiled.transfers.size())
        throw std::invalid_argument("Host boundary phases do not cover transfers");

    for (std::size_t index = 0; index < compiled.transfers.size(); ++index) {
        const auto& transfer = compiled.transfers[index];
        const auto valid_sign = [](std::int8_t sign) {
            return sign == -1 || sign == 1;
        };
        bool signs_valid = valid_sign(transfer.species_sign);
        for (const auto sign : transfer.conserved_signs)
            signs_valid = signs_valid && valid_sign(sign);
        if (transfer.logical_ordinal != index
            || transfer.source_index < 0
            || transfer.source_index >= compiled.layout.total_size
            || transfer.destination_index < 0
            || transfer.destination_index >= compiled.layout.total_size
            || !signs_valid)
            throw std::invalid_argument("Invalid Host boundary transfer metadata");
    }
}

inline double signed_copy(double value, std::int8_t sign) noexcept
{
    return sign == -1 ? -value : value;
}

/** Copy one already validated logical transfer with its compiled field signs.
 * Both complete and axis-only executors own preflight and phase barriers.
 * RZ axis parity reuses the logical mapping: m_r/m_phi odd, all other fields
 * even; this leaf contains no separate geometry or sign policy.
 */
inline void execute_transfer(const HostCompiledBoundaryTransfer& transfer,
    FluidState& state, int n_species)
{
    const int source = transfer.source_index;
    const int destination = transfer.destination_index;
    state.rho[destination] = signed_copy(
        state.rho[source], transfer.conserved_signs[0]);
    state.mom_u[destination] = signed_copy(
        state.mom_u[source], transfer.conserved_signs[1]);
    state.mom_v[destination] = signed_copy(
        state.mom_v[source], transfer.conserved_signs[2]);
    state.mom_w[destination] = signed_copy(
        state.mom_w[source], transfer.conserved_signs[3]);
    state.eng[destination] = signed_copy(
        state.eng[source], transfer.conserved_signs[4]);
    state.enuc_rate[destination] = signed_copy(
        state.enuc_rate[source], transfer.conserved_signs[5]);
    for (int species = 0; species < n_species; ++species) {
        state.X(species, destination) = signed_copy(
            state.X(species, source), transfer.species_sign);
    }
}

inline void execute(
    const HostCompiledBoundaryPlan& compiled, FluidState& state)
{
    validate_compiled_plan(compiled);
    validate_state(compiled, state);
    const int n_species = state.GetNumSpecies();
#pragma omp parallel
    {
        for (std::size_t phase_index = 0;
             phase_index < compiled.phases.size(); ++phase_index) {
            const auto phase = compiled.phases[phase_index];
#pragma omp for schedule(static)
            for (std::ptrdiff_t offset = 0;
                 offset < static_cast<std::ptrdiff_t>(phase.count); ++offset) {
                const auto& transfer = compiled.transfers[phase.first + offset];
                execute_transfer(transfer, state, n_species);
            }
        }
    }
}
/** Complete the original RZ axis parity once, including axial ghost corners.
 * Workflow: authenticate the full logical/compiled binding, build and validate
 * shared logical corner transfers before writing, execute only original axis
 * transfers, then the corner list. No Y/Z or ordinary outer radial BC executes.
 * Field copies/signs are identical to execute: m_r/m_phi odd, others even.
 * The caller owns completed positive-r axial ghosts and final EOS/publication.
 */
inline void execute_rz_axis(const BoundaryPlan& logical,
    const HostCompiledBoundaryPlan& compiled, FluidState& state)
{
    validate_compiled_plan(compiled);
    validate_state(compiled,state);
    validate_layout(logical,compiled.layout);
    if (logical.fingerprint() != compiled.logical_fingerprint
        || logical.operations().size() != compiled.transfers.size())
        throw std::invalid_argument("RZ axis logical/compiled plan identity mismatch");
    for (std::size_t phase = 0;phase < compiled.phases.size();++phase)
        if (logical.phases()[phase] != compiled.phases[phase])
            throw std::invalid_argument("RZ axis logical/compiled phase mismatch");
    // A fingerprint is not enough to authenticate a mutable compiled value.
    // Validate every actual source/destination/sign against its logical owner.
    for (std::size_t ordinal = 0;ordinal < compiled.transfers.size();++ordinal) {
        const auto expected = compile_transfer(logical.operations()[ordinal],compiled.layout);
        const auto& actual = compiled.transfers[ordinal];
        if (expected.logical_ordinal != actual.logical_ordinal
            || expected.source_index != actual.source_index
            || expected.destination_index != actual.destination_index
            || expected.conserved_signs != actual.conserved_signs
            || expected.species_sign != actual.species_sign)
            throw std::invalid_argument("RZ axis compiled transfer differs from logical owner");
    }
    const auto corners = rz_axis_corner_operations(logical);
    HostCompiledBoundaryPlan corner_compiled{};
    corner_compiled.logical_fingerprint = logical.fingerprint();
    corner_compiled.layout = compiled.layout;
    corner_compiled.phases = {BoundaryPhase{BoundaryPhaseId::X,0,corners.size()},
        BoundaryPhase{BoundaryPhaseId::Y,corners.size(),0},
        BoundaryPhase{BoundaryPhaseId::Z,corners.size(),0}};
    corner_compiled.transfers.reserve(corners.size());
    for (const auto& corner : corners)
        corner_compiled.transfers.push_back(compile_transfer(corner,compiled.layout));
    validate_compiled_plan(corner_compiled);
    validate_state(corner_compiled,state);
    const int species = state.GetNumSpecies();
#pragma omp parallel
    {
#pragma omp for schedule(static)
        for (std::ptrdiff_t ordinal = 0;
             ordinal < static_cast<std::ptrdiff_t>(compiled.transfers.size());++ordinal)
            if (logical.operations()[static_cast<std::size_t>(ordinal)].type == BoundaryType::RzAxis)
                execute_transfer(compiled.transfers[static_cast<std::size_t>(ordinal)],state,species);
        // The preceding implicit barrier completes all interior-row axis
        // writes before the distinct corner destination list is published.
#pragma omp for schedule(static)
        for (std::ptrdiff_t ordinal = 0;
             ordinal < static_cast<std::ptrdiff_t>(corner_compiled.transfers.size());++ordinal)
            execute_transfer(corner_compiled.transfers[static_cast<std::size_t>(ordinal)],state,species);
    }
}
} // namespace arch::boundary::host
