#include "NetIso7.h"
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>

namespace pyna_iso7 {
    #include "amrex_bridge.H"
    #include "network_properties.H"
    #include "burn_type.H"
    #include "actual_network.H"
    #include "actual_rhs.H"
}

void NetIso7::RegisterSpecies(SpeciesManager& specs)
{
    for (int i = 0; i < NUM_SPECIES; ++i)
    {
        specs.add_species(pyna_iso7::spec_names[i], pyna_iso7::aion[i], pyna_iso7::zion[i], 1.6667, 0.0);
    }
}

void NetIso7::SetupInitialFractions(SimConfig& config, const SpeciesManager& specs, std::vector<double>& X_out)
{
    X_out.assign(specs.count(), 1e-20);
    double sum_X = 0.0;
    for (int i = 0; i < specs.count(); ++i)
    {
        std::string name = specs.get_name(i);
        std::string target = "x" + name;
        std::transform(target.begin(), target.end(), target.begin(), ::tolower);
        double val = 0.0;
        for (const auto& kv : config.custom_params) {
            std::string k_lower = kv.first;
            std::transform(k_lower.begin(), k_lower.end(), k_lower.begin(), ::tolower);
            if (k_lower == target) {
                val = kv.second;
                break;
            }
        }
        X_out[i] += val;
        sum_X += X_out[i];
    }
    if (sum_X > 0.0)
    {
        for (int i = 0; i < specs.count(); ++i)
        {
            X_out[i] /= sum_X;
        }
    }
}

void NetIso7::eval_rhs(const double *Y, double rho, double *RHS, double &enuc)
{
    pyna_iso7::burn_t state;
    state.rho = rho;
    state.T = Y[ODE_NEQ - 1];

    for (int i = 0; i < NUM_SPECIES; ++i)
    {
        state.xn[i] = Y[i];
    }

    pyna_iso7::compute_ye(state);
    pyna_iso7::Array1D<pyna_iso7::Real, 1, NUM_SPECIES> ydot_arr;
    pyna_iso7::Real enu_weak = 0.0;

    pyna_iso7::actual_rhs(state, ydot_arr, enu_weak);

    for (int i = 0; i < NUM_SPECIES; ++i)
    {
        RHS[i] = ydot_arr(i + 1) * pyna_iso7::aion[i];
    }

    pyna_iso7::Real enuc_local = 0.0;
    pyna_iso7::ener_gener_rate(ydot_arr, enuc_local);
    enuc = static_cast<double>(enuc_local);
}

void NetIso7::eval_jacobian(const double *Y, double rho, DenseMatrixData &J_dense)
{
    pyna_iso7::burn_t state;
    state.rho = rho;
    state.T = Y[ODE_NEQ - 1];

    for (int i = 0; i < NUM_SPECIES; ++i)
        state.xn[i] = Y[i];
    pyna_iso7::compute_ye(state);

    pyna_iso7::actual_jac(state, J_dense);

    for (int i = 0; i < NUM_SPECIES; ++i) {
        for (int j = 0; j < NUM_SPECIES; ++j) {
            double val = J_dense(i + 1, j + 1);
            val = val * (pyna_iso7::aion[i] / pyna_iso7::aion[j]);
            J_dense.set(i + 1, j + 1, val);
        }
    }
}

void NetIso7::eval_jacobian(const double *Y, double rho, SparseMatrixData &J_sparse)
{
    pyna_iso7::burn_t state;
    state.rho = rho;
    state.T = Y[ODE_NEQ - 1];

    for (int i = 0; i < NUM_SPECIES; ++i)
        state.xn[i] = Y[i];
    pyna_iso7::compute_ye(state);

    pyna_iso7::actual_jac(state, J_sparse);

    for (int k = 0; k < J_sparse.nnz; ++k) {
        int i = J_sparse.rows[k];
        int j = J_sparse.cols[k];
        J_sparse.values[k] *= (pyna_iso7::aion[i] / pyna_iso7::aion[j]);
    }
}
