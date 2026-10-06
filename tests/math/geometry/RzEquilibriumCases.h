#pragma once

// Independent full-ring polynomial integrals for the frozen rotating tests.
// The production reconstruction is used only for its output, never as an oracle.
using RzMetricCases::mean_power;

inline int audit_rz_rotating_equilibrium()
{
    SpeciesManager species;species.add_species("gas",1.,1.,1.4,3.);
    IdealGas eos(1.4,species);
    constexpr auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    constexpr long double gamma_minus_one=static_cast<long double>(1.4)-1.L;
    bool passed=true;
    for(double inner:{0.,1.})for(double omega:{1.,4.})for(double cubic:{0.,.25}) {
        std::array<long double,7> previous{};
        for(int roots:{1,2,4,8}) {
            long double sum_abs=0.,sum_square=0.,volume=0.,maximum=0.;
            long double local_abs=0.,local_square=0.,local_volume=0.,local_maximum=0.,first_two=0.;
            long double J=0.,analytic_J=0.,source_maximum=0.,legacy_source_maximum=0.;
            const double h=1./(roots*amr::BLOCK_NX);
            const double domain_max=inner+1.;
            const double maximum_velocity=domain_max*(omega+cubic*domain_max*domain_max);
            const double pressure_scale=5.+maximum_velocity*maximum_velocity;
            // Forward rounding envelope for this fixed ideal-gas polynomial
            // stencil: conservative moment solves, EOS/flux arithmetic and
            // opposite metric face terms. It is prescribed from FP64 and
            // the input scale before inspecting any residual. It diagnoses
            // unresolvable order; it never replaces the resolved >=1.8 gate.
            const long double rounding=1024.L*std::numeric_limits<double>::epsilon()*pressure_scale/h;
            for(int block=0;block<roots;++block) {
                Grid g(amr::MAX_NG,inner+static_cast<double>(block)/roots,
                    inner+static_cast<double>(block+1)/roots,-.125,.125,0.,1.);
                g.dim=2;g.geometry="cylindrical";g.InitializeTopology(rz);
                const auto geometry=GridMetrics::make_geometry_view(g,rz);
                FluidState state;state.Preallocate(g.GetTotalSize());state.InitSpecies(1);
                for(int j=0;j<g.GetTotalY();++j)for(int i=0;i<g.GetTotalX();++i) {
                    long double lo=g.GetFacePosL(i),hi=g.GetFacePosR(i),sign=1.;
                    if(hi<=0.) {const auto old=lo;lo=-hi;hi=-old;sign=-1.;}
                    const auto r2=mean_power(lo,hi,2,1),r4=mean_power(lo,hi,4,1),r6=mean_power(lo,hi,6,1);
                    const long double p=5.L+omega*omega*r2/2.L+omega*cubic*r4/2.L+cubic*cubic*r6/6.L;
                    const long double kinetic=(omega*omega*r2+2.L*omega*cubic*r4+cubic*cubic*r6)/2.L;
                    const long double angular=sign*(omega*mean_power(lo,hi,1,2)+cubic*mean_power(lo,hi,3,2));
                    const int cell=g.GetIndex(i,j,0);
                    state.set(cell,{1.,0.,0.,static_cast<double>(angular),static_cast<double>(p/gamma_minus_one+kinetic)});
                    state.X(0,cell)=1.;
                }
                const int size=g.GetTotalSize();
                std::vector<FluidVector> delta(size),flux(size);
                std::vector<double> ds(size),sf(size);
                TimeIntegration::evaluate_all_dimensions<FluxHLLC<MusclReconstruction<McLimiter>>>(
                    nullptr,-1,state,eos,g,1.,delta,ds,flux,sf,nullptr,0.,1.,true,rz);
                const long double pi=std::acos(-1.L);
                for(int j=g.Js();j<g.Je();++j)for(int i=g.Is();i<g.Ie();++i) {
                    const int cell=g.GetIndex(i,j,0);
                    const long double lo=g.GetFacePosL(i),hi=g.GetFacePosR(i);
                    const long double V=pi*(hi*hi-lo*lo)*g.dx2;
                    const long double W=2.L*pi*(hi*hi*hi-lo*lo*lo)*g.dx2/3.L;
                    const long double error=std::abs(delta[cell].mom_u);
                    if(!std::isfinite(error))throw std::runtime_error("RZ equilibrium nonfinite native force");
                    sum_abs+=V*error;sum_square+=V*error*error;volume+=V;
                    maximum=std::max(maximum,error);
                    if(lo<inner+2.*h)first_two=std::max(first_two,error);
                    if(hi<=inner+.125L) {
                        local_abs+=V*error;local_square+=V*error*error;
                        local_volume+=V;local_maximum=std::max(local_maximum,error);
                    }
                    const auto pressure=[&](long double r) {
                        const long double r2=r*r;
                        return 5.L+omega*omega*r2/2.L+omega*cubic*r2*r2/2.L+cubic*cubic*r2*r2*r2/6.L;
                    };
                    const long double exact_divergence=2.L*(hi*pressure(hi)-lo*pressure(lo))/(hi*hi-lo*lo);
                    double x=1.;FluidVector integrated{},legacy{};
                    if(!TimeIntegration::add_rz_integrated_geometric_source(
                        [&state](int c){return state.get(c);},[&state](int k,int c){return state.X(k,c);},
                        cell,1,eos,geometry,i,1.,&x,integrated))
                        throw std::runtime_error("RZ equilibrium integrated source rejected");
                    TimeIntegration::add_rz_geometric_source_cell(state.get(cell),&x,eos,
                        static_cast<double>(lo),static_cast<double>(hi),1.,legacy);
                    source_maximum=std::max(source_maximum,std::abs(integrated.mom_u-exact_divergence));
                    legacy_source_maximum=std::max(legacy_source_maximum,std::abs(legacy.mom_u-exact_divergence));
                    J+=state.mom_w[cell]*W;
                    analytic_J+=pi*g.dx2*(omega*(std::pow(hi,4)-std::pow(lo,4))/2.L
                        +cubic*(std::pow(hi,6)-std::pow(lo,6))/3.L);
                }
            }
            if(!(volume>0.)||!(local_volume>0.))throw std::runtime_error("RZ equilibrium missing native measure");
            const std::array<long double,7> errors{sum_abs/volume,std::sqrt(sum_square/volume),maximum,
                first_two,local_abs/local_volume,std::sqrt(local_square/local_volume),local_maximum};
            std::array<double,7> orders{};
            bool row_pass=true,roundoff=true;
            for(int n=0;n<7;++n) {
                orders[n]=roots==1?0.:static_cast<double>(std::log2(previous[n]/errors[n]));
                const bool unresolved=errors[n]<=rounding && (roots==1||previous[n]<=rounding/2.L);
                roundoff=roundoff&&unresolved;
                if(roots>=4&&!unresolved&&(!std::isfinite(orders[n])||orders[n]<1.8))row_pass=false;
            }
            const long double jerror=std::abs(J-analytic_J)/std::abs(analytic_J);
            if(!std::isfinite(jerror)||jerror>1.e-12L)row_pass=false;
            passed=passed&&row_pass;
            std::cout<<"RZ_EQUILIBRIUM inner="<<inner<<" Omega="<<omega<<" cubic="<<cubic
                <<" cells="<<roots*amr::BLOCK_NX<<" L1="<<static_cast<double>(errors[0])
                <<" rms="<<static_cast<double>(errors[1])<<" Linf="<<static_cast<double>(errors[2])
                <<" first_two_Linf="<<static_cast<double>(errors[3])
                <<" local_L1="<<static_cast<double>(errors[4])<<" local_rms="<<static_cast<double>(errors[5])
                <<" local_Linf="<<static_cast<double>(errors[6])
                <<" p_L1="<<orders[0]<<" p_rms="<<orders[1]<<" p_Linf="<<orders[2]
                <<" p_first_two="<<orders[3]<<" p_local_L1="<<orders[4]<<" p_local_rms="<<orders[5]<<" p_local_Linf="<<orders[6]
                <<" integrated_source_error="<<static_cast<double>(source_maximum)
                <<" legacy_source_error="<<static_cast<double>(legacy_source_maximum)
                <<" roundoff_bound="<<static_cast<double>(rounding)
                <<" error_regime="<<(roundoff?"ROUNDING":"RESOLVED")
                <<" J_input_error="<<static_cast<double>(jerror)<<" status="<<(row_pass?"PASS":"FAIL")<<'\n';
            previous=errors;
        }
    }
    std::cout<<"RZ_EQUILIBRIUM_SPATIAL_GATE="<<(passed?"PASS":"NOT_CLEARED")<<'\n';
    return passed?0:2;
}
