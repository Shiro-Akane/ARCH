#pragma once
#include <string>
#include <vector>
#include <cassert>
#include <type_traits>
#include <cmath>

#include "../../data/GlobalDefs.h"
#include "../../core/RuntimeParams.h"
#include "../../physics/species/Species.h"
#include "../../numerics/linalg/DenseWrap.h"
#include "../../numerics/linalg/SparseWrap.h"

struct NetIso7 {
    static constexpr int NUM_SPECIES = 6;
    static constexpr int ODE_NEQ = NUM_SPECIES + 1; // +1 for Temperature

    static std::string get_network_name() { return "iso7"; }

    static void RegisterSpecies(SpeciesManager& specs);
    static void SetupInitialFractions(SimConfig& config, const SpeciesManager& specs, std::vector<double>& X_out);
    static void eval_rhs(const double *Y, double rho, double *RHS, double &enuc);
    static void eval_jacobian(const double *Y, double rho, DenseMatrixData &J_dense);
    static void eval_jacobian(const double *Y, double rho, SparseMatrixData &J_sparse);
};
