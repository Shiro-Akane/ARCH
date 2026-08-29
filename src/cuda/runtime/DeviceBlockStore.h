/**
 * @file DeviceBlockStore.h
 * @brief Ordinary-C++ identity index for one-device block storage.
 */

#pragma once

#include "driver/ComputeBackend.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <span>
#include <stdexcept>
#include <vector>

namespace arch::cuda {

struct DeviceLayoutGeneration {
    std::uint64_t value = 0;
    friend constexpr auto operator<=>(const DeviceLayoutGeneration&,
                                      const DeviceLayoutGeneration&) = default;
};

constexpr bool is_valid(DeviceLayoutGeneration generation) noexcept
{
    return generation.value != 0;
}

struct DeviceBlockRecord {
    amr::BlockHandle handle{};
    backend::StorageGeneration storage{};
    DeviceLayoutGeneration layout{};
};

class DeviceBlockStoreIndex {
public:
    explicit DeviceBlockStoreIndex(
        std::span<const DeviceBlockRecord> records)
        : records_(records.begin(), records.end())
    {
        if (records_.empty())
            throw std::invalid_argument("device block store is empty");
        const amr::TopologyEpoch epoch = records_.front().handle.epoch;
        std::set<backend::StorageGeneration> storage_generations;
        std::set<DeviceLayoutGeneration> layout_generations;
        for (std::size_t index = 0; index < records_.size(); ++index) {
            const auto& record = records_[index];
            if (!amr::is_valid(record.handle)
                || record.handle.epoch.value != epoch.value
                || !backend::is_valid(record.storage)
                || !is_valid(record.layout))
                throw std::invalid_argument("invalid device block record");
            if (!indices_.emplace(record.handle, index).second)
                throw std::invalid_argument("duplicate device BlockHandle");
            if (!storage_generations.insert(record.storage).second)
                throw std::invalid_argument(
                    "duplicate device storage generation");
            if (!layout_generations.insert(record.layout).second)
                throw std::invalid_argument(
                    "duplicate device layout generation");
        }
    }

    std::size_t size() const noexcept { return records_.size(); }

    std::span<const DeviceBlockRecord> records() const noexcept
    {
        return records_;
    }

    bool contains(backend::BackendStateAccess access) const noexcept
    {
        const auto found = indices_.find(access.block);
        return found != indices_.end()
            && records_[found->second].storage.value == access.storage.value;
    }

    std::size_t index_of(backend::BackendStateAccess access) const
    {
        const auto found = indices_.find(access.block);
        if (found == indices_.end()
            || records_[found->second].storage.value != access.storage.value)
            throw std::invalid_argument("stale device block access");
        return found->second;
    }

    const DeviceBlockRecord& record(
        backend::BackendStateAccess access) const
    {
        return records_[index_of(access)];
    }

private:
    std::vector<DeviceBlockRecord> records_;
    std::map<amr::BlockHandle, std::size_t> indices_;
};

} // namespace arch::cuda
