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
        if (grid.geometry!=config.geometry || grid.dim!=base.dimension)
            throw std::invalid_argument("Composite gravity geometry differs from native binding");
        result.grids.push_back(&grid);
        for (int k=grid.Ks();k<grid.Ke();++k) for (int j=grid.Js();j<grid.Je();++j)
            for (int i=grid.Is();i<grid.Ie();++i) {
                const arch::elliptic::CompositeCell cell{block.level,
                    {static_cast<int>(block.logical_x1)*BLOCK_NX+i-grid.Is(),
                     base.dimension>=2 ? static_cast<int>(block.logical_x2)*BLOCK_NY+j-grid.Js() : 0,
                     base.dimension==3 ? static_cast<int>(block.logical_x3)*BLOCK_NZ+k-grid.Ks() : 0}};
                std::array<double,3> lower=base.origin,widths=base.spacing;
                for(int a=0;a<base.dimension;++a) {
                    widths[a]=std::ldexp(base.spacing[a],-cell.level);
                    lower[a]+=cell.index[a]*widths[a];
                }
                const auto geometry=base.geometry==arch::elliptic::Geometry::Cartesian
                    ?GridMetrics::Geometry::Cartesian
                    :(base.geometry==arch::elliptic::Geometry::Cylindrical
                        ?GridMetrics::Geometry::Cylindrical:GridMetrics::Geometry::Spherical);
                const double composite=GridMetrics::CellVolume(
                    GridMetrics::make_geometry_view(geometry,base.dimension,lower,widths),0,0,0);
                const double native=GridMetrics::CellVolume(grid,i,j,k);
                if(std::abs(native-composite)>64*std::numeric_limits<double>::epsilon()*composite)
                    throw std::logic_error("Elliptic cell volume differs from native metrics");
                result.cells.push_back(cell);
                result.storage.push_back({b,grid.GetIndex(i,j,k)});
            }
    }
    return result;
}
}
