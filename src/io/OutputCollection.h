/**
 * @file OutputCollection.h
 * @brief Integrity record for one complete set of native scientific HDF5 shards.
 *
 * DriverIO owns collective preparation/publication. This record owns no MPI
 * environment, field storage or numerical state; readers verify every named
 * shard before passing their local checkpoint to the original reader.
 */
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <span>
#include "amr/topology/BlockHandle.h"

struct SimConfig;
#include <vector>

namespace io {
struct OutputShard {
    std::uint64_t leaves = 0;
    std::uint64_t bytes = 0;
    std::string sha256;
};
struct OutputCollection {
    std::string kind; // "plot" or "checkpoint"
    std::string run_id;
    std::string binary_sha256;
    std::string configuration_sha256;
    std::string eos_sha256;
    std::string topology_sha256;
    std::string domain_sha256; // Exact root geometry and AMR bounds, independent of output paths.
    int output_index = 0;
    int step = 0;
    double time = 0.0;
    std::vector<OutputShard> shards; // Rank order is authoritative.
};
std::string output_domain_sha256(const SimConfig& config);
std::string output_topology_sha256(std::span<const amr::LogicalBlockKey> leaves);
std::filesystem::path output_shard_path(const std::filesystem::path& directory, int rank);
void write_output_collection(const std::filesystem::path& directory, const OutputCollection& value);
OutputCollection read_output_collection(const std::filesystem::path& directory);
void publish_output_collection(const std::filesystem::path& staged, const std::filesystem::path& destination);
void verify_output_collection(const std::filesystem::path& directory, const OutputCollection& value);
} // namespace io
