// Real registered Gaussian Setup/Init, shared EOS; no simulation.
#include "core/problem/ProblemRegistry.h"
#include "core/config/RuntimeParams.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <limits>
#include <cmath>
static void require(bool b,const char* m){if(!b)throw std::runtime_error(m);}
int main(int argc,char**argv) {
 try {
    require(argc==2,"strict engineering input required");
    std::ifstream file(argv[1]);std::ostringstream text;text<<file.rdbuf();
    auto config=RuntimeParams::LoadText(text.str(),"Gaussian",
        arch::config::ConfigurationPurpose::InitialState);
    auto model=ProblemRegistry::Get().Create("Gaussian");
    require(bool(model),"Gaussian missing from actual registry");
    SpeciesManager species;
    const auto prepared=model->SetupChecked(config,species);
    require(species.count()==2,"Gaussian registered constituents changed");
    constexpr auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    amr::AMRControl control(4,2);control.tree->InitRootGrid(config,species.count(),rz);
    model->InitializeData(control,config,species,{arch::dispatch::EosId::Ideal,rz});
    double max_error=0.,min_x=1.,max_x=0.;
    std::size_t cells=0;
    for(int id:control.tree->GetActiveBlocks()){
        const auto& b=control.pool->GetBlock(id);
        for(int j=b.grid.Js();j<b.grid.Je();++j)for(int i=b.grid.Is();i<b.grid.Ie();++i) {
            const int c=b.grid.GetIndex(i,j,b.grid.Ks());
            const double radial=b.grid.GetCellCenterX(i),z=b.grid.GetCellCenterY(j);
            const double dr=(radial-.5)/.2,dz=(z-.25)/.2;
            const double expected=.5*std::exp(-(dr*dr+dz*dz));
            const double actual=b.fluid_state.X(1,c);
            max_error=std::max(max_error,std::abs(actual-expected));
            min_x=std::min(min_x,actual);max_x=std::max(max_x,actual);++cells;
            require(actual==expected&&b.fluid_state.X(0,c)==1.-expected,
                    "registered Gaussian RZ axial/native fraction mapping mismatch");
            require(b.fluid_state.rho[c]==2.&&b.fluid_state.mom_u[c]==0.&&
                    b.fluid_state.mom_v[c]==0.&&b.fluid_state.mom_w[c]==0.&&
                    std::abs(b.fluid_state.eng[c]-5./(1.4-1.))<2.e-12,
                    "registered Gaussian shared EOS state changed");
        }
    }
    require(max_x>min_x,"registered Gaussian axial profile lost");
    require(config.LoadedInput()->case_id=="Gaussian" &&
            config.LoadedInput()->case_source_sha256.size()==64,
            "registered Gaussian source identity missing");
    std::cout<<"PASS registered Gaussian SetupChecked -> InitializeData -> PopulateState/EOS"
             <<" cells="<<cells<<" max_fraction_error="<<max_error<<" range="<<min_x<<","<<max_x
             <<" source="<<config.LoadedInput()->case_source_sha256<<"\n";
    return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
}
