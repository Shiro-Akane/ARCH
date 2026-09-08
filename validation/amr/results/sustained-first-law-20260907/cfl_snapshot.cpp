// Diagnostic only: reuse the actual checkpoint reader, backend and CFL math.
// No time evolution or copied thermodynamic formula is introduced here.
#include "amr/AMRControl.h"
#include "core/RuntimeParams.h"
#include "cuda/runtime/CudaBackend.h"
#include "driver/DriverUtils.h"
#include "io/IO.h"
#include "io/chk/CheckpointCompatibility.h"
#include "io/hdf5/HDF5Writer.h"
#include "physics/eos/HelmEos.h"

#include <bit>
#include <filesystem>
#include <iomanip>
#include <iostream>

arch::backend::HostStateTransferView transfer(FluidState& state)
{
    return {state.rho.data(), state.mom_u.data(), state.mom_v.data(),
        state.mom_w.data(), state.eng.data(), state.enuc_rate.data(),
        state.mass_fractions.data(), state.rho.size(),
        static_cast<std::size_t>(state.GetNumSpecies()), state.rho.size()};
}

int main(int argc, char** argv)
{
    if (argc != 4) throw std::runtime_error("expected checkpoint, parameters, output directory");
    auto config = RuntimeParams::Load(argv[2]);
    const auto saved = io::read_hdf5_chk_impl(argv[1]);
    if (!saved.has_mass_fractions || saved.provenance.eos_type != "helmholtz")
        throw std::runtime_error("probe requires a native-composition Helm checkpoint");
    SpeciesManager species;
    const auto& identity = saved.provenance;
    for (int s = 0; s < saved.num_species; ++s)
        species.add_species(identity.species_names[s], identity.species_A[s],
            identity.species_Z[s], identity.species_gamma[s], identity.species_Cv[s]);
    const auto expected = io::inspect_checkpoint_provenance(config, species,
        arch::dispatch::EosId::Helmholtz, config.physics.burn.use_burn,
        config.physics.burn.network_name, config.physics.burn.use_nse);
    amr::AMRControl control(config.grid.amr_max_blocks, config.grid.dim);
    RunState restored;
    read_chk(argv[1], control, restored, config, species, expected);
    config.io.out_dir = argv[3];
    const auto save = [&](const char* name) {
        config.io.base_name = name;
        write_chk(control, restored.chk_idx, restored.plt_idx, restored.step,
            restored.time, restored.dt_old, restored.dt_burn,
            restored.resume_after_regrid, config, species, expected);
    };
    save("host");
    HelmEos eos(config.physics.eos_table_path, &species);
    // The diagnostic only invokes storage and CFL; disable unused burn lanes.
    auto launch_config = config;
    launch_config.physics.burn.use_burn = false;
    using namespace arch::dispatch;
    auto launch = arch::cuda::make_cuda_launch_config({FluxId::Hllc,
        ReconstructionId::Muscl, LimiterId::Mc, TimeIntegratorId::Rk2,
        EosId::Helmholtz, NetworkId::None, OdeSolverId::None, LinearSolverId::None,
        DiffusionIntegratorId::None}, launch_config);
    BCHandler boundary(config);
    arch::backend::StorageGenerationIssuer issuer(1);
    std::vector<arch::backend::BackendStateAccess> accesses;
    std::vector<arch::cuda::CudaBlockBinding> bindings;
    for (const int id : control.tree->GetActiveBlocks()) {
        const auto handle = amr::BlockHandle{{static_cast<std::uint64_t>(accesses.size() + 1)}, {1}};
        accesses.push_back({handle, issuer.issue(), arch::state::StateSlot::Current});
        bindings.push_back({&control.pool->GetBlock(id), handle,
            accesses.back().storage, &boundary.logical_plan()});
    }
    auto backend = arch::cuda::make_cuda_backend(bindings, 0, launch, species, eos);
    for (std::size_t b = 0; b < bindings.size(); ++b) {
        auto& state = control.pool->GetBlock(control.tree->GetActiveBlocks()[b]).fluid_state;
        backend->enqueue_upload_slot(accesses[b], arch::state::StateRegion::Interior, transfer(state));
    }
    backend->quiesce();
    double cpu_min = std::numeric_limits<double>::infinity(), cuda_min = cpu_min;
    for (std::size_t b = 0; b < bindings.size(); ++b) {
        auto& block = control.pool->GetBlock(control.tree->GetActiveBlocks()[b]);
        const auto before = block.fluid_state;
        const double cpu_dt = adaptive_dt(before, eos.get_view(), block.grid, config.numerics.cfl);
        const double cuda_dt = backend->compute_hydro_dt(accesses[b], config.numerics.cfl);
        cpu_min = std::min(cpu_min, cpu_dt); cuda_min = std::min(cuda_min, cuda_dt);
        backend->enqueue_materialize_host_current(accesses[b], arch::state::StateRegion::Interior,
            transfer(block.fluid_state));
        backend->quiesce();
        const auto exact = [](const auto& a, const auto& b) {
            if (a.size() != b.size()) throw std::runtime_error("transfer changed shape");
            for (std::size_t i = 0; i < a.size(); ++i)
                if (std::bit_cast<std::uint64_t>(a[i]) != std::bit_cast<std::uint64_t>(b[i]))
                    throw std::runtime_error("device round trip changed native state bits");
        };
        exact(before.rho, block.fluid_state.rho);
        exact(before.mom_u, block.fluid_state.mom_u);
        exact(before.mom_v, block.fluid_state.mom_v);
        exact(before.mom_w, block.fluid_state.mom_w);
        exact(before.eng, block.fluid_state.eng);
        exact(before.enuc_rate, block.fluid_state.enuc_rate);
        exact(before.mass_fractions, block.fluid_state.mass_fractions);
        std::cout << std::setprecision(17) << "CFL_SNAPSHOT block=" << b
            << " cpu=" << cpu_dt << " cuda=" << cuda_dt
            << " native_transfer_exact=true\n";
    }
    save("cuda");
    std::cout << std::setprecision(17) << "CFL_MIN cpu=" << cpu_min << " cuda=" << cuda_min
        << " relative_difference=" << std::abs(cpu_min - cuda_min) / cpu_min << '\n';
}
