/**
 * @file MemoryPool.h
 * @brief Host ownership and free-list allocation of fixed-capacity AMR blocks.
 *
 * Allocate storage for all block slots and their three fluid states once, then
 * recycle slots without resizing the pool. A block ID is a pool index, not a
 * persistent topology identity: a freed index may later hold another block.
 * AmrTree owns the hierarchy; this class only owns and recycles its storage.
 */

#pragma once

#include <iostream>
#include <stack>
#include <stdexcept>
#include <vector>

#include "Block.h"

namespace amr {

/**
 * @brief Manages a contiguous pool of AMR blocks with a free list for O(1) allocation.
 */
class MemoryPool {
private:
    std::vector<Block> pool;
    std::vector<int> free_list; // Stack of free indices
    int max_blocks;

public:
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

    /**
     * @brief Allocates a block from the free list.
     * @return Index of the allocated block.
     */
    int AllocateBlock() {
        if (free_list.empty()) {
            throw std::runtime_error("AMR MemoryPool exhausted! Increase MaxBlocks.");
        }
        int id = free_list.back();
        free_list.pop_back();

        // Reset block state (O(1) without reallocating vectors)
        pool[id].Reset();
        pool[id].id = id;
        pool[id].active = true;
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

    int GetNumActiveBlocks() const {
        return max_blocks - free_list.size();
    }
};

} // namespace amr
