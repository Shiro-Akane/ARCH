/**
 * @file NewtonianViscousStress.h
 * @brief One Host/device zero-bulk Newtonian constitutive law in physical 3D.
 *
 * Workflow:
 * 1. The operator supplies a genuine covariant physical gradient G_ij=du_i/dx_j
 *    in an orthonormal frame, and mu=rho*the already configured kinematic nu.
 *    Dimensional reduction does not change the three-dimensional Stokes law.
 * 2. Form tau=mu*(G+G^T-(2/3)*tr(G)*I), using diagonal differences so a true
 *    isotropic homology remains exactly stress-free for any finite mu.
 * 3. Read a selected physical column tau_n as traction, and pair u dot tau_n
 *    with the same traction. The caller owns conservative momentum/energy flux
 *    signs, geometry, quadrature, timestep and all scientific acceptance gates.
 * 4. The optional contraction Q=tau:G diagnoses local mechanical dissipation.
 *    It is not an extra thermal source: no heat is added by this leaf.
 *
 * Inputs/outputs are fixed nine/three-value arrays. There is no Grid, EOS,
 * field ownership, coefficient model, allocation, radius division, floor or
 * independent PDE here. False means invalid/unrepresentable; outputs retain
 * their entry values. Full diffusion/BC/RKL/AMR qualification is separate.
 */
#pragma once

#include <array>
#include <cmath>
#include <cstddef>

#include "core/ArchPortability.h"

namespace NewtonianViscousStress {
/** Row-major G_ij: physical velocity component i, physical derivative axis j. */
using VelocityGradient=std::array<double,9>;
/** Row-major physical stress; all three normal components remain represented. */
using Tensor=std::array<double,9>;
/** Three physical orthonormal components, independent of computational chart. */
using Vector=std::array<double,3>;

namespace detail {
/** Reject every nonfinite operand before zero-coefficient or tensor shortcuts. */
template<std::size_t N>
ARCH_INLINE bool finite(const std::array<double,N>& values)
{
    for(std::size_t n=0;n<N;++n)if(!std::isfinite(values[n]))return false;
    return true;
}
} // namespace detail

/** Atomically evaluate the global three-dimensional Stokes stress.
 * Diagonal identity: 2*G_ii-(2/3)*tr(G)
 * =(2/3)*[(G_ii-G_jj)+(G_ii-G_kk)]. Off-diagonal: G_ij+G_ji.
 * No output is published until every component is finite; mu==0 returns exact
 * zero only after validating the actual finite gradient and coefficient.
 */
ARCH_INLINE bool stress(const VelocityGradient& gradient,double mu,Tensor& output)
{
    if(!std::isfinite(mu)||mu<0.||!detail::finite(gradient))return false;
    Tensor candidate{};
    if(mu!=0.) {
        for(int i=0;i<3;++i) {
            const int j=(i+1)%3,k=(i+2)%3;
            candidate[3*i+i]=mu*((2./3.)*((gradient[3*i+i]-gradient[3*j+j])
                +(gradient[3*i+i]-gradient[3*k+k])));
            for(int j2=i+1;j2<3;++j2) {
                const double value=mu*(gradient[3*i+j2]+gradient[3*j2+i]);
                candidate[3*i+j2]=candidate[3*j2+i]=value;
            }
        }
    }
    if(!detail::finite(candidate))return false;
    output=candidate;return true;
}

/** Extract tau_n without inventing an outward sign or geometric lever arm.
 * A physical normal axis n uses traction_i=tau_(i,n). The operator supplies
 * the actual side orientation and conservative flux sign separately.
 */
ARCH_INLINE bool traction(const Tensor& tensor,int direction,Vector& output)
{
    if(direction<0||direction>=3||!detail::finite(tensor))return false;
    const Vector candidate{tensor[direction],tensor[3+direction],tensor[6+direction]};
    output=candidate;return true;
}

/** Pair physical velocity with exactly the traction already used for momentum.
 * Power density=u dot tau_n, with no heating or second stress evaluation.
 */
ARCH_INLINE bool power(const Vector& velocity,const Vector& face_traction,double& output)
{
    if(!detail::finite(velocity)||!detail::finite(face_traction))return false;
    const double candidate=velocity[0]*face_traction[0]
        +velocity[1]*face_traction[1]+velocity[2]*face_traction[2];
    if(!std::isfinite(candidate))return false;
    output=candidate;return true;
}

/** Contract this law's stress with its actual gradient, Q=tau:G.
 * For a genuine Newtonian tensor Q=2*mu*dev(sym(G)):dev(sym(G))>=0.
 * A negative/nonfinite computed contraction is rejected, never clipped or used
 * as a thermal repair. Arbitrary caller tensors are not certified by this API.
 */
ARCH_INLINE bool dissipation(const Tensor& tensor,const VelocityGradient& gradient,double& output)
{
    if(!detail::finite(tensor)||!detail::finite(gradient))return false;
    double candidate=0.;
    for(int n=0;n<9;++n)candidate+=tensor[n]*gradient[n];
    if(!std::isfinite(candidate)||candidate<0.)return false;
    output=candidate;return true;
}
} // namespace NewtonianViscousStress
