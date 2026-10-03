/**
 * @file HelmComponentsRegression.cpp
 * @brief Selective electron/photon Helmholtz components without ion subtraction.
 *
 * An independent quadratic free energy checks every component field. Actual
 * Timmes data check direct-electron identity, strict table endpoints and the
 * unchanged complete-EOS reference. No alternate runtime EOS is introduced.
 */
#include "physics/eos/HelmEos.h"
#include "physics/eos/sources/HelmTableReader.h"
#include "fixtures/eos/HelmReference.h"
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using View = BasicHelmEosView<SpeciesHostView>;
using Component = View::ComponentThermodynamics;

void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

void close(double actual, double expected, double tolerance, const char* message)
{
    if (!std::isfinite(actual)
        || std::abs(actual - expected) > tolerance * std::max(std::abs(expected), 1.0)) {
        std::cerr << std::setprecision(17) << message << ": actual=" << actual
                  << " expected=" << expected << " tolerance=" << tolerance << '\n';
        throw std::runtime_error(message);
    }
}

bool invalid(const Component& value)
{
    return std::isnan(value.pressure) && std::isnan(value.energy)
        && std::isnan(value.free_energy) && std::isnan(value.cv)
        && std::isnan(value.pressure_density) && std::isnan(value.pressure_temperature)
        && std::isnan(value.energy_density) && std::isnan(value.cv_temperature)
        && std::isnan(value.entropy);
}

void table_reader_boundaries()
{
    using helm_eos::loader::HelmTableReader;
    const auto same_bits = [](double a, double b) {
        return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
    };
    for (const std::string token : {"-0", "+0", "1.2345678901234567", "+1e3",
            "5e-324", "-5e-324", "1e-500", "-1e-500", "+1e-500"}) {
        std::istringstream reference(token), input(token);
        double expected = 77.0, actual = 77.0;
        require(static_cast<bool>(reference >> expected), "reader fixture rejected by original extractor");
        HelmTableReader reader(input);
        require(reader.read(actual) && same_bits(actual, expected),
                "table reader changed finite extraction, including underflow or signed zero");
        require(!reader.read(actual), "table reader did not finish at EOF");
    }
    for (const std::string token : {"", "+", "-", "+-1", "++1", "--1", "-+1",
            "1e", "1e+", "1junk", "nan", "inf", "1e500", "1e500junk", "0x1p2"}) {
        std::istringstream input(token);
        HelmTableReader reader(input);
        double value = 77.0;
        require(!reader.read(value) && value == 77.0 && !reader.read(value),
                "table reader published invalid data or lost sticky failure");
    }
    // Cross a byte window with a number, whitespace and a very long valid
    // token. The final signed zero deliberately has no newline.
    const std::string text = std::string(65535, ' ') + "+1.2345678901234567e-5 "
        + std::string(131072, '0') + "1 " + std::string(131073, '\t') + "-0";
    std::istringstream reference(text), input(text);
    HelmTableReader reader(input);
    for (int i = 0; i < 3; ++i) {
        double expected, actual;
        require(static_cast<bool>(reference >> expected) && reader.read(actual)
                && same_bits(actual, expected), "table reader changed a chunk-boundary token");
    }
    double ignored;
    require(!reader.read(ignored), "table reader did not finish a chunk-boundary stream");

    // Expose readable bytes together with a real IO failure. A final clean
    // EOF is accepted above; badbit must never publish the apparent token.
    struct PartialFaultBuffer : std::streambuf {
        std::istream* owner = nullptr;
        std::streamsize xsgetn(char* target, std::streamsize) override {
            std::memcpy(target, "1.0 ", 4);
            owner->setstate(std::ios::badbit);
            return 4;
        }
    } fault;
    std::istream broken(&fault);
    fault.owner = &broken;
    HelmTableReader broken_reader(broken);
    double unchanged = 77.0;
    require(!broken_reader.read(unchanged) && unchanged == 77.0,
            "table reader accepted bytes from a failed stream");
}

void table_reader_identity(const char* path)
{
    // Compare all 21 original fields, including the eight unused fields,
    // with formatted input. This checks conversion independently of EOS.
    std::ifstream reference(path), input(path);
    require(reference.is_open() && input.is_open(), "cannot open reader reference table");
    helm_eos::loader::HelmTableReader reader(input);
    constexpr std::size_t count = 21 * View::imax * View::jmax;
    for (std::size_t i = 0; i < count; ++i) {
        double expected, actual;
        require(static_cast<bool>(reference >> expected) && reader.read(actual),
                "canonical reader comparison ended early");
        require(std::bit_cast<std::uint64_t>(expected) == std::bit_cast<std::uint64_t>(actual),
                "canonical table decimal conversion differs from original extractor");
    }
    double extra;
    require(!(reference >> extra) && !reader.read(extra), "canonical table has extra numeric fields");
}

void boundaries(const View& view)
{
    Component result;
    for (double density : {view.density_nodes[0], view.density_nodes[View::imax - 1]})
        for (double temperature : {view.temperature_nodes[0], view.temperature_nodes[View::jmax - 1]})
            require(view.electron_positron_component(density, temperature, 1.0, result),
                    "electron component rejected an exact supported endpoint");
    const double low_density = std::nextafter(view.density_nodes[0], 0.0);
    const double high_density = std::nextafter(view.density_nodes[View::imax - 1],
                                               std::numeric_limits<double>::infinity());
    const double low_temperature = std::nextafter(view.temperature_nodes[0], 0.0);
    const double high_temperature = std::nextafter(view.temperature_nodes[View::jmax - 1],
                                                   std::numeric_limits<double>::infinity());
    const double rho = 0.5 * (view.density_nodes[0] + view.density_nodes[View::imax - 1]);
    const double T = 0.5 * (view.temperature_nodes[0] + view.temperature_nodes[View::jmax - 1]);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double rejected[][3]{{low_density, T, 1.0}, {high_density, T, 1.0},
        {rho, low_temperature, 0.5}, {rho, high_temperature, 0.5},
        {rho, T, 0.0}, {rho, T, 1.01}, {rho, T, -0.1},
        {nan, T, 0.5}, {rho, nan, 0.5}, {rho, T, nan}, {0.0, T, 0.5}};
    for (const auto& point : rejected) {
        require(!view.electron_positron_component(point[0], point[1], point[2], result)
                && invalid(result), "electron component extrapolated or retained stale output");
    }
    View missing = view;
    missing.f[4] = nullptr;
    require(!missing.electron_positron_component(rho, T, 0.5, result) && invalid(result),
            "electron component accepted missing free-energy data");
    require(!View::photon_component(0.0, 1e8, result) && invalid(result),
            "photon component accepted zero density");
    require(!View::photon_component(1e6, nan, result) && invalid(result),
            "photon component accepted NaN temperature");
}

void polynomial_components()
{
    // Electron-table F(d,T)=a*d-b*T^2+m*d*T, d=rho*Ye. The Hermite
    // interpolant must reproduce this polynomial, not just approximate a gas.
    // Dyadic nodes and coefficients make the fixture data exactly representable;
    // third derivatives must not mistake table-value rounding for polynomial error.
    constexpr double a = 2.0, b = 0.5, m = 0.125;
    View view;
    std::array<double, View::imax> density;
    std::array<double, View::jmax> temperature;
    for (int i = 0; i < View::imax; ++i)
        density[i] = 1.0 + i;
    for (int j = 0; j < View::jmax; ++j)
        temperature[j] = 1024.0 + 16.0*j;
    std::array<std::vector<double>, 9> fields;
    for (int field = 0; field < 9; ++field) {
        fields[field].resize(View::imax * View::jmax);
        view.f[field] = fields[field].data();
    }
    view.density_nodes = density.data();
    view.temperature_nodes = temperature.data();
    for (int j = 0; j < View::jmax; ++j) for (int i = 0; i < View::imax; ++i) {
        const int index = j * View::imax + i;
        const double d = density[i], T = temperature[j];
        fields[0][index] = a*d - b*T*T + m*d*T;
        fields[1][index] = a + m*T;
        fields[2][index] = -2*b*T + m*d;
        fields[4][index] = -2*b;
        fields[5][index] = m;
    }
    const double points[][3]{{64.0, 2040.0, 0.5}, {63.0, 2051.0, 0.25}, {192.0, 3103.0, 0.75}};
    for (const auto& point : points) {
        const double rho = point[0], T = point[1], ye = point[2], d = rho * ye;
        Component actual;
        require(view.electron_positron_component(rho, T, ye, actual),
                "polynomial component query failed");
        close(actual.pressure, d*d*(a+m*T), 2e-11, "polynomial electron pressure");
        close(actual.energy, ye*(a*d+b*T*T), 2e-11, "polynomial electron energy");
        close(actual.free_energy, ye*(a*d-b*T*T+m*d*T), 2e-11, "polynomial electron free energy");
        close(actual.entropy, ye*(2*b*T-m*d), 2e-11, "polynomial electron entropy");
        close(actual.cv, 2*ye*b*T, 2e-11, "polynomial electron heat capacity");
        close(actual.pressure_density, 2*ye*d*(a+m*T), 2e-11, "polynomial electron pressure-density derivative");
        close(actual.pressure_temperature, m*d*d, 2e-11, "polynomial electron pressure-temperature derivative");
        close(actual.energy_density, a*ye*ye, 2e-11, "polynomial electron energy-density derivative");
        close(actual.cv_temperature, 2*ye*b, 2e-11, "polynomial electron heat-capacity derivative");

        Component photon;
        require(View::photon_component(rho, T, photon), "photon component requires a table");
        const long double energy_density = static_cast<long double>(View::asol)
            * std::pow(static_cast<long double>(T), 4);
        close(photon.pressure, static_cast<double>(energy_density / 3), 2e-15, "photon pressure");
        close(photon.energy, static_cast<double>(energy_density / rho), 2e-15, "photon energy");
        close(photon.free_energy, static_cast<double>(-energy_density / (3*rho)), 2e-15, "photon free energy");
        close(photon.entropy, static_cast<double>(4*energy_density / (3*rho*T)), 2e-15, "photon entropy");
        close(photon.cv, static_cast<double>(4*energy_density / (rho*T)), 2e-15, "photon cv");
        close(photon.pressure_density, 0.0, 0.0, "photon fixed-T pressure-density derivative");
        close(photon.pressure_temperature, 4*photon.pressure/T, 2e-15, "photon pressure-temperature derivative");
        close(photon.energy_density, -photon.energy/rho, 2e-15, "photon energy-density derivative");
        close(photon.cv_temperature, 3*photon.cv/T, 2e-15, "photon heat-capacity derivative");
    }
    boundaries(view);
}

void coulomb_controls(const HelmEos& owner)
{
    const double x[]{.25,.75};
    constexpr double y=.25+.75/4., ye=.625;
    for (const auto point : {std::array<double,2>{1e2,1.73e8}, {2.345e6,7.36e7}, {1e9,2.37e7}}) {
        const double rho=point[0], T=point[1];
        std::array<std::array<double,13>,3> result{};
        for (int index=0;index<3;++index) {
            auto view=owner.get_view(); view.coulomb_mult=.5*index;
            auto& v=result[index]; View::ThermodynamicDerivatives d;
            view.calc_thermo_with_cv(rho,T,x,v[0],v[1],&v[2],&d);
            const double fields[]{d.pressure_density,d.pressure_temperature,d.energy_y,d.energy_z,
                d.energy_yy,d.energy_yz,d.energy_zz,d.cv_y,d.cv_z,d.cv_temperature};
            std::copy(std::begin(fields),std::end(fields),v.begin()+3);
            close(view.get_temperature(rho,v[1],x),T,1e-10,"Coulomb energy inverse");
            close(view.invert_temperature(rho,v[0],x,true),T,1e-10,"Coulomb pressure inverse");
            double pp,ep,cp,pm,em,cm; constexpr double h=1e-4;
            view.calc_thermo_with_cv(rho,T*(1+h),x,pp,ep,&cp);
            view.calc_thermo_with_cv(rho,T*(1-h),x,pm,em,&cm);
            close((ep-em)/(2*h*T),v[2],3e-5,"scaled Coulomb cv finite difference");
            close((pp-pm)/(2*h*T),d.pressure_temperature,3e-5,"scaled Coulomb p_T finite difference");
            close((cp-cm)/(2*h*T),d.cv_temperature,3e-5,"scaled Coulomb cv_T finite difference");
        }
        Component electron, photon;
        require(owner.electron_positron_component(rho,T,ye,electron),"electron reference component");
        require(View::photon_component(rho,T,photon),"photon reference component");
        const double ion=View::avo*View::kerg*y;
        close(result[0][0],electron.pressure+photon.pressure+rho*ion*T,2e-15,"zero Coulomb pressure component sum");
        close(result[0][1],electron.energy+photon.energy+1.5*ion*T,2e-15,"zero Coulomb energy component sum");
        close(result[0][2],electron.cv+photon.cv+1.5*ion,2e-15,"zero Coulomb cv component sum");
        for(int field=0;field<13;++field)
            close(result[1][field],.5*(result[0][field]+result[2][field]),3e-13,
                  "Coulomb fraction scales all derivatives consistently");
    }
}

void actual_table(const char* path)
{
    SpeciesManager species;
    species.add_species("h1", 1, 1, 5.0/3.0, 0.0);
    species.add_species("he4", 4, 2, 5.0/3.0, 0.0);
    HelmEos eos(path, &species);
    coulomb_controls(eos);
    const double fractions[]{0.25, 0.75};
    constexpr double ye = 0.625;
    for (const auto& point : HelmReference::points) {
        Component electron;
        require(eos.electron_positron_component(point.rho, point.temperature, ye, electron),
                "actual Timmes electron component failed");
        double pressure, energy, cv, free_energy, entropy;
        View::ThermodynamicDerivatives derivatives;
        eos.interpolate_ele_pos(point.rho, point.temperature, ye, pressure, energy,
                                &cv, &derivatives, &free_energy, &entropy);
        require(electron.pressure == pressure && electron.energy == energy
                && electron.cv == cv && electron.free_energy == free_energy && electron.entropy == entropy
                && electron.pressure_density == derivatives.pressure_density
                && electron.pressure_temperature == derivatives.pressure_temperature,
                "selective electron component contains an extra correction");
        close(electron.free_energy + point.temperature * electron.entropy,
              electron.energy, 4*std::numeric_limits<double>::epsilon(),
              "electron potential entropy energy identity");
        close(electron.pressure, point.values[3], 8e-14, "independent Timmes electron reference");
        eos.calc_thermo_with_cv(point.rho, point.temperature, fractions, pressure, energy, &cv);
        close(pressure, point.values[0], 8e-14, "unchanged complete Helmholtz pressure");
        close(energy, point.values[1], 1.5e-15, "unchanged complete Helmholtz energy");
        close(cv, point.values[2], 32*std::numeric_limits<double>::epsilon(),
              "unchanged complete Helmholtz cv");
    }
    // A temperature inverse may reuse only its unchanged density/composition
    // factors. Compare fresh and prepared evaluations across temperatures and
    // ensure that skipping unused derivative outputs preserves P/e/cv exactly.
    for (double rho : {1e-4, 1.0, 1e6, 1e9}) {
        const auto fixed = eos.prepare_fixed_density(rho, fractions);
        for (double T : {1e3, 1e5, 1e8, 1.23456789e8, 1e10, 1e13}) {
            double p, e, cv, prepared_p, prepared_e, prepared_cv;
            View::ThermodynamicDerivatives d;
            eos.calc_thermo_with_cv(rho, T, fractions, p, e, &cv, &d);
            eos.calc_thermo_with_cv(rho, T, fractions,
                                   prepared_p, prepared_e, &prepared_cv, nullptr, &fixed);
            require(p == prepared_p && e == prepared_e && cv == prepared_cv,
                    "prepared Helm inverse factors changed thermodynamics");
            for (auto request : {View::JetRequest::Acoustic,View::JetRequest::FirstLaw}) {
                View::ThermodynamicDerivatives selective;
                eos.calc_thermo_with_cv(rho,T,fractions,prepared_p,prepared_e,
                                       &prepared_cv,&selective,&fixed,request);
                require(p==prepared_p && e==prepared_e && cv==prepared_cv
                    && d.pressure_density==selective.pressure_density
                    && d.pressure_temperature==selective.pressure_temperature,
                    "selective Helm acoustic jet changed retained values");
                if(request==View::JetRequest::FirstLaw)
                    require(d.energy_y==selective.energy_y && d.energy_z==selective.energy_z,
                            "selective Helm first-law jet changed retained values");
            }
        }
    }
    // Inverse recovery spans ideal, degenerate and radiation-dominated states.
    // Energy rounding limits temperature accuracy by e/(Cv*T); it must not
    // cause a spurious failure or justify extrapolation beyond source bounds.
    for (double rho : {1e-4, 1.0, 1e6, 1e9}) {
        for (double T : {1e3, 1e5, 1e8, 1.23456789e8, 1e10, 1e13}) {
            const double energy=eos.get_eint_from_T(rho,T,fractions);
            const double recovered=eos.get_temperature(rho,energy,fractions);
            const double condition=std::max(1.0,std::abs(energy/(eos.get_cv(rho,T,fractions)*T)));
            close(recovered,T,32*std::numeric_limits<double>::epsilon()*condition,
                  "bounded Helm temperature inverse");
            close(eos.get_eint_from_T(rho,recovered,fractions),energy,
                  64*std::numeric_limits<double>::epsilon(),"Helm inverse energy residual");
        }
        const double minimum=eos.get_eint_from_T(rho,1e3,fractions);
        const double maximum=eos.get_eint_from_T(rho,1e13,fractions);
        require(std::isnan(eos.get_temperature(rho,minimum-std::abs(minimum)*1e-8,fractions))
                && std::isnan(eos.get_temperature(rho,maximum+std::abs(maximum)*1e-8,fractions)),
                "Helm inverse accepted target outside source bounds");
    }
    // A hydro endpoint now recovers pressure and sound speed from one strict
    // inverse. Match the original scalar derivative identity across the
    // degenerate and radiation-dominated portions of the source table.
    for (double rho : {1e-4, 1.0, 1e6, 1e9}) {
        for (double T : {1e5, 1e8, 1e10}) {
            const double energy = eos.get_eint_from_T(rho, T, fractions);
            const double old_pressure = eos.get_pressure_from_rho_e(
                rho, energy, fractions);
            const double chi = eos.get_dp_drho_e(rho, energy, fractions);
            const double kappa = eos.get_dp_de_rho(rho, energy, fractions);
            const double old_speed =
                std::sqrt(chi + (kappa / rho) * (old_pressure / rho));
            double pressure = 0.0, speed = 0.0;
            eos.get_pressure_and_sound_speed(
                rho, energy, fractions, pressure, speed);
            close(pressure, old_pressure, 1e-12,
                  "grouped Helm endpoint pressure");
            close(speed, old_speed, 1e-12,
                  "grouped Helm endpoint sound speed");
        }
        const double minimum = eos.get_eint_from_T(rho, 1e3, fractions);
        const double below = minimum - std::abs(minimum) * 1e-8;
        double pressure = 0.0, speed = 0.0;
        eos.get_pressure_and_sound_speed(
            rho, below, fractions, pressure, speed);
        require(std::isnan(pressure) && std::isnan(speed),
                "grouped Helm endpoint crossed the strict source domain");
    }
    // Burn's first-law RHS and Jacobian must see the same derivatives as
    // the independent scalar Helm queries, including degenerate states.
    for (double rho : {1e-4, 1e6, 1e9}) {
        for (double T : {1e8, 1e10}) {
            double scalar_gradient[2]{}, grouped_gradient[2]{};
            const double scalar_cv = eos.get_cv(rho, T, fractions);
            eos.get_energy_composition_gradient<3>(
                rho, T, fractions, scalar_gradient);
            double grouped_cv = 0.0;
            eos.get_cv_and_energy_composition_gradient<3>(
                rho, T, fractions, grouped_cv, grouped_gradient);
            close(grouped_cv, scalar_cv, 1e-15, "grouped burn heat capacity");
            for (int i = 0; i < 2; ++i)
                close(grouped_gradient[i], scalar_gradient[i], 1e-15,
                      "grouped burn energy-composition gradient");
            const double flow[]{0.01, -0.02};
            double scalar_cv_gradient[3]{}, grouped_cv_gradient[3]{};
            double scalar_hessian[3]{}, grouped_hessian[3]{};
            eos.get_cv_gradient<3>(rho, T, fractions, scalar_cv_gradient);
            eos.get_energy_composition_hessian_action<3>(
                rho, T, fractions, flow, scalar_hessian);
            eos.get_cv_gradient_and_energy_composition_hessian_action<3>(
                rho, T, fractions, flow,
                grouped_cv_gradient, grouped_hessian);
            for (int i = 0; i < 3; ++i) {
                close(grouped_cv_gradient[i], scalar_cv_gradient[i], 1e-15,
                      "grouped burn heat-capacity gradient");
                close(grouped_hessian[i], scalar_hessian[i], 1e-15,
                      "grouped burn energy Hessian action");
            }
        }
    }
    // Compare the public scalar/acoustic queries with their inactive-scope
    // results, including distinct one-ULP inputs and a different EOS owner.
    // This checks returned thermodynamics, not cache implementation details.
    const auto acoustic_fields = [](const View& view, double rho, double energy,
                                    const double* x) {
        std::array<double, 7> values{};
        values[0] = view.get_pressure_from_rho_e(rho, energy, x);
        view.get_pressure_and_sound_speed(rho, energy, x, values[1], values[2]);
        values[3] = view.get_dp_drho_e(rho, energy, x);
        values[4] = view.get_dp_de_rho(rho, energy, x);
        view.get_dp_drho_e_and_dp_de_rho(rho, energy, x, values[5], values[6]);
        return values;
    };
    struct AcousticPoint {
        double rho, energy;
        std::array<double, 2> fractions;
        std::array<double, 7> expected;
    };
    std::vector<AcousticPoint> points;
    for (int index = 0; index < 280; ++index) {
        const double rho = 1e5 + 1000 * index, temperature = 1e7 + 12345 * index;
        std::array<double, 2> x{.2 + index * .001, .8 - index * .001};
        const double energy = eos.get_eint_from_T(rho, temperature, x.data());
        for (int variant = 0; variant < 4; ++variant) {
            const double density = variant == 1 ? std::nextafter(rho, INFINITY) : rho;
            const double target = variant == 2 ? std::nextafter(energy, INFINITY) : energy;
            auto composition = x;
            if (variant == 3) composition[0] = std::nextafter(composition[0], INFINITY);
            points.push_back({density, target, composition,
                acoustic_fields(eos, density, target, composition.data())});
        }
    }
    View different_owner = eos;
    different_owner.coulomb_mult = .5;
    const auto other_expected = acoustic_fields(different_owner,
        points[0].rho, points[0].energy, points[0].fractions.data());
    {
        View::HostHydroScope scope(eos);
        for (int repeat = 0; repeat < 2; ++repeat) for (const auto& point : points) {
            // Alternate which valid query populates a state first.
            if (repeat) (void)eos.get_dp_drho_e(point.rho, point.energy, point.fractions.data());
            require(acoustic_fields(eos, point.rho, point.energy, point.fractions.data())
                    == point.expected, "scoped EOS queries changed exact thermodynamics");
            require(acoustic_fields(eos, point.rho, point.energy, point.fractions.data())
                    == point.expected, "repeated scoped EOS queries changed exact thermodynamics");
        }
        require(acoustic_fields(different_owner, points[0].rho, points[0].energy,
                points[0].fractions.data()) == other_expected,
                "scoped EOS queries reused a different EOS owner");
        require(std::isnan(eos.get_temperature(points[0].rho, -1., points[0].fractions.data())),
                "scoped EOS queries served success for invalid energy");
    }
    // Exercise exact inverse hits, slot collisions, key changes and nested
    // lexical scope restoration against the original uncached root solve.
    {
        View::HostHydroScope scope(eos);
        for (int repeat=0;repeat<2;++repeat) for (int index=0;index<300;++index) {
            const double rho=1e5+1000*index, temperature=1e7+12345*index;
            const double x[]{.2+index*.001, .8-index*.001};
            for (bool pressure : {false,true}) {
                const double target=pressure ? eos.get_pressure_from_rho_T(rho,temperature,x)
                                             : eos.get_eint_from_T(rho,temperature,x);
                const double expected=eos.invert_temperature_uncached(rho,target,x,pressure);
                require(eos.invert_temperature(rho,target,x,pressure)==expected
                    && eos.invert_temperature(rho,target,x,pressure)==expected,
                    "host inverse memo changed strict root");
            }
        }
        require(std::isnan(eos.get_temperature(1e6,-1.,fractions)),
                "host inverse memo hid invalid energy");
        { View::HostHydroScope nested(eos); }
        require(View::host_inverse_workspace && View::host_inverse_workspace->owner==&eos,
                "nested inverse scope did not restore its caller");
    }
    require(View::host_inverse_workspace==nullptr,"inverse cache escaped hydro scope");
    boundaries(eos);
}
} // namespace

int main(int argc, char** argv)
{
    try {
        require(argc == 2, "expected path to canonical helm_table.dat");
        table_reader_boundaries();
        table_reader_identity(argv[1]);
        polynomial_components();
        actual_table(argv[1]);
        std::cout << "Helmholtz electron/photon components: independent fields, strict domain and complete-EOS reference PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
