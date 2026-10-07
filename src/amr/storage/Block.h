/**
 * @file Block.h
 * @brief Host block record combining logical identity, geometry, and fluid storage.
 *
 * Pool indices locate storage, while level and logical coordinates identify
 * the block in the hierarchy. Transfer helpers use the shared prolongation
 * and conservative restriction mathematics rather than defining another
 * interpolation policy inside the storage record.
 * Workflow:
 * 1. Receive topology-owned block IDs and capacity bounds.
 * 2. Manage native block or pool storage and its lifetime.
 * 3. Expose storage only through checked active handles.
 */

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

#include "amr/transfer/ConservativeRestriction.h"
#include "amr/topology/Morton.h"
#include "amr/transfer/NativeRzRegridTransfer.h"
#include "amr/transfer/RegridTransferMath.h"

#include "data/FluidState.h"
#include "grid/Grid.h"
#include "grid/GridMetrics.h"

namespace amr {

/**
 * @brief Represents a single AMR block with its local fluid state.
 */
struct Block {
    int id;                 ///< Reusable storage index in the memory pool
    uint64_t morton_code;   ///< 64-bit Morton code
    int level;              ///< Refinement level (0 is root)
    int active_index = -1;  ///< Index in AmrTree active_blocks array

    // Discrete Coordinates at this level
    uint32_t logical_x1;
    uint32_t logical_x2;
    uint32_t logical_x3;

    // Grid topology object (used by physics modules)
    Grid grid;

    /** Verify that a bound native Grid belongs to this actual Block.
     * Unbound standalone mathematical fixtures retain exact physical partition
     * checks; they cannot acquire authenticated hierarchy provenance here.
     */
    void RequireNativeGeometryIdentity() const {
        const auto& identity=grid.dyadic_identity;
        if(identity.bound && (identity.level!=level
            ||identity.logical!=std::array<std::uint32_t,2>{logical_x1,logical_x2}))
            throw std::invalid_argument("Native RZ Grid is bound to a different Block identity");
    }

    // Data payload
    FluidState fluid_state;
    FluidState state_next;
    FluidState state_scratch;

    // Tree Topology (Hierarchy)
    int parent_id = -1;
    int children_id[8] = {-1, -1, -1, -1, -1, -1, -1, -1};

    // Neighbor Cache (Face neighbors: -X, +X, -Y, +Y, -Z, +Z)
    // Under 2:1 refinement, a face has one coarse or same-level neighbor,
    // or at most four fine neighbors in three dimensions. The fixed capacity
    // avoids per-face allocation while covering every supported dimension.
    struct FaceNeighbors {
        int count = 0;
        int level_diff = 0; // -1: coarse, 0: same, 1: fine
        int ids[4] = {-1, -1, -1, -1};
    };
    FaceNeighbors face_neighbors[6];

    // Status flags
    bool active = false;      ///< Whether this block is currently active in the simulation
    int refine_flag = 0;      ///< 1: refine, -1: coarsen, 0: keep

    Block() = default;

    void Reset() {
        // Clear hierarchy and state while retaining allocated storage.
        id = -1;
        morton_code = 0;
        level = 0;
        active_index = -1;
        logical_x1 = 0;
        logical_x2 = 0;
        logical_x3 = 0;
        parent_id = -1;
        for (int i = 0; i < 8; ++i) children_id[i] = -1;
        for (int i = 0; i < 6; ++i) {
            face_neighbors[i].count = 0;
            face_neighbors[i].level_diff = 0;
            for (int j = 0; j < 4; ++j) face_neighbors[i].ids[j] = -1;
        }
        active = true;
        refine_flag = 0;

        fluid_state.Reset();
        state_next.Reset();
        state_scratch.Reset();
    }

    /**
     * @brief Interpolate data from a coarse parent block using MinMod limited linear gradients.
     * @param coarse The parent block.
     * @param child_idx The relative child index (0 to 7 in 3D).
     * @param dim Dimensionality (1, 2, or 3).
     */
    void InterpolateFromCoarse(const Block& coarse, int child_idx, int dim,
                               double density_floor,
                               double min_specific_internal_energy,
                         GridMetrics::GeometrySemantics semantics = GridMetrics::GeometrySemantics::Existing,
                         double max_specific_internal_energy = std::numeric_limits<double>::max());

    /**
     * @brief Volume average data from child blocks to this coarse block.
     * @param children Array of pointers to the 8 children blocks.
     * @param dim Dimensionality (1, 2, or 3).
     */
    void AverageToCoarse(const Block* children[], int dim,
                         double density_floor,
                         double min_specific_internal_energy,
                         GridMetrics::GeometrySemantics semantics = GridMetrics::GeometrySemantics::Existing);
    // Candidate-parent preflight returns the shared mathematical status.
    // It never mutates source children or allocates/publishes a pool block.
    regrid_math::Status TryAverageToCoarse(const Block* children[], int dim,
                         double density_floor, double min_specific_internal_energy,
                         GridMetrics::GeometrySemantics semantics = GridMetrics::GeometrySemantics::Existing);



    /**
     * @brief Initialize block geometry based on root domain and its location.
     * @param root_grid  Global domain root-level Grid (carries x_min, nblockx, dim, geometry)
     * @param root_dx1/2/3  Root-level cell spacing (computed by AmrTree)
     */
    void InitGeometry(const Grid& root_grid,
                      double root_dx1, double root_dx2, double root_dx3,
                      GridMetrics::GeometrySemantics semantics = GridMetrics::GeometrySemantics::Existing)
    {
        if(semantics==GridMetrics::GeometrySemantics::AxisymmetricRz) {
            // Workflow: bind the immutable real root and logical block first;
            // derive both block endpoints from the same canonical face owner;
            // retain dx only as a representative layout descriptor. Runtime
            // physics/transfer use actual cell faces and widths from this Grid.
            if(root_grid.dim!=2||root_grid.geometry!="cylindrical")
                throw std::invalid_argument("Native RZ Block requires cylindrical dimension 2");
            GridMetrics::DyadicGridIdentity identity{};
            identity.bound=true;
            identity.root_lower={root_grid.x1_min,root_grid.x2_min};
            identity.root_upper={root_grid.x1_max,root_grid.x2_max};
            identity.root_blocks={root_grid.nblockx1,root_grid.nblockx2};
            identity.level=level;identity.logical={logical_x1,logical_x2};
            identity.periodic_axial=root_grid.dyadic_identity.periodic_axial;
            double rlo=0.,rhi=0.,dr=0.,zlo=0.,zhi=0.,dz=0.;
            if(!GridMetrics::expected_axis(identity,0,rlo,rhi,dr)
               ||!GridMetrics::expected_axis(identity,1,zlo,zhi,dz))
                throw std::invalid_argument("Native RZ canonical Block geometry is invalid");
            // The inactive third-coordinate arithmetic stays the existing one.
            const double factor=1.0/(1<<level),dx3=root_dx3*factor;
            const double x3_min=root_grid.x3_min+logical_x3*BLOCK_NZ*dx3;
            const double x3_max=x3_min+BLOCK_NZ*dx3;
            grid=Grid(MAX_NG,rlo,rhi,zlo,zhi,x3_min,x3_max,
                root_grid.nblockx1,root_grid.nblockx2,root_grid.nblockx3);
            grid.geometry=root_grid.geometry;grid.dim=root_grid.dim;
            grid.dyadic_identity=identity;
            grid.InitializeTopology(semantics);return;
        }
        // Calculate cell sizes at this level
        double factor = 1.0 / (1 << level);
        double dx1 = root_dx1 * factor;
        double dx2 = root_dx2 * factor;
        double dx3 = root_dx3 * factor;

        // Bounding box
        double x1_min = root_grid.x1_min + logical_x1 * BLOCK_NX * dx1;
        double x1_max = x1_min + BLOCK_NX * dx1;

        double x2_min = root_grid.x2_min + logical_x2 * BLOCK_NY * dx2;
        double x2_max = x2_min + BLOCK_NY * dx2;

        double x3_min = root_grid.x3_min + logical_x3 * BLOCK_NZ * dx3;
        double x3_max = x3_min + BLOCK_NZ * dx3;

        // Initialize local grid topology wrapper
        grid = Grid(MAX_NG,
                    x1_min, x1_max,
                    x2_min, x2_max,
                    x3_min, x3_max,
                    root_grid.nblockx1, root_grid.nblockx2, root_grid.nblockx3);
        grid.geometry = root_grid.geometry;
        grid.dim = root_grid.dim;
        grid.InitializeTopology(semantics);
    }
};



// Expose the shared regrid mathematics without duplicating its scalar leaves.
using regrid_math::minmod;
using regrid_math::is_admissible_conserved_state;
using regrid_math::composition_simplex_tolerance;
using regrid_math::blend_conserved_state;

inline regrid_math::ConstStateView regrid_state_view(const FluidState& state)
{
    return {{state.rho.data(), state.mom_u.data(), state.mom_v.data(),
             state.mom_w.data(), state.eng.data(), state.enuc_rate.data()},
            state.mass_fractions.data(), state.rho.size()};
}

inline void Block::InterpolateFromCoarse(
    const Block& coarse, int child_idx, int dim, double density_floor,
    double min_specific_internal_energy, GridMetrics::GeometrySemantics semantics,
    double max_specific_internal_energy)
{
    if(semantics==GridMetrics::GeometrySemantics::AxisymmetricRz) {
        RequireNativeGeometryIdentity();coarse.RequireNativeGeometryIdentity();
    }
    const auto fine_view=GridMetrics::make_geometry_view(grid,semantics);
    const auto coarse_view=GridMetrics::make_geometry_view(coarse.grid,semantics);
    const int nx = BLOCK_NX / 2;
    const int ny = dim >= 2 ? BLOCK_NY / 2 : 1;
    const int nz = dim == 3 ? BLOCK_NZ / 2 : 1;
    const int offset_x = (child_idx & 1) ? nx : 0;
    const int offset_y = dim >= 2 && (child_idx & 2) ? ny : 0;
    const int offset_z = dim == 3 && (child_idx & 4) ? nz : 0;
    const int species = fluid_state.GetNumSpecies();
    if (dim < 1 || dim > 3 || child_idx < 0 || child_idx >= (1 << dim)
        || species != coarse.fluid_state.GetNumSpecies())
        throw std::invalid_argument("invalid Host AMR prolongation binding");
    // Reuse one small cell-family workspace across the Host loop. No species
    // limit or device container leaks into the shared mathematical contract.
    std::vector<double> workspace(static_cast<std::size_t>(species)
        * regrid_math::prolongation_workspace_per_species);
    const auto source = regrid_state_view(coarse.fluid_state);
    if (semantics==GridMetrics::GeometrySemantics::AxisymmetricRz) {
        // Workflow: bind actual source/child geometry and immutable readers;
        // integrate the shared native family; write only provisional interiors.
        // Actual complete hierarchy ghosts/EOS remain the Runtime finalizer's
        // authority. No raw mixed-mean Cartesian energy recovery is performed.
        const arch::state::Bounds bounds{
            density_floor,min_specific_internal_energy,max_specific_internal_energy};
        const auto read=[&](int index){return coarse.fluid_state.get(index);};
        const auto enuc=[&](int index){return coarse.fluid_state.enuc_rate[index];};
        const auto fraction=[&](int sp,int index){return coarse.fluid_state.X(sp,index);};
        for (int j=0;j<ny;++j)for (int i=0;i<nx;++i) {
            regrid_math::NativeRzProlongationContext context{};
            context.source_geometry=coarse_view;
            context.logical_nx=coarse.grid.GetTotalX();
            context.logical_ny=coarse.grid.GetTotalY();
            context.radial_i=coarse.grid.Is()+offset_x+i;
            context.axial_j=coarse.grid.Js()+offset_y+j;
            context.has_destination_geometry=true;context.destination_geometry=fine_view;
            context.destination_nx=grid.GetTotalX();context.destination_ny=grid.GetTotalY();
            context.fine_i=grid.Is()+2*i;context.fine_j=grid.Js()+2*j;
            std::array<int,4> destination{};
            for (int child=0;child<4;++child) {
                const int fi=grid.Is()+2*i+(child&1);
                const int fj=grid.Js()+2*j+((child>>1)&1);
                destination[child]=grid.GetIndex(fi,fj,0);
                context.children[child]={fine_view.GetFacePosL(fi),fine_view.GetFacePosR(fi),
                    fine_view.GetAxialFacePosL(fj),fine_view.GetAxialFacePosR(fj)};
            }
            regrid_math::ProlongationResult result{};
            const auto status=regrid_math::prolong_native_family(context,read,enuc,fraction,
                species,bounds,workspace.data(),result);
            if (status!=regrid_math::Status::Ok)
                throw std::runtime_error(regrid_math::status_message(status));
            for (int child=0;child<4;++child) {
                fluid_state.set(destination[child],result.fluid[child]);
                fluid_state.enuc_rate[destination[child]]=result.enuc[child];
                for (int sp=0;sp<species;++sp)
                    fluid_state.X(sp,destination[child])=result.rhoX[
                        static_cast<std::size_t>(sp)*regrid_math::maximum_children+child]
                        /result.fluid[child].rho;
            }
        }
        return;
    }
    for (int k = 0; k < nz; ++k) {
        for (int j = 0; j < ny; ++j) {
            for (int i = 0; i < nx; ++i) {
                const int ci = coarse.grid.Is() + offset_x + i;
                const int cj = coarse.grid.Js() + offset_y + j;
                const int ck = coarse.grid.Ks() + offset_z + k;
                regrid_math::ProlongationGeometry geometry{};
                geometry.dimension = dim;
                geometry.center = coarse.grid.GetIndex(ci, cj, ck);
                geometry.neighbours[0] = coarse.grid.GetIndex(ci - 1, cj, ck);
                geometry.neighbours[1] = coarse.grid.GetIndex(ci + 1, cj, ck);
                geometry.neighbours[2] = dim >= 2 ? coarse.grid.GetIndex(ci, cj - 1, ck) : geometry.center;
                geometry.neighbours[3] = dim >= 2 ? coarse.grid.GetIndex(ci, cj + 1, ck) : geometry.center;
                geometry.neighbours[4] = dim == 3 ? coarse.grid.GetIndex(ci, cj, ck - 1) : geometry.center;
                geometry.neighbours[5] = dim == 3 ? coarse.grid.GetIndex(ci, cj, ck + 1) : geometry.center;
                geometry.coarse_volume = GridMetrics::CellVolume(coarse_view, ci, cj, ck);

                int destination[8]{};
                for (int child = 0; child < (1 << dim); ++child) {
                    const int fi = grid.Is() + 2 * i + (child & 1);
                    const int fj = grid.Js() + 2 * j + (dim >= 2 ? (child >> 1) & 1 : 0);
                    const int fk = grid.Ks() + 2 * k + (dim == 3 ? (child >> 2) & 1 : 0);
                    destination[child] = grid.GetIndex(fi, fj, fk);
                    geometry.fine_volumes[child] = GridMetrics::CellVolume(fine_view, fi, fj, fk);

                }
                regrid_math::ProlongationResult result{};
                const auto status = regrid_math::prolong_family(
                    source, geometry, species, density_floor,
                    min_specific_internal_energy, workspace.data(), result);
                if (status != regrid_math::Status::Ok)
                    throw std::runtime_error(regrid_math::status_message(status));
                for (int child = 0; child < (1 << dim); ++child) {
                    fluid_state.set(destination[child], result.fluid[child]);
                    fluid_state.enuc_rate[destination[child]] = result.enuc[child];
                    for (int sp = 0; sp < species; ++sp)
                        fluid_state.X(sp, destination[child]) =
                            result.rhoX[static_cast<std::size_t>(sp) * regrid_math::maximum_children + child] / result.fluid[child].rho;
                }
            }
        }
    }
}

inline void Block::AverageToCoarse(
    const Block* children[], int dim, double density_floor,
    double min_specific_internal_energy, GridMetrics::GeometrySemantics semantics)
{
    const auto status=TryAverageToCoarse(children,dim,density_floor,
                                       min_specific_internal_energy,semantics);
    if(status!=regrid_math::Status::Ok)
        throw std::runtime_error(regrid_math::status_message(status));
}

inline regrid_math::Status Block::TryAverageToCoarse(
    const Block* children[], int dim, double density_floor,
    double min_specific_internal_energy, GridMetrics::GeometrySemantics semantics)
{
    // Native bound ownership is checked against this actual Block, not merely
    // a self-consistent Grid descriptor; unbound standalone fixtures stay unbound.
    if(semantics==GridMetrics::GeometrySemantics::AxisymmetricRz)
        RequireNativeGeometryIdentity();
    const auto coarse_view=GridMetrics::make_geometry_view(grid,semantics);
    std::array<GridMetrics::GeometryView,8> fine_views{};
    const int nx = BLOCK_NX;
    const int ny = dim >= 2 ? BLOCK_NY : 1;
    const int nz = dim == 3 ? BLOCK_NZ : 1;
    const int species = fluid_state.GetNumSpecies();
    if (dim < 1 || dim > 3)
        throw std::invalid_argument("invalid Host AMR restriction dimension");
    for (int child = 0; child < (1 << dim); ++child)
        if (children[child] == nullptr
            || children[child]->fluid_state.GetNumSpecies() != species)
            throw std::invalid_argument("invalid Host AMR restriction binding");
    for (int child=0;child<(1<<dim);++child) {
        if(semantics==GridMetrics::GeometrySemantics::AxisymmetricRz)
            children[child]->RequireNativeGeometryIdentity();
        fine_views[child]=GridMetrics::make_geometry_view(children[child]->grid,semantics);
    }
    std::vector<double> workspace(static_cast<std::size_t>(species));
    for (int k = 0; k < nz; ++k) {
        for (int j = 0; j < ny; ++j) {
            for (int i = 0; i < nx; ++i) {
                const int child_index = (i >= nx / 2 ? 1 : 0)
                    | ((dim >= 2 && j >= ny / 2 ? 1 : 0) << 1)
                    | ((dim == 3 && k >= nz / 2 ? 1 : 0) << 2);
                const Block& child = *children[child_index];
                const int ibase = 2 * (i % (nx / 2));
                const int jbase = dim >= 2 ? 2 * (j % (ny / 2)) : 0;
                const int kbase = dim == 3 ? 2 * (k % (nz / 2)) : 0;
                const int ci = grid.Is() + i;
                const int cj = grid.Js() + j;
                const int ck = grid.Ks() + k;
                regrid_math::RestrictionGeometry geometry{};
                geometry.count = 1 << dim;
                geometry.coarse_volume = GridMetrics::CellVolume(coarse_view, ci, cj, ck);
                geometry.angular_momentum=semantics==GridMetrics::GeometrySemantics::AxisymmetricRz;
                if(geometry.angular_momentum)
                    geometry.coarse_angular_measure=GridMetrics::Rz::AngularMomentumMeasure(coarse_view,ci,cj);
                for (int cell = 0; cell < geometry.count; ++cell) {
                    const int fi = child.grid.Is() + ibase + (cell & 1);
                    const int fj = child.grid.Js() + jbase + (dim >= 2 ? (cell >> 1) & 1 : 0);
                    const int fk = child.grid.Ks() + kbase + (dim == 3 ? (cell >> 2) & 1 : 0);
                    geometry.source_cells[cell] = child.grid.GetIndex(fi, fj, fk);
                    geometry.volumes[cell] = GridMetrics::CellVolume(fine_views[child_index], fi, fj, fk);
                    if(geometry.angular_momentum)
                        geometry.angular_measures[cell]=GridMetrics::Rz::AngularMomentumMeasure(fine_views[child_index],fi,fj);
                }
                regrid_math::RestrictionResult result{};
                const auto status = regrid_math::restrict_family(
                    regrid_state_view(child.fluid_state), geometry, species,
                    density_floor, min_specific_internal_energy, workspace.data(), result);
                if (status != regrid_math::Status::Ok) return status;
                const int destination = grid.GetIndex(ci, cj, ck);
                fluid_state.set(destination, result.fluid);
                fluid_state.enuc_rate[destination] = result.enuc;
                for (int sp = 0; sp < species; ++sp)
                    fluid_state.X(sp, destination) = result.fractions[sp];
            }
        }
    }
    return regrid_math::Status::Ok;
}

} // namespace amr
