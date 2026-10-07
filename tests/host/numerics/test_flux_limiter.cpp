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
    test_native_diffusion_boundary_work();

    ok = failures == 0;
    std::printf("checks=%d failures=%d result=%s\n", checks, failures,
                ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
