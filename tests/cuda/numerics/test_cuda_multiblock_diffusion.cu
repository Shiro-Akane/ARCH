/**
 * @file test_cuda_multiblock_diffusion.cu
 * @brief Compare scalar and batched production CUDA RKL execution bitwise.
 *
 * Both RKL1 and RKL2 use the production backend so stage storage and block
 * exchange are tested together with the shared diffusion operator. Independent
 * CPU/analytic comparisons live in the canonical application/leaf regressions.
 * The default two-block/two-species invocation also checks genuine periodic
 * longitudinal and transverse Stokes modes with an independent Legendre oracle.
 */
#include "amr/storage/Block.h"
#include "amr/exchange/BoundaryPlan.h"
#include "amr/exchange/ExchangePlan.h"
#include "cuda/runtime/CudaBackend.h"
#include "numerics/diffusion/DiffFunction.h"
#include "physics/eos/IdealGas.h"
#include "physics/species/Species.h"

#include <array>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using arch::state::StateSlot;
int test_species_count = 2;

enum class PhysicalMode { None, Longitudinal, Transverse };
constexpr double mode_amplitude = 0.05;

/** Independent scalar Legendre polynomial, not the production RKL coefficients.
 * Workflow: evaluate P_0=1, P_1=x and the mathematical three-term recurrence.
 * The actual selected stage count and actual dt determine the final argument.
 */
long double legendre(int stages, long double argument)
{
    long double previous = 1.0L, current = argument;
    for (int degree = 2; degree <= stages; ++degree) {
        const long double next = ((2.0L*degree-1.0L)*argument*current
            -(degree-1.0L)*previous)/degree;
        previous = current;
        current = next;
    }
    return stages == 0 ? previous : current;
}

struct ModeTotals {
    long double energy = 0.0L;
    long double kinetic = 0.0L;
};

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

arch::boundary::BoundaryPlan make_boundary_plan(PhysicalMode mode = PhysicalMode::None)
{
    using namespace arch::boundary;
    BoundaryPlanInput input{};
    input.dimension = 1;
    input.active_extent = {amr::BLOCK_NX, 1, 1};
    input.ghost_depth = amr::MAX_NG;
    input.faces.fill(BoundaryType::Inactive);
    input.faces[face_index(BoundaryAxis::X1, BoundarySide::Lower)] =
        mode == PhysicalMode::None ? BoundaryType::Outflow : BoundaryType::Periodic;
    input.faces[face_index(BoundaryAxis::X1, BoundarySide::Upper)] =
        mode == PhysicalMode::None ? BoundaryType::Outflow : BoundaryType::Periodic;
    return arch::boundary::make_boundary_plan(input);
}

SpeciesManager make_species()
{
    SpeciesManager species;
    species.add_species("a", 1.0, 1.0, 1.4, 3.5);
    species.add_species("b", 4.0, 2.0, 1.5, 7.25);
    for (int s = 2; s < test_species_count; ++s)
        species.add_species("inactive" + std::to_string(s), 1.0, 1.0, 1.4, 3.5);
    return species;
}

amr::Block make_block(int id, PhysicalMode mode = PhysicalMode::None)
{
    amr::Block block{};
    block.id = id;
    block.level = 0;
    block.logical_x1 = id;
    block.active = true;
    block.grid = Grid(
        amr::MAX_NG, static_cast<double>(id), static_cast<double>(id + 1),
        0.0, 1.0, 0.0, 1.0, 1, 0, 0);
    block.grid.dim = 1;
    block.grid.geometry = "cartesian";
    block.grid.InitializeTopology();
    const int total = block.grid.GetTotalSize();
    for (FluidState* state : {
             &block.fluid_state, &block.state_next, &block.state_scratch}) {
        state->Preallocate(total);
        state->InitSpecies(test_species_count);
    }
    for (int i = 0; i < block.grid.GetTotalX(); ++i) {
        const int cell = block.grid.GetIndex(i);
        if (mode != PhysicalMode::None) {
            // Only initialization samples the physical cosine. Every subsequent
            // halo comes from the real periodic BC and actual neighboring block.
            const double velocity = mode_amplitude
                * std::cos(std::numbers::pi * block.grid.GetCellCenterX(i));
            block.fluid_state.set(cell, {1.0,
                mode == PhysicalMode::Longitudinal ? velocity : 0.0,
                mode == PhysicalMode::Transverse ? velocity : 0.0,
                0.0, 12.0 + 0.5*velocity*velocity});
            block.fluid_state.X(0, cell) = 0.35;
            block.fluid_state.X(1, cell) = 0.65;
            block.fluid_state.enuc_rate[cell] = (i & 1) ? 0.0 : -0.0;
            continue;
        }
        const double global_x = static_cast<double>((id % 2) * amr::BLOCK_NX
            + i - block.grid.Is());
        const double rho = 1.0 + 0.002 * global_x;
        const double u = 0.015 + 1.0e-4 * global_x * global_x;
        const double v = -0.0075 + 7.5e-5 * global_x;
        block.fluid_state.set(
            cell, {rho, rho * u, rho * v, 0.0,
                   rho * (12.0 + 2.0e-4 * global_x * global_x)});
        const double x0 = 0.35 + 0.002 * global_x;
        block.fluid_state.X(0, cell) = x0;
        block.fluid_state.X(1, cell) = 1.0 - x0;
        for (int s = 2; s < test_species_count; ++s) block.fluid_state.X(s, cell) = 0.0;
        block.fluid_state.enuc_rate[cell] = (i & 1) ? 0.0 : -0.0;
    }
    return block;
}

arch::backend::HostStateTransferView transfer_view(FluidState& state)
{
    return {
        state.rho.data(), state.mom_u.data(), state.mom_v.data(),
        state.mom_w.data(), state.eng.data(), state.enuc_rate.data(),
        state.mass_fractions.data(), state.rho.size(), test_species_count, state.rho.size()};
}

arch::cuda::CudaLaunchConfig make_launch_config(
    DiffFunction::RKLOrder order)
{
    SimConfig config{};
    config.physics.burn.use_burn = false;
    config.physics.diffusion.use_diffusion = true;
    config.physics.diffusion.use_thermal_diffusion = true;
    config.physics.diffusion.use_viscous_diffusion = true;
    config.physics.diffusion.use_species_diffusion = true;
    config.physics.diffusion.alpha_therm = 0.125;
    config.physics.diffusion.nu_visc = 0.03125;
    config.physics.diffusion.D_spec = 0.015625;
    config.physics.diffusion.diff_cfl = 0.8;
    config.physics.diffusion.max_stages = 17;
    const arch::dispatch::ResolvedExecutionPlan plan{
        arch::dispatch::FluxId::Hllc,
        arch::dispatch::ReconstructionId::Ppm,
        arch::dispatch::LimiterId::MinMod,
        arch::dispatch::TimeIntegratorId::Euler,
        arch::dispatch::EosId::Ideal,
        arch::dispatch::NetworkId::None,
        arch::dispatch::OdeSolverId::None,
        arch::dispatch::LinearSolverId::None,
        order == DiffFunction::RKLOrder::First
            ? arch::dispatch::DiffusionIntegratorId::Rkl1
            : arch::dispatch::DiffusionIntegratorId::Rkl2};
    return arch::cuda::make_cuda_launch_config(plan, config);
}

amr::SameLevelExchangePlan make_exchange_plan(
    std::array<amr::BlockHandle, 2> handles, PhysicalMode mode = PhysicalMode::None)
{
    const amr::LogicalBlockKey left{1, 0, 0, 0, 0};
    const amr::LogicalBlockKey right{1, 0, 1, 0, 0};
    std::array<amr::SameLevelTopologyEntry, 2> topology{};
    topology[0].logical = left;
    topology[0].handle = handles[0];
    topology[0].neighbors[1] = right;
    topology[1].logical = right;
    topology[1].handle = handles[1];
    topology[1].neighbors[0] = left;
    if (mode != PhysicalMode::None) {
        topology[0].neighbors[0] = right;
        topology[1].neighbors[1] = left;
    }
    return amr::make_same_level_exchange_plan(
        topology, 1, {amr::BLOCK_NX, 1, 1}, amr::MAX_NG,
        handles[0].epoch);
}

/** Independent periodic Stokes-mode and physical energy/work checks.
 * Workflow: the closed constant-rho Cartesian operator has discrete cosine
 * eigenvalue -4*a*nu*sin^2(pi*h/2)/h^2, a=4/3 longitudinal or 1 transverse.
 * Evaluate the Legendre stability polynomial independently, then compare real
 * active momentum, K, total E and E-K after the genuine backend RKL lane.
 * This grants no mixed-AMR, nonlinear thermal or general RKL2 qualification.
 */
void verify_physical_mode(DiffFunction::RKLOrder order, PhysicalMode mode,
    const std::vector<amr::Block>& blocks, const std::vector<FluidState>& result,
    double dt, double dt_fe, int stages, ModeTotals initial)
{
    require(blocks.size() == 2 && result.size() == 2 && stages >= 2,
        "Periodic physical mode lost its actual domain or stage count");
    const long double h = blocks[0].grid.dx1;
    require(h > 0.0L && h == blocks[1].grid.dx1,
        "Periodic mode requires its actual uniform Cartesian spacing");
    const long double pi = std::numbers::pi_v<long double>;
    const long double wave = std::sin(0.5L*pi*h);
    const long double a = mode == PhysicalMode::Longitudinal ? 4.0L/3.0L : 1.0L;
    const long double z = -4.0L*a*0.03125L*wave*wave*dt/(h*h);
    const long double denominator = static_cast<long double>(stages)*(stages+1);
    long double amplification = 0.0L;
    if (order == DiffFunction::RKLOrder::First)
        amplification = legendre(stages, 1.0L+2.0L*z/denominator);
    else {
        const long double b = (denominator-2.0L)/(2.0L*denominator);
        amplification = 1.0L-b+b*legendre(stages, 1.0L+4.0L*z/(denominator-2.0L));
    }
    require(std::isfinite(amplification), "Independent RKL mode reference is invalid");
    const auto portable = [] (double actual, long double expected) {
        require(std::isfinite(actual) && std::isfinite(expected),
            "Periodic momentum comparison received non-finite data");
        const long double error = std::abs(static_cast<long double>(actual)-expected);
        require(error <= 8.0e-15L || error <= 8.0e-16L*std::abs(expected),
            "Periodic momentum failed the original RklPortable OR budget");
    };
    const long double epsilon = 64.0L*std::numeric_limits<double>::epsilon();
    const auto summed = [&] (long double actual, long double expected) {
        require(std::isfinite(actual) && std::isfinite(expected)
            && std::abs(actual-expected) <= epsilon*std::abs(expected),
            "Periodic physical energy failed its original 64-epsilon scale budget");
    };
    ModeTotals actual{};
    long double expected_kinetic = 0.0L;
    for (std::size_t block = 0; block < blocks.size(); ++block) {
        const auto& grid = blocks[block].grid;
        const auto& state = result[block];
        for (int i = grid.Is(); i < grid.Ie(); ++i) {
            const int cell = grid.GetIndex(i);
            const long double reference = static_cast<long double>(mode_amplitude)
                *std::cos(pi*static_cast<long double>(grid.GetCellCenterX(i)))*amplification;
            const auto value = state.get(cell);
            require(value.rho == 1.0, "Periodic diffusion changed stationary density");
            portable(value.mom_u, mode == PhysicalMode::Longitudinal ? reference : 0.0L);
            portable(value.mom_v, mode == PhysicalMode::Transverse ? reference : 0.0L);
            portable(value.mom_w, 0.0L);
            portable(state.X(0,cell), static_cast<long double>(0.35));
            portable(state.X(1,cell), static_cast<long double>(0.65));
            require(state.X(0,cell) > 0.0 && state.X(1,cell) > 0.0
                && std::abs(static_cast<long double>(state.X(0,cell))+state.X(1,cell)-1.0L) <= epsilon,
                "Periodic mode lost its normalized positive species");
            actual.energy += h*value.eng;
            actual.kinetic += h*(static_cast<long double>(value.mom_u)*value.mom_u
                +static_cast<long double>(value.mom_v)*value.mom_v
                +static_cast<long double>(value.mom_w)*value.mom_w)/(2.0L*value.rho);
            expected_kinetic += 0.5L*h*reference*reference;
        }
    }
    // Both joins, including the physical periodic end, must be actual current
    // neighbor copies for every conserved/species/ENUC component.
    for (std::size_t destination = 0; destination < 2; ++destination)
        for (int depth = 0; depth < amr::MAX_NG; ++depth)
            for (int side = 0; side < 2; ++side) {
                const auto source = 1-destination;
                const int dst = blocks[destination].grid.GetIndex(side == 0
                    ? blocks[destination].grid.Is()-amr::MAX_NG+depth
                    : blocks[destination].grid.Ie()+depth);
                const int src = blocks[source].grid.GetIndex(side == 0
                    ? blocks[source].grid.Ie()-amr::MAX_NG+depth
                    : blocks[source].grid.Is()+depth);
                for (const auto member : {&FluidState::rho,&FluidState::mom_u,&FluidState::mom_v,
                    &FluidState::mom_w,&FluidState::eng,&FluidState::enuc_rate})
                    require(std::bit_cast<std::uint64_t>((result[destination].*member)[dst])
                        ==std::bit_cast<std::uint64_t>((result[source].*member)[src]),
                        "Periodic final halo is not the real neighbor state");
                for (int species = 0; species < 2; ++species)
                    require(std::bit_cast<std::uint64_t>(result[destination].X(species,dst))
                        ==std::bit_cast<std::uint64_t>(result[source].X(species,src)),
                        "Periodic final species halo is not the real neighbor state");
            }
    summed(actual.energy, initial.energy);
    summed(actual.kinetic, expected_kinetic);
    summed(actual.energy-actual.kinetic, initial.energy-expected_kinetic);
    require(actual.kinetic <= initial.kinetic+epsilon*std::abs(initial.kinetic),
        "Periodic Stokes RKL mode increased physical kinetic energy");
    const auto original_precision = std::cout.precision();
    std::cout << std::setprecision(std::numeric_limits<long double>::max_digits10)
        << "CUDA_RKL_PHYSICAL_MODE_PASS scope=UniformCartesianPeriodicOnly mode="
        << (mode == PhysicalMode::Longitudinal ? "longitudinal" : "transverse")
        << " order=" << (order == DiffFunction::RKLOrder::First ? 1 : 2)
        << " dt=" << dt << " dt_fe=" << dt_fe << " stages=" << stages
        << " amplification=" << amplification << " total_E=" << actual.energy
        << " K=" << actual.kinetic << " internal=" << actual.energy-actual.kinetic << '\n';
    std::cout.precision(original_precision);
}

std::vector<FluidState> run_route(DiffFunction::RKLOrder order, bool batched, std::size_t count = 2,
    PhysicalMode mode = PhysicalMode::None)
{
    require(mode == PhysicalMode::None || (count == 2 && test_species_count == 2),
        "Physical mode requires the authentic two-block/two-species domain");
    std::vector<amr::Block> blocks;
    std::vector<amr::BlockHandle> handles;
    std::vector<arch::backend::StorageGeneration> storage;
    blocks.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        blocks.push_back(make_block(static_cast<int>(i), mode));
        handles.push_back({{301 + i}, {11}});
        storage.push_back({401 + i});
    }
    ModeTotals initial{};
    if (mode != PhysicalMode::None)
        for (const auto& block : blocks)
            for (int i = block.grid.Is(); i < block.grid.Ie(); ++i) {
                const int cell = block.grid.GetIndex(i);
                const long double volume = block.grid.dx1;
                const auto value = block.fluid_state.get(cell);
                initial.energy += volume*value.eng;
                initial.kinetic += volume*(static_cast<long double>(value.mom_u)*value.mom_u
                    +static_cast<long double>(value.mom_v)*value.mom_v
                    +static_cast<long double>(value.mom_w)*value.mom_w)/(2.0L*value.rho);
            }
    const auto boundary_plan = make_boundary_plan(mode);
    std::vector<arch::cuda::CudaBlockBinding> bindings(count);
    for (std::size_t index = 0; index < bindings.size(); ++index)
        bindings[index] = {
            &blocks[index], handles[index], storage[index], &boundary_plan};
    SpeciesManager species = make_species();
    IdealGas eos(1.4, species);
    auto backend = arch::cuda::make_cuda_backend(
        bindings, 0, make_launch_config(order), species, eos);
    std::vector<arch::backend::BackendStateAccess> current(count);
    arch::state::StateResidencyLedger ledger({11});
    for (std::size_t index = 0; index < current.size(); ++index) {
        current[index] = {handles[index], storage[index], StateSlot::Current};
        ledger.register_block(
            handles[index], {1}, {1, arch::state::CompletionState::Complete});
        ledger.publish_ghost(
            {handles[index], StateSlot::Current},
            arch::state::ExecutionSide::Host, {1},
            {2, arch::state::CompletionState::Complete});
    }
    arch::scheduler::MonotonicSchedulerClock clock(2, 1);
    for (std::size_t index = 0; index < current.size(); ++index)
        (void)arch::backend::transfer_state_regions(
            *backend, ledger, clock, current[index],
            transfer_view(blocks[index].fluid_state),
            arch::state::PendingTransferPhase::PendingH2D);
    arch::scheduler::StageExecutionContext context{
        arch::state::ExecutionSide::Device, ledger, clock};
    // Capacity scans leave blocks beyond the first pair as isolated outflow
    // blocks. This checks kernels/storage, not a global mesh convergence claim.
    const auto exchange_plan = make_exchange_plan({handles[0], handles[1]}, mode);
    const auto waves = test_species_count > 30 ? count : (count + 1023) / 1024;

    std::uint64_t exchange_count = 0;
    const auto boundary = [&] (
        StateSlot slot, arch::state::StateVersion version,
        arch::state::CompletionToken token) {
        std::vector<arch::backend::BackendStateAccess> selected(count);
        for (std::size_t index = 0; index < current.size(); ++index) {
            selected[index] = current[index];
            selected[index].slot = slot;
            (void)backend->execute_physical_boundary(
                selected[index], version, token);
        }
        ++exchange_count;
        return backend->execute_same_level_exchange(
            {selected.data(), 2}, exchange_plan, slot, version, token);
    };
    if (mode != PhysicalMode::None) {
        // True completed domain BC/exchange precedes actual Device dt advice.
        (void)arch::scheduler::complete_boundary(
            context, handles, StateSlot::Current, {1}, boundary);
        require(exchange_count == 1, "Periodic initial boundary did not complete");
        exchange_count = 0;
    }

    const auto before_dt = backend->counters();
    auto dts = batched ? backend->compute_diffusion_dt_batch(current) : std::vector<double>{};
    if (!batched) for (const auto access : current) dts.push_back(backend->compute_diffusion_dt(access));
    require(backend->counters().stream_sync_count - before_dt.stream_sync_count == (batched ? 1 : count),
        "Diffusion dt completion was not batched");
    require(backend->counters().kernel_count - before_dt.kernel_count == (batched ? 3 * waves : 3 * count),
        "Diffusion dt kernels were not batched");
    const double dt_fe = *std::min_element(dts.begin(), dts.end());
    require(std::isfinite(dt_fe) && dt_fe > 0.0,
            "multi-block diffusion dt is invalid");
    const double dt = 2.0 * dt_fe;
    const int stages = DiffFunction::compute_stages(order, dt, dt_fe, 0.8, 17);
    require(stages >= 2, "multi-block diffusion fixture did not reach stage two");

    const auto copy = [&](StateSlot destination) {
        (void)arch::scheduler::copy_slot(
            context, handles, StateSlot::Current, destination, [&] {
                const auto before = backend->counters();
                if (batched) backend->copy_state_slot_batch(current, destination);
                else
                for (std::size_t index = 0; index < current.size(); ++index) {
                    auto target = current[index];
                    target.slot = destination;
                    backend->copy_state_slot(current[index], target);
                }
                require(backend->counters().stream_sync_count - before.stream_sync_count == (batched ? 1 : count),
                    "Diffusion slot copy completion was not batched");
                require(backend->counters().kernel_count - before.kernel_count == (batched ? (count + 1023) / 1024 : 0),
                    "Diffusion slot copy was not fused");
            });
    };
    copy(StateSlot::Scratch);
    copy(StateSlot::Next);

    const auto executor = [&] (
        const arch::scheduler::RklPlan& plan,
        const arch::scheduler::RklStageDescriptor& descriptor,
        arch::state::CompletionToken token) {
        const auto before = backend->counters();
        if (batched) (void)backend->execute_diffusion_stage_batch(current, plan, descriptor, dt, dt_fe, token);
        else
        for (const auto& access : current)
            (void)backend->execute_diffusion_stage(
                access, plan, descriptor, dt, dt_fe, token);
        require(backend->counters().stream_sync_count - before.stream_sync_count == (batched ? 1 : count),
            "Diffusion stage completion was not batched");
        require(backend->counters().kernel_count - before.kernel_count == (batched ? 5 * waves : 5 * count),
            "Diffusion stage kernels were not batched");
        return token;
    };
    const auto rotation = [&] (arch::state::SlotRotation value) {
        for (const auto& access : current) backend->rotate_slots(access, value);
    };
    const auto reflux = [] (
        const arch::scheduler::RklPlan&,
        const arch::scheduler::RklStageDescriptor&,
        arch::state::CompletionToken token) { return token; };
    if (order == DiffFunction::RKLOrder::First)
        (void)arch::scheduler::execute_single_rkl1_lane(
            context, handles, stages, executor, reflux, boundary, rotation);
    else
        (void)arch::scheduler::execute_single_rkl2_lane(
            context, handles, stages, executor, reflux, boundary, rotation);
    require(exchange_count == static_cast<std::uint64_t>(stages),
            "multi-block RKL did not exchange after every stage");

    std::vector<FluidState> downloaded(count);
    for (std::size_t index = 0; index < current.size(); ++index) {
        downloaded[index].Preallocate(blocks[index].grid.GetTotalSize());
        downloaded[index].InitSpecies(test_species_count);
        (void)arch::backend::transfer_state_regions(
            *backend, ledger, clock, current[index], transfer_view(downloaded[index]),
            arch::state::PendingTransferPhase::PendingD2H);
    }
    for (int depth = 0; depth < amr::MAX_NG; ++depth) {
        const int left_active = blocks[0].grid.GetIndex(
            blocks[0].grid.Ie() - amr::MAX_NG + depth);
        const int right_ghost = blocks[1].grid.GetIndex(
            blocks[1].grid.Is() - amr::MAX_NG + depth);
        const int right_active = blocks[1].grid.GetIndex(
            blocks[1].grid.Is() + depth);
        const int left_ghost = blocks[0].grid.GetIndex(
            blocks[0].grid.Ie() + depth);
        require(std::bit_cast<std::uint64_t>(downloaded[1].eng[right_ghost])
                    == std::bit_cast<std::uint64_t>(downloaded[0].eng[left_active]),
                "RKL right ghost is not version-matched to its neighbor");
        require(std::bit_cast<std::uint64_t>(downloaded[0].X(0, left_ghost))
                    == std::bit_cast<std::uint64_t>(downloaded[1].X(0, right_active)),
                "RKL left species ghost is not version-matched to its neighbor");
    }
    for (const auto& state : downloaded)
        for (double value : state.eng)
            require(std::isfinite(value), "multi-block RKL produced non-finite energy");
    if (mode != PhysicalMode::None)
        verify_physical_mode(order, mode, blocks, downloaded, dt, dt_fe, stages, initial);
    std::cout << "CUDA_MULTIBLOCK_DIFFUSION_PASS order="
              << (order == DiffFunction::RKLOrder::First ? 1 : 2)
              << " stages=" << stages << " exchanges=" << exchange_count
              << " blocks=" << count << " species=" << test_species_count << '\n';
    return downloaded;
}

} // namespace

int main(int argc, char** argv)
{
    try {
        std::size_t count = 2;
        require(argc <= 3, "diffusion test accepts block/species counts");
        if (argc >= 2) {
            const std::string arg = argv[1];
            std::size_t used = 0;
            count = std::stoull(arg, &used);
            require(used == arg.size() && count >= 2 && count <= 1025, "block count must be 2..1025");
        }
        if (argc == 3) {
            const std::string arg = argv[2];
            std::size_t used = 0;
            test_species_count = std::stoi(arg, &used);
            require(used == arg.size() && test_species_count >= 2 && test_species_count <= 33,
                    "species count must be 2..33");
        }
        for (const auto order : {DiffFunction::RKLOrder::First, DiffFunction::RKLOrder::Second}) {
            const auto sequential = run_route(order, false, count);
            const auto batched = run_route(order, true, count);
            for (std::size_t i = 0; i < sequential.size(); ++i)
                for (const auto member : {&FluidState::rho, &FluidState::mom_u, &FluidState::mom_v,
                     &FluidState::mom_w, &FluidState::eng, &FluidState::enuc_rate, &FluidState::mass_fractions}) {
                    const auto& a = sequential[i].*member;
                    const auto& b = batched[i].*member;
                    require(a.size() == b.size(), "Batch changed storage size");
                    for (std::size_t j = 0; j < a.size(); ++j)
                        require(std::bit_cast<std::uint64_t>(a[j]) == std::bit_cast<std::uint64_t>(b[j]),
                            "Diffusion batch changed field bits");
                }
        }
        if (count == 2 && test_species_count == 2)
            for (const auto mode : {PhysicalMode::Longitudinal, PhysicalMode::Transverse})
                for (const auto order : {DiffFunction::RKLOrder::First, DiffFunction::RKLOrder::Second}) {
                    const auto sequential = run_route(order, false, count, mode);
                    const auto batched = run_route(order, true, count, mode);
                    for (std::size_t i = 0; i < sequential.size(); ++i)
                        for (const auto member : {&FluidState::rho, &FluidState::mom_u, &FluidState::mom_v,
                             &FluidState::mom_w, &FluidState::eng, &FluidState::enuc_rate, &FluidState::mass_fractions}) {
                            const auto& a = sequential[i].*member;
                            const auto& b = batched[i].*member;
                            require(a.size() == b.size(), "Physical mode batch changed storage size");
                            for (std::size_t j = 0; j < a.size(); ++j)
                                require(std::bit_cast<std::uint64_t>(a[j]) == std::bit_cast<std::uint64_t>(b[j]),
                                    "Physical mode diffusion batch changed field bits");
                        }
                }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
