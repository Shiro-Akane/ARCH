#include "amr/Block.h"
#include "amr/BoundaryPlan.h"
#include "cuda/hydro/BoundaryPlan.h"
#include "cuda/runtime/CudaBackend.h"
#include "driver/StageScheduler.h"
#include "numerics/diffusion/DiffFunction.h"
#include "numerics/burnsolver/Networks.h"
#include "physics/eos/HelmEos.h"
#include "physics/eos/IdealGas.h"
#include "physics/eos/Tabular3DEOS.h"
#include "physics/eos/Tabular4DEOS.h"
#include "physics/species/Species.h"

#include <cmath>
#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

template <class Function>
void require_failure(Function&& function, const char* message)
{
    try {
        function();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

arch::boundary::BoundaryPlan make_boundary_plan()
{
    using namespace arch::boundary;
    BoundaryPlanInput input{};
    input.dimension = 1;
    input.active_extent = {amr::BLOCK_NX, 1, 1};
    input.ghost_depth = amr::MAX_NG;
    input.faces.fill(BoundaryType::Inactive);
    input.faces[face_index(BoundaryAxis::X1, BoundarySide::Lower)] =
        BoundaryType::Outflow;
    input.faces[face_index(BoundaryAxis::X1, BoundarySide::Upper)] =
        BoundaryType::Outflow;
    return arch::boundary::make_boundary_plan(input);
}

void require_metadata_only_construction(
    const arch::cuda::CudaBackend& backend,
    const arch::boundary::BoundaryPlan& boundary)
{
    const auto construction = backend.counters();
    const auto boundary_metadata_bytes = boundary.operations().size()
        * sizeof(arch::cuda::DeviceBoundaryTransfer);
    require(construction.bytes_h2d == boundary_metadata_bytes
                && construction.bytes_d2h == 0 && construction.kernel_count == 1,
            "CUDA construction must upload only boundary metadata and generate grid metrics");
}

amr::Block make_block()
{
    amr::Block block{};
    block.id = 0;
    block.level = 0;
    block.active = true;
    block.grid = Grid(amr::MAX_NG, 0.0, 1.0, 0.0, 1.0, 0.0, 1.0,
                      1, 0, 0);
    block.grid.dim = 1;
    block.grid.geometry = "cartesian";
    block.grid.InitializeTopology();
    const int total = block.grid.GetTotalSize();
    for (FluidState* state : {
             &block.fluid_state, &block.state_next, &block.state_scratch}) {
        state->Preallocate(total);
        state->InitSpecies(0);
    }
    for (int i = 0; i < block.grid.GetTotalX(); ++i) {
        const int cell = block.grid.GetIndex(i);
        const bool left = i < block.grid.Is()
            + (block.grid.Ie() - block.grid.Is()) / 2;
        block.fluid_state.set(
            cell, left ? FluidVector{1.0, 0.0, 0.0, 0.0, 2.5}
                       : FluidVector{0.125, 0.0, 0.0, 0.0, 0.25});
        block.fluid_state.enuc_rate[cell] = -0.0;
    }
    return block;
}

arch::backend::HostStateTransferView transfer_view(FluidState& state)
{
    const auto species = static_cast<std::size_t>(state.GetNumSpecies());
    return {
        state.rho.data(), state.mom_u.data(), state.mom_v.data(),
        state.mom_w.data(), state.eng.data(), state.enuc_rate.data(),
        species == 0 ? nullptr : state.mass_fractions.data(),
        state.rho.size(), species, species == 0 ? 0 : state.rho.size()};
}

arch::cuda::CudaLaunchConfig make_launch_config()
{
    SimConfig config{};
    config.numerics.solver_name = "hllc";
    config.numerics.reconstruction = "ppm";
    config.numerics.limiter = "minmod";
    config.numerics.time_integrator = "euler";
    config.physics.eos_type = "ideal";
    config.physics.burn.use_burn = false;
    config.physics.diffusion.use_diffusion = false;
    const arch::dispatch::ResolvedExecutionPlan plan{
        arch::dispatch::FluxId::Hllc,
        arch::dispatch::ReconstructionId::Ppm,
        arch::dispatch::LimiterId::MinMod,
        arch::dispatch::TimeIntegratorId::Euler,
        arch::dispatch::EosId::Ideal,
        arch::dispatch::NetworkId::None,
        arch::dispatch::OdeSolverId::None,
        arch::dispatch::LinearSolverId::None,
        arch::dispatch::DiffusionIntegratorId::None};
    return arch::cuda::make_cuda_launch_config(plan, config);
}

arch::cuda::CudaLaunchConfig make_hydro_route_config(
    arch::dispatch::FluxId flux,
    arch::dispatch::ReconstructionId reconstruction,
    arch::dispatch::LimiterId limiter,
    arch::dispatch::TimeIntegratorId time_integrator =
        arch::dispatch::TimeIntegratorId::Euler)
{
    SimConfig config{};
    config.numerics.sml_rho = 1.0e-20;
    config.numerics.max_eint = 1.0e30;
    config.numerics.cfl = 0.8;
    config.numerics.entropy_fix_coeff = 0.03125;
    config.physics.burn.use_burn = false;
    config.physics.diffusion.use_diffusion = false;
    const arch::dispatch::ResolvedExecutionPlan plan{
        flux, reconstruction, limiter,
        time_integrator,
        arch::dispatch::EosId::Ideal,
        arch::dispatch::NetworkId::None,
        arch::dispatch::OdeSolverId::None,
        arch::dispatch::LinearSolverId::None,
        arch::dispatch::DiffusionIntegratorId::None};
    return arch::cuda::make_cuda_launch_config(plan, config);
}

amr::Block make_route_block()
{
    amr::Block block = make_block();
    constexpr double gamma = 1.4;
    for (int i = 0; i < block.grid.GetTotalX(); ++i) {
        const int cell = block.grid.GetIndex(i);
        const double x = static_cast<double>(i + 1);
        const double rho = 0.8 + 0.017 * x + 0.0009 * x * x;
        const double u = 0.07 + 0.013 * std::sin(0.61 * x)
            + 0.002 * x;
        const double v = -0.03 + 0.009 * std::cos(0.47 * x);
        const double w = 0.011 * std::sin(0.29 * x + 0.2);
        const double pressure = 1.1 + 0.021 * x
            + 0.017 * std::sin(0.83 * x);
        const double kinetic = 0.5 * rho * (u * u + v * v + w * w);
        block.fluid_state.set(
            cell, {rho, rho * u, rho * v, rho * w,
                   pressure / (gamma - 1.0) + kinetic});
        block.fluid_state.enuc_rate[cell] = (i & 1) == 0 ? -0.0 : 0.0;
    }
    return block;
}

std::uint64_t mix_bits(std::uint64_t hash, double value)
{
    hash ^= std::bit_cast<std::uint64_t>(value);
    return hash * 1099511628211ULL;
}

std::uint64_t run_hydro_route(
    int route_index, arch::dispatch::FluxId flux,
    arch::dispatch::ReconstructionId reconstruction,
    arch::dispatch::LimiterId limiter,
    arch::dispatch::TimeIntegratorId time_integrator =
        arch::dispatch::TimeIntegratorId::Euler)
{
    amr::Block block = make_route_block();
    SpeciesManager species;
    IdealGas eos(1.4, species);
    const amr::BlockHandle handle{
        {static_cast<std::uint64_t>(100 + route_index)}, {1}};
    const arch::backend::StorageGeneration storage{
        static_cast<std::uint64_t>(100 + route_index)};
    auto backend = arch::cuda::make_cuda_backend(
        block, handle, storage, 0,
        make_hydro_route_config(
            flux, reconstruction, limiter, time_integrator), species,
        make_boundary_plan(), eos);
    const arch::backend::BackendStateAccess current{
        handle, storage, arch::state::StateSlot::Current};
    arch::state::StateResidencyLedger ledger({1});
    ledger.register_block(
        handle, {1}, {1, arch::state::CompletionState::Complete});
    ledger.publish_ghost(
        {handle, arch::state::StateSlot::Current},
        arch::state::ExecutionSide::Host, {1},
        {2, arch::state::CompletionState::Complete});
    arch::scheduler::MonotonicSchedulerClock clock(2, 1);
    (void)arch::backend::transfer_state_regions(
        *backend, ledger, clock, current, transfer_view(block.fluid_state),
        arch::state::PendingTransferPhase::PendingH2D);
    arch::scheduler::StageExecutionContext context{
        arch::state::ExecutionSide::Device, ledger, clock};
    const std::array handles{handle};
    const double dt = 0.125 * backend->compute_hydro_dt(current, 0.8);
    const auto executor = [&] (
        const arch::scheduler::StageDescriptor& descriptor,
        arch::state::CompletionToken token) {
        return backend->execute_hydro_stage(current, descriptor, dt, token);
    };
    const auto boundary = [&] (
        arch::state::StateSlot slot, arch::state::StateVersion version,
        arch::state::CompletionToken token) {
        auto access = current;
        access.slot = slot;
        return backend->execute_physical_boundary(access, version, token);
    };
    const auto rotation = [&] (arch::state::SlotRotation value) {
        backend->rotate_slots(current, value);
    };
    const auto reflux = [] (
        const arch::scheduler::HydroPlan&, arch::state::StateSlot,
        arch::state::CompletionToken token) { return token; };
    switch (time_integrator) {
    case arch::dispatch::TimeIntegratorId::Euler:
        (void)arch::scheduler::execute_euler_lane(
            context, handles, executor, boundary, rotation, reflux);
        break;
    case arch::dispatch::TimeIntegratorId::Rk2:
        (void)arch::scheduler::execute_rk2_lane(
            context, handles, executor, boundary, rotation, reflux);
        break;
    case arch::dispatch::TimeIntegratorId::Rk3:
        (void)arch::scheduler::execute_rk3_lane(
            context, handles, executor, boundary, rotation, reflux);
        break;
    default:
        throw std::invalid_argument("invalid Hydro integrator witness");
    }
    const auto advanced = ledger.inspect(
        {handle, arch::state::StateSlot::Current});
    (void)arch::scheduler::complete_boundary(
        context, handles, arch::state::StateSlot::Current,
        advanced.interior.version,
        [&](arch::state::StateSlot slot, arch::state::StateVersion version,
            arch::state::CompletionToken token) {
            auto access = current;
            access.slot = slot;
            return backend->execute_physical_boundary(access, version, token);
        });
    FluidState downloaded;
    downloaded.Preallocate(block.grid.GetTotalSize());
    downloaded.InitSpecies(0);
    (void)arch::backend::transfer_state_regions(
        *backend, ledger, clock, current, transfer_view(downloaded),
        arch::state::PendingTransferPhase::PendingD2H);

    std::uint64_t hash = 1469598103934665603ULL;
    for (int i = block.grid.Is(); i < block.grid.Ie(); ++i) {
        const int cell = block.grid.GetIndex(i);
        hash = mix_bits(hash, downloaded.rho[cell]);
        hash = mix_bits(hash, downloaded.mom_u[cell]);
        hash = mix_bits(hash, downloaded.mom_v[cell]);
        hash = mix_bits(hash, downloaded.mom_w[cell]);
        hash = mix_bits(hash, downloaded.eng[cell]);
        hash = mix_bits(hash, downloaded.enuc_rate[cell]);
    }
    return hash;
}

void run_hydro_route_matrix()
{
    using arch::dispatch::FluxId;
    using arch::dispatch::LimiterId;
    using arch::dispatch::ReconstructionId;
    constexpr std::array fluxes{
        FluxId::Vl, FluxId::Sw, FluxId::Roe, FluxId::Hll, FluxId::Hllc};
    struct ReconstructionRoute {
        ReconstructionId reconstruction;
        LimiterId limiter;
    };
    constexpr std::array reconstructions{
        ReconstructionRoute{ReconstructionId::Pcm, LimiterId::MinMod},
        ReconstructionRoute{ReconstructionId::Ppm, LimiterId::MinMod},
        ReconstructionRoute{ReconstructionId::Muscl, LimiterId::MinMod},
        ReconstructionRoute{ReconstructionId::Muscl, LimiterId::Mc},
        ReconstructionRoute{ReconstructionId::Muscl, LimiterId::SuperBee},
        ReconstructionRoute{ReconstructionId::Muscl, LimiterId::VanLeer}};
    std::array<std::uint64_t, 30> hashes{};
    int route = 0;
    for (const FluxId flux : fluxes) {
        for (const auto reconstruction : reconstructions) {
            hashes[route] = run_hydro_route(
                route, flux, reconstruction.reconstruction,
                reconstruction.limiter);
            const auto repeat = run_hydro_route(
                route, flux, reconstruction.reconstruction,
                reconstruction.limiter);
            require(hashes[route] == repeat,
                    "CUDA Hydro route is not deterministic");
            std::cout << "CUDA_HYDRO_ROUTE index=" << route
                      << " flux=" << static_cast<int>(flux)
                      << " reconstruction="
                      << static_cast<int>(reconstruction.reconstruction)
                      << " limiter="
                      << static_cast<int>(reconstruction.limiter)
                      << " hash=0x" << std::hex << hashes[route]
                      << std::dec << '\n';
            ++route;
        }
    }
    require(route == static_cast<int>(hashes.size()),
            "CUDA Hydro route count drifted");
    std::cout << "CUDA_HYDRO_ROUTE_MATRIX_PASS routes=" << route << '\n';
}

void run_hydro_integrator_matrix()
{
    using arch::dispatch::FluxId;
    using arch::dispatch::LimiterId;
    using arch::dispatch::ReconstructionId;
    using arch::dispatch::TimeIntegratorId;
    constexpr std::array integrators{
        TimeIntegratorId::Euler, TimeIntegratorId::Rk2,
        TimeIntegratorId::Rk3};
    std::array<std::uint64_t, 3> hashes{};
    for (std::size_t route = 0; route < integrators.size(); ++route) {
        hashes[route] = run_hydro_route(
            static_cast<int>(40 + route), FluxId::Hllc,
            ReconstructionId::Ppm, LimiterId::MinMod,
            integrators[route]);
        const auto repeat = run_hydro_route(
            static_cast<int>(40 + route), FluxId::Hllc,
            ReconstructionId::Ppm, LimiterId::MinMod,
            integrators[route]);
        require(hashes[route] == repeat,
                "CUDA Hydro integrator is not deterministic");
        std::cout << "CUDA_HYDRO_INTEGRATOR id="
                  << static_cast<int>(integrators[route])
                  << " hash=0x" << std::hex << hashes[route]
                  << std::dec << '\n';
    }
    std::cout << "CUDA_HYDRO_INTEGRATOR_MATRIX_PASS routes=3\n";
}

SpeciesManager make_diffusion_species()
{
    SpeciesManager species;
    species.add_species("a", 1.0, 1.0, 1.4, 3.5);
    species.add_species("b", 4.0, 2.0, 1.5, 7.25);
    return species;
}

amr::Block make_diffusion_block()
{
    amr::Block block = make_block();
    const int total = block.grid.GetTotalSize();
    for (FluidState* state : {
             &block.fluid_state, &block.state_next, &block.state_scratch}) {
        state->Preallocate(total);
        state->InitSpecies(2);
    }
    for (int i = 0; i < block.grid.GetTotalX(); ++i) {
        const int cell = block.grid.GetIndex(i);
        const double rho = 1.0 + 0.01 * i;
        const double position = static_cast<double>(i);
        const double velocity_x = 0.02 + 3.0e-4 * position * position;
        const double velocity_y = -0.01 + 2.0e-4 * position * position;
        block.fluid_state.set(
            cell, {rho, velocity_x * rho, velocity_y * rho, 0.0,
                   12.0 * rho + 2.5e-4 * rho});
        const double x0 = 0.55 + 0.01 * static_cast<double>(i);
        block.fluid_state.X(0, cell) = x0;
        block.fluid_state.X(1, cell) = 1.0 - x0;
    }
    return block;
}

std::vector<double> eos_table3(
    double base, double di, double dj, double dk)
{
    std::vector<double> data(27);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 3; ++k)
                data[(i * 3 + j) * 3 + k] =
                    base + i * di + j * dj + k * dk;
    data.back() += 0.125;
    return data;
}

std::vector<double> eos_energy3()
{
    std::vector<double> data(27);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 3; ++k)
                data[(i * 3 + j) * 3 + k] =
                    1.0e6 * std::pow(10.0, 7.0 + j)
                    + 1000.0 * i + 100.0 * k;
    data.back() += 0.25;
    return data;
}

std::vector<double> eos_table4(
    double base, double di, double dj, double dk, double dl)
{
    std::vector<double> data(81);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 3; ++k)
                for (int l = 0; l < 3; ++l)
                    data[((i * 3 + j) * 3 + k) * 3 + l] =
                        base + i * di + j * dj + k * dk + l * dl;
    data.back() += 0.375;
    return data;
}

std::vector<double> eos_energy4()
{
    std::vector<double> data(81);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 3; ++k)
                for (int l = 0; l < 3; ++l)
                    data[((i * 3 + j) * 3 + k) * 3 + l] =
                        1.0e6 * std::pow(10.0, 7.0 + j)
                        + 1000.0 * i + 100.0 * k + 10.0 * l;
    data.back() += 0.5;
    return data;
}

Tabular3DEOSHostView make_eos_tab3(
    const SpeciesManager& species,
    const std::array<std::vector<double>, 6>& tables)
{
    Tabular3DEOSHostView view{};
    view.n_rho = view.n_T = view.n_X = 3;
    view.log_rho_min = 0.0;
    view.log_rho_max = 2.0;
    view.dlog_rho = 1.0;
    view.log_T_min = 7.0;
    view.log_T_max = 9.0;
    view.dlog_T = 1.0;
    view.X_min = 0.0;
    view.X_max = 1.0;
    view.dX = 0.5;
    view.table_P = tables[0].data();
    view.table_E = tables[1].data();
    view.table_cs = tables[2].data();
    view.table_cv = tables[3].data();
    view.table_dP_drho = tables[4].data();
    view.table_dP_dT = tables[5].data();
    for (std::size_t field = 0; field < tables.size(); ++field)
        view.table_extents[field] = tables[field].size();
    view.specs = species.get_host_view();
    view.target_species_id = 1;
    return view;
}

Tabular4DEOSHostView make_eos_tab4(
    const SpeciesManager& species,
    const std::array<std::vector<double>, 6>& tables)
{
    Tabular4DEOSHostView view{};
    view.n_rho = view.n_T = view.n_A = view.n_Z = 3;
    view.log_rho_min = 0.0;
    view.log_rho_max = 2.0;
    view.dlog_rho = 1.0;
    view.log_T_min = 7.0;
    view.log_T_max = 9.0;
    view.dlog_T = 1.0;
    view.A_min = 1.0;
    view.A_max = 2.0;
    view.dA = 0.5;
    view.Z_min = 0.5;
    view.Z_max = 1.0;
    view.dZ = 0.25;
    view.table_P = tables[0].data();
    view.table_E = tables[1].data();
    view.table_cs = tables[2].data();
    view.table_cv = tables[3].data();
    view.table_dP_drho = tables[4].data();
    view.table_dP_dT = tables[5].data();
    for (std::size_t field = 0; field < tables.size(); ++field)
        view.table_extents[field] = tables[field].size();
    view.specs = species.get_host_view();
    return view;
}

arch::cuda::CudaLaunchConfig make_eos_launch_config(
    arch::dispatch::EosId eos)
{
    SimConfig config{};
    config.numerics.sml_rho = 1.0e-20;
    config.numerics.max_eint = 1.0e30;
    config.numerics.cfl = 0.8;
    config.numerics.entropy_fix_coeff = 0.03125;
    config.physics.burn.use_burn = false;
    config.physics.diffusion.use_diffusion = false;
    const arch::dispatch::ResolvedExecutionPlan plan{
        arch::dispatch::FluxId::Hllc,
        arch::dispatch::ReconstructionId::Ppm,
        arch::dispatch::LimiterId::MinMod,
        arch::dispatch::TimeIntegratorId::Euler,
        eos,
        arch::dispatch::NetworkId::None,
        arch::dispatch::OdeSolverId::None,
        arch::dispatch::LinearSolverId::None,
        arch::dispatch::DiffusionIntegratorId::None};
    return arch::cuda::make_cuda_launch_config(plan, config);
}

template <class Eos>
std::uint64_t run_eos_owner_route(
    int route, arch::dispatch::EosId eos_id, const Eos& eos,
    const SpeciesManager& species, double rho, double temperature)
{
    amr::Block block = make_diffusion_block();
    const std::array<double, 2> composition{0.7, 0.3};
    const double eint = eos.get_eint_from_T(
        rho, temperature, composition.data());
    require(std::isfinite(eint) && eint > 0.0,
            "Host EOS fixture produced invalid internal energy");
    for (int cell = 0; cell < block.grid.GetTotalSize(); ++cell) {
        block.fluid_state.set(
            cell, {rho, 0.015 * rho, -0.0075 * rho, 0.003 * rho,
                   rho * eint});
        block.fluid_state.X(0, cell) = composition[0];
        block.fluid_state.X(1, cell) = composition[1];
    }
    const amr::BlockHandle handle{
        {static_cast<std::uint64_t>(500 + route)}, {1}};
    const arch::backend::StorageGeneration storage{
        static_cast<std::uint64_t>(500 + route)};
    auto backend = arch::cuda::make_cuda_backend(
        block, handle, storage, 0, make_eos_launch_config(eos_id), species,
        make_boundary_plan(), eos);
    const arch::backend::BackendStateAccess current{
        handle, storage, arch::state::StateSlot::Current};
    arch::state::StateResidencyLedger ledger({1});
    ledger.register_block(
        handle, {1}, {1, arch::state::CompletionState::Complete});
    ledger.publish_ghost(
        {handle, arch::state::StateSlot::Current},
        arch::state::ExecutionSide::Host, {1},
        {2, arch::state::CompletionState::Complete});
    arch::scheduler::MonotonicSchedulerClock clock(2, 1);
    (void)arch::backend::transfer_state_regions(
        *backend, ledger, clock, current, transfer_view(block.fluid_state),
        arch::state::PendingTransferPhase::PendingH2D);
    const double dt = backend->compute_hydro_dt(current, 0.8);
    require(std::isfinite(dt) && dt > 0.0,
            "CUDA EOS owner route produced invalid Hydro dt");
    return std::bit_cast<std::uint64_t>(dt);
}

void run_eos_owner_matrix()
{
    using arch::dispatch::EosId;
    SpeciesManager species = make_diffusion_species();
    IdealGas ideal(1.4, species);
    const std::string helm_table = std::string(ARCH_SOURCE_DIR)
        + "/EOS_toolkit/tables/helmholtz/helm_table.dat";
    HelmEos helm(helm_table, &species);
    const std::array<std::vector<double>, 6> tables3{
        eos_table3(1.0e15, 1.0e11, 2.0e11, 3.0e11),
        eos_energy3(),
        eos_table3(2.0e7, 1.0e4, 2.0e4, 3.0e4),
        eos_table3(3.0e6, 1.0e3, 2.0e3, 3.0e3),
        eos_table3(4.0e5, 10.0, 20.0, 30.0),
        eos_table3(5.0e4, 1.0, 2.0, 3.0)};
    const std::array<std::vector<double>, 6> tables4{
        eos_table4(1.5e15, 1.0e11, 2.0e11, 3.0e11, 4.0e11),
        eos_energy4(),
        eos_table4(2.5e7, 1.0e4, 2.0e4, 3.0e4, 4.0e4),
        eos_table4(3.5e6, 1.0e3, 2.0e3, 3.0e3, 4.0e3),
        eos_table4(4.5e5, 10.0, 20.0, 30.0, 40.0),
        eos_table4(5.5e4, 1.0, 2.0, 3.0, 4.0)};
    const Tabular3DEOSHostView tab3 = make_eos_tab3(species, tables3);
    const Tabular4DEOSHostView tab4 = make_eos_tab4(species, tables4);
    const std::array<std::uint64_t, 4> hashes{
        run_eos_owner_route(0, EosId::Ideal, ideal, species, 10.0, 1.0e8),
        run_eos_owner_route(
            1, EosId::Helmholtz, helm, species, 1.0e7, 2.0e9),
        run_eos_owner_route(
            2, EosId::Tabular3D, tab3, species, 10.0, 1.0e8),
        run_eos_owner_route(
            3, EosId::Tabular4D, tab4, species, 10.0, 1.0e8)};
    const std::array<std::uint64_t, 4> repeated_hashes{
        run_eos_owner_route(0, EosId::Ideal, ideal, species, 10.0, 1.0e8),
        run_eos_owner_route(
            1, EosId::Helmholtz, helm, species, 1.0e7, 2.0e9),
        run_eos_owner_route(
            2, EosId::Tabular3D, tab3, species, 10.0, 1.0e8),
        run_eos_owner_route(
            3, EosId::Tabular4D, tab4, species, 10.0, 1.0e8)};
    for (std::size_t route = 0; route < hashes.size(); ++route) {
        require(hashes[route] == repeated_hashes[route],
                "CUDA EOS owner route is not deterministic");
        std::cout << "CUDA_EOS_OWNER id=" << route << " dt_bits=0x"
                  << std::hex << hashes[route] << std::dec << '\n';
    }
    std::cout << "CUDA_EOS_OWNER_MATRIX_PASS routes=4\n";
}

arch::cuda::CudaLaunchConfig make_diffusion_launch_config(
    DiffFunction::RKLOrder order)
{
    SimConfig config{};
    config.numerics.solver_name = "hllc";
    config.numerics.reconstruction = "ppm";
    config.numerics.limiter = "minmod";
    config.numerics.time_integrator = "euler";
    config.physics.eos_type = "ideal";
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

BurnConfig make_burn_config()
{
    BurnConfig config{};
    config.use_burn = true;
    config.use_nse = false;
    config.nuclearTempMin = 1.0e8;
    config.nuclearDensMin = 1.0;
    config.smallt = 1.0e5;
    config.smallx = 1.0e-30;
    config.enucDtFactor = 0.5;
    config.odeconfig.rtol = 1.0e-4;
    config.odeconfig.atol = 1.0e-8;
    config.odeconfig.max_newton_iter = 50;
    config.odeconfig.max_substeps = 100;
    config.odeconfig.initial_dt_frac = 1.0;
    config.odeconfig.dt_safe_factor = 0.9;
    config.odeconfig.dt_fac_min = 0.1;
    config.odeconfig.dt_fac_max = 2.0;
    return config;
}

arch::cuda::CudaLaunchConfig make_burn_launch_config(
    arch::dispatch::NetworkId network, arch::dispatch::OdeSolverId ode)
{
    SimConfig config{};
    config.numerics.solver_name = "hllc";
    config.numerics.reconstruction = "ppm";
    config.numerics.limiter = "minmod";
    config.numerics.time_integrator = "euler";
    config.physics.eos_type = "helmholtz";
    config.physics.burn = make_burn_config();
    config.physics.diffusion.use_diffusion = false;
    const arch::dispatch::ResolvedExecutionPlan plan{
        arch::dispatch::FluxId::Hllc,
        arch::dispatch::ReconstructionId::Ppm,
        arch::dispatch::LimiterId::MinMod,
        arch::dispatch::TimeIntegratorId::Euler,
        arch::dispatch::EosId::Helmholtz,
        network,
        ode,
        arch::dispatch::LinearSolverId::DenseLu,
        arch::dispatch::DiffusionIntegratorId::None};
    return arch::cuda::make_cuda_launch_config(plan, config);
}

void run_signed_enuc_transfer_witness()
{
    amr::Block block = make_route_block();
    SpeciesManager species;
    IdealGas eos(1.4, species);
    const amr::BlockHandle handle{{90}, {1}};
    const arch::backend::StorageGeneration storage{90};
    auto backend = arch::cuda::make_cuda_backend(
        block, handle, storage, 0, make_launch_config(), species,
        make_boundary_plan(), eos);
    const arch::backend::BackendStateAccess current{
        handle, storage, arch::state::StateSlot::Current};
    arch::state::StateResidencyLedger ledger({1});
    ledger.register_block(
        handle, {1}, {1, arch::state::CompletionState::Complete});
    ledger.publish_ghost(
        {handle, current.slot}, arch::state::ExecutionSide::Host, {1},
        {2, arch::state::CompletionState::Complete});
    arch::scheduler::MonotonicSchedulerClock clock(2, 1);
    (void)arch::backend::transfer_state_regions(
        *backend, ledger, clock, current, transfer_view(block.fluid_state),
        arch::state::PendingTransferPhase::PendingH2D);
    const arch::state::StateKey key{handle, current.slot};
    const auto publication = clock.next_publication();
    ledger.publish_interior(
        key, arch::state::ExecutionSide::Device, publication.version,
        publication.completion);
    ledger.publish_ghost(
        key, arch::state::ExecutionSide::Device, publication.version,
        clock.next_completion());
    FluidState downloaded;
    downloaded.Preallocate(block.grid.GetTotalSize());
    downloaded.InitSpecies(0);
    (void)arch::backend::transfer_state_regions(
        *backend, ledger, clock, current, transfer_view(downloaded),
        arch::state::PendingTransferPhase::PendingD2H);
    for (int i = block.grid.Is(); i < block.grid.Ie(); ++i) {
        const int cell = block.grid.GetIndex(i);
        const double expected = (i & 1) == 0 ? -0.0 : 0.0;
        const auto observed =
            std::bit_cast<std::uint64_t>(downloaded.enuc_rate[cell]);
        require(observed == std::bit_cast<std::uint64_t>(expected),
                "CUDA signed ENUC transfer drifted");
    }
    std::cout << "CUDA_SIGNED_ENUC_TRANSFER_PASS\n";
}

void run_lifetime_and_hydro_witness()
{
    amr::Block block = make_block();
    SpeciesManager species;
    IdealGas eos(1.4, species);
    auto mismatched_eos = make_launch_config();
    mismatched_eos.plan.eos = arch::dispatch::EosId::Helmholtz;
    require_failure(
        [&] {
            (void)arch::cuda::make_cuda_backend(
                block, {{1}, {1}}, {1}, 0, mismatched_eos, species,
                make_boundary_plan(), eos);
        },
        "mismatched resolved EOS/factory owner was accepted");
    auto backend = arch::cuda::make_cuda_backend(
        block, {{1}, {1}}, {1}, 0, make_launch_config(), species,
        make_boundary_plan(), eos);
    require(backend->side() == arch::state::ExecutionSide::Device,
            "CUDA backend side drifted");
    require(backend->block_handle() == amr::BlockHandle{{1}, {1}}
                && backend->storage_generation().value == 1,
            "CUDA backend identity drifted");
    require_metadata_only_construction(*backend, make_boundary_plan());

    const arch::backend::BackendStateAccess current{
        {{1}, {1}}, {1}, arch::state::StateSlot::Current};
    const auto host = transfer_view(block.fluid_state);
    arch::state::StateResidencyLedger ledger({1});
    ledger.register_block(
        current.block, {1}, {1, arch::state::CompletionState::Complete});
    ledger.publish_ghost(
        {current.block, current.slot}, arch::state::ExecutionSide::Host, {1},
        {2, arch::state::CompletionState::Complete});
    arch::scheduler::MonotonicSchedulerClock clock(2, 1);
    (void)arch::backend::transfer_state_regions(
        *backend, ledger, clock, current, host,
        arch::state::PendingTransferPhase::PendingH2D, 0,
        arch::backend::BackendOperation::InitialUpload);
    arch::scheduler::StageExecutionContext context{
        arch::state::ExecutionSide::Device, ledger, clock};
    const std::array handles{current.block};

    auto wrong_descriptor = arch::scheduler::make_hydro_plan(
        arch::scheduler::HydroMethod::Euler).stages.front();
    wrong_descriptor.output_slot = arch::state::StateSlot::Scratch;
    require_failure(
        [&] {
            (void)backend->execute_hydro_stage(
                current, wrong_descriptor, 1.0e-4,
                {4, arch::state::CompletionState::Complete});
        },
        "wrong Hydro descriptor was accepted");
    const auto euler_descriptor = arch::scheduler::make_hydro_plan(
        arch::scheduler::HydroMethod::Euler).stages.front();
    require_failure(
        [&] {
            (void)backend->execute_hydro_stage(
                current, euler_descriptor, 1.0e-4,
                {4, arch::state::CompletionState::Pending});
        },
        "pending Hydro completion token was accepted");

    (void)arch::scheduler::complete_boundary(
        context, handles, arch::state::StateSlot::Current, {1},
        [&](arch::state::StateSlot slot, arch::state::StateVersion version,
            arch::state::CompletionToken token) {
            auto access = current;
            access.slot = slot;
            return backend->execute_physical_boundary(access, version, token);
        });
    const double dt = backend->compute_hydro_dt(current, 0.8);
    require(std::isfinite(dt) && dt > 0.0, "CUDA Hydro dt is invalid");

    (void)arch::scheduler::execute_euler_lane(
        context, handles,
        [&](const arch::scheduler::StageDescriptor& descriptor,
            arch::state::CompletionToken token) {
            return backend->execute_hydro_stage(
                current, descriptor, 0.1 * dt, token);
        },
        [&](arch::state::StateSlot slot, arch::state::StateVersion version,
            arch::state::CompletionToken token) {
            auto access = current;
            access.slot = slot;
            return backend->execute_physical_boundary(access, version, token);
        },
        [&](arch::state::SlotRotation rotation) {
            backend->rotate_slots(current, rotation);
        },
        [](const arch::scheduler::HydroPlan&, arch::state::StateSlot,
           arch::state::CompletionToken token) { return token; });

    const auto advanced = ledger.inspect(
        {current.block, arch::state::StateSlot::Current});
    (void)arch::scheduler::complete_boundary(
        context, handles, arch::state::StateSlot::Current,
        advanced.interior.version,
        [&](arch::state::StateSlot slot, arch::state::StateVersion version,
            arch::state::CompletionToken token) {
            auto access = current;
            access.slot = slot;
            return backend->execute_physical_boundary(access, version, token);
        });

    FluidState downloaded;
    downloaded.Preallocate(block.grid.GetTotalSize());
    downloaded.InitSpecies(0);
    const auto downloaded_view = transfer_view(downloaded);
    (void)arch::backend::transfer_state_regions(
        *backend, ledger, clock, current, downloaded_view,
        arch::state::PendingTransferPhase::PendingD2H, 1,
        arch::backend::BackendOperation::Materialize);
    const int active = block.grid.GetIndex(block.grid.Is());
    require(std::isfinite(downloaded.rho[active])
                && downloaded.rho[active] > 0.0,
            "CUDA Hydro stage did not produce a valid state");
    require(backend->counters().kernel_count >= 6
                && backend->counters().bytes_h2d > 0
                && backend->counters().bytes_d2h > 0,
            "CUDA runtime counters did not observe bounded work");
    const auto trace = backend->trace_snapshot();
    require(trace.size() == 2
                && trace.front().operation
                    == arch::backend::BackendOperation::InitialUpload
                && trace.back().operation
                    == arch::backend::BackendOperation::Materialize,
            "CUDA transfer trace drifted");

    auto stale = current;
    stale.storage.value = 2;
    require_failure(
        [&] { (void)backend->compute_hydro_dt(stale, 0.8); },
        "stale CUDA storage generation was accepted");

    const auto host_publication = clock.next_publication();
    downloaded.rho[active] += 0.015625;
    const arch::state::StateKey current_key{
        current.block, current.slot};
    ledger.publish_interior(
        current_key, arch::state::ExecutionSide::Host,
        host_publication.version, host_publication.completion);
    require_failure(
        [&] {
            ledger.require_readable(
                current_key,
                {arch::state::ExecutionSide::Device,
                 host_publication.version, true, false});
        },
        "unuploaded Host publication was readable on Device");
    bool stale_device_executor_called = false;
    require_failure(
        [&] {
            (void)arch::scheduler::execute_euler_lane(
                context, handles,
                [&](const arch::scheduler::StageDescriptor&,
                    arch::state::CompletionToken token) {
                    stale_device_executor_called = true;
                    return token;
                },
                [&](arch::state::StateSlot, arch::state::StateVersion,
                    arch::state::CompletionToken token) { return token; },
                [](arch::state::SlotRotation) {},
                [](const arch::scheduler::HydroPlan&,
                   arch::state::StateSlot,
                   arch::state::CompletionToken token) { return token; });
        },
        "unuploaded Host mutation was readable on Device");
    require(!stale_device_executor_called,
            "stale Device stage reached the physical executor");
    ledger.publish_ghost(
        current_key, arch::state::ExecutionSide::Host,
        host_publication.version, clock.next_completion());
    (void)arch::backend::transfer_state_regions(
        *backend, ledger, clock, current, transfer_view(downloaded),
        arch::state::PendingTransferPhase::PendingH2D, 2,
        arch::backend::BackendOperation::Upload);
    require(std::isfinite(backend->compute_hydro_dt(current, 0.8)),
            "explicit H2D did not restore Device readability");
    backend.reset();
    auto replacement = arch::cuda::make_cuda_backend(
        block, {{1}, {1}}, {2}, 0, make_launch_config(), species,
        make_boundary_plan(), eos);
    require_failure(
        [&] { (void)replacement->compute_hydro_dt(current, 0.8); },
        "reconstructed CUDA storage accepted the destroyed generation");
    require_metadata_only_construction(*replacement, make_boundary_plan());
    std::cout << "CUDA_HYDRO_BLOCK_PASS dt=" << dt
              << " rho=" << downloaded.rho[active] << '\n';
}

void run_diffusion_rkl_witness(DiffFunction::RKLOrder order)
{
    amr::Block block = make_diffusion_block();
    SpeciesManager species = make_diffusion_species();
    IdealGas eos(1.4, species);
    auto backend = arch::cuda::make_cuda_backend(
        block, {{2}, {1}}, {2}, 0, make_diffusion_launch_config(order), species,
        make_boundary_plan(), eos);
    const arch::backend::BackendStateAccess current{
        {{2}, {1}}, {2}, arch::state::StateSlot::Current};
    arch::state::StateResidencyLedger ledger({1});
    ledger.register_block(
        current.block, {1}, {1, arch::state::CompletionState::Complete});
    ledger.publish_ghost(
        {current.block, current.slot}, arch::state::ExecutionSide::Host, {1},
        {2, arch::state::CompletionState::Complete});
    arch::scheduler::MonotonicSchedulerClock clock(2, 1);
    (void)arch::backend::transfer_state_regions(
        *backend, ledger, clock, current, transfer_view(block.fluid_state),
        arch::state::PendingTransferPhase::PendingH2D);
    arch::scheduler::StageExecutionContext context{
        arch::state::ExecutionSide::Device, ledger, clock};
    const std::array handles{current.block};

    const double dt_fe = backend->compute_diffusion_dt(current);
    require(std::isfinite(dt_fe) && dt_fe > 0.0,
            "CUDA diffusion dt is invalid");
    const double dt = 0.5 * dt_fe;
    const int stages = DiffFunction::compute_stages(
        order, dt, dt_fe, 0.8, 17);
    const auto wrong_plan = arch::scheduler::make_rkl_plan(
        order == DiffFunction::RKLOrder::First
            ? arch::scheduler::RklMethod::RKL2
            : arch::scheduler::RklMethod::RKL1,
        stages);
    require_failure(
        [&] {
            (void)backend->execute_diffusion_stage(
                current, wrong_plan, wrong_plan.stages.front(), dt, dt_fe,
                {999, arch::state::CompletionState::Complete});
        },
        "resolved CUDA RKL route accepted the opposite RKL plan");
    const auto copy = [&](arch::state::StateSlot destination) {
        (void)arch::scheduler::copy_slot(
            context, handles, arch::state::StateSlot::Current, destination,
            [&] {
                auto target = current;
                target.slot = destination;
                backend->copy_state_slot(current, target);
            });
    };
    copy(arch::state::StateSlot::Scratch);
    copy(arch::state::StateSlot::Next);
    const auto executor = [&] (
        const arch::scheduler::RklPlan& plan,
        const arch::scheduler::RklStageDescriptor& descriptor,
        arch::state::CompletionToken token) {
        return backend->execute_diffusion_stage(
            current, plan, descriptor, dt, dt_fe, token);
    };
    const auto reflux = [] (
        const arch::scheduler::RklPlan&,
        const arch::scheduler::RklStageDescriptor&,
        arch::state::CompletionToken token) { return token; };
    const auto boundary = [&] (
        arch::state::StateSlot slot, arch::state::StateVersion version,
        arch::state::CompletionToken token) {
        auto access = current;
        access.slot = slot;
        return backend->execute_physical_boundary(access, version, token);
    };
    const auto rotation = [&] (arch::state::SlotRotation value) {
        backend->rotate_slots(current, value);
    };
    if (order == DiffFunction::RKLOrder::First) {
        (void)arch::scheduler::execute_single_rkl1_lane(
            context, handles, stages, executor, reflux, boundary, rotation);
    } else {
        (void)arch::scheduler::execute_single_rkl2_lane(
            context, handles, stages, executor, reflux, boundary, rotation);
    }

    FluidState downloaded;
    downloaded.Preallocate(block.grid.GetTotalSize());
    downloaded.InitSpecies(2);
    (void)arch::backend::transfer_state_regions(
        *backend, ledger, clock, current, transfer_view(downloaded),
        arch::state::PendingTransferPhase::PendingD2H, 1,
        arch::backend::BackendOperation::Materialize);
    const int cell = block.grid.GetIndex(block.grid.Is() + 2);
    require(std::isfinite(downloaded.eng[cell])
                && downloaded.rho[cell] > 0.0
                && std::isfinite(downloaded.X(0, cell)),
            "CUDA RKL lane produced an invalid state");
    std::uint64_t hash = 1469598103934665603ULL;
    for (int i = block.grid.Is(); i < block.grid.Ie(); ++i) {
        const int active_cell = block.grid.GetIndex(i);
        hash = mix_bits(hash, downloaded.rho[active_cell]);
        hash = mix_bits(hash, downloaded.mom_u[active_cell]);
        hash = mix_bits(hash, downloaded.mom_v[active_cell]);
        hash = mix_bits(hash, downloaded.mom_w[active_cell]);
        hash = mix_bits(hash, downloaded.eng[active_cell]);
        hash = mix_bits(hash, downloaded.enuc_rate[active_cell]);
        hash = mix_bits(hash, downloaded.X(0, active_cell));
        hash = mix_bits(hash, downloaded.X(1, active_cell));
    }
    std::cout << "CUDA_DIFFUSION_RKL_OBSERVED order="
              << (order == DiffFunction::RKLOrder::First ? 1 : 2)
              << " hash=0x" << std::hex << hash << std::dec << '\n';
    std::cout << "CUDA_DIFFUSION_RKL_PASS order="
              << (order == DiffFunction::RKLOrder::First ? 1 : 2)
              << " stages=" << stages
              << " dt_fe=" << dt_fe
              << " hash=0x" << std::hex << hash << std::dec << '\n';
}

template <class Network>
void run_burn_witness(arch::dispatch::NetworkId network,
                      arch::dispatch::OdeSolverId ode,
                      const char* route)
{
    SpeciesManager species;
    Network::RegisterSpecies(species);
    amr::Block block = make_block();
    const int total = block.grid.GetTotalSize();
    for (FluidState* state : {
             &block.fluid_state, &block.state_next, &block.state_scratch}) {
        state->Preallocate(total);
        state->InitSpecies(Network::NUM_SPECIES);
    }
    const std::string table = std::string(ARCH_SOURCE_DIR)
        + "/EOS_toolkit/tables/helmholtz/helm_table.dat";
    HelmEos eos(table, &species);
    std::array<double, Network::NUM_SPECIES> composition{};
    double sum = 0.0;
    for (int i = 0; i < Network::NUM_SPECIES; ++i) {
        composition[i] = static_cast<double>(i + 1);
        sum += composition[i];
    }
    for (double& value : composition) value /= sum;
    constexpr double rho = 1.0e6;
    constexpr double temperature = 2.0e9;
    constexpr double burn_dt = 1.0e-16;
    const double eint = eos.get_eint_from_T(
        rho, temperature, composition.data());
    const int burn_cell = block.grid.GetIndex(block.grid.Is());
    const int sentinel_cell = block.grid.GetIndex(block.grid.Is() + 1);
    for (int cell = 0; cell < total; ++cell) {
        const double cell_rho = cell == burn_cell ? rho : 1.0e-2;
        block.fluid_state.set(
            cell, {cell_rho, 0.0, 0.0, 0.0, cell_rho * eint});
        block.fluid_state.enuc_rate[cell] =
            cell == sentinel_cell ? -6.25 : 0.0;
        for (int i = 0; i < Network::NUM_SPECIES; ++i)
            block.fluid_state.X(i, cell) = composition[i];
    }

    auto backend = arch::cuda::make_cuda_backend(
        block, {{3}, {1}}, {3}, 0,
        make_burn_launch_config(network, ode), species,
        make_boundary_plan(), eos);
    const arch::backend::BackendStateAccess current{
        {{3}, {1}}, {3}, arch::state::StateSlot::Current};
    arch::state::StateResidencyLedger ledger({1});
    ledger.register_block(
        current.block, {1}, {1, arch::state::CompletionState::Complete});
    ledger.publish_ghost(
        {current.block, current.slot}, arch::state::ExecutionSide::Host, {1},
        {2, arch::state::CompletionState::Complete});
    arch::scheduler::MonotonicSchedulerClock clock(2, 1);
    (void)arch::backend::transfer_state_regions(
        *backend, ledger, clock, current, transfer_view(block.fluid_state),
        arch::state::PendingTransferPhase::PendingH2D);
    arch::scheduler::StageExecutionContext context{
        arch::state::ExecutionSide::Device, ledger, clock};
    const std::array handles{current.block};
    arch::backend::BurnExecutionResult result{};
    (void)arch::scheduler::execute_burn_first_lane(
        context, handles, [&](arch::state::CompletionToken token) {
            result = backend->execute_burn(current, burn_dt, token);
            return result.completion;
        });
    std::cout << "CUDA_BURN_SUMMARY route=" << route
              << " status=" << result.status
              << " failed_cells=" << result.failed_cells
              << " limiter=" << result.dt_recommended << std::endl;
    require(result.status == 0 && result.failed_cells == 0
                && std::isfinite(result.dt_recommended)
                && result.dt_recommended > 0.0,
            "CUDA burn summary is invalid");

    const auto after = ledger.inspect({current.block, current.slot});
    (void)arch::scheduler::complete_boundary(
        context, handles, current.slot, after.interior.version,
        [&](arch::state::StateSlot slot, arch::state::StateVersion version,
            arch::state::CompletionToken token) {
            auto access = current;
            access.slot = slot;
            return backend->execute_physical_boundary(access, version, token);
        });
    FluidState downloaded;
    downloaded.Preallocate(total);
    downloaded.InitSpecies(Network::NUM_SPECIES);
    (void)arch::backend::transfer_state_regions(
        *backend, ledger, clock, current, transfer_view(downloaded),
        arch::state::PendingTransferPhase::PendingD2H);
    const int cell = burn_cell;
    double downloaded_sum = 0.0;
    for (int i = 0; i < Network::NUM_SPECIES; ++i)
        downloaded_sum += downloaded.X(i, cell);
    require(std::isfinite(downloaded.eng[cell])
                && std::isfinite(downloaded.enuc_rate[cell])
                && std::abs(downloaded_sum - 1.0) < 1.0e-8,
            "CUDA burn commit is invalid");
    require(downloaded.enuc_rate[sentinel_cell] == 0.0,
            "CUDA burn whole-field ENUC clear drifted");
    std::uint64_t hash = 1469598103934665603ULL;
    hash = mix_bits(hash, downloaded.rho[cell]);
    hash = mix_bits(hash, downloaded.mom_u[cell]);
    hash = mix_bits(hash, downloaded.mom_v[cell]);
    hash = mix_bits(hash, downloaded.mom_w[cell]);
    hash = mix_bits(hash, downloaded.eng[cell]);
    hash = mix_bits(hash, downloaded.enuc_rate[cell]);
    for (int i = 0; i < Network::NUM_SPECIES; ++i)
        hash = mix_bits(hash, downloaded.X(i, cell));
    hash = mix_bits(hash, downloaded.rho[sentinel_cell]);
    hash = mix_bits(hash, downloaded.eng[sentinel_cell]);
    hash = mix_bits(hash, downloaded.enuc_rate[sentinel_cell]);
    for (int i = 0; i < Network::NUM_SPECIES; ++i)
        hash = mix_bits(hash, downloaded.X(i, sentinel_cell));
    hash = mix_bits(hash, result.dt_recommended);
    std::cout << "CUDA_BURN_OBSERVED route=" << route << " hash=0x"
              << std::hex << hash << std::dec << '\n';
    std::cout << "CUDA_BURN_PASS route=" << route
              << " limiter=" << result.dt_recommended
              << " enuc=" << downloaded.enuc_rate[cell]
              << " hash=0x" << std::hex << hash << std::dec << '\n';
}

void run_named_burn_witness(const std::string& route)
{
    using arch::dispatch::NetworkId;
    using arch::dispatch::OdeSolverId;
    const auto run_network = [&](auto network_tag, NetworkId network) {
        using Network = typename decltype(network_tag)::type;
        if (route.ends_with(".be_nr"))
            run_burn_witness<Network>(
                network, OdeSolverId::BeNr, route.c_str());
        else if (route.ends_with(".bd"))
            run_burn_witness<Network>(
                network, OdeSolverId::Bd, route.c_str());
        else if (route.ends_with(".ros4"))
            run_burn_witness<Network>(
                network, OdeSolverId::Ros4, route.c_str());
        else
            throw std::invalid_argument("unknown CUDA burn ODE route");
    };
    if (route.starts_with("aprox13."))
        run_network(std::type_identity<NetAprox13>{}, NetworkId::Aprox13);
    else if (route.starts_with("aprox19."))
        run_network(std::type_identity<NetAprox19>{}, NetworkId::Aprox19);
    else if (route.starts_with("aprox21."))
        run_network(std::type_identity<NetAprox21>{}, NetworkId::Aprox21);
    else if (route.starts_with("iso7."))
        run_network(std::type_identity<NetIso7>{}, NetworkId::Iso7);
    else
        throw std::invalid_argument("unknown CUDA burn network route");
}

} // namespace

int main(int argc, char** argv)
{
    std::cout << std::unitbuf;
    if (argc == 2) {
        if (std::string(argv[1]) == "hydro-matrix") {
            run_hydro_route_matrix();
            return 0;
        }
        if (std::string(argv[1]) == "hydro-integrators") {
            run_hydro_integrator_matrix();
            return 0;
        }
        if (std::string(argv[1]) == "eos-matrix") {
            run_eos_owner_matrix();
            return 0;
        }
        if (std::string(argv[1]) == "rkl1") {
            run_diffusion_rkl_witness(DiffFunction::RKLOrder::First);
            return 0;
        }
        if (std::string(argv[1]) == "rkl2") {
            run_diffusion_rkl_witness(DiffFunction::RKLOrder::Second);
            return 0;
        }
        run_named_burn_witness(argv[1]);
        return 0;
    }
    if (argc != 1)
        throw std::invalid_argument("unexpected CUDA backend test arguments");
    run_signed_enuc_transfer_witness();
    run_lifetime_and_hydro_witness();
    run_diffusion_rkl_witness(DiffFunction::RKLOrder::First);
    run_diffusion_rkl_witness(DiffFunction::RKLOrder::Second);
    return 0;
}
