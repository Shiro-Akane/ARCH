/**
 * @file FluxRegister.h
 * @brief Flux register for coarse-fine boundary refluxing.
 *
 * A contiguous Host arena accumulates stage-weighted fine-minus-coarse fluxes
 * on coarse-fine faces. Reflux converts those interface integrals into cell
 * corrections using GridMetrics and AmrFluxMath. Fluid and species share face
 * indexing, with species transported and corrected as conserved rho*X.
 * Workflow:
 * 1. Read oriented hydro face fluxes and AMR level interfaces.
 * 2. Construct or apply conservative flux-register contributions.
 * 3. Return coarse-fine corrections to the stage conservation update.
 */

#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <exception>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <tuple>
#include <vector>

#include "core/MpiCollective.h"
#include "amr/flux/AmrFluxMath.h"
#include "amr/flux/AmrFluxPlan.h"
#include "amr/transfer/AmrTransferPlans.h"
#include "amr/storage/Block.h"
#include "amr/storage/MemoryPool.h"
#include "amr/topology/AmrTree.h"
#include "numerics/curvilinear/GridMetrics.h"
#if ARCH_MPI_ENABLED
#include <mpi.h>
#endif

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
    mutable std::mutex identity_mutex_;
    std::optional<std::uint64_t> topology_identity_;
    std::optional<std::uint64_t> distributed_stage_;
    bool distributed_reflux_applied_ = false;

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

    /** One synchronous Host stage's raw face values, owned by this register.
     * Workers submit distinct routes; no register/receipt writes occur until
     * the caller has joined every worker and collectively accepted the stage.
     * The topology lease and this nonmoving capture outlive all patch calls.
     */
    class HostStageCapture {
        friend class FluxRegister;
        FluxRegister& owner_;
        const AmrFluxTopologyPlan& topology_;
        double weight_;
        std::vector<std::vector<double>> values_;
        std::vector<unsigned char> received_;

      public:
        HostStageCapture(FluxRegister& owner, const AmrFluxTopologyPlan& topology, double weight)
            : owner_(owner), topology_(topology), weight_(weight), values_(topology.routes.size()),
              received_(topology.routes.size(), 0) {
            std::lock_guard lock(owner_.identity_mutex_);
            if (owner_.host_stage_capture_)
                throw std::logic_error("Host flux stage capture already active");
            owner_.host_stage_capture_ = this;
        }
        HostStageCapture(const HostStageCapture&) = delete;
        HostStageCapture(HostStageCapture&&) = delete;
        ~HostStageCapture() {
            std::lock_guard lock(owner_.identity_mutex_);
            if (owner_.host_stage_capture_ != this)
                std::terminate();
            owner_.host_stage_capture_ = nullptr;
        }
        std::span<const std::vector<double>> complete_values() const {
            if (std::any_of(received_.begin(), received_.end(),
                            [](auto value) { return value != 1; }))
                throw std::invalid_argument("Host flux stage has missing routes");
            return values_;
        }
    };
    /** Queue the original raw gather; no remote views or receipt pointers escape. */
    void CaptureHostStageValues(const AmrFluxTopologyPlan& topology,
                                const AmrFluxRegistrationRoute& route, std::vector<double> values,
                                double weight) {
        std::lock_guard lock(identity_mutex_);
        auto* stage = host_stage_capture_;
        if (!stage || &stage->topology_ != &topology || stage->weight_ != weight)
            throw std::logic_error("distributed flux values require the collective stage boundary");
        const auto* found = topology.find(route.key.source_block, axis_value(route.key.axis));
        if (found != &route || values.size() != route.plan.operations.size())
            throw std::invalid_argument("Host flux stage route/layout mismatch");
        const auto index = static_cast<std::size_t>(found - topology.routes.data());
        if (stage->received_.at(index))
            throw std::invalid_argument("Host flux stage repeats a route");
        stage->values_[index] = std::move(values);
        stage->received_[index] = 1;
    }

  private:
    HostStageCapture* host_stage_capture_ = nullptr;

  public:
    bool DistributedRefluxComplete() const noexcept { return distributed_reflux_applied_; }

    /** Owner changes may retire slots only after all captured fluxes are consumed. */
    void RequireOwnershipTransferBoundary() const {
        std::lock_guard lock(identity_mutex_);
        if (host_stage_capture_ || (distributed_stage_ && !distributed_reflux_applied_))
            throw std::logic_error("ownership transfer requires completed flux/reflux");
    }


    /** Complete Host flux owner backup; values live outside the original arena. */
    class HostSnapshot {
        friend class FluxRegister;
        int dimension_,capacity_,stride_,species_;
        std::array<int,6> cells_,offsets_;
        std::vector<FluidVector> fluid_;
        std::vector<double> composition_;
        std::vector<int> active_;
        std::optional<std::uint64_t> identity_, distributed_stage_;
        bool reflux_applied_;
        const FluidVector* fluid_address_;
        const double* composition_address_;
        const int* active_address_;
        explicit HostSnapshot(const FluxRegister& owner)
            : dimension_(owner.dim_), capacity_(owner.capacity_), stride_(owner.block_stride_),
              species_(owner.num_species_), cells_(owner.face_cells_),
              offsets_(owner.face_offsets_), fluid_(owner.fluxes_),
              composition_(owner.species_fluxes_), active_(owner.active_),
              identity_(owner.topology_identity_), distributed_stage_(owner.distributed_stage_),
              reflux_applied_(owner.distributed_reflux_applied_),
              fluid_address_(owner.fluxes_.data()),
              composition_address_(owner.species_fluxes_.data()),
              active_address_(owner.active_.data()) {}

      public:
        HostSnapshot(const HostSnapshot&)=delete;
        HostSnapshot& operator=(const HostSnapshot&)=delete;
        HostSnapshot(HostSnapshot&&)=delete;
        HostSnapshot& operator=(HostSnapshot&&)=delete;
    };
    /** Scheduler-barrier allocation before stages; no parallel registration may overlap it. */
    HostSnapshot snapshot_host() const {
        std::lock_guard lock(identity_mutex_);
        if(host_snapshot_owner_)throw std::logic_error("Host flux backup already leased");
        return HostSnapshot(*this);
    }
    /** Freeze arena extents; clearing and adding fixed-size stage fluxes remain legal. */
    void freeze_host_snapshot(const HostSnapshot& snapshot) {
        std::lock_guard lock(identity_mutex_);
        if(host_snapshot_owner_||!host_snapshot_matches(snapshot))
            throw std::logic_error("Host flux backup is stale or overlapping");
        host_snapshot_owner_=&snapshot;
    }
    /** Restore into the original fluid/species/face arrays after all Host workers joined. */
    void restore_host_snapshot_noexcept(const HostSnapshot& snapshot) noexcept {
        if(host_snapshot_owner_!=&snapshot||!snapshot_layout_matches(snapshot))std::terminate();
        std::copy(snapshot.fluid_.begin(),snapshot.fluid_.end(),fluxes_.begin());
        std::copy(snapshot.composition_.begin(),snapshot.composition_.end(),species_fluxes_.begin());
        std::copy(snapshot.active_.begin(),snapshot.active_.end(),active_.begin());
        topology_identity_=snapshot.identity_;
        distributed_stage_ = snapshot.distributed_stage_;
        distributed_reflux_applied_ = snapshot.reflux_applied_;
    }
    /** Release the exact owner; called only outside registration worker regions. */
    void release_host_snapshot(const HostSnapshot& snapshot) noexcept {
        if(host_snapshot_owner_!=&snapshot)std::terminate();
        host_snapshot_owner_=nullptr;
    }
    /** Compare metadata, original arena addresses and every FP64 payload bit. */
    bool host_snapshot_matches(const HostSnapshot& snapshot,bool require_original_arena=true) const noexcept {
        if (distributed_stage_ != snapshot.distributed_stage_ ||
            distributed_reflux_applied_ != snapshot.reflux_applied_)
            return false;
        const auto bits=[](double a,double b){return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);};
        if(!snapshot_layout_matches(snapshot,require_original_arena)||topology_identity_!=snapshot.identity_||active_!=snapshot.active_)return false;
        for(std::size_t i=0;i<fluxes_.size();++i) {
            const auto& a=fluxes_[i];const auto& b=snapshot.fluid_[i];
            if(!bits(a.rho,b.rho)||!bits(a.mom_u,b.mom_u)||!bits(a.mom_v,b.mom_v)
                ||!bits(a.mom_w,b.mom_w)||!bits(a.eng,b.eng))return false;
        }
        for(std::size_t i=0;i<species_fluxes_.size();++i)
            if(!bits(species_fluxes_[i],snapshot.composition_[i]))return false;
        return true;
    }
private:
    const HostSnapshot* host_snapshot_owner_=nullptr;
    /** Layout checks never allocate, resize or change the arena owner. */
    bool snapshot_layout_matches(const HostSnapshot& s,bool require_original_arena=true) const noexcept {
        return dim_==s.dimension_&&capacity_==s.capacity_&&block_stride_==s.stride_
            &&num_species_==s.species_&&face_cells_==s.cells_&&face_offsets_==s.offsets_
            &&fluxes_.size()==s.fluid_.size()&&species_fluxes_.size()==s.composition_.size()
            &&active_.size()==s.active_.size()&&(!require_original_arena||(fluxes_.data()==s.fluid_address_
            &&species_fluxes_.data()==s.composition_address_&&active_.data()==s.active_address_));
    }
public:

    void Resize(int max_blocks, int dim) {
        if (host_stage_capture_)
            throw std::logic_error("Host flux capture excludes arena mutation");
        if (max_blocks < 0) {
            throw std::invalid_argument("FluxRegister capacity cannot be negative.");
        }
        if (dim_ == dim && capacity_ >= max_blocks) return;

        if(host_snapshot_owner_)throw std::logic_error("Host transaction excludes flux arena resize");
        ConfigureLayout(dim);
        dim_ = dim;
        capacity_ = max_blocks;
        fluxes_.assign(static_cast<size_t>(capacity_) * block_stride_, FluidVector{});
        active_.assign(static_cast<size_t>(capacity_) * 6, 0);
        species_fluxes_.assign(static_cast<size_t>(capacity_) * block_stride_ * num_species_, 0.0);
        std::lock_guard lock(identity_mutex_);
        topology_identity_.reset();
        distributed_stage_.reset();
        distributed_reflux_applied_ = false;
    }

    void EnsureSpecies(int num_species) {
        if (host_stage_capture_)
            throw std::logic_error("Host flux capture excludes arena mutation");
        if (num_species < 0) {
            throw std::invalid_argument("FluxRegister species count cannot be negative.");
        }
        if (num_species_ == num_species) return;
        if (distributed_stage_)
            throw std::logic_error("MPI flux species resize requires Clear");
        if(host_snapshot_owner_)throw std::logic_error("Host transaction excludes flux species resize");
        num_species_ = num_species;
        species_fluxes_.assign(static_cast<size_t>(capacity_) * block_stride_ * num_species_, 0.0);
    }

    int GetNumSpecies() const { return num_species_; }

    void ValidateTopologyIdentity(std::uint64_t fingerprint) const {
        std::lock_guard lock(identity_mutex_);
        if (topology_identity_ && *topology_identity_ != fingerprint)
            throw std::invalid_argument("AMR accumulated flux topology identity mismatch; Clear required");
    }

    void Clear() {
        if (host_stage_capture_)
            throw std::logic_error("Host flux capture excludes arena mutation");
        // Clear/resize are scheduler barriers, never concurrent registration.
        std::lock_guard lock(identity_mutex_);
        topology_identity_.reset();
        distributed_stage_.reset();
        distributed_reflux_applied_ = false;
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

    /** Accumulate an already-oriented (fine - coarse) contribution. */
    void AddRegisteredFlux(int block_id, int face_dir, int cell_idx,
                           const FluidVector& flux, double coefficient) {
        FluidVector& target = FluxAt(block_id, face_dir, cell_idx);
#pragma omp atomic update
        target.rho += flux.rho * coefficient;
#pragma omp atomic update
        target.mom_u += flux.mom_u * coefficient;
#pragma omp atomic update
        target.mom_v += flux.mom_v * coefficient;
#pragma omp atomic update
        target.mom_w += flux.mom_w * coefficient;
#pragma omp atomic update
        target.eng += flux.eng * coefficient;
#pragma omp atomic write
        active_[FaceSlot(block_id, face_dir)] = 1;
    }

    void AddRegisteredSpeciesFlux(int block_id, int face_dir, int cell_idx,
                                  int species, double flux,
                                  double coefficient) {
#pragma omp atomic update
        species_fluxes_[SpeciesFluxSlot(
            block_id, face_dir, cell_idx, species)] += flux * coefficient;
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
        const std::map<AmrEndpoint, int>& pool_lowering,
        double stage_weight = 1.0,
        std::optional<std::uint64_t> topology_fingerprint = std::nullopt)
    {
        if (host_stage_capture_)
            throw std::logic_error("Host flux capture excludes immediate registration");
        if (distributed_stage_)
            throw std::logic_error(
                "collective flux stage requires Clear before serial registration");
        const auto compiled =
            PrepareRegistrationPlan(plan, values, pool_lowering, stage_weight, false);
        // Zero-weight stages are a mathematical no-op and must not create a
        // false HasData witness in the Host register.
        if (stage_weight == 0.0) return;
        if (topology_fingerprint) {
            std::lock_guard lock(identity_mutex_);
            if (topology_identity_ && *topology_identity_ != *topology_fingerprint)
                throw std::invalid_argument("AMR accumulated flux topology identity mismatch; Clear required");
            topology_identity_ = *topology_fingerprint;
        }
        CommitRegistration(compiled);
    }
    /** Collective stage boundary; caller gathers every local route before entry. */
    void ApplyDistributedRegistrationStage(const MemoryPool& pool, const AmrTree& tree,
                                           std::span<const BlockHandle> handles,
                                           const AmrFluxTopologyPlan& topology,
                                           std::span<const std::vector<double>> values,
                                           std::uint64_t stage, double stage_weight);
    void ExecuteDistributedReflux(const std::shared_ptr<MemoryPool>& pool, const AmrTree& tree,
                                  std::span<const BlockHandle> handles, double dt,
                                  FluidState Block::* slot, std::uint64_t completed_stages);

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
                        GridMetrics::make_geometry_view(block.grid),
                        axis_value(axis), i, j, k,
                        side == AmrSide::Upper);
                    const double volume = GridMetrics::CellVolume(
                        GridMetrics::make_geometry_view(block.grid), i, j, k);
                    if (!amr_plan_detail::is_finite_binary64(area)
                        || !amr_plan_detail::is_finite_binary64(volume)
                        || area <= 0.0 || volume <= 0.0)
                        throw std::invalid_argument(
                            "reflux metric is nonpositive or nonfinite");
                    LogicalAmrBox box{
                        {logical[0], logical[1], logical[2]}, {1, 1, 1}};
                    const double weight = dt * area / volume;
                    double moment_weight = weight;
                    if (dim == 2 && block.grid.geometry == "cylindrical") {
                        const double moment =
                            GridMetrics::AngularMomentumFaceMoment(
                                GridMetrics::make_geometry_view(block.grid),
                                axis_value(axis), i, j, k,
                                side == AmrSide::Upper);
                        const double volume_moment =
                            GridMetrics::AngularMomentumVolumeMoment(
                                GridMetrics::make_geometry_view(block.grid),
                                i, j, k);
                        if (!amr_plan_detail::is_finite_binary64(moment)
                            || !amr_plan_detail::is_finite_binary64(volume_moment)
                            || moment <= 0.0 || volume_moment <= 0.0)
                            throw std::invalid_argument(
                                "reflux azimuthal moment is nonpositive or nonfinite");
                        moment_weight = dt * moment / volume_moment;
                    }
                    const double sign = side == AmrSide::Lower ? 1.0 : -1.0;
                    const auto append = [&](AmrField field, int component,
                                            double field_weight) {
                        plan.operations.push_back({
                            0, endpoint, endpoint, box, box, axis, side,
                            field, component,
                            RefinementRule::RefluxCorrection,
                            field_weight, sign});
                    };
                    append(AmrField::Rho, -1, weight);
                    append(AmrField::MomU, -1, weight);
                    append(AmrField::MomV, -1, weight);
                    append(AmrField::MomW, -1, moment_weight);
                    append(AmrField::Energy, -1, weight);
                    for (int component = 0; component < species; ++component)
                        append(AmrField::Species, component, weight);
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
        FluidState Block::* state_ptr,
        double timestep_scale = 1.0)
    {
        if (distributed_stage_)
            throw std::logic_error("MPI flux reflux requires the collective boundary");
        const auto groups =
            PrepareRefluxPlan(plan, pool, active_blocks, handles, state_ptr, timestep_scale);
        CommitReflux(groups, pool, plan.dimension, state_ptr, timestep_scale);
    }

  private:
    struct CompiledContribution {
        int block_id = -1;
        int face = -1;
        int cell = -1;
        AmrField field = AmrField::Rho;
        int component = -1;
        RefinementRule rule = RefinementRule::FineFluxContribution;
        double coefficient = 0.0;
        double value = 0.0;
    };

    std::vector<CompiledContribution>
    PrepareRegistrationPlan(const FluxRegistrationPlan& plan, std::span<const double> values,
                            const std::map<AmrEndpoint, int>& pool_lowering, double stage_weight,
                            bool allow_remote_source) const {
        validate_amr_plan(plan);
        if (values.size() != plan.operations.size())
            throw std::invalid_argument("flux registration values do not match logical plan");
        if (!amr_plan_detail::is_finite_binary64(stage_weight))
            throw std::invalid_argument("flux registration stage weight is nonfinite");

        std::vector<CompiledContribution> compiled;
        compiled.reserve(plan.operations.size());
        for (std::size_t index = 0; index < plan.operations.size(); ++index) {
            const auto& operation = plan.operations[index];
            const auto source = pool_lowering.find(operation.source);
            const auto destination = pool_lowering.find(operation.destination);
            if ((!allow_remote_source && source == pool_lowering.end()) ||
                destination == pool_lowering.end())
                throw std::invalid_argument("flux registration endpoint is not active");
            const int face = 2 * axis_value(operation.axis) + side_value(operation.side);
            const int cell = face_cell_index(operation.destination_box, operation.axis);
            if (destination->second < 0 || destination->second >= capacity_ || face < 0 ||
                face >= 2 * plan.dimension || cell < 0 || cell >= face_cells_[face])
                throw std::out_of_range("flux registration destination is outside storage");
            if (!flux_math::is_flux_registration_rule(operation.rule) ||
                operation.sign != flux_math::registration_route_sign(operation.rule))
                throw std::invalid_argument("flux registration sign does not match its route");
            if (operation.field == AmrField::EnucRate)
                throw std::invalid_argument("ENUC is not a conservative face flux");
            if (operation.field == AmrField::Species && operation.component >= num_species_)
                throw std::invalid_argument("flux registration species is outside storage");
            if (!amr_plan_detail::is_finite_binary64(values[index]))
                throw std::invalid_argument("flux registration value is nonfinite");
            compiled.push_back({destination->second, face, cell, operation.field,
                                operation.component, operation.rule,
                                flux_math::registration_coefficient(operation.rule,
                                                                    operation.weight, stage_weight),
                                values[index]});
        }

        return compiled;
    }
    void CommitRegistration(std::span<const CompiledContribution> compiled) {
        for (const auto& contribution : compiled) {
            if (contribution.field == AmrField::Species) {
                AddRegisteredSpeciesFlux(contribution.block_id, contribution.face,
                                         contribution.cell, contribution.component,
                                         contribution.value, contribution.coefficient);
                continue;
            }
            FluidVector flux{};
            switch (contribution.field) {
            case AmrField::Rho:
                flux.rho = contribution.value;
                break;
            case AmrField::MomU:
                flux.mom_u = contribution.value;
                break;
            case AmrField::MomV:
                flux.mom_v = contribution.value;
                break;
            case AmrField::MomW:
                flux.mom_w = contribution.value;
                break;
            case AmrField::Energy:
                flux.eng = contribution.value;
                break;
            case AmrField::EnucRate:
            case AmrField::Species:
                throw std::logic_error("invalid compiled conservative flux field");
            }
            AddRegisteredFlux(contribution.block_id, contribution.face, contribution.cell, flux,
                              contribution.coefficient);
        }
    }
        struct CompiledGroup {
            int block_id = -1;
            int face = -1;
            int cell = -1;
            double weight = 0.0;
            double moment_weight = 0.0;
            bool has_weight = false;
            bool has_moment_weight = false;
            double sign = 1.0;
            std::array<bool, 5> fluid_fields{};
            std::vector<bool> species_fields;
        };

        std::vector<CompiledGroup>
        PrepareRefluxPlan(const RefluxPlan& plan, const std::shared_ptr<MemoryPool>& pool,
                          std::span<const int> active_blocks, std::span<const BlockHandle> handles,
                          FluidState Block::* state_ptr, double timestep_scale) const {
            validate_amr_plan(plan);
            if (!pool || !state_ptr || plan.dimension != dim_)
                throw std::invalid_argument("invalid reflux Host storage");
            if (handles.size() != active_blocks.size())
                throw std::invalid_argument("reflux Host view count mismatch");
            if (!amr_plan_detail::is_finite_binary64(timestep_scale) || timestep_scale < 0.0)
                throw std::invalid_argument("invalid reflux timestep scale");
            std::map<LogicalBlockKey, std::size_t> active_index;
            for (std::size_t index = 0; index < active_blocks.size(); ++index) {
                if (active_blocks[index] < 0 || active_blocks[index] >= capacity_)
                    throw std::out_of_range("reflux block outside register storage");
                const Block& block = pool->GetBlock(active_blocks[index]);
                const auto& state = block.*state_ptr;
                const auto cells = static_cast<std::size_t>(block.grid.GetTotalSize());
                if (state.GetNumSpecies() != num_species_ ||
                    state.block_total_size_ != static_cast<int>(cells) ||
                    state.rho.size() != cells || state.mom_u.size() != cells ||
                    state.mom_v.size() != cells || state.mom_w.size() != cells ||
                    state.eng.size() != cells || state.enuc_rate.size() != cells ||
                    (num_species_ &&
                     cells > std::numeric_limits<std::size_t>::max() / num_species_) ||
                    state.mass_fractions.size() != cells * num_species_)
                    throw std::invalid_argument("reflux Host field layout mismatch");
                if (!active_index.emplace(logical_key(block, plan.dimension), index).second ||
                    handles[index].epoch != plan.scope.from_epoch)
                    throw std::invalid_argument("invalid reflux Host endpoint");
            }

            using GroupKey = std::tuple<AmrEndpoint, LogicalAmrBox, AmrAxis, AmrSide>;
            std::map<GroupKey, CompiledGroup> grouped;
            for (const auto& operation : plan.operations) {
                const auto found = active_index.find(operation.destination.logical);
                if (found == active_index.end() ||
                    handles[found->second] != operation.destination.handle ||
                    operation.source != operation.destination ||
                    operation.rule != RefinementRule::RefluxCorrection)
                    throw std::invalid_argument("stale reflux logical endpoint");
                Block& block = pool->GetBlock(active_blocks[found->second]);
                FluidState& state = block.*state_ptr;
                const int species = state.GetNumSpecies();
                if (species != num_species_)
                    throw std::invalid_argument("reflux species count mismatch");
                const int face = 2 * axis_value(operation.axis) + side_value(operation.side);
                const int cell = face_cell_index(operation.destination_box, operation.axis);
                if (cell < 0 || cell >= face_cells_[face])
                    throw std::invalid_argument("reflux logical cell is outside register storage");

                const GroupKey key{operation.destination, operation.destination_box, operation.axis,
                                   operation.side};
                auto [entry, inserted] = grouped.try_emplace(key);
                CompiledGroup& group = entry->second;
                if (inserted) {
                    group.block_id = active_blocks[found->second];
                    group.face = face;
                    group.cell = cell;
                    group.sign = operation.sign;
                    group.species_fields.assign(static_cast<std::size_t>(species), false);
                } else if (group.block_id != active_blocks[found->second] || group.face != face ||
                           group.cell != cell ||
                           std::bit_cast<std::uint64_t>(group.sign) !=
                               std::bit_cast<std::uint64_t>(operation.sign)) {
                    throw std::invalid_argument("reflux logical group metadata is inconsistent");
                }

                if (operation.field == AmrField::MomW) {
                    if (!group.has_moment_weight) {
                        group.moment_weight = operation.weight;
                        group.has_moment_weight = true;
                    } else if (std::bit_cast<std::uint64_t>(group.moment_weight) !=
                               std::bit_cast<std::uint64_t>(operation.weight)) {
                        throw std::invalid_argument("reflux moment weight is inconsistent");
                    }
                } else {
                    if (!group.has_weight) {
                        group.weight = operation.weight;
                        group.has_weight = true;
                    } else if (std::bit_cast<std::uint64_t>(group.weight) !=
                               std::bit_cast<std::uint64_t>(operation.weight)) {
                        throw std::invalid_argument(
                            "reflux logical group metadata is inconsistent");
                    }
                }

                if (operation.field == AmrField::Species) {
                    if (operation.component < 0 || operation.component >= species ||
                        group.species_fields[static_cast<std::size_t>(operation.component)])
                        throw std::invalid_argument("reflux species field is duplicate or invalid");
                    group.species_fields[static_cast<std::size_t>(operation.component)] = true;
                } else {
                    const int field = static_cast<int>(operation.field);
                    if (field < static_cast<int>(AmrField::Rho) ||
                        field > static_cast<int>(AmrField::Energy) ||
                        group.fluid_fields[static_cast<std::size_t>(field)])
                        throw std::invalid_argument("reflux fluid field is duplicate or invalid");
                    group.fluid_fields[static_cast<std::size_t>(field)] = true;
                }
            }

            std::vector<CompiledGroup> groups;
            groups.reserve(grouped.size());
            for (auto& [key, group] : grouped) {
                (void)key;
                if (!std::all_of(group.fluid_fields.begin(), group.fluid_fields.end(),
                                 [](bool present) { return present; }) ||
                    !std::all_of(group.species_fields.begin(), group.species_fields.end(),
                                 [](bool present) { return present; }) ||
                    !group.has_weight || !group.has_moment_weight)
                    throw std::invalid_argument("reflux field group is incomplete");
                groups.push_back(std::move(group));
            }

            return groups;
        }
        void CommitReflux(std::span<const CompiledGroup> groups,
                          const std::shared_ptr<MemoryPool>& pool, int dimension,
                          FluidState Block::* state_ptr, double timestep_scale) {
            for (const auto& group : groups) {
                Block& block = pool->GetBlock(group.block_id);
                FluidState& state = block.*state_ptr;
                const AmrAxis axis = static_cast<AmrAxis>(group.face / 2);
                const AmrSide side = static_cast<AmrSide>(group.face % 2);
                const auto logical = face_cell_coordinates(group.cell, axis, side, dimension);
                const int index =
                    block.grid.GetIndex(block.grid.Is() + logical[0], block.grid.Js() + logical[1],
                                        block.grid.Ks() + logical[2]);
                const FluidVector delta = GetSummedFlux(group.block_id, group.face, group.cell);
                const double correction = timestep_scale * group.sign * group.weight;
                const double moment_correction = timestep_scale * group.sign * group.moment_weight;
                const double rho_before = state.rho[index];
                state.rho[index] = flux_math::reflux_conserved(rho_before, correction, delta.rho);
                state.mom_u[index] =
                    flux_math::reflux_conserved(state.mom_u[index], correction, delta.mom_u);
                state.mom_v[index] =
                    flux_math::reflux_conserved(state.mom_v[index], correction, delta.mom_v);
                state.mom_w[index] =
                    flux_math::reflux_conserved(state.mom_w[index], moment_correction, delta.mom_w);
                state.eng[index] =
                    flux_math::reflux_conserved(state.eng[index], correction, delta.eng);
                for (int species = 0; species < state.GetNumSpecies(); ++species) {
                    state.X(species, index) = flux_math::reflux_mass_fraction(
                        rho_before, state.X(species, index), correction,
                        GetSummedSpeciesFlux(group.block_id, group.face, group.cell, species),
                        state.rho[index]);
                }
            }
        }

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

#if ARCH_MPI_ENABLED
namespace flux_mpi_detail {
using arch::mpi::check;
using arch::mpi::require_world;
using arch::mpi::prepare;
inline std::array<std::uint64_t, 21> signature(const Grid& root, int species, TopologyEpoch epoch,
                                               std::uint64_t kind, std::uint64_t stage,
                                               double weight, std::uint64_t slot) {
    return {1,
            kind,
            static_cast<std::uint64_t>(root.dim),
            static_cast<std::uint64_t>(species),
            epoch.value,
            stage,
            std::bit_cast<std::uint64_t>(weight),
            slot,
            BLOCK_NX,
            BLOCK_NY,
            BLOCK_NZ,
            MAX_NG,
            static_cast<std::uint64_t>(root.nblockx1),
            static_cast<std::uint64_t>(root.nblockx2),
            static_cast<std::uint64_t>(root.nblockx3),
            std::bit_cast<std::uint64_t>(root.x1_min),
            std::bit_cast<std::uint64_t>(root.x1_max),
            std::bit_cast<std::uint64_t>(root.x2_min),
            std::bit_cast<std::uint64_t>(root.x2_max),
            std::bit_cast<std::uint64_t>(root.x3_min),
            std::bit_cast<std::uint64_t>(root.x3_max)};
}
inline void agree(const std::array<std::uint64_t, 21>& identity) {
    std::array<std::uint64_t, 21> low{}, high{};
    check(MPI_Allreduce(identity.data(), low.data(), 21, MPI_UINT64_T, MPI_MIN, MPI_COMM_WORLD));
    check(MPI_Allreduce(identity.data(), high.data(), 21, MPI_UINT64_T, MPI_MAX, MPI_COMM_WORLD));
    if (low != high)
        throw std::invalid_argument("MPI flux stage/epoch/layout/weight/slot mismatch");
}
/** One recipe header plus raw values, checked before any int conversion. */
inline int packet_count(std::size_t values, int previous) {
    constexpr int limit = std::numeric_limits<int>::max();
    if (previous < 0 || previous > limit - 2 ||
        values > static_cast<std::size_t>(limit - 2 - previous))
        throw std::overflow_error("MPI flux packet exceeds INT_MAX");
    return previous + static_cast<int>(values) + 2;
}
inline int displacements(const std::vector<int>& counts, std::vector<int>& offsets) {
    offsets.resize(counts.size());
    int total = 0;
    for (std::size_t i = 0; i < counts.size(); ++i) {
        if (counts[i] < 0 || counts[i] > std::numeric_limits<int>::max() - total)
            throw std::overflow_error("MPI flux count exceeds INT_MAX");
        offsets[i] = total;
        total += counts[i];
    }
    return total;
}
} // namespace flux_mpi_detail
#endif

inline void FluxRegister::ApplyDistributedRegistrationStage(
    const MemoryPool& pool, const AmrTree& tree, std::span<const BlockHandle> handles,
    const AmrFluxTopologyPlan& topology, std::span<const std::vector<double>> values,
    std::uint64_t stage, double stage_weight) {
#if ARCH_MPI_ENABLED
    using namespace flux_mpi_detail;
    const auto [rank, size] = require_world();
    std::array<std::uint64_t, 21> identity{};
    struct Route {
        AmrEndpoint source;
        AmrAxis axis;
        std::vector<CompiledContribution> contributions;
    };
    std::vector<Route> ready;
    std::vector<std::vector<std::uint64_t>> packets;
    std::vector<std::uint64_t> send, receive;
    std::vector<int> send_counts, receive_counts, send_offsets, receive_offsets;
    prepare([&] {
        if (size < 2 || tree.PartitionRank() != rank || tree.PartitionSize() != size ||
            topology.partition_rank != rank || topology.partition_size != size)
            throw std::invalid_argument("MPI flux communicator/topology mismatch");
        if (!amr_plan_detail::is_finite_binary64(stage_weight) ||
            stage == std::numeric_limits<std::uint64_t>::max() || distributed_reflux_applied_ ||
            (distributed_stage_ ? stage != *distributed_stage_ + 1 : stage != 0) ||
            (!distributed_stage_ && topology_identity_))
            throw std::invalid_argument("MPI flux stage sequence invalid; Clear required");
        validate_amr_flux_topology_plan(topology);
        const auto current = build_amr_flux_topology_plan(pool, tree.GetActiveBlocks(), handles,
                                                          tree.GetRootGridDim(), num_species_,
                                                          &tree.GetRootGrid(), rank, size);
        if (current.fingerprint != topology.fingerprint || topology.dimension != dim_)
            throw std::invalid_argument("MPI flux topology is stale");
        ValidateTopologyIdentity(topology.fingerprint);
        if (values.size() != topology.routes.size())
            throw std::invalid_argument("MPI flux stage has missing/extra local routes");
        identity =
            signature(tree.GetRootGrid(), num_species_, topology.epoch, 1, stage, stage_weight, 0);
        packets.resize(size);
        send_counts.resize(size);
        receive_counts.resize(size);
        for (std::size_t index = 0; index < topology.routes.size(); ++index) {
            const auto& route = topology.routes[index];
            if (values[index].size() != route.plan.operations.size() ||
                !topology.pool_lowering.contains(route.source) ||
                topology.owners.at(route.source) != rank)
                throw std::invalid_argument("MPI flux route values/source mismatch");
            std::map<int, std::pair<FluxRegistrationPlan, std::vector<double>>> destinations;
            for (std::size_t i = 0; i < values[index].size(); ++i) {
                const auto& operation = route.plan.operations[i];
                if (!amr_plan_detail::is_finite_binary64(values[index][i]))
                    throw std::invalid_argument("MPI flux value is nonfinite");
                const int peer = topology.owners.at(operation.destination);
                if (peer < 0 || peer >= size)
                    throw std::invalid_argument("MPI flux destination owner invalid");
                auto& [plan, payload] = destinations[peer];
                plan.dimension = topology.dimension;
                plan.scope = route.plan.scope;
                plan.operations.push_back(operation);
                payload.push_back(values[index][i]);
            }
            for (auto& [peer, batch] : destinations) {
                auto& [plan, payload] = batch;
                // Filtering a canonical route keeps relative operation order.
                finalize_amr_plan(plan);
                if (peer == rank)
                    ready.push_back({route.source, route.key.axis,
                                     PrepareRegistrationPlan(plan, payload, topology.pool_lowering,
                                                             stage_weight, false)});
                else {
                    auto& packet = packets[peer];
                    (void)packet_count(payload.size(), static_cast<int>(packet.size()));
                    packet.push_back(plan.fingerprint);
                    packet.push_back(payload.size());
                    for (double value : payload)
                        packet.push_back(std::bit_cast<std::uint64_t>(value));
                }
            }
        }
        for (int peer = 0; peer < size; ++peer)
            send_counts[peer] = static_cast<int>(packets[peer].size());
        send.reserve(displacements(send_counts, send_offsets));
        for (const auto& packet : packets)
            send.insert(send.end(), packet.begin(), packet.end());
    });
    agree(identity);
    check(MPI_Alltoall(send_counts.data(), 1, MPI_INT, receive_counts.data(), 1, MPI_INT,
                       MPI_COMM_WORLD));
    prepare([&] {
        std::vector<int> expected(size, 0);
        for (const auto& route : topology.incoming) {
            const int peer = topology.owners.at(route.source);
            const auto count = route.plan.operations.size();
            expected[peer] = packet_count(count, expected[peer]);
        }
        if (receive_counts != expected)
            throw std::invalid_argument("MPI flux missing/duplicate face coverage");
        receive.resize(displacements(receive_counts, receive_offsets));
    });
    check(MPI_Alltoallv(send.data(), send_counts.data(), send_offsets.data(), MPI_UINT64_T,
                        receive.data(), receive_counts.data(), receive_offsets.data(), MPI_UINT64_T,
                        MPI_COMM_WORLD));
    prepare([&] {
        std::vector<std::size_t> cursor(receive_offsets.begin(), receive_offsets.end());
        for (const auto& route : topology.incoming) {
            const int peer = topology.owners.at(route.source);
            const std::size_t end =
                static_cast<std::size_t>(receive_offsets[peer]) + receive_counts[peer];
            auto& first = cursor[peer];
            if (first > end || end - first < 2 || receive[first++] != route.plan.fingerprint ||
                receive[first++] != route.plan.operations.size() ||
                end - first < route.plan.operations.size())
                throw std::invalid_argument("MPI flux face recipe/extent mismatch");
            std::vector<double> payload;
            payload.reserve(route.plan.operations.size());
            for (std::size_t i = 0; i < route.plan.operations.size(); ++i)
                payload.push_back(std::bit_cast<double>(receive[first++]));
            ready.push_back({route.source, route.axis,
                             PrepareRegistrationPlan(route.plan, payload, topology.pool_lowering,
                                                     stage_weight, true)});
        }
        for (int peer = 0; peer < size; ++peer)
            if (cursor[peer] !=
                static_cast<std::size_t>(receive_offsets[peer]) + receive_counts[peer])
                throw std::invalid_argument("MPI flux trailing/duplicate face payload");
        std::sort(ready.begin(), ready.end(), [](const auto& a, const auto& b) {
            return std::tie(a.source, a.axis) < std::tie(b.source, b.axis);
        });
        for (std::size_t i = 1; i < ready.size(); ++i)
            if (ready[i - 1].source == ready[i].source && ready[i - 1].axis == ready[i].axis)
                throw std::invalid_argument("MPI flux repeated source route");
    });
    // Every fallible operation has completed on every rank. No allocation,
    // topology scan or floating-point reduction changes the native order here.
    topology_identity_ = topology.fingerprint;
    if (stage_weight != 0.)
        for (const auto& route : ready)
            CommitRegistration(route.contributions);
    distributed_stage_ = stage;
#else
    (void)pool;
    (void)tree;
    (void)handles;
    (void)topology;
    (void)values;
    (void)stage;
    (void)stage_weight;
    throw std::logic_error("distributed flux registration requires ARCH_ENABLE_MPI=ON");
#endif
}

inline void FluxRegister::ExecuteDistributedReflux(const std::shared_ptr<MemoryPool>& pool,
                                                   const AmrTree& tree,
                                                   std::span<const BlockHandle> handles, double dt,
                                                   FluidState Block::* slot,
                                                   std::uint64_t completed_stages) {
#if ARCH_MPI_ENABLED
    using namespace flux_mpi_detail;
    const auto [rank, size] = require_world();
    std::array<std::uint64_t, 21> identity{};
    std::vector<CompiledGroup> groups;
    prepare([&] {
        if (!pool || size < 2 || tree.PartitionRank() != rank || tree.PartitionSize() != size ||
            !distributed_stage_ || completed_stages != *distributed_stage_ + 1 ||
            distributed_reflux_applied_)
            throw std::invalid_argument("MPI reflux requires all stages exactly once");
        const int state_slot = slot == &Block::fluid_state     ? 0
                               : slot == &Block::state_next    ? 1
                               : slot == &Block::state_scratch ? 2
                                                               : -1;
        if (state_slot < 0)
            throw std::invalid_argument("MPI reflux invalid state slot");
        const auto topology = build_amr_flux_topology_plan(*pool, tree.GetActiveBlocks(), handles,
                                                           tree.GetRootGridDim(), num_species_,
                                                           &tree.GetRootGrid(), rank, size);
        ValidateTopologyIdentity(topology.fingerprint);
        const auto plan = build_amr_reflux_topology_plan(*pool, topology);
        groups = PrepareRefluxPlan(plan, pool, tree.GetActiveBlocks(), handles, slot, dt);
        identity = signature(tree.GetRootGrid(), num_species_, topology.epoch, 2, completed_stages,
                             dt, state_slot);
    });
    agree(identity);
    CommitReflux(groups, pool, tree.GetRootGridDim(), slot, dt);
    distributed_reflux_applied_ = true;
#else
    (void)pool;
    (void)tree;
    (void)handles;
    (void)dt;
    (void)slot;
    (void)completed_stages;
    throw std::logic_error("distributed reflux requires ARCH_ENABLE_MPI=ON");
#endif
}

} // namespace amr
