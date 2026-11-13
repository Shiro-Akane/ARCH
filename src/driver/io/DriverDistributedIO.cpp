/**
 * @file DriverDistributedIO.cpp
 * @brief Publish one complete rank collection from the original DriverIO owner.
 *
 * Native HDF5 remains the scientific format. A private same-directory staging
 * set becomes visible only after every rank finishes and rank zero verifies
 * all shards. Failed sets remain unpublished for diagnosis.
 */
#include "driver/io/DriverIO.h"
#include "driver/runtime/DriverRuntime.h"
#include "driver/schedule/DriverControl.h"
#include "core/MpiCollective.h"
#include "core/files/FileFingerprint.h"
#include "core/files/RunIdentity.h"
#include "io/OutputCollection.h"
#include "io/OutputPath.h"
#include "io/hdf5/HDF5Writer.h"
#include <algorithm>
#include <bit>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace arch::driver {
void DriverIO::write_distributed_output(bool checkpoint, double dt_burn_global,
    bool resume_after_regrid, std::span<const io::PlotScalarField> extra_fields,
    const std::function<void(OutputPhase)>& observer)
{
#if ARCH_MPI_ENABLED
    namespace fs = std::filesystem;
    const auto started = Clock::now();
    const auto [rank, size] = mpi::require_world();
    auto& control = runtime.control();
    const auto& config = runtime.configuration();
    const auto& specs = runtime.species();
    const auto notify = [&](OutputPhase phase) { if (observer) observer(phase); };
    io::OutputCollection collection;
    std::string agreement;
    mpi::prepare([&] {
        if (size < 2 || rank != control.tree->PartitionRank()
            || size != control.tree->PartitionSize() || runtime.handles().empty()
            || !plot_source_identity.formal)
            throw std::logic_error("Distributed output requires a complete native rank and formal identity");
        if (config.io.base_name.empty() || config.io.base_name == "." || config.io.base_name == ".."
            || fs::path(config.io.base_name).filename().string() != config.io.base_name)
            throw std::invalid_argument("Distributed output base_name must be one filename component");
        collection.kind = checkpoint ? "checkpoint" : "plot";
        collection.output_index = checkpoint ? ctrl.chk_file_index : ctrl.plt_file_index;
        collection.step = ctrl.step_count; collection.time = ctrl.t_current;
        collection.binary_sha256 = plot_source_identity.binary_sha256;
        collection.configuration_sha256 = plot_source_identity.effective_config_sha256;
        collection.eos_sha256 = plot_source_identity.eos_identity_sha256;
        collection.domain_sha256 = io::output_domain_sha256(config);
        collection.shards.resize(size);
        agreement = collection.kind + "\n" + std::to_string(collection.output_index) + "\n"
            + std::to_string(ctrl.step_count) + "\n"
            + std::to_string(std::bit_cast<std::uint64_t>(ctrl.t_current)) + "\n"
            + std::to_string(std::bit_cast<std::uint64_t>(ctrl.dt_old)) + "\n"
            + std::to_string(std::bit_cast<std::uint64_t>(dt_burn_global)) + "\n"
            + std::to_string(resume_after_regrid) + "\n"
            + std::to_string(ctrl.chk_file_index) + "\n" + std::to_string(ctrl.plt_file_index) + "\n"
            + collection.binary_sha256 + "\n" + collection.configuration_sha256 + "\n"
            + collection.eos_sha256 + "\n" + collection.domain_sha256 + "\n"
            + config.io.out_dir + "\n" + config.io.base_name;
        agreement = arch::core::string_sha256(agreement);
    });
    std::array<unsigned char, 64> low{}, high{};
    mpi::check(MPI_Allreduce(agreement.data(), low.data(), 64, MPI_UNSIGNED_CHAR, MPI_MIN, MPI_COMM_WORLD));
    mpi::check(MPI_Allreduce(agreement.data(), high.data(), 64, MPI_UNSIGNED_CHAR, MPI_MAX, MPI_COMM_WORLD));
    mpi::prepare([&] {
        if (low != high) throw std::logic_error("Distributed output clock, kind or run identity differs between ranks");
    });

    std::array<char, 36> run{};
    mpi::prepare([&] {
        if (rank == 0) {
            if (plot_run_id_.empty()) plot_run_id_ = arch::core::new_run_identity();
            if (!arch::core::valid_run_identity(plot_run_id_))
                throw std::logic_error("Invalid output session identity");
            std::copy_n(plot_run_id_.data(), run.size(), run.begin());
        }
    });
    mpi::check(MPI_Bcast(run.data(), run.size(), MPI_CHAR, 0, MPI_COMM_WORLD));
    mpi::prepare([&] {
        const std::string shared(run.data(), run.size());
        if (!plot_run_id_.empty() && plot_run_id_ != shared)
            throw std::logic_error("Distributed output session identity changed");
        plot_run_id_ = shared; collection.run_id = shared;
    });

    // The Runtime completes every local transfer failure collectively. Output
    // validation itself is read-only and ends before any rank opens a file.
    runtime.materialize_current_for_host();
    mpi::prepare([&] { validate_current_output(); });

    std::string local_topology;
    std::vector<char> topology_hashes;
    mpi::prepare([&] {
        std::vector<amr::LogicalBlockKey> keys;
        for (int id : control.tree->GetActiveBlocks()) {
            const auto& b = control.pool->GetBlock(id);
            keys.push_back({config.grid.dim, b.level, b.logical_x1, b.logical_x2, b.logical_x3});
        }
        local_topology = io::output_topology_sha256(keys);
        topology_hashes.resize(static_cast<std::size_t>(size) * 64);
    });
    mpi::check(MPI_Allgather(local_topology.data(), 64, MPI_CHAR,
        topology_hashes.data(), 64, MPI_CHAR, MPI_COMM_WORLD));
    mpi::prepare([&] {
        collection.topology_sha256 = arch::core::string_sha256(
            std::string_view(topology_hashes.data(), topology_hashes.size()));
    });

    fs::path staged, destination;
    std::array<char, 36> transaction{};
    mpi::prepare([&] {
        if (rank == 0) {
            const auto id = arch::core::new_run_identity();
            std::copy_n(id.data(), transaction.size(), transaction.begin());
        }
    });
    mpi::check(MPI_Bcast(transaction.data(), transaction.size(), MPI_CHAR, 0, MPI_COMM_WORLD));
    mpi::prepare([&] {
        std::ostringstream stem;
        stem << config.io.base_name << (checkpoint ? "_chk_" : "_plt_")
            << std::setw(4) << std::setfill('0') << collection.output_index;
        destination = fs::path(config.io.out_dir) / (stem.str() + ".collection");
        staged = fs::path(config.io.out_dir) / (stem.str() + ".partial-" + std::string(transaction.data(), transaction.size()));
        if (rank == 0) {
            fs::create_directories(config.io.out_dir);
            if (fs::exists(destination) || !fs::create_directory(staged))
                throw std::runtime_error("Distributed output destination or staging identity already exists");
        }
    });
    mpi::prepare([&] { notify(OutputPhase::Prepared); });
    io::OutputShard local;
    mpi::prepare([&] {
        auto target = config;
        target.io.out_dir = checkpoint ? (staged / ("rank-" + std::to_string(rank))).string() : staged.string();
        target.io.base_name = "state";
        if (checkpoint) {
            write_chk(control, ctrl.chk_file_index, ctrl.plt_file_index, ctrl.step_count,
                ctrl.t_current, ctrl.dt_old, dt_burn_global, resume_after_regrid, target, specs,
                checkpoint_provenance, ctrl.repairs,
                io::checkpoint_geometry_identity(config.grid.dim, config.grid.geometry));
        } else {
            write_plt(control, p_func, t_func, gamma1_func, eos, ctrl.plt_file_index,
                ctrl.t_current, target, specs, extra_fields, &checkpoint_provenance,
                plot_run_id_, &plot_source_identity);
        }
        std::ostringstream name;
        name << "state_chk_" << std::setw(4) << std::setfill('0')
            << collection.output_index << ".h5";
        const auto original_name=checkpoint ? name.str()
            : io::plot_filename(target.io.base_name,target.numerics.solver_name,collection.output_index);
        const auto path = io::output_shard_path(staged, rank);
        fs::rename(path.parent_path() / original_name, path);
        if (checkpoint) {
            const auto readback = io::read_hdf5_chk_impl(path.string());
            if (readback.step_count != ctrl.step_count || readback.chk_file_index != ctrl.chk_file_index
                || readback.plt_file_index != ctrl.plt_file_index
                || std::bit_cast<std::uint64_t>(readback.time) != std::bit_cast<std::uint64_t>(ctrl.t_current)
                || readback.levels.size() != control.tree->GetActiveBlocks().size())
                throw std::runtime_error("Distributed checkpoint readback changed its native identity");
        }
        local.leaves = control.tree->GetActiveBlocks().size();
        local.bytes = fs::file_size(path);
        local.sha256 = arch::core::file_sha256(path.string());
        notify(OutputPhase::ShardWritten);
    });
    std::array<std::uint64_t, 2> counts{local.leaves, local.bytes};
    std::vector<std::uint64_t> all_counts;
    std::vector<char> hashes;
    mpi::prepare([&] { all_counts.resize(static_cast<std::size_t>(size) * 2); hashes.resize(static_cast<std::size_t>(size) * 64); });
    mpi::check(MPI_Allgather(counts.data(), 2, MPI_UINT64_T, all_counts.data(), 2, MPI_UINT64_T, MPI_COMM_WORLD));
    mpi::check(MPI_Allgather(local.sha256.data(), 64, MPI_CHAR, hashes.data(), 64, MPI_CHAR, MPI_COMM_WORLD));
    mpi::prepare([&] {
        for (int peer = 0; peer < size; ++peer)
            collection.shards[peer] = {all_counts[2 * peer], all_counts[2 * peer + 1],
                std::string(hashes.data() + 64 * peer, 64)};
        if (rank == 0) {
            io::verify_output_collection(staged, collection);
            io::write_output_collection(staged, collection);
        }
        notify(OutputPhase::Verified);
    });
    mpi::prepare([&] { notify(OutputPhase::ReadyToPublish); });
    mpi::prepare([&] {
        if (rank == 0) io::publish_output_collection(staged, destination);
    });
    if (checkpoint) ++ctrl.chk_file_index; else ++ctrl.plt_file_index;
    output_seconds_ += std::chrono::duration<double>(Clock::now() - started).count();
    ++output_calls_;
#else
    (void)checkpoint; (void)dt_burn_global; (void)resume_after_regrid; (void)extra_fields; (void)observer;
    throw std::logic_error("Distributed output requires ARCH_ENABLE_MPI=ON");
#endif
}
} // namespace arch::driver
