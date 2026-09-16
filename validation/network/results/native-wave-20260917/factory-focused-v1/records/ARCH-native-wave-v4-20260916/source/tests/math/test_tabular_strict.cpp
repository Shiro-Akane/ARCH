/** @file test_tabular_strict.cpp
 *  @brief Independent exact-potential and all-root strict tabular witnesses.
 */
#include "physics/eos/Tabular3DEOS.h"
#include "physics/eos/Tabular4DEOS.h"
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void close(double value, double expected, const char* message)
{ require(std::abs(value - expected) <= 2e-11 * std::max(std::abs(expected), 1.0), message); }
template<class Function> void rejected(Function function, const char* message)
{ bool failed = false; try { function(); } catch (const std::runtime_error&) { failed = true; } require(failed, message); }

struct Species {
    int count = 1;
    double get_A(int) const { return 14.0; }
    double get_Z(int) const { return 7.0; }
    double calc_Abar(const double*) const { return 14.0; }
    double calc_Zbar(const double*) const { return 7.0; }
    double calc_Ye(const double*) const { return 0.5; }
};

// A polynomial in ln(rho),ln(T), exactly representable by the production
// Hermite patch. Composition nodes deliberately are not uniformly spaced.
struct Fixture {
    BasicTabular3DEOSView<Species> three{};
    BasicTabular4DEOSView<Species> four{};
    std::array<std::vector<double>, tabular_eos::FieldCount> fields3, fields4;
    std::vector<double> valid3, valid4;
    double composition[3]{0.1, 0.3, 0.9};
    static tabular_eos::FreeEnergyState exact(double rho, double T, double c) {
        const double x = std::log(rho), y = std::log(T);
        return {20.0 + 4*x + 0.8*x*y + 6*y-y*y+c*(5+y),
                4+0.8*y, 0.8*x+6-2*y+c, 0.0, 0.8, -2.0, 0.0};
    }
    Fixture() {
        three.n_rho=2; three.n_T=3; three.n_X=3;
        three.log_rho_min=0; three.log_rho_max=.2; three.dlog_rho=.2;
        three.log_T_min=0; three.log_T_max=.4; three.dlog_T=.2;
        three.X_min=.1; three.X_max=.9; three.dX=.4;
        three.axis_nodes[2]=composition; three.target_species_id=0;
        three.uses_free_energy=three.strict_domain=true;
        three.energy_reference_shift=3;
        four.n_rho=2; four.n_T=3; four.n_A=2; four.n_Z=2;
        four.log_rho_min=0; four.log_rho_max=.2; four.dlog_rho=.2;
        four.log_T_min=0; four.log_T_max=.4; four.dlog_T=.2;
        four.A_min=10; four.A_max=20; four.dA=10;
        four.Z_min=4; four.Z_max=9; four.dZ=5;
        four.uses_free_energy=four.strict_domain=true;
        four.energy_reference_shift=3;
        for(auto& field:fields3) field.assign(18,0);
        for(auto& field:fields4) field.assign(24,0);
        auto write=[&](auto& fields, std::size_t index, const auto& f) {
            fields[tabular_eos::F][index]=f.a; fields[tabular_eos::Fx][index]=f.ax;
            fields[tabular_eos::Fy][index]=f.ay; fields[tabular_eos::Fxx][index]=f.axx;
            fields[tabular_eos::Fxy][index]=f.axy; fields[tabular_eos::Fyy][index]=f.ayy;
        };
        for(int i=0;i<2;++i) for(int j=0;j<3;++j) {
            const double rho=std::pow(10.,.2*i), T=std::pow(10.,.2*j);
            for(int k=0;k<3;++k) write(fields3,three.free_energy_index(i,j,k),exact(rho,T,composition[k]));
            for(int k=0;k<2;++k) for(int l=0;l<2;++l)
                write(fields4,four.free_energy_index(i,j,k,l),exact(rho,T,.01*(10+10*k)+.02*(4+5*l)));
        }
        valid3.assign(18,1); valid4.assign(24,1);
        three.table_valid=valid3.data(); four.table_valid=valid4.data();
        for(int k=0;k<tabular_eos::FieldCount;++k) {
            three.free_energy_fields[k]=fields3[k].data(); four.free_energy_fields[k]=fields4[k].data();
        }
    }
};

template<class View> void view_checks(View& view, double composition) {
    const double X[1]{.5};
    for(double rho:{1.0,1.3,std::pow(10.,.2)})
        for(double T:{1.0,1.2,std::pow(10.,.2),2.0,std::pow(10.,.4)}) {
            const auto f=Fixture::exact(rho,T,composition);
            const auto thermal=tabular_eos::require_thermodynamics(tabular_eos::evaluate_thermodynamics(f,rho,T,3));
            close(view.get_pressure_from_rho_T(rho,T,X),thermal.pressure,"exact pressure");
            close(view.get_eint_from_T(rho,T,X),thermal.energy,"exact energy");
            close(view.get_cv(rho,T,X),thermal.cv,"exact cv");
            close(view.get_sound_speed_from_rho_T(rho,T,X),thermal.sound_speed,"exact sound speed");
            const double energy=view.get_eint_from_T(rho,T,X), pressure=view.get_pressure_from_rho_T(rho,T,X);
            close(view.get_temperature(rho,energy,X),T,"strict energy inverse");
            close(view.get_total_energy_primitive(rho,0,0,0,pressure,X),rho*energy,"strict primitive inverse");
        }
    const double lowE=view.get_eint_from_T(1,1,X);
    const double lowP=view.get_pressure_from_rho_T(1,1,X);
    const double highP=view.get_pressure_from_rho_T(1,std::pow(10.,.4),X);
    rejected([&]{view.get_temperature(1,std::nextafter(lowE,-INFINITY),X);},"outside energy endpoint accepted");
    rejected([&]{view.get_total_energy_primitive(1,0,0,0,std::nextafter(lowP,-INFINITY),X);},"outside pressure lower endpoint accepted");
    rejected([&]{view.get_total_energy_primitive(1,0,0,0,std::nextafter(highP,INFINITY),X);},"outside pressure upper endpoint accepted");
    rejected([&]{view.get_eint_from_T(1,std::nextafter(1.,0.),X);},"outside temperature endpoint accepted");
    rejected([&]{view.get_cv(std::nextafter(1.,0.),1,X);},"outside rho endpoint accepted");
    rejected([&]{view.get_pressure_from_rho_T(NAN,1,X);},"NaN rho accepted");
    rejected([&]{view.get_temperature(1,NAN,X);},"NaN energy accepted");
    rejected([&]{view.composition_derivatives(0,1,X);},"invalid derivative query accepted");
    rejected([&]{view.get_eta(0,1,X);},"invalid eta query accepted");
}

void seeded_fields_checks() {
    std::vector<double> f(25), fx(25,4), fy(25,6);
    for(int i=0;i<5;++i) for(int j=0;j<5;++j) f[5*i+j]=20+4*i+6*j;
    const auto ordinary=tabular_eos::build_derivative_fields(f,5,5,1,1,1);
    const auto seeded=tabular_eos::build_derivative_fields(f,5,5,1,1,1,&fx,&fy);
    for(int k=0;k<tabular_eos::FieldCount;++k)
        for(int i=0;i<25;++i) close(seeded[k][i],ordinary[k][i],"seeded derivative mismatch");
    fx.pop_back();
    rejected([&]{tabular_eos::build_derivative_fields(f,5,5,1,1,1,&fx,&fy);},"invalid seed extent accepted");
}

void actual_view_multiroot() {
    Fixture fixture;
    for(int i=0;i<2;++i) for(int j=0;j<3;++j) for(int k=0;k<3;++k) {
        const double x=std::log(10.)*.2*i, y=std::log(10.)*.2*j;
        const double hy=std::log(10.)*.2, t=double(j);
        const double q=10+(t-.2)*(t-.5)*(t-.8);
        const double qy=(3*t*t-3*t+.66)/hy, qyy=(6*t-3)/(hy*hy);
        const auto index=fixture.three.free_energy_index(i,j,k);
        fixture.fields3[tabular_eos::F][index]=100+6*y-y*y+x*q;
        fixture.fields3[tabular_eos::Fx][index]=q;
        fixture.fields3[tabular_eos::Fy][index]=6-2*y+x*qy;
        fixture.fields3[tabular_eos::Fxy][index]=qy;
        fixture.fields3[tabular_eos::Fyy][index]=-2+x*qyy;
        fixture.fields3[tabular_eos::Fxyy][index]=qyy;
    }
    const double X[]{.5};
    for(double root:{.2,.5,.8}) {
        const double T=std::pow(10.,.2*root);
        close(fixture.three.get_pressure_from_rho_T(1,T,X),10,"actual multiroot forward mismatch");
        require(fixture.three.get_cv(1,T,X)>0,"multiroot branch invalid cv");
        require(fixture.three.get_sound_speed_from_rho_T(1,T,X)>0,"multiroot branch invalid cs");
    }
    rejected([&]{fixture.three.get_total_energy_primitive(1,0,0,0,10,X);},"actual same-cell pressure roots missed");
}

void inverse_checks() {
    using namespace tabular_eos;
    // Three roots within ONE cell, including two on the positive-slope branch.
    ThermalPolynomial p{9.92,.66,-1.5,1.,0.,0.}; // 10+(t-.2)(t-.5)(t-.8)
    auto state=[](double) { return FreeEnergyResult{}; };
    auto solve=[&](const ThermalPolynomial& polynomial,double target) {
        return invert_free_energy_temperature(2,0,1,target,
            [&](int,ThermalPolynomial& out){out=polynomial;return true;},
            [&](double T){return polynomial_value(polynomial,5,std::log10(T));},state);
    };
    require(solve(p,10).status==FreeEnergyStatus::ambiguous_temperature_inversion,"same-cell roots missed");
    require(solve(ThermalPolynomial{10,0,0,0,0,0},10).status==FreeEnergyStatus::ambiguous_temperature_inversion,"constant branch accepted");
    double overflow_roots[5]{};
    require(stationary_points(ThermalPolynomial{1,0,0,0,0,1e307},overflow_roots)<0,
            "derivative hierarchy overflow did not fail closed");
    const auto gap=invert_free_energy_temperature(4,0,1,2.5,
        [](int j,ThermalPolynomial& out){out={double(j+1),1,0,0,0,0};return j!=1;},
        [](double T){return 1+std::log10(T);},state);
    require(gap.status==FreeEnergyStatus::invalid_temperature_inversion,"invalid gap bridged");
    const auto unique=solve(ThermalPolynomial{10,1,0,0,0,0},10.4);
    require(unique.status==FreeEnergyStatus::success,"unique root rejected");
    close(unique.temperature,std::pow(10.,.4),"unique root inaccurate");
    const double endpoint=std::pow(10.,.3);
    const auto endpoint_inverse=invert_free_energy_temperature(4,.1,(.3-.1)/3,endpoint,
        [](int,ThermalPolynomial& out){out={1,1,0,0,0,0};return true;},
        [](double T){return T;},
        [&](double T){return T>endpoint ? free_energy_failure(FreeEnergyStatus::invalid_native_domain)
            : FreeEnergyResult{};},.3);
    require(endpoint_inverse.status==FreeEnergyStatus::success && endpoint_inverse.temperature==endpoint,
            "rounded uniform-grid last endpoint rejected");
}
} // namespace

int main() {
    try {
        inverse_checks(); seeded_fields_checks(); actual_view_multiroot(); Fixture fixture;
        view_checks(fixture.three,.5); view_checks(fixture.four,.28);
        const double X[]{.5}, T=1.4;
        const auto derivatives=fixture.three.composition_derivatives(1.3,T,X);
        close(derivatives.energy[0],4+std::log(T),"nonuniform composition energy gradient");
        close(derivatives.cv[0],1/T,"nonuniform composition cv gradient");
        fixture.valid3[fixture.three.free_energy_index(1,1,2)]=0;
        fixture.valid4[fixture.four.free_energy_index(1,1,1,1)]=0;
        rejected([&]{fixture.three.get_eint_from_T(1.3,T,X);},"3D composition corner mask ignored");
        rejected([&]{fixture.four.get_eint_from_T(1.3,T,X);},"4D composition corner mask ignored");
        std::cout << "strict free-energy 3D/4D exact states, endpoint/domain/mask and all-root inverses PASS\n";
    } catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
