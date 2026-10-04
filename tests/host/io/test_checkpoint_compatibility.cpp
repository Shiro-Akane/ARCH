/**
 * @file test_checkpoint_compatibility.cpp
 * @brief Check checkpoint identities and lossless state restoration.
 *
 * Exercise file fingerprints, physical-configuration matching, HDF5 round trips
 * and native mass fractions, including explicitly rejected incompatibilities.
 */
#include "amr/AMRControl.h"
#include "core/config/ConfigValidation.h"
#include "core/files/FileFingerprint.h"
#include "io/IO.h"
#include "grid/GridMetrics.h"
#include "io/chk/CheckpointCompatibility.h"
#include "io/hdf5/HDF5Writer.h"
#include "physics/species/Species.h"

#include <highfive/H5File.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace {

using arch::dispatch::EosId;

void expect(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

template <class Function>
void expect_rejected(Function&& function, const std::string& message,
                     std::string_view diagnostic = {})
{
    bool rejected = false;
    std::string error;
    try {
        function();
    } catch (const std::runtime_error& exception) {
        rejected = true;
        error = exception.what();
    } catch (const std::logic_error& exception) {
        rejected = true;
        error = exception.what();
    }
    expect(rejected, message);
    expect(error.find(diagnostic) != std::string::npos, message + ": " + error);
}

void write_bytes(const std::filesystem::path& path, const std::string& bytes)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!output) throw std::runtime_error("failed writing table fixture");
}

void test_sha256_padding_boundaries(const std::filesystem::path& directory)
{
    struct StandardVector
    {
        std::size_t size;
        std::string_view digest;
    };
    constexpr std::array<StandardVector, 6> vectors{{
        {0, "e3b0c44298fc1c149afbf4c8996fb924"
            "27ae41e4649b934ca495991b7852b855"},
        {55, "9f4390f8d30c2dd92ec9f095b65e2b9"
             "ae9b0a925a5258e241c9f1e910f734318"},
        {56, "b35439a4ac6f0948b6d6f9e3c6af0f5"
             "f590ce20f1bde7090ef7970686ec6738a"},
        {63, "7d3e74a05d7db15bce4ad9ec0658ea98"
             "e3f06eeecf16b4c6fff2da457ddc2f34"},
        {64, "ffe054fe7ae0cb6dc65c3af9b61d5209"
             "f439851db43d0ba5997337df154668eb"},
        {65, "635361c48bb9eab14198e76ea8ab7f1a"
             "41685d6ad62aa9146d301d4f17eb0ae0"},
    }};
    for (const auto& vector : vectors) {
        const auto path = directory
            / ("sha256-" + std::to_string(vector.size) + "-bytes.bin");
        write_bytes(path, std::string(vector.size, 'a'));
        expect(arch::core::file_sha256(path.string()) == vector.digest,
               "SHA-256 padding boundary failed at "
                   + std::to_string(vector.size) + " bytes");
        expect(arch::core::string_sha256(std::string(vector.size, 'a')) == vector.digest,
               "in-memory fingerprint does not use the file SHA-256 contract");
    }
}


SpeciesManager make_species()
{
    SpeciesManager species;
    species.add_species("c12", 12.0, 6.0, 1.4, 3.0);
    species.add_species("o16", 16.0, 8.0, 1.5, 4.0);
    return species;
}

io::CheckpointData make_checkpoint(
    const io::CheckpointProvenance& provenance)
{
    io::CheckpointData checkpoint;
    checkpoint.state_controls = arch::config::StateControlIdentity(SimConfig{});
    checkpoint.time = 0.25;
    checkpoint.dt_old = 0.01;
    checkpoint.dt_burn = 0.02;
    checkpoint.step_count = 7;
    checkpoint.chk_file_index = 3;
    checkpoint.plt_file_index = 4;
    checkpoint.dim = 1;
    checkpoint.num_species = 2;
    checkpoint.repairs.reset(2);
    checkpoint.geometry = "cartesian";
    checkpoint.cells_per_block = 2;
    checkpoint.has_timestep_state = true;
    checkpoint.resume_after_regrid = true;
    checkpoint.has_enuc_rate = true;
    checkpoint.has_mass_fractions = true;
    checkpoint.provenance = provenance;
    checkpoint.levels = {0};
    checkpoint.logical_x1 = {0};
    checkpoint.logical_x2 = {0};
    checkpoint.logical_x3 = {0};
    checkpoint.rho = {2.0, 4.0};
    checkpoint.mom_u = {1.0, 2.0};
    checkpoint.mom_v = {0.0, 0.0};
    checkpoint.mom_w = {0.0, 0.0};
    checkpoint.eng = {10.0, 20.0};
    checkpoint.enuc_rate = {-3.5, 8.25};
    checkpoint.rhoX = {0.5, 1.0, 1.5, 3.0};
    checkpoint.mass_fractions = {0.25, 0.25, 0.75, 0.75};
    return checkpoint;
}

void test_shared_gravity_identity()
{
    SimConfig config;
    config.physics.gravity.type = "self";
    const auto species = make_species();
    const auto current = io::inspect_checkpoint_provenance(
        config, species, EosId::Ideal, false, "none", false);
    constexpr double G = arch::constants::gravity::cgs::gravitational_constant;
    expect(current.gravity_controls.size() == 4 && current.gravity_controls.front() == G,
           "checkpoint identity does not record the shared CGS constant");
    expect(io::require_checkpoint_provenance_compatible(current, current),
           "matching shared gravity identity was rejected");
    for (double historical : {1e-20, 6.67408e-8, 0.,
                              std::numeric_limits<double>::quiet_NaN()}) {
        auto saved = current;
        saved.gravity_controls.front() = historical;
        expect_rejected([&] {
            io::require_checkpoint_provenance_compatible(saved, current);
        }, "different saved G was accepted", "gravity policy/boundary/controls");
        expect(current.gravity_controls.front() == G,
               "saved identity overwrote current physical constant");
    }
}

void test_identity_and_digest(const std::filesystem::path& directory)
{
    const auto table = directory / "table-a.bin";
    const auto relocated = directory / "table-relocated.bin";
    const auto changed = directory / "table-changed.bin";
    write_bytes(table, "abc");
    write_bytes(relocated, "abc");
    write_bytes(changed, "abd");
    const std::string abc_digest = arch::core::file_sha256(table.string());
    expect(abc_digest
               == "ba7816bf8f01cfea414140de5dae2223"
                  "b00361a396177a9cb410ff61f20015ad",
           "SHA-256 implementation drifted from the standard vector");
    const auto original_timestamp = std::filesystem::last_write_time(table);
    write_bytes(table, "abd");
    std::filesystem::last_write_time(table, original_timestamp);
    expect(arch::core::file_sha256(table.string()) != abc_digest,
           "same-size/same-mtime table replacement reused a stale digest");
    write_bytes(table, "abc");

    SpeciesManager species = make_species();
    SimConfig config;
    config.physics.eos_type = "TaBuLaR";
    config.physics.eos_table_path = table.string();
    config.physics.burn.use_burn = true;
    config.physics.burn.network_name = "APROX19";
    config.physics.burn.use_nse = true;
    const auto saved = io::inspect_checkpoint_provenance(
        config, species, EosId::Tabular3D, true, "APROX19", true);
    expect(saved.burn_enabled && saved.active_network == "aprox19"
               && saved.nse_enabled && saved.eos_type == "tabular3d",
           "active burn identity was not canonicalized");

    const auto four_dimensional_identity =
        io::inspect_checkpoint_provenance(
            config, species, EosId::Tabular4D, true, "aprox19", true);
    expect(four_dimensional_identity.eos_type == "tabular4d",
           "resolved 4D table identity was not canonicalized");
    expect_rejected(
        [&] {
            (void)io::require_checkpoint_provenance_compatible(
                saved, four_dimensional_identity);
        },
        "3D and 4D tabular EOS identities were treated as interchangeable");

    SimConfig relocated_config = config;
    relocated_config.physics.eos_table_path = relocated.string();
    const auto relocated_identity =
        io::inspect_checkpoint_provenance(
            relocated_config, species, EosId::Tabular3D,
            true, "aprox19", true);
    expect(io::require_checkpoint_provenance_compatible(
               saved, relocated_identity),
           "relocated byte-identical EOS table was rejected");

    SimConfig changed_config = config;
    changed_config.physics.eos_table_path = changed.string();
    const auto changed_identity =
        io::inspect_checkpoint_provenance(
            changed_config, species, EosId::Tabular3D,
            true, "aprox19", true);
    expect_rejected(
        [&] {
            (void)io::require_checkpoint_provenance_compatible(
                saved, changed_identity);
        },
        "changed EOS table content was accepted");

    auto reordered = saved;
    std::swap(reordered.species_names[0], reordered.species_names[1]);
    std::swap(reordered.species_A[0], reordered.species_A[1]);
    std::swap(reordered.species_Z[0], reordered.species_Z[1]);
    std::swap(reordered.species_gamma[0], reordered.species_gamma[1]);
    std::swap(reordered.species_Cv[0], reordered.species_Cv[1]);
    expect_rejected(
        [&] {
            (void)io::require_checkpoint_provenance_compatible(
                reordered, saved);
        },
        "same-count reordered species registry was accepted");

    auto wrong_eos = saved;
    wrong_eos.eos_type = "helmholtz";
    expect_rejected(
        [&] {
            (void)io::require_checkpoint_provenance_compatible(
                wrong_eos, saved);
        },
        "different EOS policy was accepted");
    auto wrong_network = saved;
    wrong_network.active_network = "aprox13";
    expect_rejected(
        [&] {
            (void)io::require_checkpoint_provenance_compatible(
                wrong_network, saved);
        },
        "different reaction network was accepted");

    auto burn_disabled = saved;
    burn_disabled.burn_enabled = false;
    burn_disabled.active_network = "none";
    burn_disabled.nse_enabled = false;
    expect_rejected(
        [&] {
            (void)io::require_checkpoint_provenance_compatible(
                burn_disabled, saved);
        },
        "burn enablement mismatch was accepted");

    auto nse_disabled = saved;
    nse_disabled.nse_enabled = false;
    expect_rejected(
        [&] {
            (void)io::require_checkpoint_provenance_compatible(
                nse_disabled, saved);
        },
        "NSE enablement mismatch was accepted");

    SimConfig no_burn_config = config;
    no_burn_config.physics.burn.use_burn = false;
    const auto no_burn = io::inspect_checkpoint_provenance(
        no_burn_config, species, EosId::Tabular3D,
        false, "none", false);
    expect(!no_burn.burn_enabled && no_burn.active_network == "none"
               && !no_burn.nse_enabled,
           "disabled burn did not record the inactive network identity");
    expect_rejected(
        [&] {
            (void)io::make_checkpoint_provenance(
                no_burn_config, species, EosId::Tabular3D,
                false, "aprox19", false,
                abc_digest);
        },
        "disabled burning accepted an active reaction network");
    expect_rejected(
        [&] {
            (void)io::make_checkpoint_provenance(
                no_burn_config, species, EosId::Tabular3D,
                false, "none", true,
                abc_digest);
        },
        "disabled burning accepted active NSE");

    expect_rejected(
        [&] {
            io::require_loaded_eos_table_compatible(
                saved.eos_table_sha256,
                changed_identity.eos_table_sha256);
        },
        "EOS replacement between restart verification and load was accepted");

    expect_rejected([&] { io::require_checkpoint_provenance_compatible(
               io::CheckpointProvenance{}, saved); },
           "missing checkpoint identity was presented as provenance-verified");
}

void test_hdf5_round_trip(const std::filesystem::path& directory)
{
    const auto table = directory / "round-trip-table.bin";
    const auto checkpoint_path = directory / "checkpoint-native.h5";
    write_bytes(table, "checkpoint table identity");
    SpeciesManager species = make_species();
    SimConfig config;
    config.physics.eos_type = "tabular";
    config.physics.eos_table_path = table.string();
    config.physics.burn.use_burn = true;
    config.physics.burn.network_name = "aprox19";
    config.physics.burn.use_nse = true;
    const auto expected = io::inspect_checkpoint_provenance(
        config, species, EosId::Tabular3D, true, "aprox19", true);
    const auto checkpoint = make_checkpoint(expected);
    write_bytes(table, "table replaced after the EOS owner was loaded");
    io::write_hdf5_chk_impl(checkpoint_path.string(), checkpoint);

    const auto restored = io::read_hdf5_chk_impl(checkpoint_path.string());
    expect(restored.has_enuc_rate
               && restored.enuc_rate == checkpoint.enuc_rate,
           "checkpoint did not preserve ENUC state");
    expect(restored.rhoX == checkpoint.rhoX,
           "checkpoint changed species conserved state");
    expect(restored.state_controls == checkpoint.state_controls
               && restored.state_controls.size() == arch::config::StateControlCount
               && restored.state_controls.front() == arch::config::StateControlRevision,
           "checkpoint did not preserve the current control identity");
    expect(restored.has_mass_fractions
               && restored.mass_fractions == checkpoint.mass_fractions,
           "checkpoint changed native composition");
    expect(io::require_checkpoint_provenance_compatible(
               restored.provenance, expected),
           "checkpoint lost its scientific identity");
    expect(restored.provenance.eos_table_sha256
               != arch::core::file_sha256(table.string()),
           "checkpoint output re-fingerprinted the table path instead of "
           "preserving the loaded EOS identity");
    expect(restored.provenance.burn_enabled
               && restored.provenance.active_network == "aprox19"
               && restored.provenance.nse_enabled,
           "checkpoint lost its active burn/NSE identity");

    {
        HighFive::File file(checkpoint_path.string(), HighFive::File::ReadWrite);
        file.getAttribute("burn_enabled").write(2);
    }
    expect_rejected(
        [&] { (void)io::read_hdf5_chk_impl(checkpoint_path.string()); },
        "non-Boolean burn provenance attribute was accepted");
    {
        HighFive::File file(checkpoint_path.string(), HighFive::File::ReadWrite);
        file.getAttribute("burn_enabled").write(1);
    }

    // A complete payload cannot authorize a format outside the writer's schema.
    for (const int version : {0, 1, 2, 3, io::checkpoint_format_version + 1}) {
        {
            HighFive::File file(checkpoint_path.string(), HighFive::File::ReadWrite);
            file.getAttribute("checkpoint_version").write(version);
        }
        expect_rejected(
            [&] { (void)io::read_hdf5_chk_impl(checkpoint_path.string()); },
            "reader accepted unsupported checkpoint format " + std::to_string(version));
    }

    SimConfig ideal_config;
    ideal_config.physics.eos_type = "ideal";
    ideal_config.physics.gamma = 1.4;
    ideal_config.physics.burn.network_name = "aprox19";
    const auto ideal_identity =
        io::inspect_checkpoint_provenance(
            ideal_config, species, EosId::Ideal, false, "none", false);
    expect_rejected(
        [&] {
            (void)io::make_checkpoint_provenance(
                ideal_config, species, EosId::Ideal,
                false, "none", false,
                expected.eos_table_sha256);
        },
        "ideal-gas provenance accepted a table digest");
    const auto ideal_path = directory / "checkpoint-ideal.h5";
    io::write_hdf5_chk_impl(
        ideal_path.string(), make_checkpoint(ideal_identity));
    const auto ideal = io::read_hdf5_chk_impl(ideal_path.string());
    expect(io::require_checkpoint_provenance_compatible(
               ideal.provenance, ideal_identity),
           "ideal-gas identity did not round trip");
}

void test_native_composition(const std::filesystem::path& directory)
{
    SimConfig config;
    const auto species = make_species();
    const auto identity = io::inspect_checkpoint_provenance(
        config, species, EosId::Ideal, false, "none", false);
    auto checkpoint = make_checkpoint(identity);
    // Actual BurnGradient witness: binary64 multiplication/division changes
    // the original fraction by one ULP despite preserving ordinary budgets.
    checkpoint.rho[0] = 0x1.312cfcc31ba75p+23;
    checkpoint.mass_fractions[0] = 0x1.ffffffffdd789p-2;
    checkpoint.mass_fractions[2] = 1.0 - checkpoint.mass_fractions[0];
    for (size_t entry = 0; entry < checkpoint.rhoX.size(); ++entry)
        checkpoint.rhoX[entry] = checkpoint.rho[entry % checkpoint.rho.size()]
                              * checkpoint.mass_fractions[entry];
    expect(checkpoint.rhoX[0] / checkpoint.rho[0] != checkpoint.mass_fractions[0],
           "native-composition fixture does not expose lossy rhoX reconstruction");

    const auto path = directory / "native-composition.h5";
    io::write_hdf5_chk_impl(path.string(), checkpoint);
    const auto restored = io::read_hdf5_chk_impl(path.string());
    expect(restored.has_mass_fractions && restored.mass_fractions == checkpoint.mass_fractions,
           "native mass fractions did not round trip exactly");
    expect(restored.rhoX == checkpoint.rhoX, "conserved composition changed");
    {
        HighFive::File file(path.string(), HighFive::File::ReadOnly);
        int version = 0;
        file.getAttribute("checkpoint_version").read(version);
        expect(version == io::checkpoint_format_version,
               "native composition lacks the writer's format version");
    }
    auto bad = checkpoint;
    bad.has_mass_fractions = false;
    bad.mass_fractions.clear();
    expect_rejected([&] { io::write_hdf5_chk_impl(path.string(), bad); },
                    "writer accepted missing native composition");
    bad = checkpoint;
    bad.mass_fractions.pop_back();
    expect_rejected([&] { io::write_hdf5_chk_impl(path.string(), bad); },
                    "writer accepted malformed native composition dimensions");
    for (const double corrupted : {0.0, std::numeric_limits<double>::quiet_NaN()}) {
        auto fractions = checkpoint.mass_fractions;
        fractions[0] = corrupted;
        {
            HighFive::File file(path.string(), HighFive::File::ReadWrite);
            file.getDataSet("Data/X").write_raw(fractions.data());
        }
        expect_rejected([&] { (void)io::read_hdf5_chk_impl(path.string()); },
                        "reader accepted inconsistent or nonfinite native composition");
    }
    {
        HighFive::File file(path.string(), HighFive::File::ReadWrite);
        file.unlink("Data/X");
    }
    expect_rejected([&] { (void)io::read_hdf5_chk_impl(path.string()); },
                    "checkpoint reader accepted a missing native-composition dataset");
    {
        HighFive::File file(path.string(), HighFive::File::ReadWrite);
        file.getAttribute("checkpoint_version").write(3);
    }
    expect_rejected([&] { (void)io::read_hdf5_chk_impl(path.string()); },
                    "reader accepted an unsupported conserved-only composition payload");
}

void test_host_restart(const std::filesystem::path& directory)
{
    SimConfig config;
    config.grid.dim = 1;
    config.grid.nblockx1 = 1;
    config.grid.nblockx2 = 0;
    config.grid.nblockx3 = 0;
    config.grid.amr_max_blocks = 4;
    config.amr.lrefinemax = 1;
    const auto species = make_species();
    const auto identity = io::inspect_checkpoint_provenance(
        config, species, EosId::Ideal, false, "none", false);
    auto checkpoint = make_checkpoint(identity);
    checkpoint.cells_per_block = amr::BLOCK_NX;
    checkpoint.levels = {1, 1};
    checkpoint.logical_x1 = {0, 1};
    checkpoint.logical_x2 = {0, 0};
    checkpoint.logical_x3 = {0, 0};
    const size_t count = checkpoint.levels.size() * checkpoint.cells_per_block;
    checkpoint.rho.resize(count);
    checkpoint.mom_u.resize(count);
    checkpoint.mom_v.resize(count);
    checkpoint.mom_w.resize(count);
    checkpoint.eng.resize(count);
    checkpoint.enuc_rate.resize(count);
    checkpoint.mass_fractions.resize(2 * count);
    checkpoint.rhoX.resize(2 * count);
    for (size_t cell = 0; cell < count; ++cell) {
        checkpoint.rho[cell] = 2.0 + cell;
        checkpoint.mom_u[cell] = 0.25 + cell;
        checkpoint.mom_v[cell] = -0.5 * cell;
        checkpoint.mom_w[cell] = 0.75 * cell;
        checkpoint.eng[cell] = 100.0 + cell;
        checkpoint.enuc_rate[cell] = -0.5 + cell;
        checkpoint.mass_fractions[cell] = 0.25;
        checkpoint.mass_fractions[count + cell] = 0.75;
    }
    // Carry the non-invertible rhoX witness through the actual host restore.
    checkpoint.rho[0] = 0x1.312cfcc31ba75p+23;
    checkpoint.mass_fractions[0] = 0x1.ffffffffdd789p-2;
    checkpoint.mass_fractions[count] = 1.0 - checkpoint.mass_fractions[0];
    for (size_t entry = 0; entry < checkpoint.rhoX.size(); ++entry)
        checkpoint.rhoX[entry] = checkpoint.rho[entry % count]
                              * checkpoint.mass_fractions[entry];
    checkpoint.state_controls = arch::config::StateControlIdentity(config);
    checkpoint.repairs.reset(species.count());
    const auto path = directory / "host-restart.h5";
    io::write_hdf5_chk_impl(path.string(), checkpoint);

    // A control change must fail before replacing the live AMR state.
    for(int control=0;control<4;++control) {
        auto changed=config;
        if(control==0) changed.numerics.dt_max=1.;
        if(control==1) changed.physics.eos_coulomb_mult=.5;
        if(control==2) changed.numerics.hll_roe_wave_speed=false;
        if(control==3) {changed.amr.refine_on_jeans=true;changed.amr.jeans_cells=160.;}
        amr::AMRControl untouched(config.grid.amr_max_blocks,config.grid.dim);
        RunState unchanged;
        expect_rejected([&] { read_chk(path.string(),untouched,unchanged,changed,species,identity); },
                        "restart accepted a changed Coulomb/face/timestep control");
    }

    // Unused target/output selection must not alter active trajectory identity.
    auto output_only=config;output_only.io.vars.jens=true;output_only.amr.jeans_cells=160.;
    expect(arch::config::StateControlIdentity(output_only)==checkpoint.state_controls,
           "output-only or unused target changed restart identity");
    auto active_config=config;active_config.amr.refine_on_jeans=true;
    active_config.amr.jeans_cells=160.;
    auto active_payload=checkpoint;
    active_payload.state_controls=arch::config::StateControlIdentity(active_config);
    const auto active_path=directory/"jeans-restart.h5";
    io::write_hdf5_chk_impl(active_path.string(),active_payload);
    amr::AMRControl active_tree(config.grid.amr_max_blocks,config.grid.dim);
    RunState active_state;
    read_chk(active_path.string(),active_tree,active_state,active_config,species,identity);
    expect(active_tree.tree->GetActiveBlocks().size()==checkpoint.levels.size(),
           "active JENS identity roundtrip failed");
    active_config.amr.jeans_cells=161.;
    expect_rejected([&] {read_chk(active_path.string(),active_tree,active_state,
                                 active_config,species,identity);},
                    "restart accepted a changed Jeans resolution target");

    amr::AMRControl restored(config.grid.amr_max_blocks, config.grid.dim);
    RunState state;
    read_chk(path.string(), restored, state, config, species, identity);
    const auto& leaves = restored.tree->GetActiveBlocks();
    expect(leaves.size() == checkpoint.levels.size(), "host restore changed leaf count");
    for (size_t leaf = 0; leaf < leaves.size(); ++leaf) {
        const auto& block = restored.pool->GetBlock(leaves[leaf]);
        expect(block.level == checkpoint.levels[leaf]
                   && block.logical_x1 == checkpoint.logical_x1[leaf]
                   && block.logical_x2 == checkpoint.logical_x2[leaf]
                   && block.logical_x3 == checkpoint.logical_x3[leaf],
               "host restore changed leaf topology");
        size_t local = 0;
        for (int i = block.grid.Is(); i < block.grid.Ie(); ++i, ++local) {
            const int cell = block.grid.GetIndex(i, block.grid.Js(), block.grid.Ks());
            const size_t offset = leaf * checkpoint.cells_per_block + local;
            const auto& fluid = block.fluid_state;
            expect(fluid.rho[cell] == checkpoint.rho[offset]
                       && fluid.mom_u[cell] == checkpoint.mom_u[offset]
                       && fluid.mom_v[cell] == checkpoint.mom_v[offset]
                       && fluid.mom_w[cell] == checkpoint.mom_w[offset]
                       && fluid.eng[cell] == checkpoint.eng[offset]
                       && fluid.enuc_rate[cell] == checkpoint.enuc_rate[offset],
                   "host restore changed a fluid or ENUC field");
            for (int spec = 0; spec < species.count(); ++spec)
                expect(fluid.X(spec, cell) == checkpoint.mass_fractions[spec * count + offset],
                       "host restore changed native mass fractions");
        }
        expect(local == checkpoint.cells_per_block, "host restore used the wrong block extent");
    }
    expect(state.time == checkpoint.time && state.step == checkpoint.step_count
               && state.dt_old == checkpoint.dt_old && state.dt_burn == checkpoint.dt_burn
               && state.chk_idx == checkpoint.chk_file_index
               && state.plt_idx == checkpoint.plt_file_index
               && state.has_timestep_state && state.resume_after_regrid
               && state.verified_eos_table_sha256 == identity.eos_table_sha256,
           "host restore changed controller, output phase, or verified table identity");

    // Schema/identity rejection must precede the topology-replacement boundary.
    // Snapshot all live fields, not only leaf IDs which a pool can recycle.
    amr::AMRControl protected_tree(config.grid.amr_max_blocks, config.grid.dim);
    protected_tree.tree->InitRootGrid(config, species.count());
    auto& original = protected_tree.pool->GetBlock(protected_tree.tree->GetActiveBlocks().front());
    std::fill(original.fluid_state.rho.begin(), original.fluid_state.rho.end(), 17.0);
    RunState protected_state = state;
    const auto snapshot = [&] {
        const auto& ids = protected_tree.tree->GetActiveBlocks();
        expect(!ids.empty(), "rejected restart cleared live topology");
        const auto& block = protected_tree.pool->GetBlock(ids.front());
        const auto& fluid = block.fluid_state;
        return std::make_tuple(ids, protected_tree.pool->GetNumActiveBlocks(),
            block.level, block.logical_x1, block.logical_x2, block.logical_x3,
            fluid.rho, fluid.mom_u, fluid.mom_v, fluid.mom_w, fluid.eng,
            fluid.enuc_rate, fluid.mass_fractions,
            protected_state.time, protected_state.step, protected_state.dt_old,
            protected_state.dt_burn, protected_state.chk_idx, protected_state.plt_idx,
            protected_state.has_timestep_state, protected_state.resume_after_regrid,
            protected_state.verified_eos_table_sha256);
    };
    const auto before = snapshot();
    const auto reject_unchanged = [&](const io::CheckpointProvenance& expected,
                                      const std::string& reason,
                                      std::string_view diagnostic = {}) {
        expect_rejected([&] { read_chk(path.string(), protected_tree, protected_state,
                                      config, species, expected); }, reason, diagnostic);
        expect(snapshot() == before, "rejected restart modified live state: " + reason);
    };
    // Actual HDF/read_chk path: missing/partial/future/RZ chart must reject
    // before replacing this populated live hierarchy or controller.
    for (int corruption = 0; corruption < 5; ++corruption) {
        io::write_hdf5_chk_impl(path.string(), checkpoint);
        {
            HighFive::File file(path.string(), HighFive::File::ReadWrite);
            if (corruption == 0) file.deleteAttribute("geometry_chart");
            if (corruption == 1) file.getAttribute("geometry_semantics_revision").write(2);
            if (corruption == 2) file.getAttribute("geometry_chart").write(std::string("axisymmetric-rz"));
            if (corruption >= 3) {
                file.deleteAttribute("geometry_chart");
                file.deleteAttribute("geometry_semantics_revision");
            }
        }
        if (corruption == 3) {
            // Legacy Cartesian v6 remains readable by the existing chart.
            const auto legacy = io::read_hdf5_chk_impl(path.string());
            io::require_checkpoint_geometry_compatible(legacy.dim, legacy.geometry, legacy.geometry_identity);
        } else if (corruption == 4) {
            expect_rejected([&] { read_chk(path.string(), protected_tree, protected_state,
                config, species, identity, io::current_rz_checkpoint_geometry()); },
                "legacy checkpoint accepted as RZ");
        } else {
            reject_unchanged(identity, "invalid geometry identity");
        }
        expect(snapshot() == before, "geometry rejection modified live state");
    }
    // HDF roundtrip of explicit internal RZ identity; not public RZ restart.
    auto rz_payload = checkpoint;
    rz_payload.dim = 2;
    rz_payload.geometry = "cylindrical";
    rz_payload.geometry_identity = io::current_rz_checkpoint_geometry();
    rz_payload.repairs.bind_semantics(arch::state::RepairSemantics::RzVolumeAngular);
    rz_payload.native_domain={{0.,1.,0.,1.},{1,1},{static_cast<int>(rz_payload.cells_per_block),1}};
    const auto rz_path = directory / "internal-rz-identity.h5";
    io::write_hdf5_chk_impl(rz_path.string(), rz_payload);
    const auto rz = io::read_hdf5_chk_impl(rz_path.string());
    io::require_checkpoint_geometry_compatible(2, "cylindrical", rz.geometry_identity,
                                               io::current_rz_checkpoint_geometry());
    expect_rejected([&] { io::require_checkpoint_geometry_compatible(
        2, "cylindrical", rz.geometry_identity); }, "RZ accepted as legacy polar");
    expect_rejected([&] { io::require_checkpoint_geometry_compatible(
        2, "cylindrical", {}, io::current_rz_checkpoint_geometry()); }, "legacy polar accepted as RZ");
    // Real old 2D cylindrical HDF + populated live tree: never interpreted as RZ.
    {
        HighFive::File file(rz_path.string(), HighFive::File::ReadWrite);
        file.deleteAttribute("geometry_semantics_revision");
        file.deleteAttribute("geometry_chart");
        file.deleteAttribute("repair_semantics");
    }
    auto polar_config = config;
    polar_config.grid.dim = 2;
    polar_config.grid.geometry = "cylindrical";
    polar_config.grid.nblockx2 = 1;
    amr::AMRControl polar_live(polar_config.grid.amr_max_blocks, 2);
    polar_live.tree->InitRootGrid(polar_config, species.count());
    const auto polar_ids = polar_live.tree->GetActiveBlocks();
    auto& polar_fluid = polar_live.pool->GetBlock(polar_ids.front()).fluid_state;
    std::fill(polar_fluid.rho.begin(), polar_fluid.rho.end(), 19.0);
    const auto polar_rho = polar_fluid.rho;
    RunState polar_state = state;
    expect_rejected([&] { read_chk(rz_path.string(), polar_live, polar_state,
        polar_config, species, identity, io::current_rz_checkpoint_geometry()); },
        "actual legacy polar file accepted as RZ", "no authoritative RZ");
    expect(polar_live.tree->GetActiveBlocks() == polar_ids && polar_fluid.rho == polar_rho
        && polar_state.time == state.time && polar_state.step == state.step
        && polar_state.chk_idx == state.chk_idx && polar_state.plt_idx == state.plt_idx,
        "legacy polar rejection changed live state");
    auto invalid_geometry = checkpoint;
    invalid_geometry.geometry_identity = {2, "existing"};
    io::write_hdf5_chk_impl(path.string(), checkpoint);
    const auto geometry_digest = arch::core::file_sha256(path.string());
    expect_rejected([&] { io::write_hdf5_chk_impl(path.string(), invalid_geometry); },
                    "future geometry revision accepted");
    expect(arch::core::file_sha256(path.string()) == geometry_digest,
           "invalid geometry writer truncated previous checkpoint");

    // Format v6 predates controls revision 2. Exercise an actual v6/15-value
    // payload instead of merely changing the currently active configuration.
    auto legacy_controls = checkpoint.state_controls;
    legacy_controls.resize(15);
    legacy_controls[0] = 1.0;
    auto prior_controls=checkpoint.state_controls;
    prior_controls.resize(18);prior_controls[0]=2.0;
    auto future_controls = checkpoint.state_controls;
    future_controls[0] = arch::config::StateControlRevision + 1.0;
    auto short_controls = checkpoint.state_controls;
    short_controls.pop_back();
    auto nonfinite_controls = checkpoint.state_controls;
    nonfinite_controls[1] = std::numeric_limits<double>::quiet_NaN();
    for (const auto& [controls, diagnostic] : std::vector<std::pair<std::vector<double>, std::string>>{
             {legacy_controls, "Unsupported checkpoint state-control revision"},
             {prior_controls, "Unsupported checkpoint state-control revision"},
             {future_controls, "Unsupported checkpoint state-control revision"},
             {short_controls, "Invalid checkpoint state-control length"},
             {nonfinite_controls, "missing or nonfinite values"},
             {{}, "missing or nonfinite values"}}) {
        io::write_hdf5_chk_impl(path.string(), checkpoint);
        const auto original_digest = arch::core::file_sha256(path.string());
        auto rejected_payload = checkpoint;
        rejected_payload.state_controls = controls;
        expect_rejected([&] { io::write_hdf5_chk_impl(path.string(), rejected_payload); },
                        "writer accepted invalid controls", diagnostic);
        expect(arch::core::file_sha256(path.string()) == original_digest,
               "rejected writer truncated the previous checkpoint");
        {
            HighFive::File file(path.string(), HighFive::File::ReadWrite);
            file.unlink("state_controls");
            file.createDataSet("state_controls", controls);
        }
        reject_unchanged(identity, "invalid or incompatible checkpoint controls", diagnostic);
    }
    // Damage the repair ledger independently of the now-valid control identity.
    for (int corruption = 0; corruption < 3; ++corruption) {
        io::write_hdf5_chk_impl(path.string(), checkpoint);
        auto ledger = checkpoint.repairs.values;
        if (corruption == 0) ledger[0] = -1.0;
        if (corruption == 1) ledger[0] = std::numeric_limits<double>::quiet_NaN();
        if (corruption == 2) ledger.pop_back();
        auto rejected_payload = checkpoint;
        rejected_payload.repairs.values = ledger;
        const auto original_digest = arch::core::file_sha256(path.string());
        expect_rejected([&] { io::write_hdf5_chk_impl(path.string(), rejected_payload); },
                        "writer accepted corrupt repair accounting", "repair ledger is invalid");
        expect(arch::core::file_sha256(path.string()) == original_digest,
               "invalid repair ledger truncated the previous checkpoint");
        {
            HighFive::File file(path.string(), HighFive::File::ReadWrite);
            file.unlink("state_repairs");
            file.createDataSet("state_repairs", ledger);
        }
        reject_unchanged(identity, "corrupt repair accounting", "repair ledger is invalid");
    }
    io::write_hdf5_chk_impl(path.string(), checkpoint);
    for (const int version : {0, 1, 2, 3, io::checkpoint_format_version + 1}) {
        {
            HighFive::File file(path.string(), HighFive::File::ReadWrite);
            file.getAttribute("checkpoint_version").write(version);
        }
        reject_unchanged(identity, "unsupported host checkpoint version " + std::to_string(version));
    }
    for (const char* attribute : {"dt_old", "dt_burn", "resume_after_regrid", "eos_type"}) {
        io::write_hdf5_chk_impl(path.string(), checkpoint);
        {
            HighFive::File file(path.string(), HighFive::File::ReadWrite);
            file.deleteAttribute(attribute);
        }
        reject_unchanged(identity, std::string("missing required attribute ") + attribute);
    }
    for (const char* dataset : {"Data/enuc_rate", "Data/X", "Species/name", "state_controls", "state_repairs"}) {
        io::write_hdf5_chk_impl(path.string(), checkpoint);
        {
            HighFive::File file(path.string(), HighFive::File::ReadWrite);
            file.unlink(dataset);
        }
        reject_unchanged(identity, std::string("missing required dataset ") + dataset);
    }
    io::write_hdf5_chk_impl(path.string(), checkpoint);
    reject_unchanged(io::CheckpointProvenance{}, "missing expected scientific identity");
    auto wrong_identity = identity;
    wrong_identity.ideal_gamma += 0.1;
    reject_unchanged(wrong_identity, "incompatible EOS identity");
    wrong_identity = identity;
    std::swap(wrong_identity.species_names[0], wrong_identity.species_names[1]);
    reject_unchanged(wrong_identity, "incompatible ordered species identity");
    {
        HighFive::File file(path.string(), HighFive::File::ReadWrite);
        file.getAttribute("dim").write(2);
    }
    reject_unchanged(identity, "incompatible spatial dimension");
}

void test_native_rz_checkpoint(const std::filesystem::path& directory)
{
    SimConfig config;
    config.grid.dim = 2;
    config.grid.geometry = "cylindrical";
    config.grid.nblockx1 = 2;
    config.grid.nblockx2 = 1;
    config.grid.nblockx3 = 0;
    config.grid.x2_min = -10.;
    config.grid.x2_max = 10.;
    config.grid.amr_max_blocks = 16;
    config.amr.lrefinemax = 1;
    config.io.out_dir = (directory / "native-rz").string();
    config.io.base_name = "rz";
    const auto species = make_species();
    const auto provenance = io::inspect_checkpoint_provenance(
        config, species, EosId::Ideal, false, "none", false);
    amr::AMRControl source(config.grid.amr_max_blocks, 2);
    source.tree->LoadLeafGrid(config, species.count(), {1,1,1,1,0},
        {0,1,0,1,1}, {0,0,1,1,0}, {0,0,0,0,0},
        GridMetrics::GeometrySemantics::AxisymmetricRz);
    for (int id : source.tree->GetActiveBlocks()) {
    auto& block = source.pool->GetBlock(id);
    auto& fluid = block.fluid_state;
    const auto& grid = block.grid;
    for (int j=grid.Js(); j<grid.Je(); ++j)
        for (int i=grid.Is(); i<grid.Ie(); ++i) {
            const int c=grid.GetIndex(i,j,grid.Ks());
            fluid.rho[c]=2.;
            fluid.mom_u[c]=.1;
            fluid.mom_v[c]=.2;
            fluid.mom_w[c]=.3;
            fluid.eng[c]=100.;
            fluid.enuc_rate[c]=-.5;
            fluid.X(0,c)=.25;
            fluid.X(1,c)=.75;
        }
    }
    arch::state::RepairBudget repairs;
    repairs.reset(species.count(),arch::state::RepairSemantics::RzVolumeAngular);
    expect(repairs.view().conserved_density(.5,1.,-2.,3.,4.,2.,5.),"RZ repair density recording failed");
    repairs.view().event(2.,7);
    write_chk(source, 3, 4, 7, .25, .01, .02, true,
              config, species, provenance, repairs, io::current_rz_checkpoint_geometry());
    const auto file = directory / "native-rz/rz_chk_0003.h5";
    const auto payload=io::read_hdf5_chk_impl(file.string());
    expect(payload.geometry_identity.revision==io::rz_checkpoint_revision &&
           payload.geometry_identity.chart=="axisymmetric-rz",
           "native writer lost explicit RZ identity");
    amr::AMRControl restored(config.grid.amr_max_blocks,2);
    RunState state;
    read_chk(file.string(), restored, state, config, species, provenance,
             io::current_rz_checkpoint_geometry());
    expect(payload.repairs.semantics==arch::state::RepairSemantics::RzVolumeAngular
        && state.repairs.semantics==payload.repairs.semantics
        && state.repairs.values==repairs.values && state.repairs.values[6]==15.,
        "RZ checkpoint did not preserve nonzero J repair ledger identity");
    expect(restored.tree->GetActiveBlocks().size()==5,
           "mixed RZ checkpoint changed leaf count");
    for (int id : restored.tree->GetActiveBlocks()) {
    const auto& rb=restored.pool->GetBlock(id);
    const double lower=-10.+rb.logical_x2*20./(1<<rb.level);
    expect(rb.grid.x2_min == lower && rb.grid.x2_max == lower+20./(1<<rb.level),
           "mixed RZ native leaf restore lost physical z domain");
    for (int j=rb.grid.Js(); j<rb.grid.Je(); ++j)
        for (int i=rb.grid.Is(); i<rb.grid.Ie(); ++i) {
            const int c=rb.grid.GetIndex(i,j,rb.grid.Ks());
            expect(rb.fluid_state.rho[c]==2. && rb.fluid_state.mom_u[c]==.1 &&
                   rb.fluid_state.mom_v[c]==.2 && rb.fluid_state.mom_w[c]==.3 &&
                   rb.fluid_state.eng[c]==100. && rb.fluid_state.enuc_rate[c]==-.5 &&
                   rb.fluid_state.X(0,c)==.25 && rb.fluid_state.X(1,c)==.75,
                   "native RZ checkpoint restore changed original FP64 state");
        }
    }
    expect(state.time==.25 && state.step==7 && state.chk_idx==3 &&
           state.plt_idx==4 && state.dt_old==.01 && state.dt_burn==.02 &&
           state.resume_after_regrid, "native RZ controller changed");
    const auto native_angular_identity=[](const amr::AMRControl& control) {
        std::vector<double> weights;long double angular=0.;
        for(int id:control.tree->GetActiveBlocks()) {
            const auto& b=control.pool->GetBlock(id);const auto& g=b.grid;
            for(int j=g.Js();j<g.Je();++j)for(int i=g.Is();i<g.Ie();++i) {
                const double w=GridMetrics::Rz::AngularMomentumMeasure(g.GetFacePosL(i),g.GetFacePosR(i),g.dx2);
                weights.push_back(w);
                angular+=static_cast<long double>(b.fluid_state.mom_w[g.GetIndex(i,j,g.Ks())])*w;
            }
        }
        return std::make_pair(weights,angular);
    };
    const auto [source_weights,source_angular]=native_angular_identity(source);
    const auto [restored_weights,restored_angular]=native_angular_identity(restored);
    expect(source_weights==restored_weights && source_angular==restored_angular,
        "native RZ checkpoint reinterpreted W or J");
    std::cout<<std::setprecision(21)<<"RZ_CHECKPOINT_NATIVE_W_J_PASS cells="<<source_weights.size()
        <<" source_J="<<source_angular<<" restored_J="<<restored_angular<<"\n";

    const auto digest=arch::core::file_sha256(file.string());
    auto wrong_repair_profile=payload;
    wrong_repair_profile.repairs.semantics=arch::state::RepairSemantics::ExistingVolume;
    expect_rejected([&]{io::write_hdf5_chk_impl(file.string(),wrong_repair_profile);},
        "RZ writer accepted ordinary volume repair identity");
    expect(arch::core::file_sha256(file.string())==digest,
        "wrong repair identity writer truncated previous checkpoint");
    const auto snapshot_restored=[&] {
        std::vector<double> words;
        for(int id:restored.tree->GetActiveBlocks()) {
            const auto& f=restored.pool->GetBlock(id).fluid_state;
            for(const auto* v:{&f.rho,&f.mom_u,&f.mom_v,&f.mom_w,&f.eng,&f.enuc_rate,&f.mass_fractions})
                words.insert(words.end(),v->begin(),v->end());
        }
        return words;
    };
    const auto before=snapshot_restored();
    // Preserve earlier local evidence when this scoped fixture is rerun.
    const auto fresh_corruption_copy=[&](const std::string& prefix,int test) {
        auto bad=directory/(prefix+"-"+std::to_string(test)+".h5");
        for(int suffix=1;std::filesystem::exists(bad);++suffix)
            bad=directory/(prefix+"-"+std::to_string(test)+"-"+std::to_string(suffix)+".h5");
        std::filesystem::copy_file(file,bad);return bad;
    };
    for(int corruption=0;corruption<7;++corruption) {
        const auto bad=fresh_corruption_copy("rz-state-corruption",corruption);
        {
            HighFive::File hdf(bad.string(),HighFive::File::ReadWrite);
            if(corruption==0)hdf.getAttribute("geometry_semantics_revision").write(1);
            if(corruption==1)hdf.deleteAttribute("state_semantics");
            if(corruption==2)hdf.getAttribute("state_semantics").write(std::string("volume-average-momentum-phi-v1"));
            if(corruption==3)hdf.deleteAttribute("repair_semantics");
            if(corruption==4)hdf.getAttribute("repair_semantics").write(std::string("existing-volume-v1"));
            if(corruption==5)hdf.getAttribute("repair_semantics").write(std::string("rz-native-V-angular-J-v2"));
            if(corruption==6) {
                hdf.deleteAttribute("repair_semantics");
                hdf.getDataSet("state_repairs").write(std::vector<double>(repairs.values.size(),0.));
            }
        }
        expect_rejected([&]{read_chk(bad.string(),restored,state,config,species,provenance,
            io::current_rz_checkpoint_geometry());},"RZ state identity corruption accepted");
        expect(snapshot_restored()==before && state.time==.25 && state.step==7 &&
            state.chk_idx==3 && state.plt_idx==4,"RZ state rejection mutated live state");
        expect(arch::core::file_sha256(file.string())==digest,"RZ rejection modified original checkpoint");
    }
    // Active config must not reinterpret native W or the flattening geometry.
    for(int change=0;change<6;++change) {
        auto different=config;
        if(change==0)different.grid.x1_min+=.125;
        if(change==1)different.grid.x1_max+=.25;
        if(change==2)different.grid.x2_min-=1.;
        if(change==3)different.grid.x2_max+=1.;
        if(change==4)different.grid.nblockx1+=1;
        if(change==5)different.grid.nblockx2+=1;
        expect_rejected([&]{read_chk(file.string(),restored,state,different,species,provenance,
            io::current_rz_checkpoint_geometry());},"wrong native domain accepted","native domain/measure");
        expect(snapshot_restored()==before && state.time==.25 && state.step==7,
            "wrong-domain restore mutated live state");
        expect_rejected([&]{write_chk(source,3,4,7,.25,.01,.02,true,
            different,species,provenance,repairs,io::current_rz_checkpoint_geometry());},
            "writer mislabeled actual tree with changed config","native domain/measure");
        expect(arch::core::file_sha256(file.string())==digest,
            "wrong-domain writer replaced previous checkpoint");
    }
    for(int corruption=0;corruption<7;++corruption) {
        const auto bad=fresh_corruption_copy("rz-domain-corruption",corruption);
        {
            HighFive::File hdf(bad.string(),HighFive::File::ReadWrite);
            if(corruption==0)hdf.unlink("NativeDomain");
            else {
                auto domain=hdf.getGroup("NativeDomain");
                if(corruption==1) {domain.unlink("bounds");domain.createDataSet("bounds",std::vector<double>{0.,1.,-10.});}
                if(corruption==2) {
                    auto bounds=payload.native_domain.bounds;bounds[0]=std::numeric_limits<double>::quiet_NaN();
                    domain.getDataSet("bounds").write(bounds);
                }
                if(corruption==3)domain.getAttribute("coordinate_unit").write(std::string("code_length"));
                if(corruption==4)domain.getAttribute("measure_normalization").write(std::string("per_radian"));
                if(corruption==5)domain.getAttribute("version").write(2);
                if(corruption==6)domain.getDataSet("cell_shape").write(
                    std::vector<int>{amr::BLOCK_NX*2,amr::BLOCK_NY/2});
            }
        }
        expect_rejected([&]{read_chk(bad.string(),restored,state,config,species,provenance,
            io::current_rz_checkpoint_geometry());},"corrupt native domain accepted");
        expect(snapshot_restored()==before && state.time==.25 && state.step==7 &&
            state.chk_idx==3 && state.plt_idx==4,"domain rejection mutated live state");
        expect(arch::core::file_sha256(file.string())==digest,"domain rejection changed source checkpoint");
    }
    std::cout<<"RZ_NATIVE_DOMAIN_IDENTITY_PASS changed-config=6 corrupted-hdf=7\n";
    expect_rejected([&] { write_chk(source,3,4,7,.25,.01,.02,true,
        config,species,provenance,repairs,{3,"axisymmetric-rz"}); },
        "native writer accepted future chart revision");
    expect(arch::core::file_sha256(file.string())==digest,
           "rejected native writer changed previous output");
    auto invalid=config;
    invalid.grid.geometry="cartesian";
    invalid.io.out_dir=(directory/"invalid-rz").string();
    expect_rejected([&] { write_chk(source,3,4,7,.25,.01,.02,true,
        invalid,species,provenance,repairs,io::current_rz_checkpoint_geometry()); },
        "native writer accepted Cartesian RZ chart");
    expect(!std::filesystem::exists(invalid.io.out_dir),
           "invalid geometry created output directory");
}

} // namespace

int main(int argc, char** argv)
{
    try {
        const FluidVector output_state{1.,0.,0.,0.,1.};
        const auto good=+[](const FluidVector&,const double*,const void*) { return 1.; };
        const auto invalid=+[](const FluidVector&,const double*,const void*) {
            return std::numeric_limits<double>::quiet_NaN();
        };
        io::require_output_thermodynamics(output_state,nullptr,good,good,good,nullptr);
        for (int field=0;field<3;++field) {
            expect_rejected([&]{io::require_output_thermodynamics(output_state,nullptr,
                field==0 ? invalid : good,field==1 ? invalid : good,field==2 ? invalid : good,nullptr);},
                "nonfinite EOS query must prevent persistence");
        }
        if (argc != 2)
            throw std::runtime_error("checkpoint test requires an output directory");
        const std::filesystem::path directory = argv[1];
        std::filesystem::create_directories(directory);
        test_sha256_padding_boundaries(directory);
        test_shared_gravity_identity();
        test_identity_and_digest(directory);
        test_hdf5_round_trip(directory);
        test_native_composition(directory);
        test_host_restart(directory);
        // Exact synthetic units distinguish V=2 from W=5 without invoking floors.
        arch::state::RepairBudget volume(0),angular(0,arch::state::RepairSemantics::RzVolumeAngular);
        expect(volume.view().conserved_density(.5,1.,-2.,3.,4.,2.),"volume ledger failed");
        expect(angular.view().conserved_density(.5,1.,-2.,3.,4.,2.,5.),"angular ledger failed");
        expect(volume.values[2]==1. && angular.values[2]==1.
            &&volume.values[6]==6. &&angular.values[6]==15.
            &&angular.values[4]==2. &&angular.values[5]==-4. &&angular.values[7]==8.,
            "repair ledger confused V-integrated momentum and W-integrated J");
        const auto before_angular=angular.values;
        expect_rejected([&]{angular.combine(volume);},"mixed measure ledger merge accepted");
        expect(angular.values==before_angular,"rejected merge changed ledger");
        expect_rejected([&]{volume.bind_semantics(arch::state::RepairSemantics::RzVolumeAngular);},
            "nonzero historical volume ledger reinterpreted as J");
        expect(!angular.view().conserved_density(0.,0.,0.,3.,0.,2.,0.)
            &&!angular.view().conserved_density(0.,0.,0.,3.,0.,2.,std::numeric_limits<double>::infinity())
            &&!angular.view().conserved_density(0.,0.,0.,std::numeric_limits<double>::max(),0.,2.,5.)
            &&angular.values==before_angular,"invalid W/product altered repair ledger");
        arch::state::RepairBudget second=angular;
        angular.combine(second,.5);
        expect(angular.values[6]==22.5,"RK weighted J ledger changed units");
        std::cout<<"REPAIR_V_W_IDENTITY_PASS\n";
        test_native_rz_checkpoint(directory);
        std::cout << "checkpoint compatibility tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
