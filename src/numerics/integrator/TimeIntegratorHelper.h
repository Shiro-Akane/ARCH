/**
 * @file TimeIntegratorHelper.h
 * @brief Common helper functions for dimensional sweeping and state updates.
 *
 * Shared cell leaves compute delta U = dt*(A_left*F_left - A_right*F_right)/V
 * and combine normalized stages as
 * U_new = U_old + w_flux*((U_current - U_old) + delta U), w_n+w_flux=1.
 * This is the same convex RK combination in increment form; it preserves a
 * finite stationary state without a repeated multiply/add normalization bias.
 * A and V are physical face/cell measures from GridMetrics. Species increments
 * carry rho*X, then recover mass fractions after the density update and state
 * admissibility repairs. Host traversal calls these leaves and registers
 * coarse-fine fluxes; the integrator supplies weights and the driver schedules exchange.
 * Workflow for native RZ candidates:
 * 1. Combine conserved V/W means and species densities without floor repairs.
 * 2. Precheck finite fields, positive density and the shared mass-fraction simplex.
 * 3. Collect completed RK receipts before slot rotation; start a separate reflux row.
 * 4. After actual whole-domain BC/exchange, the Runtime-owned EOS gate validates
 *    thermal bounds with the same-stage density closure before ghost publication.
 */

#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#ifdef _OPENMP
#include <omp.h>
#endif

#include "amr/AMRControl.h"
#include "amr/flux/AMRFluxRegistering.h"
#include "data/FluidState.h"
#include "driver/runtime/StateResidency.h"
#include "grid/Grid.h"
#include "grid/GridMetrics.h"
#include "numerics/flux/InvariantDomainFlux.h"
#include "numerics/integrator/GeometricSources.h"
#include "numerics/state/RzNativeClosure.h"
#include "numerics/state/StateAdmissibility.h"
#include "physics/gravity/IGravityPolicy.h"
#include "physics/gravity/NativeExternalSource.h"
#include "physics/gravity/NativeSelfStage.h"

namespace TimeIntegration
{
    /**
     * Resolve a Host Hydro output slot before physical boundary or ghost writes.
     * Current is the actual final storage after rotation and reflux; it is not
     * an alias for Next. Integrators reject any narrower storage-profile slot
     * before using this shared mapping. Unknown enum values always fail closed.
     */
    inline FluidState amr::Block::* hydro_boundary_state_member(
        arch::state::StateSlot slot)
    {
        switch (slot) {
        case arch::state::StateSlot::Current: return &amr::Block::fluid_state;
        case arch::state::StateSlot::Next: return &amr::Block::state_next;
        case arch::state::StateSlot::Scratch: return &amr::Block::state_scratch;
        }
        throw std::logic_error("Host Hydro boundary selected unknown state slot");
    }

    /** Fill block ghosts and propagate callback failures after workers join. */
    template<class BCPolicy>
    inline void apply_domain_boundary(amr::AMRControl& control, BCPolicy& boundary,
                                     FluidState amr::Block::* member)
    {
        const auto& active = control.tree->GetActiveBlocks();
        arch::state::HostFailure failure;
#pragma omp parallel for schedule(dynamic, 1)
        for (std::size_t i = 0; i < active.size(); ++i) {
            try {
                auto& block = control.pool->GetBlock(active[i]);
                boundary.apply(block.*member, block.grid);
            } catch (...) { failure.capture_current(); }
        }
        failure.rethrow();
    }

    /** Synchronize one actual Host domain without owning GhostValid publication.
     * Workflow: Existing apply->exchange is unchanged. Native preflights the
     * actual domain/layout/chart and BC frame; applies builtin seeds; performs
     * actual exchange; prepares all immutable per-patch candidates; validates
     * all frames/candidates; publishes every surface noexcept; exchanges again
     * and completes axis corners. The existing caller owns final EOS/rollback.
     * Only small metadata/surface candidates are retained, never a U/X clone.
     * prepare_native borrows const state and its callback must remain pure.
     */
    template<class BCPolicy>
    inline void synchronize_domain_boundary(amr::AMRControl& control,BCPolicy& boundary,
        FluidState amr::Block::* member,std::span<const amr::BlockHandle> handles,
        GridMetrics::GeometrySemantics semantics=GridMetrics::GeometrySemantics::Existing,
        arch::state::Bounds bounds={})
    {
        if(semantics==GridMetrics::GeometrySemantics::Existing) {
            apply_domain_boundary(control,boundary,member);
            control.ghost_exchange.ExecuteExchange(control.pool,control.tree,
                control.tree->GetRootGridDim(),member,handles,
                amr::CoordinateSeamGeometry::ExistingChart,bounds);
            return;
        }
        if(semantics!=GridMetrics::GeometrySemantics::AxisymmetricRz)
            throw std::invalid_argument("Unknown Host domain boundary chart");
        using Service=std::remove_cvref_t<BCPolicy>;
        if constexpr(requires(BCPolicy& service,FluidState& state,const FluidState& input,
            const Grid& grid,typename Service::NativeCandidate& candidate) {
            {service.geometry_semantics()} -> std::same_as<GridMetrics::GeometrySemantics>;
            service.logical_plan(grid);service.snapshot_stage_context();
            {service.stage_context_matches(service.snapshot_stage_context())} -> std::convertible_to<bool>;
            service.apply_builtin(state,grid);
            {service.prepare_native(input,grid)} -> std::same_as<typename Service::NativeCandidate>;
            service.validate_native_candidate(candidate,input,grid);
            {service.publish_native_noexcept(std::move(candidate),state)} noexcept -> std::same_as<void>;
            service.complete_axis(state,grid);
        }) {
            if(member!=&amr::Block::fluid_state&&member!=&amr::Block::state_next
                &&member!=&amr::Block::state_scratch)
                throw std::invalid_argument("Native boundary selected unknown actual state member");
            const auto pool=control.pool;const auto tree=control.tree;
            if(!pool||!tree||tree->GetRootGridDim()!=2||!arch::state::valid_bounds(bounds))
                throw std::invalid_argument("Native boundary requires actual RZ domain and bounds");
            const auto& active=tree->GetActiveBlocks();
            if(active.empty()||handles.size()!=active.size()||boundary.geometry_semantics()!=semantics)
                throw std::invalid_argument("Native boundary domain/handles/chart mismatch");
            const std::vector<int> ids(active.begin(),active.end());
            const std::vector<amr::BlockHandle> saved_handles(handles.begin(),handles.end());
            const auto bc_frame=boundary.snapshot_stage_context();
            using Field=std::vector<double> FluidState::*;
            constexpr std::array<Field,7> fields{&FluidState::rho,&FluidState::mom_u,
                &FluidState::mom_v,&FluidState::mom_w,&FluidState::eng,
                &FluidState::enuc_rate,&FluidState::mass_fractions};
            struct PatchFrame {
                amr::Block* block;Grid grid;int species,id,level,parent,active_index;
                bool active;std::uint64_t morton;std::array<std::uint32_t,3> logical;
                std::array<int,8> children;std::array<amr::Block::FaceNeighbors,6> neighbours;
                std::array<const double*,7> addresses;std::array<std::size_t,7> sizes;
            };
            std::vector<PatchFrame> frames;frames.reserve(ids.size());
            for(std::size_t n=0;n<ids.size();++n) {
                auto& block=pool->GetBlock(ids[n]);const auto& state=block.*member;
                (void)GridMetrics::make_geometry_view(block.grid,semantics);
                (void)boundary.logical_plan(block.grid);
                const int extent=block.grid.GetTotalSize(),species=state.GetNumSpecies();
                if(!amr::is_valid(handles[n])||handles[n].epoch!=handles.front().epoch
                    ||extent<=0||species<0||state.block_total_size_!=extent
                    ||state.rho.size()!=static_cast<std::size_t>(extent)
                    ||state.mom_u.size()!=state.rho.size()||state.mom_v.size()!=state.rho.size()
                    ||state.mom_w.size()!=state.rho.size()||state.eng.size()!=state.rho.size()
                    ||state.enuc_rate.size()!=state.rho.size()
                    ||state.mass_fractions.size()!=static_cast<std::size_t>(species)*extent
                    ||(n&&species!=frames.front().species))
                    throw std::invalid_argument("Native boundary actual patch layout/handle mismatch");
                PatchFrame f{&block,block.grid,species,block.id,block.level,block.parent_id,
                    block.active_index,block.active,block.morton_code,
                    {block.logical_x1,block.logical_x2,block.logical_x3},{},{},{},{}};
                std::copy(std::begin(block.children_id),std::end(block.children_id),f.children.begin());
                std::copy(std::begin(block.face_neighbors),std::end(block.face_neighbors),f.neighbours.begin());
                for(std::size_t k=0;k<fields.size();++k) {
                    const auto& values=state.*fields[k];f.addresses[k]=values.data();f.sizes[k]=values.size();
                }
                frames.push_back(std::move(f));
            }
            // Metadata preflight never reads unfinished rho or changes U/X.
            (void)control.ghost_exchange.GetPlans(pool,tree,2,handles,
                amr::CoordinateSeamGeometry::RzAxisymmetric);
            std::vector<typename Service::NativeCandidate> candidates(ids.size());
            /** Compare complete geometry by exact FP64 bits, including signed zero. */
            const auto same_grid=[](const Grid& a,const Grid& b) {
                if(a.geometry!=b.geometry||a.dim!=b.dim||a.ng!=b.ng
                    ||a.nblockx1!=b.nblockx1||a.nblockx2!=b.nblockx2||a.nblockx3!=b.nblockx3
                    ||a.stride_y!=b.stride_y||a.stride_z!=b.stride_z||a.total_size!=b.total_size
                    ||a.GetTotalX()!=b.GetTotalX()||a.GetTotalY()!=b.GetTotalY()
                    ||a.GetTotalZ()!=b.GetTotalZ())return false;
                for(double Grid::* f:std::array<double Grid::*,9>{&Grid::x1_min,&Grid::x1_max,
                    &Grid::x2_min,&Grid::x2_max,&Grid::x3_min,&Grid::x3_max,&Grid::dx1,&Grid::dx2,&Grid::dx3})
                    if(std::bit_cast<std::uint64_t>(a.*f)!=std::bit_cast<std::uint64_t>(b.*f))return false;
                if(!GridMetrics::equal_identity(a.dyadic_identity,b.dyadic_identity))return false;
                return std::equal(std::begin(a.amr_coarse_fine_face),std::end(a.amr_coarse_fine_face),
                    std::begin(b.amr_coarse_fine_face));
            };
            /** Recheck borrowed domain/BC/array ownership before candidate scatter. */
            const auto require_frame=[&] {
                if(control.pool!=pool||control.tree!=tree||tree->GetActiveBlocks()!=ids
                    ||handles.size()!=saved_handles.size()
                    ||!std::equal(handles.begin(),handles.end(),saved_handles.begin())
                    ||boundary.geometry_semantics()!=semantics||!boundary.stage_context_matches(bc_frame))
                    throw std::logic_error("Native boundary domain or BC frame changed during preparation");
                for(std::size_t n=0;n<frames.size();++n) {
                    const auto& f=frames[n];const auto& block=pool->GetBlock(ids[n]);const auto& state=block.*member;
                    if(&block!=f.block||!same_grid(block.grid,f.grid)||block.id!=f.id
                        ||block.level!=f.level||block.parent_id!=f.parent||block.active_index!=f.active_index
                        ||block.active!=f.active||block.morton_code!=f.morton
                        ||block.logical_x1!=f.logical[0]||block.logical_x2!=f.logical[1]||block.logical_x3!=f.logical[2]
                        ||state.GetNumSpecies()!=f.species||state.block_total_size_!=f.grid.GetTotalSize()
                        ||!std::equal(std::begin(block.children_id),std::end(block.children_id),f.children.begin()))
                        throw std::logic_error("Native boundary actual patch topology/layout changed");
                    for(std::size_t face=0;face<f.neighbours.size();++face) {
                        const auto& a=block.face_neighbors[face];const auto& b=f.neighbours[face];
                        if(a.count!=b.count||a.level_diff!=b.level_diff
                            ||!std::equal(std::begin(a.ids),std::end(a.ids),std::begin(b.ids)))
                            throw std::logic_error("Native boundary actual neighbour topology changed");
                    }
                    for(std::size_t k=0;k<fields.size();++k) {
                        const auto& values=state.*fields[k];
                        if(values.data()!=f.addresses[k]||values.size()!=f.sizes[k])
                            throw std::logic_error("Native boundary actual field allocation changed");
                    }
                }
            };
            /** Join all patch failures before advancing the synchronization phase. */
            const auto joined=[&](const auto& operation) {
                arch::state::HostFailure failure;
#pragma omp parallel for schedule(dynamic,1)
                for(std::size_t n=0;n<frames.size();++n) {
                    try {operation(n);}catch(...) {failure.capture_current();}
                }
                failure.rethrow();
            };
            require_frame();
            joined([&](std::size_t n) {auto& b=*frames[n].block;boundary.apply_builtin(b.*member,b.grid);});
            require_frame();
            control.ghost_exchange.ExecuteExchange(pool,tree,2,member,handles,
                amr::CoordinateSeamGeometry::RzAxisymmetric,bounds);
            require_frame();
            joined([&](std::size_t n) {
                const auto& b=*frames[n].block;
                candidates[n]=boundary.prepare_native(static_cast<const FluidState&>(b.*member),b.grid);
            });
            require_frame();
            joined([&](std::size_t n) {
                const auto& b=*frames[n].block;
                // Config and overlay-frame authenticity remain the service's
                // existing frozen candidate contract, not a new config owner.
                boundary.validate_native_candidate(candidates[n],b.*member,b.grid);
            });
            require_frame();
            // Every candidate's fallible work is finished before first scatter.
            for(std::size_t n=0;n<frames.size();++n)
                boundary.publish_native_noexcept(std::move(candidates[n]),(*frames[n].block).*member);
            control.ghost_exchange.ExecuteExchange(pool,tree,2,member,handles,
                amr::CoordinateSeamGeometry::RzAxisymmetric,bounds);
            require_frame();
            joined([&](std::size_t n) {auto& b=*frames[n].block;boundary.complete_axis(b.*member,b.grid);});
            require_frame();
        } else throw std::logic_error("Native RZ boundary requires the phased candidate service");
    }

    /**
     * Check one native RZ patch before its genuine post-boundary EOS gate.
     * rho and E are V means, while m_phi=J/W; their thermal state cannot be
     * recovered by subtracting point kinetic energy from this raw vector.
     * Validate the actual complete layout and interior finite/rho/simplex
     * values through the common scalar leaf. No floor, repair, normalization,
     * thermal acceptance or publication occurs here.
     */
    inline void validate_provisional_rz_stage_state(
        const FluidState& state, const Grid& grid, const NumericsConfig& config)
    {
        (void)GridMetrics::make_geometry_view(grid,
            GridMetrics::GeometrySemantics::AxisymmetricRz);
        const int extent=grid.GetTotalSize();
        const int species=state.GetNumSpecies();
        const arch::state::Bounds bounds{
            config.sml_rho,config.min_eint,config.max_eint};
        if(!arch::state::valid_bounds(bounds)||extent<=0||species<0
            ||state.block_total_size_!=extent
            ||state.rho.size()!=static_cast<std::size_t>(extent)
            ||state.mom_u.size()!=state.rho.size()||state.mom_v.size()!=state.rho.size()
            ||state.mom_w.size()!=state.rho.size()||state.eng.size()!=state.rho.size()
            ||state.enuc_rate.size()!=state.rho.size()
            ||state.mass_fractions.size()!=static_cast<std::size_t>(species)*extent)
            throw std::invalid_argument("RZ stage precheck requires the actual complete patch layout/bounds");
        for(int k=grid.Ks();k<grid.Ke();++k)
            for(int j=grid.Js();j<grid.Je();++j)
                for(int i=grid.Is();i<grid.Ie();++i) {
                    const int cell=grid.GetIndex(i,j,k);
                    const auto status=RzThermodynamics::provisional_native_state(
                        state.get(cell),species ? state.mass_fractions.data()+cell : nullptr,
                        species,extent,bounds);
                    if(status!=arch::state::Status::valid)
                        throw std::runtime_error("Invalid provisional RZ stage state: cell="
                            +std::to_string(cell)+" status="
                            +std::to_string(static_cast<int>(status)));
                }
    }

    /** Describe a rejected composition without changing its values or tolerance.
     * Workflow: this cold error path reports finite/negative counts, min/max and
     * sum at full binary64 decimal precision. The same shared simplex rule
     * |sum(X)-1| <= 512*N*epsilon remains the acceptance authority.
     */
    inline std::string rejected_stage_composition(const FluidState& state, int cell)
    {
        const int count = state.GetNumSpecies();
        double sum = 0.0;
        double minimum = std::numeric_limits<double>::infinity();
        double maximum = -std::numeric_limits<double>::infinity();
        int nonfinite = 0, negative = 0;
        for (int species = 0; species < count; ++species) {
            const double value = state.X(species, cell);
            sum += value;
            if (!std::isfinite(value)) ++nonfinite;
            else {
                minimum = std::min(minimum, value);
                maximum = std::max(maximum, value);
                if (value < 0.0) ++negative;
            }
        }
        std::ostringstream message;
        message.precision(std::numeric_limits<double>::max_digits10);
        message << " composition_count=" << count << " nonfinite=" << nonfinite
                << " negative=" << negative << " Xi_min=" << minimum
                << " Xi_max=" << maximum << " Xi_sum=" << sum
                << " simplex_error=" << std::abs(sum - 1.0)
                << " simplex_limit=" << 512.0 * count
                    * std::numeric_limits<double>::epsilon();
        return message.str();
    }

    /** Repair Existing trace errors; native RZ only prechecks before Runtime EOS. */
    inline void accept_stage_state(FluidState& state, const Grid& grid,
                                   const NumericsConfig& config,
                                   GridMetrics::GeometrySemantics semantics =
                                       GridMetrics::GeometrySemantics::Existing)
    {
        if(semantics==GridMetrics::GeometrySemantics::AxisymmetricRz) {
            validate_provisional_rz_stage_state(state,grid,config);
            return;
        }
        if(semantics!=GridMetrics::GeometrySemantics::Existing)
            throw std::invalid_argument("Unknown stage acceptance chart");
        const auto geometry = GridMetrics::make_geometry_view(grid);
        for (int k = grid.Ks(); k < grid.Ke(); ++k)
            for (int j = grid.Js(); j < grid.Je(); ++j)
                for (int i = grid.Is(); i < grid.Ie(); ++i) {
                    const int cell = grid.GetIndex(i, j, k);
                    double* fractions = state.GetNumSpecies()
                        ? state.mass_fractions.data() + cell : nullptr;
                    const auto status = arch::state::accept_conservative_state(
                        state.get(cell), fractions, state.GetNumSpecies(),
                        grid.GetTotalSize(), config.sml_rho, config.min_eint, config.max_eint,
                        GridMetrics::CellVolume(geometry, i, j, k),
                        state.stage_repairs.view(), cell);
                    if (!arch::state::accepted(status))
                        throw std::runtime_error("Invalid accepted state: cell=" + std::to_string(cell)
                            + " status=" + std::to_string(static_cast<int>(status))
                            + (status == arch::state::Status::invalid_composition
                                ? rejected_stage_composition(state, cell) : std::string{}));
                }
    }

    /** Accept after all reflux faces; optionally retain the RKL-stage receipt. */
    inline void accept_reflux_state(amr::AMRControl& control,
                                    const NumericsConfig& config,
                                    FluidState amr::Block::* slot = &amr::Block::fluid_state,
                                    bool reset_receipt = true,
                                    GridMetrics::GeometrySemantics semantics =
                                        GridMetrics::GeometrySemantics::Existing)
    {
        if(semantics!=GridMetrics::GeometrySemantics::Existing
            &&semantics!=GridMetrics::GeometrySemantics::AxisymmetricRz)
            throw std::invalid_argument("Unknown reflux acceptance chart");
        const auto repair_semantics=semantics==GridMetrics::GeometrySemantics::AxisymmetricRz
            ? arch::state::RepairSemantics::RzVolumeAngular
            : arch::state::RepairSemantics::ExistingVolume;
        for (int id : control.tree->GetActiveBlocks()) {
            auto& block = control.pool->GetBlock(id);
            auto& state = block.*slot;
            if (reset_receipt) state.stage_repairs.reset(state.GetNumSpecies(),repair_semantics);
            try { accept_stage_state(state, block.grid, config,semantics); }
            catch (const std::exception& error) {
                throw std::runtime_error("AMR block=" + std::to_string(id) + ": " + error.what());
            }
        }
    }

    /**
     * Begin the native RZ Hydro reflux receipt after all RK stages are accepted.
     * The scheduler has rotated the final output to Current; its completed RK
     * receipt already belongs to the driver's pending stage budget. Start an
     * independent reflux row so the later Current collection cannot count that
     * final-stage correction again. This clears no accepted history and performs
     * no state repair; RKL deliberately retains its stage row through reflux.
     */
    inline void begin_rz_hydro_reflux_receipts(amr::AMRControl& control)
    {
        // Validate every row before clearing any block: an invalid measure or
        // species layout is an owner error, never a reason to erase evidence.
        for (int id : control.tree->GetActiveBlocks()) {
            const auto& state = control.pool->GetBlock(id).fluid_state;
            const int species = state.GetNumSpecies();
            if (species < 0
                || state.stage_repairs.semantics
                    != arch::state::RepairSemantics::RzVolumeAngular
                || state.stage_repairs.values.size()
                    != static_cast<std::size_t>(arch::state::RepairView::fixed_size)
                        + 2 * static_cast<std::size_t>(species))
                throw std::invalid_argument("RZ Hydro reflux receipt measure/layout mismatch");
        }
        for (int id : control.tree->GetActiveBlocks()) {
            auto& state = control.pool->GetBlock(id).fluid_state;
            state.stage_repairs.reset(state.GetNumSpecies(),
                arch::state::RepairSemantics::RzVolumeAngular);
        }
    }

    inline void validate_stage_state(const FluidState& state, const Grid& grid,
                                     const NumericsConfig& config)
    {
        for (int k = grid.Ks(); k < grid.Ke(); ++k)
            for (int j = grid.Js(); j < grid.Je(); ++j)
                for (int i = grid.Is(); i < grid.Ie(); ++i) {
                    const int cell = grid.GetIndex(i, j, k);
                    const auto status = arch::state::validate(state.get(cell),
                        state.GetNumSpecies() ? state.mass_fractions.data() + cell : nullptr,
                        state.GetNumSpecies(), grid.GetTotalSize(),
                        config.sml_rho, config.min_eint, config.max_eint);
                    if (status != arch::state::Status::valid)
                        throw std::runtime_error("Invalid accepted state: cell=" + std::to_string(cell)
                            + " status=" + std::to_string(static_cast<int>(status)));
                }
    }

    /**
     * Precheck the native RZ reflux candidate without point-state energy recovery.
     * The three Host Hydro callers select this owner only for explicit native RZ.
     * m_phi=J/W and E=E_V have different measures; only the subsequent actual
     * Current BC/exchange and Runtime EOS gate can accept their thermal closure.
     * This function performs no repair, heating, normalization or publication.
     */
    inline void validate_reflux_state(const amr::AMRControl& control,
                                      const NumericsConfig& config,
                                      FluidState amr::Block::* slot = &amr::Block::fluid_state)
    {
        for (int id : control.tree->GetActiveBlocks()) {
            const auto& block = control.pool->GetBlock(id);
            try {
                validate_provisional_rz_stage_state(block.*slot,block.grid,config);
            }
            catch (const std::exception& error) {
                throw std::runtime_error("AMR block=" + std::to_string(id) + ": " + error.what());
            }
        }
    }

    /** Borrowed geometry for one native angular cell increment. This
     * mathematical context conveys no Runtime/source/ghost authority.
     */
    struct NativeAngularDivergence {
        const GridMetrics::GeometryView* geometry = nullptr;
        int direction = 0;
        int i = 0;
        int j = 0;
    };

    /** Add V-measure fields/species and, when explicitly supplied, the
     * conditioned native J/W increment from the shared geometry leaf.
     * Validate the native angular increment before any cell/species write.
     * False leaves all outputs untouched; it never falls back to V transport.
     */
    ARCH_INLINE bool accumulate_cell_divergence(
        const FluidVector& lower_flux, const FluidVector& upper_flux,
        const double* lower_species_flux, const double* upper_species_flux,
        int n_spec, int species_stride,
        double area_l, double area_r, double volume, double dt,
        FluidVector& dU, double* d_spec,
        const NativeAngularDivergence* angular = nullptr)
    {
        double increment=0.0;
        if(angular&&(!angular->geometry
            ||!GridMetrics::Rz::AngularFluxIncrement(*angular->geometry,
                angular->direction,angular->i,angular->j,
                lower_flux.mom_w,upper_flux.mom_w,dt,increment))) return false;
        double dt_over_vol = dt / volume;
        auto lower=lower_flux,upper=upper_flux;
        // The unique RZ m_phi slot is J/W. All other fields remain V averages.
        // Do not form an unused m_phi V-divergence before replacing it.
        if(angular) {
            lower.mom_w=upper.mom_w=0.0;
            dU.mom_w += increment;
        }
        dU = dU + (lower * area_l - upper * area_r) * dt_over_vol;
        for (int s = 0; s < n_spec; ++s)
        {
            int off = s * species_stride;
            d_spec[off] += (lower_species_flux[off] * area_l - upper_species_flux[off] * area_r) * dt_over_vol;
        }
        return true;
    }

    // Helper 1: Accumulate Flux Divergence
    inline void accumulate_divergence(
        std::vector<FluidVector> &dU, std::vector<double> &d_spec,
        const std::vector<FluidVector> &fluxes, const std::vector<double> &spec_fluxes,
        const Grid &grid, double dt, int dir, int n_spec,
        GridMetrics::GeometrySemantics semantics = GridMetrics::GeometrySemantics::Existing,
        bool angular_transport = false)
    {
        const auto geometry=GridMetrics::make_geometry_view(grid,semantics);
        // Hydro opts in only after its curvature source migration. The existing
        // viscous stress/source consumer must migrate together in its own node.
        const bool torque=angular_transport
            && semantics==GridMetrics::GeometrySemantics::AxisymmetricRz;
        int stride = (dir == 0) ? 1 : ((dir == 1) ? grid.stride_y : grid.stride_z);
        int total_size = grid.GetTotalSize();

        const int ks = grid.Ks(), ke = grid.Ke();
        const int js = grid.Js(), je = grid.Je();
        const int nk = ke - ks, nj = je - js;

        arch::state::HostFailure failure;
        const auto accumulate_row = [&](int kj) {
            try {
            int k = ks + kj / nj;
            int j = js + kj % nj;
            for (int i = grid.Is(); i < grid.Ie(); ++i)
            {
                int idx = grid.GetIndex(i, j, k);

                const double volume = GridMetrics::CellVolume(geometry, i, j, k);
                const double area_l = GridMetrics::FaceArea(geometry, dir, i, j, k, false);
                const double area_r = GridMetrics::FaceArea(geometry, dir, i, j, k, true);

                const double* lower_species_flux = n_spec > 0
                    ? spec_fluxes.data() + idx : nullptr;
                const double* upper_species_flux = n_spec > 0
                    ? spec_fluxes.data() + idx + stride : nullptr;
                double* species_delta = n_spec > 0
                    ? d_spec.data() + idx : nullptr;
                const NativeAngularDivergence angular{&geometry,dir,i,j};
                if(!accumulate_cell_divergence(
                    fluxes[idx], fluxes[idx + stride],
                    lower_species_flux, upper_species_flux,
                    n_spec, total_size, area_l, area_r, volume, dt,
                    dU[idx], species_delta,torque ? &angular : nullptr))
                    throw std::runtime_error("Invalid native angular flux divergence: cell="+std::to_string(idx));
            }
            } catch (...) { failure.capture_current(); }
        };
        bool parallel_rows = nk * nj > 1;
#ifdef _OPENMP
        parallel_rows = parallel_rows && !omp_in_parallel();
#endif
        if (parallel_rows) {
#pragma omp parallel for schedule(static)
            for (int kj = 0; kj < nk * nj; ++kj)
                accumulate_row(kj);
        } else {
            for (int kj = 0; kj < nk * nj; ++kj)
                accumulate_row(kj);
        }
        failure.rethrow();
    }

    // Helper: Geometric Source Terms (cylindrical / spherical)
    // Radial pressure source: S=+p/r in cylindrical coordinates and +2p/r
    // in spherical coordinates.
    template <typename EosType>
    inline void add_geometric_sources(
        std::vector<FluidVector> &dU,
        const FluidState &state,
        const EosType &eos,
        const Grid &grid,
        double dt,
        GridMetrics::GeometrySemantics semantics = GridMetrics::GeometrySemantics::Existing)
    {
        const auto geometry = GridMetrics::make_geometry_view(grid, semantics);
        arch::state::HostFailure failure;
        if (grid.geometry == "cartesian")
            return;

        int n_spec = state.GetNumSpecies();
        const int ks = grid.Ks(), ke = grid.Ke();
        const int js = grid.Js(), je = grid.Je();
        const int nk = ke - ks, nj = je - js;

#pragma omp parallel
        {
            std::vector<double> Xi(n_spec);
#pragma omp for schedule(static)
            for (int kj = 0; kj < nk * nj; ++kj)
            {
                try {
                    int k = ks + kj / nj;
                    int j = js + kj % nj;
                    for (int i = grid.Is(); i < grid.Ie(); ++i)
                    {
                        int idx = grid.GetIndex(i, j, k);
                        state.get_species_to_buffer(idx, Xi.data());
                        if(semantics==GridMetrics::GeometrySemantics::AxisymmetricRz) {
                            if(!add_rz_integrated_geometric_source(
                                [&state](int c){return state.get(c);},
                                [&state](int k,int c){return state.X(k,c);},
                                idx,n_spec,eos,geometry,i,dt,Xi.data(),dU[idx]))
                                throw std::runtime_error("RZ geometric source has an inadmissible point/stencil");
                        } else add_geometric_source_cell(
                            state.get(idx), Xi.data(), eos, geometry, i, j, dt, dU[idx]);
                    }

                } catch (...) { failure.capture_current(); }
            }
        }

        failure.rethrow();
    }

    // Helper: Physical Source Terms (Gravity)
    inline void add_gravity_sources(
        std::vector<FluidVector> &dU,
        const FluidState &state,
        const Grid &grid,
        double dt,
        const Physical::Gravity::IGravityPolicy* gravity)
    {
        // Gravity policies own the patch loop; this call preserves the same
        // source-term contract for host and backend-specific implementations.
        if (gravity) {
            gravity->add_sources_on_patch(dU, state, grid, dt, nullptr);
        }
    }    // ---------------------------------------------------------
    /**
     * Evaluate one normalized RK component while preserving stationary states.
     *
     * Workflow: validate the finite constituents and explicit Euler trial;
     * retain Euler's current+delta order; otherwise evaluate
     * old+w_flux*((current-old)+delta). Mathematically this is
     * (1-w_flux)*old+w_flux*(current+delta), the original convex combination.
     * If a difference overflows for finite extreme constituents, use that
     * equivalent range-protected form. Nonfinite input/trial is never repaired.
     * The caller validates normalized weights once for all conserved fields.
     */
    ARCH_INLINE double normalized_stage_component(
        double old, double current, double delta, double weight_flux)
    {
        if (!std::isfinite(old) || !std::isfinite(current) || !std::isfinite(delta))
            return arch::state::invalid();
        const double trial = current + delta;
        if (!std::isfinite(trial)) return arch::state::invalid();
        if (weight_flux == 1.0) return trial;
        if (weight_flux == 0.0) return old;
        const double difference = current - old;
        const double increment = difference + delta;
        if (std::isfinite(difference) && std::isfinite(increment)) {
            const double result = old + weight_flux * increment;
            if (std::isfinite(result)) return result;
        }
        return (1.0 - weight_flux) * old + weight_flux * trial;
    }

    /**
     * Combine the normalized RK state and conserved rho*X with the same leaf.
     * The default path retains its original ordinary-state bounds and repair
     * accounting. strict_conservative selects native RZ provisional checks:
     * J/W is not an ordinary point momentum, so its raw thermal energy is not
     * recovered here. The actual post-ghost Runtime EOS gate owns acceptance.
     */
    ARCH_INLINE arch::state::Status update_stage_cell(
        const FluidVector& U_old, const FluidVector& U_curr,
        const FluidVector& delta,
        const double* Xi_old, const double* Xi_curr, const double* d_spec,
        int n_spec, int species_stride,
        double weight_n, double weight_flux,
        double sml_rho, double min_eint, double max_eint,
        FluidVector& U_new, double* Xi_new, arch::state::RepairView repairs = {},
        double cell_volume = 1.0, int cell = 0, bool strict_conservative = false,
        double angular_measure = 0.0)
    {
        // The supported scheduler supplies convex coefficients with w_n+w_f=1.
        // Reject malformed internal requests rather than silently normalizing
        // arbitrary weights or interpreting them as a different RK method.
        if (!std::isfinite(weight_n) || !std::isfinite(weight_flux)
            || weight_n < 0.0 || weight_n > 1.0
            || weight_flux < 0.0 || weight_flux > 1.0
            || weight_n + weight_flux != 1.0) {
            U_new.eng = arch::state::invalid();
            return arch::state::Status::invalid_thermodynamics;
        }
        U_new = {
            normalized_stage_component(U_old.rho, U_curr.rho, delta.rho, weight_flux),
            normalized_stage_component(U_old.mom_u, U_curr.mom_u, delta.mom_u, weight_flux),
            normalized_stage_component(U_old.mom_v, U_curr.mom_v, delta.mom_v, weight_flux),
            normalized_stage_component(U_old.mom_w, U_curr.mom_w, delta.mom_w, weight_flux),
            normalized_stage_component(U_old.eng, U_curr.eng, delta.eng, weight_flux)};

        const double raw_density = U_new.rho;
        arch::state::Repair repair{};
        if(strict_conservative)
            repair.status=RzThermodynamics::provisional_native_state(U_new,nullptr,0,1,
                {sml_rho,min_eint,max_eint});
        else
            repair=arch::state::apply_bounds(U_new,sml_rho,min_eint,max_eint);
        if (!arch::state::accepted(repair.status)) {
            U_new.eng = arch::state::invalid();
            return repair.status;
        }
        // Native candidates must expose the real species-major layout before
        // any indexed access. Existing callers retain their original contract.
        if(strict_conservative&&(n_spec<0||species_stride<=0
            ||(n_spec>0&&(!Xi_old||!Xi_curr||!d_spec||!Xi_new)))) {
            U_new.eng=arch::state::invalid();
            return arch::state::Status::invalid_composition;
        }
        double sum = 0.0;
        bool composition_repaired = false;
        for (int s = 0; s < n_spec; ++s) {
            const int off = s * species_stride;
            const double density = normalized_stage_component(
                U_old.rho * Xi_old[off], U_curr.rho * Xi_curr[off],
                d_spec[off], weight_flux);
            double fraction = density / raw_density;
            if (!std::isfinite(fraction) || fraction <
                (strict_conservative ? 0.0 : -arch::state::composition_roundoff_limit)) {
                U_new.eng = arch::state::invalid();
                return arch::state::Status::invalid_composition;
            }
            composition_repaired = composition_repaired || fraction < 0.0;
            fraction = std::max(0.0, fraction);
            Xi_new[off] = fraction;
            sum += fraction;
        }
        if (n_spec && (!std::isfinite(sum) || std::abs(sum - 1.0)
                > 512.0 * n_spec * std::numeric_limits<double>::epsilon())) {
            U_new.eng = arch::state::invalid();
            return arch::state::Status::invalid_composition;
        }
        if (composition_repaired && !arch::state::normalize_composition(Xi_new,n_spec,species_stride)) {
            U_new.eng = arch::state::invalid();
            return arch::state::Status::invalid_composition;
        }
        if(strict_conservative) {
            const auto status=RzThermodynamics::provisional_native_state(
                U_new,Xi_new,n_spec,species_stride,{sml_rho,min_eint,max_eint});
            if(status!=arch::state::Status::valid) {
                U_new.eng=arch::state::invalid();
                return status;
            }
        }
        if (repair.status == arch::state::Status::repaired || composition_repaired) {
            if(!repairs.conserved_density(repair.delta.rho,repair.delta.mom_u,
                repair.delta.mom_v,repair.delta.mom_w,repair.delta.eng,cell_volume,angular_measure)) {
                U_new.eng=arch::state::invalid();
                return arch::state::Status::nonfinite;
            }
            repairs.event(cell_volume, cell);
            for (int s = 0; s < n_spec; ++s) {
                const int off = s * species_stride;
                const double before = normalized_stage_component(
                    U_old.rho * Xi_old[off], U_curr.rho * Xi_curr[off],
                    d_spec[off], weight_flux);
                repairs.species_mass(s, cell_volume * (U_new.rho * Xi_new[off] - before));
            }
            return arch::state::Status::repaired;
        }
        return arch::state::Status::valid;
    }

    // Helper 2: Generalized Weighted RK Update
    inline void perform_stage_update(
        const FluidState &u_n, const FluidState &u_current, FluidState &u_dest,
        const std::vector<FluidVector> &dU, const std::vector<double> &d_spec,
        const Grid &grid, double weight_n, double weight_flux,
        double sml_rho, double min_eint, double max_eint,
        GridMetrics::GeometrySemantics semantics = GridMetrics::GeometrySemantics::Existing)
    {
        const auto geometry = GridMetrics::make_geometry_view(grid, semantics);
        int n_spec = u_n.GetNumSpecies();
        int total_size = grid.GetTotalSize();

        const int ks = grid.Ks();
        const int ke = grid.Ke();
        const int js = grid.Js();
        const int je = grid.Je();
        const int nk = ke - ks;
        const int nj = je - js;

        const auto repair_profile=semantics==GridMetrics::GeometrySemantics::AxisymmetricRz
            ? arch::state::RepairSemantics::RzVolumeAngular : arch::state::RepairSemantics::ExistingVolume;
        u_dest.stage_repairs.reset(n_spec,repair_profile);
        int invalid_count = 0;
        const auto update_row = [&](int kj, arch::state::RepairBudget& local,
                                    int& local_invalid) {
            int k = ks + kj / nj;
            int j = js + kj % nj;
            for (int i = grid.Is(); i < grid.Ie(); ++i)
            {
                int idx = grid.GetIndex(i, j, k);

                FluidVector U_old = u_n.get(idx);
                FluidVector U_curr = u_current.get(idx);
                FluidVector U_new;
                const double* Xi_old = n_spec > 0
                    ? u_n.mass_fractions.data() + idx : nullptr;
                const double* Xi_curr = n_spec > 0
                    ? u_current.mass_fractions.data() + idx : nullptr;
                const double* species_delta = n_spec > 0
                    ? d_spec.data() + idx : nullptr;
                double* Xi_new = n_spec > 0
                    ? u_dest.mass_fractions.data() + idx : nullptr;
                const auto status = update_stage_cell(
                    U_old, U_curr, dU[idx], Xi_old, Xi_curr, species_delta,
                    n_spec, total_size, weight_n, weight_flux,
                    sml_rho, min_eint, max_eint, U_new, Xi_new, local.view(),
                    GridMetrics::CellVolume(geometry, i, j, k), idx,
                    semantics==GridMetrics::GeometrySemantics::AxisymmetricRz,
                    semantics==GridMetrics::GeometrySemantics::AxisymmetricRz
                        ? GridMetrics::Rz::AngularMomentumMeasure(geometry,i,j) : 0.0);
                if (!arch::state::accepted(status)) ++local_invalid;
                u_dest.set(idx, U_new);
            }
        };
        bool parallel_rows = nk * nj > 1;
#ifdef _OPENMP
        parallel_rows = parallel_rows && !omp_in_parallel();
#endif
        if (parallel_rows) {
#pragma omp parallel reduction(+:invalid_count)
            {
                arch::state::RepairBudget local(n_spec,repair_profile);
#pragma omp for schedule(static)
                for (int kj = 0; kj < nk * nj; ++kj)
                    update_row(kj, local, invalid_count);
#pragma omp critical(arch_state_repairs)
                u_dest.stage_repairs.combine(local);
            }
        } else {
            arch::state::RepairBudget local(n_spec,repair_profile);
            for (int kj = 0; kj < nk * nj; ++kj)
                update_row(kj, local, invalid_count);
            u_dest.stage_repairs.combine(local);
        }
        if (invalid_count) throw std::runtime_error("Hydro candidate rejected: invalid density, composition or unresolved/internal energy");
    }

    // Helper 3: Evaluate fluxes in every active dimension.
    /**
     * Observe the final oriented hydro flux on every physical outer face.
     * Only faces that own a stage plane are touched; internal AMR joins and
     * periodic pairs keep null planes, so this writes no simulation state.
     */
    inline void capture_hydro_surface_flux(
        const FluidState& state, const Grid& grid, int dir,
        const std::vector<FluidVector>& flux_buffer,
        const std::vector<double>& spec_flux_buffer)
    {
        const auto storage = state.boundary_flux_capture;
        if (!storage) return;
        const arch::boundary::BoundaryFluxCaptureView view = storage->view();
        const int stride = (dir == 0) ? 1 : ((dir == 1) ? grid.stride_y : grid.stride_z);
        const int species = state.GetNumSpecies();
        const int total_size = grid.GetTotalSize();
        const int lower[3]{grid.Is(), grid.Js(), grid.Ks()};
        const int upper[3]{grid.Ie(), grid.Je(), grid.Ke()};
        const int tangent_a = (dir + 1) % 3, tangent_b = (dir + 2) % 3;
        for (int side = 0; side < 2; ++side) {
            if (!view.stage[2 * dir + side]) continue;
            int face[3]{lower[0], lower[1], lower[2]};
            int cell[3]{lower[0], lower[1], lower[2]};
            face[dir] = side == 0 ? lower[dir] : upper[dir];
            cell[dir] = side == 0 ? lower[dir] : upper[dir] - 1;
            for (int ib = lower[tangent_b]; ib < upper[tangent_b]; ++ib) {
                cell[tangent_b] = face[tangent_b] = ib;
                for (int ia = lower[tangent_a]; ia < upper[tangent_a]; ++ia) {
                    cell[tangent_a] = face[tangent_a] = ia;
                    const int index = grid.GetIndex(cell[0], cell[1], cell[2])
                        + (side == 1 ? stride : 0);
                    arch::boundary::CaptureBoundaryFlux(view, dir,
                        face[0], face[1], face[2],
                        lower[0], upper[0], lower[1], upper[1], lower[2], upper[2],
                        flux_buffer[index],
                        species > 0 ? spec_flux_buffer.data() + index : nullptr,
                        species, total_size, 0.0);
                }
            }
        }
    }

    /** Apply the original selected Hydro patch math under a real source receipt.
     * Workflow: preflight chart/source/input/interval/bounds before clearing
     * outputs/cache; compute each selected flux; accumulate original divergence;
     * consume same-stage self face work and paired AMR Energy registration;
     * retain geometric sources; finally apply external original body math OR
     * self momentum and complete the patch. The registry alone applies original
     * area/sign/RK weights and reflux dt. No potential history is retained here.
     */
    // FluxSchemePolicy supplies compute_fluxes for the selected reconstruction.
    template <typename FluxSchemePolicy, typename EosType>
    inline void evaluate_all_dimensions(
        amr::AMRControl* amr_ctrl, int block_id,
        const FluidState &state, const EosType &eos, const Grid &grid, double dt,
        std::vector<FluidVector> &dU, std::vector<double> &d_spec,
        std::vector<FluidVector> &flux_buffer, std::vector<double> &spec_flux_buffer,
        const Physical::Gravity::IGravityPolicy* gravity,
        double entropy_fix_coeff, double flux_weight = 1.0, bool roe_wave_speed = true,
        GridMetrics::GeometrySemantics semantics = GridMetrics::GeometrySemantics::Existing,
        arch::state::Bounds physical_bounds = {},
        const arch::boundary::HydroBoundaryView& hydro_boundary = {},
        Physical::Gravity::NativeExternalStageFrame::PatchReceipt* native_source = nullptr,
        Physical::Gravity::NativeSelfStageFrame::PatchReceipt* native_self = nullptr)
    {
        // Validate the chart and reject consumers not yet migrated before any
        // output/cache mutation. Runtime Grid still uses its existing chart.
        (void)GridMetrics::make_geometry_view(grid, semantics);
        const bool rz=semantics==GridMetrics::GeometrySemantics::AxisymmetricRz;
        if(native_source&&native_self)
            throw std::invalid_argument("Native gravity supplied both external and self receipts");
        if((native_source||native_self)&&(!rz||!gravity))
            throw std::invalid_argument("Native gravity consumer lacks its actual source receipt");
        if(rz&&gravity) {
            using Physical::Gravity::GravitySourceOrigin;
            const auto origin=gravity->source_descriptor().origin;
            if(origin!=GravitySourceOrigin::NativeExternalOrthonormal&&origin!=GravitySourceOrigin::NativeSelfComposite)
                throw std::invalid_argument("RZ Hydro gravity consumer not migrated");
            if((origin==GravitySourceOrigin::NativeExternalOrthonormal&&(!native_source||native_self))
                ||(origin==GravitySourceOrigin::NativeSelfComposite&&(!native_self||native_source)))
                throw std::invalid_argument("Native gravity consumer lacks its actual source receipt");
        }
        if(native_source)native_source->require_application(state,grid,
            GridMetrics::make_geometry_view(grid,semantics),dt,physical_bounds);
        if(native_self)native_self->require_application(state,grid,
            GridMetrics::make_geometry_view(grid,semantics),dt,physical_bounds);
        int n_spec = state.GetNumSpecies();
        std::fill(dU.begin(), dU.end(), FluidVector());
        std::fill(d_spec.begin(), d_spec.end(), 0.0);

        // Reset at every patch-stage: no result crosses RK, AMR, or restart
        // state versions. The cache only removes duplicate exact mean queries
        // for every flux policy; grouped EOS queries remain optional at the EOS leaf.
        static thread_local FluxAdmissibility::MeanThermoCache mean_cache;
        mean_cache.reset(grid.GetTotalSize());
        mean_cache.roe_wave_speed = roe_wave_speed;
        mean_cache.geometry_semantics = semantics;
        mean_cache.physical_bounds = physical_bounds;
        mean_cache.hydro_boundary = hydro_boundary;

        for (int dir = 0; dir < grid.dim; ++dir)
        {
            std::fill(flux_buffer.begin(), flux_buffer.end(), FluidVector());
            std::fill(spec_flux_buffer.begin(), spec_flux_buffer.end(), 0.0);
            FluxSchemePolicy::compute_fluxes(
                state, eos, grid, flux_buffer, spec_flux_buffer,
                dir, entropy_fix_coeff, &mean_cache);

            capture_hydro_surface_flux(state, grid, dir, flux_buffer, spec_flux_buffer);

            accumulate_divergence(dU, d_spec, flux_buffer, spec_flux_buffer, grid, dt, dir, n_spec, semantics,true);

            // Same actual stage: self work consumes this axis's immutable
            // Riemann mass flux immediately after its original divergence.
            if(native_self)native_self->add_flux_work(dU,flux_buffer,dir);
            else if (gravity&&!native_source) gravity->add_flux_work_on_patch(dU, flux_buffer, state, grid, dt, dir);

            // Flux registration has one shared face-index convention for all AMR operators.
            if (amr_ctrl && block_id >= 0) {
                amr::RegisterCoarseFineFluxes(*amr_ctrl, block_id, grid, dir,
                                               flux_buffer, spec_flux_buffer, n_spec, flux_weight, semantics,
                    semantics==GridMetrics::GeometrySemantics::AxisymmetricRz,native_self);
            }
        }

        add_geometric_sources(dU, state, eos, grid, dt, semantics);
        if(native_source)
            Physical::Gravity::add_native_external_sources(dU,flux_buffer,spec_flux_buffer,
                state,eos,grid,GridMetrics::make_geometry_view(grid,semantics),dt,physical_bounds,*native_source);
        else if(native_self) {
            // The frame owns dt and the actual prepared force. No external
            // rho*u dot g, stage weight or second compatible-work pass occurs.
            native_self->add_momentum(dU);
            native_self->commit();
        }
        else add_gravity_sources(dU, state, grid, dt, gravity);
    }
} // namespace TimeIntegration
