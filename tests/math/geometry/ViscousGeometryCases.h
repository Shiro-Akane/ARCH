// Independent Cartesian/radial witnesses for div(mu grad(v)) and its work flux.
// Test data only: no call to production gradient, connection or flux operators.
#pragma once

#include "data/FluidState.h"
#include "grid/GridGeometryView.h"
#include "grid/Grid.h"
#include "numerics/diffusion/NewtonianViscousStress.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace ViscousGeometryCases {

inline constexpr double viscosity = .03;

struct Sample {
    FluidVector state;
    FluidVector derivative;
};

struct Evaluation {
    std::vector<FluidVector> derivative;
    double raw_dt;
};

inline Sample sample(GridMetrics::Geometry geometry, int dimension,
                     double r, double q2, double q3, bool uniform,
                     double density_slope)
{
    Sample result{};
    if (dimension == 1) {
        // v=r^3 e_r, rho=1+a*r; physical dimension is the radial measure's
        // Cartesian/cylindrical/spherical embedding. Tangential flow is zero.
        const int embedded = geometry == GridMetrics::Geometry::Cartesian ? 1
                           : geometry == GridMetrics::Geometry::Cylindrical ? 2 : 3;
        const double rho = 1. + density_slope*r;
        const double velocity = r*r*r;
        result.state = {rho, rho*velocity, 0., 0., rho*(30.+.5*velocity*velocity)};
        result.derivative.mom_u = viscosity*((2.*embedded+4.)*rho*r
                                            + 3.*density_slope*r*r);
        result.derivative.eng = viscosity*((3.*embedded+12.)*rho*std::pow(r,4)
                                          + 3.*density_slope*std::pow(r,5));
        return result;
    }

    // v=q e_x, q=1 or x^2+y^2(+z^2); rho=1+a*x, mu=nu*rho.
    // div(mu grad(q)) = nu*(2*dimension*rho + 2*a*x).
    // div(mu*q*grad(q)) = nu*q*((2*dimension+4)*rho + 2*a*x).
    // A uniform Cartesian vector gives zero momentum AND work-flux divergence.
    double x = r, squared_radius = r*r;
    std::array<double,3> cartesian_x_projection{1.,0.,0.};
    if (geometry == GridMetrics::Geometry::Cartesian) {
        squared_radius += q2*q2;
        if (dimension == 3) squared_radius += q3*q3;
    } else if (dimension == 2) {
        x = r*std::cos(q2);
        cartesian_x_projection = {std::cos(q2),-std::sin(q2),0.};
    } else if (geometry == GridMetrics::Geometry::Cylindrical) {
        x = r*std::cos(q3);
        squared_radius += q2*q2;
        cartesian_x_projection = {std::cos(q3),0.,-std::sin(q3)};
    } else {
        x = r*std::sin(q2)*std::cos(q3);
        cartesian_x_projection = {std::sin(q2)*std::cos(q3),
                                 std::cos(q2)*std::cos(q3),-std::sin(q3)};
    }
    const double rho = 1.+density_slope*x;
    const double q = uniform ? 1. : squared_radius;
    const auto& b = cartesian_x_projection;
    result.state = {rho,rho*q*b[0],rho*q*b[1],rho*q*b[2],rho*(30.+.5*q*q)};
    if (!uniform) {
        const double momentum = viscosity*(2.*dimension*rho+2.*density_slope*x);
        result.derivative.mom_u = momentum*b[0];
        result.derivative.mom_v = momentum*b[1];
        result.derivative.mom_w = momentum*b[2];
        result.derivative.eng = viscosity*q*((2.*dimension+4.)*rho+2.*density_slope*x);
    }
    return result;
}

inline double error(const FluidVector& actual, const FluidVector& expected)
{
    const double a[]{actual.rho,actual.mom_u,actual.mom_v,actual.mom_w,actual.eng};
    const double e[]{expected.rho,expected.mom_u,expected.mom_v,expected.mom_w,expected.eng};
    double maximum = 0.;
    for (int field=0; field<5; ++field) {
        if (!std::isfinite(a[field])) return std::numeric_limits<double>::infinity();
        maximum = std::max(maximum,std::abs(a[field]-e[field])/std::max(1.,std::abs(e[field])));
    }
    return maximum;
}

// Both executors consume identical input fields and independently derived
// derivatives. Only evaluate(state, grid) is backend-specific.
template <typename Evaluate>
void convergence(const char* backend, Evaluate evaluate)
{
    for (const char* name : {"cartesian", "cylindrical", "spherical"})
    for (int dimension : {1, 2, 3})
    for (double density_slope : {0., .1})
    for (bool uniform : {false, true}) {
        if (dimension == 1 && uniform) continue;
        double previous = 0.;
        for (double spacing : {.05, .025, .0125}) {
            const double x = 2. - (amr::BLOCK_NX/2 + .5)*spacing;
            const double y = .9 - (amr::BLOCK_NY/2 + .5)*spacing;
            const double z = .7 - (amr::BLOCK_NZ/2 + .5)*spacing;
            Grid grid(amr::MAX_NG, x, x+amr::BLOCK_NX*spacing,
                y, y+amr::BLOCK_NY*spacing, z, z+amr::BLOCK_NZ*spacing);
            grid.dim = dimension; grid.geometry = name; grid.InitializeTopology();
            const auto geometry = GridMetrics::geometry_from_name(name);
            FluidState state;
            state.Preallocate(grid.GetTotalSize()); state.InitSpecies(1);
            for (int k=0; k<grid.GetTotalZ(); ++k)
            for (int j=0; j<grid.GetTotalY(); ++j)
            for (int i=0; i<grid.GetTotalX(); ++i) {
                const int cell = grid.GetIndex(i,j,k);
                state.set(cell, sample(geometry, dimension, grid.GetCellCenterX(i),
                    grid.GetCellCenterY(j), grid.GetCellCenterZ(k), uniform, density_slope).state);
                state.X(0,cell) = 1.;
            }
            const int i = grid.Is()+amr::BLOCK_NX/2;
            const int j = dimension >= 2 ? grid.Js()+amr::BLOCK_NY/2 : 0;
            const int k = dimension == 3 ? grid.Ks()+amr::BLOCK_NZ/2 : 0;
            const auto expected = sample(geometry, dimension, grid.GetCellCenterX(i),
                grid.GetCellCenterY(j), grid.GetCellCenterZ(k), uniform, density_slope);
            const double current = error(evaluate(state, grid).derivative[grid.GetIndex(i,j,k)], expected.derivative);
            std::cout << "VISCOUS_SPATIAL_CONVERGENCE backend=" << backend << " geometry=" << name
                      << " dim=" << dimension << " uniform=" << uniform << " density_slope=" << density_slope
                      << " h=" << spacing << " error=" << current << std::endl;
            if (!std::isfinite(current) || (previous > 1.e-10 && previous < 3.5*current))
                throw std::runtime_error("viscous momentum/work flux lost second-order consistency");
            previous = current;
        }
        if (previous > 1.e-4) throw std::runtime_error("viscous analytic derivative budget exceeded");
    }
}

// v=r e_r is linear in Cartesian coordinates: its vector Laplacian is zero,
// including the first active cell touching r=0. Its work divergence is d*nu.
// Then assemble the actual 1D momentum operator from unit columns. A forward
// Euler matrix with nonnegative entries and row sums <=1 is a contraction in
// the max norm; this check does not reuse the production timestep derivation.
struct Contraction {
    double minimum_entry = 1.;
    double maximum_row_sum = 0.;
    double raw_dt = 0.;
};

template <typename Evaluate>
Contraction momentum_contraction(FluidState state, const Grid& grid, Evaluate evaluate)
{
    Contraction result{};
    result.raw_dt = evaluate(state, grid).raw_dt;
    if (!(result.raw_dt > 0.) || !std::isfinite(result.raw_dt))
        throw std::runtime_error("invalid diffusion stability limit");
    std::vector<double> row_sums(amr::BLOCK_NX, 0.);
    for (int column=0; column<amr::BLOCK_NX; ++column) {
        for (int i=0; i<grid.GetTotalX(); ++i) {
            const double rho = state.rho[i];
            const double velocity = i == grid.Is()+column ? 1. : 0.;
            state.set(i, {rho,rho*velocity,0.,0.,rho*(30.+.5*velocity*velocity)});
        }
        const auto basis = evaluate(state, grid);
        for (int row=0; row<amr::BLOCK_NX; ++row) {
            const int cell = grid.Is()+row;
            const double entry = (row == column ? 1. : 0.)
                + result.raw_dt*basis.derivative[cell].mom_u/state.rho[cell];
            result.minimum_entry = std::min(result.minimum_entry, entry);
            row_sums[row] += entry;
        }
    }
    result.maximum_row_sum = *std::max_element(row_sums.begin(), row_sums.end());
    if (!(result.minimum_entry >= -2.e-12 && result.maximum_row_sum <= 1.+2.e-12))
        throw std::runtime_error("forward Euler is not a velocity max-norm contraction");
    return result;
}

template <typename Evaluate>
void radial_origin(const char* backend, Evaluate evaluate)
{
    for (const char* name : {"cylindrical", "spherical"})
    for (int dimension : {1, 2, 3})
    for (double spacing : {.025, .0125, .00625}) {
        Grid grid(amr::MAX_NG, 0., amr::BLOCK_NX*spacing, .5, 1., .2, .7);
        grid.dim = dimension; grid.geometry = name; grid.InitializeTopology();
        FluidState state;
        state.Preallocate(grid.GetTotalSize()); state.InitSpecies(1);
        for (int k=0; k<grid.GetTotalZ(); ++k)
        for (int j=0; j<grid.GetTotalY(); ++j)
        for (int i=0; i<grid.GetTotalX(); ++i) {
            const int cell = grid.GetIndex(i,j,k);
            const double r = grid.GetCellCenterX(i);
            state.set(cell, {1., r, 0., 0., 30.+.5*r*r});
            state.X(0,cell) = 1.;
        }
        const auto measured = evaluate(state, grid);
        const int j = dimension >= 2 ? grid.Js()+amr::BLOCK_NY/2 : 0;
        const int k = dimension == 3 ? grid.Ks()+amr::BLOCK_NZ/2 : 0;
        const int embedded = grid.geometry == "spherical" && dimension != 2 ? 3 : 2;
        const double null_error = error(measured.derivative[grid.GetIndex(grid.Is(),j,k)],
                                       {0.,0.,0.,0.,embedded*viscosity});
        std::cout << "VISCOUS_ORIGIN backend=" << backend << " geometry=" << name
                  << " dim=" << dimension << " h=" << spacing << " error=" << null_error << '\n';
        if (!(null_error <= 2.e-11)) throw std::runtime_error("radial linear field fails origin balance");
        if (dimension != 1) continue;

        const auto stability = momentum_contraction(state, grid, evaluate);
        std::cout << "VISCOUS_RADIAL_STABILITY backend=" << backend << " geometry=" << name
                  << " h=" << spacing << " minimum_entry=" << stability.minimum_entry
                  << " maximum_row_sum=" << stability.maximum_row_sum << '\n';
    }
}

template <typename Evaluate>
void density_stability(const char* backend, Evaluate evaluate)
{
    for (const char* name : {"cartesian", "cylindrical", "spherical"})
    for (double contrast : {1., 10., 100.}) {
        Grid grid(amr::MAX_NG, 0., 1., 0., 1., 0., 1.);
        grid.dim = 1; grid.geometry = name; grid.InitializeTopology();
        FluidState state;
        state.Preallocate(grid.GetTotalSize()); state.InitSpecies(1);
        for (int cell=0; cell<grid.GetTotalSize(); ++cell) {
            const double rho = cell % 2 ? contrast : 1.;
            state.set(cell, {rho,0.,0.,0.,30.*rho});
            state.X(0,cell) = 1.;
        }
        const auto stability = momentum_contraction(state, grid, evaluate);
        std::cout << "VISCOUS_DENSITY_STABILITY backend=" << backend << " geometry=" << name
                  << " contrast=" << contrast << " minimum_entry=" << stability.minimum_entry
                  << " maximum_row_sum=" << stability.maximum_row_sum << '\n';
    }
}

/** Independent point-law inputs/closed analytical expectations, not a PDE oracle.
 * In RZ the physical component order is (r,z,phi). Gradients below include
 * the true connections G_rphi=-u_phi/r and G_phiphi=u_r/r through their exact
 * regular polynomial values, including r=0; the production law divides by no
 * radius. mu=rho*nu varies with z in the shear/swirl examples. Cartesian 1D
 * retains the same three-dimensional Stokes trace subtraction.
 */
struct NewtonianPointCase {
    NewtonianViscousStress::VelocityGradient gradient{};
    NewtonianViscousStress::Vector velocity{};
    NewtonianViscousStress::Tensor expected_stress{};
    std::array<double,3> expected_power{};
    double density=1.,nu=0.,expected_dissipation=0.;
    bool exactly_stress_free=false;
};

/** Hand-derived closed examples; long-double expressions own expected values.
 * No production stress, gradient, connection or flux function constructs any
 * expected tensor, power or dissipation. These only qualify a point law.
 */
inline std::vector<NewtonianPointCase> newtonian_point_cases()
{
    std::vector<NewtonianPointCase> cases;
    for(long double z:{-.75L,.75L})for(long double r:{0.L,.5L,1.5L,-.5L}) {
        const long double a=.5L,b=-.25L,nu=.125L;
        const long double rho=1.L+z/4.L,mu=rho*nu;
        NewtonianPointCase homology{};
        homology.density=static_cast<double>(rho);homology.nu=static_cast<double>(nu);
        homology.gradient={static_cast<double>(a),0.,0.,0.,static_cast<double>(a),0.,0.,0.,static_cast<double>(a)};
        homology.velocity={static_cast<double>(a*r),static_cast<double>(a*z),0.};
        homology.exactly_stress_free=true;cases.push_back(homology);
        auto anisotropic=homology;anisotropic.exactly_stress_free=false;
        anisotropic.gradient[4]=static_cast<double>(b);anisotropic.velocity[1]=static_cast<double>(b*z);
        const long double d=a-b,rr=2.L*mu*d/3.L,zz=-4.L*mu*d/3.L;
        anisotropic.expected_stress[0]=anisotropic.expected_stress[8]=static_cast<double>(rr);
        anisotropic.expected_stress[4]=static_cast<double>(zz);
        anisotropic.expected_power={static_cast<double>(a*r*rr),static_cast<double>(b*z*zz),0.};
        anisotropic.expected_dissipation=static_cast<double>(4.L*mu*d*d/3.L);cases.push_back(anisotropic);
        NewtonianPointCase shear{};shear.density=static_cast<double>(rho);shear.nu=static_cast<double>(nu);
        shear.gradient[3]=static_cast<double>(a*r);shear.velocity[1]=static_cast<double>(a*r*r/2.L);
        shear.expected_stress[1]=shear.expected_stress[3]=static_cast<double>(mu*a*r);
        shear.expected_power[0]=static_cast<double>(mu*a*a*r*r*r/2.L);
        shear.expected_dissipation=static_cast<double>(mu*a*a*r*r);cases.push_back(shear);
        NewtonianPointCase rotation{};rotation.density=static_cast<double>(rho);rotation.nu=static_cast<double>(nu);
        rotation.gradient[2]=-2.;rotation.gradient[6]=2.;rotation.velocity[2]=static_cast<double>(2.L*r);
        rotation.exactly_stress_free=true;cases.push_back(rotation);
        NewtonianPointCase swirl{};swirl.density=static_cast<double>(rho);swirl.nu=static_cast<double>(nu);
        const long double c=.25L,tau=2.L*mu*c*r*r;
        swirl.gradient[2]=static_cast<double>(-c*r*r);swirl.gradient[6]=static_cast<double>(3.L*c*r*r);
        swirl.velocity[2]=static_cast<double>(c*r*r*r);
        swirl.expected_stress[2]=swirl.expected_stress[6]=static_cast<double>(tau);
        swirl.expected_power[0]=static_cast<double>(2.L*mu*c*c*r*r*r*r*r);
        swirl.expected_dissipation=static_cast<double>(4.L*mu*c*c*r*r*r*r);cases.push_back(swirl);
        NewtonianPointCase cartesian{};cartesian.density=static_cast<double>(rho);cartesian.nu=static_cast<double>(nu);
        cartesian.gradient[0]=static_cast<double>(a);cartesian.velocity[0]=static_cast<double>(a*r);
        cartesian.expected_stress[0]=static_cast<double>(4.L*mu*a/3.L);
        cartesian.expected_stress[4]=cartesian.expected_stress[8]=static_cast<double>(-2.L*mu*a/3.L);
        cartesian.expected_power[0]=static_cast<double>(4.L*mu*a*a*r/3.L);
        cartesian.expected_dissipation=static_cast<double>(4.L*mu*a*a/3.L);cases.push_back(cartesian);
        homology.nu=0.;cases.push_back(homology);
    }
    // A finite enormous coefficient must not destroy the exact zero law.
    auto huge=cases[0];huge.density=1.;huge.nu=std::numeric_limits<double>::max();cases.push_back(huge);
    return cases;
}

/** Execute the same shared point law against independent expectations.
 * The single frozen 64-epsilon rounding window compares components/power/Q;
 * exact homology/rotation/zero-mu cases additionally require exact numerical zero.
 * Backend executors supply runtime-uploaded identical case values.
 */
ARCH_INLINE double newtonian_point_error(const NewtonianPointCase& input)
{
    using namespace NewtonianViscousStress;
    const double invalid=std::numeric_limits<double>::infinity();
    Tensor tensor{};double q=0.,maximum=0.;
    if(!stress(input.gradient,input.density*input.nu,tensor)
        ||!dissipation(tensor,input.gradient,q))return invalid;
    for(int field=0;field<9;++field) {
        if(input.exactly_stress_free&&tensor[field]!=0.)return invalid;
        maximum=std::max(maximum,std::abs(tensor[field]-input.expected_stress[field])
            /std::max(1.,std::abs(input.expected_stress[field])));
    }
    maximum=std::max(maximum,std::abs(q-input.expected_dissipation)
        /std::max(1.,std::abs(input.expected_dissipation)));
    for(int direction=0;direction<3;++direction) {
        Vector face{};double work=0.;
        if(!traction(tensor,direction,face)||!power(input.velocity,face,work))return invalid;
        for(int component=0;component<3;++component)
            maximum=std::max(maximum,std::abs(face[component]-input.expected_stress[3*component+direction])
                /std::max(1.,std::abs(input.expected_stress[3*component+direction])));
        maximum=std::max(maximum,std::abs(work-input.expected_power[direction])
            /std::max(1.,std::abs(input.expected_power[direction])));
    }
    return maximum;
}

/** Exact failure/no-publication witnesses, shared by host and device fixtures.
 * Invalid coefficient/gradient, tensor/velocity/direction and unrepresentable
 * tensor or contraction must not replace caller sentinels. No epsilon cutoff,
 * floor, stress clipping or fabricated coefficient is used in these negatives.
 */
ARCH_INLINE bool newtonian_point_guards(const NewtonianPointCase& input)
{
    using namespace NewtonianViscousStress;
    const double nan=std::numeric_limits<double>::quiet_NaN();
    Tensor sentinel{};sentinel.fill(42.);Tensor output=sentinel;
    if(stress(input.gradient,-1.,output)||output!=sentinel)return false;
    if(stress(input.gradient,nan,output)||output!=sentinel)return false;
    if(stress(input.gradient,std::numeric_limits<double>::infinity(),output)||output!=sentinel)return false;
    for(int component=0;component<9;++component) {
        auto bad=input.gradient;bad[component]=nan;
        if(stress(bad,0.,output)||output!=sentinel)return false;
    }
    VelocityGradient huge{};huge[0]=std::numeric_limits<double>::max();huge[4]=-huge[0];
    if(stress(huge,1.,output)||output!=sentinel)return false;
    // mu==0 needs no finite differences/sums, but still requires finite inputs.
    if(!stress(huge,0.,output))return false;
    for(double component:output)if(component!=0.)return false;
    const Vector vector_sentinel{41.,43.,47.};Vector face=vector_sentinel;
    if(traction(sentinel,-1,face)||face!=vector_sentinel)return false;
    if(traction(sentinel,3,face)||face!=vector_sentinel)return false;
    auto invalid_tensor=sentinel;invalid_tensor[0]=nan;
    if(traction(invalid_tensor,0,face)||face!=vector_sentinel)return false;
    double scalar=53.;auto invalid_velocity=input.velocity;invalid_velocity[0]=nan;
    if(power(invalid_velocity,Vector{},scalar)||scalar!=53.)return false;
    if(power(Vector{std::numeric_limits<double>::max(),0.,0.},Vector{2.,0.,0.},scalar)||scalar!=53.)return false;
    if(dissipation(invalid_tensor,input.gradient,scalar)||scalar!=53.)return false;
    Tensor negative{};negative[0]=-1.;VelocityGradient positive{};positive[0]=1.;
    if(dissipation(negative,positive,scalar)||scalar!=53.)return false;
    Tensor enormous{};enormous[0]=std::numeric_limits<double>::max();positive[0]=2.;
    if(dissipation(enormous,positive,scalar)||scalar!=53.)return false;
    return true;
}

/** Qualify the analytical point law on this backend, without a PDE claim. */
inline void newtonian_constitutive()
{
    double maximum=0.;const auto cases=newtonian_point_cases();
    for(const auto& input:cases) {
        maximum=std::max(maximum,newtonian_point_error(input));
        if(!newtonian_point_guards(input))throw std::runtime_error("Newtonian point-law failure publication contract");
    }
    if(!std::isfinite(maximum)||maximum>64.*std::numeric_limits<double>::epsilon())
        throw std::runtime_error("Newtonian analytical point-law 64-epsilon window");
    std::cout<<"NEWTONIAN_CONSTITUTIVE_POINT cases="<<cases.size()<<" max_error="<<maximum<<'\n';
}

} // namespace ViscousGeometryCases
