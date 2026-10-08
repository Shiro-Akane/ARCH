/**
 * @file PhysicalBoundaryHandler.cpp
 * @brief Evaluate case ghost data on native domain faces with deterministic corners.
 *
 * Workflow:
 * 1. Apply the unchanged logical built-in boundary plan.
 * 2. Identify only the physical faces owned by the current AMR leaf.
 * 3. Existing callbacks retain their x1/x2/x3 ordering. Native RZ gathers
 *    true point-EOS/V/W reflecting seeds and immutable user siblings per axis,
 *    completing x1 before x2 and authenticating all frames before scatter.
 * 4. Scatter provisional native ghosts and separate face-center transport
 *    controls; complete only shared axis parity after final domain exchange.
 *    Runtime actual completed-ghost EOS precedes scheduler GhostValid.
 *
 * Native source points remain inside their actual support. Negative axis
 * corners are signed copies, and padding is never a physical halo. The domain
 * and outer macro/regrid owners handle genuine exchange and rollback.
 */
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>
#ifdef _OPENMP
#include <omp.h>
#endif

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
std::array<const double*,7> native_storage_pointers(const FluidState& state) {
    return {state.rho.data(),state.mom_u.data(),state.mom_v.data(),state.mom_w.data(),
        state.eng.data(),state.enuc_rate.data(),state.mass_fractions.data()};
}
/** Track all borrowed conserved/diagnostic/composition buffer extents. */
std::array<std::size_t,7> native_storage_sizes(const FluidState& state) {
    return {state.rho.size(),state.mom_u.size(),state.mom_v.size(),state.mom_w.size(),
        state.eng.size(),state.enuc_rate.size(),state.mass_fractions.size()};
}
/** Bit-exact geometry frame, avoiding struct-padding comparisons. */
std::array<std::uint64_t,20> native_grid_identity(const Grid& grid) {
    std::array<std::uint64_t,20> result{std::bit_cast<std::uint64_t>(grid.x1_min),std::bit_cast<std::uint64_t>(grid.x1_max),
        std::bit_cast<std::uint64_t>(grid.x2_min),std::bit_cast<std::uint64_t>(grid.x2_max),
        std::bit_cast<std::uint64_t>(grid.x3_min),std::bit_cast<std::uint64_t>(grid.x3_max),
        std::bit_cast<std::uint64_t>(grid.dx1),std::bit_cast<std::uint64_t>(grid.dx2),
        std::bit_cast<std::uint64_t>(grid.dx3)};
    const auto provenance=GridMetrics::identity_words(grid.dyadic_identity);
    std::copy(provenance.begin(),provenance.end(),result.begin()+9);
    return result;
}
/** Freeze the configured native root values, including unbound fixture context.
 * Only immutable identity is recorded: no configured value is replaced/defaulted.
 */
std::array<std::uint64_t,7> native_config_root_identity(const SimConfig& config) {
    return {std::bit_cast<std::uint64_t>(config.grid.x1_min),
        std::bit_cast<std::uint64_t>(config.grid.x1_max),
        std::bit_cast<std::uint64_t>(config.grid.x2_min),
        std::bit_cast<std::uint64_t>(config.grid.x2_max),
        static_cast<std::uint64_t>(config.grid.nblockx1),
        static_cast<std::uint64_t>(config.grid.nblockx2),
        static_cast<std::uint64_t>(config.grid.x2l_boundary_type=="periodic"
            &&config.grid.x2r_boundary_type=="periodic")};
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

BCHandler::BCHandler(const SimConfig& config, GridMetrics::GeometrySemantics semantics)
    : config_(&config), semantics_(semantics), logical_plan_(make_logical_plan(config)),
      compiled_(arch::boundary::host::compile(logical_plan_,
          arch::boundary::host::make_canonical_layout(config.grid.dim))) {
    if (semantics_ == GridMetrics::GeometrySemantics::AxisymmetricRz) {
        if (config.grid.dim != 2 || config.grid.geometry != "cylindrical"
            || !std::isfinite(config.grid.x1_min) || config.grid.x1_min < 0.)
            throw std::invalid_argument("RZ boundary requires cylindrical 2D nonnegative radius");
        if (config.grid.x1_min == 0.) {
            auto input = logical_plan_.input();
            if (input.faces[0] == arch::boundary::BoundaryType::Periodic)
                throw std::invalid_argument("RZ axis cannot be radial periodic");
            input.faces[0] = arch::boundary::BoundaryType::RzAxis;
            axis_plan_ = arch::boundary::make_boundary_plan(input);
            axis_compiled_ = arch::boundary::host::compile(*axis_plan_, compiled_.layout);
        }
    } else if (semantics_ != GridMetrics::GeometrySemantics::Existing)
        throw std::invalid_argument("Unknown boundary chart");
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
    using namespace arch::boundary;
    std::vector<Ghost> result;
    const int lower[3]{grid.Is(), grid.Js(), grid.Ks()}, upper[3]{grid.Ie(), grid.Je(), grid.Ke()};
    const bool native_rz=semantics_==GridMetrics::GeometrySemantics::AxisymmetricRz;
    // Native state callbacks own real logical cells; PAD_NX is storage only.
    const int total[3]{native_rz?grid.GetTotalX():grid.stride_y,
        native_rz?grid.GetTotalY():(grid.dim >= 2 ? grid.stride_z / grid.stride_y : 1),
        native_rz?grid.GetTotalZ():(grid.dim == 3 ? grid.total_size / grid.stride_z : 1)};
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
        if (name == "periodic" || (purpose_ == BoundaryPurpose::Hydro && !hydro_user)) continue;
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
                if(native_rz) {
                    const double radial_lower=grid.GetFacePosL(ghost[0]);
                    if(radial_lower<0.) {
                        // Physical EOS is never evaluated at a negative radius.
                        // The final axis owner mirrors completed positive axial ghosts.
                        if(axis==1&&grid.x1_min==0.)continue;
                        throw std::invalid_argument("RZ user physical halo must remain at nonnegative radius");
                    }
                    // Axial corners borrow the real positive radial halo, including
                    // preceding x1 candidates, rather than extrapolating first/last
                    // active-cell closure outside its own physical support.
                    if(axis==1)donor[0]=ghost[0];
                }
                std::array<double, 3> native{grid.GetCellCenterX(donor[0]),
                    grid.GetCellCenterY(donor[1]), grid.GetCellCenterZ(donor[2])};
                if(native_rz)native={grid.GetCellCenterX(ghost[0]),
                    grid.GetCellCenterY(ghost[1]),grid.GetCellCenterZ(ghost[2])};
                native[axis] = edge;
                const std::array<double, 3> ghost_native{grid.GetCellCenterX(ghost[0]),
                    grid.GetCellCenterY(ghost[1]), grid.GetCellCenterZ(ghost[2])};
                const bool active_tangent = ca >= lower[a] && ca < upper[a] && cb >= lower[b] && cb < upper[b];
                const int plane = (donor[a] - lower[a]) + (upper[a] - lower[a]) * (donor[b] - lower[b]);
                result.push_back({grid.GetIndex(donor[0], donor[1], donor[2]),
                    grid.GetIndex(ghost[0], ghost[1], ghost[2]), face, plane, depth == 1 && active_tangent,
                    MakeBoundaryCoordinates(grid, native, static_cast<BoundaryAxis>(axis),
                        static_cast<BoundarySide>(side), time_, depth, purpose_, ghost_native, semantics_),
                    {donor[0],donor[1]},{ghost[0],ghost[1]}});
            }
    }
    return result;
}


/** Authenticate exact configured root provenance without allocation or EOS.
 * The real Tree copies these root endpoints/counts verbatim into each bound
 * Native Block. Rechecking before final candidate publication also rejects a
 * callback that changed configuration root context while preserving Grid bytes.
 */
void BCHandler::require_native_root_frame(const Grid& grid) const {
    if(semantics_!=GridMetrics::GeometrySemantics::AxisymmetricRz)
        throw std::logic_error("Native reflecting projection requires the explicit RZ chart");
    (void)logical_plan(grid);
    (void)GridMetrics::make_geometry_view(grid,semantics_);
    if(config_->grid.dim!=2||config_->grid.geometry!="cylindrical")
        throw std::invalid_argument("Native reflecting root configuration changed chart");
    const auto& identity=grid.dyadic_identity;
    const double domain_lower[2]{config_->grid.x1_min,config_->grid.x2_min};
    const double domain_upper[2]{config_->grid.x1_max,config_->grid.x2_max};
    if(identity.bound) {
        for(int axis=0;axis<2;++axis)
            if(std::bit_cast<std::uint64_t>(identity.root_lower[axis])
                    !=std::bit_cast<std::uint64_t>(domain_lower[axis])
                ||std::bit_cast<std::uint64_t>(identity.root_upper[axis])
                    !=std::bit_cast<std::uint64_t>(domain_upper[axis])
                ||identity.root_blocks[axis]!=(axis==0?config_->grid.nblockx1:config_->grid.nblockx2))
                throw std::invalid_argument("Native reflecting Grid root identity does not match the actual configuration");
        const bool periodic_axial=config_->grid.x2l_boundary_type=="periodic"
            &&config_->grid.x2r_boundary_type=="periodic";
        if(identity.periodic_axial!=periodic_axial)
            throw std::invalid_argument("Native reflecting Grid periodic root rule changed");
    }
}

/** Select only configured Reflecting faces of this actual root-domain patch.
 * Workflow: authenticate Grid/config/plan -> prove logical root edge (or exact
 * unbound local endpoint) -> exclude the regular radial origin. This same leaf
 * drives ghost construction and the flat point-face wall view; internal AMR
 * faces cannot acquire a wall from a matching zero velocity or a capture flag.
 */
arch::boundary::HydroBoundaryView BCHandler::native_reflecting_faces(const Grid& grid) const {
    using namespace arch::boundary;
    require_native_root_frame(grid);
    const auto& plan=logical_plan(grid);
    const auto& identity=grid.dyadic_identity;
    const double domain_lower[2]{config_->grid.x1_min,config_->grid.x2_min};
    const double domain_upper[2]{config_->grid.x1_max,config_->grid.x2_max};
    const double block_lower[2]{grid.x1_min,grid.x2_min};
    const double block_upper[2]{grid.x1_max,grid.x2_max};
    HydroBoundaryView result;
    for(int axis=0;axis<2;++axis)for(int side=0;side<2;++side) {
        const int face=2*axis+side;
        if(plan.input().faces[face]!=BoundaryType::Reflecting)continue;
        const bool physical=identity.bound
            ?(side?std::uint64_t(identity.logical[axis])+1
                ==(std::uint64_t(identity.root_blocks[axis])<<identity.level)
                :identity.logical[axis]==0)
            :(side?block_upper[axis]==domain_upper[axis]:block_lower[axis]==domain_lower[axis]);
        const double edge=side?block_upper[axis]:block_lower[axis];
        result.reflecting[face]=physical&&!(axis==0&&edge==0.);
    }
    return result;
}

/** Enumerate actual physical reflecting walls without a user callback.
 * Workflow: authenticate native Grid/root/config context -> select a real
 * logical domain edge -> retain positive real target/donor mirror cells.
 * For bound grids, lower logical=0 or upper logical+1=root_blocks*2^level.
 * Unbound local fixtures use exact physical endpoint equality only. The
 * ordinary callback enumerator and its transport channel selection are intact.
 * Axis-negative corners stay with the sole final signed-axis owner.
 */
std::vector<BCHandler::Ghost> BCHandler::reflecting_ghosts(const Grid& grid) const {
    using namespace arch::boundary;
    const auto walls=native_reflecting_faces(grid);
    std::vector<Ghost> result;
    const int lower[2]{grid.Is(),grid.Js()},upper[2]{grid.Ie(),grid.Je()};
    const int total[2]{grid.GetTotalX(),grid.GetTotalY()};
    const double block_lower[2]{grid.x1_min,grid.x2_min};
    const double block_upper[2]{grid.x1_max,grid.x2_max};
    for(int axis=0;axis<2;++axis)for(int side=0;side<2;++side) {
        const int face=2*axis+side;
        if(!walls.reflecting[face])continue;
        const double edge=side?block_upper[axis]:block_lower[axis];
        const int tangent=1-axis;
        for(int t=0;t<total[tangent];++t)for(int depth=1;depth<=grid.ng;++depth) {
            int donor[2]{lower[0],lower[1]},ghost[2]{lower[0],lower[1]};
            ghost[tangent]=t;donor[tangent]=std::clamp(t,lower[tangent],upper[tangent]-1);
            donor[axis]=side?upper[axis]-depth:lower[axis]+depth-1;
            ghost[axis]=side?upper[axis]+depth-1:lower[axis]-depth;
            const double radial_lower=grid.GetFacePosL(ghost[0]);
            if(radial_lower<0.) {
                if(axis==1&&grid.x1_min==0.)continue;
                throw std::invalid_argument("Native reflecting physical halo must remain at nonnegative radius");
            }
            if(axis==1)donor[0]=ghost[0];
            std::array<double,3> native{grid.GetCellCenterX(ghost[0]),grid.GetCellCenterY(ghost[1]),grid.GetCellCenterZ(0)};
            native[axis]=edge;
            const std::array<double,3> ghost_native{grid.GetCellCenterX(ghost[0]),grid.GetCellCenterY(ghost[1]),grid.GetCellCenterZ(0)};
            const bool active_tangent=t>=lower[tangent]&&t<upper[tangent];
            const int plane=donor[tangent]-lower[tangent];
            result.push_back({grid.GetIndex(donor[0],donor[1],0),grid.GetIndex(ghost[0],ghost[1],0),
                face,plane,depth==1&&active_tangent,
                MakeBoundaryCoordinates(grid,native,static_cast<BoundaryAxis>(axis),
                    static_cast<BoundarySide>(side),time_,depth,purpose_,ghost_native,semantics_),
                {donor[0],donor[1]},{ghost[0],ghost[1]}});
        }
    }
    return result;
}

std::shared_ptr<arch::boundary::DiffusionBoundaryStorage> BCHandler::make_diffusion_storage(const Grid& grid, int species) const {
    auto result = std::make_shared<arch::boundary::DiffusionBoundaryStorage>();
    if (purpose_ != arch::boundary::BoundaryPurpose::Diffusion) return result;
    const int extent[3]{grid.Ie() - grid.Is(), grid.Je() - grid.Js(), grid.Ke() - grid.Ks()};
    for (const auto& g : ghosts(grid)) if (g.first_active_layer && result->faces[g.face].empty()) {
        const int axis = g.face / 2;
        result->faces[g.face].resize(extent[(axis + 1) % 3] * extent[(axis + 2) % 3] * (4 + species));
    }
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

const arch::boundary::BoundaryPlan& BCHandler::logical_plan(const Grid& grid) const {
    if (!(arch::boundary::host::make_layout(grid) == compiled_.layout))
        throw std::invalid_argument("Grid does not match prepared boundary layout");
    if (semantics_ == GridMetrics::GeometrySemantics::AxisymmetricRz) {
        (void)GridMetrics::make_geometry_view(grid, semantics_);
        if (!std::isfinite(grid.x1_min) || grid.x1_min < 0.)
            throw std::invalid_argument("Invalid RZ physical boundary radius");
        const double scale=std::max({std::abs(grid.x1_min),std::abs(config_->grid.x1_min),std::abs(grid.dx1)});
        if(config_->grid.x1_min>0.
            &&std::abs(grid.x1_min-config_->grid.x1_min)
                <=32.*std::numeric_limits<double>::epsilon()*scale
            &&grid.GetFacePosL(0)<0.)
            throw std::invalid_argument("RZ off-axis physical halo must remain at nonnegative radius");
        if (grid.x1_min == 0.) {
            if (!axis_plan_) throw std::invalid_argument("RZ axis patch disagrees with prepared source domain");
            return *axis_plan_;
        }
    }
    return logical_plan_;
}

/** Apply only the selected original logical seed, never a callback/EOS gate. */
void BCHandler::apply_builtin(FluidState& state,const Grid& grid) const {
    const auto& selected=logical_plan(grid);
    arch::boundary::host::execute(&selected==&logical_plan_?compiled_:*axis_compiled_,state);
}

/** Exact complete root metadata, including all three inactive endpoint values. */
std::array<std::uint64_t,12> BCHandler::input_root_identity() const {
    const auto& g=config_->grid;
    return {std::bit_cast<std::uint64_t>(g.x1_min),std::bit_cast<std::uint64_t>(g.x1_max),
        std::bit_cast<std::uint64_t>(g.x2_min),std::bit_cast<std::uint64_t>(g.x2_max),
        std::bit_cast<std::uint64_t>(g.x3_min),std::bit_cast<std::uint64_t>(g.x3_max),
        std::uint64_t(g.nblockx1),std::uint64_t(g.nblockx2),std::uint64_t(g.nblockx3),
        std::uint64_t(g.dim),std::uint64_t(GridMetrics::geometry_from_name(g.geometry)),
        std::uint64_t(semantics_)};
}
/** Freeze actual storage once; no scientific state is copied or accepted.
 * Workflow: require real chart/layout -> capture seven allocation leases and
 * original twenty/seven Native identity words -> add explicit common chart/root.
 */
BCHandler::InputIdentity BCHandler::capture_input_identity(
    const FluidState& state,const Grid& grid) const {
    if(semantics_==GridMetrics::GeometrySemantics::Existing&&grid.dyadic_identity.bound)
        throw std::logic_error("Ordinary Hydro input cannot borrow a Native dyadic stamp");
    InputIdentity identity;
    (void)logical_plan(grid);
    (void)GridMetrics::make_geometry_view(grid,semantics_);
    arch::boundary::host::validate_state(compiled_,state);
    identity.owner_=this;identity.binding_revision_=binding_revision_;
    identity.state_=&state;identity.grid_=&grid;
    identity.layout_=arch::boundary::host::make_layout(grid);
    identity.pointers_=native_storage_pointers(state);identity.sizes_=native_storage_sizes(state);
    identity.geometry_=native_grid_identity(grid);identity.root_context_=native_config_root_identity(*config_);
    identity.species_=state.GetNumSpecies();
    identity.revision_=stage_revision_;identity.time_bits_=std::bit_cast<std::uint64_t>(time_);
    identity.purpose_=purpose_;
    identity.full_root_=input_root_identity();identity.semantics_=semantics_;
    identity.chart_=GridMetrics::geometry_from_name(grid.geometry);identity.dimension_=grid.dim;
    return identity;
}
/** Native surface preparation retains its explicit role and original preflight. */
void BCHandler::capture_native_frame(NativeCandidate& candidate,
    const FluidState& state,const Grid& grid) const {
    if(semantics_!=GridMetrics::GeometrySemantics::AxisymmetricRz)
        throw std::logic_error("Native boundary candidate requires the explicit RZ chart");
    candidate.identity_=capture_input_identity(state,grid);
}

/** Prepare unique final native surfaces from immutable per-axis layers.
 * Workflow: incoming previous-axis prefix -> gather all builtin reflectors ->
 * expose the complete builtin seed -> gather all user siblings -> expose the
 * completed final axis. No current-layer sibling can influence another.
 * append/replace lookup maintains one final entry per actual destination; the
 * original arrays, ENUC values and callback transport conditions stay read-only
 * until every domain candidate has passed the existing publication checks.
 */
BCHandler::NativeCandidate BCHandler::prepare_native(const FluidState& state,
    const Grid& grid) const {
    NativeCandidate candidate;capture_native_frame(candidate,state,grid);
    const auto builtin_requests=reflecting_ghosts(grid);
    const auto user_requests=callback_?ghosts(grid):std::vector<Ghost>{};
    if(!builtin_requests.empty()&&!native_reflecting_evaluate_)
        throw std::logic_error("Native reflecting boundary EOS has not been bound");
    if(!user_requests.empty()&&!native_evaluate_)
        throw std::logic_error("User native RZ boundary EOS has not been bound");
    if(builtin_requests.size()>std::numeric_limits<std::size_t>::max()-user_requests.size())
        throw std::length_error("Native boundary candidate extent is not representable");
    candidate.entries_.reserve(builtin_requests.size()+user_requests.size());
    if(callback_) {
        candidate.storage_=make_diffusion_storage(grid,state.GetNumSpecies());
        candidate.publish_controls_=true;
    }
    std::vector<int> lookup(static_cast<std::size_t>(grid.GetTotalSize()),-1);
    const auto checked_offset=[&](int index) {
        if(index<0||index>=grid.GetTotalSize()
            ||index%grid.stride_y>=grid.GetTotalX())
            throw std::out_of_range("Native boundary reader requires a real logical cell");
        return lookup[static_cast<std::size_t>(index)];
    };
    const NativeConservedReader read=[&](int index) {
        const int offset=checked_offset(index);
        return offset<0?state.get(index):candidate.entries_[static_cast<std::size_t>(offset)].conserved;
    };
    const NativeFractionReader fraction=[&](int species,int index) {
        if(species<0||species>=candidate.identity_.species_)
            throw std::out_of_range("Native boundary reader requires a registered species");
        const int offset=checked_offset(index);
        return offset<0?state.X(species,index)
            :candidate.entries_[static_cast<std::size_t>(offset)].fractions[static_cast<std::size_t>(species)];
    };
    std::vector<NativeCandidate::Entry> layer;
    layer.reserve(std::max(builtin_requests.size(),user_requests.size()));
    /** Publish only a complete successful temporary layer to candidate lookup.
     * This is not solver publication: existing arrays remain unchanged.
     */
    const auto overlay=[&] {
        for(auto& entry:layer) {
            const int offset=checked_offset(entry.destination);
            if(offset<0) {
                if(candidate.entries_.size()>static_cast<std::size_t>(std::numeric_limits<int>::max()))
                    throw std::length_error("Native boundary lookup index is not representable");
                const int next=static_cast<int>(candidate.entries_.size());
                candidate.entries_.push_back(std::move(entry));
                lookup[static_cast<std::size_t>(candidate.entries_.back().destination)]=next;
            } else candidate.entries_[static_cast<std::size_t>(offset)]=std::move(entry);
        }
    };
    /** Gather siblings against one immutable prefix, preserving ENUC mirror.
     * User face-center conditions populate only provisional callback storage.
     */
    const auto gather=[&](const std::vector<Ghost>& requests,int axis,
        const auto& evaluator,bool user) {
        layer.clear();
        for(const auto& ghost:requests) {
            if(ghost.face/2!=axis)continue;
            const arch::boundary::NativeRzBoundaryRequest request{
                ghost.source_logical,ghost.destination_logical,ghost.coordinates};
            const auto value=evaluator(grid,request,read,fraction);
            if(value.mass_fractions.size()!=static_cast<std::size_t>(candidate.identity_.species_))
                throw std::logic_error("Native boundary candidate has an incomplete composition");
            const int source_offset=checked_offset(ghost.source);
            const double enuc=source_offset<0?state.enuc_rate[ghost.source]
                :candidate.entries_[static_cast<std::size_t>(source_offset)].enuc;
            layer.push_back({ghost.destination,value.conserved,value.mass_fractions,enuc});
            if(user)store_conditions(*candidate.storage_,ghost,value.conditions,candidate.identity_.species_);
        }
        overlay();
    };
    for(int axis=0;axis<2;++axis) {
        gather(builtin_requests,axis,native_reflecting_evaluate_,false);
        gather(user_requests,axis,native_evaluate_,true);
    }
    validate_native_candidate(candidate,state,grid);
    return candidate;
}

/** Authenticate the borrowed layout/geometry/BC frame without numerical work. */
/** Recheck common metadata, retaining every original twenty/seven-word gate. */
void BCHandler::validate_input_identity(const InputIdentity& identity,
    const FluidState& state,const Grid& grid) const {
    if(identity.owner_!=this||identity.binding_revision_!=binding_revision_
        ||identity.state_!=&state||identity.grid_!=&grid
        ||identity.revision_!=stage_revision_
        ||identity.time_bits_!=std::bit_cast<std::uint64_t>(time_)
        ||identity.purpose_!=purpose_||identity.species_!=state.GetNumSpecies()
        ||identity.layout_!=arch::boundary::host::make_layout(grid)
        ||identity.pointers_!=native_storage_pointers(state)
        ||identity.sizes_!=native_storage_sizes(state)
        ||identity.geometry_!=native_grid_identity(grid)
        ||identity.root_context_!=native_config_root_identity(*config_)
        ||identity.full_root_!=input_root_identity()||identity.semantics_!=semantics_
        ||identity.dimension_!=grid.dim
        ||identity.chart_!=GridMetrics::geometry_from_name(grid.geometry)
        ||config_->grid.dim!=grid.dim||config_->grid.geometry!=grid.geometry
        ||(semantics_==GridMetrics::GeometrySemantics::Existing&&grid.dyadic_identity.bound))
        throw std::logic_error("Native boundary candidate storage/geometry/stage frame drifted");
}
/** Native surface validation keeps root science and actual layout checks. */
void BCHandler::validate_native_frame(const NativeCandidate& candidate,
    const FluidState& state,const Grid& grid) const {
    if(semantics_!=GridMetrics::GeometrySemantics::AxisymmetricRz)
        throw std::logic_error("Native boundary candidate storage/geometry/stage frame drifted");
    validate_input_identity(candidate.identity_,state,grid);
    require_native_root_frame(grid);
    arch::boundary::host::validate_state(compiled_,state);
}

/** Authenticate final entries before the domain's first callback scatter. */
void BCHandler::validate_native_candidate(const NativeCandidate& candidate,
    const FluidState& state,const Grid& grid) const {
    validate_native_frame(candidate,state,grid);
    if(candidate.publish_controls_&&!candidate.storage_)
        throw std::logic_error("Native boundary candidate lost its face conditions");
    std::vector<unsigned char> destinations(static_cast<std::size_t>(grid.GetTotalSize()),0);
    for(const auto& entry:candidate.entries_) {
        if(entry.destination<0||entry.destination>=grid.GetTotalSize()
            ||entry.destination%grid.stride_y>=grid.GetTotalX()
            ||entry.fractions.size()!=static_cast<std::size_t>(candidate.identity_.species_))
            throw std::logic_error("Native boundary candidate lost its logical surface extent");
        if(destinations[static_cast<std::size_t>(entry.destination)]++)
            throw std::logic_error("Native boundary candidate repeats a final destination");
    }
}

/** Observe only the genuine Hydro input metadata after boundary completion.
 * Native additionally retains the real bound point-EOS/root prerequisites;
 * ordinary metadata never fabricates a Native stamp or requires its EOS hook.
 */
BCHandler::HydroInputFrame BCHandler::capture_hydro_input_frame(
    const FluidState& state,const Grid& grid,double expected_time) const {
    if(!std::isfinite(expected_time)||!std::isfinite(time_)
        ||std::bit_cast<std::uint64_t>(expected_time)!=std::bit_cast<std::uint64_t>(time_)
        ||purpose_!=arch::boundary::BoundaryPurpose::Hydro
        ||(semantics_==GridMetrics::GeometrySemantics::AxisymmetricRz&&!native_reflecting_evaluate_))
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
        ||purpose_!=arch::boundary::BoundaryPurpose::Hydro
        ||(semantics_==GridMetrics::GeometrySemantics::AxisymmetricRz&&!native_reflecting_evaluate_))
        throw std::logic_error("Hydro boundary opaque frame changed role");
    validate_input_identity(frame.identity_,state,grid);
    if(semantics_==GridMetrics::GeometrySemantics::AxisymmetricRz)require_native_root_frame(grid);
    arch::boundary::host::validate_state(compiled_,state);
}

/** Allocation-free numerical scatter; actual EOS/GhostValid remain external. */
void BCHandler::publish_native_noexcept(NativeCandidate&& candidate,FluidState& state) const noexcept {
    for(const auto& entry:candidate.entries_) {
        state.set(entry.destination,entry.conserved);
        for(int species=0;species<candidate.identity_.species_;++species)
            state.X(species,entry.destination)=entry.fractions[static_cast<std::size_t>(species)];
        state.enuc_rate[entry.destination]=entry.enuc;
    }
    if(candidate.publish_controls_)state.diffusion_boundary=std::move(candidate.storage_);
}

/** Reuse sole logical RzAxis parity, including completed axial ghost rows. */
void BCHandler::complete_axis(FluidState& state,const Grid& grid) const {
    const auto& selected=logical_plan(grid);
    if(semantics_==GridMetrics::GeometrySemantics::AxisymmetricRz&&grid.x1_min==0.)
        arch::boundary::host::execute_rz_axis(selected,*axis_compiled_,state);
}

void BCHandler::apply(FluidState& state, const Grid& grid) const {
    apply_builtin(state,grid);
    if(semantics_==GridMetrics::GeometrySemantics::AxisymmetricRz) {
        auto candidate=prepare_native(state,grid);
        validate_native_candidate(candidate,state,grid);
        publish_native_noexcept(std::move(candidate),state);
        complete_axis(state,grid);
        return;
    }
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
    if(semantics_==GridMetrics::GeometrySemantics::AxisymmetricRz)
        throw std::logic_error("Native RZ device boundary remains unqualified");
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
