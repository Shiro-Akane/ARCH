#pragma once

#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

#include "math/geometry/RzViscousCases.h"
#include "numerics/flux/FluxHLLC.h"
#include "numerics/reconstruction/RzCellPolynomial.h"

namespace RzReconstructionCases {
using Reference=RzViscousCases::NativeClosureReference;

inline constexpr std::array<long double,4> nodes{
    -.8611363115940525752239464888928095L,
    -.3399810435848562648026657591032447L,
     .3399810435848562648026657591032447L,
     .8611363115940525752239464888928095L};
inline constexpr std::array<long double,4> weights{
    .3478548451374538573730639492219994L,
    .6521451548625461426269360507780006L,
    .6521451548625461426269360507780006L,
    .3478548451374538573730639492219994L};

/** Populate actual native cells from independent physical antiderivatives.
 * Xi0(r)=x0+slope*r/R, Xi1=1-Xi0; stored X are density-weighted cell fractions.
 */
inline void populate(FluidState& state,const Grid& grid,const Reference& reference,
    long double x0,long double slope)
{
    state.Preallocate(grid.GetTotalSize());state.InitSpecies(2);
    for(int j=0;j<grid.GetTotalY();++j)for(int i=0;i<grid.GetTotalX();++i) {
        const long double lo=grid.GetFacePosL(i),hi=grid.GetFacePosR(i);
        const long double mass=reference.density_moment(lo,hi,1);
        const long double species0=x0*mass+slope/reference.radius_scale
            *reference.density_moment(lo,hi,2);
        const int index=grid.GetIndex(i,j,0);
        state.set(index,reference.native(lo,hi));
        state.X(0,index)=static_cast<double>(species0/mass);
        state.X(1,index)=static_cast<double>((mass-species0)/mass);
    }
}

/** Independent physical V/W and species means, without evaluating a profile. */
inline std::array<long double,7> means(const Reference& reference,long double lo,
    long double hi,long double x0,long double slope)
{
    const long double sign=hi<=0.?-1.L:1.L;
    const long double volume=sign*RzViscousCases::integral(lo,hi,1);
    const long double mass=sign*reference.density_moment(lo,hi,1);
    const long double inertia=reference.density_moment(lo,hi,3);
    const long double density=mass/volume;
    const long double radial=reference.reflect_radial&&sign<0.?-reference.radial:reference.radial;
    const long double species0=sign*(x0*reference.density_moment(lo,hi,1)
        +slope/reference.radius_scale*reference.density_moment(lo,hi,2))/volume;
    return {density,density*radial,density*reference.axial,
        reference.omega*inertia/RzViscousCases::integral(lo,hi,2),
        density*(reference.internal+(radial*radial+reference.axial*reference.axial)/2.L)
            +reference.omega*reference.omega*std::abs(inertia)/(2.L*volume),
        species0,density-species0};
}

/** Re-integrate returned points and compare all means with source integrals.
 * Actual recovery/simplex checks retain the original gates. This is finite
 * Gauss-node verification, not a claim about every r or bitwise conservation.
 */
template<class StateReader,class FractionReader>
inline void check_means(const StateReader& read,const FractionReader& fraction,int index,
    const RzReconstruction::RadialCell& geometry,const RzReconstruction::LimitedProfile& profile,
    const std::array<long double,7>& expected,const arch::state::Bounds& bounds={})
{
    if(!profile.valid)throw std::runtime_error("RZ independent mean check received an invalid profile");
    const long double lo=profile.baseline.density.lower,hi=profile.baseline.density.upper;
    const long double half=(hi-lo)/2.L,midpoint=lo+half,sign=hi<=0.?-1.L:1.L;
    std::array<long double,7> sums{};
    for(unsigned n=0;n<nodes.size();++n) {
        const double radius=static_cast<double>(midpoint+half*nodes[n]);
        const auto point=profile.at(radius);
        const double fractions[]{RzReconstruction::limited_fraction(read,fraction,index,0,
            geometry,radius,profile,point.rho),RzReconstruction::limited_fraction(read,fraction,
            index,1,geometry,radius,profile,point.rho)};
        if(arch::state::validate(point,fractions,2,1,bounds.density,bounds.internal_min,
                bounds.internal_max)!=arch::state::Status::valid)
            throw std::runtime_error("RZ returned profile point failed original physical/simplex gate");
        const std::array<long double,7> values{point.rho,point.mom_u,point.mom_v,
            point.mom_w,point.eng,static_cast<long double>(point.rho)*fractions[0],
            static_cast<long double>(point.rho)*fractions[1]};
        for(unsigned field=0;field<values.size();++field)
            sums[field]+=weights[n]*values[field]*(field==3?
                static_cast<long double>(radius)*radius:std::abs(static_cast<long double>(radius)));
    }
    for(unsigned field=0;field<sums.size();++field) {
        const long double measure=field==3?RzViscousCases::integral(lo,hi,2)
            :sign*RzViscousCases::integral(lo,hi,1);
        RzViscousCases::closure_check(static_cast<double>(half*sums[field]/measure),expected[field],
            "RZ limited profile changed a source-antiderivative V/W or species mean");
    }
}

/** Check analytic physical points where high/base share a known exact field.
 * Uniform cold rotation and smooth rho with constant translational velocities
 * have quadratic physical energy when Omega=0, so no production fit is an oracle.
 * A zero ray also has the explicit physical baseline for arbitrary quadratic rho.
 */
template<class StateReader,class FractionReader>
inline void check_physical_points(const StateReader& read,const FractionReader& fraction,
    int index,const RzReconstruction::RadialCell& geometry,
    const RzReconstruction::LimitedProfile& profile,const Reference& reference,const IdealGas& eos)
{
    const long double lo=profile.baseline.density.lower,hi=profile.baseline.density.upper;
    const long double half=(hi-lo)/2.L,midpoint=lo+half;
    for(int n=0;n<6;++n) {
        const double radius=static_cast<double>(n==0?lo:n==1?hi:midpoint+half*nodes[n-2]);
        const long double x=static_cast<long double>(radius)/reference.radius_scale;
        const long double rho=reference.density_scale*(reference.constant+reference.linear*x
            +reference.quadratic*x*x);
        const long double gradient=reference.density_scale/reference.radius_scale*(reference.linear
            +2.L*reference.quadratic*x);
        const long double ur=reference.radial,uz=reference.axial,omega=reference.omega;
        const auto point=profile.at(radius);
        const double composition[]{RzReconstruction::limited_fraction(read,fraction,index,0,
            geometry,radius,profile,point.rho),RzReconstruction::limited_fraction(read,fraction,
            index,1,geometry,radius,profile,point.rho)};
        const std::array<double,5> actual{point.rho,point.mom_u,point.mom_v,point.mom_w,point.eng};
        const std::array<long double,5> expected{rho,rho*ur,rho*uz,rho*omega*radius,
            rho*(reference.internal+(ur*ur+uz*uz+omega*omega*radius*radius)/2.L)};
        for(unsigned field=0;field<actual.size();++field)
            RzViscousCases::closure_check(actual[field],expected[field],
                "RZ physical profile differs from its independent polynomial field");
        RzViscousCases::closure_check(eos.get_pressure(point,composition),
            .4L*rho*reference.internal,"RZ limited physical ideal pressure mismatch");
        const auto derivative=profile.derivative(radius);
        const std::array<double,5> derivative_actual{derivative.rho,derivative.mom_u,
            derivative.mom_v,derivative.mom_w,derivative.eng};
        const std::array<long double,5> derivative_expected{gradient,gradient*ur,gradient*uz,
            omega*(gradient*radius+rho),gradient*(reference.internal+(ur*ur+uz*uz)/2.L)
                +omega*omega*(gradient*radius*radius+2.L*rho*radius)/2.L};
        for(unsigned field=0;field<derivative_actual.size();++field)
            RzViscousCases::closure_check(derivative_actual[field],derivative_expected[field],
                "RZ limited derivative does not differentiate the physical baseline/high ray");
    }
}

/** Exercise the actual Host traversal's native mean-EOS cache producer.
 * Independent physical antiderivatives supply rho_V, J/W and E_V. Distinct
 * species heat capacities/gammas make the pressure/acoustic checks sensitive
 * to the actual composition of every required stage cell. No expected cache
 * is constructed by calling the closure or its thermodynamic leaves.
 *
 * Both cold/hot production faces use genuine physical B point fluxes. The
 * separate direct raw-native bar test preserves the mixed-measure misuse
 * counterexample; neither mean-cache readiness nor finite faces prove a whole
 * native-RZ update, source coupling or long-time scientific qualification.
 */
inline void native_mean_cache_traversal()
{
    constexpr auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    SpeciesManager species;
    species.add_species("gas0",1.,1.,1.5,2.);
    species.add_species("gas1",2.,1.,1.75,4.);
    IdealGas eos(1.4,species);
    int required_checks=0;
    for(int hot=0;hot<2;++hot) {
        Reference reference;
        if(hot) {
            reference.constant=7.L/8.L;reference.quadratic=1.L/4.L;
            reference.internal=4.L;
        }
        Grid grid(amr::MAX_NG,0.,amr::BLOCK_NX,-.5,.5,0.,1.);
        grid.dim=2;grid.geometry="cylindrical";grid.InitializeTopology(rz);
        FluidState state;populate(state,grid,reference,.5L,0.L);
        for(int j=0;j<grid.GetTotalY();++j)for(int i=0;i<grid.GetTotalX();++i) {
            const int cell=grid.GetIndex(i,j,0);
            const double x0=.25+(i-grid.Is())/128.+(j-grid.Js())/256.;
            state.X(0,cell)=x0;state.X(1,cell)=1.-x0;
        }
        const auto original_rho=state.rho,original_mr=state.mom_u;
        const auto original_mz=state.mom_v,original_mphi=state.mom_w;
        const auto original_energy=state.eng,original_fractions=state.mass_fractions;
        if(!hot) {
            const auto native=state.get(grid.GetIndex(grid.Is(),grid.Js(),0));
            RzViscousCases::check(native.rho,1.L,"RZ cold traversal rho_V input");
            RzViscousCases::check(native.mom_w,3.L/4.L,"RZ cold traversal J/W input");
            RzViscousCases::check(native.eng,17.L/64.L,"RZ cold traversal E_V input");
            if(arch::state::recover(native).status!=arch::state::Status::unresolved_energy)
                throw std::runtime_error("RZ cold traversal did not isolate raw mixed-measure recovery");
        }
        for(int dir=0;dir<2;++dir) {
            FluxAdmissibility::MeanThermoCache cache;
            cache.geometry_semantics=rz;cache.reset(grid.GetTotalSize());
            std::vector<FluidVector> flux(grid.GetTotalSize());
            std::vector<double> species_flux(2*grid.GetTotalSize());
            FluxTraversal::compute_fluxes<FluxHLLC<PCMReconstruction>,PCMReconstruction>(
                state,eos,grid,flux,species_flux,dir,.1,&cache);

            // Required mean cells form a closed strip, one ghost on each
            // normal side. This set is checked independently of traversal order.
            for(int j=0;j<grid.GetTotalY();++j)for(int i=0;i<grid.GetTotalX();++i) {
                const int cell=grid.GetIndex(i,j,0);
                const bool required=dir==0
                    ?i>=grid.Is()-1&&i<=grid.Ie()&&j>=grid.Js()&&j<grid.Je()
                    :i>=grid.Is()&&i<grid.Ie()&&j>=grid.Js()-1&&j<=grid.Je();
                if(bool(cache.ready[cell])!=required)
                    throw std::runtime_error("RZ actual traversal published an incorrect mean-cache cell set");
                if(!required)continue;
                const long double lo=grid.GetFacePosL(i),hi=grid.GetFacePosR(i);
                const long double rho=reference.density_moment(lo,hi,1)
                    /RzViscousCases::integral(lo,hi,1);
                const long double x0=state.X(0,cell),x1=state.X(1,cell);
                // Cv=(2,4), gamma=(3/2,7/4): gamma_mix-1=(X0+3X1)/(2X0+4X1).
                const long double gamma_minus_one=(x0+3.L*x1)/(2.L*x0+4.L*x1);
                const long double pressure=gamma_minus_one*rho*reference.internal;
                const long double sound=std::sqrt((1.L+gamma_minus_one)*pressure/rho);
                RzViscousCases::check(cache.pressure[cell],pressure,
                    "RZ actual traversal mean pressure differs from independent physical rho/e/X");
                RzViscousCases::check(cache.sound_speed[cell],sound,
                    "RZ actual traversal mean sound speed differs from independent physical rho/e/X");
                ++required_checks;
            }
            // Actual selected Native faces must be finite in both directions,
            // including the cold first cell whose raw point recovery is invalid.
            // All existing fluid/species finite checks also remain on hot faces.
            for(int j=grid.Js();j<(dir==1?grid.Je()+1:grid.Je());++j)
                for(int i=grid.Is();i<(dir==0?grid.Ie()+1:grid.Ie());++i) {
                    const int face=grid.GetIndex(i,j,0);
                    const auto value=flux[face];
                    if(!std::isfinite(value.rho)||!std::isfinite(value.mom_u)
                       ||!std::isfinite(value.mom_v)||!std::isfinite(value.mom_w)
                       ||!std::isfinite(value.eng)||!std::isfinite(species_flux[face])
                       ||!std::isfinite(species_flux[grid.GetTotalSize()+face]))
                        throw std::runtime_error("RZ cold/hot traversal returned a nonfinite actual fluid/species flux");
                }
            if(!hot&&dir==1) {
                // Preserve the independent negative example at the exact
                // owning raw Native states, not at the new physical B path.
                // Here U_L=U_R=(1,0,0,3/4,17/64). The axial pressure jump
                // adds only normal bar momentum; its generic point e is
                // -1/64-delta_mz^2/2, so physical cached P/c cannot qualify it.
                const int left=grid.GetIndex(grid.Is(),grid.Js(),0);
                const int right=left+grid.stride_y;
                const double x_left[]{state.X(0,left),state.X(1,left)};
                const double x_right[]{state.X(0,right),state.X(1,right)};
                const double high_species[]{species_flux[right],
                    species_flux[grid.GetTotalSize()+right]};
                const auto raw_factor=FluxAdmissibility::point_face_blend_with_thermo(
                    state.get(left),state.get(right),x_left,x_right,2,
                    cache.pressure[left],cache.sound_speed[left],
                    cache.pressure[right],cache.sound_speed[right],1,
                    flux[right],high_species);
                if(raw_factor.valid)
                    throw std::runtime_error("Physical cache P/c falsely qualified a raw-native point bar");
            }
            if(state.rho!=original_rho||state.mom_u!=original_mr||state.mom_v!=original_mz
               ||state.mom_w!=original_mphi||state.eng!=original_energy
               ||state.mass_fractions!=original_fractions)
                throw std::runtime_error("RZ traversal changed its immutable native stage inputs");
        }

        if(hot) {
            // An invalid required cell is never marked ready. Earlier valid
            // cells may already be published: this is no whole-cache atomicity claim.
            const int invalid_cell=grid.GetIndex(grid.Is()+2,grid.Js(),0);
            auto invalid=state.get(invalid_cell);invalid.eng=0.;state.set(invalid_cell,invalid);
            FluxAdmissibility::MeanThermoCache cache;
            cache.geometry_semantics=rz;cache.reset(grid.GetTotalSize());
            std::vector<FluidVector> flux(grid.GetTotalSize());
            std::vector<double> species_flux(2*grid.GetTotalSize());
            bool rejected=false;
            try {
                FluxTraversal::compute_fluxes<FluxHLLC<PCMReconstruction>,PCMReconstruction>(
                    state,eos,grid,flux,species_flux,0,.1,&cache);
            } catch(const std::runtime_error&) {rejected=true;}
            if(!rejected||cache.ready[invalid_cell])
                throw std::runtime_error("RZ actual traversal published an invalid closure as a ready mean");
        }
    }
    std::cout<<"RZ_NATIVE_MEAN_CACHE required_cells="<<required_checks
        <<" hot_flux_finite=1 cold_native_flux_finite_both_dirs=1"
        <<" cold_raw_native_point_bar_rejected=1 PASS\n";
}

/** Real-EOS acceptance from independent density and inertia antiderivatives.
 * A cold native mean is provisional until its real density ghosts provide I_*.
 * Mean acceptance does not imply that cancellation at every point is resolved.
 */
inline void native_acceptance_leaves()
{
    constexpr auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    SpeciesManager species;species.add_species("gas0",1.,1.,1.4,3.);
    species.add_species("gas1",2.,1.,1.4,3.);IdealGas eos(1.4,species);
    const double fractions[]{.5,.5};
    for(bool variable:{false,true}) {
        Reference reference;
        if(variable) {reference.constant=7.L/8.L;reference.quadratic=1.L/4.L;}
        Grid grid(amr::MAX_NG,0.,amr::BLOCK_NX,-.5,.5,0.,1.);
        grid.dim=2;grid.geometry="cylindrical";grid.InitializeTopology(rz);
        FluidState state;populate(state,grid,reference,.5L,0.L);
        const auto before=state;
        const auto view=GridMetrics::make_geometry_view(grid,rz);
        const int i=grid.Is(),index=grid.GetIndex(i,grid.Js()+1,0);
        const auto read=[&](int n){return state.get(n);};
        const auto raw=read(index);
        if(arch::state::recover(raw).status!=arch::state::Status::unresolved_energy
            ||RzThermodynamics::provisional_native_state(raw,fractions,2,1)
                !=arch::state::Status::valid)
            throw std::runtime_error("Cold native mean was confused with a point momentum");
        const auto cell=RzThermodynamics::make_cell(read,index,view,i);
        if(RzThermodynamics::validate_mean_eos(cell,fractions,2,eos)
            !=arch::state::Status::valid)
            throw std::runtime_error("Real EOS rejected independent cold native means");
        const long double mass=reference.density_moment(0.L,1.L,1);
        const long double rho=2.L*mass;
        RzViscousCases::closure_check(cell.internal,reference.internal,
            "Native EOS closure disagrees with independent thermal energy");
        RzViscousCases::closure_check(eos.get_temperature(raw.rho,cell.internal,fractions),
            reference.internal/3.L,"Native mean temperature differs from caloric reference");
        const double pressure=eos.get_pressure(cell.effective_mean,fractions);
        RzViscousCases::closure_check(pressure,.4L*rho*reference.internal,
            "Native mean pressure differs from independent density integral");
        const double sound=eos.get_sound_speed(cell.effective_mean,pressure,fractions);
        RzViscousCases::closure_check(sound*sound,1.4L*.4L*reference.internal,
            "Native sound speed differs from independent caloric reference");
        RzThermodynamics::validate_patch_eos(state,grid,2,{},eos);
        if(state.rho!=before.rho||state.mom_u!=before.mom_u||state.mom_v!=before.mom_v
            ||state.mom_w!=before.mom_w||state.eng!=before.eng
            ||state.mass_fractions!=before.mass_fractions)
            throw std::runtime_error("Read-only native acceptance altered conserved means");
        const double strided[]{.5,77.,.5};
        if(RzThermodynamics::provisional_native_state(raw,strided,2,2)
            !=arch::state::Status::valid)
            throw std::runtime_error("Native acceptance rejected actual species-major stride");
        const double invalid[][2]{{-.1,1.1},{.25,.25},
            {std::numeric_limits<double>::quiet_NaN(),1.}};
        for(const auto& composition:invalid)
            if(RzThermodynamics::provisional_native_state(raw,composition,2,1)
                !=arch::state::Status::invalid_composition)
                throw std::runtime_error("Native provisional state accepted invalid composition");
        if(RzThermodynamics::provisional_native_state(raw,nullptr,2,1)
                !=arch::state::Status::invalid_composition
            ||RzThermodynamics::provisional_native_state(raw,fractions,2,0)
                !=arch::state::Status::invalid_composition
            ||RzThermodynamics::provisional_native_state(raw,nullptr,0,1)
                !=arch::state::Status::valid)
            throw std::runtime_error("Native composition layout validation changed");
        auto invalid_rho=raw;invalid_rho.rho=0.;
        if(RzThermodynamics::provisional_native_state(invalid_rho,fractions,2,1)
            !=arch::state::Status::nonpositive_density)
            throw std::runtime_error("Native provisional state accepted exact vacuum");
        arch::state::Bounds malformed;malformed.internal_min=-1.;
        if(RzThermodynamics::provisional_native_state(raw,fractions,2,1,malformed)
            !=arch::state::Status::invalid_thermodynamics)
            throw std::runtime_error("Native provisional state accepted invalid configured bounds");
    }
    {
        Reference reference;reference.internal=std::ldexp(11.L,-54);
        Grid grid(amr::MAX_NG,0.,amr::BLOCK_NX,-.5,.5,0.,1.);
        grid.dim=2;grid.geometry="cylindrical";grid.InitializeTopology(rz);
        FluidState state;populate(state,grid,reference,.5L,0.L);
        const auto view=GridMetrics::make_geometry_view(grid,rz);
        const int i=grid.Is(),index=grid.GetIndex(i,grid.Js()+1,0);
        const auto cell=RzThermodynamics::make_cell([&](int n){return state.get(n);},index,view,i);
        if(RzThermodynamics::validate_mean_eos(cell,fractions,2,eos)
                !=arch::state::Status::valid
            ||arch::state::validate_eos(RzThermodynamics::base_point(cell,
                static_cast<double>(.5L+.5L*nodes.back())),fractions,2,{},eos)
                !=arch::state::Status::unresolved_energy)
            throw std::runtime_error("Mean acceptance concealed the independent unresolved point");
        bool rejected=false;
        try {RzThermodynamics::validate_patch_eos(state,grid,2,{},eos);}
        catch(const std::runtime_error&) {rejected=true;}
        if(!rejected)throw std::runtime_error("Post-ghost gate accepted an unresolved physical point");
    }
    std::cout<<"RZ_NATIVE_ACCEPTANCE real_eos=1 independent_integrals=1 point_veto=1 PASS\n";
}

/** Shared pure-profile verification with real stencil ownership and two species.
 * These local mathematical cases do not qualify hierarchy stages or the full
 * scientific admissibility of every neighboring cell.
 */
inline void native_profile()
{
    constexpr auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    SpeciesManager species;species.add_species("gas0",1.,1.,1.4,3.);
    species.add_species("gas1",2.,1.,1.4,3.);IdealGas eos(1.4,species);
    int cases=0;
    for(int mode=0;mode<3;++mode) {
        Reference reference;
        if(mode>0) {
            reference.constant=7.L/8.L;reference.quadratic=1.L/4.L;
            reference.radial=1.L/8.L;reference.axial=1.L/4.L;
        }
        if(mode==1)reference.omega=0.L;
        const double inner=mode==0?0.:1.;
        Grid grid(amr::MAX_NG,inner,inner+amr::BLOCK_NX,-.5,.5,0.,1.);
        grid.dim=2;grid.geometry="cylindrical";grid.InitializeTopology(rz);
        const auto view=GridMetrics::make_geometry_view(grid,rz);
        FluidState state;populate(state,grid,reference,mode==1?1.L/4.L:1.L/2.L,
            mode==1?1.L/64.L:0.L);
        const int i=grid.Is(),index=grid.GetIndex(i,grid.Js()+1,0);
        if(mode==2) {state.X(0,index)=0.;state.X(1,index)=1.;}
        const auto read=[&](int n){return state.get(n);};
        const auto fraction=[&](int s,int n){return state.X(s,n);};
        const auto geometry=RzReconstruction::radial_cell(view,i);
        const auto baseline=RzThermodynamics::make_cell(read,index,view,i);
        const auto profile=RzReconstruction::limited_profile(read,fraction,index,2,geometry,baseline);
        if(!profile.valid)throw std::runtime_error("RZ analytic native high profile rejected");
        if(mode==0&&arch::state::recover(read(index)).status!=arch::state::Status::unresolved_energy)
            throw std::runtime_error("RZ cold profile no longer isolates raw mixed-measure recovery");
        if(mode<2&&profile.theta!=1.)
            throw std::runtime_error("RZ smooth admissible high profile unnecessarily discarded");
        if(mode==2&&profile.theta!=0.)
            throw std::runtime_error("RZ zero central species failed to enforce the common zero ray");
        check_physical_points(read,fraction,index,geometry,profile,reference,eos);
        const long double lo=grid.GetFacePosL(i),hi=grid.GetFacePosR(i);
        auto expected=means(reference,lo,hi,mode==1?1.L/4.L:1.L/2.L,
            mode==1?1.L/64.L:0.L);
        if(mode==2) {expected[5]=0.L;expected[6]=expected[0];}
        check_means(read,fraction,index,geometry,profile,expected);
        if(mode<2) {
            auto ray=profile;ray.theta=.5;
            check_means(read,fraction,index,geometry,ray,expected);
            ray.theta=0.;check_means(read,fraction,index,geometry,ray,expected);
        } else {
            auto poisoned=profile;
            poisoned.polynomial.density.constant=std::numeric_limits<double>::quiet_NaN();
            poisoned.polynomial.radial.constant=std::numeric_limits<double>::quiet_NaN();
            poisoned.polynomial.angular.cubic=std::numeric_limits<double>::quiet_NaN();
            poisoned.polynomial.energy.constant=std::numeric_limits<double>::quiet_NaN();
            check_physical_points(read,fraction,index,geometry,poisoned,reference,eos);
            check_means(read,fraction,index,geometry,poisoned,expected);
        }

        const auto neighbour_baseline=RzThermodynamics::make_cell(read,index+1,view,i+1);
        if(RzReconstruction::limited_profile(read,fraction,index,2,geometry,neighbour_baseline).valid)
            throw std::runtime_error("RZ high profile accepted an unrelated cell origin/support");
        auto invalid_geometry=geometry;invalid_geometry.spacing*=2.;
        if(RzReconstruction::limited_profile(read,fraction,index,2,invalid_geometry,baseline).valid)
            throw std::runtime_error("RZ high profile accepted mismatched support spacing");
        for(int offset:{1,2})for(int field=0;field<5;++field) {
            const auto saved=state.get(index+offset);auto invalid=saved;
            const double bad=std::numeric_limits<double>::infinity();
            if(field==0)invalid.rho=bad;
            if(field==1)invalid.mom_u=bad;
            if(field==2)invalid.mom_v=bad;
            if(field==3)invalid.mom_w=bad;
            if(field==4)invalid.eng=bad;
            state.set(index+offset,invalid);
            const bool accepted=RzReconstruction::limited_profile(read,fraction,index,2,geometry,baseline).valid;
            state.set(index+offset,saved);
            if(accepted)throw std::runtime_error("RZ profile accepted a nonfinite native neighbour field");
        }
        for(int malformed=0;malformed<6;++malformed) {
            arch::state::Bounds bounds;
            if(malformed==0)bounds.density=std::numeric_limits<double>::quiet_NaN();
            if(malformed==1)bounds.density=-1.;
            if(malformed==2)bounds.internal_min=std::numeric_limits<double>::quiet_NaN();
            if(malformed==3)bounds.internal_min=-1.;
            if(malformed==4)bounds.internal_max=std::numeric_limits<double>::quiet_NaN();
            if(malformed==5) {bounds.internal_min=1.;bounds.internal_max=.5;}
            if(RzReconstruction::limited_profile(read,fraction,index,2,geometry,baseline,bounds).valid)
                throw std::runtime_error("RZ profile accepted malformed numerical bounds");
        }
        ++cases;
    }

    // Density correction has a nonzero role here. Source densities are
    // piecewise constants (20,1,20); their old quadratic has rho(3/2)=-7/12.
    // E=1000 and X=(1/2,1/2) are independently constant native source inputs.
    // The positive rho* and corrected q must retain an admissible theta=1 ray.
    {
        Grid grid(amr::MAX_NG,1.,1.+amr::BLOCK_NX,-.5,.5,0.,1.);
        grid.dim=2;grid.geometry="cylindrical";grid.InitializeTopology(rz);
        const auto view=GridMetrics::make_geometry_view(grid,rz);
        FluidState state;state.Preallocate(grid.GetTotalSize());state.InitSpecies(2);
        for(int n=0;n<grid.GetTotalSize();++n) {
            state.set(n,{20.,0.,0.,0.,1000.});state.X(0,n)=.5;state.X(1,n)=.5;
        }
        const int i=grid.Is(),index=grid.GetIndex(i,grid.Js()+1,0);
        state.set(index,{1.,0.,0.,0.,1000.});
        const auto read=[&](int n){return state.get(n);};
        const auto fraction=[&](int s,int n){return state.X(s,n);};
        const auto geometry=RzReconstruction::radial_cell(view,i);
        const auto baseline=RzThermodynamics::make_cell(read,index,view,i);
        const auto profile=RzReconstruction::limited_profile(read,fraction,index,2,geometry,baseline);
        if(!profile.valid||profile.theta!=1.||!(profile.at(1.5).rho>0.))
            throw std::runtime_error("RZ density/species correction failed its independent trough witness");
        check_means(read,fraction,index,geometry,profile,{1.L,0.L,0.L,0.L,1000.L,.5L,.5L});
        for(long double node:nodes) {
            const double radius=static_cast<double>(1.5L+.5L*node);
            const auto point=profile.at(radius);
            const double composition[]{RzReconstruction::limited_fraction(read,fraction,index,0,
                geometry,radius,profile,point.rho),RzReconstruction::limited_fraction(read,fraction,
                index,1,geometry,radius,profile,point.rho)};
            RzViscousCases::closure_check(eos.get_pressure(point,composition),400.L,
                "RZ corrected-density high profile changed independent constant-energy pressure");
        }
        ++cases;
    }

    // A local nonconvex-upper-bound stencil, not an accepted physical hierarchy:
    // rho=E=1, high mr=6r-5, native mean mr=-1 on [0,1]. The baseline and
    // high r=1 endpoint have e=1/2; their midpoint has e=1 and is forbidden.
    {
        Grid grid(amr::MAX_NG,0.,amr::BLOCK_NX,-.5,.5,0.,1.);
        grid.dim=2;grid.geometry="cylindrical";grid.InitializeTopology(rz);
        const auto view=GridMetrics::make_geometry_view(grid,rz);
        FluidState state;state.Preallocate(grid.GetTotalSize());state.InitSpecies(2);
        for(int j=0;j<grid.GetTotalY();++j)for(int i=0;i<grid.GetTotalX();++i) {
            const long double lo=grid.GetFacePosL(i),hi=grid.GetFacePosR(i);
            const long double mr=6.L*RzViscousCases::integral(lo,hi,2)
                /RzViscousCases::integral(lo,hi,1)-5.L;
            const int n=grid.GetIndex(i,j,0);
            state.set(n,{1.,static_cast<double>(mr),0.,0.,1.});
            state.X(0,n)=.5;state.X(1,n)=.5;
        }
        const int i=grid.Is(),index=grid.GetIndex(i,grid.Js()+1,0);
        const auto read=[&](int n){return state.get(n);};
        const auto fraction=[&](int s,int n){return state.X(s,n);};
        const auto geometry=RzReconstruction::radial_cell(view,i);
        const auto baseline=RzThermodynamics::make_cell(read,index,view,i);
        arch::state::Bounds bounds;bounds.internal_max=.5;
        if(arch::state::validate({1.,-1.,0.,0.,1.},nullptr,0,1,0.,0.,.5)
                !=arch::state::Status::valid
           ||arch::state::validate({1.,1.,0.,0.,1.},nullptr,0,1,0.,0.,.5)
                !=arch::state::Status::valid
           ||arch::state::validate({1.,0.,0.,0.,1.},nullptr,0,1,0.,0.,.5)
                !=arch::state::Status::energy_ceiling)
            throw std::runtime_error("RZ independent upper-energy nonconvex counterexample changed");
        const auto profile=RzReconstruction::limited_profile(read,fraction,index,2,geometry,baseline,bounds);
        if(!profile.valid)throw std::runtime_error("RZ nonconvex profile discarded its valid baseline");
        check_means(read,fraction,index,geometry,profile,{1.L,-1.L,0.L,0.L,1.L,.5L,.5L},bounds);
        for(int n=0;n<6;++n) {
            const double radius=static_cast<double>(n==0?0.L:n==1?1.L:.5L+.5L*nodes[n-2]);
            const auto point=profile.at(radius);
            if(arch::state::validate(point,nullptr,0,1,0.,0.,.5)!=arch::state::Status::valid)
                throw std::runtime_error("RZ nonconvex search returned an unchecked actual candidate");
            const long double mr=-1.L+profile.theta*(6.L*radius-4.L);
            const double composition[]{RzReconstruction::limited_fraction(read,fraction,index,0,
                geometry,radius,profile,point.rho),RzReconstruction::limited_fraction(read,fraction,
                index,1,geometry,radius,profile,point.rho)};
            RzViscousCases::closure_check(eos.get_pressure(point,composition),.4L*(1.L-mr*mr/2.L),
                "RZ nonconvex candidate pressure differs from independent linear-ray polynomial");
        }
        ++cases;
    }

    // Mean recovery passes, but the outer Gauss point's physical cancellation
    // fails the unchanged 8eps rule. No profile fallback may declare it valid.
    {
        Reference reference;reference.internal=std::ldexp(11.L,-54);
        Grid grid(amr::MAX_NG,0.,amr::BLOCK_NX,-.5,.5,0.,1.);
        grid.dim=2;grid.geometry="cylindrical";grid.InitializeTopology(rz);
        const auto view=GridMetrics::make_geometry_view(grid,rz);
        FluidState state;populate(state,grid,reference,.5L,0.L);
        const int i=grid.Is(),index=grid.GetIndex(i,grid.Js()+1,0);
        const auto read=[&](int n){return state.get(n);};
        const auto fraction=[&](int s,int n){return state.X(s,n);};
        const auto geometry=RzReconstruction::radial_cell(view,i);
        const auto baseline=RzThermodynamics::make_cell(read,index,view,i);
        if(!baseline.valid()||arch::state::recover(RzThermodynamics::base_point(baseline,
                static_cast<double>(.5L+.5L*nodes.back()))).status!=arch::state::Status::unresolved_energy)
            throw std::runtime_error("RZ unresolved baseline witness no longer isolates the point gate");
        if(RzReconstruction::limited_profile(read,fraction,index,2,geometry,baseline).valid)
            throw std::runtime_error("RZ profile accepted an unresolvable physical baseline point");
        ++cases;
    }

    // Pure zero-ray fraction-accessor contract: rho*X underflows but X itself
    // is a representable positive input. This is not an evolved trace-mass
    // conservation certificate, and no floor or normalization is introduced.
    {
        Reference reference;reference.density_scale=std::ldexp(1.L,-300);
        reference.omega=0.L;reference.internal=1.L;
        Grid grid(amr::MAX_NG,0.,amr::BLOCK_NX,-.5,.5,0.,1.);
        grid.dim=2;grid.geometry="cylindrical";grid.InitializeTopology(rz);
        const auto view=GridMetrics::make_geometry_view(grid,rz);
        FluidState state;populate(state,grid,reference,.5L,0.L);
        const double trace=std::numeric_limits<double>::denorm_min();
        for(int n=0;n<grid.GetTotalSize();++n) {state.X(0,n)=trace;state.X(1,n)=1.;}
        const int i=grid.Is(),index=grid.GetIndex(i,grid.Js()+1,0);
        const auto read=[&](int n){return state.get(n);};
        const auto fraction=[&](int s,int n){return state.X(s,n);};
        const auto geometry=RzReconstruction::radial_cell(view,i);
        const auto baseline=RzThermodynamics::make_cell(read,index,view,i);
        auto profile=RzReconstruction::limited_profile(read,fraction,index,2,geometry,baseline);
        if(!profile.valid)throw std::runtime_error("RZ trace accessor profile rejected finite legal input");
        profile.theta=0.;profile.polynomial.energy.constant=std::numeric_limits<double>::quiet_NaN();
        const auto point=profile.at(.5);
        if(!std::isfinite(point.eng)||RzReconstruction::limited_fraction(read,fraction,index,0,
                geometry,.5,profile,point.rho)!=trace)
            throw std::runtime_error("RZ zero ray evaluated NaN high state or lost representable central X");
        ++cases;
    }
    std::cout<<"RZ_NATIVE_LIMITED_PROFILE cases="<<cases<<" PASS\n";
    native_mean_cache_traversal();
    native_acceptance_leaves();
}
} // namespace RzReconstructionCases
