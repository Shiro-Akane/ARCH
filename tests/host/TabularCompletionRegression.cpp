/**
 * @file TabularCompletionRegression.cpp
 * @brief Independent component-assembly and single-potential regression checks.
 *
 * Manufactured baryon free energies and separately queried Helm components
 * produce explicit total tables. Compare their public 3D/4D owners with tables
 * completed at import. This tests the data contract and its thermodynamic
 * closure, not the accuracy of any external nuclear-matter model or Shen table.
 */
#include "physics/eos/HelmEos.h"
#include "physics/eos/Tabular3DEOS.h"
#include "physics/eos/Tabular4DEOS.h"
#include "physics/eos/TabularSource.h"
#include "physics/eos/eosdispatch.h"
#include "core/FileFingerprint.h"
#include <highfive/H5File.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Path = std::filesystem::path;
using HelmView = HelmEosHostView;
constexpr int nr = 25, nt = 29, nx = 5, na = 3, nz = 3;
constexpr double lr0 = 5.0, lr1 = 6.0, lt0 = 8.0, lt1 = 8.7;
constexpr double ye0 = 0.25, ye1 = 0.85;
constexpr double a0 = 1.5, a1 = 2.5, z0 = 0.6, z1 = 1.4;
constexpr double gas_R = 8.0e7, energy_origin = 4.0e16;

void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

double close(double actual, double expected, double tolerance, const char* message,
             double reference_scale = 1.0)
{
    const double scale = std::max(std::abs(expected), reference_scale);
    const double error = std::abs(actual - expected) / scale;
    if (!std::isfinite(actual) || !std::isfinite(expected) || error > tolerance) {
        std::cerr << std::setprecision(17) << message << ": actual=" << actual
                  << " expected=" << expected << " scaled error=" << error
                  << " tolerance=" << tolerance << '\n';
        throw std::runtime_error(message);
    }
    return error;
}

template <class Action>
void rejects(Action action, const char* reason, const char* message)
{
    try { action(); }
    catch (const std::exception& error) {
        if (std::string(error.what()).find(reason) != std::string::npos) return;
        throw std::runtime_error(std::string(message) + ": unexpected rejection: " + error.what());
    }
    throw std::runtime_error(std::string("Accepted invalid completion input: ") + message);
}

double node(int index, int count, double lower, double upper)
{
    return index + 1 == count ? upper : lower + index * (upper - lower) / (count - 1);
}

struct Input {
    int rank = 3;
    bool electrons = false;
    bool photons = false;
    std::string declaration = "baryons";
    std::string composition_axis = "Ye";
    int equilibrium = -1;
    bool direct = false;
    double baryon_mass_g = 0.0;
};

void field(HighFive::File& file, const char* name,
           const std::vector<std::size_t>& shape, const std::vector<double>& values)
{
    file.createDataSet<double>(name, HighFive::DataSpace(shape)).write_raw(values.data());
}

void write_table(const Path& path, const Input& input, const HelmView& helm)
{
    const int nc = input.rank == 3 ? nx : na * nz;
    const double electron_mass_scale = input.baryon_mass_g > 0.0
        ? 1.0 / (input.baryon_mass_g * HelmView::avo) : 1.0;
    const std::vector<std::size_t> shape = input.rank == 3
        ? std::vector<std::size_t>{nr, nt, nx}
        : std::vector<std::size_t>{nr, nt, na, nz};
    std::vector<double> potential(nr * nt * nc);
    std::array<std::vector<double>, 4> direct;
    if (input.direct) for (auto& values : direct) values.resize(potential.size());
    for (int r = 0; r < nr; ++r) for (int t = 0; t < nt; ++t)
        for (int c = 0; c < nc; ++c) {
            const double rho = std::pow(10.0, node(r, nr, lr0, lr1));
            const double T = std::pow(10.0, node(t, nt, lt0, lt1));
            const double A = input.rank == 3 ? 2.0 : node(c / nz, na, a0, a1);
            const double Ye = input.rank == 3 ? node(c, nx, ye0, ye1)
                : node(c % nz, nz, z0, z1) / A;
            const double R = gas_R * (1.0 + 0.2 * Ye) / A;
            const double origin = energy_origin + 3.0e15 * Ye * Ye;
            const double baryon = R * T * (std::log(rho / 1e5)
                - 1.5 * std::log(T / 1e8)) + origin;
            // Long-double accumulation is independent of the importer's
            // compensated summation. Only the documented component API is shared.
            long double total = baryon;
            HelmView::ComponentThermodynamics component;
            if (input.electrons) {
                require(helm.electron_positron_component(rho * electron_mass_scale, T, Ye, component),
                        "fixture electron state is outside the real Helm table");
                total += static_cast<long double>(component.free_energy) * electron_mass_scale;
            }
            if (input.photons) {
                require(HelmView::photon_component(rho, T, component),
                        "fixture photon state is invalid");
                total += static_cast<long double>(component.free_energy);
            }
            const auto index = (r * nt + t) * nc + c;
            potential[index] = static_cast<double>(total);
            if (input.direct) {
                direct[0][index] = rho * R * T;
                direct[1][index] = 1.5 * R * T + origin;
                direct[2][index] = std::sqrt((5.0 / 3.0) * R * T);
                direct[3][index] = 1.5 * R;
            }
        }
    HighFive::File file(path.string(), HighFive::File::Overwrite);
    file.createDataSet("arch_eos_version", 1);
    file.createDataSet("table_rank", input.rank);
    file.createDataSet("thermodynamic_model", std::string(input.direct ? "direct" : "free_energy"));
    file.createDataSet("n_rho", nr);
    file.createDataSet("n_T", nt);
    file.createDataSet("log_rho_min", lr0);
    file.createDataSet("log_rho_max", lr1);
    file.createDataSet("log_T_min", lt0);
    file.createDataSet("log_T_max", lt1);
    if (!input.declaration.empty()) file.createDataSet("eos_components", input.declaration);
    if (input.equilibrium >= 0) file.createDataSet("nuclear_equilibrium", input.equilibrium);
    if (input.baryon_mass_g > 0.0) file.createDataSet("baryon_mass_g", input.baryon_mass_g);
    if (input.rank == 3) {
        file.createDataSet("n_X", nx);
        file.createDataSet("X_min", ye0);
        file.createDataSet("X_max", ye1);
        if (!input.composition_axis.empty())
            file.createDataSet("composition_axis", input.composition_axis);
    } else {
        file.createDataSet("n_A", na);
        file.createDataSet("n_Z", nz);
        file.createDataSet("A_min", a0);
        file.createDataSet("A_max", a1);
        file.createDataSet("Z_min", z0);
        file.createDataSet("Z_max", z1);
    }
    if (input.direct) {
        const char* names[]{"pressure", "energy", "sound_speed", "cv"};
        for (int i = 0; i < 4; ++i) field(file, names[i], shape, direct[i]);
    } else field(file, "free_energy", shape, potential);
}

SpeciesManager species()
{
    SpeciesManager result;
    result.add_species("h1", 1, 1, 5.0 / 3.0, 1.5e8);
    result.add_species("he4", 4, 2, 5.0 / 3.0, 3.75e7);
    result.add_species("n", 1, 0, 5.0 / 3.0, 1.5e8);
    return result;
}

double potential(const Tabular3DEOSHostView& eos, double rho, double T, const double* X)
{
    return eos.interpolate_free_energy(rho, T, eos.get_target_X(X)).a;
}

double potential(const Tabular4DEOSHostView& eos, double rho, double T, const double* X)
{
    return eos.interpolate_free_energy(rho, T, eos.get_Abar(X), eos.get_Zbar(X)).a;
}

template <class View>
void closure(const View& eos, double rho, double T, const double* X)
{
    // These differences operate on the queried potential/energy, not on the
    // derivative arrays or another copy of the thermodynamic formulas.
    constexpr double step = 2e-5;
    const double dr = rho * step, dt = T * step;
    const double pressure = eos.get_pressure_from_rho_T(rho, T, X);
    const double cv = eos.get_cv(rho, T, X);
    const double fr = (potential(eos, rho + dr, T, X)
                     - potential(eos, rho - dr, T, X)) / (2 * dr);
    close(rho * rho * fr, pressure, 2e-6, "P = rho^2 dF/drho");
    const double ft = (potential(eos, rho, T + dt, X)
                     - potential(eos, rho, T - dt, X)) / (2 * dt);
    close(potential(eos, rho, T, X) - T * ft,
          eos.get_eint_from_T(rho, T, X), 2e-6, "e = F - T dF/dT");
    const double energy_T = (eos.get_eint_from_T(rho, T + dt, X)
                           - eos.get_eint_from_T(rho, T - dt, X)) / (2 * dt);
    close(energy_T, cv, 2e-6, "cv = de/dT");
    const double energy_rho = (eos.get_eint_from_T(rho + dr, T, X)
                             - eos.get_eint_from_T(rho - dr, T, X)) / (2 * dr);
    const double pressure_T = (eos.get_pressure_from_rho_T(rho, T + dt, X)
                             - eos.get_pressure_from_rho_T(rho, T - dt, X)) / (2 * dt);
    close(rho * rho * energy_rho, pressure - T * pressure_T,
          3e-6, "first-law density identity", pressure);

    const double direction[]{0.17, -0.11, -0.06, 0.0};
    double plus[3], minus[3], gradient[4], capacity_gradient[4];
    constexpr double dx = 1e-5;
    for (int i = 0; i < 3; ++i) {
        plus[i] = X[i] + dx * direction[i];
        minus[i] = X[i] - dx * direction[i];
    }
    eos.template get_energy_composition_gradient<4>(rho, T, X, gradient);
    eos.template get_cv_gradient<4>(rho, T, X, capacity_gradient);
    double energy_direction = 0.0, cv_direction = 0.0;
    for (int i = 0; i < 3; ++i) {
        energy_direction += gradient[i] * direction[i];
        cv_direction += capacity_gradient[i] * direction[i];
    }
    close((eos.get_eint_from_T(rho, T, plus) - eos.get_eint_from_T(rho, T, minus)) / (2 * dx),
          energy_direction, 2e-6, "completion energy composition gradient");
    close((eos.get_cv(rho, T, plus) - eos.get_cv(rho, T, minus)) / (2 * dx),
          cv_direction, 2e-6, "completion cv composition gradient", cv);
}

template <class View>
void compare(const View& completed, const View& total)
{
    require(completed.uses_free_energy && total.uses_free_energy,
            "component completion did not retain a single free-energy closure");
    require(completed.energy_reference_shift == 0.0,
            "positive-energy manufactured table acquired an unnecessary energy shift");
    double worst = 0.0;
    for (int i = 0; i < 12; ++i) {
        const double rho = std::pow(10.0, 5.12 + 0.75 * (i + 0.37) / 12);
        const double T = std::pow(10.0, 8.09 + 0.51 * ((7 * i) % 12 + 0.23) / 12);
        const double h = 0.14 + 0.13 * (i + 0.31) / 12;
        const double he = 0.61 + 0.065 * ((5 * i) % 12 + 0.19) / 12;
        const double X[]{h, he, 1.0 - h - he};
        const double e = total.get_eint_from_T(rho, T, X);
        const auto check = [&](double a, double b, const char* label) {
            worst = std::max(worst, close(a, b, 2e-8, label));
        };
        check(completed.get_pressure_from_rho_T(rho, T, X),
              total.get_pressure_from_rho_T(rho, T, X), "completed pressure");
        check(completed.get_eint_from_T(rho, T, X), e, "completed energy");
        check(completed.get_cv(rho, T, X), total.get_cv(rho, T, X), "completed cv");
        check(completed.get_sound_speed_from_rho_T(rho, T, X),
              total.get_sound_speed_from_rho_T(rho, T, X), "completed sound speed");
        check(completed.get_dp_drho_e(rho, e, X), total.get_dp_drho_e(rho, e, X), "completed chi");
        check(completed.get_dp_de_rho(rho, e, X), total.get_dp_de_rho(rho, e, X), "completed kappa");
        check(completed.get_temperature(rho, e, X), T, "completed inverse");
        check(total.get_temperature(rho, e, X), T, "explicit-total inverse");
        double g[4], reference_g[4], c[4], reference_c[4], a[4], reference_a[4];
        const double flow[]{0.17, -0.11, -0.06, 0.0};
        completed.template get_energy_composition_gradient<4>(rho, T, X, g);
        total.template get_energy_composition_gradient<4>(rho, T, X, reference_g);
        completed.template get_cv_gradient<4>(rho, T, X, c);
        total.template get_cv_gradient<4>(rho, T, X, reference_c);
        completed.template get_energy_composition_hessian_action<4>(rho, T, X, flow, a);
        total.template get_energy_composition_hessian_action<4>(rho, T, X, flow, reference_a);
        for (int j = 0; j < 4; ++j) {
            check(g[j], reference_g[j], "completed energy gradient");
            check(c[j], reference_c[j], "completed cv gradient");
            check(a[j], reference_a[j], "completed energy Hessian action");
        }
        closure(completed, rho, T, X);
    }
    const double X[]{0.2, 0.65, 0.15};
    rejects([&] { completed.get_pressure_from_rho_T(std::pow(10.0, lr0 - 0.01), 2e8, X); },
            "domain", "completed table silently extrapolated its finite source domain");
    std::cout << std::setprecision(8) << "component assembly max scaled difference=" << worst << '\n';
}

void equivalence(const Path& dir, const std::string& helm_path, const HelmView& helm,
                 SpeciesManager& specs)
{
    const auto missing = (dir / "absent-helm-table.dat").string();
    for (int rank : {3, 4}) {
        const auto total_path = dir / ("total-" + std::to_string(rank) + "d.h5");
        Input total{rank, true, true, "baryons,electrons_positrons,photons"};
        total.equilibrium = 1;
        write_table(total_path, total, helm);
        const auto info = inspect_tabular_source(total_path.string());
        require(info.rank == rank && info.nuclear_equilibrium && info.components.declared
                && !info.components.needs_completion(), "total-table metadata was lost");
        require(tabular_source_fingerprint(total_path.string(), missing)
                == tabular_source_fingerprint(total_path.string(), helm_path),
                "complete table fingerprint depends on unused Helm data");
        for (const Input& partial : {Input{rank, false, false, "baryons"},
                 Input{rank, true, false, "baryons,electrons_positrons"},
                 Input{rank, false, true, "baryons,photons"}}) {
            const auto path = dir / ("partial-" + std::to_string(rank) + "d-"
                + std::to_string(partial.electrons) + std::to_string(partial.photons) + ".h5");
            write_table(path, partial, helm);
            const auto source = inspect_tabular_source(path.string());
            require(source.rank == rank && !source.nuclear_equilibrium
                    && source.components.declared && source.components.needs_completion()
                    && source.components.electrons_positrons == partial.electrons
                    && source.components.photons == partial.photons,
                    "partial-table component metadata was misinterpreted");
            const std::string supplement = partial.electrons ? missing : helm_path;
            if (rank == 3) {
                Tabular3DEOS complete(path.string(), &specs, supplement);
                Tabular3DEOS explicit_total(total_path.string(), &specs, missing);
                compare(complete.get_view(), explicit_total.get_view());
            } else {
                Tabular4DEOS complete(path.string(), &specs, supplement);
                Tabular4DEOS explicit_total(total_path.string(), &specs, missing);
                compare(complete.get_view(), explicit_total.get_view());
            }
        }
    }
}

void rejection_controls(const Path& dir, const std::string& helm_path,
                        const HelmView& helm, SpeciesManager& specs)
{
    const Path path = dir / "metadata-control.h5";
    for (const char* declaration : {"baryons,unknown", "baryons,photons,photons",
             "electrons_positrons,photons", "baryons,", "baryons,,photons"}) {
        Input input;
        input.declaration = declaration;
        write_table(path, input, helm);
        rejects([&] { inspect_tabular_source(path.string()); }, "eos_components", declaration);
    }
    for (int rank : {3, 4}) {
        Input input{rank, false, false, "total"};
        input.direct = true;
        write_table(path, input, helm);
        if (rank == 3) { Tabular3DEOS valid(path.string(), &specs, "absent"); }
        else { Tabular4DEOS valid(path.string(), &specs, "absent"); }
        input.declaration = "baryons";
        write_table(path, input, helm);
        rejects([&] {
            if (rank == 3) { Tabular3DEOS invalid(path.string(), &specs, helm_path); }
            else { Tabular4DEOS invalid(path.string(), &specs, helm_path); }
        }, "free_energy", "direct fields cannot be completed independently");
    }
    Input input;
    write_table(path, input, helm);
    rejects([&] { Tabular3DEOS invalid(path.string(), &specs, (dir / "absent-helm-table.dat").string()); },
            "", "missing electron table was silently replaced");
    input.composition_axis = "species:h1";
    write_table(path, input, helm);
    rejects([&] { Tabular3DEOS invalid(path.string(), &specs, helm_path); }, "Ye",
            "a single species fraction does not define the missing electrons' Ye");
    input = {};
    input.equilibrium = 0;
    write_table(path, input, helm);
    require(!inspect_tabular_source(path.string()).nuclear_equilibrium,
            "explicit kinetic source was marked as nuclear equilibrium");
    input.equilibrium = 2;
    write_table(path, input, helm);
    rejects([&] { inspect_tabular_source(path.string()); }, "nuclear_equilibrium",
            "non-Boolean equilibrium flag");
    input.equilibrium = -1;
    write_table(path, input, helm);
    {
        HighFive::File file(path.string(), HighFive::File::ReadWrite);
        file.createDataSet("nuclear_equilibrium", 0.5);
    }
    rejects([&] { inspect_tabular_source(path.string()); }, "nuclear_equilibrium",
            "fractional equilibrium flag cannot be silently converted to integer");

    input = {3, true, true, "total"};
    write_table(path, input, helm);
    require(!inspect_tabular_source(path.string()).components.needs_completion(),
            "total alias was not recognized");
    Tabular3DEOS alias(path.string(), &specs, "absent");
    input.declaration.clear();
    write_table(path, input, helm);
    require(!inspect_tabular_source(path.string()).components.declared
            && !inspect_tabular_source(path.string()).components.needs_completion(),
            "legacy complete table was guessed to contain missing components");
    Tabular3DEOS legacy(path.string(), &specs, "absent");
}

void identity(const Path& dir, const std::string& helm_path)
{
    const auto baryons = (dir / "partial-3d-00.h5").string();
    const auto photons_only = (dir / "partial-3d-10.h5").string();
    const auto copy = dir / "helm-fingerprint-copy.dat";
    std::filesystem::copy_file(helm_path, copy, std::filesystem::copy_options::overwrite_existing);
    const auto digest = tabular_source_fingerprint(baryons, helm_path);
    require(digest != arch::core::file_sha256(baryons),
            "completed EOS identity omitted its supplement or interpretation");
    require(digest == tabular_source_fingerprint(baryons, copy.string()),
            "identical electron data changed identity when moved");
    {
        // Trailing whitespace preserves the numerical table while changing its
        // exact file identity. Only the private test copy is modified.
        std::ofstream output(copy, std::ios::app);
        require(static_cast<bool>(output), "cannot modify private fingerprint fixture");
        output << '\n';
    }
    require(digest != tabular_source_fingerprint(baryons, copy.string()),
            "completed EOS identity did not bind exact supplement bytes");
    require(tabular_source_fingerprint(photons_only, helm_path)
            == tabular_source_fingerprint(photons_only, "absent"),
            "photon-only completion depends on an unused electron table");
}

void mass_convention(const Path& dir, const std::string& helm_path,
                     const HelmView& helm, SpeciesManager& specs)
{
    // Deliberately distinguish this synthetic mass convention from both the
    // provider's convention and any particular nuclear-EOS product. At fixed
    // n_B = rho/m_B, Helm takes rho_H = n_B/N_A. Its specific F/E are per rho_H,
    // so F/E in source mass units gain rho_H/rho; physical pressure does not.
    const double mass = 1.2 / HelmView::avo;
    const double ratio = 1.0 / (mass * HelmView::avo);
    constexpr double rho = 3e5, T = 2e8, ye = 0.51, step = 2e-5;
    HelmView::ComponentThermodynamics center, plus, minus, original;
    require(helm.electron_positron_component(rho * ratio, T, ye, center)
            && helm.electron_positron_component(rho * ratio * (1 + step), T, ye, plus)
            && helm.electron_positron_component(rho * ratio * (1 - step), T, ye, minus)
            && helm.electron_positron_component(rho, T, ye, original),
            "mass-convention control lies outside Helm support");
    close(rho * ratio * (plus.free_energy - minus.free_energy) / (2 * step),
          center.pressure, 2e-6, "mass convention leaves physical electron pressure unscaled");
    require(std::abs(ratio * center.energy - original.energy)
            > 1e-4 * std::max(std::abs(original.energy), 1.0),
            "mass-convention fixture does not distinguish a missing unit conversion");
    for (int rank : {3, 4}) {
        Input full{rank, true, true, "total"};
        full.baryon_mass_g = mass;
        const auto full_path = dir / ("mass-total-" + std::to_string(rank) + "d.h5");
        write_table(full_path, full, helm);
        for (Input partial : {Input{rank, false, false, "baryons"},
                 Input{rank, true, false, "baryons,electrons_positrons"},
                 Input{rank, false, true, "baryons,photons"}}) {
            partial.baryon_mass_g = mass;
            const auto path = dir / ("mass-partial-" + std::to_string(rank) + "d-"
                + std::to_string(partial.electrons) + std::to_string(partial.photons) + ".h5");
            write_table(path, partial, helm);
            close(inspect_tabular_source(path.string()).baryon_mass_g, mass, 0.0,
                  "source baryon mass metadata changed", mass);
            const std::string supplement = partial.electrons ? "absent" : helm_path;
            if (rank == 3) {
                Tabular3DEOS completed(path.string(), &specs, supplement);
                Tabular3DEOS total(full_path.string(), &specs, "absent");
                compare(completed.get_view(), total.get_view());
            } else {
                Tabular4DEOS completed(path.string(), &specs, supplement);
                Tabular4DEOS total(full_path.string(), &specs, "absent");
                compare(completed.get_view(), total.get_view());
            }
        }
    }
    const auto invalid = dir / "invalid-mass-convention.h5";
    for (double value : {0.0, -1.0, std::numeric_limits<double>::infinity()}) {
        write_table(invalid, Input{}, helm);
        {
            HighFive::File file(invalid.string(), HighFive::File::ReadWrite);
            file.createDataSet("baryon_mass_g", value);
        }
        rejects([&] { inspect_tabular_source(invalid.string()); }, "baryon_mass_g",
                "declared baryon mass must be finite and strictly positive");
    }
}

void coupling_controls()
{
    // Composition-resolved tables may drive kinetic burning without already
    // containing equilibrium binding. Neither that distinction nor an explicit
    // total declaration supplies the missing electron transport diagnostics.
    for (bool equilibrium : {false, true}) {
        for (bool complete : {false, true}) {
            TabularSourceInfo source;
            source.nuclear_equilibrium = equilibrium;
            source.components = {true, complete, complete};
            SimConfig config;
            config.physics.burn.use_burn = false;
            config.physics.diffusion.use_diffusion = false;
            EOSDispatcher::validate_coupling(config, source, false);
            config.physics.burn.use_burn = true;
            if (equilibrium)
                rejects([&] { EOSDispatcher::validate_coupling(config, source, false); },
                    "double-count", "equilibrium binding plus kinetic burning was accepted");
            else EOSDispatcher::validate_coupling(config, source, false);
            config.physics.burn.use_burn = false;
            rejects([&] { EOSDispatcher::validate_coupling(config, source, true); },
                "Steger-Warming", "declared table accepted a composition-only gamma flux");
            auto& diffusion = config.physics.diffusion;
            diffusion.use_diffusion = diffusion.use_thermal_diffusion = true;
            for (double alpha : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN()}) {
                diffusion.alpha_therm = alpha;
                rejects([&] { EOSDispatcher::validate_coupling(config, source, false); },
                    "electron diagnostics", "missing conductivity diagnostics silently became zero transport");
            }
            diffusion.alpha_therm = 1.0;
            EOSDispatcher::validate_coupling(config, source, false);
            diffusion.alpha_therm = 0.0;
            diffusion.use_thermal_diffusion = false;
            EOSDispatcher::validate_coupling(config, source, false);
        }
    }
    TabularSourceInfo legacy;
    SimConfig config;
    config.physics.burn.use_burn = true;
    config.physics.diffusion.use_diffusion = true;
    config.physics.diffusion.use_thermal_diffusion = true;
    config.physics.diffusion.alpha_therm = 0.0;
    EOSDispatcher::validate_coupling(config, legacy, true);
    legacy.nuclear_equilibrium = true;
    rejects([&] { EOSDispatcher::validate_coupling(config, legacy, false); },
        "double-count", "equilibrium-only metadata failed to constrain kinetic burning");
    config.physics.burn.use_burn = false;
    rejects([&] { EOSDispatcher::validate_coupling(config, legacy, false); },
        "electron diagnostics", "equilibrium-only metadata failed to constrain conductivity");
}
} // namespace

int main(int argc, char** argv)
{
    try {
        require(argc == 3, "expected output directory and canonical helm_table.dat path");
        const Path directory = argv[1];
        std::filesystem::create_directories(directory);
        auto specs = species();
        HelmEos helm(argv[2], &specs);
        equivalence(directory, argv[2], helm.get_view(), specs);
        rejection_controls(directory, argv[2], helm.get_view(), specs);
        identity(directory, argv[2]);
        mass_convention(directory, argv[2], helm.get_view(), specs);
        coupling_controls();
        std::cout << "3D/4D missing-component assembly, potential closure, metadata rejection "
                     "and supplement identity/coupling PASS (manufactured contract checks, not Shen accuracy)\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
