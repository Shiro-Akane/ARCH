/** @file EllipticMeshAdapter.h
 * Snapshot of active native cells for an independent elliptic workspace.
 * Borrowed Grid addresses remain valid only in the supplied topology epoch.
 */
#pragma once
#include "amr/topology/BlockHandle.h"
#include "numerics/elliptic/CompositePoisson.h"
#include <span>
#include "grid/Grid.h"
#include "grid/ScalarFieldView.h"
class Grid;
struct GridConfig;
namespace amr { class AMRControl;
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
