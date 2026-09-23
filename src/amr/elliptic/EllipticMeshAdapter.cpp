/**
 * @file EllipticMeshAdapter.cpp
 * @brief Snapshot active native AMR cells, native offsets and stable handles into one composite elliptic binding.
 *
 * Workflow:
 * 1. Read active cells and topology from their AMR owner.
 * 2. Snapshot active native AMR cells, native offsets and stable handles into one composite elliptic binding.
 * 3. Expose only stable bindings or derived indicators to downstream solvers.
 */

#include <cmath>
#include <limits>

#include "amr/elliptic/EllipticMeshAdapter.h"

#include "amr/AMRControl.h"
#include "grid/GridMetrics.h"

namespace amr {
/** Bind every valid AMR leaf to one x-fast composite Poisson cell and native storage offset. */
EllipticMeshBinding bind_elliptic_mesh(const AMRControl& control, const GridConfig& config,
                                     std::span<const BlockHandle> handles)
{
    EllipticMeshBinding result;
    auto& base=result.base;
    base.dimension=config.dim;
    if(config.geometry=="cartesian")base.geometry=arch::elliptic::Geometry::Cartesian;
    else if(config.geometry=="cylindrical")base.geometry=arch::elliptic::Geometry::Cylindrical;
    else if(config.geometry=="spherical")base.geometry=arch::elliptic::Geometry::Spherical;
    else throw std::invalid_argument("Unsupported elliptic geometry");
    if (base.dimension<1 || base.dimension>3) throw std::invalid_argument("Invalid elliptic dimension");
    const int roots[]{config.nblockx1,config.nblockx2,config.nblockx3};
    const int block_cells[]{BLOCK_NX,BLOCK_NY,BLOCK_NZ};
    for (int axis=0;axis<base.dimension;++axis) {
        const auto extent=static_cast<long long>(roots[axis])*block_cells[axis];
        if (extent<2 || extent>std::numeric_limits<int>::max())
            throw std::invalid_argument("Elliptic root extent exceeds indexing capacity");
        base.cells[axis]=static_cast<int>(extent);
    }
    base.origin={config.x1_min,config.x2_min,config.x3_min};
    const double ends[]{config.x1_max,config.x2_max,config.x3_max};
    for (int a=0;a<base.dimension;++a) base.spacing[a]=(ends[a]-base.origin[a])/base.cells[a];
    arch::elliptic::validate_mesh(base);
    const auto& active=control.tree->GetActiveBlocks();
    if (active.size()!=handles.size() || handles.empty()) throw std::logic_error("Elliptic topology binding size mismatch");
    result.handles.assign(handles.begin(),handles.end());
    for (std::size_t b=0;b<active.size();++b) {
        if (!is_valid(handles[b]) || handles[b].epoch!=handles.front().epoch)
            throw std::logic_error("Elliptic topology has invalid handles");
        const auto& block=control.pool->GetBlock(active[b]); const auto& grid=block.grid;
        if (grid.geometry!=config.geometry || grid.dim!=base.dimension ||
            (base.geometry!=arch::elliptic::Geometry::Cartesian && base.dimension!=1))
            throw std::invalid_argument("Composite gravity geometry differs from native 1D binding");
        result.grids.push_back(&grid);
        double volume=1.; for (int a=0;a<base.dimension;++a) volume*=std::ldexp(base.spacing[a],-block.level);
        if(base.geometry!=arch::elliptic::Geometry::Cartesian) {
            const double left=base.origin[0]+
                static_cast<double>(block.logical_x1*BLOCK_NX)*std::ldexp(base.spacing[0],-block.level);
            const double right=left+std::ldexp(base.spacing[0],-block.level);
            volume=base.geometry==arch::elliptic::Geometry::Spherical
                ? GridMetrics::radial_shell_volume(left,right)
                : GridMetrics::cylindrical_annulus_volume(left,right);
        }
        const double native=GridMetrics::CellVolume(grid,grid.Is(),grid.Js(),grid.Ks());
        if (std::abs(native-volume)>64*std::numeric_limits<double>::epsilon()*volume)
            throw std::logic_error("Elliptic geometry differs from native cell metrics");
        for (int k=grid.Ks();k<grid.Ke();++k) for (int j=grid.Js();j<grid.Je();++j)
            for (int i=grid.Is();i<grid.Ie();++i) {
                result.cells.push_back({block.level,{static_cast<int>(block.logical_x1)*BLOCK_NX+i-grid.Is(),
                    base.dimension>=2 ? static_cast<int>(block.logical_x2)*BLOCK_NY+j-grid.Js() : 0,
                    base.dimension==3 ? static_cast<int>(block.logical_x3)*BLOCK_NZ+k-grid.Ks() : 0}});
                result.storage.push_back({b,grid.GetIndex(i,j,k)});
            }
    }
    return result;
}
}
