/**
 * @file test_flux_limiter.cpp
 * @brief Common flux constraints, zero-trace continuity and conservative mixing.
 *
 * Workflow:
 * 1. Independently construct the Lax-Friedrichs flux and its fluid/species bar states.
 * 2. Recover theta from the emitted flux and check Q_s +/- theta*D_s >= 0,
 *    strict fluid admissibility and sum(species flux) == mass flux.
 * 3. Exercise the finite-switch witness, both signs of real violations,
 *    physical face reflection and density/energy scaling through 1e-100.
 *
 * The shared 64-epsilon trace band is unchanged. Reflections exchange the cells
 * and reverse normal velocity, rather than merely swapping the input states.
 */
#include "physics/boundary/BoundaryDiagnostics.h"
#include "physics/boundary/BoundaryFlux.h"
#include "grid/Grid.h"
#include "numerics/diffusion/DiffFlux.h"
#include "numerics/flux/InvariantDomainFlux.h"
#include "numerics/flux/StationarySlipWallFlux.h"
#include "physics/eos/IdealGas.h"
#include "numerics/flux/FluxHLLC.h"
#include "numerics/state/StateAdmissibility.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cfenv>
#include <cstdio>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <vector>

namespace {

constexpr int kDim = 0;      // x-direction face
constexpr int kSpecies = 2;
// Frozen shared trace band, 64*double epsilon. The leaf asserts it was not
// retuned; it is used below as the only slack for exact algebraic identities.
constexpr double kBand = arch::state::composition_roundoff_limit;
const IdealGasView kEos{};   // gamma = 1.4, composition-independent

int checks = 0;
int failures = 0;

void check(bool ok, const char* what)
{
    ++checks;
    if (!ok) {
        std::cerr << "FAIL " << what << '\n';
        ++failures;
    }
}

FluidVector conserved(double rho, double u, double p)
{ return {rho, rho * u, 0.0, 0.0, p / 0.4 + 0.5 * rho * u * u}; }

double max_abs_diff(const FluidVector& a, const FluidVector& b)
{
    const double d[5] = {a.rho - b.rho, a.mom_u - b.mom_u, a.mom_v - b.mom_v,
                         a.mom_w - b.mom_w, a.eng - b.eng};
    double m = 0.0;
    for (double v : d) m = std::max(m, std::abs(v));
    return m;
}

bool finite_state(const FluidVector& a)
{
    return std::isfinite(a.rho) && std::isfinite(a.mom_u) && std::isfinite(a.mom_v)
        && std::isfinite(a.mom_w) && std::isfinite(a.eng);
}

// Mirror of an emitted conservative state across the face plane (x -> -x):
// density, internal energy and tangential momentum are kept, the normal
// momentum component is negated.
FluidVector reflect_state(const FluidVector& u)
{ return {u.rho, -u.mom_u, u.mom_v, u.mom_w, u.eng}; }

// The same mirror on a normal-direction flux: mass, energy and tangential
// momentum flux flip sign, the normal-momentum flux is preserved.
FluidVector reflect_flux(const FluidVector& f)
{ return {-f.rho, f.mom_u, -f.mom_v, -f.mom_w, -f.eng}; }

// One 64eps band relative to the magnitudes under comparison. Deliberately the
// frozen trace band, never a new tolerance knob.
double band_of(double a, double b)
{ return kBand * std::max(std::abs(a), std::abs(b)); }

bool close_rel(double actual, double expected)
{ return std::abs(actual - expected) <= band_of(actual, expected); }

// Value inside the closed span of two emitted values, up to the frozen band.
bool in_span(double low, double value, double high)
{
    const double band = kBand * std::max(std::abs(low), std::max(std::abs(high), std::abs(value)));
    return value >= std::min(low, high) - band && value <= std::max(low, high) + band;
}

// Independently recomputed face quantities of the frozen formula.
struct Face {
    double a = 0.0;
    FluidVector fl{}, fr{}, low{}, bar{};
    double low_species[kSpecies]{}, bar_species[kSpecies]{};
    double Qbar[kSpecies]{}, D[kSpecies]{};
};

Face prepare(const FluidVector& L, const FluidVector& R, const double* xL,
             const double* xR, const FluidVector& high, const double* species_flux)
{
    Face f;
    double pL = 0.0, cL = 0.0, pR = 0.0, cR = 0.0;
    FluxAdmissibility::required_mean_thermo(L, xL, kEos, pL, cL);
    FluxAdmissibility::required_mean_thermo(R, xR, kEos, pR, cR);
    f.a = std::max(std::abs(get_un(L, kDim)) + cL, std::abs(get_un(R, kDim)) + cR);
    f.fl = get_flux(L, pL, kDim);
    f.fr = get_flux(R, pR, kDim);
    f.low = 0.5 * f.fl + 0.5 * f.fr - (0.5 * f.a) * (R - L);
    f.bar = 0.5 * L + 0.5 * R - (0.5 / f.a) * (f.fr - f.fl);
    const double c_rho = (f.low.rho - high.rho) / f.a;
    for (int s = 0; s < kSpecies; ++s) {
        const double ql = L.rho * xL[s], qr = R.rho * xR[s];
        const double fl_s = f.fl.rho * xL[s], fr_s = f.fr.rho * xR[s];
        f.low_species[s] = 0.5 * fl_s + 0.5 * fr_s - 0.5 * f.a * (qr - ql);
        f.bar_species[s] = 0.5 * ql + 0.5 * qr - (0.5 / f.a) * (fr_s - fl_s);
        f.Qbar[s] = f.bar_species[s] + kBand * f.bar.rho;
        f.D[s] = (f.low_species[s] - species_flux[s]) / f.a + kBand * c_rho;
    }
    return f;
}

FluidVector limit(const FluidVector& L, const FluidVector& R, const double* xL,
                  const double* xR, const FluidVector& high,
                  const double* species_flux, double* species_out)
{
    FluidVector limited = high;
    for (int s = 0; s < kSpecies; ++s) species_out[s] = species_flux[s];
    FluxAdmissibility::limit_face(L, R, xL, xR, kSpecies, kEos, kDim, limited, species_out);
    return limited;
}

// Recovered shared theta from the emitted species split of the zero-mean-trace
// cases, where low_species[s] is exactly zero.
double theta_from_species(const Face& f, int s, double species_high, double species_limited)
{
    const double denominator = species_high - f.low_species[s];
    return denominator != 0.0 ? (species_limited - f.low_species[s]) / denominator
                              : std::numeric_limits<double>::quiet_NaN();
}

// Independent shifted-cone inequality: Qbar_s +/- theta*D_s >= 0, with only the
// frozen 64eps band as slack. theta is the value recovered from the output.
bool cone_holds(const Face& f, int s, double theta)
{
    const double d = theta * f.D[s];
    const double band = kBand * std::max(std::abs(f.Qbar[s]), std::abs(d));
    return f.Qbar[s] + d >= -band && f.Qbar[s] - d >= -band;
}

// Retired zero-trace switch, reconstructed only to prove the leaf rejects it.
// This is not called by production; the assertions compare the measured output
// against this prediction.
struct RetiredPrediction {
    FluidVector flux;
    double theta = 0.0;
};

RetiredPrediction retired_zero_trace_switch(const Face& f, const FluidVector& high,
                                            const double* species_flux, double euler_theta)
{
    double theta = euler_theta;
    for (int s = 0; s < kSpecies; ++s) {
        const double deviation = std::abs((f.low_species[s] - species_flux[s]) / f.a);
        if (deviation > f.bar_species[s])
            theta = std::min(theta, std::max(0.0, f.bar_species[s]) / deviation);
    }
    RetiredPrediction p;
    p.theta = theta;
    p.flux = theta == 0.0 ? f.low : f.low + theta * (high - f.low);
    return p;
}

// Euler segment bound of the frozen density/internal-energy branch, evaluated
// with the shared recover()-based leaf (not the function under test).
double euler_theta_reference(const Face& f, const FluidVector& high)
{
    const FluidVector correction = (f.low - high) / f.a;
    return std::min(FluxAdmissibility::segment_fraction(f.bar, correction),
                    FluxAdmissibility::segment_fraction(f.bar, -1.0 * correction));
}

// Strict density/internal-energy check of the fluid blend, independent of
// segment_fraction: both bar-side states of the applied theta must recover.
void check_fluid_blend(const Face& f, const FluidVector& high, double theta,
                       const char* what)
{
    const FluidVector correction = (f.low - high) / f.a;
    check(FluxAdmissibility::valid(f.bar + theta * correction),
          what /* bar + theta*C admissible */);
    check(FluxAdmissibility::valid(f.bar - theta * correction),
          what /* bar - theta*C admissible */);
}

const char* kConePlus = "shifted cone bar + theta*C";

/** Independent dyadic rarefaction reference: gm=2^-10 and v/c=2^10
 * give b=1/2 and 2*gamma/gm=2050 exactly, so P_wall=P*2^-2050.
 * These point-range witnesses have finite compatible rho/momentum/energy;
 * neither a Runtime wall identity nor a complete EOS-range grant is inferred.
 */
void test_gamma_wall_positive_product_range() {
    const double gm=std::ldexp(1.,-10),speed=1.,away=std::ldexp(1.,10);
    const double pressure=std::ldexp(1.,1000);
    const double rho=(1.+gm)*pressure;
    const double momentum=rho*away;
    const double thermal=pressure/gm;
    const double energy=thermal+.5*momentum*away;
    check(std::isfinite(rho)&&rho>0.&&std::isfinite(momentum)
        &&std::isfinite(thermal)&&thermal>0.&&std::isfinite(energy),
        "dyadic rarefaction inputs have finite compatible gas conservative state");
    double output=123.;
    check(StationarySlipWallFlux::gamma_wall_pressure(gm,pressure,speed,away,output)
        &&output==std::ldexp(1.,-1050),
        "positive representable wall pressure survives an underflowing inner exponential");
    output=123.;
    check(StationarySlipWallFlux::gamma_wall_pressure(gm,std::ldexp(1.,976),speed,away,output)
        &&output==std::numeric_limits<double>::denorm_min(),
        "independent exact dyadic wall pressure reaches minimum positive subnormal");
    output=123.;
    // Exact 2^-1075 is halfway between 0 and min-subnormal; round-to-nearest
    // ties-to-even returns zero, which is not a representable positive pressure.
    check(!StationarySlipWallFlux::gamma_wall_pressure(gm,std::ldexp(1.,975),speed,away,output)
        &&output==123.,"unrepresentable positive wall pressure rejects without a floor or output write");
    output=123.;
    check(StationarySlipWallFlux::gamma_wall_pressure(gm,pressure,speed,2.*away,output)
        &&output==0.,"actual b<=0 vacuum retains exact zero traction");
    // This engineering check repeats the old direct expression ONLY to check
    // unchanged ordinary-range output bits; it is not an independent EOS oracle.
    for(double velocity:{0.,.0125,-.025}) {
        const double ordinary_gm=.4,p=.00625,c=.1;
        double expected=p;
        if(velocity>0.) {
            const double decrement=.5*ordinary_gm*(velocity/c);
            const double exponent=(2.*(ordinary_gm+1.))/ordinary_gm;
            expected=p*std::exp(exponent*std::log1p(-decrement));
        } else if(velocity<0.) {
            const double gamma=ordinary_gm+1.,mach=velocity/c;
            const double K=.5*gamma*(gamma+1.)*mach*mach;
            const double B=ordinary_gm/(gamma+1.);
            expected=p*(1.+.5*K+std::hypot(.5*K,std::sqrt(K*(1.+B))));
        }
        output=123.;
        check(StationarySlipWallFlux::gamma_wall_pressure(ordinary_gm,p,c,velocity,output)
            &&std::bit_cast<std::uint64_t>(output)==std::bit_cast<std::uint64_t>(expected),
            "ordinary rarefaction shock and at-rest pressure bits retain original arithmetic");
    }
}

/** Actual third orthonormal direction uses the same gamma-law point solve.
 * Independent long-double rarefaction/shock relations give pressure. This is
 * direction/component portability only; it grants no ordinary wall authority.
 */
void test_shared_gamma_wall_third_direction() {
    const double xi[kSpecies]{.75,.25};
    constexpr double rho=2.,normal_velocity=.125,pressure=.025;
    const long double gamma=1.4L,gm1=.4L;
    const long double sound=std::sqrt(gamma*static_cast<long double>(pressure)/rho);
    const long double mach=normal_velocity/sound;
    const long double rare=static_cast<long double>(pressure)
        *std::pow(1.L-gm1*mach/2.L,2.L*gamma/gm1);
    const long double K=gamma*(gamma+1.L)*mach*mach/2.L,B=gm1/(gamma+1.L);
    const long double shock=static_cast<long double>(pressure)
        *(1.L+K/2.L+std::sqrt(K*K/4.L+K*(1.L+B)));
    const FluidVector point{rho,0.,0.,rho*normal_velocity,
        pressure/.4+.5*rho*normal_velocity*normal_velocity};
    for(int side:{0,1}) {
        FluidVector flux{123.,234.,345.,456.,567.};
        check(StationarySlipWallFlux::point_wall_flux(point,xi,kEos,2,side,
            pressure,double(sound),flux),"third-direction stationary gamma-law wall resolves");
        check(close_rel(flux.mom_w,double(side==0?rare:shock)),
            "third-direction pressure matches independent rarefaction/shock reference");
        check(flux.rho==0.&&flux.mom_u==0.&&flux.mom_v==0.&&flux.eng==0.,
            "third-direction wall has zero mass/work/tangential advection");
        for(int s=0;s<kSpecies;++s)check(flux.rho*xi[s]==0.,
            "third-direction impermeability gives zero constant-fraction species advection");
        check(xi[0]==.75&&xi[1]==.25,"point-wall leaf modified borrowed composition");
    }
    FluidVector resting{rho,0.,0.,0.,pressure/.4},flux{};
    check(StationarySlipWallFlux::point_wall_flux(resting,xi,kEos,2,0,
        pressure,double(sound),flux)&&flux.mom_w==pressure,
        "third-direction exact rest preserves the original pressure bits");
    const FluidVector sentinel{123.,234.,345.,456.,567.};
    for(const auto invalid:std::array<std::array<int,2>,3>{{{{-1,0}},{{3,0}},{{2,2}}}}) {
        flux=sentinel;
        check(!StationarySlipWallFlux::point_wall_flux(point,xi,kEos,invalid[0],invalid[1],
            pressure,double(sound),flux)&&max_abs_diff(flux,sentinel)==0.,
            "invalid wall direction/side rejects without publishing");
    }
    // All input reads precede publication, including actual third momentum.
    auto alias=point;
    check(StationarySlipWallFlux::point_wall_flux(alias,xi,kEos,2,0,
        pressure,double(sound),alias)&&close_rel(alias.mom_w,double(rare))&&alias.eng==0.,
        "third-direction point/output alias preserves physical input evaluation");
}

/** All three point directions bind the same exact low and original factor.
 * Independent rarefaction/shock formulas and explicit one-sided bars supply
 * the physical oracle; bad optional high is distinct from a bad required base.
 * This is point algebra, not a whole-native stage/Runtime wall certificate.
 */
void test_shared_gamma_selected_point_blend() {
    const double xi[kSpecies]{.75,.25},zero[kSpecies]{0.,0.};
    constexpr double speed_normal=.125,pressure=.00625;
    const long double gamma=1.4L,gm1=.4L,c=std::sqrt(gamma*pressure);
    const long double mach=speed_normal/c;
    const long double rare=pressure*std::pow(1.L-gm1*mach/2.L,2.L*gamma/gm1);
    const long double K=gamma*(gamma+1.L)*mach*mach/2.L,B=gm1/(gamma+1.L);
    const long double shock=pressure*(1.L+K/2.L+std::sqrt(K*K/4.L+K*(1.L+B)));
    const auto point=[](int dir,double velocity,double energy) {
        FluidVector value{1.,0.,0.,0.,energy};
        if(dir==0)value.mom_u=velocity;else if(dir==1)value.mom_v=velocity;else value.mom_w=velocity;
        return value;
    };
    const auto traction=[](int dir,double p) {
        FluidVector value{};
        if(dir==0)value.mom_u=p;else if(dir==1)value.mom_v=p;else value.mom_w=p;
        return value;
    };
    const double energy=pressure/.4+.5*speed_normal*speed_normal;
    const double nan=std::numeric_limits<double>::quiet_NaN();
    for(int dir:{0,1,2})for(int side:{0,1}) {
        const double un_left=side==0?-speed_normal:speed_normal;
        const double un_right=-un_left;
        const auto left=point(dir,un_left,energy),right=point(dir,un_right,energy);
        const auto expected=traction(dir,double(side==0?rare:shock));
        double baseline[kSpecies]{123.,456.};
        const auto factor=FluxAdmissibility::selected_wall_point_blend(
            left,right,xi,xi,kSpecies,pressure,double(c),pressure,double(c),
            kEos,dir,side,expected,zero,baseline);
        check(factor.valid,"three-direction exact wall selected baseline is admissible");
        check(max_abs_diff(factor.low,expected)<=kBand*std::max(std::abs(expected.mom_u),
            std::max(std::abs(expected.mom_v),std::abs(expected.mom_w))),
            "three-direction selected wall low matches independent shock/rarefaction traction");
        check(baseline[0]==0.&&baseline[1]==0.,"selected wall uses zero baseline species flux");
        // Direct independent physical fluxes, never a producer factor oracle.
        const auto physical=[&](double velocity) {
            auto value=traction(dir,velocity*velocity+pressure);
            value.rho=velocity;value.eng=velocity*(energy+pressure);return value;
        };
        const double a=speed_normal+double(c);
        const auto bar_left=left+(physical(un_left)-expected)/a;
        const auto bar_right=right+(expected-physical(un_right))/a;
        check(FluxAdmissibility::valid(bar_left)&&FluxAdmissibility::valid(bar_right),
            "independent exact selected wall has two admissible physical bars");
        const FluidVector bad_high{nan,0.,0.,0.,nan};const double bad_species[kSpecies]{nan,nan};
        const auto rejected_high=FluxAdmissibility::selected_wall_point_blend(
            left,right,xi,xi,kSpecies,pressure,double(c),pressure,double(c),kEos,
            dir,side,bad_high,bad_species,baseline);
        check(rejected_high.valid&&rejected_high.theta==0.
            &&max_abs_diff(rejected_high.low,factor.low)==0.,
            "optional bad high contracts to theta0 of the identical canonical wall low");
        for(int failure:{0,1,2}) {
            baseline[0]=123.;baseline[1]=456.;
            const double bad_xi[kSpecies]{nan,.25};
            const auto bad=FluxAdmissibility::selected_wall_point_blend(
                left,right,failure==2?bad_xi:xi,xi,kSpecies,
                failure==0?nan:pressure,failure==1?nan:double(c),pressure,double(c),
                kEos,dir,side,expected,zero,baseline);
            check(!bad.valid&&baseline[0]==123.&&baseline[1]==456.,
                "malformed required wall pressure/acoustics/species rejects before scratch publication");
        }
    }
    // All three aliases would overwrite required inputs when zeroing low X.
    // They must reject before writing, without adding a numerical tolerance.
    for(int alias_kind:{0,1,2}) {
        double xl[kSpecies]{.75,.25},xr[kSpecies]{.75,.25},high_species[kSpecies]{0.,0.};
        const auto left=point(2,-speed_normal,energy),right=point(2,speed_normal,energy);
        const auto high=traction(2,double(rare));
        double* scratch=alias_kind==0?xl:alias_kind==1?xr:high_species;
        const auto factor=FluxAdmissibility::selected_wall_point_blend(
            left,right,xl,xr,kSpecies,pressure,double(c),pressure,double(c),kEos,
            2,0,high,high_species,scratch);
        check(!factor.valid&&xl[0]==.75&&xl[1]==.25&&xr[0]==.75&&xr[1]==.25
            &&high_species[0]==0.&&high_species[1]==0.,
            "selected wall scratch overlap rejects before required species inputs change");
    }
    IdealGasView stiff=kEos;stiff.global_gamma=10.;
    const long double stiff_c=std::sqrt(10.L),u=-10.L*stiff_c;
    const long double stiff_K=10.L*11.L*100.L/2.L,stiff_B=9.L/11.L;
    const double stiff_wall=double(1.L+stiff_K/2.L+std::sqrt(stiff_K*stiff_K/4.L+stiff_K*(1.L+stiff_B)));
    const double stiff_energy=double(1.L/9.L+.5L*u*u);
    for(int dir:{0,1,2}) {
        const auto left=point(dir,-double(u),stiff_energy),right=point(dir,double(u),stiff_energy);
        const auto high=traction(dir,stiff_wall);double baseline[kSpecies]{123.,456.};
        const auto factor=FluxAdmissibility::selected_wall_point_blend(
            left,right,xi,xi,kSpecies,1.,double(stiff_c),1.,double(stiff_c),
            stiff,dir,0,high,zero,baseline);
        check(!factor.valid,"stiff gamma10 compressive wall rejects invalid two-sided fixed-a base");
    }
}

/** Actual ordinary Host sweeps consume given physical ghost points and flat
 * wall metadata. This is a numerical producer witness, not Runtime authority.
 * PCM cold rare/shock tractions have independent long-double references;
 * selected MC/PPM at rest retain the exact physical pressure/zero advection.
 */
template<class Reconstruction>
void ordinary_wall_sweep_case(int direction,double velocity,bool analytic) {
    Grid grid(amr::MAX_NG,1.,3.,-1.,1.,-1.,1.);
    grid.dim=direction+1;grid.geometry="cartesian";grid.InitializeTopology();
    FluidState state;state.Preallocate(grid.GetTotalSize());state.InitSpecies(kSpecies);
    constexpr double pressure=.00625;
    const double energy=pressure/.4+.5*velocity*velocity;
    for(int cell=0;cell<grid.GetTotalSize();++cell) {
        state.set(cell,{1.,0.,0.,0.,energy});state.X(0,cell)=.75;state.X(1,cell)=.25;
    }
    for(int k=0;k<grid.GetTotalZ();++k)for(int j=0;j<grid.GetTotalY();++j)
        for(int i=0;i<grid.GetTotalX();++i) {
            const int position=direction==0?i:direction==1?j:k;
            const int lower=direction==0?grid.Is():direction==1?grid.Js():grid.Ks();
            const int upper=direction==0?grid.Ie():direction==1?grid.Je():grid.Ke();
            const double normal=position<lower||position>=upper?-velocity:velocity;
            auto point=state.get(grid.GetIndex(i,j,k));
            if(direction==0)point.mom_u=normal;else if(direction==1)point.mom_v=normal;else point.mom_w=normal;
            state.set(grid.GetIndex(i,j,k),point);
        }
    const auto before=state;
    const int total=grid.GetTotalSize();
    std::vector<FluidVector> actual(total),plain(total);
    std::vector<double> species(total*kSpecies),plain_species(total*kSpecies);
    FluxAdmissibility::MeanThermoCache cache;cache.reset(total);
    cache.hydro_boundary.reflecting[2*direction]=true;cache.hydro_boundary.reflecting[2*direction+1]=true;
    FluxHLLC<Reconstruction>::compute_fluxes(state,kEos,grid,actual,species,direction,0.,&cache);
    FluxAdmissibility::MeanThermoCache empty;empty.reset(total);
    FluxHLLC<Reconstruction>::compute_fluxes(state,kEos,grid,plain,plain_species,direction,0.,&empty);
    const long double gamma=1.4L,gm1=.4L,sound=std::sqrt(gamma*pressure),mach=velocity/sound;
    const long double rare=pressure*std::pow(1.L-gm1*mach/2.L,2.L*gamma/gm1);
    const long double K=gamma*(gamma+1.L)*mach*mach/2.L,B=gm1/(gamma+1.L);
    const long double shock=pressure*(1.L+K/2.L+std::sqrt(K*K/4.L+K*(1.L+B)));
    for(int side:{0,1}) {
        int i=grid.Is(),j=grid.Js(),k=grid.Ks();
        const int face=direction==0?(side?grid.Ie():grid.Is()):direction==1?
            (side?grid.Je():grid.Js()):(side?grid.Ke():grid.Ks());
        if(direction==0)i=face;else if(direction==1)j=face;else k=face;
        const int index=grid.GetIndex(i,j,k);const auto& f=actual[index];
        const double normal=direction==0?f.mom_u:direction==1?f.mom_v:f.mom_w;
        check(std::isfinite(normal)&&normal>0.,"ordinary actual selected wall retains positive physical traction");
        if(analytic)check(close_rel(normal,double(side==0?rare:shock)),
            "ordinary actual PCM rare/shock traction matches independent gamma invariant");
        check(f.rho==0.&&f.eng==0.&&(direction==0||f.mom_u==0.)
            &&(direction==1||f.mom_v==0.)&&(direction==2||f.mom_w==0.),
            "ordinary actual wall has exact zero mass/work/tangential advection");
        for(int s=0;s<kSpecies;++s)check(species[s*total+index]==0.,
            "ordinary actual wall publishes exact zero species advection");
    }
    // Nonwall numerical bytes cannot change from merely present root flags.
    const auto same=[](double a,double b){return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);};
    for(int k=grid.Ks();k<grid.Ke();++k)for(int j=grid.Js();j<grid.Je();++j)
        for(int i=grid.Is();i<grid.Ie();++i) {
            const int coordinate=direction==0?i:direction==1?j:k;
            const int lower=direction==0?grid.Is():direction==1?grid.Js():grid.Ks();
            if(coordinate==lower)continue; // exclude the actual lower-wall slot.
            const int index=grid.GetIndex(i,j,k);const auto& a=actual[index];const auto& b=plain[index];
            check(same(a.rho,b.rho)&&same(a.eng,b.eng)&&same(a.mom_u,b.mom_u)
                &&same(a.mom_v,b.mom_v)&&same(a.mom_w,b.mom_w),
                "ordinary nonwall selected flux remains byte identical");
            for(int species_index=0;species_index<kSpecies;++species_index)
                check(same(species[species_index*total+index],plain_species[species_index*total+index]),
                    "ordinary nonwall species flux remains byte identical");
        }
    check(state.rho==before.rho&&state.mom_u==before.mom_u&&state.mom_v==before.mom_v
        &&state.mom_w==before.mom_w&&state.eng==before.eng&&state.mass_fractions==before.mass_fractions,
        "ordinary wall sweep changed immutable source arrays");
}
void test_ordinary_wall_sweeps() {
    for(int direction:{0,1,2}) {
        ordinary_wall_sweep_case<PCMReconstruction>(direction,.125,true);
        ordinary_wall_sweep_case<MusclReconstruction<McLimiter>>(direction,0.,true);
        ordinary_wall_sweep_case<PPMReconstruction>(direction,0.,true);
    }
    Grid grid(amr::MAX_NG,1.,3.,0.,1.,0.,1.);grid.dim=1;grid.InitializeTopology();
    FluidState state;state.Preallocate(grid.GetTotalSize());state.InitSpecies(kSpecies);
    for(int cell=0;cell<grid.GetTotalSize();++cell){state.set(cell,{1.,0.,0.,0.,1.});state.X(0,cell)=.75;state.X(1,cell)=.25;}
    const FluidVector sentinel{123.,234.,345.,456.,567.};
    std::vector<FluidVector> flux(grid.GetTotalSize(),sentinel);
    std::vector<double> species(kSpecies*grid.GetTotalSize(),987.);
    FluxAdmissibility::MeanThermoCache cache;cache.reset(grid.GetTotalSize());
    cache.hydro_boundary.reflecting[2]=true;bool rejected=false;
    try{FluxHLLC<PCMReconstruction>::compute_fluxes(state,kEos,grid,flux,species,0,0.,&cache);}
    catch(const std::invalid_argument&){rejected=true;}
    check(rejected&&max_abs_diff(flux[grid.Is()],sentinel)==0.&&species[grid.Is()]==987.,
        "inactive ordinary wall flags reject before numerical publication");
}

/** Exercise extracted point assembly with the actual HLLC high policy.
 * True B fractions are deliberately distinct from reconstructed fractions;
 * their read occurs only after the high callback, retaining old EOS order.
 * This is shared point wiring/atomicity, not a Runtime boundary certificate.
 */
void test_shared_point_wall_assembler() {
    using Status=FluxAdmissibility::PointWallStatus;
    constexpr double pressure=.00625;
    const double sound=std::sqrt(1.4*pressure),true_xi[2]{.75,.25};
    const FluidVector point{1.,0.,0.,0.,pressure/.4};
    for(int dir=0;dir<3;++dir)for(int side=0;side<2;++side)for(int trial=0;trial<2;++trial) {
        double left[2]{.25,.75},right[2]{.25,.75},low[2]{123.,456.},high_species[2]{123.,456.};
        bool high_seen=false;int base_reads=0;
        const auto read_base=[&](int species) {
            check(high_seen,"true base composition is read after optional high policy");
            ++base_reads;return true_xi[species];
        };
        const auto high_compute=[&](const FluidVector& L,const FluidVector& R,
            const double* xl,const double* xr,const auto& eos,FluidVector& high,double* species) {
            high_seen=true;
            check(xl[0]==.25&&xr[0]==.25,"selected high receives reconstructed composition");
            if(trial)throw std::runtime_error("documented optional high EOS failure");
            FluxHLLC<PCMReconstruction>::compute_face_flux(L,R,xl,xr,2,eos,dir,1.,high,species);
        };
        FluidVector output{123.,456.,789.,123.,456.};
        const auto status=FluxAdmissibility::assemble_wall_point(point,point,point,point,
            dir,side,2,pressure,sound,kEos,left,right,low,high_species,read_base,high_compute,output);
        check(status==Status::valid&&high_seen&&base_reads==2,
            "shared point wall consumes original high then true base exactly once");
        check(output.rho==0.&&output.eng==0.&&high_species[0]==0.&&high_species[1]==0.,
            "shared point wall publishes its actual zero-advection selected blend");
        check(std::abs((dir==0?output.mom_u:dir==1?output.mom_v:output.mom_w)-pressure)
                <=kBand*pressure,
            "at-rest actual selected wall retains physical pressure traction in original rounding band");
        check(left[0]==.75&&right[0]==.75,"required wall factor uses true mean composition");
    }
    double left[2]{.25,.75},right[2]{.25,.75},low[2]{123.,456.},high_species[2]{123.,456.};
    int high_calls=0;
    const auto read_base=[&](int species){return true_xi[species];};
    const auto high_compute=[&](const FluidVector& L,const FluidVector& R,
        const double* xl,const double* xr,const auto& eos,FluidVector& high,double* species) {
        ++high_calls;FluxHLLC<PCMReconstruction>::compute_face_flux(L,R,xl,xr,2,eos,0,1.,high,species);
    };
    const FluidVector sentinel{123.,456.,789.,123.,456.};auto output=sentinel;
    check(FluxAdmissibility::assemble_wall_point(point,point,point,point,3,0,2,
        pressure,sound,kEos,left,right,low,high_species,read_base,high_compute,output)
            ==Status::invalid_metadata&&high_calls==0&&max_abs_diff(output,sentinel)==0.,
        "shared wall invalid direction rejects before callback/output publication");
    check(FluxAdmissibility::assemble_wall_point(point,point,point,point,0,0,2,
        -pressure,sound,kEos,left,right,low,high_species,read_base,high_compute,output)
            !=Status::valid&&max_abs_diff(output,sentinel)==0.,
        "invalid actual baseline pressure rejects without fluid publication");
}

/** Analytic stiffened-gas closure, deliberately without gamma-law capability.
 * p=(g-1)*rho*e-g*p_inf and c^2=g*(p+p_inf)/rho; e=T+p_inf/rho
 * sets cv=1. The pressure offset makes this a real non-gamma-law test EOS,
 * not a replacement Helm model. Only grouped optional trials can be injected.
 */
struct StiffenedWallEos {
    static constexpr double gamma=1.5,pressure_offset=1./32.;
    bool trial=false,fail_trial=false;
    bool* trial_seen=nullptr;
    int* trial_queries=nullptr;
    double* trial_fraction=nullptr;

    StiffenedWallEos candidate_view() const {
        auto result=*this;result.trial=true;return result;
    }
    double get_pressure_from_rho_e(double rho,double e,const double*) const {
        return (gamma-1.)*rho*e-gamma*pressure_offset;
    }
    double get_pressure(const FluidVector& point,const double* xi) const {
        const double u=point.mom_u/point.rho,v=point.mom_v/point.rho,w=point.mom_w/point.rho;
        return get_pressure_from_rho_e(point.rho,point.eng/point.rho-.5*(u*u+v*v+w*w),xi);
    }
    double get_sound_speed(const FluidVector& point,double pressure,const double*) const {
        return std::sqrt(gamma*(pressure+pressure_offset)/point.rho);
    }
    void get_pressure_and_sound_speed(double rho,double e,const double* xi,
        double& pressure,double& speed) const {
        if(trial) {
            if(trial_seen)*trial_seen=true;
            if(trial_queries)++*trial_queries;
            if(trial_fraction)*trial_fraction=xi[0];
            if(fail_trial)throw std::runtime_error("documented optional wall EOS trial failure");
        }
        pressure=get_pressure_from_rho_e(rho,e,xi);
        speed=std::sqrt(gamma*(pressure+pressure_offset)/rho);
    }
    double get_temperature(double rho,double e,const double*) const {
        return e-pressure_offset/rho;
    }
    double get_total_energy_primitive(double rho,double u,double v,double w,
        double pressure,const double*) const {
        return (pressure+gamma*pressure_offset)/(gamma-1.)+.5*rho*(u*u+v*v+w*w);
    }
    double get_dp_drho_e(double,double e,const double*) const {return (gamma-1.)*e;}
    double get_dp_de_rho(double rho,double,const double*) const {return (gamma-1.)*rho;}
};

// Independent point and physical flux arithmetic for the selected wall bars.
FluidVector stiffened_wall_point(double rho,double normal,double pressure,int direction) {
    double velocity[3]{1./16.,-1./32.,3./64.};velocity[direction]=normal;
    return {rho,rho*velocity[0],rho*velocity[1],rho*velocity[2],
        (pressure+3./64.)/.5+.5*rho*(velocity[0]*velocity[0]
            +velocity[1]*velocity[1]+velocity[2]*velocity[2])};
}
FluidVector wall_reflection(FluidVector point,int direction) {
    if(direction==0)point.mom_u=-point.mom_u;
    else if(direction==1)point.mom_v=-point.mom_v;else point.mom_w=-point.mom_w;
    return point;
}
FluidVector wall_physical_flux(const FluidVector& point,double pressure,int direction) {
    const double normal=(direction==0?point.mom_u:direction==1?point.mom_v:point.mom_w)/point.rho;
    FluidVector flux{point.rho*normal,point.mom_u*normal,point.mom_v*normal,
        point.mom_w*normal,(point.eng+pressure)*normal};
    if(direction==0)flux.mom_u+=pressure;
    else if(direction==1)flux.mom_v+=pressure;else flux.mom_w+=pressure;
    return flux;
}
long double wall_internal_density(const FluidVector& point) {
    const long double u=point.mom_u,v=point.mom_v,w=point.mom_w;
    return static_cast<long double>(point.eng)-(u*u+v*v+w*w)/(2.L*point.rho);
}
double wall_traction(const FluidVector& flux,int direction) {
    return direction==0?flux.mom_u:direction==1?flux.mom_v:flux.mom_w;
}
void check_wall_zeros(const FluidVector& flux,const double* species,int direction) {
    check(flux.rho==0.&&flux.eng==0.&&(direction==0||flux.mom_u==0.)
        &&(direction==1||flux.mom_v==0.)&&(direction==2||flux.mom_w==0.),
        "general-EOS selected wall has exact zero mass/work/tangential advection");
    for(int s=0;s<kSpecies;++s)check(species[s]==0.,
        "general-EOS selected wall has exact zero species advection");
}

/** Physical-pressure H and L come from distinct actual EOS inputs. Inspect
 * emitted bars independently, including a large finite H that needs limiting.
 * The distinct opposite donor is not used instead of the true interior point.
 */
void test_general_eos_point_wall_assembler() {
    using Status=FluxAdmissibility::PointWallStatus;
    constexpr double pressure=.25,rho=2.,normal_speed=.125;
    const double sound=std::sqrt(1.5*(pressure+1./32.)/rho),true_xi[2]{.75,.25};
    const FluidVector sentinel{123.,456.,789.,123.,456.};
    for(int direction=0;direction<3;++direction)for(int side=0;side<2;++side)
        for(double sign:{-1.,1.})for(double high_pressure:{.375,4.})for(bool fail:{false,true}) {
        const double normal=(side==0?sign:-sign)*normal_speed;
        const auto base=stiffened_wall_point(rho,normal,pressure,direction);
        const auto high_point=stiffened_wall_point(1.5,normal,high_pressure,direction);
        const auto decoy=stiffened_wall_point(3.,-.25,.625,direction);
        bool trial_seen=false;int trial_queries=0,base_reads=0,policy_calls=0;double trial_fraction=0.;
        StiffenedWallEos eos;eos.fail_trial=fail;eos.trial_seen=&trial_seen;
        eos.trial_queries=&trial_queries;eos.trial_fraction=&trial_fraction;
        double left[2]{side==0?.625:.25,side==0?.375:.75};
        double right[2]{side==0?.25:.625,side==0?.75:.375};
        double low_species[2]{123.,456.},species_flux[2]{123.,456.};
        const auto read_base=[&](int species) {
            check(trial_seen,"general-EOS true mean species are consumed after the optional physical-point EOS");
            ++base_reads;return true_xi[species];
        };
        const auto forbidden_policy=[&](const auto&,const auto&,const auto*,const auto*,
            const auto&,auto&,auto*) {++policy_calls;};
        auto output=sentinel;
        const auto status=FluxAdmissibility::assemble_wall_point(side==0?decoy:base,
            side==0?base:decoy,side==0?decoy:high_point,side==0?high_point:decoy,
            direction,side,kSpecies,pressure,sound,eos,left,right,low_species,species_flux,
            read_base,forbidden_policy,output);
        check(status==Status::valid&&trial_seen&&trial_queries==1&&base_reads==kSpecies
            &&policy_calls==0&&trial_fraction==.25,
            "general-EOS wall queries true H once and bypasses the mirrored selected high policy");
        if(status!=Status::valid)continue;
        check(left[0]==.75&&right[0]==.75&&left[1]==.25&&right[1]==.25,
            "general-EOS wall baseline uses immutable true mean composition on both sides");
        check_wall_zeros(output,species_flux,direction);
        const double emitted=wall_traction(output,direction);
        const double theta=fail?0.:(emitted-pressure)/(high_pressure-pressure);
        check(std::isfinite(theta)&&theta>=0.&&theta<=1.,
            "general-EOS pressure is the single selected low/high convex blend");
        if(fail)check(emitted==pressure,"optional general-EOS failure selects actual mean pressure baseline");
        else if(high_pressure==4.)check(theta>0.&&theta<1.,
            "large finite reconstructed pressure activates the original joint limiter");
        else check(close_rel(emitted,high_pressure),
            "admissible reconstructed pressure is retained instead of the mean pressure");
        const auto L=side==0?wall_reflection(base,direction):base;
        const auto R=side==0?base:wall_reflection(base,direction);
        const auto fl=wall_physical_flux(L,pressure,direction),fr=wall_physical_flux(R,pressure,direction);
        const double a=std::abs(normal)+sound;
        FluidVector low{};if(direction==0)low.mom_u=pressure;
        else if(direction==1)low.mom_v=pressure;else low.mom_w=pressure;
        const auto bl=L+(fl-low)/a,br=R+(low-fr)/a;
        const auto accepted_left=L+(fl-output)/a,accepted_right=R+(output-fr)/a;
        check(bl.rho>0.&&br.rho>0.&&wall_internal_density(bl)>0.L&&wall_internal_density(br)>0.L,
            "independently recomputed selected physical-pressure baseline has two positive bars");
        check(accepted_left.rho>0.&&accepted_right.rho>0.
            &&wall_internal_density(accepted_left)>0.L&&wall_internal_density(accepted_right)>0.L,
            "actual emitted general-EOS wall flux has two independently positive bars");
        const double signed_away=sign*normal_speed,b=a-signed_away;
        const long double e=wall_internal_density(base)/rho;
        const double expected_internal=double(e-signed_away*pressure/(rho*b));
        check(close_rel(double(wall_internal_density(bl)/bl.rho),expected_internal)
            &&close_rel(double(wall_internal_density(br)/br.rho),expected_internal),
            "physical-pressure two-bar internal energy matches the independent signed-away formula");
        for(int s=0;s<kSpecies;++s)check((true_xi[s]+kBand)*accepted_left.rho>=0.
            &&(true_xi[s]+kBand)*accepted_right.rho>=0.,
            "zero species wall flux preserves both actual shifted trace cones");
    }
    // Required cache data and fixed-a baseline rejection stay strict. The cold
    // witness is an EOS-valid input; positivity of P_wall alone cannot pass it.
    for(int direction=0;direction<3;++direction)for(int side=0;side<2;++side) {
        const auto base=stiffened_wall_point(1.,side==0?2.:-2.,1./16.,direction);
        const double cold_sound=.375;
        check(wall_internal_density(base)>0.L,"cold strong-away wall witness has positive input internal energy");
        for(double required_pressure:{1./16.,-1./16.}) {
            double left[2]{.75,.25},right[2]{.75,.25},low[2]{123.,456.},species[2]{123.,456.};
            const auto read_base=[&](int s){return true_xi[s];};
            const auto forbidden_policy=[](const auto&,const auto&,const auto*,const auto*,const auto&,auto&,auto*){};
            auto output=sentinel;
            const auto status=FluxAdmissibility::assemble_wall_point(base,base,base,base,
                direction,side,kSpecies,required_pressure,cold_sound,StiffenedWallEos{},
                left,right,low,species,read_base,forbidden_policy,output);
            check(status==Status::inadmissible_baseline&&max_abs_diff(output,sentinel)==0.,
                "cold strong-away or bad required pressure rejects without raising a or publishing");
        }
        check(7.L/32.L-2.L*(1.L/16.L)/(.375L)<0.L,
            "independent cold strong-away fixed-a physical-pressure bar is really inadmissible");
    }
}

/** Independent stiffened-gas reflected-wave cell integrals. For gamma=3/2,
 * rho=rho0*z^4 and q=(p+p_inf)=q0*z^6 in the expansion fan, so all conserved
 * components have explicit polynomial antiderivatives. No production wall
 * pressure or flux routine supplies this reference. Compression is a single
 * Rankine-Hugoniot shock; the integration splits at its actual position.
 */
std::array<long double,3> stiffened_wall_reference_integral(
    long double lower,long double upper,long double time,long double away) {
    constexpr long double gamma=1.5L,gm=.5L,rho0=1.L,p0=.125L,pinf=1.L/32.L;
    constexpr long double q0=p0+pinf;
    const long double c0=std::sqrt(gamma*q0/rho0);
    const long double far_energy=(p0+gamma*pinf)/gm+.5L*rho0*away*away;
    std::array<long double,3> result{};
    const auto uniform=[&](long double begin,long double end,long double rho,
        long double momentum,long double energy) {
        const long double width=std::max(0.L,std::min(upper,end)-std::max(lower,begin));
        result[0]+=width*rho;result[1]+=width*momentum;result[2]+=width*energy;
    };
    if(away<0.L) {
        const long double mach=away/c0,K=.5L*gamma*(gamma+1.L)*mach*mach;
        const long double B=gm/(gamma+1.L);
        const long double ratio=1.L+K/2.L+std::sqrt(K*K/4.L+K*(1.L+B));
        const long double rho_wall=rho0*(ratio+B)/(B*ratio+1.L);
        const long double shock=-rho0*away/(rho_wall-rho0),position=shock*time;
        uniform(lower,position,rho_wall,0.L,q0*ratio/gm+pinf);
        uniform(position,upper,rho0,rho0*away,far_energy);
        return result;
    }
    const long double cw=c0-.5L*gm*away,z_wall=cw/c0;
    const long double rho_wall=rho0*std::pow(z_wall,4),q_wall=q0*std::pow(z_wall,6);
    const long double tail=cw*time,head=(away+c0)*time;
    uniform(lower,tail,rho_wall,0.L,q_wall/gm+pinf);
    uniform(head,upper,rho0,rho0*away,far_energy);
    const long double begin=std::max(lower,tail),end=std::min(upper,head);
    if(end>begin) {
        const long double d=away-2.L*c0/gm,k=2.L*c0/gm,D=(gamma+1.L)*c0/gm;
        const auto primitive=[&](long double x) {
            const long double z=(2.L*c0+gm*(x/time-away))/((gamma+1.L)*c0);
            const long double z2=z*z,z4=z2*z2,z5=z4*z,z6=z5*z,z7=z6*z;
            return std::array<long double,3>{D*rho0*z5/5.L,
                D*rho0*(d*z5/5.L+k*z6/6.L),
                D*((q0/gm+.5L*rho0*k*k)*z7/7.L+rho0*d*k*z6/6.L
                    +.5L*rho0*d*d*z5/5.L+pinf*z)};
        };
        const auto a=primitive(begin),b=primitive(end);
        for(int field=0;field<3;++field)result[field]+=time*(b[field]-a[field]);
    }
    return result;
}

/** Small actual first-order FV evolution: shared PCM/HLLC, current limiter,
 * left stationary wall and right outflow. Real EOS/trace checks and physical
 * boundary ledgers accompany the independent reflected-wave convergence test.
 * This private loop qualifies the numerical wall, not Runtime/AMR scheduling.
 */
double stiffened_wall_fv_error(int cells,double away) {
    constexpr double final_time=.4,pressure=.125;
    const double dx=1./cells,sound=std::sqrt(1.5*(pressure+1./32.));
    const FluidVector initial{1.,away,0.,0.,(pressure+3./64.)/.5+.5*away*away};
    const StiffenedWallEos eos;
    std::vector<FluidVector> state(cells,initial),next(cells),flux(cells+1);
    std::vector<std::array<double,kSpecies>> fraction(cells,{.75,.25}),next_fraction(cells),species_flux(cells+1);
    std::vector<double> p(cells),c(cells);
    std::array<long double,4> outflow{};
    double time=0.;int face_visits=0;
    while(time<final_time&&face_visits<1000000) {
        double fastest=0.;
        for(int cell=0;cell<cells;++cell) {
            const auto& u=state[cell];
            FluxAdmissibility::required_mean_thermo(u,fraction[cell].data(),eos,p[cell],c[cell]);
            fastest=std::max(fastest,std::abs(u.mom_u/u.rho)+c[cell]);
        }
        check(std::isfinite(fastest)&&fastest>0.,"actual stiffened wall FV has valid acoustic CFL data");
        if(!std::isfinite(fastest)||!(fastest>0.))return std::numeric_limits<double>::infinity();
        const double dt=std::min(.25*dx/fastest,final_time-time);
        double left[kSpecies]{fraction[0][0],fraction[0][1]},right[kSpecies]{left[0],left[1]},low[kSpecies]{};
        const auto base_species=[&](int s){return fraction[0][s];};
        const auto high_policy=[&](const auto& L,const auto& R,const double* xl,const double* xr,
            const auto& trial,FluidVector& high,double* species) {
            FluxHLLC<PCMReconstruction>::compute_face_flux(L,R,xl,xr,kSpecies,trial,0,0.,high,species);
        };
        const auto wall_status=FluxAdmissibility::assemble_wall_point(state[0],state[0],state[0],state[0],
            0,0,kSpecies,p[0],c[0],eos,left,right,low,species_flux[0].data(),base_species,high_policy,flux[0]);
        check(wall_status==FluxAdmissibility::PointWallStatus::valid,
            "actual stiffened wall FV accepts the selected wall baseline");
        if(wall_status!=FluxAdmissibility::PointWallStatus::valid)return std::numeric_limits<double>::infinity();
        check_wall_zeros(flux[0],species_flux[0].data(),0);
        const auto trial=arch::state::candidate_eos(eos);
        for(int face=1;face<=cells;++face) {
            const int l=face-1,r=std::min(face,cells-1);
            FluxAdmissibility::compute_candidate([&] {
                FluxHLLC<PCMReconstruction>::compute_face_flux(state[l],state[r],fraction[l].data(),
                    fraction[r].data(),kSpecies,trial,0,0.,flux[face],species_flux[face].data());
            },flux[face],species_flux[face].data(),kSpecies);
            FluxAdmissibility::limit_face_with_thermo(state[l],state[r],fraction[l].data(),
                fraction[r].data(),kSpecies,p[l],c[l],p[r],c[r],0,flux[face],species_flux[face].data());
        }
        face_visits+=cells+1;
        outflow[0]+=static_cast<long double>(dt)*flux[cells].rho;
        outflow[1]+=static_cast<long double>(dt)*flux[cells].eng;
        for(int s=0;s<kSpecies;++s)outflow[2+s]+=static_cast<long double>(dt)*species_flux[cells][s];
        for(int cell=0;cell<cells;++cell) {
            next[cell]=state[cell]-(dt/dx)*(flux[cell+1]-flux[cell]);
            for(int s=0;s<kSpecies;++s)next_fraction[cell][s]=
                (state[cell].rho*fraction[cell][s]-(dt/dx)*(species_flux[cell+1][s]-species_flux[cell][s]))/next[cell].rho;
            const auto status=arch::state::validate_eos(next[cell],next_fraction[cell].data(),
                kSpecies,arch::state::Bounds{},eos);
            check(status==arch::state::Status::valid,"every actual stiffened wall FV step has positive EOS/closed species states");
            if(status!=arch::state::Status::valid)return std::numeric_limits<double>::infinity();
        }
        state.swap(next);fraction.swap(next_fraction);time+=dt;
    }
    check(time==final_time&&face_visits<1000000,"bounded stiffened reflected wave reaches its original fixed terminal time");
    check(final_time*(sound+3.*std::abs(away))<1.,"independent reflected wave remains away from the right boundary");
    long double error=0.;std::array<long double,4> totals{};
    for(int cell=0;cell<cells;++cell) {
        const auto exact=stiffened_wall_reference_integral(static_cast<long double>(cell)/cells,
            static_cast<long double>(cell+1)/cells,final_time,away);
        error+=std::abs(static_cast<long double>(state[cell].rho)*dx-exact[0]);
        error+=std::abs(static_cast<long double>(state[cell].mom_u)*dx-exact[1])/sound;
        error+=std::abs(static_cast<long double>(state[cell].eng)*dx-exact[2])/(initial.eng+pressure+1./32.);
        totals[0]+=static_cast<long double>(state[cell].rho)*dx;
        totals[1]+=static_cast<long double>(state[cell].eng)*dx;
        for(int s=0;s<kSpecies;++s)totals[2+s]+=static_cast<long double>(state[cell].rho)*fraction[cell][s]*dx;
    }
    const long double expected[4]{1.L,initial.eng,.75L,.25L};
    for(int field=0;field<4;++field)check(std::abs(totals[field]+outflow[field]-expected[field])
        <=kBand*std::max(std::abs(totals[field])+std::abs(outflow[field]),std::abs(expected[field])),
        "actual reflected wave conserves mass/E/species with its measured outflow ledger");
    std::printf("stiffened_wall away=%+.3f cells=%d faces=%d L1=%.9g\n",away,cells,face_visits,double(error));
    return double(error);
}
void test_general_eos_reflected_wave_convergence() {
    for(double away:{-.125,.125}) {
        const double coarse=stiffened_wall_fv_error(64,away),middle=stiffened_wall_fv_error(128,away),
            fine=stiffened_wall_fv_error(256,away);
        check(std::isfinite(coarse)&&std::isfinite(middle)&&std::isfinite(fine)
            &&coarse>middle&&middle>fine&&fine>0.,
            "actual stiffened-gas compression/expansion L1 decreases through three refinements");
    }
}

/** The finite optional H is canonicalized before factor/publication; an
 * unresolved optional trial and invalid pressure must remain unchanged. */
void test_stationary_candidate_projection() {
    using Status=StationarySlipWallFlux::StationaryCandidateStatus;
    for(int dir=0;dir<3;++dir) {
        FluidVector high{3.,2.,2.,2.,4.};double species[2]={.4,-.4};
        check(StationarySlipWallFlux::stationary_candidate(dir,high,species,2)==Status::canonical,
            "finite stationary high canonicalizes before factor");
        check(high.rho==0.&&high.eng==0.&&species[0]==0.&&species[1]==0.,
            "stationary high advective components are exact zero before factor");
        check(high.mom_u==(dir==0?2.:0.)&&high.mom_v==(dir==1?2.:0.)
            &&high.mom_w==(dir==2?2.:0.),"stationary high retains true normal traction");
        FluidVector bad{3.,2.,2.,2.,4.};double unresolved[2]={.4,std::numeric_limits<double>::quiet_NaN()};
        check(StationarySlipWallFlux::stationary_candidate(dir,bad,unresolved,2)==Status::nonfinite_trial
            &&bad.rho==3.&&bad.eng==4.&&unresolved[0]==.4&&std::isnan(unresolved[1]),
            "nonfinite optional species trial is not cleared to success");
        if(dir==0)bad.mom_u=-1.;else if(dir==1)bad.mom_v=-1.;else bad.mom_w=-1.;
        double finite_species[2]={.4,.6};
        check(StationarySlipWallFlux::stationary_candidate(dir,bad,finite_species,2)==Status::invalid
            &&bad.rho==3.&&bad.eng==4.&&finite_species[0]==.4,
            "negative finite wall traction rejects without publication");
    }
}

} // namespace

int main()
{
    check(kBand == 64.0 * std::numeric_limits<double>::epsilon(),
          "production trace band is still 64*double epsilon");

    // Valid ideal-gas means with exactly zero trace fraction in both cells.
    const FluidVector mean_L = conserved(1.0, 0.3, 1.0);
    const FluidVector mean_R = conserved(0.125, -0.1, 0.1);
    const double xm_L[kSpecies] = {1.0, 0.0};
    const double xm_R[kSpecies] = {1.0, 0.0};
    check(FluxAdmissibility::valid(mean_L) && FluxAdmissibility::valid(mean_R),
          "cell means are admissible ideal-gas states");

    bool ok = failures == 0;

    // ---- Case A: zero/tiny reconstructed trace over zero mean trace. --------
    const double traces[] = {0.0, 1e-16, 1e-30, 1e-100, 1e-189,
                             std::numeric_limits<double>::denorm_min()};
    FluidVector high_first;
    for (double e : traces) {
        const double xr_L[kSpecies] = {1.0 - e, e};
        const double xr_R[kSpecies] = {1.0, 0.0};
        FluidVector high;
        double species_high[kSpecies] = {0.0, 0.0};
        FluxHLLC<PCMReconstruction>::compute_face_flux(mean_L, mean_R, xr_L, xr_R,
            kSpecies, kEos, kDim, 0.0, high, species_high);
        check(finite_state(high) && std::isfinite(species_high[0])
              && std::isfinite(species_high[1]), "HLLC high-order flux is finite");
        // Normalized species flux sums: the split is consistent with the flux.
        check(close_rel(species_high[0] + species_high[1], high.rho),
              "input species flux sums to the high-order mass flux");

        const Face f = prepare(mean_L, mean_R, xm_L, xm_R, high, species_high);
        check(FluxAdmissibility::valid(f.bar), "strict fluid bar state is admissible");
        check(FluxAdmissibility::valid(f.low), "Lax-Friedrichs low face is admissible");
        check(f.bar_species[1] == 0.0, "cell means carry exactly zero trace");
        const double euler_theta = euler_theta_reference(f, high);

        double species_limited[kSpecies] = {0.0, 0.0};
        const FluidVector limited = limit(mean_L, mean_R, xm_L, xm_R, high,
                                          species_high, species_limited);
        check(finite_state(limited), "limited flux is finite");
        check(close_rel(species_limited[0] + species_limited[1], limited.rho),
              "limited species flux sums to the limited mass flux");

        // Hydrodynamic continuity: an at-most-tiny trace must not touch the
        // shared fluid flux or the species split.
        check(max_abs_diff(limited, high) == 0.0,
              "zero/tiny trace leaves the shared fluid flux unchanged");
        check(species_limited[0] == species_high[0]
              && species_limited[1] == species_high[1],
              "zero/tiny trace leaves the species split unchanged");
        if (species_high[1] != 0.0) {
            const double theta = theta_from_species(f, 1, species_high[1], species_limited[1]);
            check(theta == 1.0, "recovered theta is exactly one for the tiny trace");
            check(cone_holds(f, 0, theta) && cone_holds(f, 1, theta),
                  "tiny trace stays inside the shifted cone");
        }

        const RetiredPrediction retired =
            retired_zero_trace_switch(f, high, species_high, euler_theta);
        if (e > 0.0 && species_high[1] != 0.0) {
            // The old 0/0 branch maps any nonzero reconstructed trace to
            // theta = 0, i.e. a finite switch onto the Lax-Friedrichs face.
            check(retired.theta == 0.0 && max_abs_diff(retired.flux, high) > 0.1,
                  "retired zero-trace branch would switch the hydrodynamic flux");
            check(max_abs_diff(retired.flux, limited) > 0.1,
                  "measured limiter output rejects the retired switch");
        } else {
            check(retired.theta == euler_theta,
                  "exact zero trace has no switch in either formula");
        }

        if (e == 0.0) {
            high_first = high;
        } else {
            // Continuity across arbitrarily small traces: the hydrodynamic face
            // flux is unchanged relative to the exact-zero-trace row.
            check(close_rel(high.rho, high_first.rho)
                  && max_abs_diff(high, high_first) == 0.0,
                  "face hydrodynamic flux is independent of the tiny trace");
        }

        std::printf("A trace=%-10.3g high.rho=%-22.16g lim.rho=%-22.16g |lim-high|=%-8.3g "
                    "retired.rho=%-22.16g |retired-high|=%-8.3g spec1.high=%-10.3g\n",
                    e, high.rho, limited.rho, max_abs_diff(limited, high),
                    retired.flux.rho, max_abs_diff(retired.flux, high), species_high[1]);
    }

    // ---- Case B: true above-band violations stay limited, both signs. -------
    FluidVector high_B;
    double species_ref[kSpecies] = {0.0, 0.0};
    {
        const double xr_L[kSpecies] = {1.0 - 1e-3, 1e-3};
        const double xr_R[kSpecies] = {1.0, 0.0};
        FluxHLLC<PCMReconstruction>::compute_face_flux(mean_L, mean_R, xr_L, xr_R,
            kSpecies, kEos, kDim, 0.0, high_B, species_ref);
    }
    const double trace_violation = 0.5 * high_B.rho;   // far above the 64eps band
    double theta_base = 0.0;
    FluidVector limited_base;
    for (int sign = 0; sign < 2; ++sign) {
        const double trace = sign == 0 ? -trace_violation : trace_violation;
        double species_high[kSpecies] = {high_B.rho - trace, trace};
        check(close_rel(species_high[0] + species_high[1], high_B.rho),
              "violating input species flux sums to the mass flux");
        const Face f = prepare(mean_L, mean_R, xm_L, xm_R, high_B, species_high);
        check(FluxAdmissibility::valid(f.bar) && FluxAdmissibility::valid(f.low),
              "strict fluid bar and low face stay admissible");
        // Structural proof that this case is genuinely outside the band.
        check(std::abs(f.D[1]) > f.Qbar[1],
              "species trace deviation exceeds the shifted cone");
        const double euler_theta = euler_theta_reference(f, high_B);
        check(euler_theta == 1.0, "Euler branch alone would keep the high face");

        double species_limited[kSpecies] = {0.0, 0.0};
        const FluidVector limited = limit(mean_L, mean_R, xm_L, xm_R, high_B,
                                          species_high, species_limited);
        check(finite_state(limited), "limited above-band flux is finite");
        const double theta = theta_from_species(f, 1, species_high[1], species_limited[1]);
        check(theta > 0.0 && theta < 0.5,
              "true above-band violation is limited, not ignored or collapsed");
        check(theta < euler_theta, "species cone binds below the Euler-only theta");
        check(cone_holds(f, 0, theta) && cone_holds(f, 1, theta), kConePlus);
        check_fluid_blend(f, high_B, theta, "above-band fluid blend");
        check(close_rel(species_limited[0] + species_limited[1], limited.rho),
              "above-band species flux sums to the limited mass flux");
        check(in_span(f.low.rho, limited.rho, high_B.rho)
              && in_span(f.low.eng, limited.eng, high_B.eng)
              && in_span(f.low.mom_u, limited.mom_u, high_B.mom_u),
              "limited fluid flux is a convex blend of low and high");
        // The bounded face trace itself, with no new floor: it must be far
        // smaller than the violating input and still finite.
        check(std::abs(species_limited[1]) < std::abs(trace)
              && std::isfinite(species_limited[1]),
              "bounded trace magnitude is below the violating input");

        if (sign == 0) {
            theta_base = theta;
            limited_base = limited;
        }
        std::printf("B sign=%+d theta=%-12.6g euler=%-5.3g Qbar1=%-12.5g D1=%-12.5g "
                    "spec1.lim=%-12.5g lim.rho=%-22.16g\n",
                    sign == 0 ? -1 : 1, theta, euler_theta, f.Qbar[1], f.D[1],
                    species_limited[1], limited.rho);
    }

    // Physical reflection swaps the cells and reverses normal velocity.
    // Normal-momentum flux is even; mass, energy and species flux are odd.
    {
        const double species_high[kSpecies] = {high_B.rho + trace_violation, -trace_violation};
        const Face forward = prepare(mean_L, mean_R, xm_L, xm_R, high_B, species_high);
        const auto reflected_left = reflect_state(mean_R);
        const auto reflected_right = reflect_state(mean_L);
        const auto reflected_high = reflect_flux(high_B);
        const double reflected_species[kSpecies]{-species_high[0], -species_high[1]};
        const Face reverse = prepare(reflected_left, reflected_right, xm_R, xm_L,
                                     reflected_high, reflected_species);
        check(close_rel(forward.bar.rho, reverse.bar.rho)
              && close_rel(forward.bar.eng, reverse.bar.eng)
              && close_rel(forward.bar.mom_u, -reverse.bar.mom_u),
              "physical reflection transforms the fluid bar state");
        check(close_rel(forward.low.rho, -reverse.low.rho)
              && close_rel(forward.low.eng, -reverse.low.eng)
              && close_rel(forward.low.mom_u, reverse.low.mom_u),
              "physical reflection transforms the low fluid flux");
        for (int s = 0; s < kSpecies; ++s) {
            check(close_rel(forward.bar_species[s], reverse.bar_species[s])
                  && close_rel(forward.low_species[s], -reverse.low_species[s]),
                  "physical reflection transforms the species bar and low flux");
            check(close_rel(forward.Qbar[s], reverse.Qbar[s])
                  && close_rel(forward.D[s], -reverse.D[s]),
                  "physical reflection preserves Qbar and reverses D");
        }
        double reflected_limited_species[kSpecies];
        const auto reflected_limited = limit(reflected_left, reflected_right, xm_R, xm_L,
            reflected_high, reflected_species, reflected_limited_species);
        const double reflected_theta = theta_from_species(reverse, 1,
            reflected_species[1], reflected_limited_species[1]);
        check(close_rel(reflected_theta, theta_base),
              "physical reflection preserves the applied theta");
        check(cone_holds(reverse, 0, reflected_theta)
              && cone_holds(reverse, 1, reflected_theta),
              "reflected emitted flux satisfies both cone constraints");
        check(close_rel(reflected_limited.rho, -limited_base.rho)
              && close_rel(reflected_limited.eng, -limited_base.eng)
              && close_rel(reflected_limited.mom_u, limited_base.mom_u),
              "physical reflection transforms the emitted fluid flux");
        check(close_rel(reflected_limited_species[0] + reflected_limited_species[1],
                        reflected_limited.rho),
              "reflected species flux sums to the reflected mass flux");
        check_fluid_blend(reverse, reflected_high, reflected_theta, "reflected fluid blend");
    }

    // ---- Case D: scaling from 1 down to 1e-100. -----------------------------
    {
        const double scales[] = {1.0, 1e-12, 1e-30, 1e-60, 1e-100};
        const double species_base[kSpecies] = {high_B.rho + trace_violation, -trace_violation};
        for (double scale : scales) {
            const FluidVector L = scale * mean_L, R = scale * mean_R;
            const FluidVector high = scale * high_B;
            double species_high[kSpecies] = {scale * species_base[0], scale * species_base[1]};
            const Face f = prepare(L, R, xm_L, xm_R, high, species_high);
            check(FluxAdmissibility::valid(L) && FluxAdmissibility::valid(R)
                  && FluxAdmissibility::valid(f.bar),
                  "scaled states and bar remain admissible");
            double species_limited[kSpecies] = {0.0, 0.0};
            const FluidVector limited = limit(L, R, xm_L, xm_R, high, species_high,
                                              species_limited);
            const double theta = theta_from_species(f, 1, species_high[1], species_limited[1]);
            check(close_rel(theta, theta_base),
                  "scaling keeps the applied theta");
            check(close_rel(limited.rho, scale * limited_base.rho)
                  && close_rel(limited.eng, scale * limited_base.eng)
                  && close_rel(limited.mom_u, scale * limited_base.mom_u),
                  "scaling scales the limited flux linearly");
            check(cone_holds(f, 0, theta) && cone_holds(f, 1, theta),
                  "scaled case stays inside the shifted cone");
            check(close_rel(species_limited[0] + species_limited[1], limited.rho),
                  "scaled species flux sums to the limited mass flux");
            std::printf("D scale=%-8.3g theta=%-12.6g lim.rho=%-22.16g "
                        "scale*base.rho=%-22.16g\n",
                        scale, theta, limited.rho, scale * limited_base.rho);
        }
    }

    test_gamma_wall_positive_product_range();
    test_shared_gamma_wall_third_direction();
    test_shared_gamma_selected_point_blend();
    test_ordinary_wall_sweeps();
    test_shared_point_wall_assembler();
    test_general_eos_point_wall_assembler();
    test_general_eos_reflected_wave_convergence();
    test_stationary_candidate_projection();

    ok = failures == 0;
    std::printf("checks=%d failures=%d result=%s\n", checks, failures,
                ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
