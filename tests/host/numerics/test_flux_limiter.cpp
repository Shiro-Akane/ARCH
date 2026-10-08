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
#include "grid/Grid.h"
#include "numerics/diffusion/DiffFlux.h"
#include "numerics/flux/RzNativeFaceFlux.h"
#include "numerics/flux/StationarySlipWallFlux.h"
#include "physics/eos/IdealGas.h"
#include "numerics/flux/FluxHLLC.h"
#include "numerics/flux/InvariantDomainFlux.h"
#include "numerics/state/StateAdmissibility.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
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

// Independent native wall mathematics on a bound annulus. These exact V/W
// ghost integrals are prescribed fixture data; no BCHandler, scheduler or
// Runtime authority is mocked or certified by the flat mathematical flags.
struct NativeWallFixture {
    static constexpr auto semantics=GridMetrics::GeometrySemantics::AxisymmetricRz;
    static constexpr long double internal=1.L/64.L;
    SpeciesManager species;
    IdealGas eos;
    Grid grid{amr::MAX_NG,1.,3.,-.5,.5,0.,1.};
    FluidState state;
    arch::state::Bounds bounds{1.e-14,0.,1.};

    static SpeciesManager materials() {
        SpeciesManager result;
        result.add_species("wall_a",1.,1.,1.4,2.);
        result.add_species("wall_b",4.,2.,1.4,2.);
        return result;
    }
    // Integrate (c+d*r) and its square with the actual W/V measures. The
    // physical tangential velocity reflects across a radial wall without
    // changing its sign; radial/axial normal velocities are genuinely zero.
    static FluidVector integral(double lower,double upper,long double c,long double d) {
        const long double a=lower,b=upper;
        const long double volume=(b*b-a*a)/2.L;
        const long double angular=(b*b*b-a*a*a)/3.L;
        const long double mphi=(c*(b*b*b-a*a*a)/3.L
            +d*(b*b*b*b-a*a*a*a)/4.L)/angular;
        const long double squared=(c*c*(b*b-a*a)/2.L
            +2.L*c*d*(b*b*b-a*a*a)/3.L
            +d*d*(b*b*b*b-a*a*a*a)/4.L)/volume;
        return {1.,0.,0.,double(mphi),double(internal+.5L*squared)};
    }
    NativeWallFixture():species(materials()),eos(1.4,species) {
        grid.dim=2;grid.geometry="cylindrical";
        grid.InitializeTopology(semantics);
        grid.dyadic_identity.bound=true;
        grid.dyadic_identity.root_lower={1.,-.5};
        grid.dyadic_identity.root_upper={3.,.5};
        grid.dyadic_identity.root_blocks={1,1};
        grid.dyadic_identity.level=0;grid.dyadic_identity.logical={0,0};
        grid.InitializeTopology(semantics);
        check(GridMetrics::matches_identity(GridMetrics::make_geometry_view(grid,semantics)),
            "native wall fixture authenticates its actual bound annular grid");
        state.Preallocate(grid.GetTotalSize());state.InitSpecies(kSpecies);
        for(int j=0;j<grid.GetTotalY();++j)for(int i=0;i<grid.GetTotalX();++i) {
            long double c=0.L,d=1.L;
            if(i<grid.Is()){c=2.L*grid.x1_min;d=-1.L;}
            else if(i>=grid.Ie()){c=2.L*grid.x1_max;d=-1.L;}
            const int index=grid.GetIndex(i,j);
            state.set(index,integral(grid.GetFacePosL(i),grid.GetFacePosR(i),c,d));
            state.X(0,index)=.75;state.X(1,index)=.25;state.enuc_rate[index]=0.;
        }
    }
    arch::boundary::HydroBoundaryView walls() const {
        arch::boundary::HydroBoundaryView result;
        result.reflecting={true,true,true,true,false,false};return result;
    }
    RzSelectedReconstruction::Context context(int direction,int side) const {
        const int i=direction==0?(side?grid.Ie()-1:grid.Is()-1):grid.Is()+3;
        const int j=direction==1?(side?grid.Je()-1:grid.Js()-1):grid.Js()+3;
        return {GridMetrics::make_geometry_view(grid,semantics),grid.GetTotalX(),
            grid.GetTotalY(),i,j,direction,kSpecies,bounds};
    }
};

// The whole logical fixture, including real endpoint halos, is checked with
// the actual selected IdealGas at all six shared baseline physical points.
void check_native_wall_required_eos(const NativeWallFixture& fixture) {
    const auto geometry=GridMetrics::make_geometry_view(fixture.grid,NativeWallFixture::semantics);
    const auto read=[&](int index){return fixture.state.get(index);};
    const double xi[kSpecies]{.75,.25};
    for(int j=0;j<fixture.grid.GetTotalY();++j)for(int i=0;i<fixture.grid.GetTotalX();++i) {
        int begin=i-1;begin=std::max(0,std::min(begin,fixture.grid.GetTotalX()-3));
        const int index=fixture.grid.GetIndex(i,j);
        const auto cell=RzThermodynamics::make_cell_supported(read,index,geometry,i,begin,fixture.bounds);
        check(cell.valid(),"independent wall halo has a valid real density/inertia closure");
        if(!cell.valid())continue;
        for(int n=0;n<6;++n)
            check(arch::state::validate_eos(RzThermodynamics::base_point(cell,
                RzThermodynamics::physical_node_radius(cell,n)),xi,kSpecies,fixture.bounds,fixture.eos)
                ==arch::state::Status::valid,"every actual wall-halo baseline point passes real IdealGas/bounds");
    }
}

// Borrow only real state readers and caller-owned, non-overlapping workspaces.
// The view has Native semantics so all physical points query the actual EOS;
// its Roe-wave option remains explicit rather than being lost through nullptr.
template<class Reconstruction>
arch::state::Status native_wall_compute(const NativeWallFixture& fixture,
    const RzSelectedReconstruction::Context& context,
    const arch::boundary::HydroBoundaryView* boundary,FluidVector& flux,
    std::array<double,kSpecies>& species_flux,
    GridMetrics::GeometrySemantics mean_semantics=NativeWallFixture::semantics) {
    std::array<double,16*kSpecies> rhoX{};
    std::array<double,19*kSpecies> reconstruction{};
    std::array<double,kSpecies> left{},right{},candidate{},high{},low{};
    RzNativeFaceFlux::Scratch scratch{rhoX.data(),reconstruction.data(),reconstruction.size(),
        left.data(),right.data(),candidate.data(),high.data(),low.data()};
    FluxAdmissibility::MeanThermoView means;means.geometry_semantics=mean_semantics;
    means.roe_wave_speed=false;
    const auto read=[&](int index){return fixture.state.get(index);};
    const auto fraction=[&](int s,int index){return fixture.state.X(s,index);};
    const int left_cell=context.geometry.GetIndex(context.face_i,context.face_j);
    const int right_cell=left_cell+(context.direction==0?1:context.geometry.stride_y);
    if(boundary)return RzNativeFaceFlux::compute<FluxHLLC<Reconstruction>,Reconstruction>(
        read,fraction,context,fixture.eos,0.,&means,left_cell,right_cell,scratch,flux,
        species_flux.data(),*boundary);
    return RzNativeFaceFlux::compute<FluxHLLC<Reconstruction>,Reconstruction>(
        read,fraction,context,fixture.eos,0.,&means,left_cell,right_cell,scratch,flux,species_flux.data());
}

// Zero normal velocity is chosen deliberately: independent wall mass/work and
// tangential advective/species fluxes are zero. Normal pressure traction stays
// positive; it is not forcibly equated to the mean pressure for high profiles.
template<class Reconstruction>
void check_native_wall_face(const NativeWallFixture& fixture,int direction,int side,bool pcm_pressure) {
    const auto context=fixture.context(direction,side);const auto wall=fixture.walls();
    FluidVector flux;std::array<double,kSpecies> species_flux{};
    const auto status=native_wall_compute<Reconstruction>(fixture,context,&wall,flux,species_flux);
    check(status==arch::state::Status::valid&&finite_state(flux),
        "actual native reflected face returns a finite publishable mathematical flux");
    if(status!=arch::state::Status::valid)return;
    const double traction=direction==0?flux.mom_u:flux.mom_v;
    const double tangent=direction==0?flux.mom_v:flux.mom_u;
    // Reuse the original 64-epsilon band, in this fixture's unit-scale flux.
    const double roundoff=kBand*std::max(1.,std::abs(traction));
    check(traction>0.,"reflecting wall retains positive normal pressure traction");
    check(std::abs(flux.rho)<=roundoff&&std::abs(flux.eng)<=roundoff
        &&std::abs(flux.mom_w)<=roundoff&&std::abs(tangent)<=roundoff,
        "four-wall cold rotation has zero mass/work/angular/tangent advective flux");
    check(std::abs(species_flux[0])<=roundoff&&std::abs(species_flux[1])<=roundoff,
        "reflecting wall does not transport either actual species");
    if(pcm_pressure) {
        const long double expected=(1.4L-1.L)*NativeWallFixture::internal;
        const int interior=context.geometry.GetIndex(context.face_i+(direction==0&&!side?1:0),
            context.face_j+(direction==1&&!side?1:0));
        // Independent antiderivative E includes the rotational cancellation
        // scale. The same original 64eps budget covers represented EOS energy.
        const double scale=std::max(std::abs(double(expected)),std::abs(fixture.state.get(interior).eng));
        check(std::abs(traction-double(expected))<=kBand*scale,
            "PCM no-normal-flow wall traction matches independent constant internal-energy pressure");
    }
}

/** Cold rotating wall with actual nonzero axial away speed.
 * Independent ideal-gas rarefaction invariant and shock-jump quadratic supply
 * pressure; the source V/W antiderivatives and existing 64-epsilon band stay.
 * These flat flags qualify point/face math only, not Runtime wall authority.
 */
void test_native_gamma_wall_away_pressure() {
    NativeWallFixture fixture;
    constexpr double velocity=.125;
    for(int j=0;j<fixture.grid.GetTotalY();++j)for(int i=0;i<fixture.grid.GetTotalX();++i) {
        const int cell=fixture.grid.GetIndex(i,j);
        auto state=fixture.state.get(cell);
        state.mom_v=velocity;
        state.eng+=.5*velocity*velocity;
        fixture.state.set(cell,state);
    }
    check_native_wall_required_eos(fixture);
    const long double gm1=.4L,gamma=1.4L,P=gm1*NativeWallFixture::internal;
    const long double c=std::sqrt(gamma*P),mach=velocity/c;
    const long double rarefaction=P*std::pow(1.L-gm1*mach/2.L,2.L*gamma/gm1);
    const long double K=gamma*(gamma+1.L)*mach*mach/2.L,B=gm1/(gamma+1.L);
    const long double compression=P*(1.L+K/2.L+std::sqrt(K*K/4.L+K*(1.L+B)));
    const auto wall=fixture.walls();
    for(int side=0;side<2;++side) {
        FluidVector flux{};std::array<double,kSpecies> species_flux{};
        const auto context=fixture.context(1,side);
        const auto status=native_wall_compute<PCMReconstruction>(fixture,context,&wall,flux,species_flux);
        check(status==arch::state::Status::valid,"cold away/compression gamma-law wall produces a valid flux");
        if(status!=arch::state::Status::valid)continue;
        const double expected=double(side==0?rarefaction:compression);
        const int interior=context.geometry.GetIndex(context.face_i,context.face_j+(side==0));
        const double scale=std::max(std::abs(expected),std::abs(fixture.state.get(interior).eng));
        check(flux.mom_v>0.&&std::abs(flux.mom_v-expected)<=kBand*scale,
            "cold gamma-law wall pressure matches independent rarefaction/shock jump");
        check(flux.rho==0.&&flux.eng==0.&&flux.mom_u==0.&&flux.mom_w==0.
            &&species_flux[0]==0.&&species_flux[1]==0.,
            "stationary gamma-law wall has exactly zero advective/work/species flux");
    }
    // Independent one-sided baseline states from the frozen wall pressure.
    // This checks the actual prerequisite, rather than borrowing an LLF bar.
    const double speed=double(c),pressure=double(P),wall_pressure=double(rarefaction);
    const FluidVector left{1.,0.,-velocity,0.,double(NativeWallFixture::internal+.5L*velocity*velocity)};
    const FluidVector right{1.,0.,velocity,0.,left.eng};
    const double a=velocity+speed;
    const FluidVector wall_flux{0.,0.,wall_pressure,0.,0.};
    const FluidVector left_physical{-velocity,0.,velocity*velocity+pressure,0.,-velocity*(left.eng+pressure)};
    const FluidVector right_physical{velocity,0.,velocity*velocity+pressure,0.,velocity*(right.eng+pressure)};
    const auto bl=left+(left_physical-wall_flux)/a;
    const auto br=right+(wall_flux-right_physical)/a;
    check(FluxAdmissibility::valid(bl)&&FluxAdmissibility::valid(br),
        "independent cold exact wall has two admissible baseline states");
    const double xi[kSpecies]{.75,.25},zero[kSpecies]{0.,0.};
    const auto selected=FluxAdmissibility::point_face_blend_with_baseline_and_thermo(
        left,right,xi,xi,kSpecies,pressure,speed,pressure,speed,1,
        wall_flux,zero,wall_flux,zero);
    check(selected.valid&&selected.theta==1.&&selected.low.mom_v==wall_pressure,
        "selected-baseline factor binds its actual wall low and equal high");
    for(int species=0;species<kSpecies;++species) {
        const double ql=left.rho*xi[species]+left_physical.rho*xi[species]/a;
        const double qr=right.rho*xi[species]-right_physical.rho*xi[species]/a;
        check(ql>0.&&qr>0.&&std::abs(ql-bl.rho*xi[species])<=kBand*std::abs(ql)
            &&std::abs(qr-br.rho*xi[species])<=kBand*std::abs(qr),
            "both actual selected wall species bases retain positive constant fractions");
    }
    const double nan=std::numeric_limits<double>::quiet_NaN();
    const FluidVector bad_high{nan,0.,0.,0.,nan};
    const double bad_species[kSpecies]{nan,nan};
    const auto trial_rejected=FluxAdmissibility::point_face_blend_with_baseline_and_thermo(
        left,right,xi,xi,kSpecies,pressure,speed,pressure,speed,1,
        bad_high,bad_species,wall_flux,zero);
    check(trial_rejected.valid&&trial_rejected.theta==0.
        &&trial_rejected.low.mom_v==wall_pressure,
        "nonfinite optional high chooses theta0 of the actual admissible wall baseline");
    // A legitimate gamma-law exact wall alone cannot guarantee the fixed-a
    // one-sided bases. Independent gamma=10, compressive Mach=-10 witness
    // has a negative internal energy; the factor must fail without raising a.
    const long double stiff_gamma=10.L,stiff_gm1=9.L,stiff_p=1.L;
    const long double stiff_c=std::sqrt(stiff_gamma*stiff_p),stiff_v=-10.L*stiff_c;
    const long double stiff_K=stiff_gamma*(stiff_gamma+1.L)*100.L/2.L;
    const long double stiff_B=stiff_gm1/(stiff_gamma+1.L);
    const double stiff_wall=double(stiff_p*(1.L+stiff_K/2.L
        +std::sqrt(stiff_K*stiff_K/4.L+stiff_K*(1.L+stiff_B))));
    const double u=double(stiff_v),es=double(stiff_p/stiff_gm1+.5L*stiff_v*stiff_v);
    const FluidVector stiff_left{1.,0.,-u,0.,es},stiff_right{1.,0.,u,0.,es};
    const FluidVector stiff_low{0.,0.,stiff_wall,0.,0.};
    const double stiff_a=std::abs(u)+double(stiff_c);
    const FluidVector stiff_fl{-u,0.,u*u+1.,0.,-u*(es+1.)};
    const auto stiff_bar=stiff_left+(stiff_fl-stiff_low)/stiff_a;
    check(stiff_bar.eng/stiff_bar.rho-.5*stiff_bar.mom_v*stiff_bar.mom_v
        /(stiff_bar.rho*stiff_bar.rho)<0.,
        "independent stiff compressive wall really has an inadmissible fixed-a base");
    const auto baseline_rejected=FluxAdmissibility::point_face_blend_with_baseline_and_thermo(
        stiff_left,stiff_right,xi,xi,kSpecies,1.,double(stiff_c),1.,double(stiff_c),1,
        stiff_low,zero,stiff_low,zero);
    check(!baseline_rejected.valid,
        "inadmissible selected wall prerequisite fails closed without an acoustic-speed increase");
    double untouched=123.;
    check(!StationarySlipWallFlux::gamma_wall_pressure(.4,.00625,.1,
        std::numeric_limits<double>::infinity(),untouched)&&untouched==123.,
        "nonfinite gamma wall input rejects without publishing");
    check(StationarySlipWallFlux::gamma_wall_pressure(.4,.00625,.1,1.,untouched)&&untouched==0.,
        "true gamma-law vacuum has zero traction without a pressure floor");
}

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
        check(StationarySlipWallFlux::gamma_wall_flux(point,xi,kEos,2,side,
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
    check(StationarySlipWallFlux::gamma_wall_flux(resting,xi,kEos,2,0,
        pressure,double(sound),flux)&&flux.mom_w==pressure,
        "third-direction exact rest preserves the original pressure bits");
    const FluidVector sentinel{123.,234.,345.,456.,567.};
    for(const auto invalid:std::array<std::array<int,2>,3>{{{{-1,0}},{{3,0}},{{2,2}}}}) {
        flux=sentinel;
        check(!StationarySlipWallFlux::gamma_wall_flux(point,xi,kEos,invalid[0],invalid[1],
            pressure,double(sound),flux)&&max_abs_diff(flux,sentinel)==0.,
            "invalid wall direction/side rejects without publishing");
    }
    // All input reads precede publication, including actual third momentum.
    auto alias=point;
    check(StationarySlipWallFlux::gamma_wall_flux(alias,xi,kEos,2,0,
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
        const auto factor=StationarySlipWallFlux::gamma_selected_point_blend(
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
        const auto rejected_high=StationarySlipWallFlux::gamma_selected_point_blend(
            left,right,xi,xi,kSpecies,pressure,double(c),pressure,double(c),kEos,
            dir,side,bad_high,bad_species,baseline);
        check(rejected_high.valid&&rejected_high.theta==0.
            &&max_abs_diff(rejected_high.low,factor.low)==0.,
            "optional bad high contracts to theta0 of the identical canonical wall low");
        for(int failure:{0,1,2}) {
            baseline[0]=123.;baseline[1]=456.;
            const double bad_xi[kSpecies]{nan,.25};
            const auto bad=StationarySlipWallFlux::gamma_selected_point_blend(
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
        const auto factor=StationarySlipWallFlux::gamma_selected_point_blend(
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
        const auto factor=StationarySlipWallFlux::gamma_selected_point_blend(
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
    using Status=StationarySlipWallFlux::PointWallStatus;
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
        const auto status=StationarySlipWallFlux::assemble_point(point,point,point,point,
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
    check(StationarySlipWallFlux::assemble_point(point,point,point,point,3,0,2,
        pressure,sound,kEos,left,right,low,high_species,read_base,high_compute,output)
            ==Status::invalid_metadata&&high_calls==0&&max_abs_diff(output,sentinel)==0.,
        "shared wall invalid direction rejects before callback/output publication");
    check(StationarySlipWallFlux::assemble_point(point,point,point,point,0,0,2,
        -pressure,sound,kEos,left,right,low,high_species,read_base,high_compute,output)
            !=Status::valid&&max_abs_diff(output,sentinel)==0.,
        "invalid actual baseline pressure rejects without fluid publication");
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

void test_native_reflecting_face_math() {
    NativeWallFixture fixture;check_native_wall_required_eos(fixture);
    const auto source_rho=fixture.state.rho,source_u=fixture.state.mom_u,
        source_v=fixture.state.mom_v,source_w=fixture.state.mom_w,
        source_e=fixture.state.eng,source_x=fixture.state.mass_fractions;
    for(int direction=0;direction<2;++direction)for(int side=0;side<2;++side) {
        check_native_wall_face<MusclReconstruction<McLimiter>>(fixture,direction,side,false);
        check_native_wall_face<PCMReconstruction>(fixture,direction,side,true);
    }
    check_native_wall_face<PPMReconstruction>(fixture,1,0,false);

    const arch::boundary::HydroBoundaryView empty{};const auto wall=fixture.walls();
    auto internal=fixture.context(0,0);internal.face_i=fixture.grid.Is()+5;
    FluidVector omitted{},explicit_empty{},flagged_internal{};
    std::array<double,kSpecies> omitted_x{},empty_x{},internal_x{};
    const auto a=native_wall_compute<PCMReconstruction>(fixture,internal,nullptr,omitted,omitted_x);
    const auto b=native_wall_compute<PCMReconstruction>(fixture,internal,&empty,explicit_empty,empty_x);
    const auto c=native_wall_compute<PCMReconstruction>(fixture,internal,&wall,flagged_internal,internal_x);
    check(a==arch::state::Status::valid&&b==a&&c==a
        &&max_abs_diff(omitted,explicit_empty)==0.&&max_abs_diff(omitted,flagged_internal)==0.
        &&omitted_x==empty_x&&omitted_x==internal_x,
        "empty/default wall view and dormant internal-face flags preserve exact original face results");

    const FluidVector sentinel{123.,-456.,789.,-321.,654.};
    const std::array<double,kSpecies> sentinel_x{17.,19.};
    auto reject=[&](RzSelectedReconstruction::Context context,arch::boundary::HydroBoundaryView view,
        GridMetrics::GeometrySemantics semantics,const char* message) {
        auto flux=sentinel;auto species_flux=sentinel_x;
        const auto status=native_wall_compute<PCMReconstruction>(fixture,context,&view,flux,species_flux,semantics);
        check(status!=arch::state::Status::valid&&max_abs_diff(flux,sentinel)==0.&&species_flux==sentinel_x,message);
    };
    auto bad_direction=fixture.context(0,0);bad_direction.direction=2;
    reject(bad_direction,wall,NativeWallFixture::semantics,"malformed native wall direction rejects without publishing");
    auto x3=wall;x3.reflecting[4]=true;
    reject(fixture.context(0,0),x3,NativeWallFixture::semantics,"native 2D x3 wall flags reject without publishing");
    auto bad_normal=fixture.context(0,0);bad_normal.face_i=fixture.grid.Is()-2;
    reject(bad_normal,wall,NativeWallFixture::semantics,"wall face outside actual active normal bounds rejects without publishing");
    auto bad_tangent=fixture.context(0,0);bad_tangent.face_j=fixture.grid.Js()-1;
    reject(bad_tangent,wall,NativeWallFixture::semantics,"wall tangent ghost-plane request rejects without publishing");
    reject(fixture.context(0,0),wall,GridMetrics::GeometrySemantics::Existing,
        "foreign ordinary mean cache cannot certify mirrored native physical points");

    int side=91;
    check(!wall.reflecting_side(-1,4,4,20,side)&&side==91,
        "flat view checked lookup rejects invalid direction without changing output");
    check(!wall.reflecting_side(0,4,20,4,side)&&side==91,
        "flat view checked lookup rejects reversed active bounds");
    check(wall.reflecting_side(0,5,4,20,side)&&side==-1,
        "flat view internal face has no physical wall authority");

    // Preserve the independent cold raw-mean counterexample: physical wall
    // points do not make raw mixed-measure U a legal generic point LLF state.
    const FluidVector raw{1.,0.,0.,3./4.,17./64.};
    const double xi[kSpecies]{.75,.25};const double p=(1.4-1.)/64.;
    const double sound=std::sqrt(1.4*p);
    check(arch::state::recover(raw).status==arch::state::Status::unresolved_energy,
        "cold raw native mean remains outside the generic point thermal domain");
    const FluidVector high{0.,p,0.,0.,0.};const double species_high[kSpecies]{0.,0.};
    const auto factor=FluxAdmissibility::point_face_blend_with_thermo(raw,raw,xi,xi,kSpecies,
        p,sound,p,sound,0,high,species_high);
    check(!factor.valid,"raw cold mean generic LLF remains rejected independently of physical-wall face success");
    check(fixture.state.rho==source_rho&&fixture.state.mom_u==source_u
        &&fixture.state.mom_v==source_v&&fixture.state.mom_w==source_w
        &&fixture.state.eng==source_e&&fixture.state.mass_fractions==source_x,
        "all native wall face and failure requests preserve actual source arrays");
}


// Actual native axial producer -> actual physical-face override. Independent
// V/W antiderivatives own the expected mechanical work; there is no mocked
// Native frame or Runtime acceptance, and no full-tensor physics assertion.
void test_native_diffusion_boundary_work()
{
    constexpr auto native=GridMetrics::GeometrySemantics::AxisymmetricRz;
    SpeciesManager species;species.add_species("work_a",1.,1.,1.4,2.);
    species.add_species("work_b",4.,2.,1.4,2.);
    IdealGas eos(1.4,species);
    Grid grid(amr::MAX_NG,4.,20.,-.5,.5,0.,1.);
    grid.dim=2;grid.geometry="cylindrical";grid.InitializeTopology(native);
    grid.dyadic_identity.bound=true;grid.dyadic_identity.root_lower={4.,-.5};
    grid.dyadic_identity.root_upper={20.,.5};grid.dyadic_identity.root_blocks={1,1};
    grid.dyadic_identity.level=0;grid.dyadic_identity.logical={0,0};
    grid.InitializeTopology(native);
    check(GridMetrics::matches_identity(GridMetrics::make_geometry_view(grid,native)),
        "native viscous work fixture has actual bound root geometry");
    FluidState state;state.Preallocate(grid.GetTotalSize());state.InitSpecies(kSpecies);
    for(int j=0;j<grid.GetTotalY();++j)for(int i=0;i<grid.GetTotalX();++i) {
        const long double a=grid.GetFacePosL(i),b=grid.GetFacePosR(i);
        const long double capacity=(b*b*b*b-a*a*a*a)/4.L;
        const long double torque_measure=(b*b*b-a*a*a)/3.L;
        const long double omega=j<grid.Js()?1.L:2.L;
        const int index=grid.GetIndex(i,j);
        state.set(index,{1.,0.,0.,double(omega*capacity/torque_measure),10000.});
        state.X(0,index)=.75;state.X(1,index)=.25;state.enuc_rate[index]=0.;
    }
    const std::array<std::vector<double>,7> inputs{state.rho,state.mom_u,state.mom_v,
        state.mom_w,state.eng,state.enuc_rate,state.mass_fractions};
    const std::array<const double*,7> pointers{state.rho.data(),state.mom_u.data(),
        state.mom_v.data(),state.mom_w.data(),state.eng.data(),state.enuc_rate.data(),
        state.mass_fractions.data()};
    SimConfig config;config.physics.diffusion.use_diffusion=true;
    config.physics.diffusion.use_viscous_diffusion=true;
    // The heat-flux channel is genuinely enabled, with zero conductivity.
    // This permits its zero prescription under the actual callback contract.
    config.physics.diffusion.use_thermal_diffusion=true;
    config.physics.diffusion.alpha_therm=0.;
    config.physics.diffusion.use_species_diffusion=false;
    constexpr double nu=.01;config.physics.diffusion.nu_visc=nu;
    const int i=grid.Is()+1,j=grid.Js(),face=grid.GetIndex(i,j);
    check(grid.GetFacePosL(i)==5.&&grid.GetFacePosR(i)==6.,
        "native viscous work independent reference uses real cell [5,6]");
    // For rho=1 on [5,6]: C=int r^3 dr=671/4, M2=int r^2 dr=91/3,
    // M1=int r dr=11/2. Across this actual axial face Omega_L=1,Omega_R=2.
    // F_phi=-nu*C*(Omega_R-Omega_L)/(dz*M2); F_E/F_phi=1.5*M2/M1=91/11.
    // Raw average (J/W)/rho instead gives 1.5*C/M2=6039/728; the exact
    // difference is 181/8008. This reference never divides production E/F.
    constexpr long double capacity=671.L/4.L,torque_measure=91.L/3.L;
    constexpr long double volume_measure=11.L/2.L;
    constexpr long double work_velocity=91.L/11.L,raw_velocity=6039.L/728.L;
    check(close_rel(double(raw_velocity-work_velocity),double(181.L/8008.L)),
        "native work witness separates raw velocity from independent physical work");
    const long double spacing=grid.GetAxialFacePosR(j)-grid.GetAxialFacePosL(j);
    const double expected_phi=double(-static_cast<long double>(nu)*capacity/(spacing*torque_measure));
    const double expected_energy=double(-static_cast<long double>(nu)*capacity*1.5L/(spacing*volume_measure));
    std::vector<FluidVector> flux(grid.GetTotalSize());
    std::vector<double> species_flux(grid.GetTotalSize()*kSpecies,0.);
    DiffFlux::compute_fluxes(state,eos,grid,config,flux,species_flux,1,false,native);
    const FluidVector baseline=flux[face];
    check(close_rel(baseline.mom_w,expected_phi)&&close_rel(baseline.eng,expected_energy),
        "actual native axial stress and area work match independent C/M2/M1");
    check(close_rel(baseline.eng,double(static_cast<long double>(baseline.mom_w)*work_velocity)),
        "actual native axial face work uses independent 91/11 carrier");
    for(int mode=0;mode<4;++mode) {
        auto controls=std::make_shared<arch::boundary::DiffusionBoundaryStorage>();
        controls->faces[2].resize((grid.Ie()-grid.Is())*(4+kSpecies));
        for(int plane=0;plane<grid.Ie()-grid.Is();++plane) {
            auto* values=controls->faces[2].data()+plane*(4+kSpecies);
            if(mode&1)values[0]={arch::boundary::ScalarBoundaryKind::OutwardFlux,0.};
            if(mode&2)values[3]={arch::boundary::ScalarBoundaryKind::OutwardFlux,0.};
        }
        const auto saved_controls=controls->faces[2];
        state.diffusion_boundary=controls;
        // Real observer storage, owned exactly as a physical lower-Y plane.
        // Its default weight=1/initial_weight=0 captures the published face,
        // and NaN sentinels make an unexecuted observer impossible to pass.
        auto capture=std::make_shared<arch::boundary::BoundaryFluxCaptureStorage>();
        constexpr int captured_fields=6+kSpecies;
        capture->stage[2].assign((grid.Ie()-grid.Is())*captured_fields,
            std::numeric_limits<double>::quiet_NaN());
        state.boundary_flux_capture=capture;
        std::fill(flux.begin(),flux.end(),FluidVector{});
        std::fill(species_flux.begin(),species_flux.end(),0.);
        DiffFlux::compute_fluxes(state,eos,grid,config,flux,species_flux,1,true,native);
        const auto result=flux[face];
        const double* observed=capture->stage[2].data()+(i-grid.Is())*captured_fields;
        check(std::isfinite(observed[captured_fields-1])
            &&std::abs(observed[captured_fields-1])<=band_of(baseline.eng,expected_energy),
            "actual native observer captures zero heat with the original reference-scaled band");
        check(observed[0]==result.rho&&observed[1]==result.mom_u
            &&observed[2]==result.mom_v&&observed[3]==result.mom_w
            &&observed[4]==result.eng,
            "actual native observer traction and total energy agree with the published face");
        for(int sp=0;sp<kSpecies;++sp)
            check(observed[5+sp]==species_flux[sp*grid.GetTotalSize()+face],
                "actual native observer keeps each emitted species flux in its real field");
        check(finite_state(result)&&result.rho==0.&&result.mom_u==0.&&result.mom_v==0.,
            "native controlled work preserves absent transport components");
        if(mode&2) {
            check(result.mom_w==0.,"native zero prescribed phi traction is realized");
            // With no thermal diffusion, changing phi traction to zero must
            // remove all its work, rather than reclassify a residual as heat.
            check(std::abs(result.eng)<=band_of(baseline.eng,expected_energy),
                "native zero phi traction leaves zero area work and zero heat");
        } else {
            check(close_rel(result.mom_w,expected_phi)&&close_rel(result.eng,expected_energy),
                "native zero heat or inherited controls preserve actual paired mechanical work");
        }
        for(int sp=0;sp<kSpecies;++sp)
            check(species_flux[sp*grid.GetTotalSize()+face]==0.,
                "native work controls preserve zero species diffusion");
        bool controls_unchanged=controls->faces[2].size()==saved_controls.size();
        for(std::size_t n=0;n<saved_controls.size();++n)
            controls_unchanged=controls_unchanged
                &&controls->faces[2][n].kind==saved_controls[n].kind
                &&std::bit_cast<std::uint64_t>(controls->faces[2][n].value)
                    ==std::bit_cast<std::uint64_t>(saved_controls[n].value);
        check(controls_unchanged,"actual native boundary producer leaves callback controls immutable");
    }
    const std::array<const std::vector<double>*,7> fields{&state.rho,&state.mom_u,&state.mom_v,
        &state.mom_w,&state.eng,&state.enuc_rate,&state.mass_fractions};
    bool immutable=true;
    for(std::size_t field=0;field<fields.size();++field) {
        immutable=immutable&&fields[field]->data()==pointers[field]
            &&fields[field]->size()==inputs[field].size();
        for(std::size_t n=0;n<inputs[field].size();++n)
            immutable=immutable&&std::bit_cast<std::uint64_t>((*fields[field])[n])
                ==std::bit_cast<std::uint64_t>(inputs[field][n]);
    }
    check(immutable,"all native diffusion face/control requests preserve seven real input arrays and leases");
    std::cout<<"RZ_NATIVE_DIFFUSION_BOUNDARY_WORK modes=4 PASS_IF_ALL_CHECKS\n";
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

    test_native_reflecting_face_math();
    test_native_gamma_wall_away_pressure();
    test_gamma_wall_positive_product_range();
    test_shared_gamma_wall_third_direction();
    test_shared_gamma_selected_point_blend();
    test_ordinary_wall_sweeps();
    test_shared_point_wall_assembler();
    test_stationary_candidate_projection();
    test_native_diffusion_boundary_work();

    ok = failures == 0;
    std::printf("checks=%d failures=%d result=%s\n", checks, failures,
                ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
