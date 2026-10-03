// Actual authoritative PopulateState/EOS path; no timestep advancement.
#include "interface/ProblemGenerator.h"
#include "core/problem/ProblemHelper.h"
#include "physics/constant/PhysicalConstants.h"
#include <algorithm>
#include <iostream>
#include <atomic>
#include <stdexcept>
#include <cmath>
static void require(bool b,const char* m){if(!b)throw std::runtime_error(m);}
int main() {
 try {
    using GridMetrics::GeometrySemantics;
    for(bool rz:{false,true}) for(bool repair:{false,true}) {
        SimConfig config;config.grid.dim=2;config.grid.geometry="cylindrical";
        config.grid.nblockx1=1;config.grid.nblockx2=1;config.grid.nblockx3=0;
        config.grid.x1_min=0.;config.grid.x1_max=2.;
        config.grid.x2_min=rz?-4.:-.5;config.grid.x2_max=rz?4.:.5;
        config.physics.eos_type="ideal";config.physics.gamma=1.4;
        if(repair)config.numerics.sml_rho=4.;
        SpeciesManager species;species.add_species("gas",1.,1.,1.4,2.);
        const auto sem=rz?GeometrySemantics::AxisymmetricRz:GeometrySemantics::Existing;
        amr::AMRControl control(4,2);control.tree->InitRootGrid(config,1,sem);
        auto callback=[](const PointCoords& p,PrimitiveData& d) {
            d.rho=2.+.25*p.z_cy;d.u=0.;d.v=3.;d.w=0.;
            d.p=5.+.5*p.z_cy;d.mass_fractions[0]=1.;
        };
        ProblemHelper::detail::PopulateState(control,config,species,
            {arch::dispatch::EosId::Ideal,sem},callback);
        const auto& b=control.pool->GetBlock(control.tree->GetActiveBlocks().front());
        long double volume=0.,mass_delta=0.,energy_delta=0.;
        double max_error=0.;
        for(int j=b.grid.Js();j<b.grid.Je();++j)
            for(int i=b.grid.Is();i<b.grid.Ie();++i){
                const int c=b.grid.GetIndex(i,j,b.grid.Ks());
                const double z=rz?b.grid.GetCellCenterY(j):0.;
                const double rho=2.+.25*z,pressure=5.+.5*z;
                const double old_energy=pressure/(1.4-1.)+.5*rho*9.;
                const double expected_rho=repair?4.:rho;
                const double expected_energy=repair?old_energy*4./rho:old_energy;
                max_error=std::max(max_error,std::abs(b.fluid_state.eng[c]-expected_energy));
                require(b.fluid_state.rho[c]==expected_rho&&b.fluid_state.mom_u[c]==0.&&
                        b.fluid_state.mom_v[c]==expected_rho*3.&&b.fluid_state.mom_w[c]==0.&&
                        b.fluid_state.X(0,c)==1.,"authoritative z-dependent population mismatch");
                require(std::abs(b.fluid_state.eng[c]-expected_energy)<2.e-12,
                        "independent ideal initialization energy mismatch");
                const long double lo=b.grid.GetFacePosL(i),hi=b.grid.GetFacePosR(i);
                const long double cell=(rz?2.L*arch::constants::math::pi:1.L)
                    *.5L*(hi-lo)*(hi+lo)*b.grid.dx2;
                volume+=cell;mass_delta+=(expected_rho-rho)*cell;
                energy_delta+=(expected_energy-old_energy)*cell;
            }
        const auto& ledger=b.fluid_state.stage_repairs.values;
        if(repair) {
            require(ledger[0]==amr::BLOCK_NX*amr::BLOCK_NY,"repair event count changed");
            require(std::abs(ledger[1]-double(volume))<2.e-12*double(volume),
                    "initial repair used wrong chart volume");
            require(std::abs(ledger[2]-double(mass_delta))<2.e-12*double(mass_delta),
                    "initial repair mass budget used wrong chart");
            require(std::abs(ledger[7]-double(energy_delta))<2.e-12*double(energy_delta),
                    "initial repair energy budget used wrong chart");
        } else require(ledger[0]==0.,"unexpected initialization repair");
        std::cout<<"PASS POPULATE "<<(rz?"RZ":"polar")<<" repair="<<repair
                 <<" max_energy_error="<<max_error<<" measure="<<double(volume)<<"\n";
        // Invalid profile preflight must precede callbacks and all block mutation.
        auto& mutable_state=control.pool->GetBlock(control.tree->GetActiveBlocks().front()).fluid_state;
        const auto rho_before=mutable_state.rho,eng_before=mutable_state.eng;
        const auto ledger_before=mutable_state.stage_repairs.values;
        const auto leaves_before=control.tree->GetActiveBlocks();
        const auto pool_before=control.pool->GetNumActiveBlocks();
        bool root_rejected=false;
        try { control.tree->InitRootGrid(config,1,static_cast<GeometrySemantics>(99)); }
        catch(const std::invalid_argument&) {root_rejected=true;}
        require(root_rejected&&control.tree->GetActiveBlocks()==leaves_before&&
                control.pool->GetNumActiveBlocks()==pool_before&&mutable_state.rho==rho_before,
                "invalid RZ root preflight changed live topology/storage");
        std::atomic<int> called{0};
        bool rejected=false;
        try {ProblemHelper::detail::PopulateState(control,config,species,
            {arch::dispatch::EosId::Ideal,static_cast<GeometrySemantics>(99)},
            [&](const PointCoords&,PrimitiveData&){++called;});}
        catch(const std::invalid_argument&) {rejected=true;}
        require(rejected&&called==0&&mutable_state.rho==rho_before&&mutable_state.eng==eng_before&&
                mutable_state.stage_repairs.values==ledger_before,
                "invalid initialization profile changed state or invoked callback");
    }
    return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
