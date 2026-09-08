/**
 * @file eos_state.h
 * @brief Thermodynamic query inputs and outputs shared with transport evaluation.
 *
 * Workflow:
 * 1. Fluid solver populates the inputs (rho, T, Xi).
 * 2. EOS computes the thermodynamic properties and fills the outputs.
 * 3. Transport evaluation consumes the additional electron quantities when supplied.
 *
 * E is specific internal energy, unlike FluidVector::eng (total energy per
 * volume). Quantities use the selected EOS's consistent unit system; the
 * Helmholtz and tabular thermodynamic policies use cgs units.
 */

#pragma once

struct eos_state_t {
    // 1. Inputs (State)
    double rho = 0.0;
    double T = 0.0;
    const double* Xi = nullptr;

    // 2. Basic Outputs (Filled by all EOS types)
    double P = 0.0;
    double E = 0.0;
    double cv = 0.0;
    double sound_speed = 0.0;
    double dp_drho = 0.0;
    double dp_dT = 0.0;

    // 3. Electron Outputs (Filled when supported, e.g. by HelmEos)
    double pele = 0.0;
    double xne = 0.0;
    double eta = 0.0;
};
