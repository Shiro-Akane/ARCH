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
#include <bit>
#include <map>
#include <memory>
#include <span>
#include <stdexcept>
#include <tuple>
#include <vector>

#include "AmrTransferPlans.h"
#include "Block.h"
#include "MemoryPool.h"
#include "../grid/GridMetrics.h"

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

    void ApplyRegistrationPlan(
        const FluxRegistrationPlan& plan,
        std::span<const double> values,
        const std::map<AmrEndpoint, int>& pool_lowering)
    {
        validate_amr_plan(plan);
        if (values.size() != plan.operations.size())
            throw std::invalid_argument(
                "flux registration values do not match logical plan");

        struct CompiledContribution {
            int block_id = -1;
            int face = -1;
            int cell = -1;
            AmrField field = AmrField::Rho;
            int component = -1;
            RefinementRule rule = RefinementRule::FineFluxContribution;
            double weight = 0.0;
            double value = 0.0;
        };
        std::vector<CompiledContribution> compiled;
        compiled.reserve(plan.operations.size());
        for (std::size_t index = 0; index < plan.operations.size(); ++index) {
            const auto& operation = plan.operations[index];
            const auto source = pool_lowering.find(operation.source);
            const auto destination = pool_lowering.find(operation.destination);
            if (source == pool_lowering.end()
                || destination == pool_lowering.end())
                throw std::invalid_argument(
                    "flux registration endpoint is not active");
            const int face = 2 * axis_value(operation.axis)
                + side_value(operation.side);
            const int cell = face_cell_index(
                operation.destination_box, operation.axis);
            if (destination->second < 0 || destination->second >= capacity_
                || face < 0 || face >= 2 * plan.dimension
                || cell < 0 || cell >= face_cells_[face])
                throw std::out_of_range(
                    "flux registration destination is outside storage");
            if ((operation.rule == RefinementRule::FineFluxContribution
                    && operation.sign != 1.0)
                || (operation.rule
                        == RefinementRule::CoarseFluxContribution
                    && operation.sign != -1.0))
                throw std::invalid_argument(
                    "flux registration sign does not match its route");
            if (operation.field == AmrField::EnucRate)
                throw std::invalid_argument(
                    "ENUC is not a conservative face flux");
            if (operation.field == AmrField::Species
                && operation.component >= num_species_)
                throw std::invalid_argument(
                    "flux registration species is outside storage");
            if (!amr_plan_detail::is_finite_binary64(values[index]))
                throw std::invalid_argument(
                    "flux registration value is nonfinite");
            compiled.push_back({
                destination->second, face, cell, operation.field,
                operation.component, operation.rule, operation.weight,
                values[index]});
        }

        for (const auto& contribution : compiled) {
            if (contribution.field == AmrField::Species) {
                if (contribution.rule
                    == RefinementRule::FineFluxContribution) {
                    AddFineSpeciesFlux(
                        contribution.block_id, contribution.face,
                        contribution.cell, contribution.component,
                        contribution.value, contribution.weight);
                } else {
                    AddCoarseSpeciesFlux(
                        contribution.block_id, contribution.face,
                        contribution.cell, contribution.component,
                        contribution.value, contribution.weight);
                }
                continue;
            }
            FluidVector flux{};
            switch (contribution.field) {
            case AmrField::Rho: flux.rho = contribution.value; break;
            case AmrField::MomU: flux.mom_u = contribution.value; break;
            case AmrField::MomV: flux.mom_v = contribution.value; break;
            case AmrField::MomW: flux.mom_w = contribution.value; break;
            case AmrField::Energy: flux.eng = contribution.value; break;
            case AmrField::EnucRate:
            case AmrField::Species:
                throw std::logic_error(
                    "invalid compiled conservative flux field");
            }
            if (contribution.rule
                == RefinementRule::FineFluxContribution) {
                AddFineFlux(contribution.block_id, contribution.face,
                            contribution.cell, flux, contribution.weight);
            } else {
                AddCoarseFlux(contribution.block_id, contribution.face,
                              contribution.cell, flux, contribution.weight);
            }
        }
    }

    RefluxPlan BuildRefluxPlan(
        const std::shared_ptr<MemoryPool>& pool,
        std::span<const int> active_blocks,
        std::span<const BlockHandle> handles, int dim, double dt) const
    {
        if (dim != dim_ || handles.size() != active_blocks.size()
            || handles.empty()
            || !amr_plan_detail::is_finite_binary64(dt) || dt < 0.0)
            throw std::invalid_argument("invalid Host reflux plan inputs");
        RefluxPlan plan{};
        plan.dimension = dim;
        plan.scope = {0, handles.front().epoch, handles.front().epoch};
        for (std::size_t block_index = 0;
             block_index < active_blocks.size(); ++block_index) {
            const int block_id = active_blocks[block_index];
            if (block_id < 0 || block_id >= capacity_)
                throw std::out_of_range("reflux block is outside storage");
            const Block& block = pool->GetBlock(block_id);
            const AmrEndpoint endpoint{
                logical_key(block, dim), handles[block_index]};
            const int species = block.fluid_state.GetNumSpecies();
            if (species != num_species_)
                throw std::invalid_argument(
                    "reflux state and register species counts differ");
            for (int face = 0; face < 2 * dim; ++face) {
                if (!HasData(block_id, face)) continue;
                const AmrAxis axis = static_cast<AmrAxis>(face / 2);
                const AmrSide side = static_cast<AmrSide>(face % 2);
                for (int cell = 0; cell < face_cells_[face]; ++cell) {
                    const auto logical = face_cell_coordinates(
                        cell, axis, side, dim);
                    const int i = block.grid.Is() + logical[0];
                    const int j = block.grid.Js() + logical[1];
                    const int k = block.grid.Ks() + logical[2];
                    const double area = GridMetrics::FaceArea(
                        block.grid, axis_value(axis), i, j, k,
                        side == AmrSide::Upper);
                    const double volume = GridMetrics::CellVolume(
                        block.grid, i, j, k);
                    if (!amr_plan_detail::is_finite_binary64(area)
                        || !amr_plan_detail::is_finite_binary64(volume)
                        || area <= 0.0 || volume <= 0.0)
                        throw std::invalid_argument(
                            "reflux metric is nonpositive or nonfinite");
                    LogicalAmrBox box{
                        {logical[0], logical[1], logical[2]}, {1, 1, 1}};
                    const double weight = dt * area / volume;
                    const double sign = side == AmrSide::Lower ? 1.0 : -1.0;
                    const auto append = [&](AmrField field, int component) {
                        plan.operations.push_back({
                            0, endpoint, endpoint, box, box, axis, side,
                            field, component,
                            RefinementRule::RefluxCorrection,
                            weight, sign});
                    };
                    append(AmrField::Rho, -1);
                    append(AmrField::MomU, -1);
                    append(AmrField::MomV, -1);
                    append(AmrField::MomW, -1);
                    append(AmrField::Energy, -1);
                    for (int component = 0; component < species; ++component)
                        append(AmrField::Species, component);
                }
            }
        }
        finalize_amr_plan(plan);
        return plan;
    }

    void ExecuteRefluxPlan(
        const RefluxPlan& plan,
        const std::shared_ptr<MemoryPool>& pool,
        std::span<const int> active_blocks,
        std::span<const BlockHandle> handles,
        FluidState Block::* state_ptr)
    {
        validate_amr_plan(plan);
        if (handles.size() != active_blocks.size())
            throw std::invalid_argument("reflux Host view count mismatch");
        std::map<LogicalBlockKey, std::size_t> active_index;
        for (std::size_t index = 0; index < active_blocks.size(); ++index) {
            const Block& block = pool->GetBlock(active_blocks[index]);
            if (!active_index.emplace(
                    logical_key(block, plan.dimension), index).second
                || handles[index].epoch != plan.scope.from_epoch)
                throw std::invalid_argument("invalid reflux Host endpoint");
        }

        struct CompiledGroup {
            int block_id = -1;
            int face = -1;
            int cell = -1;
            double weight = 0.0;
            double sign = 1.0;
            std::array<bool, 5> fluid_fields{};
            std::vector<bool> species_fields;
        };
        using GroupKey = std::tuple<
            AmrEndpoint, LogicalAmrBox, AmrAxis, AmrSide>;
        std::map<GroupKey, CompiledGroup> grouped;
        for (const auto& operation : plan.operations) {
            const auto found = active_index.find(operation.destination.logical);
            if (found == active_index.end()
                || handles[found->second] != operation.destination.handle
                || operation.source != operation.destination
                || operation.rule != RefinementRule::RefluxCorrection)
                throw std::invalid_argument("stale reflux logical endpoint");
            Block& block = pool->GetBlock(active_blocks[found->second]);
            FluidState& state = block.*state_ptr;
            const int species = state.GetNumSpecies();
            if (species != num_species_)
                throw std::invalid_argument("reflux species count mismatch");
            const int face = 2 * axis_value(operation.axis)
                + side_value(operation.side);
            const int cell = face_cell_index(
                operation.destination_box, operation.axis);
            if (!HasData(active_blocks[found->second], face)
                || cell < 0 || cell >= face_cells_[face])
                throw std::invalid_argument(
                    "reflux logical cell has no register data");

            const GroupKey key{
                operation.destination, operation.destination_box,
                operation.axis, operation.side};
            auto [entry, inserted] = grouped.try_emplace(key);
            CompiledGroup& group = entry->second;
            if (inserted) {
                group.block_id = active_blocks[found->second];
                group.face = face;
                group.cell = cell;
                group.weight = operation.weight;
                group.sign = operation.sign;
                group.species_fields.assign(
                    static_cast<std::size_t>(species), false);
            } else if (group.block_id != active_blocks[found->second]
                       || group.face != face || group.cell != cell
                       || std::bit_cast<std::uint64_t>(group.weight)
                           != std::bit_cast<std::uint64_t>(operation.weight)
                       || std::bit_cast<std::uint64_t>(group.sign)
                           != std::bit_cast<std::uint64_t>(operation.sign)) {
                throw std::invalid_argument(
                    "reflux logical group metadata is inconsistent");
            }

            if (operation.field == AmrField::Species) {
                if (operation.component < 0
                    || operation.component >= species
                    || group.species_fields[
                        static_cast<std::size_t>(operation.component)])
                    throw std::invalid_argument(
                        "reflux species field is duplicate or invalid");
                group.species_fields[
                    static_cast<std::size_t>(operation.component)] = true;
            } else {
                const int field = static_cast<int>(operation.field);
                if (field < static_cast<int>(AmrField::Rho)
                    || field > static_cast<int>(AmrField::Energy)
                    || group.fluid_fields[static_cast<std::size_t>(field)])
                    throw std::invalid_argument(
                        "reflux fluid field is duplicate or invalid");
                group.fluid_fields[static_cast<std::size_t>(field)] = true;
            }
        }

        std::vector<CompiledGroup> groups;
        groups.reserve(grouped.size());
        for (auto& [key, group] : grouped) {
            (void)key;
            if (!std::all_of(group.fluid_fields.begin(),
                             group.fluid_fields.end(),
                             [](bool present) { return present; })
                || !std::all_of(group.species_fields.begin(),
                                group.species_fields.end(),
                                [](bool present) { return present; }))
                throw std::invalid_argument("reflux field group is incomplete");
            groups.push_back(std::move(group));
        }

        for (const auto& group : groups) {
            Block& block = pool->GetBlock(group.block_id);
            FluidState& state = block.*state_ptr;
            const AmrAxis axis = static_cast<AmrAxis>(group.face / 2);
            const AmrSide side = static_cast<AmrSide>(group.face % 2);
            const auto logical = face_cell_coordinates(
                group.cell, axis, side, plan.dimension);
            const int index = block.grid.GetIndex(
                block.grid.Is() + logical[0],
                block.grid.Js() + logical[1],
                block.grid.Ks() + logical[2]);
            const FluidVector delta = GetSummedFlux(
                group.block_id, group.face, group.cell);
            const double correction = group.sign * group.weight;
            const double rho_before = state.rho[index];
            state.rho[index] += correction * delta.rho;
            state.mom_u[index] += correction * delta.mom_u;
            state.mom_v[index] += correction * delta.mom_v;
            state.mom_w[index] += correction * delta.mom_w;
            state.eng[index] += correction * delta.eng;
            for (int species = 0; species < state.GetNumSpecies(); ++species) {
                const double rho_x = rho_before * state.X(species, index)
                    + correction * GetSummedSpeciesFlux(
                        group.block_id, group.face, group.cell, species);
                state.X(species, index) = rho_x / state.rho[index];
            }
        }
    }

private:
    static LogicalBlockKey logical_key(const Block& block, int dim)
    {
        return {dim, block.level, block.logical_x1,
                block.logical_x2, block.logical_x3};
    }

    static int face_cell_index(const LogicalAmrBox& box, AmrAxis axis)
    {
        if (box.extent != std::array<std::uint32_t, 3>{1, 1, 1})
            throw std::invalid_argument(
                "flux logical destination is not one cell");
        const int i = box.first[0];
        const int j = box.first[1];
        const int k = box.first[2];
        switch (axis) {
        case AmrAxis::X: return k * BLOCK_NY + j;
        case AmrAxis::Y: return k * BLOCK_NX + i;
        case AmrAxis::Z: return j * BLOCK_NX + i;
        }
        throw std::invalid_argument("invalid flux logical axis");
    }

    static std::array<std::int32_t, 3> face_cell_coordinates(
        int cell, AmrAxis axis, AmrSide side, int dim)
    {
        const int nx = BLOCK_NX;
        const int ny = dim >= 2 ? BLOCK_NY : 1;
        const int nz = dim == 3 ? BLOCK_NZ : 1;
        std::array<std::int32_t, 3> logical{};
        switch (axis) {
        case AmrAxis::X:
            logical = {side == AmrSide::Lower ? 0 : nx - 1,
                       cell % ny, cell / ny};
            break;
        case AmrAxis::Y:
            logical = {cell % nx,
                       side == AmrSide::Lower ? 0 : ny - 1,
                       cell / nx};
            break;
        case AmrAxis::Z:
            logical = {cell % nx, cell / nx,
                       side == AmrSide::Lower ? 0 : nz - 1};
            break;
        }
        return logical;
    }
};

} // namespace amr
