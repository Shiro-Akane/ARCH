// Actual authoritative PopulateState/EOS path; no timestep advancement.
#include "interface/ProblemGenerator.h"
#include "core/problem/ProblemHelper.h"
#include "interface/GenericProblem.h"
#include "driver/initialization/InitialMesh.h"
#include "driver/runtime/DriverRuntime.h"
#include "driver/schedule/DriverControl.h"
#include <cstring>
#include <iomanip>
#include "physics/constant/PhysicalConstants.h"
#include <algorithm>
#include <iostream>
#include <atomic>
#include <stdexcept>
#include <cmath>
static void require(bool b,const char* m){if(!b)throw std::runtime_error(m);}
static bool same_bits(const std::vector<double>& a,const std::vector<double>& b){
    return a.size()==b.size()&&std::memcmp(a.data(),b.data(),a.size()*sizeof(double))==0;
}
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
        if(rz&&repair) {
            const auto before=control.pool->GetBlock(control.tree->GetActiveBlocks().front()).fluid_state;
            bool rejected=false;
            try {ProblemHelper::detail::PopulateState(control,config,species,
                {arch::dispatch::EosId::Ideal,sem},callback);}
            catch(const std::runtime_error&){rejected=true;}
            const auto& after=control.pool->GetBlock(control.tree->GetActiveBlocks().front()).fluid_state;
            require(rejected&&after.rho==before.rho&&after.eng==before.eng&&
                    after.mom_w==before.mom_w&&after.mass_fractions==before.mass_fractions&&
                    after.stage_repairs.values==before.stage_repairs.values,
                    "RZ repaired candidate changed accepted state/ledger");
            std::cout<<"PASS POPULATE RZ repair-required rejected before publication\n";
            continue;
        }
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
                const auto equal=[&](double a,double b) {return rz ? std::abs(a-b)<2.e-12 : a==b;};
                require(equal(b.fluid_state.rho[c],expected_rho)&&b.fluid_state.mom_u[c]==0.&&
                        equal(b.fluid_state.mom_v[c],expected_rho*3.)&&b.fluid_state.mom_w[c]==0.&&
                        equal(b.fluid_state.X(0,c),1.),"authoritative z-dependent population mismatch");
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
                control.pool->GetNumActiveBlocks()==pool_before&&same_bits(mutable_state.rho,rho_before),
                "invalid RZ root preflight changed live topology/storage");
        std::atomic<int> called{0};
        bool rejected=false;
        try {ProblemHelper::detail::PopulateState(control,config,species,
            {arch::dispatch::EosId::Ideal,static_cast<GeometrySemantics>(99)},
            [&](const PointCoords&,PrimitiveData&){++called;});}
        catch(const std::invalid_argument&) {rejected=true;}
        require(rejected&&called==0&&same_bits(mutable_state.rho,rho_before)&&same_bits(mutable_state.eng,eng_before)&&
                mutable_state.stage_repairs.values==ledger_before,
                "invalid initialization profile changed state or invoked callback");
    }
    {
        constexpr auto rz=GeometrySemantics::AxisymmetricRz;
        SimConfig config;config.grid.dim=2;config.grid.geometry="cylindrical";
        config.grid.nblockx1=2;config.grid.nblockx2=1;config.grid.nblockx3=0;
        config.grid.x1_min=0.;config.grid.x1_max=1.;
        config.grid.x2_min=-1.;config.grid.x2_max=1.;
        config.physics.eos_type="ideal";config.physics.gamma=1.4;
        SpeciesManager species;species.add_species("gas",1.,1.,1.4,2.);
        GenericProblemGenerator problem(nullptr,[](const PointCoords& p,PrimitiveData& d){
            d.rho=1.;d.w=p.r_cy;d.p=4.;d.mass_fractions={1.};
        });
        amr::AMRControl control(8,2);
        arch::driver::InitializeRootState(control,problem,config,species,{arch::dispatch::EosId::Ideal,rz});
        long double total_j=0.,reference_j=0.;double maximum_error=0.;
        for(int id:control.tree->GetActiveBlocks()) {
            const auto& b=control.pool->GetBlock(id);
            for(int j=b.grid.Js();j<b.grid.Je();++j)
            for(int i=b.grid.Is();i<b.grid.Ie();++i) {
                const int c=b.grid.GetIndex(i,j,b.grid.Ks());
                const long double lo=b.grid.GetFacePosL(i),hi=b.grid.GetFacePosR(i);
                const long double rw=.75L*(std::pow(hi,4)-std::pow(lo,4))/
                    (std::pow(hi,3)-std::pow(lo,3));
                const long double e=4.L/(1.4L-1.L)+.25L*(hi*hi+lo*lo);
                const double error=std::max(std::abs(b.fluid_state.mom_w[c]-double(rw)),
                    std::abs(b.fluid_state.eng[c]-double(e)));
                maximum_error=std::max(maximum_error,error);
                require(error<2.e-12,"actual RZ InitializeRootState used midpoint angular state");
                // Independent source cell J and native quadrature mean: pi*dz*(b^4-a^4)/2.
                const long double w=2.L*arch::constants::math::pi*b.grid.dx2*
                    (std::pow(hi,3)-std::pow(lo,3))/3;
                total_j+=b.fluid_state.mom_w[c]*w;
                reference_j+=arch::constants::math::pi*b.grid.dx2*
                    (std::pow(hi,4)-std::pow(lo,4))/2;
            }
            require(b.fluid_state.stage_repairs.values[0]==0.,"RZ Init added repairs");
        }
        const double jerror=double(std::abs(total_j-reference_j)/reference_j);
        require(jerror<=1.e-12,"actual Init violated owner angular integral budget");
        BCHandler boundaries(config,rz);
        SimulationController ctrl(config,RunState{});
        arch::driver::DriverRuntime runtime(control,boundaries,config,species,ctrl);
        runtime.initialize_topology(); // real committed identities and ghost exchange, no timestep
        const auto& ids=control.tree->GetActiveBlocks();
        require(ids.size()==2,"RZ root block topology changed");
        const amr::Block* left=nullptr;const amr::Block* right=nullptr;
        for(int id:ids) {
            const auto& b=control.pool->GetBlock(id);
            (b.logical_x1==0 ? left : right)=&b;
        }
        require(left&&right,"RZ root logical keys missing");
        size_t ghosts=0;
        const auto check=[&](const amr::Block& b,int i,int j,const amr::Block& donor,int di,int dj,bool reflect){
            const auto a=b.fluid_state.get(b.grid.GetIndex(i,j,b.grid.Ks()));
            const auto s=donor.fluid_state.get(donor.grid.GetIndex(di,dj,donor.grid.Ks()));
            require(a.rho==s.rho&&a.eng==s.eng&&a.mom_v==s.mom_v&&
                    a.mom_u==(reflect?-s.mom_u:s.mom_u)&&a.mom_w==(reflect?-s.mom_w:s.mom_w),
                    "RZ actual ghost donor/sign mismatch");
            require(b.fluid_state.X(0,b.grid.GetIndex(i,j,b.grid.Ks()))==
                    donor.fluid_state.X(0,donor.grid.GetIndex(di,dj,donor.grid.Ks())),
                    "RZ Init species ghost changed");
            ++ghosts;
        };
        for(int j=left->grid.Js();j<left->grid.Je();++j)
        for(int depth=1;depth<=left->grid.ng;++depth) {
            check(*left,left->grid.Is()-depth,j,*left,left->grid.Is()+depth-1,j,true);
            check(*left,left->grid.Ie()+depth-1,j,*right,right->grid.Is()+depth-1,j,false);
            check(*right,right->grid.Is()-depth,j,*left,left->grid.Ie()-depth,j,false);
            check(*right,right->grid.Ie()+depth-1,j,*right,right->grid.Ie()-1,j,false);
        }
        for(int id:ids) {
            const auto& b=control.pool->GetBlock(id);
            for(int j=0;j<b.grid.GetTotalY();++j)for(int i=0;i<b.grid.GetTotalX();++i) {
                const auto s=b.fluid_state.get(b.grid.GetIndex(i,j,b.grid.Ks()));
                require(std::isfinite(s.rho)&&std::isfinite(s.eng)&&std::isfinite(s.mom_w),
                        "RZ published actual ghost left sentinel");
            }
        }
        std::vector<FluidState> before;
        for(int id:ids)before.push_back(control.pool->GetBlock(id).fluid_state);
        bool rejected=false;
        try {ProblemHelper::detail::PopulateState(control,config,species,
            {arch::dispatch::EosId::Ideal,rz},[](const PointCoords& p,PrimitiveData& d) {
                d.rho=1.;d.w=(p.r_cy<.5 ? 1. : 32.)*p.r_cy;
                d.p=p.r_cy<.5 ? 4. : .001;d.mass_fractions={1.};
            });}
        catch(const std::runtime_error&){rejected=true;}
        require(rejected,"unresolved average reached actual publication");
        const auto bits=[](const auto& a,const auto& b) {
            return a.size()==b.size()&&std::memcmp(a.data(),b.data(),a.size()*sizeof(double))==0;
        };
        for(size_t index=0;index<ids.size();++index) {
            const auto& a=control.pool->GetBlock(ids[index]).fluid_state;const auto& b=before[index];
            require(bits(a.rho,b.rho)&&bits(a.mom_u,b.mom_u)&&bits(a.mom_v,b.mom_v)&&
                bits(a.mom_w,b.mom_w)&&bits(a.eng,b.eng)&&bits(a.mass_fractions,b.mass_fractions)&&
                bits(a.enuc_rate,b.enuc_rate)&&bits(a.stage_repairs.values,b.stage_repairs.values),
                "failed RZ multi-block initialization partially published");
        }
        SpeciesManager wrong_species;
        wrong_species.add_species("gas",1.,1.,1.4,2.);
        wrong_species.add_species("extra",2.,1.,1.4,2.);
        int callbacks=0;bool layout_rejected=false;
        try {ProblemHelper::detail::PopulateState(control,config,wrong_species,
            {arch::dispatch::EosId::Ideal,rz},[&](const PointCoords&,PrimitiveData&){++callbacks;});}
        catch(const std::invalid_argument&){layout_rejected=true;}
        require(layout_rejected&&callbacks==0,"RZ species mismatch reached callback");
        auto& truncated=control.pool->GetBlock(ids.front()).fluid_state.eng;
        const double last=truncated.back();truncated.pop_back();
        const auto truncated_before=truncated;
        layout_rejected=false;
        try {ProblemHelper::detail::PopulateState(control,config,species,
            {arch::dispatch::EosId::Ideal,rz},[&](const PointCoords&,PrimitiveData&){++callbacks;});}
        catch(const std::invalid_argument&){layout_rejected=true;}
        require(layout_rejected&&callbacks==0&&same_bits(truncated,truncated_before),
                "RZ invalid array layout mutated data or reached callback");
        truncated.push_back(last);
        std::cout<<std::setprecision(17)<<"PASS RZ REAL POPULATE/ROOT/TOPOLOGY cells=512 ghosts="
            <<ghosts<<" max_error="<<maximum_error<<" angular_relative_error="<<jerror
            <<" failure_atomic=true layout_preflight=true no_timestep=true\n";
    }
    return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
