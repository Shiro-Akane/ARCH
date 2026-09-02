/**
 * @file AMRInterfaceStencil.h
 * @brief Backend-neutral coarse/fine reconstruction stencil predicate.
 */

#pragma once

#include "../../core/ArchPortability.h"

namespace AMRInterfaceReconstruction {

inline constexpr int kTvdInterfaceGhostDepth = 2;

/**
 * @brief Return whether a wide reconstruction must fall back to TVD MUSCL.
 *
 * Face flags use the common lower/upper ordering: 2*direction is the lower
 * face and 2*direction+1 the upper face.  Invalid wide-stencil metadata takes
 * the conservative path and requests the narrow stencil.
 */
template <typename FaceFlags>
ARCH_INLINE bool needs_tvd_interface_stencil(
    int reconstruction_ghost_depth, const FaceFlags& coarse_fine_faces,
    int direction, int normal_index, int normal_begin, int normal_end)
{
    if (reconstruction_ghost_depth <= kTvdInterfaceGhostDepth) return false;
    if (direction < 0 || direction >= 3 || normal_begin >= normal_end)
        return true;
    const bool touches_lower =
        coarse_fine_faces[2 * direction] != 0
        && normal_index < normal_begin + 2;
    const bool touches_upper =
        coarse_fine_faces[2 * direction + 1] != 0
        && normal_index >= normal_end - 3;
    return touches_lower || touches_upper;
}

} // namespace AMRInterfaceReconstruction
