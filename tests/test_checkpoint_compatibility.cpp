#include "core/RuntimeParams.h"
#include "core/FileFingerprint.h"
#include "io/chk/CheckpointCompatibility.h"
#include "io/hdf5/HDF5Writer.h"
#include "physics/species/Species.h"

#include <highfive/H5File.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
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
           "legacy checkpoint was presented as provenance-verified");
}

void test_hdf5_v3_round_trip(const std::filesystem::path& directory)
{
    const auto table = directory / "round-trip-table.bin";
    const auto checkpoint_path = directory / "checkpoint-v3.h5";
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
           "v3 checkpoint did not preserve ENUC state");
    expect(restored.rhoX == checkpoint.rhoX,
           "v3 checkpoint changed species conserved state");
    expect(io::require_checkpoint_provenance_compatible(
               restored.provenance, expected),
           "v3 checkpoint lost its scientific identity");
    expect(restored.provenance.eos_table_sha256
               != arch::core::file_sha256(table.string()),
           "checkpoint output re-fingerprinted the table path instead of "
           "preserving the loaded EOS identity");
    expect(restored.provenance.burn_enabled
               && restored.provenance.active_network == "aprox19"
               && restored.provenance.nse_enabled,
           "v3 checkpoint lost its active burn/NSE identity");

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

    // A v1/v2 reader contract deliberately ignores fields introduced later.
    // Downgrading only the version marker is enough to exercise that branch;
    // real legacy files simply omit the ignored extra objects as well.
    {
        HighFive::File file(checkpoint_path.string(), HighFive::File::ReadWrite);
        file.getAttribute("checkpoint_version").write(2);
    }
    const auto legacy = io::read_hdf5_chk_impl(checkpoint_path.string());
    expect(!legacy.has_enuc_rate && !legacy.provenance.available
               && legacy.enuc_rate.empty(),
           "v2 checkpoint was incorrectly marked v3-compatible");

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
    const auto ideal_path = directory / "checkpoint-v3-ideal.h5";
    io::write_hdf5_chk_impl(
        ideal_path.string(), make_checkpoint(ideal_identity));
    const auto ideal = io::read_hdf5_chk_impl(ideal_path.string());
    expect(io::require_checkpoint_provenance_compatible(
               ideal.provenance, ideal_identity),
           "ideal-gas v3 identity did not round trip");
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
        test_hdf5_v3_round_trip(directory);
        std::cout << "checkpoint compatibility tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
