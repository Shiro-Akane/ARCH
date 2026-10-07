/**
 * @file DyadicGridIdentity.h
 * @brief Value-only provenance of an actual native RZ AMR block geometry.
 *
 * Workflow:
 * 1. The real tree supplies root bounds/counts and its paired-periodic z rule.
 * 2. Native Block initialization binds its real level and logical position.
 * 3. Generate every bound native face from one checked global integer index.
 * 4. Authenticate block endpoints/representative spacing against that owner.
 * 5. Lend the same face/cell leaves to Host/device consumers. Periodic images,
 *    fluid/EOS acceptance and topology publication remain separate owners.
 *
 * Formula: p(G)=root_lower+(root_upper-root_lower)*(double(G)/double(N)),
 * N=root_cells*2^level; p(0)/p(N) retain the original user endpoint bits.
 * The product is materialized before addition, so no FMA changes face identity.
 * A parent G and its fine 2G therefore share one binary64 face when both exact
 * integer representations are supported. Cells use these faces, not local dx.
 */
#pragma once

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>

#include "amr/topology/AmrDefines.h"
#include "amr/topology/Morton.h"
#include "core/ArchPortability.h"

namespace GridMetrics {

/** Actual root generation context, never a second state or a user option.
 * bound=false is ordinary/unbound geometry. Tree may seed periodic_axial on
 * its unbound native root before a real Block binds the complete context.
 * A bound flag alone is insufficient: all consumers recheck the field values.
 */
struct DyadicGridIdentity {
    bool bound = false;
    std::array<double,2> root_lower{};
    std::array<double,2> root_upper{};
    std::array<int,2> root_blocks{};
    int level = 0;
    std::array<std::uint32_t,2> logical{};
    bool periodic_axial = false;
};

namespace dyadic_identity_detail {
/** Reuse the active block shape; inactive/unknown axes have no native identity. */
ARCH_INLINE int axis_cells(int axis)
{
    return axis==0 ? amr::BLOCK_NX : axis==1 ? amr::BLOCK_NY : 0;
}

/** Compare object values by binary64 identity, including the sign of zero.
 * No structure padding is read; no numerical tolerance is introduced.
 */
ARCH_INLINE bool same_binary64(double left,double right)
{
    return std::bit_cast<std::uint64_t>(left)==std::bit_cast<std::uint64_t>(right);
}

/** Check the actual supported Morton/root extents before integer arithmetic.
 * Every logical coordinate names an interior block, not a periodic image.
 * Signed ghost/image cells are handled separately by global_cell below.
 */
ARCH_INLINE bool valid_context(const DyadicGridIdentity& identity)
{
    if(!identity.bound || identity.level<0 || identity.level>amr::kMaxRefinementLevel)
        return false;
    const auto factor=std::uint64_t{1}<<identity.level;
    for(int axis=0;axis<2;++axis) {
        const int cells=axis_cells(axis),roots=identity.root_blocks[axis];
        if(cells<=0 || roots<=0 || roots>std::numeric_limits<int>::max()/cells
           || !std::isfinite(identity.root_lower[axis])
           || !std::isfinite(identity.root_upper[axis])
           || !(identity.root_upper[axis]>identity.root_lower[axis]))return false;
        const double span=identity.root_upper[axis]-identity.root_lower[axis];
        if(!std::isfinite(span)||!(span>0.))return false;
        const auto extent=static_cast<std::uint64_t>(roots)*factor;
        if(extent==0 || extent-1>amr::kMortonCoordinateMask
           || identity.logical[axis]>=extent
           || identity.logical[axis]>amr::kMortonCoordinateMask
           || static_cast<std::uint64_t>(identity.logical[axis])
               >std::numeric_limits<std::uint32_t>::max()/static_cast<std::uint64_t>(cells))
            return false;
    }
    return identity.root_lower[0]>=0.;
}
} // namespace dyadic_identity_detail

/** Canonical signed face of an actual finite root interval.
 * Workflow: validate exact integer capacity and ordered finite root/span;
 * preserve endpoint bytes; divide exact global index by exact dyadic count;
 * materialize the product before addition; publish only a finite coordinate.
 * Formula p(G)=lo+(hi-lo)*(G/(root_cells*2^level)). Signed ghosts are genuine
 * coordinates, never clipped or reduced modulo the physical domain.
 * This pure leaf accepts general root cell counts, not just block multiples.
 */
ARCH_INLINE bool canonical_dyadic_face(double root_lower,double root_upper,
    std::uint64_t root_cells,int level,std::int64_t global_face,double& output)
{
    constexpr std::uint64_t exact_limit=std::uint64_t{1}<<53;
    constexpr std::int64_t signed_limit=std::int64_t{1}<<53;
    if(level<0||level>amr::kMaxRefinementLevel||root_cells==0
       ||root_cells>(exact_limit>>level)
       ||global_face < -signed_limit||global_face>signed_limit
       ||!std::isfinite(root_lower)||!std::isfinite(root_upper)
       ||!(root_upper>root_lower))return false;
    const auto total=root_cells<<level; // Checked <=2^53 and therefore int64.
    const double span=root_upper-root_lower;
    if(!std::isfinite(span)||!(span>0.))return false;
    if(global_face==0) {output=root_lower;return true;}
    if(global_face==static_cast<std::int64_t>(total)) {output=root_upper;return true;}
    const double fraction=static_cast<double>(global_face)/static_cast<double>(total);
    // The volatile scalar forces this binary64 product to round before the
    // addition on both backends, independently of compiler contraction flags.
    const volatile double product=span*fraction;
    const double coordinate=root_lower+product;
    if(!std::isfinite(fraction)||!std::isfinite(product)||!std::isfinite(coordinate))return false;
    output=coordinate;return true;
}

/** Checked logical*BLOCK_N+local index, including negative ghost faces/cells.
 * No modulo/image selection occurs here. The output is unchanged on failure.
 */
ARCH_INLINE bool global_cell(const DyadicGridIdentity& identity,int axis,
    std::int64_t local_cell,std::int64_t& result)
{
    using namespace dyadic_identity_detail;
    if(axis<0||axis>=2||!valid_context(identity))return false;
    const auto origin=static_cast<std::int64_t>(identity.logical[axis])*axis_cells(axis);
    if(local_cell>std::numeric_limits<std::int64_t>::max()-origin)return false;
    result=origin+local_cell;return true;
}

/** Bind a signed block-local face to the sole canonical root expression. */
ARCH_INLINE bool canonical_axis_face(const DyadicGridIdentity& identity,int axis,
    std::int64_t local_face,double& output)
{
    std::int64_t global=0;
    if(!global_cell(identity,axis,local_face,global))return false;
    const auto roots=static_cast<std::uint64_t>(identity.root_blocks[axis])
        *static_cast<std::uint64_t>(dyadic_identity_detail::axis_cells(axis));
    return canonical_dyadic_face(identity.root_lower[axis],identity.root_upper[axis],
        roots,identity.level,global,output);
}

/** Resolve one ordered represented cell from its two actual canonical faces.
 * Center=left+(right-left)/2; width=right-left. No nominal spacing or floor
 * substitutes for a collapsed/nonfinite cell. All output references are atomic.
 */
ARCH_INLINE bool canonical_axis_cell(const DyadicGridIdentity& identity,int axis,
    std::int64_t local_cell,double& lower,double& upper,double& center,double& width)
{
    double left=0.,right=0.;
    if(local_cell==std::numeric_limits<std::int64_t>::max()
       ||!canonical_axis_face(identity,axis,local_cell,left)
       ||!canonical_axis_face(identity,axis,local_cell+1,right))return false;
    const double length=right-left,middle=left+.5*length;
    if(!std::isfinite(length)||!(length>0.)||!std::isfinite(middle)
       ||!(middle>left)||!(middle<right))return false;
    lower=left;upper=right;center=middle;width=length;return true;
}

/** Canonical block bounds and a representative dx for existing descriptors.
 * dx=(block_upper-block_lower)/BLOCK_N authenticates the stored descriptor;
 * actual finite-volume widths always come from canonical_axis_cell instead.
 */
ARCH_INLINE bool expected_axis(const DyadicGridIdentity& identity,int axis,
    double& lower,double& upper,double& spacing)
{
    double left=0.,right=0.;
    if(axis<0||axis>=2
       ||!canonical_axis_face(identity,axis,0,left)
       ||!canonical_axis_face(identity,axis,dyadic_identity_detail::axis_cells(axis),right))return false;
    const double step=(right-left)/dyadic_identity_detail::axis_cells(axis);
    if(!std::isfinite(step)||!(step>0.)||!(right>left))return false;
    lower=left;upper=right;spacing=step;return true;
}

/** Compare every identity value, including disabled root seed provenance.
 * Unknown/stale metadata cannot compare equal by merely sharing bound=false.
 */
ARCH_INLINE bool equal_identity(const DyadicGridIdentity& left,const DyadicGridIdentity& right)
{
    if(left.bound!=right.bound || left.level!=right.level
       ||left.periodic_axial!=right.periodic_axial
       ||left.root_blocks!=right.root_blocks || left.logical!=right.logical)return false;
    for(int axis=0;axis<2;++axis)
        if(!dyadic_identity_detail::same_binary64(left.root_lower[axis],right.root_lower[axis])
           ||!dyadic_identity_detail::same_binary64(left.root_upper[axis],right.root_upper[axis]))
            return false;
    return true;
}

/** Authenticate actual block descriptors against the unique canonical generator.
 * Missing provenance is not an authenticated native hierarchy. Caller still
 * authenticates chart, layout, live root ownership and source/destination maps.
 */
ARCH_INLINE bool matches_identity(const DyadicGridIdentity& identity,
    const std::array<double,2>& lower,const std::array<double,2>& upper,
    const std::array<double,2>& spacing)
{
    for(int axis=0;axis<2;++axis) {
        double expected_lower=0.,expected_upper=0.,expected_spacing=0.;
        if(!expected_axis(identity,axis,expected_lower,expected_upper,expected_spacing)
           ||!dyadic_identity_detail::same_binary64(lower[axis],expected_lower)
           ||!dyadic_identity_detail::same_binary64(upper[axis],expected_upper)
           ||!dyadic_identity_detail::same_binary64(spacing[axis],expected_spacing))return false;
    }
    return true;
}

/** Authenticate a borrowed GeometryView's actual min/max/dx generation values.
 * Upper bounds are explicit borrowed values; deriving them from min+16*dx
 * would hide an independently changed stored endpoint.
 */
template<class View>
ARCH_INLINE bool matches_identity(const View& view)
{
    return matches_identity(view.dyadic_identity,{view.x1_min,view.x2_min},
        view.actual_block_upper,{view.dx1,view.dx2});
}

/** Serialize value identity for existing Host cache/borrowed-frame keys.
 * Binary64 bits include signed zero. No padding, coordinate tolerance or
 * persistent derived cache is introduced; these words are only provenance.
 */
ARCH_INLINE std::array<std::uint64_t,11> identity_words(const DyadicGridIdentity& identity)
{
    return {std::uint64_t(identity.bound),
        std::bit_cast<std::uint64_t>(identity.root_lower[0]),
        std::bit_cast<std::uint64_t>(identity.root_lower[1]),
        std::bit_cast<std::uint64_t>(identity.root_upper[0]),
        std::bit_cast<std::uint64_t>(identity.root_upper[1]),
        std::uint64_t(identity.root_blocks[0]),std::uint64_t(identity.root_blocks[1]),
        std::uint64_t(identity.level),std::uint64_t(identity.logical[0]),
        std::uint64_t(identity.logical[1]),std::uint64_t(identity.periodic_axial)};
}

static_assert(std::is_standard_layout_v<DyadicGridIdentity>);
static_assert(std::is_trivially_copyable_v<DyadicGridIdentity>);
} // namespace GridMetrics
