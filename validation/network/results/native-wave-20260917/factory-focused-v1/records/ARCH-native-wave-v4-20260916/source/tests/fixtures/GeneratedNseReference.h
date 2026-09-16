#pragma once

// Independent analytic detailed-balance roots, rounded from 80-digit arithmetic.
// Constants and nuclear data are the literal binary64 inputs in GeneratedNseCases.
// For n+p <-> d at Ye=1/2: Xn=Xp=x, Xd=1-2x, Xd=K*x*x,
// so x=1/(1+sqrt(1+K)). K=Cd/(Cn*Cp), where
// Ci=(A_nuc_i*mu)^2.5*g_i/rho * (k*T/(2*pi*hbar^2))^1.5 * exp(Bi/(kMeV*T)).
// At T=5e9, rho=1e7, K=0.065613636059246346653039901163980288243093001464892.
// Doubling the constant deuteron partition function doubles K.
// For d+d <-> He4: Xd+XHe=1, XHe=K*Xd^2, hence
// Xd=2/(1+sqrt(1+4*K)); at T=1e10, rho=1e9, K=358986870.2867991820154.
// These are closed-form reaction/conservation roots, not NSESolver output.
namespace GeneratedNseReference {
inline constexpr double neutron_proton = 0x1.f7ddbdf4bb6fcp-2;
inline constexpr double deuteron = 0x1.044841689207ep-6;
inline constexpr double partition_neutron_proton = 0x1.f038e1ea835fbp-2;
inline constexpr double alpha_deuteron = 0x1.babaef0577519p-15;
inline constexpr double alpha_helium = 0x1.fff9151443ea2p-1;
} // namespace GeneratedNseReference
