/**
 * @file test_cuda_multiblock_burn.cu
 * @brief Check multiblock CUDA burning for each supported ODE method.
 *
 * Run aprox13 with BE-NR, BD and ROS4 through the production backend and
 * compare sequential and batched device fields bitwise. A native-RZ subset
 * also compares the real backend with the shared Host burn operation, keeping
 * native density/momenta and inactive storage unchanged. This finite service
 * check does not enable the full native CUDA Runtime.
 */
#include "amr/storage/Block.h"
#include "amr/exchange/BoundaryPlan.h"
#include "amr/exchange/ExchangePlan.h"
#include "cuda/runtime/CudaBackend.h"
#include "driver/stages/DriverBurn.h"
#include "driver/stages/DriverMacroStep.h"
#include "numerics/burnsolver/BurnDispatch.h"
#include "numerics/flux/FluxHLLC.h"
#include "numerics/integrator/HydroSolverImpl.h"
#include "numerics/integrator/TimeIntegratorRK2.h"
#include "numerics/reconstruction/Reconstruction.h"
#include "physics/boundary/UserBoundary.h"
#include "driver/DriverUtils.h"
#include "numerics/burnsolver/Networks.h"
#include "numerics/burnsolver/ode/ode_bd.h"
#include "numerics/burnsolver/ode/ode_be-nr.h"
#include "numerics/burnsolver/ode/ode_ros4.h"
#include "physics/eos/HelmEos.h"
#include "physics/species/Species.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <atomic>
#include <cstdlib>
#include <optional>
#include <iostream>
#include <iomanip>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

arch::boundary::BoundaryPlan make_boundary_plan(int dimension = 1)
{
    using namespace arch::boundary;
    BoundaryPlanInput input{};
    input.dimension = dimension;
    input.active_extent = {amr::BLOCK_NX, dimension == 2 ? amr::BLOCK_NY : 1, 1};
    input.ghost_depth = amr::MAX_NG;
    input.faces.fill(BoundaryType::Inactive);
    input.faces[face_index(BoundaryAxis::X1, BoundarySide::Lower)] =
        BoundaryType::Outflow;
    input.faces[face_index(BoundaryAxis::X1, BoundarySide::Upper)] =
        BoundaryType::Outflow;
    if (dimension == 2) {
        input.faces[face_index(BoundaryAxis::X2, BoundarySide::Lower)] = BoundaryType::Outflow;
        input.faces[face_index(BoundaryAxis::X2, BoundarySide::Upper)] = BoundaryType::Outflow;
    }
    return arch::boundary::make_boundary_plan(input);
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

arch::cuda::CudaLaunchConfig make_launch_config(
    arch::dispatch::OdeSolverId ode,
    arch::dispatch::LinearSolverId linear = arch::dispatch::LinearSolverId::DenseLu)
{
    SimConfig config{};
    config.physics.eos_type = "helmholtz";
    config.physics.burn = make_burn_config();
    config.physics.diffusion.use_diffusion = false;
    const arch::dispatch::ResolvedExecutionPlan plan{
        arch::dispatch::FluxId::Hllc,
        arch::dispatch::ReconstructionId::Ppm,
        arch::dispatch::LimiterId::MinMod,
        arch::dispatch::TimeIntegratorId::Euler,
        arch::dispatch::EosId::Helmholtz,
        arch::dispatch::NetworkId::Aprox13,
        ode,
        linear,
        arch::dispatch::DiffusionIntegratorId::None};
    return arch::cuda::make_cuda_launch_config(plan, config);
}

amr::Block make_block(int id, const HelmEos& eos)
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
        state->InitSpecies(NetAprox13::NUM_SPECIES);
    }
    std::array<double, NetAprox13::NUM_SPECIES> composition{};
    double normalization = 0.0;
    for (int species = 0; species < NetAprox13::NUM_SPECIES; ++species) {
        composition[species] = static_cast<double>(species + 1 + id);
        normalization += composition[species];
    }
    for (double& value : composition) value /= normalization;
    constexpr double rho = 1.0e6;
    constexpr double temperature = 2.0e9;
    const double eint = eos.get_eint_from_T(
        rho, temperature, composition.data());
    const int burn_cell = block.grid.GetIndex(block.grid.Is() + id % 2);
    for (int cell = 0; cell < total; ++cell) {
        const double cell_rho = cell == burn_cell ? rho : 1.0e-2;
        block.fluid_state.set(
            cell, {cell_rho, 0.0, 0.0, 0.0, cell_rho * eint});
        block.fluid_state.enuc_rate[cell] = -6.25;
        for (int species = 0; species < NetAprox13::NUM_SPECIES; ++species)
            block.fluid_state.X(species, cell) = composition[species];
    }
    return block;
}

arch::backend::HostStateTransferView transfer_view(FluidState& state)
{
    return {
        state.rho.data(), state.mom_u.data(), state.mom_v.data(),
        state.mom_w.data(), state.eng.data(), state.enuc_rate.data(),
        state.mass_fractions.data(), state.rho.size(),
        NetAprox13::NUM_SPECIES, state.rho.size()};
}

amr::SameLevelExchangePlan make_exchange_plan(
    std::array<amr::BlockHandle, 2> handles)
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
    return amr::make_same_level_exchange_plan(
        topology, 1, {amr::BLOCK_NX, 1, 1}, amr::MAX_NG,
        handles[0].epoch);
}

std::vector<FluidState> run_route(arch::dispatch::OdeSolverId ode, const char* name, bool batched,
    std::size_t count = 2)
{
    SpeciesManager species;
    NetAprox13::RegisterSpecies(species);
    const std::string table = std::string(ARCH_SOURCE_DIR)
        + "/EOS_toolkit/tables/helmholtz/helm_table.dat";
    HelmEos eos(table, &species);
    std::vector<amr::Block> blocks;
    std::vector<amr::BlockHandle> handles;
    std::vector<arch::backend::StorageGeneration> storage;
    blocks.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        blocks.push_back(make_block(static_cast<int>(i), eos));
        handles.push_back({{501 + i}, {13}});
        storage.push_back({601 + i});
    }
    const auto boundary_plan = make_boundary_plan();
    std::vector<arch::cuda::CudaBlockBinding> bindings(count);
    for (std::size_t index = 0; index < bindings.size(); ++index)
        bindings[index] = {
            &blocks[index], handles[index], storage[index], &boundary_plan};
    auto backend = arch::cuda::make_cuda_backend(
        bindings, 0, make_launch_config(ode), species, eos);
    std::vector<arch::backend::BackendStateAccess> current(count);
    arch::state::StateResidencyLedger ledger({13});
    for (std::size_t index = 0; index < current.size(); ++index) {
        current[index] = {
            handles[index], storage[index], arch::state::StateSlot::Current};
        ledger.register_block(
            handles[index], {1}, {1, arch::state::CompletionState::Complete});
        ledger.publish_ghost(
            {handles[index], current[index].slot},
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
    constexpr double burn_dt = 1.0e-16;
    std::vector<arch::backend::BurnExecutionResult> results(count);
    (void)arch::scheduler::execute_burn_first_lane(
        context, handles, [&](arch::state::CompletionToken token) {
            const auto before = backend->counters();
            const auto batch = batched ? backend->execute_burn_batch(current, burn_dt, token)
                : std::vector<arch::backend::BurnExecutionResult>{};
            for (std::size_t index = 0; index < current.size(); ++index) {
                results[index] = batched ? batch[index] : backend->execute_burn(current[index], burn_dt, token);
                require(results[index].completion.value == token.value
                            && arch::state::is_complete(results[index].completion),
                        "multi-block burn completion token drifted");
            }
            require(backend->counters().stream_sync_count - before.stream_sync_count == (batched ? 1 : count),
                "Dense burn summary completion was not batched");
            require(backend->counters().kernel_count - before.kernel_count == (batched ? 2 * ((count + 1023) / 1024) : 2 * count),
                "Dense burn cell/reduction kernels were not batched");
            return token;
        });

    std::vector<arch::reduction::ReductionCandidate> candidates;
    for (std::size_t index = 0; index < results.size(); ++index) {
        require(results[index].status == 0 && results[index].failed_cells == 0,
                "multi-block burn reported a failed cell");
        candidates.push_back({
            results[index].dt_recommended,
            DriverReduction::make_block_reduction_key(
                0, static_cast<std::uint64_t>(index),
                static_cast<int>(index), 0, 0,
                DriverReduction::BlockReductionComponent::BurnFirstHalf),
            true});
    }
    const double global = DriverReduction::reduce_block_minimum(
        DriverBurn::INACTIVE_LIMITER_CANDIDATE, candidates);
    std::reverse(candidates.begin(), candidates.end());
    const double permuted = DriverReduction::reduce_block_minimum(
        DriverBurn::INACTIVE_LIMITER_CANDIDATE, candidates);
    require(std::bit_cast<std::uint64_t>(global)
                == std::bit_cast<std::uint64_t>(permuted),
            "multi-block burn reduction depends on traversal order");

    // Only the first two blocks participate in this exchange sub-contract;
    // the optional capacity scan below qualifies burn, not a global mesh.
    const auto exchange_plan = make_exchange_plan({handles[0], handles[1]});
    const auto version = ledger.inspect({handles[0], current[0].slot})
        .interior.version;
    (void)arch::scheduler::complete_boundary(
        context, handles, current[0].slot, version,
        [&](arch::state::StateSlot slot, arch::state::StateVersion selected,
            arch::state::CompletionToken token) {
            std::vector<arch::backend::BackendStateAccess> accesses(count);
            for (std::size_t index = 0; index < current.size(); ++index) {
                accesses[index] = current[index];
                accesses[index].slot = slot;
                (void)backend->execute_physical_boundary(
                    accesses[index], selected, token);
            }
            return backend->execute_same_level_exchange(
                {accesses.data(), 2}, exchange_plan, slot, selected, token);
        });

    std::vector<FluidState> downloaded(count);
    for (std::size_t index = 0; index < downloaded.size(); ++index) {
        downloaded[index].Preallocate(blocks[index].grid.GetTotalSize());
        downloaded[index].InitSpecies(NetAprox13::NUM_SPECIES);
        (void)arch::backend::transfer_state_regions(
            *backend, ledger, clock, current[index], transfer_view(downloaded[index]),
            arch::state::PendingTransferPhase::PendingD2H);
        const int burn_cell = blocks[index].grid.GetIndex(
            blocks[index].grid.Is() + static_cast<int>(index % 2));
        double sum = 0.0;
        for (int species_index = 0;
             species_index < NetAprox13::NUM_SPECIES; ++species_index)
            sum += downloaded[index].X(species_index, burn_cell);
        require(std::isfinite(downloaded[index].eng[burn_cell])
                    && std::abs(sum - 1.0) < 1.0e-8,
                "multi-block burn composition commit is invalid");
        const int sentinel = blocks[index].grid.GetIndex(
            blocks[index].grid.Is() + 3);
        require(downloaded[index].enuc_rate[sentinel] == 0.0,
                "multi-block burn did not clear whole-field ENUC");
    }
    std::cout << "CUDA_MULTIBLOCK_BURN_PASS route=" << name
              << " blocks=" << count << " limiter=" << global << '\n';
    return downloaded;
}


/** Burn two real native patches through each existing dense ODE route.
 * Independent constant-density full-ring moments seed the geometry:
 * V~(b^2-a^2)/2, W~(b^3-a^3)/3, I~rho*(b^4-a^4)/4,
 * q=Omega*I/W and E=rho*e+rho*(u_r^2+u_z^2)/2+Omega^2*I/(2V).
 * One hot cell per patch reacts; cold cells and all ghosts keep their state.
 * The same Host operator supplies the paired endpoint, with the established
 * burn-global 1e-8 state/energy and 1e-12 mass budgets. This is backend/closure
 * parity, not an independent nuclear-rate reference or Runtime capability.
 */
template<template<class, class, class> class Solver>
void run_native_route(arch::dispatch::OdeSolverId ode, const char* name,
    arch::dispatch::LinearSolverId linear = arch::dispatch::LinearSolverId::DenseLu)
{
    constexpr auto native = GridMetrics::GeometrySemantics::AxisymmetricRz;
    SpeciesManager species;
    NetAprox13::RegisterSpecies(species);
    HelmEos eos(std::string(ARCH_SOURCE_DIR)
        + "/EOS_toolkit/tables/helmholtz/helm_table.dat", &species);
    Grid root(amr::MAX_NG, 1., 3., -.5, .5, 0., 1., 2, 1, 1);
    root.geometry = "cylindrical"; root.dim = 2;
    std::array<amr::Block, 2> blocks;
    const double poison = std::numeric_limits<double>::quiet_NaN();
    constexpr double rho = 1.e6, ur = 1.e7, uz = -2.e7, omega = 5.e8;
    for (int b = 0; b < 2; ++b) {
        auto& block = blocks[b]; block.Reset(); block.active = true;
        block.level = 0; block.logical_x1 = b; block.logical_x2 = 0;
        block.InitGeometry(root, 1./amr::BLOCK_NX, 1./amr::BLOCK_NY, 1., native);
        block.RequireNativeGeometryIdentity();
        const auto& grid = block.grid;
        for (auto* field : {&block.fluid_state, &block.state_next, &block.state_scratch}) {
            field->Preallocate(grid.GetTotalSize()); field->InitSpecies(NetAprox13::NUM_SPECIES);
        }
        auto& field = block.fluid_state;
        for (auto* plane : {&field.rho, &field.mom_u, &field.mom_v, &field.mom_w,
                            &field.eng, &field.mass_fractions})
            std::fill(plane->begin(), plane->end(), poison);
        std::fill(field.enuc_rate.begin(), field.enuc_rate.end(), -6.25);
        std::array<double, NetAprox13::NUM_SPECIES> composition{};
        double sum = 0.;
        for (int n = 0; n < NetAprox13::NUM_SPECIES; ++n) {
            composition[n] = n + 1 + b; sum += composition[n];
        }
        for (double& x : composition) x /= sum;
        for (int j = 0; j < grid.GetTotalY(); ++j)
            for (int i = 0; i < grid.GetTotalX(); ++i) {
                const int cell = grid.GetIndex(i,j,0);
                const double temperature = i == grid.Is() && j == grid.Js() ? 2.e9 : 5.e7;
                const double thermal = eos.get_eint_from_T(rho, temperature, composition.data());
                const long double a = grid.GetFacePosL(i), z = grid.GetFacePosR(i);
                const long double V = (z*z-a*a)/2.L, W = (z*z*z-a*a*a)/3.L;
                const long double I = rho*(z*z*z*z-a*a*a*a)/4.L;
                field.set(cell, {rho, rho*ur, rho*uz, double(omega*I/W),
                    double(rho*static_cast<long double>(thermal)
                        + .5L*rho*(ur*ur+uz*uz) + .5L*omega*omega*I/V)});
                for (int n = 0; n < NetAprox13::NUM_SPECIES; ++n) field.X(n,cell)=composition[n];
            }
    }
    const std::array initial{blocks[0].fluid_state, blocks[1].fluid_state};
    auto expected = initial;
    SimConfig config{}; config.physics.eos_type="helmholtz";
    config.physics.burn=make_burn_config();config.physics.burn.network_name="aprox13";
    config.numerics.sml_rho=1.e-30;config.numerics.min_eint=1.e-30;config.numerics.max_eint=1.e30;
    constexpr double interval=1.e-12;
    Solver<NetAprox13,DenseMatrixData<NetAprox13::ODE_NEQ>,DenseLUSolver> burner;
    std::array<DriverBurn::HostBurnPatch,2> patches{{{&expected[0],&blocks[0].grid,native},
                                                 {&expected[1],&blocks[1].grid,native}}};
    std::array<double,2> limits{};
    DriverBurn::execute_host_burn_batch(patches,interval,eos,burner,config,limits);
    auto launch=make_launch_config(ode,linear);
    launch.density_floor=config.numerics.sml_rho;
    launch.minimum_internal_energy=config.numerics.min_eint;
    launch.maximum_internal_energy=config.numerics.max_eint;
    const auto plan=make_boundary_plan(2);
    const std::array handles{amr::BlockHandle{{3101},{31}},amr::BlockHandle{{3102},{31}}};
    const std::array storage{arch::backend::StorageGeneration{3101},arch::backend::StorageGeneration{3102}};
    const std::array bindings{arch::cuda::CudaBlockBinding{&blocks[0],handles[0],storage[0],&plan},
                             arch::cuda::CudaBlockBinding{&blocks[1],handles[1],storage[1],&plan}};
    auto backend=arch::cuda::make_cuda_backend(bindings,0,launch,species,eos);
    const std::array accesses{arch::backend::BackendStateAccess{handles[0],storage[0],arch::state::StateSlot::Current},
                             arch::backend::BackendStateAccess{handles[1],storage[1],arch::state::StateSlot::Current}};
    for (int b=0;b<2;++b)
        for (auto region:{arch::state::StateRegion::Interior,arch::state::StateRegion::Ghost})
            backend->enqueue_upload_slot(accesses[b],region,transfer_view(blocks[b].fluid_state));
    backend->quiesce();
    const auto results=backend->execute_burn_batch(accesses,interval,
        {3,arch::state::CompletionState::Complete});
    require(results.size()==2,"Native burn lost a batch result");
    for (const auto& result:results)
        require(result.status==0 && result.failed_cells==0 && arch::state::is_complete(result.completion),
            "Native actual backend burn rejected the physical closure");
    require(!backend->validate_completed_native_eos_batch(accesses,
        {launch.density_floor,launch.minimum_internal_energy,launch.maximum_internal_energy}),
        "Native burned state failed resident completed EOS acceptance");
    for (int b=0;b<2;++b) {
        auto actual=initial[b]; const auto& grid=blocks[b].grid;
        for (auto region:{arch::state::StateRegion::Interior,arch::state::StateRegion::Ghost})
            backend->enqueue_materialize_host_current(accesses[b],region,transfer_view(actual));
        backend->quiesce();
        for (auto member:{&FluidState::rho,&FluidState::mom_u,&FluidState::mom_v,&FluidState::mom_w})
            for (std::size_t k=0;k<(actual.*member).size();++k)
                require(std::bit_cast<std::uint64_t>((actual.*member)[k])
                    == std::bit_cast<std::uint64_t>((initial[b].*member)[k]),
                    "Native burner overwrote density/native moments or padding");
        const int hot=grid.GetIndex(grid.Is(),grid.Js(),0);
        require(actual.eng[hot]!=initial[b].eng[hot] && actual.enuc_rate[hot]!=0.,
            "Native paired burn check accepted a no-op");
        for (int j=0;j<grid.GetTotalY();++j) for (int i=0;i<grid.GetTotalX();++i) {
            const int cell=grid.GetIndex(i,j,0);
            double sum=0.;
            for (int n=0;n<NetAprox13::NUM_SPECIES;++n) {
                require(std::isfinite(actual.X(n,cell)) && actual.X(n,cell)>=0.
                    && std::abs(actual.X(n,cell)-expected[b].X(n,cell))<=1.e-8,
                    "Native Host/device species parity failed");
                sum+=actual.X(n,cell);
            }
            require(std::abs(sum-1.)<=1.e-12
                && std::isfinite(actual.eng[cell])
                && std::abs(actual.eng[cell]/expected[b].eng[cell]-1.)<=1.e-8,
                "Native Host/device energy or mass parity failed");
            if (cell!=hot)
                require(std::bit_cast<std::uint64_t>(actual.eng[cell])
                        == std::bit_cast<std::uint64_t>(initial[b].eng[cell])
                    && actual.enuc_rate[cell]==0.,"Native cold/ghost burn changed state");
        }
    }
    std::cout << "CUDA_NATIVE_MULTIBLOCK_BURN_PASS route=" << name << " blocks=2\n";
}

/** Failure-only Host observations before the original macro owner unwinds.
 * Reuse the original source/profile/EOS leaves on the actual three slots;
 * local delta/X scratch is never published and the original exception survives.
 */
void diagnose_native_geometric_source(arch::driver::DriverRuntime& runtime,
    const HelmEos& eos,double dt)
{
    if(runtime.backend())return;
    constexpr auto native=GridMetrics::GeometrySemantics::AxisymmetricRz;
    const char* slot_names[]{"Current","Next","Scratch"};int printed=0;
    const auto& active=runtime.control().tree->GetActiveBlocks();
    for(int slot=0;slot<3&&printed<4;++slot)for(int id:active) {
        const auto& block=runtime.control().pool->GetBlock(id);const auto& grid=block.grid;
        const FluidState* slots[]{&block.fluid_state,&block.state_next,&block.state_scratch};
        const auto& state=*slots[slot];const int species=state.GetNumSpecies();
        const auto geometry=GridMetrics::make_geometry_view(grid,native);
        const auto read=[&](int c){return state.get(c);};
        const auto fraction=[&](int s,int c){return state.X(s,c);};
        std::vector<double> x(species);
        for(int j=grid.Js();j<grid.Je()&&printed<4;++j)for(int i=grid.Is();i<grid.Ie()&&printed<4;++i) {
            const int index=grid.GetIndex(i,j,0);FluidVector probe_delta{};
            bool source_valid=false;std::string query_exception;
            try {source_valid=TimeIntegration::add_rz_integrated_geometric_source(
                read,fraction,index,species,eos,geometry,i,dt,x.data(),probe_delta);}
            catch(const std::exception& error){query_exception=error.what();}
            if(source_valid)continue;
            const auto cell=RzReconstruction::radial_cell(geometry,i);
            const auto baseline=RzThermodynamics::make_cell(read,index,geometry,i);
            const bool stencil=RzReconstruction::native_stencil_valid(read,fraction,index,species);
            const bool baseline_nodes=baseline.valid()&&RzReconstruction::baseline_nodes_valid(baseline,{});
            const auto profile=RzReconstruction::limited_profile(read,fraction,index,species,cell,baseline);
            int failed_node=-1,point_status=-1;const char* category=profile.valid?"integral_or_query":"profile";
            double pressure=std::numeric_limits<double>::quiet_NaN(),composition_sum=0.;
            double composition_min=std::numeric_limits<double>::infinity();
            for(int s=0;s<species;++s){const double value=fraction(s,index);composition_sum+=value;
                composition_min=std::min(composition_min,value);}
            if(profile.valid)for(int n=0;n<4;++n) {
                const double radius=RzReconstruction::certified_node_radius(n+2,baseline);
                const auto point=profile.at(radius);composition_sum=0.;composition_min=std::numeric_limits<double>::infinity();
                for(int s=0;s<species;++s) {
                    x[s]=RzReconstruction::limited_fraction(read,fraction,index,s,cell,radius,profile,point.rho);
                    composition_sum+=x[s];composition_min=std::min(composition_min,x[s]);
                }
                const auto status=arch::state::validate(point,x.data(),species,1,0.,0.,
                    std::numeric_limits<double>::max());point_status=static_cast<int>(status);
                if(status!=arch::state::Status::valid){failed_node=n;category="point";break;}
                try {pressure=eos.get_pressure(point,x.data());}
                catch(const std::exception& error){failed_node=n;category="pressure_exception";query_exception=error.what();break;}
                if(!std::isfinite(pressure)||!(pressure>0.)){failed_node=n;category="pressure";break;}
            }
            ++printed;
            std::cerr<<std::setprecision(17)<<"NATIVE_SOURCE_NONE_GEOMETRIC_DIAGNOSTIC side=Host slot="<<slot_names[slot]
                <<" block="<<id<<" i="<<i<<" j="<<j<<" cell="<<index<<" dt="<<dt
                <<" macro_transaction="<<bool(runtime.active_runtime_state_transaction())
                <<" category="<<category<<" profile="<<profile.valid<<" theta="<<profile.theta
                <<" baseline="<<baseline.valid()<<" baseline_status="<<static_cast<int>(baseline.status)
                <<" stencil="<<stencil<<" baseline_nodes="<<baseline_nodes
                <<" gauss_node="<<failed_node<<" point_status="<<point_status<<" pressure="<<pressure
                <<" composition_sum="<<composition_sum<<" composition_min="<<composition_min
                <<" composition_scope="<<(profile.valid?"last_queried_gauss":"native_central")
                <<" query_exception="<<query_exception<<'\n';
        }
        if(printed>=4)break;
    }
    std::cerr<<"NATIVE_SOURCE_NONE_GEOMETRIC_DIAGNOSTIC_COMPLETE invalid_cells="<<printed
        <<" limit=4 original_exception_rethrown=1\n";
}

/** Exercise the actual source-free Native macro with its paired Host owner.
 * Workflow: real Tree/Runtime/EOS -> resident upload -> original five segments
 * -> actual endpoint acceptance -> quiescent comparison. The mixed domain also
 * rejects a real late endpoint callback, checks the complete owner, and reuses
 * that same resident allocation. This candidate has no per-burn energy ledger;
 * final ENUC is never integrated as a substitute for the two burn half-steps.
 */
void run_native_source_none_macro(bool mixed)
{
    using namespace arch;using state::StateSlot;using state::StateRegion;
    constexpr auto native=GridMetrics::GeometrySemantics::AxisymmetricRz;
    constexpr double dt=2.e-12;const double pi=std::acos(-1.);
    struct ActivityEnvironment {
        std::optional<std::string> old;
        ActivityEnvironment(){if(const auto* v=std::getenv("ARCH_TRACE_DIFFUSION_ACTIVITY"))old=v;
            require(::setenv("ARCH_TRACE_DIFFUSION_ACTIVITY","1",1)==0,"Cannot enable actual diffusion activity");}
        ~ActivityEnvironment(){if(old)::setenv("ARCH_TRACE_DIFFUSION_ACTIVITY",old->c_str(),1);
            else ::unsetenv("ARCH_TRACE_DIFFUSION_ACTIVITY");}
    } activity_environment;
    SpeciesManager species;NetAprox13::RegisterSpecies(species);
    HelmEos eos(std::string(ARCH_SOURCE_DIR)+"/EOS_toolkit/tables/helmholtz/helm_table.dat",&species);
    const int count=species.count();require(count==13,"SourceNone lost actual aprox13 species");
    SimConfig config;config.grid.dim=2;config.grid.geometry="cylindrical";
    config.grid.nblockx1=2;config.grid.nblockx2=1;config.grid.nblockx3=0;
    config.grid.x1_min=mixed?1.:0.;config.grid.x1_max=config.grid.x1_min+2.;
    config.grid.x2_min=-1.;config.grid.x2_max=1.;config.grid.amr_max_blocks=16;
    config.grid.x1l_boundary_type=config.grid.x1r_boundary_type="reflecting";
    config.grid.x2l_boundary_type="reflecting";config.grid.x2r_boundary_type=mixed?"user":"reflecting";
    config.amr.lrefinemin=0;config.amr.lrefinemax=1;
    config.numerics.sml_rho=config.numerics.min_eint=1.e-14;config.numerics.max_eint=1.e24;
    config.numerics.cfl=.4;config.numerics.dt_init=dt;config.numerics.dt_max=dt;
    config.numerics.entropy_fix_coeff=0.;config.numerics.hll_roe_wave_speed=true;
    config.numerics.solver_name="HLLC";config.numerics.reconstruction="ppm";
    config.numerics.limiter="minmod";config.numerics.time_integrator="rk2";
    config.physics.eos_type="helmholtz";config.physics.burn=make_burn_config();
    config.physics.gravity.type="none";config.io.tmax=1.;
    auto& diffusion=config.physics.diffusion;diffusion.use_diffusion=diffusion.use_thermal_diffusion=true;
    diffusion.use_viscous_diffusion=diffusion.use_species_diffusion=false;
    diffusion.alpha_therm=0.;diffusion.integrator="RKL2";
    const dispatch::ResolvedExecutionPlan selected{dispatch::FluxId::Hllc,dispatch::ReconstructionId::Ppm,
        dispatch::LimiterId::MinMod,dispatch::TimeIntegratorId::Rk2,dispatch::EosId::Helmholtz,
        dispatch::NetworkId::Aprox13,dispatch::OdeSolverId::Bd,dispatch::LinearSolverId::DenseLu,
        dispatch::DiffusionIntegratorId::Rkl2};
    bool inject_endpoint_fault=false;std::atomic<bool> fault_armed{false};
    std::atomic<int> late_calls{0};double fault_time=0.;
    boundary::ResolvedUserBoundaries callbacks;callbacks.identity="native-source-none-real-macro";
    if(mixed)callbacks.physical=[&](const boundary::PhysicalBoundaryContext& request){
        const bool upper_z=request.axis==boundary::BoundaryAxis::X2
            &&request.side==boundary::BoundarySide::Upper;
        // Diffusion asks every physical face for optional flux/traction data.
        // An empty response retains its configured reflecting boundary; only
        // the actual upper-z user face supplies the mirrored hydro primitive.
        if(request.purpose==boundary::BoundaryPurpose::Diffusion&&!upper_z)
            return boundary::PhysicalBoundaryData{};
        require(upper_z,"SourceNone callback escaped actual upper-z face");
        if(fault_armed.load()&&request.purpose==boundary::BoundaryPurpose::Hydro
            &&request.time==fault_time&&request.ghost_point.r_cy>2.5){
            ++late_calls;throw std::runtime_error("Native SourceNone late endpoint callback fault");}
        auto primitive=request.interior;primitive.v=-primitive.v;
        boundary::PhysicalBoundaryData result;result.hydro=std::move(primitive);return result;
    };
    boundary::ScopedUserBoundarySelection selection(callbacks,config,species);
    amr::AMRControl host_control(16,2),device_control(16,2);
    const auto seed=[&](amr::AMRControl& owner){
        if(mixed)owner.tree->LoadLeafGrid(config,count,{1,1,1,1,0},{0,1,0,1,1},
            {0,0,1,1,0},{0,0,0,0,0},native);
        else owner.tree->InitRootGrid(config,count,native);
        require(owner.tree->GetActiveBlocks().size()==(mixed?5u:2u),"SourceNone actual domain extent changed");
        for(int id:owner.tree->GetActiveBlocks()){
            auto& block=owner.pool->GetBlock(id);block.RequireNativeGeometryIdentity();const auto& g=block.grid;
            require(g.ng>=3,"SourceNone lost actual PPM/axis stencil");
            std::vector<double> x(count);double total=0.;for(int n=0;n<count;++n){x[n]=n+1+id;total+=x[n];}
            for(auto& value:x)value/=total;
            for(auto* u:{&block.fluid_state,&block.state_next,&block.state_scratch}){
                u->stage_repairs.reset(count,state::RepairSemantics::RzVolumeAngular);
                for(int j=0;j<g.GetTotalY();++j)for(int i=0;i<g.GetTotalX();++i){
                    const int c=g.GetIndex(i,j,0);const long double a=g.GetFacePosL(i),b=g.GetFacePosR(i);
                    const long double l=std::min(std::abs(a),std::abs(b)),h=std::max(std::abs(a),std::abs(b));
                    const long double V=(h*h-l*l)/2.L,W=(h*h*h-l*l*l)/3.L,I=(h*h*h*h-l*l*l*l)/4.L;
                    const double r=g.GetCellCenterX(i),z=g.GetCellCenterY(j),rho=1.e6*(2.+.02*z);
                    const double vr=.03e6*std::sin(pi*(r-config.grid.x1_min)/2.);
                    const double vz=.02e6*std::sin(pi*(z+1.)/2.),omega=.15e7*(1.+.05*std::cos(pi*z));
                    const double temperature=i==g.Is()&&j==g.Js()?2.e9:5.e7;
                    const double thermal=rho*eos.get_eint_from_T(rho,temperature,x.data());
                    u->set(c,{rho,rho*vr,rho*vz,double((b<=0.?-1.:1.)*rho*omega*I/W),
                        double(thermal+.5L*rho*(vr*vr+vz*vz+omega*omega*I/V))});
                    u->enuc_rate[c]=-6.25;for(int n=0;n<count;++n)u->X(n,c)=x[n];
                }
            }
        }
    };seed(host_control);seed(device_control);
    RunState start;start.repairs.reset(count,state::RepairSemantics::RzVolumeAngular);
    SimulationController host_clock(config,start),device_clock(config,start);
    BCHandler host_boundary(config,native),device_boundary(config,native);
    host_boundary.bind(eos,species);device_boundary.bind(eos,species);
    host_boundary.configure_stage(0.,boundary::BoundaryPurpose::Hydro);
    device_boundary.configure_stage(0.,boundary::BoundaryPurpose::Hydro);
    driver::DriverRuntime host(host_control,host_boundary,config,species,host_clock);
    driver::DriverRuntime device(device_control,device_boundary,config,species,device_clock);
    host.bind_native_rz_eos(eos);device.bind_native_rz_eos(eos);host.initialize_topology();device.initialize_topology();
    const auto recipes=device.prepare_backend_bindings();std::vector<cuda::CudaBlockBinding> bindings;
    for(const auto& entry:recipes)bindings.push_back({entry.block,entry.handle,entry.storage,entry.physical_boundary});
    device.install_backend(cuda::make_cuda_backend(bindings,0,cuda::make_cuda_launch_config(selected,config),species,eos));
    device.upload_initial_state();auto* const backend=device.backend();
    const auto topology=device_control.RequireFluxTopologyPlan(count,native,-1,true);
    const auto reflux=device_control.RequireRefluxTopologyPlan(count,native,true);
    require(topology.angular_transport&&topology.semantics==native,"SourceNone bootstrap lost actual angular topology");
    require(mixed?!reflux.operations.empty():reflux.operations.empty(),"SourceNone actual CF reflux mismatch");
    // The source candidate supplies native bootstrap. Do not mask a false-mode
    // bootstrap by reinstalling a different plan in this fixture.
    host.ensure_fluid_ghosts();device.ensure_fluid_ghosts();
    driver::DriverStageWorkspace host_workspace,device_workspace;
    backend->copy_state_slot_batch(device_workspace.current_accesses(device),StateSlot::Next);
    backend->copy_state_slot_batch(device_workspace.current_accesses(device),StateSlot::Scratch);backend->quiesce();
    dispatch::BackendResolution cpu_resolution{},cuda_resolution{};
    cpu_resolution.resolved_backend=dispatch::ComputeBackend::Cpu;
    cuda_resolution.resolved_backend=dispatch::ComputeBackend::Cuda;
    auto host_burn=BurnDispatcher::make_host_handle<HelmEos>(config,selected,cpu_resolution);
    auto device_burn=BurnDispatcher::make_host_handle<HelmEos>(config,selected,cuda_resolution);
    Numerics::HydroSolverImpl<HelmEos,FluxHLLC<PPMReconstruction>> hydro(eos,native);
    const auto bits=[](double a,double b){return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);};
    const auto monotone=[](const backend::BackendCounters& now,const backend::BackendCounters& old){
        return now.kernel_count>=old.kernel_count&&now.bytes_h2d>=old.bytes_h2d
            &&now.bytes_d2h>=old.bytes_d2h&&now.stream_sync_count>=old.stream_sync_count&&now.getter_count>=old.getter_count;
    };
    const auto same_repair=[&](const state::RepairBudget& a,const state::RepairBudget& b){
        return a.values.size()==b.values.size()&&std::equal(a.values.begin(),a.values.end(),b.values.begin(),bits)
            &&a.semantics==b.semantics&&a.block_uid==b.block_uid&&a.stage==b.stage&&bits(a.time,b.time)
            &&std::equal(std::begin(a.position),std::end(a.position),std::begin(b.position),bits);
    };
    const auto observe=[&]{
        require(!device.active_runtime_state_transaction(),"SourceNone observed armed resident fields");
        std::vector<FluidState> result;const auto& active=device_control.tree->GetActiveBlocks();
        for(int id:active)result.push_back(device_control.pool->GetBlock(id).fluid_state);
        for(std::size_t n=0;n<result.size();++n)for(auto region:{StateRegion::Interior,StateRegion::Ghost})
            backend->enqueue_materialize_host_current(device.backend_access(n,StateSlot::Current),region,transfer_view(result[n]));
        backend->quiesce();return result;
    };
    const auto all_slots=[&]{
        std::array<std::vector<FluidState>,3> result;
        constexpr state::SlotRotation cycle{StateSlot::Next,StateSlot::Scratch,StateSlot::Current};
        for(auto& slot:result){slot=observe();for(std::size_t n=0;n<device.handles().size();++n)
            backend->rotate_slots(device.backend_access(n,StateSlot::Current),cycle);}
        return result;
    };
    const auto same_fields=[&](const auto& a,const auto& b){
        require(a.size()==b.size(),"SourceNone resident rollback changed extent");
        for(std::size_t n=0;n<a.size();++n){const auto& g=device_control.pool->GetBlock(device_control.tree->GetActiveBlocks()[n]).grid;
            // Only the public region-visible cells are evidence; padding is not.
            for(int j=0;j<g.GetTotalY();++j)for(int i=0;i<g.GetTotalX();++i){const int c=g.GetIndex(i,j,0);
                for(auto field:{&FluidState::rho,&FluidState::mom_u,&FluidState::mom_v,&FluidState::mom_w,&FluidState::eng,&FluidState::enuc_rate})
                    require(bits((a[n].*field)[c],(b[n].*field)[c]),"SourceNone rollback changed actual slot bits");
                for(int s=0;s<count;++s)require(bits(a[n].X(s,c),b[n].X(s,c)),"SourceNone rollback changed composition bits");}
        }
    };
    const auto totals=[&](const auto& values){std::array<long double,3> sum{};
        const auto& active=device_control.tree->GetActiveBlocks();
        for(std::size_t n=0;n<values.size();++n){const auto& g=device_control.pool->GetBlock(active[n]).grid;
            for(int j=g.Js();j<g.Je();++j)for(int i=g.Is();i<g.Ie();++i){const int c=g.GetIndex(i,j,0);
                const long double a=g.GetFacePosL(i),b=g.GetFacePosR(i);
                const long double V=std::acos(-1.L)*(b*b-a*a)*g.dx2,W=2.L*std::acos(-1.L)*(b*b*b-a*a*a)*g.dx2/3.L;
                sum[0]+=values[n].rho[c]*V;sum[1]+=values[n].eng[c]*V;sum[2]+=values[n].mom_w[c]*W;}}
        return sum;
    };
    const auto initial=observe();const auto initial_totals=totals(initial);
    double host_advice=1.e99,device_advice=1.e99;
    const auto execute=[&](driver::DriverRuntime& runtime,BCHandler& bc,SimulationController& clock,
        driver::DriverStageWorkspace& workspace,const auto& burner,bool late_fault){
        const auto candidates=driver::calculate_timestep_candidates(runtime,workspace,eos,&selected);
        require(std::isfinite(candidates.diffusion_forward_euler)&&candidates.diffusion_forward_euler>0.
            &&dt<=candidates.hydro&&dt<=candidates.diffusion_sts,"Frozen SourceNone dt is not admissible");
        auto context=runtime.stage_context();context.hydro_preparation=nullptr;
        context.step_start_time=clock.t_current;context.step_dt=dt;
        context.configure_boundary_context=[&](double time,boundary::BoundaryPurpose purpose){
            bc.configure_stage(time,purpose);runtime.bind_native_boundary_acceptance(context,runtime.handles());};
        context.physical_boundary_preparation=[&](StateSlot slot,double time,boundary::BoundaryPurpose purpose){
            context.configure_boundary_context(time,purpose);runtime.ensure_fluid_ghosts(slot);};
        context.configure_boundary_context(clock.t_current,boundary::BoundaryPurpose::Hydro);
        runtime.bind_boundary_accounting(context);scheduler::ScopedStageBinding binding(context,runtime.handles());
        int burns=0,diffusions=0,hydros=0,attempts=0;
        double& advice=runtime.backend()?device_advice:host_advice;const double advice_before=advice;
        const auto activity_before=runtime.diffusion_activity_totals();
        const auto activity_work_before=runtime.diffusion_activity_operations();
        auto saved_owner=driver::RuntimeStateTransaction::snapshot_owner(runtime,context);
        const auto slots=late_fault?all_slots():std::array<std::vector<FluidState>,3>{};
        const auto surfaces=late_fault?backend->download_boundary_flux_capture():std::vector<backend::BoundaryFluxPlanes>{};
        const auto stage_receipt=backend->stage_repairs,reflux_receipt=backend->reflux_repairs;
        const auto work_before=backend->counters();const auto trace_before=backend->trace_snapshot().size();
        fault_time=clock.t_current+dt;inject_endpoint_fault=late_fault;fault_armed=false;late_calls=0;
        bool rejected=false;const double dt_before=clock.dt_old;
        const auto actual_macro=[&](double accepted_dt,std::uint64_t index){
                ++attempts;require(index==1&&accepted_dt==dt,"SourceNone changed frozen Device attempt");
                driver::execute_driver_macro_step(runtime,context,&hydro,true,
                    [&](driver::BurnHalf half,double half_dt,state::CompletionToken token){
                        require(half_dt==.5*dt&&half==(burns==0?driver::BurnHalf::First:driver::BurnHalf::Second),
                            "SourceNone changed actual burn half order/interval");
                        const auto completion=driver::execute_burn_half(runtime,workspace,eos,burner,half,half_dt,advice,token);
                        ++burns;require(std::isfinite(advice)&&advice>0.,"Actual burn lost finite timestep advice");
                        if(half==driver::BurnHalf::Second&&inject_endpoint_fault)fault_armed=true;return completion;},
                    [&](double half_dt){require(half_dt==.5*dt,"SourceNone changed actual diffusion half interval");
                        driver::advance_diffusion(runtime,workspace,context,eos,&selected,clock.step_count,
                        half_dt,candidates.diffusion_forward_euler);++diffusions;},
                    [&](double hydro_dt){
                        try {driver::advance_hydro(runtime,workspace,context,&selected,hydro_dt,
                            &SolverRK2::solve<BCHandler>,nullptr,&hydro,driver::RuntimeStateQualification::Production);}
                        catch(const std::runtime_error& error) {
                            if(!runtime.backend()&&std::string(error.what())=="RZ geometric source has an inadmissible point/stencil") {
                                try {diagnose_native_geometric_source(runtime,eos,hydro_dt);}
                                catch(...) {} // Diagnostics must not replace the original macro refusal.
                            }
                            throw;
                        }
                        ++hydros;
                    },
                    [&](driver::CpuStage,auto&& operation){operation();});
        };
        if(&runtime==&device&&clock.step_count==1&&!late_fault){
            // Actual configuration changes are rejected by the same macro
            // before any of the three real module callables is entered.
            const auto cold_fields=all_slots();
            const auto cold_owner=driver::RuntimeStateTransaction::snapshot_owner(runtime,context);
            for(const char* source:{"external","self"}){
                const auto before=backend->counters();const auto trace=backend->trace_snapshot().size();
                config.physics.gravity.type=source;bool cold_rejected=false;
                try{actual_macro(dt,1);}catch(const std::logic_error&){cold_rejected=true;}
                config.physics.gravity.type="none";
                auto expected=before;expected.getter_count+=2; // Trace snapshot and this counter observation.
                const auto after_work=backend->counters();
                if(!cold_rejected||burns!=0||diffusions!=0||hydros!=0||after_work!=expected)
                    std::cerr<<"SOURCE_NONE_PREFLIGHT_DIAGNOSTIC source="<<source
                        <<" rejected="<<cold_rejected<<" burns="<<burns<<" diffusions="<<diffusions
                        <<" hydros="<<hydros<<" attempts="<<attempts
                        <<" kernel_delta="<<(after_work.kernel_count-before.kernel_count)
                        <<" h2d_delta="<<(after_work.bytes_h2d-before.bytes_h2d)
                        <<" d2h_delta="<<(after_work.bytes_d2h-before.bytes_d2h)
                        <<" sync_delta="<<(after_work.stream_sync_count-before.stream_sync_count)
                        <<" getter_delta="<<(after_work.getter_count-before.getter_count)<<std::endl;
                require(cold_rejected&&burns==0&&diffusions==0&&hydros==0
                    &&after_work==expected&&backend->trace_snapshot().size()==trace,
                    "SourceNone source preflight did actual split work");
                require(driver::RuntimeStateTransaction::owner_matches(runtime,context,cold_owner),
                    "SourceNone cold refusal changed accepted Runtime metadata");
                const auto after=all_slots();for(int slot=0;slot<3;++slot)same_fields(after[slot],cold_fields[slot]);
            }
            attempts=0;
        }
        try{const double actual_dt=driver::execute_driver_macro_attempts(runtime,clock,advice,dt,actual_macro);
            require(actual_dt==dt,"SourceNone accepted a different macro interval");
        }catch(const std::runtime_error& error){if(!late_fault)throw;
            rejected=std::string(error.what())=="Native SourceNone late endpoint callback fault";if(!rejected)throw;}
        fault_armed=false;inject_endpoint_fault=false;
        require(burns==2&&diffusions==2&&hydros==1&&attempts==1,"SourceNone did not execute the original five real segments once");
        require(!runtime.active_runtime_state_transaction(),"SourceNone leaked a macro owner");
        if(&runtime==&device){std::size_t actual_burn_records=0;std::uint64_t burn_kernels=0,burn_joins=0;
            const auto trace=backend->trace_snapshot();for(std::size_t t=trace_before;t<trace.size();++t)
                if(trace[t].operation==backend::BackendOperation::Burn){const auto& record=trace[t];
                    require(record.slot==StateSlot::Current,
                        "SourceNone burn trace is not actual completed resident work");
                    bool authentic=false;for(std::size_t n=0;n<runtime.handles().size();++n){const auto access=runtime.backend_access(n,StateSlot::Current);
                        authentic|=record.block==access.block&&record.storage==access.storage;}
                    require(authentic,"SourceNone burn trace lost actual handle/storage identity");
                    burn_kernels+=record.kernel_count;burn_joins+=record.stream_sync_count;++actual_burn_records;}
            require(actual_burn_records==2*runtime.handles().size()&&burn_kernels>0&&burn_joins>=2,"SourceNone lost either completed real burn trace");}

        const auto& accepted=runtime.diffusion_activity_totals();
        if(late_fault){require(rejected&&late_calls>0,"SourceNone fault was not an actual late endpoint callback");
            require(bits(clock.dt_old,dt_before)&&bits(advice,advice_before),"SourceNone rejected attempt leaked accepted advice");
            require(monotone(runtime.boundary_observer_operations(),saved_owner.counters)
                &&monotone(runtime.diffusion_activity_operations(),activity_work_before),
                "SourceNone rollback erased actual observer/activity work");
            // Work counters remain monotone; the original noncopyable owner snapshot
            // still authenticates every accepted field/ledger/register identity.
            saved_owner.counters=runtime.boundary_observer_operations();
            require(driver::RuntimeStateTransaction::owner_matches(runtime,context,saved_owner),"SourceNone rollback changed exact Runtime owner");
            require(accepted.signed_energy_change==activity_before.signed_energy_change
                &&accepted.absolute_energy_change==activity_before.absolute_energy_change&&accepted.cells==activity_before.cells
                &&accepted.accepted_halves==activity_before.accepted_halves&&accepted.accepted_macros==activity_before.accepted_macros
                &&bits(accepted.process_start_time,activity_before.process_start_time)
                &&accepted.process_start_step==activity_before.process_start_step,
                "SourceNone rejected macro promoted activity");
            require(same_repair(backend->stage_repairs,stage_receipt)&&same_repair(backend->reflux_repairs,reflux_receipt),
                "SourceNone rollback changed actual accepted backend receipts");
            const auto work_after=backend->counters();require(monotone(work_after,work_before)
                &&work_after.kernel_count>work_before.kernel_count&&backend->trace_snapshot().size()>trace_before,
                "SourceNone rollback erased actual completed work");
            const auto restored=all_slots();for(int n=0;n<3;++n)same_fields(restored[n],slots[n]);
            const auto observed=backend->download_boundary_flux_capture();require(observed.size()==surfaces.size(),"SourceNone lost observer owners");
            for(std::size_t n=0;n<surfaces.size();++n){require(observed[n].block==surfaces[n].block,"SourceNone changed observer identity");
                for(int f=0;f<6;++f)for(int plane=0;plane<2;++plane){const auto& a=plane?observed[n].initial[f]:observed[n].stage[f];
                    const auto& b=plane?surfaces[n].initial[f]:surfaces[n].stage[f];
                    require(a.size()==b.size()&&std::equal(a.begin(),a.end(),b.begin(),bits),"SourceNone observer rollback changed bits");}}
        }else{require(!rejected&&accepted.accepted_halves==activity_before.accepted_halves+2
                &&accepted.accepted_macros==activity_before.accepted_macros+1&&accepted.cells>activity_before.cells,
                "SourceNone accepted activity promotion lost real halves");
            require(std::isfinite(accepted.signed_energy_change)&&std::isfinite(accepted.absolute_energy_change)
                &&(accepted.absolute_energy_change-activity_before.absolute_energy_change)
                    /std::max(1.L,std::abs(initial_totals[1]))>64.*std::numeric_limits<double>::epsilon(),
                "SourceNone diffusion produced no resolvable actual aggregate activity");
            for(auto handle:runtime.handles()){const auto current=context.ledger.inspect({handle,StateSlot::Current});
                scheduler::detail::require_settled_destination(current);
                context.ledger.require_readable({handle,StateSlot::Current},{context.side,current.interior.version,true,true});}
            clock.advance(dt);
        }
    };
    const auto compare=[&]{const auto values=observe();double transport=0.,composition=0.,last_burn_enuc=0.;
        const auto& active=device_control.tree->GetActiveBlocks();
        for(std::size_t n=0;n<values.size();++n){const auto& g=device_control.pool->GetBlock(active[n]).grid;
            const auto& reference=host_control.pool->GetBlock(host_control.tree->GetActiveBlocks()[n]).fluid_state;
            for(int j=g.Js();j<g.Je();++j)for(int i=g.Is();i<g.Ie();++i){const int c=g.GetIndex(i,j,0);double sum=0.;
                require(std::isfinite(values[n].enuc_rate[c]),"SourceNone final actual burn diagnostic is not finite");
                last_burn_enuc=std::max(last_burn_enuc,std::abs(values[n].enuc_rate[c]));
                for(auto field:{&FluidState::rho,&FluidState::mom_u,&FluidState::mom_v,&FluidState::mom_w,&FluidState::eng}){
                    const double h=(reference.*field)[c],d=(values[n].*field)[c];
                    require(std::isfinite(d)&&std::abs(d-h)/std::max(1.,std::abs(h))<=1.e-8,"SourceNone actual macro endpoint parity exceeded frozen budget");}
                transport=std::max(transport,std::abs(values[n].mom_u[c]-initial[n].mom_u[c])/std::max(1.,std::abs(initial[n].mom_u[c])));
                for(int s=0;s<count;++s){const double x=values[n].X(s,c);sum+=x;
                    require(std::isfinite(x)&&x>=0.&&std::abs(x-reference.X(s,c))<=1.e-8,"SourceNone actual composition is invalid");
                    composition=std::max(composition,std::abs(x-initial[n].X(s,c)));}
                require(std::abs(sum-1.)<=1.e-12,"SourceNone composition is not normalized");}
        }
        require(transport>64.*std::numeric_limits<double>::epsilon()&&composition>64.*std::numeric_limits<double>::epsilon()&&last_burn_enuc>0.,
            "SourceNone actual combined transport or burning remained inactive");
        const auto final=totals(values);const auto& h=device.hydro_boundary_budget();const auto& d=device.diffusion_boundary_budget();
        require(h.size()==static_cast<std::size_t>(6+count)&&d.size()==h.size(),"SourceNone lost actual boundary budgets");
        for(int f:{0,2}){const int field=f==0?0:3;const long double residual=final[f]-initial_totals[f]+h[field]+d[field];
            require(std::abs(residual)/std::max(std::abs(final[f]),std::abs(initial_totals[f]))<=2.e-12L,
                "SourceNone independent V/W physical budget exceeded frozen tolerance");}
        for(auto* owner:{&host,&device}){const auto& repair=owner->repair_budget();
            require(repair.semantics==state::RepairSemantics::RzVolumeAngular
                &&std::all_of(repair.values.begin(),repair.values.end(),[](double value){return value==0.;}),
                "SourceNone macro performed actual repairs");}
        require(host_clock.t_current==device_clock.t_current&&host_clock.step_count==device_clock.step_count,"SourceNone endpoint clocks differ");
    };
    execute(host,host_boundary,host_clock,host_workspace,host_burn,false);
    execute(device,device_boundary,device_clock,device_workspace,device_burn,false);compare();
    if(mixed){execute(device,device_boundary,device_clock,device_workspace,device_burn,true);
        execute(host,host_boundary,host_clock,host_workspace,host_burn,false);
        execute(device,device_boundary,device_clock,device_workspace,device_burn,false);compare();}
    std::cout<<"CUDA_NATIVE_SOURCE_NONE_MACRO_CANDIDATE mixed="<<mixed<<" real_owner=1 endpoint_parity=1\n";
}

} // namespace

int main(int argc, char** argv)
{
    try {
        std::size_t count = 2;
        if (argc == 2) {
            const std::string arg = argv[1];
            std::size_t used = 0;
            count = std::stoull(arg, &used);
            require(used == arg.size() && count >= 2 && count <= 1025,
                    "burn test block count must be 2..1025");
        } else require(argc == 1, "burn test accepts at most one block count");
        for (const auto ode : {arch::dispatch::OdeSolverId::BeNr, arch::dispatch::OdeSolverId::Bd,
                              arch::dispatch::OdeSolverId::Ros4}) {
            const auto sequential = run_route(ode, "sequential", false, count);
            const auto batched = run_route(ode, "batched", true, count);
            for (std::size_t i = 0; i < sequential.size(); ++i)
                for (const auto member : {&FluidState::rho, &FluidState::mom_u, &FluidState::mom_v,
                     &FluidState::mom_w, &FluidState::eng, &FluidState::enuc_rate, &FluidState::mass_fractions}) {
                    const auto& a = sequential[i].*member;
                    const auto& b = batched[i].*member;
                    require(a.size() == b.size(), "Batch changed storage size");
                    for (std::size_t j = 0; j < a.size(); ++j)
                        require(std::bit_cast<std::uint64_t>(a[j]) == std::bit_cast<std::uint64_t>(b[j]),
                            "Burn batch changed field bits");
                }
        }
        run_native_route<Solver_BE_NR>(arch::dispatch::OdeSolverId::BeNr,"be_nr");
        run_native_route<Solver_BD>(arch::dispatch::OdeSolverId::Bd,"bd");
        run_native_route<Solver_ROS4>(arch::dispatch::OdeSolverId::Ros4,"ros4");
#if ARCH_HAS_CUDSS_PROVIDER
        run_native_route<Solver_BE_NR>(arch::dispatch::OdeSolverId::BeNr,"be_nr_cudss",arch::dispatch::LinearSolverId::CuDss);
        run_native_route<Solver_BD>(arch::dispatch::OdeSolverId::Bd,"bd_cudss",arch::dispatch::LinearSolverId::CuDss);
        run_native_route<Solver_ROS4>(arch::dispatch::OdeSolverId::Ros4,"ros4_cudss",arch::dispatch::LinearSolverId::CuDss);
#endif
        run_native_source_none_macro(false);
        run_native_source_none_macro(true);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
