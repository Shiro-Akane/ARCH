#include "core/problem/InitialStateConversion.h"
#include "physics/eos/IdealGas.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

static void require(bool good, const char *message) {
    if (!good) throw std::runtime_error(message);
}
struct DensityBoundedIdealGas : IdealGas {
    using IdealGas::IdealGas;
    double get_temperature(double rho, double e, const double* fractions) const {
        return rho <= 3.0 ? IdealGas::get_temperature(rho, e, fractions)
                          : std::numeric_limits<double>::quiet_NaN();
    }
};
int main() {
    SpeciesManager empty;
    IdealGas air(1.4, empty);
    require(std::abs(air.get_eint_from_T(1, 300, nullptr) - 2.154e9) < 1e-5, "CGS air Cv times temperature");
    require(std::abs(air.get_temperature(1, 2.154e9, nullptr) - 300) < 1e-10, "CGS energy to kelvin");
    require(std::abs(air.get_pressure_from_rho_T(1, 300, nullptr) - 8.616e8) < 1e-4, "CGS ideal pressure from density and temperature");
    SpeciesManager species;
    species.add_species("test", 1, 1, 1.4, 2.0);
    IdealGas eos(1.4, species);
    PrimitiveData data{};
    data.rho = 2;
    data.u = 3;
    data.v = 4;
    data.p = 5;
    data.mass_fractions = {1};
    const auto pressure_state = ProblemHelper::detail::InitialConservedState(data, eos);
    require(std::abs(pressure_state.eng - 37.5) < 1e-12, "pressure initial energy");
    require(pressure_state.mom_u == 6 && pressure_state.mom_v == 8, "initial momentum");
    data.SetTemperature(10);
    const auto thermal_state = ProblemHelper::detail::InitialConservedState(data, eos);
    require(std::abs(thermal_state.eng - 65) < 1e-12, "temperature must own thermal energy");
    NumericsConfig limits;
    limits.sml_rho = 4.0;
    arch::state::Repair repair;
    const auto repaired = ProblemHelper::detail::InitialConservedState(data, eos, limits, &repair);
    require(repaired.rho == 4.0 && repaired.eng == 130.0 &&
            repair.status == arch::state::Status::repaired, "valid initial repair");
    DensityBoundedIdealGas bounded(1.4, species);
    bool invalid_repair = false;
    try { (void)ProblemHelper::detail::InitialConservedState(data, bounded, limits); }
    catch (const std::runtime_error&) { invalid_repair = true; }
    require(invalid_repair, "a repair must not publish a state outside the EOS domain");
    data.temperature = std::numeric_limits<double>::quiet_NaN();
    bool rejected = false;
    try { (void)ProblemHelper::detail::InitialConservedState(data, eos); }
    catch (const std::runtime_error &) { rejected = true; }
    require(rejected, "invalid temperature conversion must fail");
    // Independent monomial integrals distinguish V average, W average and
    // midpoint rho*u_phi. No new rotating model or evolution threshold.
    SpeciesManager material;
    material.add_species("one",1,1,1.4,2.);
    material.add_species("two",2,1,1.4,2.);
    IdealGas gas(1.4,material);
    NumericsConfig regular;
    double maximum_error=0.;
    for (const auto bounds:std::array<std::array<double,2>,3>{{{0.,.125},{.5,1.},{4.,4.25}}})
    for (bool temperature_input:{false,true}) {
        const double a=bounds[0],b=bounds[1],omega=2.;
        const auto result=ProblemHelper::detail::InitialRzCellState(a,b,-.25,.75,2,
            gas,regular,[&](const PointCoords& p,PrimitiveData& d) {
                d.rho=2.;d.u=.5*p.r_cy;d.v=.25*p.z_cy;d.w=omega*p.r_cy;
                d.p=5.+p.z_cy;
                d.mass_fractions={.4+.1*p.z_cy,.6-.1*p.z_cy};
                if(temperature_input)d.SetTemperature(3.);
            });
        const long double lo=a,hi=b,zlo=-.25L,zhi=.75L,rho=2.,om=omega;
        const long double rv=2.L*(hi*hi*hi-lo*lo*lo)/(3.L*(hi*hi-lo*lo));
        const long double rw=3.L*(std::pow(hi,4)-std::pow(lo,4))/
            (4.L*(hi*hi*hi-lo*lo*lo));
        const long double r2v=(hi*hi+lo*lo)/2;
        const long double z=(zlo+zhi)/2,z2=(zlo*zlo+zlo*zhi+zhi*zhi)/3;
        const long double thermal=temperature_input ? 12.L : (5.L+z)/(1.4L-1.L);
        const long double expected_e=thermal+.5L*rho*((.25L+om*om)*r2v+.0625L*z2);
        const std::array<double,5> expected{double(rho),double(rho*.5L*rv),
            double(rho*.25L*z),double(rho*om*rw),double(expected_e)};
        const auto& s=result.conserved;
        const std::array<double,5> actual{s.rho,s.mom_u,s.mom_v,s.mom_w,s.eng};
        for(size_t c=0;c<5;++c) {
            const double error=std::abs(actual[c]-expected[c]);
            maximum_error=std::max(maximum_error,error);
            require(error<2.e-12,"independent RZ cell integral mismatch");
        }
        require(std::abs(result.mass_fractions[0]-.425)<2.e-12
                &&std::abs(result.mass_fractions[1]-.575)<2.e-12,
                "RZ species must follow volume averaged rhoX");
        if(a==.5)require(std::abs(s.mom_w-2*omega*.75)>.01,
                        "RZ angular state fell back to midpoint rho*u_phi");
    }
    const auto spin=[&](double omega,double pressure,const NumericsConfig& limits) {
        return ProblemHelper::detail::InitialRzCellState(.5,1.,0.,1.,2,gas,limits,
            [&](const PointCoords& p,PrimitiveData& d) {
                d.rho=1.;d.w=omega*p.r_cy;d.p=pressure;d.mass_fractions={1.,0.};
            });
    };
    const auto accepted_spin=spin(1.,1.,regular);
    require(std::abs(accepted_spin.conserved.mom_w-45./56.)<2.e-12,
            "owner rigid spin W average mismatch");
    require(std::abs(accepted_spin.conserved.eng-(2.5+5./16.))<2.e-12,
            "RZ total energy must retain true volume integral");
    bool closure_rejected=false,sample_repaired_rejected=false;
    try{(void)spin(32.,.001,regular);}catch(const std::runtime_error&){closure_rejected=true;}
    NumericsConfig needs_floor=regular;needs_floor.sml_rho=2.;
    try{(void)spin(1.,1.,needs_floor);}catch(const std::runtime_error&){sample_repaired_rejected=true;}
    require(closure_rejected&&sample_repaired_rejected,
            "RZ candidate must reject unresolved closure/repairs, not add heat");
    int called=0;bool bad_cell=false;
    try{(void)ProblemHelper::detail::InitialRzCellState(-1.,1.,0.,1.,2,gas,regular,
        [&](const PointCoords&,PrimitiveData&){++called;});}
    catch(const std::invalid_argument&){bad_cell=true;}
    require(bad_cell&&called==0,"invalid native cell reached callback");
    for (int invalid_measure=0;invalid_measure<2;++invalid_measure) {
        bool rejected=false;
        try {
            const double lo=invalid_measure ? .5 : 0.;
            const double hi=invalid_measure ? 1. : 1.e-200;
            const double zl=invalid_measure ? -std::numeric_limits<double>::max() : 0.;
            const double zh=invalid_measure ? std::numeric_limits<double>::max() : 1.;
            (void)ProblemHelper::detail::InitialRzCellState(lo,hi,zl,zh,2,gas,regular,
                [&](const PointCoords&,PrimitiveData&){++called;});
        } catch(const std::invalid_argument&){rejected=true;}
        require(rejected&&called==0,"collapsed/overflow native W/V reached callback");
    }
    // Independent closure consistency: finite-volume representation error
    // converges on the physical axis as well as away from it. This is not a
    // hydro evolution or force convergence test.
    double previous_error=0.;
    for (int cells:{16,32,64}) {
        long double error2=0.,volume=0.;
        for(int i=0;i<cells;++i) {
            const double lo=double(i)/cells,hi=double(i+1)/cells;
            const auto cell=ProblemHelper::detail::InitialRzCellState(lo,hi,0.,1.,2,
                gas,regular,[](const PointCoords& p,PrimitiveData& d) {
                    d.rho=1.;d.w=p.r_cy;d.p=1.;d.mass_fractions={1.,0.};
                });
            const auto thermal=arch::state::recover(cell.conserved);
            const long double v=(static_cast<long double>(hi)-lo)*(hi+lo);
            error2+=v*std::pow(static_cast<long double>(thermal.internal)-2.5L,2);
            volume+=v;
        }
        const double error=double(std::sqrt(error2/volume));
        if(previous_error)require(std::log2(previous_error/error)>=1.8,
                                  "RZ representative closure consistency below owner 1.8 gate");
        std::cout<<"RZ_CELL_CONSISTENCY cells="<<cells<<" error="<<error
                 <<" order="<<(previous_error?std::log2(previous_error/error):0.)<<"\n";
        previous_error=error;
    }
    std::cout<<"RZ_CELL_INTEGRAL_PASS cases=6 max_error="<<maximum_error
             <<" raw_omega_reference=45/56 closure_and_repair_rejected=true\n";
    std::cout << "Initial state conversion passed\n";
}
