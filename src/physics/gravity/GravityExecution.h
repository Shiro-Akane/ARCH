/** Shared physical leaves; execution providers only arrange loops and memory.
 * Workflow:
 * 1. Receive active density with mesh and generation identity.
 * 2. Declare physical gravity work descriptors independent of CPU/CUDA launch policy.
 * 3. Publish a checked potential/acceleration field for the requested stage.
 */

#pragma once

#include "numerics/multigrid/CompositeExecution.h"
#include "physics/gravity/GravityBoundary.h"
#include "physics/gravity/GravitySource.h"

namespace Physical::Gravity {
struct GravityCell {int block,offset;double width[3];};
struct GatherDensity {
    int size;const GravityCell* cells;const double* const* patches;double* out;
    /** Gather one active native density into the composite source order. */
    ARCH_INLINE void operator()(int i) const {
        const auto& c=cells[i];const double rho=patches[c.block][c.offset];
        out[i]=rho>0.&&std::isfinite(rho)?rho:std::numeric_limits<double>::quiet_NaN();
    }
};
struct UpdateMoments {
    int size;const int* indices;const BoundaryTreeNode* nodes;BoundaryMoments* moments;
    const double* rho;const double* volumes;
    /** Compute a leaf mass moment or combine its child moments. */
    ARCH_INLINE void operator()(int i) const {
        const int n=indices[i],c=nodes[n].cell;
        if(c>=0){for(int q=0;q<10;++q)
            moments[n].value[q]=rho[c]*nodes[n].unit_moments.value[q];}
        else moments[n]=combine_boundary_moments(nodes,moments,n);
    }
};
struct BoundaryPoint {double position[3];int face;};
struct EvaluateBoundary {
    int size;const BoundaryPoint* points;const BoundaryTreeNode* nodes;const BoundaryMoments* moments;
    int node_count,dimension;GridMetrics::Geometry geometry;
    double G,reference_radius;double* values;
    /** Evaluate one isolated face value from the current multipole tree. */
    ARCH_INLINE void operator()(int i) const {
        values[points[i].face]=dimension==2
            ?isolated_log_potential(nodes,moments,node_count,points[i].position,G,reference_radius,.25,2,geometry)
            :isolated_potential(nodes,moments,node_count,points[i].position,G,.25,2,geometry,dimension);
    }
};
struct CellAcceleration {
    int size,dimension;const GravityCell* cells;const double* sides;double* g;double* inverse_dt_squared;
    /** Average adjacent face accelerations and estimate the gravity timestep. */
    ARCH_INLINE void operator()(int i) const {
        double maximum=0.;
        for(int a=0;a<3;++a) {
            const double value=a<dimension?0.5*(sides[6*i+2*a]+sides[6*i+2*a+1]):0.;
            g[a*size+i]=value;
            if(a<dimension){const double q=std::abs(value)/cells[i].width[a];if(!std::isfinite(q))maximum=std::numeric_limits<double>::infinity();
                else if(q>maximum)maximum=q;}
        }
        // max_a |g_a|/dx_a is an inverse acceleration timescale squared.
        inverse_dt_squared[i]=maximum;
    }
};
/** Write one host-evaluated physical datum onto its composite face.
 *  Only O(surface) values/indices cross the backend boundary each stage; the
 *  interior of the boundary vector is never downloaded or rebuilt. */
struct ScatterBoundary {
    int size;const int* faces;const double* values;double* out;
    /** out[faces[i]] = values[i]; one owner per physical face. */
    ARCH_INLINE void operator()(int i) const {out[faces[i]]=values[i];}
};
/** Sample actual eliminated boundary fields; only these surface pairs download. */
struct GravityBoundarySample {
    int size;
    arch::multigrid::SparseView potential_rows;
    const int* faces;
    const int* signs;
    const double* datum_coefficients;
    const double* potential;
    const double* datum;
    const double* gradient;
    double* out;
    /** Phi_B = sum(coeff*Phi_cell)+coeff_B*c; derivative points outward. */
    ARCH_INLINE void operator()(int i) const {
        arch::math::CompensatedSum sum;
        for(int k=potential_rows.offsets[i];k<potential_rows.offsets[i+1];++k)
            sum.add(potential_rows.values[k]*potential[potential_rows.columns[k]]);
        sum.add(datum_coefficients[i]*datum[faces[i]]);
        out[2*i]=sum.value(); out[2*i+1]=signs[i]*gradient[faces[i]];
    }
};
using GravityWork=std::variant<GatherDensity,UpdateMoments,EvaluateBoundary,CellAcceleration,ScatterBoundary,GravityBoundarySample>;
class GravityExecution {
public:
    virtual ~GravityExecution()=default;
    virtual std::shared_ptr<arch::multigrid::CompositeExecution> numeric() const=0;
    virtual void run(const GravityWork&)=0;
};
std::shared_ptr<GravityExecution> make_host_gravity_execution();
}
