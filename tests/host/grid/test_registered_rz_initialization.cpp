// Real registered Gaussian Setup/Init with independent native cell integral.
// Engineering initialization consistency only; no timestep or new model.
#include "core/problem/ProblemRegistry.h"
#include "core/config/RuntimeParams.h"
#include "driver/initialization/InitialMesh.h"
#include "physics/constant/PhysicalConstants.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <limits>
#include <cmath>
static void require(bool b,const char* m){if(!b)throw std::runtime_error(m);}
static long double gaussian_average(long double a,long double b,long double c,long double d){
    constexpr long double sigma=.2L,rc=.5L,zc=.25L;
    const auto primitive=[&](long double x){
        return rc*sigma*std::sqrt(arch::constants::math::pi)/2*std::erf((x-rc)/sigma)
            -sigma*sigma/2*std::exp(-std::pow((x-rc)/sigma,2));
    };
    const long double radial=primitive(b)-primitive(a);
    const long double axial=sigma*std::sqrt(arch::constants::math::pi)/2*
        (std::erf((d-zc)/sigma)-std::erf((c-zc)/sigma));
    return .5L*radial*axial/((b*b-a*a)/2*(d-c));
}
int main(int argc,char**argv) {
 try {
    require(argc==2,"strict engineering input required");
    std::ifstream file(argv[1]);std::ostringstream text;text<<file.rdbuf();
    double previous=0.;
    for(int roots:{1,2,4}) {
        std::string input=text.str();
        const auto replace=[&](const std::string& a,const std::string& b){
            const auto at=input.find(a);require(at!=std::string::npos,"fixture root declaration missing");
            input.replace(at,a.size(),b);
        };
        replace("nblockx1 = 1","nblockx1 = "+std::to_string(roots));
        replace("nblockx2 = 1","nblockx2 = "+std::to_string(roots));
        replace("max_blocks = 4","max_blocks = 32");
        auto config=RuntimeParams::LoadText(input,"Gaussian",
            arch::config::ConfigurationPurpose::InitialState);
        auto model=ProblemRegistry::Get().Create("Gaussian");
        require(bool(model),"Gaussian missing from actual registry");
        SpeciesManager species;
        const auto prepared=model->SetupChecked(config,species);
        require(species.count()==2,"Gaussian registered constituents changed");
        constexpr auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
        amr::AMRControl control(32,2);
        arch::driver::InitializeRootState(control,*model,config,species,{arch::dispatch::EosId::Ideal,rz});
        double max_error=0.,min_x=1.,max_x=0.;
        long double error2=0.,measure=0.;
        std::size_t cells=0;
        for(int id:control.tree->GetActiveBlocks()){
            const auto& b=control.pool->GetBlock(id);
            for(int j=b.grid.Js();j<b.grid.Je();++j)for(int i=b.grid.Is();i<b.grid.Ie();++i) {
                const int index=b.grid.GetIndex(i,j,b.grid.Ks());
                const long double lo=b.grid.GetFacePosL(i),hi=b.grid.GetFacePosR(i);
                const long double zl=b.grid.x2_min+(j-b.grid.ng)*b.grid.dx2;
                const long double zh=b.grid.x2_min+(j-b.grid.ng+1)*b.grid.dx2;
                const long double reference=gaussian_average(lo,hi,zl,zh);
                const double actual=b.fluid_state.X(1,index);
                const double error=std::abs(actual-double(reference));
                max_error=std::max(max_error,error);
                min_x=std::min(min_x,actual);max_x=std::max(max_x,actual);++cells;
                const long double v=arch::constants::math::pi*(hi*hi-lo*lo)*(zh-zl);
                error2+=v*error*error;measure+=v;
                require(std::isfinite(actual)&&actual>=0.&&actual<=1.&&
                    std::abs(b.fluid_state.X(0,index)+actual-1.)<2.e-12,
                    "registered Gaussian volume composition invalid");
                require(std::abs(b.fluid_state.rho[index]-2.)<2.e-12&&b.fluid_state.mom_u[index]==0.&&
                    b.fluid_state.mom_v[index]==0.&&b.fluid_state.mom_w[index]==0.&&
                    std::abs(b.fluid_state.eng[index]-5./(1.4-1.))<2.e-12,
                    "registered Gaussian shared EOS state changed");
            }
            require(b.fluid_state.stage_repairs.values[0]==0.,"registered RZ initialization repaired");
        }
        require(max_x>min_x,"registered Gaussian axial profile lost");
        require(config.LoadedInput()->case_id=="Gaussian"&&
                config.LoadedInput()->case_source_sha256.size()==64,
                "registered Gaussian source identity missing");
        const double rms=double(std::sqrt(error2/measure));
        const double order=previous?std::log2(previous/rms):0.;
        if(previous)require(order>=1.8,"registered Gaussian native average convergence below owner gate");
        std::cout<<std::setprecision(17)<<"PASS registered Gaussian SetupChecked -> Root -> PopulateState/EOS"
            <<" roots_per_axis="<<roots<<" cells="<<cells<<" rms_fraction_error="<<rms
            <<" max_fraction_error="<<max_error<<" order="<<order
            <<" source="<<config.LoadedInput()->case_source_sha256<<"\n";
        previous=rms;
    }
    return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
}
