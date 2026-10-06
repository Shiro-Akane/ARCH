#include "core/problem/InitialStateConversion.h"
#include "physics/eos/IdealGas.h"
#include <array>
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

// Exact binary states independently fix the requested caloric state and K.
// This is an initialization arithmetic check, not a physical high-velocity
// model or a qualification of subnormal-density evolution on a full grid.
static void test_extreme_ideal_initialization() {
    SpeciesManager material;
    material.add_species("binary", 1., 1., 2., 1.);
    IdealGas eos(2., material);
    NumericsConfig limits;
    limits.sml_rho = std::numeric_limits<double>::denorm_min();
    limits.max_eint = std::numeric_limits<double>::max();
    const arch::state::Bounds bounds{limits.sml_rho, limits.min_eint, limits.max_eint};
    struct Reference {
        double rho, velocity, internal, momentum, pressure;
        long double kinetic, energy;
    };
    const std::array<Reference, 2> references{{
        {std::ldexp(1., -1074), std::ldexp(1., 20), std::ldexp(1., 42),
         std::ldexp(1., -1054), std::ldexp(1., -1032),
         std::ldexp(3.L, -1035), std::ldexp(11.L, -1035)},
        {std::ldexp(1., -100), std::ldexp(1., 512), std::ldexp(1., 1020),
         std::ldexp(1., 412), std::ldexp(1., 920),
         std::ldexp(3.L, 923), std::ldexp(25.L, 920)}
    }};
    for (const auto& reference : references) {
        PrimitiveData data{};
        data.rho = reference.rho;
        data.u = data.v = data.w = reference.velocity;
        data.p = reference.pressure;
        data.mass_fractions = {1.};
        for (bool temperature_input : {false, true}) {
            if (temperature_input) data.SetTemperature(reference.internal);
            arch::state::Repair receipt;
            const auto state = ProblemHelper::detail::InitialConservedState(data, eos, limits, &receipt);
            require(receipt.status == arch::state::Status::valid,
                    "representable extreme Init must not need an energy or density repair");
            require(state.rho == reference.rho
                    && state.mom_u == reference.momentum
                    && state.mom_v == reference.momentum
                    && state.mom_w == reference.momentum,
                    "extreme Init changed independently specified conserved momentum");
            require(state.eng == static_cast<double>(reference.energy),
                    "extreme Init lost the independently specified total energy");
            const auto thermal = arch::state::recover(state);
            require(thermal.status == arch::state::Status::valid
                    && thermal.kinetic == static_cast<double>(reference.kinetic)
                    && thermal.internal == reference.internal,
                    "extreme Init changed the requested resolvable caloric state");
            require(arch::state::validate_eos(state, data.mass_fractions.data(), 1, bounds, eos)
                        == arch::state::Status::valid
                    && eos.get_temperature(state.rho, thermal.internal, data.mass_fractions.data())
                        == reference.internal
                    && eos.get_pressure(state, data.mass_fractions.data()) == reference.pressure,
                    "actual IdealGas rejected or changed a representable extreme Init state");
            const double sound = eos.get_sound_speed(state, reference.pressure,
                                                     data.mass_fractions.data());
            require(std::isfinite(sound) && sound > 0.
                    && sound == std::sqrt(std::ldexp(reference.internal, 1)),
                    "actual IdealGas sound speed differs from the independent gamma=2 state");
        }
    }

    // K alone is finite, but rho*u cannot be stored in FluidVector. This
    // primitive must fail through the existing finite-conservative gate.
    PrimitiveData invalid{};
    invalid.rho = std::ldexp(1.5, 1023);
    invalid.u = 1.5;
    invalid.p = std::ldexp(invalid.rho, -6);
    invalid.mass_fractions = {1.};
    const long double finite_kinetic = .5L * static_cast<long double>(invalid.rho) * 2.25L;
    require(std::isfinite(static_cast<double>(finite_kinetic))
            && !std::isfinite(invalid.rho * invalid.u)
            && !std::isfinite(eos_utils::calc_kinetic_energy(invalid.rho, invalid.u, 0., 0.)),
            "unrepresentable conservative momentum was silently replaced by a finite K");
    for (bool temperature_input : {false, true}) {
        if (temperature_input) invalid.SetTemperature(std::ldexp(1., -6));
        bool rejected = false;
        try { (void)ProblemHelper::detail::InitialConservedState(invalid, eos, limits); }
        catch (const std::runtime_error&) { rejected = true; }
        require(rejected, "Init accepted an unrepresentable conserved momentum");
    }
    std::cout << "INITIAL_RANGE_PASS states=4 actual_ideal_eos=true no_repair=true\n";
}

int main() {
    test_extreme_ideal_initialization();
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
    // A cold rigid spin has legal physical Gauss samples and provisional
    // native means. J/W is not a point momentum; raw recovery is only a
    // diagnostic. Its real closure requires a completed density stencil.
    const auto cold_spin=spin(32.,.001,regular);
    require(RzThermodynamics::provisional_native_state(cold_spin.conserved,
                cold_spin.mass_fractions.data(),2,1,
                {regular.sml_rho,regular.min_eint,regular.max_eint})==arch::state::Status::valid
            &&arch::state::recover(cold_spin.conserved).status==arch::state::Status::unresolved_energy,
            "cold native Init was confused with a raw point-momentum state");
    require(std::abs(cold_spin.conserved.mom_w-32.*45./56.)<2.e-12
            &&std::abs(cold_spin.conserved.eng-(.0025+320.))<2.e-12,
            "cold native Init changed independent V/W conserved integrals");
    bool sample_repaired_rejected=false;
    NumericsConfig needs_floor=regular;needs_floor.sml_rho=2.;
    try{(void)spin(1.,1.,needs_floor);}catch(const std::runtime_error&){sample_repaired_rejected=true;}
    require(sample_repaired_rejected,
            "RZ candidate must reject repaired physical samples, not add heat");
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
    // Raw representative-state approximation diagnostic: applying a Cartesian
    // momentum interpretation to native V/W means has a convergent projection
    // residual. This is not the native thermodynamic closure's error, a reason
    // to veto a cold native mean, or a Hydro/force convergence qualification.
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
                                  "RZ raw representative approximation below owner 1.8 gate");
        std::cout<<"RZ_RAW_MEAN_APPROXIMATION cells="<<cells<<" error="<<error
                 <<" order="<<(previous_error?std::log2(previous_error/error):0.)<<"\n";
        previous_error=error;
    }
    std::cout<<"RZ_CELL_INTEGRAL_PASS cases=6 max_error="<<maximum_error
             <<" raw_omega_reference=45/56 cold_native_provisional=true sample_repair_rejected=true\n";
    std::cout << "Initial state conversion passed\n";
}
