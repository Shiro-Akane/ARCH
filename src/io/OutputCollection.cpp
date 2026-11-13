/**
 * @file OutputCollection.cpp
 * @brief Strict collection metadata and native shard integrity, without MPI policy.
 */
#include "io/OutputCollection.h"
#include "core/files/FileFingerprint.h"
#include "core/config/RuntimeParams.h"
#include <algorithm>
#include "core/files/RunIdentity.h"

#include <bit>
#include <cerrno>
#include <system_error>
#if defined(__linux__)
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/syscall.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#endif
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace io {
namespace {
bool digest(const std::string& value) {
    return value.size() == 64 && value.find_first_not_of("0123456789abcdef") == std::string::npos;
}
void validate(const OutputCollection& value) {
    if ((value.kind != "plot" && value.kind != "checkpoint")
        || !arch::core::valid_run_identity(value.run_id)
        || !digest(value.binary_sha256) || !digest(value.configuration_sha256)
        || !digest(value.eos_sha256) || !digest(value.topology_sha256) || !digest(value.domain_sha256)
        || value.output_index < 0 || value.step < 0 || !std::isfinite(value.time) || value.time < 0.0
        || value.shards.size() < 2
        || value.shards.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::runtime_error("Invalid distributed output collection identity");
    std::uint64_t leaves = 0;
    for (const auto& shard : value.shards) {
        if (!shard.leaves || !shard.bytes || !digest(shard.sha256)
            || shard.leaves > std::numeric_limits<std::uint64_t>::max() - leaves)
            throw std::runtime_error("Invalid distributed output shard record");
        leaves += shard.leaves;
    }
}
void key(std::istream& input, std::string_view expected) {
    std::string word;
    if (!(input >> word) || word != expected)
        throw std::runtime_error("Invalid distributed output manifest field: " + std::string(expected));
}
void regular(const std::filesystem::path& path) {
    // Canonical rank filenames below never consume a manifest-supplied path.
    if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(path)))
        throw std::runtime_error("Missing or non-regular distributed output file: " + path.string());
}
} // namespace

std::string output_domain_sha256(const SimConfig& config) {
    const auto& grid = config.grid;
    std::string record = "arch-output-domain-1\n" + grid.geometry + "\n";
    for (int value : {grid.dim, grid.nblockx1, grid.nblockx2, grid.nblockx3,
                     config.amr.lrefinemin, config.amr.lrefinemax})
        record += std::to_string(value) + "\n";
    for (double value : {grid.x1_min, grid.x1_max, grid.x2_min, grid.x2_max, grid.x3_min, grid.x3_max})
        record += std::to_string(std::bit_cast<std::uint64_t>(value)) + "\n";
    return arch::core::string_sha256(record);
}

std::string output_topology_sha256(std::span<const amr::LogicalBlockKey> leaves) {
    std::vector<amr::LogicalBlockKey> sorted(leaves.begin(), leaves.end());
    std::sort(sorted.begin(), sorted.end());
    if (sorted.empty() || std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end())
        throw std::runtime_error("Output topology is empty or duplicated");
    std::string record = "arch-output-local-topology-1\n";
    for (const auto& leaf : sorted) {
        record += std::to_string(leaf.dimension) + " " + std::to_string(leaf.level) + " "
            + std::to_string(leaf.logical_x1) + " " + std::to_string(leaf.logical_x2) + " "
            + std::to_string(leaf.logical_x3) + "\n";
    }
    return arch::core::string_sha256(record);
}

std::filesystem::path output_shard_path(const std::filesystem::path& directory, int rank) {
    if (rank < 0) throw std::invalid_argument("Negative output shard rank");
    return directory / ("rank-" + std::to_string(rank)) / "state.h5";
}

void write_output_collection(const std::filesystem::path& directory, const OutputCollection& value) {
    validate(value);
    const auto path = directory / "manifest.txt";
    if (std::filesystem::exists(path))
        throw std::runtime_error("Distributed output manifest already exists");
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("Cannot create distributed output manifest");
    out << "ARCH_OUTPUT_COLLECTION 1\nkind " << value.kind
        << "\nrun " << value.run_id
        << "\nbinary " << value.binary_sha256
        << "\nconfiguration " << value.configuration_sha256
        << "\neos " << value.eos_sha256
        << "\ntopology " << value.topology_sha256
        << "\ndomain " << value.domain_sha256
        << "\nindex " << value.output_index << "\nstep " << value.step
        << "\ntime_bits " << std::bit_cast<std::uint64_t>(value.time)
        << "\nranks " << value.shards.size() << '\n';
    for (std::size_t rank = 0; rank < value.shards.size(); ++rank) {
        const auto& shard = value.shards[rank];
        out << "shard " << rank << ' ' << shard.leaves << ' ' << shard.bytes
            << ' ' << shard.sha256 << '\n';
    }
    out << "end\n";
    out.close();
    if (!out) throw std::runtime_error("Cannot finish distributed output manifest");
}

static OutputCollection read_manifest(const std::filesystem::path& directory) {
    const auto path = directory / "manifest.txt";
    regular(path);
    std::ifstream input(path, std::ios::binary);
    int version = 0;
    key(input, "ARCH_OUTPUT_COLLECTION"); input >> version;
    if (version != 1) throw std::runtime_error("Unsupported distributed output manifest version");
    OutputCollection value;
    key(input, "kind"); input >> value.kind;
    key(input, "run"); input >> value.run_id;
    key(input, "binary"); input >> value.binary_sha256;
    key(input, "configuration"); input >> value.configuration_sha256;
    key(input, "eos"); input >> value.eos_sha256;
    key(input, "topology"); input >> value.topology_sha256;
    key(input, "domain"); input >> value.domain_sha256;
    key(input, "index"); input >> value.output_index;
    key(input, "step"); input >> value.step;
    std::uint64_t bits = 0, ranks = 0;
    key(input, "time_bits"); input >> bits; value.time = std::bit_cast<double>(bits);
    key(input, "ranks"); input >> ranks;
    // Every rank needs a nonempty textual record; reject impossible counts
    // before allocation, using this actual manifest size instead of a policy cap.
    if (!input || ranks < 2 || ranks > std::filesystem::file_size(path) / 70
        || ranks > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
        throw std::runtime_error("Invalid distributed output manifest rank count");
    value.shards.resize(static_cast<std::size_t>(ranks));
    for (std::uint64_t rank = 0; rank < ranks; ++rank) {
        std::uint64_t recorded = ranks;
        auto& shard = value.shards[static_cast<std::size_t>(rank)];
        key(input, "shard");
        input >> recorded >> shard.leaves >> shard.bytes >> shard.sha256;
        if (!input || recorded != rank)
            throw std::runtime_error("Missing, duplicated or unordered distributed output shard");
    }
    key(input, "end");
    input >> std::ws;
    if (!input.eof()) throw std::runtime_error("Trailing distributed output manifest fields");
    validate(value);
    return value;
}


OutputCollection read_output_collection(const std::filesystem::path& directory) {
    if (directory.extension() != ".collection"
        || !std::filesystem::is_directory(std::filesystem::symlink_status(directory)))
        throw std::runtime_error("Restart/output collection has not been published");
    return read_manifest(directory);
}

void publish_output_collection(const std::filesystem::path& staged, const std::filesystem::path& destination) {
    if (destination.extension() != ".collection" || staged == destination
        || staged.parent_path() != destination.parent_path()
        || !std::filesystem::is_directory(std::filesystem::symlink_status(staged)))
        throw std::runtime_error("Invalid output collection publication paths");
    verify_output_collection(staged, read_manifest(staged));
    // Atomic visibility and no replacement are both required. A failed
    // publication leaves its staging evidence and any previous output intact.
#if defined(__linux__) && defined(SYS_renameat2)
    if (::syscall(SYS_renameat2, AT_FDCWD, staged.c_str(), AT_FDCWD, destination.c_str(), RENAME_NOREPLACE) != 0)
        throw std::system_error(errno, std::generic_category(), "Publish output collection");
#elif defined(_WIN32)
    if (!MoveFileW(staged.c_str(), destination.c_str()))
        throw std::system_error(GetLastError(), std::system_category(), "Publish output collection");
#else
    throw std::runtime_error("Atomic non-replacing collection publication is unavailable on this platform");
#endif
}

void verify_output_collection(const std::filesystem::path& directory, const OutputCollection& value) {
    validate(value);
    for (std::size_t rank = 0; rank < value.shards.size(); ++rank) {
        const auto path = output_shard_path(directory, static_cast<int>(rank));
        if (!std::filesystem::is_directory(std::filesystem::symlink_status(path.parent_path())))
            throw std::runtime_error("Missing or non-directory output shard parent");
        regular(path);
        const auto& shard = value.shards[rank];
        if (std::filesystem::file_size(path) != shard.bytes
            || arch::core::file_sha256(path.string()) != shard.sha256)
            throw std::runtime_error("Distributed output shard integrity mismatch: " + path.string());
    }
}
} // namespace io
