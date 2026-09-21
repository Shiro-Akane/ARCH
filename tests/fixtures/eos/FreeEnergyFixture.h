#pragma once

#include "physics/eos/tabular/Tabular3DEOS.h"
#include "physics/eos/tabular/Tabular4DEOS.h"
#include <array>
#include <cmath>
#include <type_traits>
#include <vector>

namespace arch::test {
// Explicit, immutable composition for leaf failure probes that do not own a
// hydrodynamic species field. This is test input, not a production fallback.
struct FixedTableComposition {
    int count = 1;
    ARCH_INLINE int size() const { return 1; }
    ARCH_INLINE double get_A(int) const { return 14.; }
    ARCH_INLINE double get_Z(int) const { return 7.; }
    ARCH_INLINE double calc_Abar(const double*) const { return 14.; }
    ARCH_INLINE double calc_Zbar(const double*) const { return 7.; }
    ARCH_INLINE double calc_Ye(const double*) const { return .5; }
};

// Exact polynomial potential in ln(rho), ln(T). Positive heat capacity and
// pressure throughout the declared box; composition is deliberately passive.
// Owning fields and mask outlive every borrowed host/device upload descriptor.
template<bool FourDimensional>
struct FreeEnergyFixture {
    using Host = std::conditional_t<FourDimensional, Tabular4DEOSHostView, Tabular3DEOSHostView>;
    Host host{};
    std::array<std::vector<double>, tabular_eos::FieldCount> fields;
    std::vector<double> valid;

    explicit FreeEnergyFixture(const SpeciesManager& species)
    {
        host.n_rho = host.n_T = 3;
        host.log_rho_min = host.log_T_min = 0.0;
        host.log_rho_max = host.log_T_max = .4;
        host.dlog_rho = host.dlog_T = .2;
        host.specs = species.get_host_view();
        if constexpr (FourDimensional) {
            host.n_A = host.n_Z = 3;
            host.A_min = 1.; host.A_max = 4.; host.dA = 1.5;
            host.Z_min = .5; host.Z_max = 2.; host.dZ = .75;
        } else {
            host.n_X = 3; host.X_min = 0.; host.X_max = 1.; host.dX = .5;
            host.target_species_id = 0;
        }
        constexpr int composition_count = FourDimensional ? 9 : 3;
        constexpr int cells = 9 * composition_count;
        valid.assign(cells, 1.0);
        host.table_valid = valid.data(); host.valid_extent = valid.size();
        for (auto& field : fields) field.resize(cells);
        for (int ir = 0; ir < 3; ++ir) for (int it = 0; it < 3; ++it) {
            const double r = std::log(10.) * .2 * ir, t = std::log(10.) * .2 * it;
            const double jet[]{100. + 2*r + r*t - 1.5*t*t,
                2. + t, r - 3*t, 0., 1., -3., 0., 0., 0.};
            for (int c = 0; c < composition_count; ++c)
                for (int field = 0; field < tabular_eos::FieldCount; ++field)
                    fields[field][(ir*3 + it)*composition_count + c] = jet[field];
        }
        for (int field = 0; field < tabular_eos::FieldCount; ++field) {
            host.free_energy_fields[field] = fields[field].data();
            host.free_energy_extents[field] = fields[field].size();
        }
    }
    FreeEnergyFixture(const FreeEnergyFixture&) = delete;
    FreeEnergyFixture& operator=(const FreeEnergyFixture&) = delete;
    static double pressure(double rho, double temperature)
    { return rho * (2. + std::log(temperature)); }
    static double energy(double rho, double temperature)
    { const double r=std::log(rho),t=std::log(temperature); return 100.+r+r*t+3*t-1.5*t*t; }
    static double cv(double rho, double temperature)
    { return (std::log(rho)-3*std::log(temperature)+3.)/temperature; }
};
} // namespace arch::test
