/** Manufactured conservative chain for sparse execution tests, not nuclear data. */
#pragma once
#include "core/ArchPortability.h"

template <int Species>
struct SparseTransferNetwork
{
    static_assert(Species >= 2);
    static constexpr int NUM_SPECIES = Species, ODE_NEQ = Species + 1;
    static constexpr bool SUPPORTS_NSE = false;
    static constexpr double ENERGY_CONVERSION = 0.0;
    ARCH_HOST_DEVICE static double aion(int) { return 1.0; }
    ARCH_HOST_DEVICE static double energy_weight(int) { return 0.0; }
    ARCH_HOST_DEVICE static void eval_rhs(
        const double* x, double, double, double* rhs, double& enuc)
    {
        for (int species = 0; species < Species; ++species) {
            const double incoming = species == 0 ? 0.0 : 0.02 * x[species - 1];
            const double outgoing = species == Species - 1 ? 0.0 : 0.02 * x[species];
            rhs[species] = incoming - outgoing;
        }
        enuc = 0.0;
    }
    template <class Matrix>
    ARCH_HOST_DEVICE static void eval_jacobian(
        const double*, double, double, Matrix& matrix, double* energy)
    {
        for (int species = 0; species < Species; ++species) {
            if (species < Species - 1) matrix.set(species + 1, species + 1, -0.02);
            if (species > 0) matrix.set(species + 1, species, 0.02);
            if (energy != nullptr) energy[species] = 0.0;
        }
    }
    ARCH_HOST_DEVICE static void eval_temperature_derivative(
        const double*, double, double, double* rhs, double& energy)
    {
        for (int species = 0; species < Species; ++species) rhs[species] = 0.0;
        energy = 0.0;
    }
};

/** A conservative growing mode whose first BE matrix is exactly singular when
 * h=1/1024: row zero of I-hJ is identically zero but its RHS is nonzero. The
 * adaptive solver must reject it, reduce h, and continue. Other species are
 * inert. Binary-exact h and rate make this a deterministic failure fixture.
 */
template <int Species>
struct SparseSingularRetryNetwork : SparseTransferNetwork<Species>
{
    static constexpr double RATE = 1024.0;
    ARCH_HOST_DEVICE static void eval_rhs(
        const double* x, double, double, double* rhs, double& enuc)
    {
        for (int species = 0; species < Species; ++species) rhs[species] = 0.0;
        rhs[0] = RATE * x[0];
        rhs[1] = -rhs[0];
        enuc = 0.0;
    }
    template <class Matrix>
    ARCH_HOST_DEVICE static void eval_jacobian(
        const double*, double, double, Matrix& matrix, double* energy)
    {
        matrix.set(1, 1, RATE);
        matrix.set(2, 1, -RATE);
        if (energy != nullptr)
            for (int species = 0; species < Species; ++species) energy[species] = 0.0;
    }
};

struct SparseTransferEos
{
    ARCH_HOST_DEVICE double get_eta(double, double, const double*) const { return 0.0; }
    ARCH_HOST_DEVICE double get_cv(double, double, const double*) const { return 1.0; }
    ARCH_HOST_DEVICE double get_temperature(double, double e, const double*) const { return e; }
    ARCH_HOST_DEVICE double get_eint_from_T(double, double t, const double*) const { return t; }
};
