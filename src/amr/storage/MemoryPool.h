/**
 * @file MemoryPool.h
 * @brief Host ownership and free-list allocation of fixed-capacity AMR blocks.
 *
 * Allocate storage for all block slots and their three fluid states once, then
 * recycle slots without resizing the pool. A block ID is a pool index, not a
 * persistent topology identity: a freed index may later hold another block.
 * AmrTree owns the hierarchy; this class only owns and recycles its storage.
 * Workflow:
 * 1. Receive topology-owned block IDs and capacity bounds.
 * 2. Manage native block or pool storage and its lifetime.
 * 3. Expose storage only through checked active handles.
 */

#pragma once

#include <cstddef>
#include <exception>
#include <utility>
#include <stdexcept>
#include <vector>

#include "amr/storage/Block.h"

namespace amr {

/**
 * @brief Manages a contiguous pool of AMR blocks with a free list for O(1) allocation.
 */
class MemoryPool {
private:
    std::vector<Block> pool;
    std::vector<int> free_list; // Stack of free indices
    int max_blocks;
    int reserved_blocks = 0;

    void activate_block(int id) {
        pool[id].Reset();
        pool[id].id = id;
        pool[id].active = true;
    }

public:
    /**
     * Owns one batch from reservation through candidate construction. Until
     * CommitNoexcept(), destruction returns both constructed and unused slots.
     * The pool must outlive this owner; callers must not FreeBlock owned slots.
     * Storage never moves, so reserving does not invalidate existing views.
     */
    class Reservation {
    public:
        Reservation(const Reservation&) = delete;
        Reservation& operator=(const Reservation&) = delete;
        Reservation& operator=(Reservation&&) = delete;
        Reservation(Reservation&& other) noexcept
            : owner_(std::exchange(other.owner_, nullptr)),
              ids_(std::move(other.ids_)), used_(other.used_) {}
        ~Reservation() {
            if (!owner_) return;
            // Reverse acquisition restores the original free-list order.
            for (auto it = ids_.rbegin(); it != ids_.rend(); ++it) {
                owner_->pool[*it].active = false;
                owner_->free_list.push_back(*it);
            }
            owner_->reserved_blocks -= static_cast<int>(remaining());
        }

        int AllocateBlock() {
            if (!owner_ || remaining() == 0)
                throw std::logic_error("AMR block reservation is exhausted or committed");
            const int id = ids_[used_];
            owner_->activate_block(id);
            ++used_;
            --owner_->reserved_blocks;
            return id;
        }

        std::size_t remaining() const noexcept {
            return owner_ ? ids_.size() - used_ : 0;
        }

        // Publication transfers all constructed blocks to the existing tree.
        void CommitNoexcept() noexcept {
            if (!owner_ || remaining() != 0) std::terminate();
            owner_ = nullptr;
        }

    private:
        friend class MemoryPool;
        Reservation(MemoryPool& owner, std::vector<int> ids) noexcept
            : owner_(&owner), ids_(std::move(ids)) {}
        MemoryPool* owner_;
        std::vector<int> ids_;
        std::size_t used_ = 0;
    };

    MemoryPool(int max_capacity, int dim) : max_blocks(max_capacity) {
        pool.resize(max_capacity);
        free_list.reserve(max_capacity);

        int total_ny = (dim >= 2) ? (BLOCK_NY + 2 * MAX_NG) : 1;
        int total_nz = (dim == 3) ? (BLOCK_NZ + 2 * MAX_NG) : 1;
        int block_total_size = PAD_NX * total_ny * total_nz;

        // Initialize free list with all indices (in reverse order so 0 is popped first)
        for (int i = max_capacity - 1; i >= 0; --i) {
            free_list.push_back(i);
            pool[i].fluid_state.Preallocate(block_total_size);
            pool[i].state_next.Preallocate(block_total_size);
            pool[i].state_scratch.Preallocate(block_total_size);
            pool[i].id = i;
        }
    }

    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;
    MemoryPool(MemoryPool&&) = delete;
    MemoryPool& operator=(MemoryPool&&) = delete;

    Reservation ReserveBlocks(std::size_t count) {
        if (count > free_list.size())
            throw std::runtime_error("AMR MemoryPool exhausted! Increase MaxBlocks.");
        // Allocate bookkeeping before withdrawing any slots.
        std::vector<int> ids(free_list.rbegin(), free_list.rbegin() + count);
        free_list.resize(free_list.size() - count);
        reserved_blocks += static_cast<int>(count);
        return Reservation(*this, std::move(ids));
    }

    /**
     * @brief Allocates a block from the free list.
     * @return Index of the allocated block.
     */
    int AllocateBlock() {
        if (free_list.empty()) {
            throw std::runtime_error("AMR MemoryPool exhausted! Increase MaxBlocks.");
        }
        int id = free_list.back();
        activate_block(id);
        free_list.pop_back();
        return id;
    }

    /**
     * @brief Returns a block to the free list.
     * @param id The ID of the block to free.
     */
    void FreeBlock(int id) noexcept {
        if (id < 0 || id >= max_blocks || !pool[id].active) {
            return; // Invalid or already-free slots do not alter the free list.
        }
        pool[id].active = false;
        free_list.push_back(id);
    }

    /**
     * @brief Get a reference to a block by ID.
     */
    Block& GetBlock(int id) {
        return pool[id];
    }

    const Block& GetBlock(int id) const {
        return pool[id];
    }

    int GetCapacity() const noexcept { return max_blocks; }
    int GetNumAvailableBlocks() const noexcept {
        return static_cast<int>(free_list.size());
    }
    int GetNumReservedBlocks() const noexcept { return reserved_blocks; }

    // Includes unpublished constructed candidates, not just topology leaves.
    int GetNumActiveBlocks() const noexcept {
        return max_blocks - GetNumAvailableBlocks() - reserved_blocks;
    }
};

} // namespace amr
