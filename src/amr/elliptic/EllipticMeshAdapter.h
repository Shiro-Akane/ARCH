/** @file EllipticMeshAdapter.h
 * Snapshot of active native cells for an independent elliptic workspace.
 * Borrowed Grid addresses remain valid only in the supplied topology epoch.
 * Workflow:
 * 1. Read active cells and topology from their AMR owner.
 * 2. Define the non-owning, generation-checked bridge from active AMR storage to scalar elliptic cells.
 * 3. Expose only stable bindings or derived indicators to downstream solvers.
 */

#pragma once

#include <span>

#include "amr/topology/BlockHandle.h"
#include "grid/Grid.h"
#include "grid/ScalarFieldView.h"
#include "numerics/elliptic/CompositePoisson.h"

class Grid;
struct GridConfig;
namespace amr { class AMRControl;
/** Describe a native padded scalar field without copying its active cells. */
inline arch::grid::ScalarFieldLayout native_scalar_layout(const Grid& grid) {
    using Size=std::size_t;
    return {grid.dim,{Size(PAD_NX),Size(grid.GetTotalY()),Size(grid.GetTotalZ())},
        {1,Size(grid.stride_y),Size(grid.stride_z)},
        {Size(grid.Is()),Size(grid.Js()),Size(grid.Ks())},
        {Size(grid.Ie()),Size(grid.Je()),Size(grid.Ke())}};
}
struct EllipticCellBinding { std::size_t block; int offset; };
struct EllipticMeshBinding {
    arch::elliptic::CartesianMesh base;
    std::vector<arch::elliptic::CompositeCell> cells;
    std::vector<EllipticCellBinding> storage;
    std::vector<const Grid*> grids;
    std::vector<BlockHandle> handles;
};
EllipticMeshBinding bind_elliptic_mesh(const AMRControl&, const GridConfig&,
                                     std::span<const BlockHandle>);
}
