/**
 * @file PhysicalBoundaryHandler.cpp
 * @brief Evaluate case ghost data on native domain faces with deterministic corners.
 *
 * Workflow:
 * 1. Apply the unchanged logical built-in boundary plan.
 * 2. Identify only the physical faces owned by the current AMR leaf.
 * 3. Evaluate callbacks in their existing x1/x2/x3 order.
 * 4. Scatter ghosts and separate face-center transport controls.
 * Epoch control shapes borrow this same physical-face enumeration with an
 * explicit Diffusion purpose. No callback/EOS or BC stage mutation is needed.
 */
#include "physics/boundary/BoundaryFlux.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>
#ifdef _OPENMP
#include <omp.h>
#endif

#include "amr/topology/AmrTree.h"
#include "data/StateDiagnostics.h"
#include "driver/runtime/ComputeBackend.h"
#include "grid/CoordinateBoundary.h"
#include "physics/boundary/PhysicalBoundaryHandler.h"

namespace {
/** Select one of the existing native domain faces. */
std::array<std::string_view, 6> face_names(const SimConfig& c) {
    return {c.grid.x1l_boundary_type, c.grid.x1r_boundary_type,
        c.grid.x2l_boundary_type, c.grid.x2r_boundary_type,
        c.grid.x3l_boundary_type, c.grid.x3r_boundary_type};
}
/** Freeze storage addresses/extents without copying the complete mesh. */
std::array<const double*,7> input_storage_pointers(const FluidState& state) {
    return {state.rho.data(),state.mom_u.data(),state.mom_v.data(),state.mom_w.data(),
        state.eng.data(),state.enuc_rate.data(),state.mass_fractions.data()};
}
/** Track all borrowed conserved/diagnostic/composition buffer extents. */
std::array<std::size_t,7> input_storage_sizes(const FluidState& state) {
    return {state.rho.size(),state.mom_u.size(),state.mom_v.size(),state.mom_w.size(),
        state.eng.size(),state.enuc_rate.size(),state.mass_fractions.size()};
}
/** Bit-exact geometry frame, avoiding struct-padding comparisons. */
std::array<std::uint64_t,9> input_grid_identity(const Grid& grid) {
    std::array<std::uint64_t,9> result{std::bit_cast<std::uint64_t>(grid.x1_min),std::bit_cast<std::uint64_t>(grid.x1_max),
        std::bit_cast<std::uint64_t>(grid.x2_min),std::bit_cast<std::uint64_t>(grid.x2_max),
        std::bit_cast<std::uint64_t>(grid.x3_min),std::bit_cast<std::uint64_t>(grid.x3_max),
        std::bit_cast<std::uint64_t>(grid.dx1),std::bit_cast<std::uint64_t>(grid.dx2),
        std::bit_cast<std::uint64_t>(grid.dx3)};
    return result;
}
/** Physical Neumann denotes the existing zero-normal-gradient hydro boundary. */
arch::boundary::BoundaryType logical_type(std::string_view token) {
    using arch::boundary::BoundaryType;
    if (token == "user" || token == "inflow" || token == "dirichlet" || token == "neumann")
        return BoundaryType::Outflow;
    const auto parsed = arch::dispatch::parse_boundary(token);
    if (!parsed.ok) throw std::invalid_argument("Unknown physical boundary type: " + std::string(token));
    if (parsed.value == arch::dispatch::BoundaryFeature::Periodic) return BoundaryType::Periodic;
    if (parsed.value == arch::dispatch::BoundaryFeature::Reflecting) return BoundaryType::Reflecting;
    return BoundaryType::Outflow;
}
}

BCHandler::BCHandler(const SimConfig& config)
    : config_(&config), logical_plan_(make_logical_plan(config)),
      compiled_(arch::boundary::host::compile(logical_plan_,
          arch::boundary::host::make_canonical_layout(config.grid.dim))) {
    (void)GridMetrics::resolve_public_chart(config.grid.geometry,config.grid.dim);
    if (const auto* selection = arch::boundary::CurrentUserBoundaries())
        callback_ = selection->callbacks.physical;
    const auto names = face_names(config);
    for (int face = 0; face < 2 * config.grid.dim; ++face)
        if ((names[face] == "user" || names[face] == "inflow" || names[face] == "dirichlet") && !callback_)
            throw std::invalid_argument("A user physical face requires same-directory physical_boundary.cpp registration");
}

arch::boundary::BoundaryPlan BCHandler::make_logical_plan(const SimConfig& config) {
    using namespace arch::boundary;
    BoundaryPlanInput input{};
    input.dimension = config.grid.dim;
    input.active_extent = {amr::BLOCK_NX, config.grid.dim >= 2 ? amr::BLOCK_NY : 1,
        config.grid.dim == 3 ? amr::BLOCK_NZ : 1};
    input.ghost_depth = amr::MAX_NG;
    input.faces.fill(BoundaryType::Inactive);
    const auto names = face_names(config);
    for (int face = 0; face < 2 * config.grid.dim; ++face) input.faces[face] = logical_type(names[face]);
    return make_boundary_plan(input);
}

void BCHandler::configure_stage(double time, arch::boundary::BoundaryPurpose purpose) {
    if (!std::isfinite(time)) throw std::invalid_argument("Non-finite boundary stage time");
    // The callback is a pure function of its read-only snapshot. Keep the
    // exact time bits (including signed zero) and purpose in its cache identity;
    // field versions and topology epochs are checked by the existing driver.
    if (std::bit_cast<std::uint64_t>(time) != std::bit_cast<std::uint64_t>(time_)
        || purpose != purpose_) {
        if (stage_revision_ == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("Boundary stage revision exhausted");
        ++stage_revision_;
    }
    time_ = time; purpose_ = purpose;
}

/** Build ordered ghost requests in this handler's explicitly bound chart.
 * Coordinate mapping and metric distance use shared Grid/GridMetrics leaves;
 * native moment/thermodynamic conversion remains a separate physical contract.
 */
std::vector<BCHandler::Ghost> BCHandler::ghosts(const Grid& grid) const {
    return ghosts(grid,purpose_);
}

/** Reuse the original face/mirror enumeration for one explicit purpose.
 * This changes no BC time/revision or callback binding; metadata-only capacity
 * preparation selects Diffusion while all original consumers keep their stage.
 */
std::vector<BCHandler::Ghost> BCHandler::ghosts(const Grid& grid,
    arch::boundary::BoundaryPurpose purpose) const {
    using namespace arch::boundary;
    std::vector<Ghost> result;
    const int lower[3]{grid.Is(), grid.Js(), grid.Ks()}, upper[3]{grid.Ie(), grid.Je(), grid.Ke()};
    const int total[3]{grid.stride_y,
        grid.dim >= 2 ? grid.stride_z / grid.stride_y : 1,
        grid.dim == 3 ? grid.total_size / grid.stride_z : 1};
    const double block_lower[3]{grid.x1_min, grid.x2_min, grid.x3_min};
    const double block_upper[3]{grid.x1_max, grid.x2_max, grid.x3_max};
    const double domain_lower[3]{config_->grid.x1_min, config_->grid.x2_min, config_->grid.x3_min};
    const double domain_upper[3]{config_->grid.x1_max, config_->grid.x2_max, config_->grid.x3_max};
    const double widths[3]{grid.dx1, grid.dx2, grid.dx3};
    const auto names = face_names(*config_);
    for (int axis = 0; axis < grid.dim; ++axis) for (int side = 0; side < 2; ++side) {
        const int face = 2 * axis + side;
        const auto name = names[face];
        const bool hydro_user = name == "user" || name == "inflow" || name == "dirichlet";
        if (name == "periodic" || (purpose == BoundaryPurpose::Hydro && !hydro_user)) continue;
        const double edge = side ? block_upper[axis] : block_lower[axis];
        const double domain = side ? domain_upper[axis] : domain_lower[axis];
        const double scale = std::max({std::abs(edge), std::abs(domain), std::abs(widths[axis])});
        if (std::abs(edge - domain) > 32. * std::numeric_limits<double>::epsilon() * scale) continue;
        // A radial origin or spherical polar join has a regularity/seam owner.
        if (GridMetrics::IsCoordinateJoin(GridMetrics::geometry_kind(grid),grid.dim,axis,domain)) continue;
        const int a = (axis + 1) % 3, b = (axis + 2) % 3;
        for (int cb = 0; cb < total[b]; ++cb) for (int ca = 0; ca < total[a]; ++ca)
            for (int depth = 1; depth <= grid.ng; ++depth) {
                int donor[3]{lower[0], lower[1], lower[2]}, ghost[3]{lower[0], lower[1], lower[2]};
                ghost[a] = ca; ghost[b] = cb;
                donor[a] = std::clamp(ca, lower[a], upper[a] - 1);
                donor[b] = std::clamp(cb, lower[b], upper[b] - 1);
                donor[axis] = side ? upper[axis] - depth : lower[axis] + depth - 1;
                ghost[axis] = side ? upper[axis] + depth - 1 : lower[axis] - depth;

                std::array<double, 3> native{grid.GetCellCenterX(donor[0]),
                    grid.GetCellCenterY(donor[1]), grid.GetCellCenterZ(donor[2])};
                native[axis] = edge;
                const std::array<double, 3> ghost_native{grid.GetCellCenterX(ghost[0]),
                    grid.GetCellCenterY(ghost[1]), grid.GetCellCenterZ(ghost[2])};
                const bool active_tangent = ca >= lower[a] && ca < upper[a] && cb >= lower[b] && cb < upper[b];
                const int plane = (donor[a] - lower[a]) + (upper[a] - lower[a]) * (donor[b] - lower[b]);
                result.push_back({grid.GetIndex(donor[0], donor[1], donor[2]),
                    grid.GetIndex(ghost[0], ghost[1], ghost[2]), face, plane, depth == 1 && active_tangent,
                    MakeBoundaryCoordinates(grid, native, static_cast<BoundaryAxis>(axis),
                        static_cast<BoundarySide>(side), time_, depth, purpose, ghost_native)});
            }
    }
    return result;
}


/** Select the original actual Diffusion face shapes, without creating values.
 * Workflow: check species/layout -> enumerate existing physical faces using
 * explicit Diffusion purpose -> count each first-active face once. Capacity is
 * (4+N)*N_face records; it conveys neither a control value nor a valid binding.
 */
std::array<std::size_t,6> BCHandler::diffusion_control_extents(const Grid& grid,int species) const {
    if(species<0)
        throw std::invalid_argument("Diffusion boundary capacity requires a valid species extent");
    (void)logical_plan(grid);
    std::array<std::size_t,6> result{};
    if(!callback_)return result;
    const int extent[3]{grid.Ie()-grid.Is(),grid.Je()-grid.Js(),grid.Ke()-grid.Ks()};
    const std::size_t fields=4+static_cast<std::size_t>(species);
    for(const auto& ghost:ghosts(grid,arch::boundary::BoundaryPurpose::Diffusion)) {
        if(!ghost.first_active_layer||result[ghost.face])continue;
        const int axis=ghost.face/2;
        const std::size_t a=static_cast<std::size_t>(extent[(axis+1)%3]),
            b=static_cast<std::size_t>(extent[(axis+2)%3]);
        if(!a||!b||a>std::numeric_limits<std::size_t>::max()/b
            ||a*b>std::numeric_limits<std::size_t>::max()/fields)
            throw std::overflow_error("Diffusion boundary capacity is not representable");
        result[ghost.face]=a*b*fields;
    }
    return result;
}

/** Allocate actual callback storage using the same shape as capacity preparation.
 * The values are populated later by the existing real producer/store_conditions;
 * a metadata count is never uploaded or advertised as a bound None condition.
 */
std::shared_ptr<arch::boundary::DiffusionBoundaryStorage> BCHandler::make_diffusion_storage(const Grid& grid, int species) const {
    auto result = std::make_shared<arch::boundary::DiffusionBoundaryStorage>();
    if (purpose_ != arch::boundary::BoundaryPurpose::Diffusion) return result;
    const auto counts=diffusion_control_extents(grid,species);
    for(int face=0;face<6;++face)result->faces[face].resize(counts[face]);
    return result;
}

void BCHandler::store_conditions(arch::boundary::DiffusionBoundaryStorage& storage, const Ghost& ghost,
    const arch::boundary::PhysicalBoundaryData& data, int species) const {
    if (purpose_ != arch::boundary::BoundaryPurpose::Diffusion || !ghost.first_active_layer) return;
    auto* destination = storage.faces[ghost.face].data() + ghost.plane * (4 + species);
    destination[0] = data.temperature;
    for (int d = 0; d < 3; ++d) destination[1 + d] = data.velocity[d];
    for (int s = 0; s < species; ++s)
        destination[4 + s] = data.species.empty() ? arch::boundary::ScalarBoundaryCondition{} : data.species[s];
}

/** Reuse the exact Host face ownership law for both backend bindings. */
arch::boundary::HydroBoundaryView BCHandler::hydro_physical_faces(
    const amr::AmrTree& tree,const amr::Block& block) const {
        const auto& root=tree.GetRootGrid();
        block.RequireLogicalGeometryIdentity(root);
        if(tree.FindBlock(block.level,block.logical_x1,block.logical_x2,block.logical_x3)!=block.id)
            throw std::logic_error("Hydro boundary logical block is not its actual Tree member");
        const auto& plan=logical_plan(block.grid).input();
        const int roots[3]{root.nblockx1,root.nblockx2,root.nblockx3};
        const std::uint32_t logical[3]{block.logical_x1,block.logical_x2,block.logical_x3};
        const double lower[3]{root.x1_min,root.x2_min,root.x3_min};
        const double upper[3]{root.x1_max,root.x2_max,root.x3_max};
        const auto chart=GridMetrics::geometry_from_name(root.geometry);
        arch::boundary::HydroBoundaryView result;
        for(int axis=0;axis<root.dim;++axis)for(int side=0;side<2;++side) {
            const bool root_face=side?std::uint64_t(logical[axis])+1
                ==(std::uint64_t(roots[axis])<<block.level):logical[axis]==0;
            result.reflecting[2*axis+side]=root_face
                &&plan.faces[2*axis+side]==arch::boundary::BoundaryType::Reflecting
                &&!GridMetrics::IsCoordinateJoin(chart,root.dim,axis,side?upper[axis]:lower[axis]);
        }
        return result;
    }

const arch::boundary::BoundaryPlan& BCHandler::logical_plan(const Grid& grid) const {
    if (!(arch::boundary::host::make_layout(grid) == compiled_.layout))
        throw std::invalid_argument("Grid does not match prepared boundary layout");
    (void)GridMetrics::make_geometry_view(grid);

    return logical_plan_;
}

/** Apply only the selected original logical seed, never a callback/EOS gate. */
void BCHandler::apply_builtin(FluidState& state,const Grid& grid) const {
    (void)logical_plan(grid);
    arch::boundary::host::execute(compiled_,state);
}

/** Exact complete root metadata, including all three inactive endpoint values. */
std::array<std::uint64_t,11> BCHandler::input_root_identity() const {
    const auto& g=config_->grid;
    return {std::bit_cast<std::uint64_t>(g.x1_min),std::bit_cast<std::uint64_t>(g.x1_max),
        std::bit_cast<std::uint64_t>(g.x2_min),std::bit_cast<std::uint64_t>(g.x2_max),
        std::bit_cast<std::uint64_t>(g.x3_min),std::bit_cast<std::uint64_t>(g.x3_max),
        std::uint64_t(g.nblockx1),std::uint64_t(g.nblockx2),std::uint64_t(g.nblockx3),
        std::uint64_t(g.dim),std::uint64_t(GridMetrics::geometry_from_name(g.geometry))};
}
/** Freeze actual storage once; no scientific state is copied or accepted.
 * Workflow: require real chart/layout -> capture seven allocation leases and
 * actual geometry words -> add explicit common chart/root.
 */
BCHandler::InputIdentity BCHandler::capture_input_identity(
    const FluidState& state,const Grid& grid) const {
    InputIdentity identity;
    (void)logical_plan(grid);
    (void)GridMetrics::make_geometry_view(grid);
    arch::boundary::host::validate_state(compiled_,state);
    identity.owner_=this;
    identity.state_=&state;identity.grid_=&grid;
    identity.layout_=arch::boundary::host::make_layout(grid);
    identity.pointers_=input_storage_pointers(state);identity.sizes_=input_storage_sizes(state);
    identity.geometry_=input_grid_identity(grid);
    identity.species_=state.GetNumSpecies();
    identity.revision_=stage_revision_;identity.time_bits_=std::bit_cast<std::uint64_t>(time_);
    identity.purpose_=purpose_;
    identity.full_root_=input_root_identity();
    identity.chart_=GridMetrics::geometry_from_name(grid.geometry);identity.dimension_=grid.dim;
    return identity;
}
/** Authenticate the borrowed layout/geometry/BC frame without numerical work. */
/** Recheck common storage, geometry, root and stage metadata. */
void BCHandler::validate_input_identity(const InputIdentity& identity,
    const FluidState& state,const Grid& grid) const {
    if(identity.owner_!=this
        ||identity.state_!=&state||identity.grid_!=&grid
        ||identity.revision_!=stage_revision_
        ||identity.time_bits_!=std::bit_cast<std::uint64_t>(time_)
        ||identity.purpose_!=purpose_||identity.species_!=state.GetNumSpecies()
        ||identity.layout_!=arch::boundary::host::make_layout(grid)
        ||identity.pointers_!=input_storage_pointers(state)
        ||identity.sizes_!=input_storage_sizes(state)
        ||identity.geometry_!=input_grid_identity(grid)
        ||identity.full_root_!=input_root_identity()
        ||identity.dimension_!=grid.dim
        ||identity.chart_!=GridMetrics::geometry_from_name(grid.geometry)
        ||config_->grid.dim!=grid.dim||config_->grid.geometry!=grid.geometry)
        throw std::logic_error("Boundary input storage/geometry/stage frame drifted");
}
/** Observe only the genuine Hydro input metadata after boundary completion.
 */
BCHandler::HydroInputFrame BCHandler::capture_hydro_input_frame(
    const FluidState& state,const Grid& grid,double expected_time) const {
    if(!std::isfinite(expected_time)||!std::isfinite(time_)
        ||std::bit_cast<std::uint64_t>(expected_time)!=std::bit_cast<std::uint64_t>(time_)
        ||purpose_!=arch::boundary::BoundaryPurpose::Hydro)
        throw std::logic_error("Hydro boundary time/purpose/EOS binding is not ready");
    HydroInputFrame frame;frame.identity_=capture_input_identity(state,grid);
    require_hydro_input_frame(frame,state,grid,expected_time);
    return frame;
}
/** Metadata-only revalidation; wall/ledger/EOS authority belongs to real domain. */
void BCHandler::require_hydro_input_frame(const HydroInputFrame& frame,
    const FluidState& state,const Grid& grid,double expected_time) const {
    if(!std::isfinite(expected_time)||!std::isfinite(time_)
        ||std::bit_cast<std::uint64_t>(expected_time)!=std::bit_cast<std::uint64_t>(time_)
        ||purpose_!=arch::boundary::BoundaryPurpose::Hydro)
        throw std::logic_error("Hydro boundary opaque frame changed role");
    validate_input_identity(frame.identity_,state,grid);
    arch::boundary::host::validate_state(compiled_,state);
}

void BCHandler::apply(FluidState& state, const Grid& grid) const {
    apply_builtin(state,grid);

    if (!callback_) return;
    if (!evaluate_) throw std::logic_error("User boundary EOS has not been bound");
    auto storage = make_diffusion_storage(grid, state.GetNumSpecies());
    const auto list=ghosts(grid);
    std::vector<FluidVector> inherited;
    std::vector<double> inherited_x;
    if(purpose_==arch::boundary::BoundaryPurpose::Diffusion) {
        // Snapshot the builtin plan before any callback writes a corner;
        // CUDA gathers the same immutable surface in one operation.
        for(const auto& ghost:list) {
            inherited.push_back(state.get(ghost.destination));
            for(int species=0;species<state.GetNumSpecies();++species)
                inherited_x.push_back(state.X(species,ghost.destination));
        }
    }
    std::vector<double> composition(state.GetNumSpecies());
    for (std::size_t i=0;i<list.size();++i) {
        const auto& ghost=list[i];
        for (int species=0;species<state.GetNumSpecies();++species) composition[species]=state.X(species,ghost.source);
        const auto data=evaluate_(ghost.coordinates,state.get(ghost.source),composition,
            inherited.empty()?nullptr:&inherited[i],inherited.empty()?std::span<const double>{}
                :std::span<const double>(inherited_x).subspan(i*state.GetNumSpecies(),state.GetNumSpecies()));
        state.set(ghost.destination,data.conserved);
        for(int species=0;species<state.GetNumSpecies();++species) state.X(species,ghost.destination)=data.mass_fractions[species];
        state.enuc_rate[ghost.destination]=state.enuc_rate[ghost.source];
        store_conditions(*storage,ghost,data.conditions,state.GetNumSpecies());
    }
    state.diffusion_boundary = std::move(storage);
}

void BCHandler::apply_device(arch::backend::ComputeBackend& backend, arch::backend::BackendStateAccess access,
    const Grid& grid) const {
    if (!callback_) return;
    if (!evaluate_) throw std::logic_error("User boundary EOS has not been bound");
    const auto list = ghosts(grid);
    if (list.empty()) return;
    std::vector<int> sources, destinations;
    for (const auto& ghost : list) { sources.push_back(ghost.source); destinations.push_back(ghost.destination); }
    auto packed = backend.read_boundary_cells(access, sources);
    const int species = static_cast<int>(packed.species_count);
    arch::backend::BoundaryCells inherited;
    if(purpose_==arch::boundary::BoundaryPurpose::Diffusion)
        inherited=backend.read_boundary_cells(access,destinations,arch::state::StateRegion::Ghost);
    auto storage = make_diffusion_storage(grid, species);
    // Each sample owns its gathered inputs and its outputs: slot i of the packed
    // boundary cells and the unique (face, plane) control row of its own
    // first-active-layer face. No per-sample locking or reduction is needed;
    // the loop joins before scatter. The if clause keeps a nested team from
    // oversubscribing an already-parallel caller and leaves small surfaces to the
    // single encountering thread; both branches run the identical sample body.
    const auto evaluate_sample = [&](std::size_t i) {
        const auto data = evaluate_(list[i].coordinates, packed.conserved[i],
            std::span<const double>(packed.composition).subspan(i * species, species),
            inherited.conserved.empty()?nullptr:&inherited.conserved[i],inherited.conserved.empty()
                ?std::span<const double>{}:std::span<const double>(inherited.composition).subspan(i*species,species));
        packed.conserved[i] = data.conserved;
        for (int s = 0; s < species; ++s) packed.composition[i * species + s] = data.mass_fractions[s];
        store_conditions(*storage, list[i], data.conditions, species);
    };
    arch::state::HostFailure failure;
#pragma omp parallel for if(list.size() >= 512 && !omp_in_parallel()) schedule(static)
    for (std::size_t i = 0; i < list.size(); ++i) {
        // Exceptions must not leave the OpenMP structured region: preserve the
        // first failure here and rethrow below, before any scatter publishes.
        try { evaluate_sample(i); } catch (...) { failure.capture_current(); }
    }
    failure.rethrow();
    backend.write_boundary_cells(access, destinations, packed, *storage);
}
