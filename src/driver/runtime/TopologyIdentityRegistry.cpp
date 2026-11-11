/**
 * @file TopologyIdentityRegistry.cpp
 * @brief Collect logical ownership at the existing driver's topology boundary.
 *
 * This transports metadata only; the registry retains identity/epoch authority
 * and the tree/pool retain local fields. Call from the MPI initialization thread.
 */
#include "driver/runtime/TopologyIdentityRegistry.h"
#include "core/MpiCollective.h"

#if ARCH_MPI_ENABLED
#include <mpi.h>
#endif

namespace arch::topology {

std::vector<TopologyObservation> collect_topology_observations(
    const std::vector<TopologyObservation>& local, int rank, int rank_count)
{
#if ARCH_MPI_ENABLED
    int initialized = 0, finalized = 0, actual_rank = 0, actual_size = 1;
    MPI_Initialized(&initialized);
    if (initialized) MPI_Finalized(&finalized);
    if (!initialized || finalized) {
        if (rank == 0 && rank_count == 1) return local;
        throw std::logic_error("distributed topology requires live MPI");
    }
    const auto world = mpi::require_world();
    actual_rank=world.first; actual_size=world.second;
    if (actual_size == 1 && rank == 0 && rank_count == 1) return local;

    constexpr int words = 5; // dimension, level, global logical x/y/z
    constexpr auto max_records = std::numeric_limits<int>::max() / words;
    mpi::prepare([&] {
        if (rank!=actual_rank || rank_count!=actual_size || local.size()>static_cast<std::size_t>(max_records))
            throw std::invalid_argument("invalid local topology communicator/count");
        std::set<int> local_slots;
        std::set<LogicalBlockIdentity> local_keys;
        for (const auto& observation : local) {
            const auto& key=observation.identity;
            if (observation.owner_rank!=actual_rank || observation.pool_index<0
                || key.dimension<1 || key.dimension>3 || key.level<0 || key.level>=32
                || !local_slots.insert(observation.pool_index).second
                || !local_keys.insert(key).second)
                throw std::invalid_argument("invalid local topology on an MPI rank");
        }
    });

    const std::uint64_t local_count = local.size();
    std::vector<std::uint64_t> counts;
    mpi::prepare([&] {counts.resize(actual_size);});
    mpi::check(MPI_Allgather(&local_count, 1, MPI_UINT64_T, counts.data(), 1,
                  MPI_UINT64_T, MPI_COMM_WORLD));
    std::vector<int> wire_counts, displacements;
    std::vector<std::uint64_t> send, receive;
    std::vector<TopologyObservation> result;
    std::uint64_t total = 0;
    mpi::prepare([&] {
        wire_counts.resize(actual_size); displacements.resize(actual_size);
        for (int peer = 0; peer < actual_size; ++peer) {
            if (counts[peer] > static_cast<std::uint64_t>(max_records) - total)
                throw std::overflow_error("distributed topology exceeds MPI count range");
            displacements[peer] = static_cast<int>(total) * words;
            wire_counts[peer] = static_cast<int>(counts[peer]) * words;
            total += counts[peer];
        }
        send.resize(local.size()*words); receive.resize(total*words);
        result.reserve(total);
        for (std::size_t index = 0; index < local.size(); ++index) {
            const auto& key = local[index].identity;
            const std::size_t offset = index * words;
            send[offset] = static_cast<std::uint64_t>(key.dimension);
            send[offset + 1] = static_cast<std::uint64_t>(key.level);
            send[offset + 2] = key.logical_x1;
            send[offset + 3] = key.logical_x2;
            send[offset + 4] = key.logical_x3;
        }
    });
    mpi::check(MPI_Allgatherv(send.data(), wire_counts[actual_rank], MPI_UINT64_T,
                   receive.data(), wire_counts.data(), displacements.data(),
                   MPI_UINT64_T, MPI_COMM_WORLD));
    for (int peer = 0; peer < actual_size; ++peer) {
        for (std::size_t index = 0; index < counts[peer]; ++index) {
            const auto offset = static_cast<std::size_t>(displacements[peer]) + index * words;
            const auto* key = receive.data() + offset;
            // The sender rank is authoritative; no transmitted owner or pool ID
            // can claim another process's storage. Keep only our actual slots.
            result.push_back({peer == actual_rank ? local[index].pool_index : -1,
                {static_cast<int>(key[0]), static_cast<int>(key[1]),
                 static_cast<std::uint32_t>(key[2]), static_cast<std::uint32_t>(key[3]),
                 static_cast<std::uint32_t>(key[4])}, peer});
        }
    }
    return result;
#else
    if (rank == 0 && rank_count == 1) return local;
    throw std::logic_error("distributed topology requires ARCH_ENABLE_MPI=ON");
#endif
}

} // namespace arch::topology
