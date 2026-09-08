/**
 * @file test_checkpoint_compatibility.cpp
 * @brief Check checkpoint identities and lossless state restoration.
 *
 * Exercise file fingerprints, physical-configuration matching, HDF5 round trips
 * and native mass fractions, including explicitly rejected incompatibilities.
 */
#include "amr/AMRControl.h"
#include "core/RuntimeParams.h"
#include "core/FileFingerprint.h"
#include "io/IO.h"
#include "io/chk/CheckpointCompatibility.h"
#include "io/hdf5/HDF5Writer.h"
#include "physics/species/Species.h"

#include <highfive/H5File.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace {

using arch::dispatch::EosId;

void expect(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

template <class Function>
void expect_rejected(Function&& function, const std::string& message)
{
    bool rejected = false;
    try {
        function();
    } catch (const std::runtime_error&) {
        rejected = true;
    } catch (const std::logic_error&) {
        rejected = true;
    }
    expect(rejected, message);
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
    checkpoint.time = 0.25;
    checkpoint.dt_old = 0.01;
    checkpoint.dt_burn = 0.02;
    checkpoint.step_count = 7;
    checkpoint.chk_file_index = 3;
    checkpoint.plt_file_index = 4;
    checkpoint.dim = 1;
    checkpoint.num_species = 2;
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

    expect(!io::require_checkpoint_provenance_compatible(
               io::CheckpointProvenance{}, saved),
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
    const auto path = directory / "host-restart.h5";
    io::write_hdf5_chk_impl(path.string(), checkpoint);

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
                                      const std::string& reason) {
        expect_rejected([&] { read_chk(path.string(), protected_tree, protected_state,
                                      config, species, expected); }, reason);
        expect(snapshot() == before, "rejected restart modified live state: " + reason);
    };
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
    for (const char* dataset : {"Data/enuc_rate", "Data/X", "Species/name"}) {
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

} // namespace

int main(int argc, char** argv)
{
    try {
        if (argc != 2)
            throw std::runtime_error("checkpoint test requires an output directory");
        const std::filesystem::path directory = argv[1];
        std::filesystem::create_directories(directory);
        test_sha256_padding_boundaries(directory);
        test_identity_and_digest(directory);
        test_hdf5_round_trip(directory);
        test_native_composition(directory);
        test_host_restart(directory);
        std::cout << "checkpoint compatibility tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
