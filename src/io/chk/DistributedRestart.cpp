/**
 * @file DistributedRestart.cpp
 * @brief Validate a published native checkpoint set before original restoration.
 *
 * Restart retains the recorded rank ownership. The original reader remains the
 * authority for physics, composition, repairs and controller fields; Runtime
 * then establishes new handles, true EOS/ghost acceptance and Device storage.
 */
#include "io/IO.h"
#include "io/OutputCollection.h"
#include "io/hdf5/HDF5Writer.h"
#include "core/MpiCollective.h"
#include "core/files/FileFingerprint.h"
#include "amr/AMRControl.h"
#include "core/config/RuntimeParams.h"
#include "data/GlobalDefs.h"
#include <array>
#include <bit>
#include <filesystem>

void read_distributed_chk(const std::string& directory, amr::AMRControl& control,
    RunState& state, const SimConfig& config, const SpeciesManager& species,
    const io::CheckpointProvenance& expected_provenance,
    const io::CheckpointGeometryIdentity& expected_geometry)
{
#if ARCH_MPI_ENABLED
    using namespace arch;
    const auto [rank, size] = mpi::require_world();
    io::OutputCollection collection;
    io::CheckpointData checkpoint;
    std::string manifest_hash;
    mpi::prepare([&] {
        if (size < 2 || rank != control.tree->PartitionRank() || size != control.tree->PartitionSize()
            || !control.tree->GetActiveBlocks().empty())
            throw std::logic_error("Distributed restart requires an empty native rank at startup");
        collection = io::read_output_collection(directory);
        if (collection.kind != "checkpoint" || collection.shards.size() != static_cast<std::size_t>(size))
            throw std::invalid_argument("Checkpoint collection kind/rank count differs from launch");
        if (collection.domain_sha256 != io::output_domain_sha256(config))
            throw std::invalid_argument("Checkpoint collection root geometry or AMR bounds differ from configuration");
        manifest_hash = core::file_sha256((std::filesystem::path(directory) / "manifest.txt").string());
    });
    std::array<unsigned char, 64> minimum{}, maximum{};
    mpi::check(MPI_Allreduce(manifest_hash.data(), minimum.data(), 64, MPI_UNSIGNED_CHAR, MPI_MIN, MPI_COMM_WORLD));
    mpi::check(MPI_Allreduce(manifest_hash.data(), maximum.data(), 64, MPI_UNSIGNED_CHAR, MPI_MAX, MPI_COMM_WORLD));
    mpi::prepare([&] {
        if (minimum != maximum) throw std::runtime_error("Checkpoint manifest differs between ranks");
        if (rank == 0) io::verify_output_collection(directory, collection);
        const auto path = io::output_shard_path(directory, rank);
        const auto& shard = collection.shards[rank];
        if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(path))
            || std::filesystem::file_size(path) != shard.bytes
            || core::file_sha256(path.string()) != shard.sha256)
            throw std::runtime_error("Local checkpoint shard differs from its collection");
        checkpoint = io::read_hdf5_chk_impl(path.string());
        if (checkpoint.levels.size() != shard.leaves
            || checkpoint.levels.size() > static_cast<std::size_t>(control.pool->GetCapacity())
            || checkpoint.step_count != collection.step || checkpoint.chk_file_index != collection.output_index
            || std::bit_cast<std::uint64_t>(checkpoint.time) != std::bit_cast<std::uint64_t>(collection.time))
            throw std::runtime_error("Checkpoint shard clock, leaf count or local pool capacity is incompatible");
    });

    std::string local_topology, controller;
    std::vector<char> topology_hashes;
    mpi::prepare([&] {
        std::vector<amr::LogicalBlockKey> leaves;
        for (std::size_t i = 0; i < checkpoint.levels.size(); ++i)
            leaves.push_back({checkpoint.dim, checkpoint.levels[i], checkpoint.logical_x1.at(i),
                checkpoint.logical_x2.at(i), checkpoint.logical_x3.at(i)});
        local_topology = io::output_topology_sha256(leaves);
        topology_hashes.resize(static_cast<std::size_t>(size) * 64);
        controller = std::to_string(checkpoint.step_count) + "\n"
            + std::to_string(checkpoint.chk_file_index) + "\n" + std::to_string(checkpoint.plt_file_index) + "\n"
            + std::to_string(std::bit_cast<std::uint64_t>(checkpoint.time)) + "\n"
            + std::to_string(std::bit_cast<std::uint64_t>(checkpoint.dt_old)) + "\n"
            + std::to_string(std::bit_cast<std::uint64_t>(checkpoint.dt_burn)) + "\n"
            + std::to_string(checkpoint.has_timestep_state) + "\n"
            + std::to_string(checkpoint.resume_after_regrid);
        controller = core::string_sha256(controller);
    });
    mpi::check(MPI_Allgather(local_topology.data(), 64, MPI_CHAR,
        topology_hashes.data(), 64, MPI_CHAR, MPI_COMM_WORLD));
    mpi::check(MPI_Allreduce(controller.data(), minimum.data(), 64, MPI_UNSIGNED_CHAR, MPI_MIN, MPI_COMM_WORLD));
    mpi::check(MPI_Allreduce(controller.data(), maximum.data(), 64, MPI_UNSIGNED_CHAR, MPI_MAX, MPI_COMM_WORLD));
    mpi::prepare([&] {
        if (minimum != maximum) throw std::runtime_error("Checkpoint controller state differs between shards");
        if (core::string_sha256(std::string_view(topology_hashes.data(), topology_hashes.size())) != collection.topology_sha256)
            throw std::runtime_error("Checkpoint ownership topology differs from its collection");
        restore_chk(checkpoint, control, state, config, species, expected_provenance, expected_geometry);
    });
#else
    (void)directory; (void)control; (void)state; (void)config; (void)species; (void)expected_provenance; (void)expected_geometry;
    throw std::logic_error("Distributed restart requires ARCH_ENABLE_MPI=ON");
#endif
}
