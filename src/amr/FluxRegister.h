/**
 * @file FluxRegister.h
 * @brief Flux register for coarse-fine boundary refluxing.
 */

/**
 * Workflow:
 * 1. Build or query topology using the single hierarchy and memory-pool ownership model.
 * 2. Synchronize state or face data with the documented 2:1 AMR index convention.
 * 3. Return conservative leaf data to the driver for refluxing, regridding, or timestep work.
 */

#pragma once

#include <algorithm>
#include <array>
#include <stdexcept>
#include <vector>

#include "Block.h"

namespace amr {

/**
 * Stores coarse/fine flux differences in one contiguous, dimension-aware
 * arena.  Fluid and species fluxes share identical face indexing so a reflux
 * update always corrects conserved quantities at the same interface cells.
 */
class FluxRegister {
private:
    int dim_ = 0;
    int capacity_ = 0;
    int block_stride_ = 0;
    int num_species_ = 0;
    std::array<int, 6> face_cells_{};
    std::array<int, 6> face_offsets_{};
    std::vector<FluidVector> fluxes_; // [block][active face][face cell]
    std::vector<double> species_fluxes_; // [block][active face][face cell][species]
    std::vector<int> active_;         // [block][face], atomic write target

    void ConfigureLayout(int dim) {
        if (dim < 1 || dim > 3) {
            throw std::invalid_argument("FluxRegister requires dimension 1, 2, or 3.");
        }

        const int ny = (dim >= 2) ? BLOCK_NY : 1;
        const int nz = (dim == 3) ? BLOCK_NZ : 1;

        face_cells_.fill(0);
        face_cells_[0] = face_cells_[1] = ny * nz;
        if (dim >= 2) {
            face_cells_[2] = face_cells_[3] = BLOCK_NX * nz;
        }
        if (dim == 3) {
            face_cells_[4] = face_cells_[5] = BLOCK_NX * ny;
        }

        block_stride_ = 0;
        for (int face = 0; face < 6; ++face) {
            face_offsets_[face] = block_stride_;
            block_stride_ += face_cells_[face];
        }
    }

    size_t FaceSlot(int block_id, int face_dir) const {
        return static_cast<size_t>(block_id) * 6 + face_dir;
    }

    size_t FluxSlot(int block_id, int face_dir, int cell_idx) const {
        return static_cast<size_t>(block_id) * block_stride_ + face_offsets_[face_dir] + cell_idx;
    }

    FluidVector& FluxAt(int block_id, int face_dir, int cell_idx) {
        return fluxes_[FluxSlot(block_id, face_dir, cell_idx)];
    }

    const FluidVector& FluxAt(int block_id, int face_dir, int cell_idx) const {
        return fluxes_[FluxSlot(block_id, face_dir, cell_idx)];
    }

    size_t SpeciesFluxSlot(int block_id, int face_dir, int cell_idx, int species) const {
        return FluxSlot(block_id, face_dir, cell_idx) * num_species_ + species;
    }

public:
    FluxRegister() = default;

    void Resize(int max_blocks, int dim) {
        if (max_blocks < 0) {
            throw std::invalid_argument("FluxRegister capacity cannot be negative.");
        }
        if (dim_ == dim && capacity_ >= max_blocks) return;

        ConfigureLayout(dim);
        dim_ = dim;
        capacity_ = max_blocks;
        fluxes_.assign(static_cast<size_t>(capacity_) * block_stride_, FluidVector{});
        active_.assign(static_cast<size_t>(capacity_) * 6, 0);
        species_fluxes_.assign(static_cast<size_t>(capacity_) * block_stride_ * num_species_, 0.0);
    }

    void EnsureSpecies(int num_species) {
        if (num_species < 0) {
            throw std::invalid_argument("FluxRegister species count cannot be negative.");
        }
        if (num_species_ == num_species) return;
        num_species_ = num_species;
        species_fluxes_.assign(static_cast<size_t>(capacity_) * block_stride_ * num_species_, 0.0);
    }

    int GetNumSpecies() const { return num_species_; }

    void Clear() {
        for (int block_id = 0; block_id < capacity_; ++block_id) {
            for (int face_dir = 0; face_dir < 6; ++face_dir) {
                const size_t face_slot = FaceSlot(block_id, face_dir);
                if (active_[face_slot] == 0) continue;

                const size_t begin = FluxSlot(block_id, face_dir, 0);
                std::fill_n(fluxes_.begin() + begin, face_cells_[face_dir], FluidVector{});
                if (num_species_ > 0) {
                    std::fill_n(species_fluxes_.begin() + begin * num_species_,
                                static_cast<size_t>(face_cells_[face_dir]) * num_species_, 0.0);
                }
                active_[face_slot] = 0;
            }
        }
    }

    void AddFineFlux(int coarse_id, int coarse_face_dir, int coarse_cell_idx,
                     const FluidVector& flux, double weight) {
        FluidVector& target = FluxAt(coarse_id, coarse_face_dir, coarse_cell_idx);
#pragma omp atomic update
        target.rho += flux.rho * weight;
#pragma omp atomic update
        target.mom_u += flux.mom_u * weight;
#pragma omp atomic update
        target.mom_v += flux.mom_v * weight;
#pragma omp atomic update
        target.mom_w += flux.mom_w * weight;
#pragma omp atomic update
        target.eng += flux.eng * weight;
#pragma omp atomic write
        active_[FaceSlot(coarse_id, coarse_face_dir)] = 1;
    }

    void AddCoarseFlux(int block_id, int face_dir, int cell_idx,
                       const FluidVector& flux, double weight) {
        FluidVector& target = FluxAt(block_id, face_dir, cell_idx);
#pragma omp atomic update
        target.rho -= flux.rho * weight;
#pragma omp atomic update
        target.mom_u -= flux.mom_u * weight;
#pragma omp atomic update
        target.mom_v -= flux.mom_v * weight;
#pragma omp atomic update
        target.mom_w -= flux.mom_w * weight;
#pragma omp atomic update
        target.eng -= flux.eng * weight;
#pragma omp atomic write
        active_[FaceSlot(block_id, face_dir)] = 1;
    }

    void AddFineSpeciesFlux(int coarse_id, int coarse_face_dir, int coarse_cell_idx,
                            int species, double flux, double weight) {
#pragma omp atomic update
        species_fluxes_[SpeciesFluxSlot(coarse_id, coarse_face_dir, coarse_cell_idx, species)] += flux * weight;
#pragma omp atomic write
        active_[FaceSlot(coarse_id, coarse_face_dir)] = 1;
    }

    void AddCoarseSpeciesFlux(int block_id, int face_dir, int cell_idx,
                              int species, double flux, double weight) {
#pragma omp atomic update
        species_fluxes_[SpeciesFluxSlot(block_id, face_dir, cell_idx, species)] -= flux * weight;
#pragma omp atomic write
        active_[FaceSlot(block_id, face_dir)] = 1;
    }

    bool HasData(int coarse_id, int face_dir) const {
        return active_[FaceSlot(coarse_id, face_dir)] != 0;
    }

    FluidVector GetSummedFlux(int coarse_id, int face_dir, int coarse_cell_idx) const {
        return FluxAt(coarse_id, face_dir, coarse_cell_idx);
    }

    double GetSummedSpeciesFlux(int coarse_id, int face_dir, int coarse_cell_idx, int species) const {
        return species_fluxes_[SpeciesFluxSlot(coarse_id, face_dir, coarse_cell_idx, species)];
    }
};

} // namespace amr
