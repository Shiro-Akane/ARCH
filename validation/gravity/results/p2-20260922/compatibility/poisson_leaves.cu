// Compile-only P2 eligibility check. No device execution or P6 claim.
#include "numerics/multigrid/MGTransfer.h"
__global__ void poisson_leaf_probe(arch::elliptic::CartesianMesh fine,
    arch::elliptic::CartesianMesh coarse, const double* u, double* out)
{
    const int i = blockIdx.x*blockDim.x+threadIdx.x;
    if (i < fine.size()) {
        out[i] = arch::elliptic::apply_cell(fine,arch::elliptic::BoundaryKind::Periodic,u,i)
            + arch::elliptic::diagonal(fine,arch::elliptic::BoundaryKind::Dirichlet,i)
            + arch::multigrid::prolong_cell(fine,coarse,arch::elliptic::BoundaryKind::Dirichlet,u,i);
    }
    if (i < coarse.size()) out[i] += arch::multigrid::restrict_cell(fine,coarse,u,i);
}
