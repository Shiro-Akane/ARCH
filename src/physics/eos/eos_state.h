/**
 * @file eos_state.h
 * @brief Universal thermodynamic and compositional state struct for fluid nodes.
 *
 * Workflow:
 * 1. Fluid solver populates the inputs (rho, T, Xi).
 * 2. EOS computes the thermodynamic properties and fills the outputs.
 * 3. Deep physical outputs are extracted for diffusion coefficients computation.
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

    // 3. Deep Physical Outputs (Filled by complex EOS e.g. HelmEos)
    double pele = 0.0;
    double xne = 0.0;
    double eta = 0.0;
};
