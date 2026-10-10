/**
 * @file RzSelectedReconstruction.h
 * @brief Selected native RZ physical donor bundles for shared face consumers.
 *
 * Workflow:
 * 1. Borrow an explicit actual logical stage geometry, U/X readers and bounds.
 * 2. Require all selected stencil inputs and their genuine density closures/EOS.
 * 3. Build selected weighted/axis-parity radial or physical-node axial profiles.
 * 4. Reconstruct N-1 rhoX fields; the largest central fraction closes the sum.
 * 5. Contract each donor's complete fluid/rhoX bundle by ONE finite theta.
 * 6. Return physical points and conservative rhoX, without publishing a flux.
 *
 * U_theta=B+theta*(H-B), q_theta=rho_B*Xbar+theta*(q_H-rho_B*Xbar).
 * V applies to rho/mr/mz/E/rhoX; W applies only to stored native mphi=J/W.
 * This point/EOS adapter is not a whole-native-stage invariant-domain theorem.
 * Root traversal owns current ghosts, method/interface choice, flux averaging,
 * torque lever, registration and final Runtime density/inertia/EOS acceptance.
 * The heavy entry uses the same Host inline body and a CUDA call boundary;
 * this controls compile expansion without changing its reconstruction order.
 */
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

#include "core/ArchPortability.h"
#include "data/FluidState.h"
#include "grid/GridMetrics.h"
#include "numerics/reconstruction/Reconstruction.h"
#include "numerics/reconstruction/RzParityReconstruction.h"
#include "numerics/reconstruction/RzWeightedReconstruction.h"
#include "numerics/state/RzNativeClosure.h"
#include "numerics/state/StateAdmissibility.h"

namespace RzSelectedReconstruction {
using Status=arch::state::Status;
/** Actual borrowed logical patch and the face's lower donor coordinates. */
struct Context {
    GridMetrics::GeometryView geometry;
    int logical_nx=0,logical_ny=0,face_i=0,face_j=0,direction=0,species=0;
    arch::state::Bounds bounds;
};
/** Complete one-cell bundle: radial six nodes, axial two traces x four nodes. */
struct Donor {
    FluidVector point[8]{};
    FluidVector baseline[8]{}; // Required physical B is immutable through contraction.
    double radius[8]{};
    int node_count=0;
    double theta=0.;
};
/** No output array is assigned until both genuine donor bundles pass. */
struct FaceBundles {
    Donor donor[2];
    Status status=Status::invalid_thermodynamics;
};
/** Method identity is its actual type, never just its ghost depth. */
template<class Policy> struct PolicyTraits { static constexpr int kind=-1; };
template<> struct PolicyTraits<PCMReconstruction> { static constexpr int kind=0; };
template<class Limiter> struct PolicyTraits<MusclReconstruction<Limiter>> {
    static constexpr int kind=1;using limiter=Limiter;
};
template<> struct PolicyTraits<PPMReconstruction> { static constexpr int kind=2; };

namespace detail {
/** Read/write the exact five conserved components without layout aliasing. */
ARCH_INLINE double component(const FluidVector& u,int n) {
    return n==0?u.rho:n==1?u.mom_u:n==2?u.mom_v:n==3?u.mom_w:u.eng;
}
ARCH_INLINE void component(FluidVector& u,int n,double v) {
    if(n==0)u.rho=v;else if(n==1)u.mom_u=v;else if(n==2)u.mom_v=v;
    else if(n==3)u.mom_w=v;else u.eng=v;
}
/** A strictly positive required representation may not silently become zero. */
ARCH_INLINE bool product(double rho,double x,double& q) {
    q=rho*x;
    return std::isfinite(q)&&(!(rho>0.&&x>0.)||q>0.);
}
ARCH_INLINE bool quotient(double q,double rho,double& x) {
    x=q/rho;
    return std::isfinite(x)&&(!(q>0.&&rho>0.)||x>0.);
}
/** High-trial EOS failure can contract the whole ray; required EOS is uncaught. */
template<class Eos>
ARCH_INLINE bool trial_valid(const FluidVector& u,const double* x,int species,
    const arch::state::Bounds& bounds,const Eos& eos) {
    const auto& trial_eos=arch::state::candidate_eos(eos);
#if defined(__CUDA_ARCH__)
    return arch::state::validate_eos(u,x,species,bounds,trial_eos)==Status::valid;
#else
    try {return arch::state::validate_eos(u,x,species,bounds,trial_eos)==Status::valid;}
    catch(const std::runtime_error&) {return false;}
#endif
}
/** Geometry and scratch extent are checked before any reader access. */
ARCH_INLINE bool context_valid(const Context& c,std::size_t workspace_count,
    const double* output,const double* workspace) {
    if(c.geometry.semantics!=GridMetrics::GeometrySemantics::AxisymmetricRz
        ||c.geometry.geometry!=GridMetrics::Geometry::Cylindrical||c.geometry.dim!=2
        ||c.logical_nx<3||c.logical_ny<1||c.species<0
        ||c.geometry.stride_y<c.logical_nx||c.geometry.total_size<=0
        ||(c.direction!=0&&c.direction!=1)||!arch::state::valid_bounds(c.bounds)
        ||c.face_i<0||c.face_i>=c.logical_nx||c.face_j<0||c.face_j>=c.logical_ny)
        return false;
    const auto last=std::int64_t(c.logical_ny-1)*c.geometry.stride_y+c.logical_nx;
    if(last>c.geometry.total_size)return false;
    const auto count=static_cast<std::size_t>(c.species);
    if(count>std::numeric_limits<std::size_t>::max()/19)return false;
    if(c.species&&(!output||!workspace||workspace_count<19*count))return false;
    return !c.geometry.dyadic_identity.bound||GridMetrics::matches_identity(c.geometry);
}
/** Require one real donor U/X and all six physical baseline EOS nodes.
 * Explicit begin=clamp(i-1,0,nx-3) uses logical cells, never padding.
 */
template<class StateReader,class FractionReader,class Eos>
ARCH_INLINE Status require_cell(const StateReader& read,const FractionReader& fraction,
    const Context& c,int i,int j,const Eos& eos,double* x,RzThermodynamics::Cell& cell) {
    if(i<0||i>=c.logical_nx||j<0||j>=c.logical_ny)return Status::invalid_thermodynamics;
    const int index=c.geometry.GetIndex(i,j);
    for(int s=0;s<c.species;++s)x[s]=fraction(s,index);
    auto status=RzThermodynamics::provisional_native_state(read(index),x,c.species,1,c.bounds);
    if(status!=Status::valid)return status;
    int begin=i-1;if(begin<0)begin=0;if(begin>c.logical_nx-3)begin=c.logical_nx-3;
    // The actual three density support states must themselves be finite/rho/X
    // valid before a positive reconstruction can conceal invalid source data.
    for(int k=begin;k<begin+3;++k) {
        const auto u=read(c.geometry.GetIndex(k,j));
        const int support_index=c.geometry.GetIndex(k,j);
        for(int s=0;s<c.species;++s)x[s]=fraction(s,support_index);
        status=RzThermodynamics::provisional_native_state(u,x,c.species,1,c.bounds);
        if(status!=Status::valid)return status;
    }
    for(int s=0;s<c.species;++s)x[s]=fraction(s,index);
    cell=RzThermodynamics::make_cell_supported(read,index,c.geometry,i,begin,c.bounds);
    if(!cell.valid())return cell.status;
    for(int n=0;n<6;++n) {
        const auto point=RzThermodynamics::base_point(cell,
            RzThermodynamics::physical_node_radius(cell,n));
        status=arch::state::validate_eos(point,x,c.species,c.bounds,eos);
        if(status!=Status::valid)return status;
        for(int s=0;s<c.species;++s) {
            double q=0.;if(!product(point.rho,x[s],q))return Status::invalid_composition;
        }
    }
    return Status::valid;
}
/** Select the frozen V/W scalar profile without method-name substitution. */
template<class Policy,std::size_t N>
ARCH_INLINE bool radial_scalar(const std::array<double,N>& values,
    const std::array<RzReconstruction::weighted::Cell,N>& cells,
    RzReconstruction::weighted::Profile& left,RzReconstruction::weighted::Profile& right) {
    if constexpr(PolicyTraits<Policy>::kind==1)
        return RzReconstruction::weighted::muscl<typename PolicyTraits<Policy>::limiter>(values,cells,left,right);
    else if constexpr(PolicyTraits<Policy>::kind==2)
        return RzReconstruction::weighted::ppm(values,cells,left,right);
    else return false;
}
/** Largest central Xi is a fixed dependent fraction, not a normalization. */
ARCH_INLINE int closure_species(const double* own,int species,double& alpha) {
    alpha=0.;int largest=0;
    for(int s=0;s<species;++s){alpha+=own[s];if(own[s]>own[largest])largest=s;}
    return largest;
}
/** Form q_cl=alpha*rho_H-sum(q_noncl) for every node of this donor. */
ARCH_INLINE bool close_species(const Donor& donor,const FluidVector* high,
    double* q,int species,int closure,double alpha) {
    for(int n=0;n<donor.node_count;++n) {
        double sum=0.;for(int s=0;s<species;++s)if(s!=closure)sum+=q[std::size_t(n)*species+s];
        q[std::size_t(n)*species+closure]=alpha*high[n].rho-sum;
        if(!std::isfinite(q[std::size_t(n)*species+closure]))return false;
    }
    return true;
}
/** Test one common ray on all physical nodes with its exact rhoX/rho. */
template<class Eos>
ARCH_INLINE bool ray_valid(const Donor& base,const FluidVector* high,const double* highq,
    const double* own,int species,double theta,const Context& c,const Eos& eos,double* x) {
    for(int n=0;n<base.node_count;++n) {
        const auto u=theta==0.?base.point[n]:theta==1.?high[n]:
            base.point[n]+theta*(high[n]-base.point[n]);
        for(int s=0;s<species;++s) {
            double b=0.;if(!product(base.point[n].rho,own[s],b))return false;
            const double q=theta==0.?b:theta==1.?highq[std::size_t(n)*species+s]:b+theta*(highq[std::size_t(n)*species+s]-b);
            if(!std::isfinite(q)||!quotient(q,u.rho,x[s]))return false;
        }
        if(!trial_valid(u,x,species,c.bounds,eos))return false;
    }
    return true;
}
/** Complete the original finite halving ladder and explicit baseline branch.
 * Only workspace is changed until both donor results are returned as valid.
 */
template<class Eos>
ARCH_INLINE Status finish_donor(Donor& base,FluidVector* high,double* q,const double* own,
    const Context& c,const Eos& eos,double* x) {
    bool found=false;
    for(int trial=0;trial<=54;++trial) {
        base.theta=trial==0?1.:std::ldexp(1.,-trial);
        if(ray_valid(base,high,q,own,c.species,base.theta,c,eos,x)){found=true;break;}
    }
    if(!found) {
        base.theta=0.;
        if(!ray_valid(base,high,q,own,c.species,0.,c,eos,x))return Status::invalid_thermodynamics;
    }
    for(int n=0;n<base.node_count;++n) {
        const auto old=base.point[n];
        for(int s=0;s<c.species;++s) {
            double b=0.;if(!product(old.rho,own[s],b))return Status::invalid_composition;
            q[std::size_t(n)*c.species+s]=base.theta==0.?b:base.theta==1.?q[std::size_t(n)*c.species+s]:
                b+base.theta*(q[std::size_t(n)*c.species+s]-b);
        }
        base.point[n]=base.theta==0.?old:base.theta==1.?high[n]:old+base.theta*(high[n]-old);
    }
    return Status::valid;
}
/** Authenticate the entire bound native axis domain, never a local cutoff.
 * context_valid has already checked chart, actual extents, bounds and identity;
 * exact root_lower==0 selects one consistent policy for all connected cells.
 */
ARCH_INLINE bool axis_muscl_domain(const Context& c) {
    return c.direction==0&&c.geometry.dyadic_identity.bound
        &&c.geometry.dyadic_identity.root_lower[0]==0.
        &&GridMetrics::matches_identity(c.geometry);
}

/** Actual same-sign support ordered by increasing absolute radius.
 * G0/-1 use their genuine first three cells; every other donor uses centered
 * three cells. Selection depends on the owning global cell, not queried face.
 */
struct AxisSupport {
    std::array<int,3> index{};
    std::array<RzReconstruction::parity::Cell,3> cell{};
    int target=0;
};

/** Resolve checked global G and real signed faces before any state read.
 * Positive: G0 -> {i,i+1,i+2}, else {i-1,i,i+1}.
 * Negative: G=-1 -> {i,i-1,i-2}, else {i+1,i,i-1}.
 * No source is synthesized or clamped; missing actual logical halo rejects.
 */
ARCH_INLINE bool axis_support(const Context& c,int i,AxisSupport& output) {
    if(!axis_muscl_domain(c)||i<0||i>=c.logical_nx)return false;
    std::int64_t global=0;
    if(!GridMetrics::global_cell(c.geometry.dyadic_identity,0,
        std::int64_t(i)-c.geometry.ng,global))return false;
    const int target=(global==0||global==-1)?0:1;
    const int first=global==0?0:global>0?-1:global==-1?0:1;
    const int increment=global>=0?1:-1;
    AxisSupport result;result.target=target;
    for(int k=0;k<3;++k) {
        const std::int64_t candidate=std::int64_t(i)+first+increment*k;
        if(candidate<0||candidate>=c.logical_nx)return false;
        result.index[k]=static_cast<int>(candidate);
        result.cell[k]={c.geometry.GetFacePosL(result.index[k]),
            c.geometry.GetFacePosR(result.index[k])};
    }
    output=result;return true;
}

/** Build both axis-regular selected MUSCL donors from immutable real sources.
 * Workflow: authenticate each own support; require all genuine source B/EOS;
 * fit eta=r^2 profiles (even V, odd V/W); close N-1 rhoX by original alpha;
 * contract each complete six-node bundle through the existing common ladder;
 * publish 16*S rhoX only after BOTH donors pass. Physical baseline/scaled-omega,
 * upstream signed ghost parity and final Runtime acceptance remain their owners.
 * Even H=qbar+s*(t_eta-mu); odd H=(r/R)*(gbar+s*(t_eta-mu)).
 */
template<class Limiter,class StateReader,class FractionReader,class Eos>
ARCH_INLINE FaceBundles axis_muscl_donors(const StateReader& read,
    const FractionReader& fraction,const Context& c,const Eos& eos,
    double* output_rhoX,double* workspace) {
    FaceBundles result;
    FluidVector high[2][8]{};
    double* q=workspace;
    double* own=c.species?workspace+16*std::size_t(c.species):nullptr;
    double* scratch=c.species?workspace+18*std::size_t(c.species):nullptr;
    for(int d=0;d<2;++d)for(int n=0;n<8;++n)for(int s=0;s<c.species;++s)
        q[std::size_t(d*8+n)*c.species+s]=0.;
    for(int d=0;d<2;++d) {
        const std::int64_t owning=std::int64_t(c.face_i)+d;
        if(owning<0||owning>=c.logical_nx)return result;
        AxisSupport support;
        if(!axis_support(c,static_cast<int>(owning),support))return result;
        std::array<FluidVector,3> native;
        std::array<RzThermodynamics::Cell,3> source;
        for(int k=0;k<3;++k) {
            const auto status=require_cell(read,fraction,c,support.index[k],
                c.face_j,eos,scratch,source[k]);
            if(status!=Status::valid){result.status=status;return result;}
            native[k]=read(c.geometry.GetIndex(support.index[k],c.face_j));
        }
        const int own_index=c.geometry.GetIndex(static_cast<int>(owning),c.face_j);
        for(int s=0;s<c.species;++s)own[std::size_t(d)*c.species+s]=fraction(s,own_index);
        auto& donor=result.donor[d];const auto& baseline=source[support.target];
        donor.node_count=6;
        for(int n=0;n<6;++n) {
            donor.radius[n]=RzThermodynamics::physical_node_radius(baseline,n);
            donor.point[n]=RzThermodynamics::base_point(baseline,donor.radius[n]);
            donor.baseline[n]=donor.point[n];high[d][n]=donor.point[n];
        }
        for(int f=0;f<5;++f) {
            std::array<double,3> values;
            for(int k=0;k<3;++k)values[k]=component(native[k],f);
            const auto measure=f==1?RzReconstruction::parity::Measure::OddV:
                f==3?RzReconstruction::parity::Measure::OddW:
                     RzReconstruction::parity::Measure::EvenV;
            RzReconstruction::parity::Profile profile;
            if(!RzReconstruction::parity::reconstruct<Limiter>(values,
                support.cell,support.target,measure,profile))
                {result.status=Status::nonfinite;return result;}
            for(int n=0;n<6;++n) {
                const double value=profile.at(donor.radius[n]);
                if(!std::isfinite(value)){result.status=Status::nonfinite;return result;}
                component(high[d][n],f,value);
            }
        }
        double alpha=0.;
        const int closure=closure_species(c.species?own+std::size_t(d)*c.species:nullptr,
            c.species,alpha);
        for(int s=0;s<c.species;++s)if(s!=closure) {
            std::array<double,3> values;
            for(int k=0;k<3;++k) {
                const int index=c.geometry.GetIndex(support.index[k],c.face_j);
                if(!product(native[k].rho,fraction(s,index),values[k]))
                    {result.status=Status::invalid_composition;return result;}
            }
            RzReconstruction::parity::Profile profile;
            if(!RzReconstruction::parity::reconstruct<Limiter>(values,support.cell,
                support.target,RzReconstruction::parity::Measure::EvenV,profile))
                {result.status=Status::nonfinite;return result;}
            for(int n=0;n<6;++n) {
                const double value=profile.at(donor.radius[n]);
                if(!std::isfinite(value)){result.status=Status::nonfinite;return result;}
                q[std::size_t(d*8+n)*c.species+s]=value;
            }
        }
        if(c.species&&!close_species(donor,high[d],q+std::size_t(d)*8*c.species,
            c.species,closure,alpha))
            {result.status=Status::invalid_composition;return result;}
    }
    for(int d=0;d<2;++d) {
        const auto status=finish_donor(result.donor[d],high[d],
            c.species?q+std::size_t(d)*8*c.species:nullptr,
            c.species?own+std::size_t(d)*c.species:nullptr,c,eos,scratch);
        if(status!=Status::valid){result.status=status;return result;}
    }
    for(std::size_t k=0;k<16*std::size_t(c.species);++k)output_rhoX[k]=q[k];
    result.status=Status::valid;return result;
}

/** Required point projection of one source closure at one actual radial node. */
ARCH_INLINE FluidVector source_point(const RzThermodynamics::Cell& cell,double radius) {
    return RzThermodynamics::base_point(cell,radius);
}
} // namespace detail

/** Build BOTH selected donor bundles using immutable actual logical readers.
 * output_rhoX[(donor*8+node)*S+s] requires 16*S elements; scratch requires
 * exactly 19*S: high/final q(16S), own Xi(2S), contiguous EOS Xi(1S).
 * Unused nodes are output zero only on full success. Policy/interface selection
 * must precede this call; unknown policy or insufficient real support rejects.
 */
template<class Policy,class StateReader,class FractionReader,class Eos>
ARCH_HEAVY_INLINE FaceBundles reconstruct_face(const StateReader& read,const FractionReader& fraction,
    const Context& c,const Eos& eos,double* output_rhoX,double* workspace,std::size_t workspace_count) {
    FaceBundles result;
    if(!detail::context_valid(c,workspace_count,output_rhoX,workspace))return result;
    // Authenticate the whole bound axis domain BEFORE the original N4 gather:
    // each owning donor has its own genuine three-source parity support.
    if constexpr(PolicyTraits<Policy>::kind==1) {
        if(detail::axis_muscl_domain(c))
            return detail::axis_muscl_donors<typename PolicyTraits<Policy>::limiter>(
                read,fraction,c,eos,output_rhoX,workspace);
    }
    if constexpr(PolicyTraits<Policy>::kind<0)return result;
    else {
        constexpr int kind=PolicyTraits<Policy>::kind;
        constexpr int N=kind==2?6:kind==1?4:2;
        constexpr int center=kind==2?2:kind==1?1:0;
        constexpr int first=kind==2?-2:kind==1?-1:0;
        // Check signed gather capacity before adding offsets to int indices.
        const std::int64_t normal=c.direction==0?c.face_i:c.face_j;
        const std::int64_t extent=c.direction==0?c.logical_nx:c.logical_ny;
        if(normal+first<0||normal+first+N-1>=extent)return result;
        RzThermodynamics::Cell cells[N];FluidVector native[N];
        FluidVector high[2][8]{};
        double* q=workspace;
        double* own=c.species?workspace+16*std::size_t(c.species):nullptr;
        double* scratch=c.species?workspace+18*std::size_t(c.species):nullptr;
        for(int d=0;d<2;++d)for(int n=0;n<8;++n)for(int s=0;s<c.species;++s)
            q[std::size_t(d*8+n)*c.species+s]=0.;
        for(int k=0;k<N;++k) {
            const int i=c.face_i+(c.direction==0?first+k:0);
            const int j=c.face_j+(c.direction==1?first+k:0);
            result.status=detail::require_cell(read,fraction,c,i,j,eos,scratch,cells[k]);
            if(result.status!=Status::valid)return result;
            const int index=c.geometry.GetIndex(i,j);native[k]=read(index);
            if(k==center||k==center+1)for(int s=0;s<c.species;++s)
                own[std::size_t(k-center)*c.species+s]=fraction(s,index);
        }
        for(int d=0;d<2;++d) {
            auto& donor=result.donor[d];const auto& cell=cells[center+d];
            donor.node_count=c.direction==0?6:8;
            for(int n=0;n<donor.node_count;++n) {
                donor.radius[n]=RzThermodynamics::physical_node_radius(cell,c.direction==0?n:2+n%4);
                donor.point[n]=RzThermodynamics::base_point(cell,donor.radius[n]);
                donor.baseline[n]=donor.point[n];
                high[d][n]=donor.point[n];
            }
        }
        if constexpr(kind!=0) {
            if(c.direction==0) {
                std::array<RzReconstruction::weighted::Cell,N> volume,angular;
                for(int k=0;k<N;++k) {
                    const int i=c.face_i+first+k;
                    if(!RzReconstruction::weighted::bind_cell(c.geometry.GetFacePosL(i),c.geometry.GetFacePosR(i),false,volume[k])
                        ||!RzReconstruction::weighted::bind_cell(c.geometry.GetFacePosL(i),c.geometry.GetFacePosR(i),true,angular[k]))
                        {result.status=Status::invalid_thermodynamics;return result;}
                }
                for(int f=0;f<5;++f) {
                    std::array<double,N> values;for(int k=0;k<N;++k)values[k]=detail::component(native[k],f);
                    RzReconstruction::weighted::Profile a,b;
                    if(!detail::radial_scalar<Policy>(values,f==3?angular:volume,a,b))
                        {result.status=Status::nonfinite;return result;}
                    for(int d=0;d<2;++d)for(int n=0;n<6;++n)
                        detail::component(high[d][n],f,(d?b:a).at(result.donor[d].radius[n]));
                }
                for(int d=0;d<2;++d) {
                    double alpha=0.;const int closure=detail::closure_species(c.species?own+std::size_t(d)*c.species:nullptr,c.species,alpha);
                    for(int s=0;s<c.species;++s)if(s!=closure) {
                        std::array<double,N> values;
                        for(int k=0;k<N;++k) {
                            const int index=c.geometry.GetIndex(c.face_i+first+k,c.face_j);
                            if(!detail::product(native[k].rho,fraction(s,index),values[k]))
                                {result.status=Status::invalid_composition;return result;}
                        }
                        RzReconstruction::weighted::Profile a,b;
                        if(!detail::radial_scalar<Policy>(values,volume,a,b))
                            {result.status=Status::nonfinite;return result;}
                        for(int n=0;n<6;++n)q[std::size_t(d*8+n)*c.species+s]=(d?b:a).at(result.donor[d].radius[n]);
                    }
                    if(c.species&&!detail::close_species(result.donor[d],high[d],q+std::size_t(d)*8*c.species,c.species,closure,alpha))
                        {result.status=Status::invalid_composition;return result;}
                }
            } else {
                for(int n=0;n<4;++n) {
                    FluidVector point[N];double rho[N],ux[N],uy[N],uz[N],pressure[N];
                    for(int k=0;k<N;++k) {
                        point[k]=detail::source_point(cells[k],result.donor[0].radius[n]);
                        const int index=c.geometry.GetIndex(c.face_i,c.face_j+first+k);
                        for(int s=0;s<c.species;++s)scratch[s]=fraction(s,index);
                        if constexpr(kind==2) PPMReconstruction::gather_eos_stencil_point(point[k],scratch,eos,rho[k],ux[k],uy[k],uz[k],pressure[k]);
                    }
                    double prim[2][5][2]{};
                    for(int d=0;d<2;++d) {
                        if constexpr(kind==2) {
                            double r[5],u[5],v[5],w[5],p[5];
                            for(int k=0;k<5;++k){r[k]=rho[k+d];u[k]=ux[k+d];v[k]=uy[k+d];w[k]=uz[k+d];p[k]=pressure[k+d];}
                            PPMReconstruction::reconstruct_cell_primitives(r,u,v,w,p,prim[d][0],prim[d][1],prim[d][2],prim[d][3],prim[d][4]);
                            for(int side=0;side<2;++side) {
                                auto& h=high[d][side*4+n];h={prim[d][0][side],prim[d][0][side]*prim[d][1][side],prim[d][0][side]*prim[d][2][side],prim[d][0][side]*prim[d][3][side],arch::state::invalid()};
                            }
                        } else {
                            for(int f=0;f<5;++f) {
                                const double mean=detail::component(point[d+1],f);
                                const double slope=compute_limited_slope<typename PolicyTraits<Policy>::limiter>(detail::component(point[d],f),mean,detail::component(point[d+2],f));
                                detail::component(high[d][n],f,mean-slope);
                                detail::component(high[d][4+n],f,mean+slope);
                            }
                        }
                        double alpha=0.;const int closure=detail::closure_species(c.species?own+std::size_t(d)*c.species:nullptr,c.species,alpha);
                        for(int s=0;s<c.species;++s)if(s!=closure) {
                            double values[N];for(int k=0;k<N;++k) {
                                const int index=c.geometry.GetIndex(c.face_i,c.face_j+first+k);
                                if(!detail::product(point[k].rho,fraction(s,index),values[k]))
                                    {result.status=Status::invalid_composition;return result;}
                            }
                            double lower=0.,upper=0.;
                            if constexpr(kind==2) {
                                const double v[5]{values[d],values[d+1],values[d+2],values[d+3],values[d+4]};
                                PPMReconstruction::reconstruct_scalar_cell_ppm(v,lower,upper);
                            } else {
                                const double slope=compute_limited_slope<typename PolicyTraits<Policy>::limiter>(values[d],values[d+1],values[d+2]);
                                lower=values[d+1]-slope;upper=values[d+1]+slope;
                            }
                            q[std::size_t(d*8+n)*c.species+s]=lower;q[std::size_t(d*8+4+n)*c.species+s]=upper;
                        }
                        if(c.species) {
                            for(int side=0;side<2;++side) {
                                const int node=side*4+n;double sum=0.;
                                for(int s=0;s<c.species;++s)if(s!=closure)sum+=q[std::size_t(d*8+node)*c.species+s];
                                q[std::size_t(d*8+node)*c.species+closure]=alpha*high[d][node].rho-sum;
                            }
                        }
                        if constexpr(kind==2) {
                            for(int side=0;side<2;++side) {
                                const int node=side*4+n;bool composition=true;
                                for(int s=0;s<c.species;++s)composition=detail::quotient(q[std::size_t(d*8+node)*c.species+s],high[d][node].rho,scratch[s])&&composition;
                                if(composition&&arch::state::validate_composition(scratch,c.species,1)==Status::valid)
                                    high[d][node].eng=ReconstructionMath::probe_face_energy(eos,prim[d][0][side],prim[d][1][side],prim[d][2][side],prim[d][3][side],prim[d][4][side],scratch);
                            }
                        }
                    }
                }
            }
        } else {
            for(int d=0;d<2;++d)for(int n=0;n<result.donor[d].node_count;++n)for(int s=0;s<c.species;++s)
                if(!detail::product(high[d][n].rho,own[std::size_t(d)*c.species+s],q[std::size_t(d*8+n)*c.species+s]))
                    {result.status=Status::invalid_composition;return result;}
        }
        for(int d=0;d<2;++d) {
            result.status=detail::finish_donor(result.donor[d],high[d],c.species?q+std::size_t(d)*8*c.species:nullptr,
                c.species?own+std::size_t(d)*c.species:nullptr,c,eos,scratch);
            if(result.status!=Status::valid)return result;
        }
        for(std::size_t k=0;k<16*std::size_t(c.species);++k)output_rhoX[k]=q[k];
        result.status=Status::valid;return result;
    }
}
} // namespace RzSelectedReconstruction
