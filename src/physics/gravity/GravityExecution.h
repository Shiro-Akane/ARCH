/** Shared physical leaves; execution providers only arrange loops and memory. */
#pragma once
#include "numerics/multigrid/CompositeExecution.h"
#include "physics/gravity/GravityBoundary.h"
#include "physics/gravity/GravityPatchView.h"
namespace Physical::Gravity {
struct GravityCell {int block,offset;double width[3];};
struct GatherDensity {
    int size;const GravityCell* cells;const double* const* patches;double* out;
    ARCH_INLINE void operator()(int i) const {
        const auto& c=cells[i];const double rho=patches[c.block][c.offset];
        out[i]=rho>0.&&std::isfinite(rho)?rho:std::numeric_limits<double>::quiet_NaN();
    }
};
struct UpdateMoments {
    int size;const int* indices;const BoundaryTreeNode* nodes;BoundaryMoments* moments;
    const double* rho;const double* volumes;
    ARCH_INLINE void operator()(int i) const {
        const int n=indices[i],c=nodes[n].cell;
        if(c>=0){moments[n]={};moments[n].value[0]=rho[c]*volumes[c];}
        else moments[n]=combine_boundary_moments(nodes,moments,n);
    }
};
struct BoundaryPoint {double position[3];int face;};
struct EvaluateBoundary {
    int size;const BoundaryPoint* points;const BoundaryTreeNode* nodes;const BoundaryMoments* moments;
    int node_count;double G;double* values;
    ARCH_INLINE void operator()(int i) const {
        values[points[i].face]=isolated_potential(nodes,moments,node_count,points[i].position,G);
    }
};
struct CellAcceleration {
    int size,dimension;const GravityCell* cells;const double* sides;double* g;double* inverse_dt_squared;
    ARCH_INLINE void operator()(int i) const {
        double maximum=0.;
        for(int a=0;a<3;++a) {
            const double value=a<dimension?0.5*(sides[6*i+2*a]+sides[6*i+2*a+1]):0.;
            g[a*size+i]=value;
            if(a<dimension){const double q=std::abs(value)/cells[i].width[a];if(!std::isfinite(q))maximum=std::numeric_limits<double>::infinity();
                else if(q>maximum)maximum=q;}
        }
        inverse_dt_squared[i]=maximum;
    }
};
using GravityWork=std::variant<GatherDensity,UpdateMoments,EvaluateBoundary,CellAcceleration>;
class GravityExecution {
public:
    virtual ~GravityExecution()=default;
    virtual std::shared_ptr<arch::multigrid::CompositeExecution> numeric() const=0;
    virtual void run(const GravityWork&)=0;
};
std::shared_ptr<GravityExecution> make_host_gravity_execution();
}
