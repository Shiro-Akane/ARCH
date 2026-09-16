// Mechanically generated from the Timmes-derived TimmesRhs.inc.
// Upstream index: https://cococubed.com/code_pages/burn.shtml.
// All rate[] entries are held fixed in this partial Jacobian.
// Keep the large sparse expression out of its caller's register set.
#if defined(__CUDACC__)
#  define TIMMES_JACOBIAN_NOINLINE __noinline__
#elif defined(__GNUC__) || defined(__clang__)
#  define TIMMES_JACOBIAN_NOINLINE __attribute__((noinline))
#else
#  define TIMMES_JACOBIAN_NOINLINE
#endif
TIMMES_HD TIMMES_JACOBIAN_NOINLINE inline void jacobian_aprox19_molar_fixed_rates(
    const double* y, const double* rate, double* jac)
{
    for (int i = 0; i < 19 * 19; ++i) jac[i] = 0.0;
    jac[(ih1) * 19 + (ih1)] = ((-3.0 * rate[irpp]) * y[ih1]) + ((-2.0 * rate[ircpg]) * y[ic12]) + ((-2.0 * rate[irnpg]) * y[in14]) + ((-2.0 * rate[iropg]) * y[io16]) + ((-3.0 * rate[irpen]));
    jac[(ih1) * 19 + (ihe3)] = ((2.0 * rate[ir33]) * y[ihe3]) + ((-1.0 * rate[irhe3ag]) * y[ihe4]);
    jac[(ih1) * 19 + (ihe4)] = ((-1.0 * rate[irhe3ag]) * y[ihe3]);
    jac[(ih1) * 19 + (ic12)] = ((-2.0 * rate[ircpg]) * y[ih1]);
    jac[(ih1) * 19 + (in14)] = ((-2.0 * rate[irnpg]) * y[ih1]);
    jac[(ih1) * 19 + (io16)] = ((-2.0 * rate[iropg]) * y[ih1]);
    jac[(ihe3) * 19 + (ih1)] = (rate[irpp] * y[ih1]) + (rate[irpen]);
    jac[(ihe3) * 19 + (ihe3)] = ((-2.0 * rate[ir33]) * y[ihe3]) + ((-1.0 * rate[irhe3ag]) * y[ihe4]);
    jac[(ihe3) * 19 + (ihe4)] = ((-1.0 * rate[irhe3ag]) * y[ihe3]);
    jac[(ihe4) * 19 + (ih1)] = (rate[iropg] * y[io16]) + ((rate[ifa] * rate[irnpg]) * y[in14]);
    jac[(ihe4) * 19 + (ihe3)] = (rate[ir33] * y[ihe3]) + (rate[irhe3ag] * y[ihe4]);
    jac[(ihe4) * 19 + (ihe4)] = ((-1.5 * rate[ir3a]) * y[ihe4] * y[ihe4]) + ((-1.0 * rate[ircag]) * y[ic12]) + ((-1.0 * rate[iroag]) * y[io16]) + ((-1.0 * rate[irneag]) * y[ine20]) + (((-1.0 * rate[irmgag]) + (-1.0 * rate[irmgap] * ((-1.0 * rate[irr1]) + 1.0))) * y[img24]) + (((-1.0 * rate[irsiag]) + (-1.0 * rate[irsiap] * ((-1.0 * rate[irs1]) + 1.0))) * y[isi28]) + (((-1.0 * rate[irsag]) + (-1.0 * rate[irsap] * ((-1.0 * rate[irt1]) + 1.0))) * y[is32]) + (((-1.0 * rate[irarag]) + (-1.0 * rate[irarap] * ((-1.0 * rate[iru1]) + 1.0))) * y[iar36]) + (((-1.0 * rate[ircaag]) + (-1.0 * rate[ircaap] * ((-1.0 * rate[irv1]) + 1.0))) * y[ica40]) + (((-1.0 * rate[irtiag]) + (-1.0 * rate[irtiap] * ((-1.0 * rate[irw1]) + 1.0))) * y[iti44]) + (((-1.0 * rate[ircrag]) + (-1.0 * rate[ircrap] * ((-1.0 * rate[irx1]) + 1.0))) * y[icr48]) + (((-1.0 * rate[irfeag]) + (-1.0 * rate[ir6f54])) * y[ife52]) + ((-1.0 * rate[ir7f54]) * y[ife52] * y[iprot]) + ((-1.0 * rate[iralf1])) + (rate[irhe3ag] * y[ihe3]) + ((-1.5 * rate[irnag]) * y[in14]);
    jac[(ihe4) * 19 + (ic12)] = (rate[ir1212] * y[ic12]) + ((0.5 * rate[ir1216]) * y[io16]) + ((3.0 * rate[irg3a])) + ((-1.0 * rate[ircag]) * y[ihe4]);
    jac[(ihe4) * 19 + (in14)] = ((-1.5 * rate[irnag]) * y[ihe4]) + ((rate[ifa] * rate[irnpg]) * y[ih1]);
    jac[(ihe4) * 19 + (io16)] = ((0.5 * rate[ir1216]) * y[ic12]) + ((2.0 * ((0.28000000000000003 * rate[ir1616]) + (0.17000000000000001 * rate[irs1] * rate[ir1616]))) * y[io16]) + (rate[iroga]) + ((-1.0 * rate[iroag]) * y[ihe4]) + (rate[iropg] * y[ih1]);
    jac[(ihe4) * 19 + (ine20)] = (rate[irnega]) + ((-1.0 * rate[irneag]) * y[ihe4]);
    jac[(ihe4) * 19 + (img24)] = (rate[irmgga]) + (((-1.0 * rate[irmgag]) + (-1.0 * rate[irmgap] * ((-1.0 * rate[irr1]) + 1.0))) * y[ihe4]);
    jac[(ihe4) * 19 + (isi28)] = ((rate[irsiga] + (rate[irsigp] * rate[irr1]))) + (((-1.0 * rate[irsiag]) + (-1.0 * rate[irsiap] * ((-1.0 * rate[irs1]) + 1.0))) * y[ihe4]);
    jac[(ihe4) * 19 + (is32)] = ((rate[irsga] + (rate[irsgp] * rate[irs1]))) + (((-1.0 * rate[irsag]) + (-1.0 * rate[irsap] * ((-1.0 * rate[irt1]) + 1.0))) * y[ihe4]);
    jac[(ihe4) * 19 + (iar36)] = ((rate[irarga] + (rate[irargp] * rate[irt1]))) + (((-1.0 * rate[irarag]) + (-1.0 * rate[irarap] * ((-1.0 * rate[iru1]) + 1.0))) * y[ihe4]);
    jac[(ihe4) * 19 + (ica40)] = ((rate[ircaga] + (rate[ircagp] * rate[iru1]))) + (((-1.0 * rate[ircaag]) + (-1.0 * rate[ircaap] * ((-1.0 * rate[irv1]) + 1.0))) * y[ihe4]);
    jac[(ihe4) * 19 + (iti44)] = ((rate[irtiga] + (rate[irtigp] * rate[irv1]))) + (((-1.0 * rate[irtiag]) + (-1.0 * rate[irtiap] * ((-1.0 * rate[irw1]) + 1.0))) * y[ihe4]);
    jac[(ihe4) * 19 + (icr48)] = ((rate[ircrga] + (rate[ircrgp] * rate[irw1]))) + (((-1.0 * rate[ircrag]) + (-1.0 * rate[ircrap] * ((-1.0 * rate[irx1]) + 1.0))) * y[ihe4]);
    jac[(ihe4) * 19 + (ife52)] = ((rate[irfega] + (rate[irfegp] * rate[irx1]))) + (((-1.0 * rate[irfeag]) + (-1.0 * rate[ir6f54])) * y[ihe4]) + ((-1.0 * rate[ir7f54]) * y[ihe4] * y[iprot]);
    jac[(ihe4) * 19 + (ife54)] = (rate[ir5f54] * y[iprot] * y[iprot]);
    jac[(ihe4) * 19 + (ini56)] = (rate[irniga]) + (rate[ir8f54] * y[iprot]);
    jac[(ihe4) * 19 + (ineut)] = ((2.0 * rate[iralf2]) * y[ineut] * y[iprot] * y[iprot]);
    jac[(ihe4) * 19 + (iprot)] = ((2.0 * rate[ir5f54]) * y[ife54] * y[iprot]) + ((-1.0 * rate[ir7f54]) * y[ife52] * y[ihe4]) + (rate[ir8f54] * y[ini56]) + ((2.0 * rate[iralf2]) * y[ineut] * y[ineut] * y[iprot]);
    jac[(ic12) * 19 + (ih1)] = ((-1.0 * rate[ircpg]) * y[ic12]) + ((rate[ifa] * rate[irnpg]) * y[in14]);
    jac[(ic12) * 19 + (ihe4)] = ((3.0 * sixth * rate[ir3a]) * y[ihe4] * y[ihe4]) + ((-1.0 * rate[ircag]) * y[ic12]);
    jac[(ic12) * 19 + (ic12)] = ((-2.0 * rate[ir1212]) * y[ic12]) + ((-1.0 * rate[ir1216]) * y[io16]) + ((-1.0 * rate[irg3a])) + ((-1.0 * rate[ircag]) * y[ihe4]) + ((-1.0 * rate[ircpg]) * y[ih1]);
    jac[(ic12) * 19 + (in14)] = ((rate[ifa] * rate[irnpg]) * y[ih1]);
    jac[(ic12) * 19 + (io16)] = ((-1.0 * rate[ir1216]) * y[ic12]) + (rate[iroga]);
    jac[(in14) * 19 + (ih1)] = (rate[ircpg] * y[ic12]) + ((-1.0 * rate[irnpg]) * y[in14]) + (rate[iropg] * y[io16]);
    jac[(in14) * 19 + (ihe4)] = ((-1.0 * rate[irnag]) * y[in14]);
    jac[(in14) * 19 + (ic12)] = (rate[ircpg] * y[ih1]);
    jac[(in14) * 19 + (in14)] = ((-1.0 * rate[irnpg]) * y[ih1]) + ((-1.0 * rate[irnag]) * y[ihe4]);
    jac[(in14) * 19 + (io16)] = (rate[iropg] * y[ih1]);
    jac[(io16) * 19 + (ih1)] = ((-1.0 * rate[iropg]) * y[io16]) + ((rate[ifg] * rate[irnpg]) * y[in14]);
    jac[(io16) * 19 + (ihe4)] = (rate[ircag] * y[ic12]) + ((-1.0 * rate[iroag]) * y[io16]);
    jac[(io16) * 19 + (ic12)] = ((-1.0 * rate[ir1216]) * y[io16]) + (rate[ircag] * y[ihe4]);
    jac[(io16) * 19 + (in14)] = ((rate[ifg] * rate[irnpg]) * y[ih1]);
    jac[(io16) * 19 + (io16)] = ((-1.0 * rate[ir1216]) * y[ic12]) + ((-2.0 * rate[ir1616]) * y[io16]) + ((-1.0 * rate[iroag]) * y[ihe4]) + ((-1.0 * rate[iroga])) + ((-1.0 * rate[iropg]) * y[ih1]);
    jac[(io16) * 19 + (ine20)] = (rate[irnega]);
    jac[(ine20) * 19 + (ihe4)] = (rate[iroag] * y[io16]) + ((-1.0 * rate[irneag]) * y[ine20]) + (rate[irnag] * y[in14]);
    jac[(ine20) * 19 + (ic12)] = (rate[ir1212] * y[ic12]);
    jac[(ine20) * 19 + (in14)] = (rate[irnag] * y[ihe4]);
    jac[(ine20) * 19 + (io16)] = (rate[iroag] * y[ihe4]);
    jac[(ine20) * 19 + (ine20)] = ((-1.0 * rate[irneag]) * y[ihe4]) + ((-1.0 * rate[irnega]));
    jac[(ine20) * 19 + (img24)] = (rate[irmgga]);
    jac[(img24) * 19 + (ihe4)] = (rate[irneag] * y[ine20]) + (((-1.0 * rate[irmgag]) + (-1.0 * rate[irmgap] * ((-1.0 * rate[irr1]) + 1.0))) * y[img24]);
    jac[(img24) * 19 + (ic12)] = ((0.5 * rate[ir1216]) * y[io16]);
    jac[(img24) * 19 + (io16)] = ((0.5 * rate[ir1216]) * y[ic12]);
    jac[(img24) * 19 + (ine20)] = (rate[irneag] * y[ihe4]);
    jac[(img24) * 19 + (img24)] = (((-1.0 * rate[irmgag]) + (-1.0 * rate[irmgap] * ((-1.0 * rate[irr1]) + 1.0))) * y[ihe4]) + ((-1.0 * rate[irmgga]));
    jac[(img24) * 19 + (isi28)] = ((rate[irsiga] + (rate[irr1] * rate[irsigp])));
    jac[(isi28) * 19 + (ihe4)] = ((rate[irmgag] + (rate[irmgap] * ((-1.0 * rate[irr1]) + 1.0))) * y[img24]) + (((-1.0 * rate[irsiag]) + (-1.0 * rate[irsiap] * ((-1.0 * rate[irs1]) + 1.0))) * y[isi28]);
    jac[(isi28) * 19 + (ic12)] = ((0.5 * rate[ir1216]) * y[io16]);
    jac[(isi28) * 19 + (io16)] = ((0.5 * rate[ir1216]) * y[ic12]) + ((2.0 * ((0.28000000000000003 * rate[ir1616]) + (0.17000000000000001 * rate[irs1] * rate[ir1616]))) * y[io16]);
    jac[(isi28) * 19 + (img24)] = ((rate[irmgag] + (rate[irmgap] * ((-1.0 * rate[irr1]) + 1.0))) * y[ihe4]);
    jac[(isi28) * 19 + (isi28)] = (((-1.0 * rate[irsiag]) + (-1.0 * rate[irsiap] * ((-1.0 * rate[irs1]) + 1.0))) * y[ihe4]) + (((-1.0 * rate[irsiga]) + (-1.0 * rate[irr1] * rate[irsigp])));
    jac[(isi28) * 19 + (is32)] = ((rate[irsga] + (rate[irs1] * rate[irsgp])));
    jac[(is32) * 19 + (ihe4)] = ((rate[irsiag] + (rate[irsiap] * ((-1.0 * rate[irs1]) + 1.0))) * y[isi28]) + (((-1.0 * rate[irsag]) + (-1.0 * rate[irsap] * ((-1.0 * rate[irt1]) + 1.0))) * y[is32]);
    jac[(is32) * 19 + (io16)] = ((2.0 * ((0.050000000000000003 * rate[ir1616]) + (0.17000000000000001 * rate[ir1616] * ((-1.0 * rate[irs1]) + 1.0)))) * y[io16]);
    jac[(is32) * 19 + (isi28)] = ((rate[irsiag] + (rate[irsiap] * ((-1.0 * rate[irs1]) + 1.0))) * y[ihe4]);
    jac[(is32) * 19 + (is32)] = (((-1.0 * rate[irsag]) + (-1.0 * rate[irsap] * ((-1.0 * rate[irt1]) + 1.0))) * y[ihe4]) + (((-1.0 * rate[irsga]) + (-1.0 * rate[irs1] * rate[irsgp])));
    jac[(is32) * 19 + (iar36)] = ((rate[irarga] + (rate[irt1] * rate[irargp])));
    jac[(iar36) * 19 + (ihe4)] = ((rate[irsag] + (rate[irsap] * ((-1.0 * rate[irt1]) + 1.0))) * y[is32]) + (((-1.0 * rate[irarag]) + (-1.0 * rate[irarap] * ((-1.0 * rate[iru1]) + 1.0))) * y[iar36]);
    jac[(iar36) * 19 + (is32)] = ((rate[irsag] + (rate[irsap] * ((-1.0 * rate[irt1]) + 1.0))) * y[ihe4]);
    jac[(iar36) * 19 + (iar36)] = (((-1.0 * rate[irarag]) + (-1.0 * rate[irarap] * ((-1.0 * rate[iru1]) + 1.0))) * y[ihe4]) + (((-1.0 * rate[irarga]) + (-1.0 * rate[irt1] * rate[irargp])));
    jac[(iar36) * 19 + (ica40)] = ((rate[ircaga] + (rate[ircagp] * rate[iru1])));
    jac[(ica40) * 19 + (ihe4)] = ((rate[irarag] + (rate[irarap] * ((-1.0 * rate[iru1]) + 1.0))) * y[iar36]) + (((-1.0 * rate[ircaag]) + (-1.0 * rate[ircaap] * ((-1.0 * rate[irv1]) + 1.0))) * y[ica40]);
    jac[(ica40) * 19 + (iar36)] = ((rate[irarag] + (rate[irarap] * ((-1.0 * rate[iru1]) + 1.0))) * y[ihe4]);
    jac[(ica40) * 19 + (ica40)] = (((-1.0 * rate[ircaag]) + (-1.0 * rate[ircaap] * ((-1.0 * rate[irv1]) + 1.0))) * y[ihe4]) + (((-1.0 * rate[ircaga]) + (-1.0 * rate[ircagp] * rate[iru1])));
    jac[(ica40) * 19 + (iti44)] = ((rate[irtiga] + (rate[irtigp] * rate[irv1])));
    jac[(iti44) * 19 + (ihe4)] = ((rate[ircaag] + (rate[ircaap] * ((-1.0 * rate[irv1]) + 1.0))) * y[ica40]) + (((-1.0 * rate[irtiag]) + (-1.0 * rate[irtiap] * ((-1.0 * rate[irw1]) + 1.0))) * y[iti44]);
    jac[(iti44) * 19 + (ica40)] = ((rate[ircaag] + (rate[ircaap] * ((-1.0 * rate[irv1]) + 1.0))) * y[ihe4]);
    jac[(iti44) * 19 + (iti44)] = (((-1.0 * rate[irtiag]) + (-1.0 * rate[irtiap] * ((-1.0 * rate[irw1]) + 1.0))) * y[ihe4]) + (((-1.0 * rate[irtiga]) + (-1.0 * rate[irv1] * rate[irtigp])));
    jac[(iti44) * 19 + (icr48)] = ((rate[ircrga] + (rate[irw1] * rate[ircrgp])));
    jac[(icr48) * 19 + (ihe4)] = ((rate[irtiag] + (rate[irtiap] * ((-1.0 * rate[irw1]) + 1.0))) * y[iti44]) + (((-1.0 * rate[ircrag]) + (-1.0 * rate[ircrap] * ((-1.0 * rate[irx1]) + 1.0))) * y[icr48]);
    jac[(icr48) * 19 + (iti44)] = ((rate[irtiag] + (rate[irtiap] * ((-1.0 * rate[irw1]) + 1.0))) * y[ihe4]);
    jac[(icr48) * 19 + (icr48)] = (((-1.0 * rate[ircrag]) + (-1.0 * rate[ircrap] * ((-1.0 * rate[irx1]) + 1.0))) * y[ihe4]) + (((-1.0 * rate[ircrga]) + (-1.0 * rate[irw1] * rate[ircrgp])));
    jac[(icr48) * 19 + (ife52)] = ((rate[irfega] + (rate[irx1] * rate[irfegp])));
    jac[(ife52) * 19 + (ihe4)] = ((rate[ircrag] + (rate[ircrap] * ((-1.0 * rate[irx1]) + 1.0))) * y[icr48]) + (((-1.0 * rate[irfeag]) + (-1.0 * rate[ir6f54])) * y[ife52]) + ((-1.0 * rate[ir7f54]) * y[ife52] * y[iprot]);
    jac[(ife52) * 19 + (icr48)] = ((rate[ircrag] + (rate[ircrap] * ((-1.0 * rate[irx1]) + 1.0))) * y[ihe4]);
    jac[(ife52) * 19 + (ife52)] = (((-1.0 * rate[irfeag]) + (-1.0 * rate[ir6f54])) * y[ihe4]) + (((-1.0 * rate[irfega]) + (-1.0 * rate[irx1] * rate[irfegp]))) + ((-1.0 * rate[ir2f54]) * y[ineut] * y[ineut]) + ((-1.0 * rate[ir7f54]) * y[ihe4] * y[iprot]);
    jac[(ife52) * 19 + (ife54)] = (rate[ir1f54]) + (rate[ir5f54] * y[iprot] * y[iprot]);
    jac[(ife52) * 19 + (ini56)] = (rate[irniga]) + (rate[ir8f54] * y[iprot]);
    jac[(ife52) * 19 + (ineut)] = ((-2.0 * rate[ir2f54]) * y[ife52] * y[ineut]);
    jac[(ife52) * 19 + (iprot)] = ((2.0 * rate[ir5f54]) * y[ife54] * y[iprot]) + ((-1.0 * rate[ir7f54]) * y[ife52] * y[ihe4]) + (rate[ir8f54] * y[ini56]);
    jac[(ife54) * 19 + (ihe4)] = (rate[ir6f54] * y[ife52]);
    jac[(ife54) * 19 + (ife52)] = (rate[ir2f54] * y[ineut] * y[ineut]) + (rate[ir6f54] * y[ihe4]);
    jac[(ife54) * 19 + (ife54)] = ((-1.0 * rate[ir1f54])) + (((-1.0 * rate[ir3f54]) + (-1.0 * rate[ir5f54])) * y[iprot] * y[iprot]);
    jac[(ife54) * 19 + (ini56)] = ((rate[ir4f54] + (c54 * rate[irn56ec])));
    jac[(ife54) * 19 + (ineut)] = ((2.0 * rate[ir2f54]) * y[ife52] * y[ineut]);
    jac[(ife54) * 19 + (iprot)] = ((2.0 * ((-1.0 * rate[ir3f54]) + (-1.0 * rate[ir5f54]))) * y[ife54] * y[iprot]);
    jac[(ini56) * 19 + (ihe4)] = (rate[irfeag] * y[ife52]) + (rate[ir7f54] * y[ife52] * y[iprot]);
    jac[(ini56) * 19 + (ife52)] = (rate[irfeag] * y[ihe4]) + (rate[ir7f54] * y[ihe4] * y[iprot]);
    jac[(ini56) * 19 + (ife54)] = (rate[ir3f54] * y[iprot] * y[iprot]);
    jac[(ini56) * 19 + (ini56)] = (((-1.0 * rate[irniga]) + (-1.0 * rate[irn56ec]) + (-1.0 * rate[ir4f54]))) + ((-1.0 * rate[ir8f54]) * y[iprot]);
    jac[(ini56) * 19 + (iprot)] = ((2.0 * rate[ir3f54]) * y[ife54] * y[iprot]) + (rate[ir7f54] * y[ife52] * y[ihe4]) + ((-1.0 * rate[ir8f54]) * y[ini56]);
    jac[(ineut) * 19 + (ihe4)] = ((2.0 * rate[iralf1]));
    jac[(ineut) * 19 + (ife52)] = ((-2.0 * rate[ir2f54]) * y[ineut] * y[ineut]);
    jac[(ineut) * 19 + (ife54)] = ((2.0 * rate[ir1f54]));
    jac[(ineut) * 19 + (ineut)] = ((-4.0 * rate[ir2f54]) * y[ife52] * y[ineut]) + ((-4.0 * rate[iralf2]) * y[ineut] * y[iprot] * y[iprot]) + ((-1.0 * rate[irnep]));
    jac[(ineut) * 19 + (iprot)] = ((-4.0 * rate[iralf2]) * y[ineut] * y[ineut] * y[iprot]) + (rate[irpen]);
    jac[(iprot) * 19 + (ihe4)] = ((2.0 * rate[ir6f54]) * y[ife52]) + ((2.0 * rate[iralf1]));
    jac[(iprot) * 19 + (ife52)] = ((2.0 * rate[ir6f54]) * y[ihe4]);
    jac[(iprot) * 19 + (ife54)] = (((-2.0 * rate[ir3f54]) + (-2.0 * rate[ir5f54])) * y[iprot] * y[iprot]);
    jac[(iprot) * 19 + (ini56)] = ((2.0 * rate[ir4f54]));
    jac[(iprot) * 19 + (ineut)] = ((-4.0 * rate[iralf2]) * y[ineut] * y[iprot] * y[iprot]) + (rate[irnep]);
    jac[(iprot) * 19 + (iprot)] = ((2.0 * ((-2.0 * rate[ir3f54]) + (-2.0 * rate[ir5f54]))) * y[ife54] * y[iprot]) + ((-4.0 * rate[iralf2]) * y[ineut] * y[ineut] * y[iprot]) + ((-1.0 * rate[irpen]));
}

// Add only the chain-rule contribution from abundance-dependent
// rates produced by form_extended_equilibrium().  differentiated_rate
// contains the closure evaluated with one active Dual<1> abundance.
TIMMES_HD TIMMES_JACOBIAN_NOINLINE inline void add_aprox19_rate_derivative_column(
    const double* y, const double* rate,
    const timmes::Dual<1>* differentiated_rate,
    int column, double* jac)
{
    jac[(ih1) * 19 + column] += ((-1.0 * y[ihe3] * y[ihe4]) * differentiated_rate[irhe3ag].deriv[0]) + ((-2.0 * y[ih1] * y[in14]) * differentiated_rate[irnpg].deriv[0]) + ((-2.0 * y[ih1] * y[io16]) * differentiated_rate[iropg].deriv[0]);
    jac[(ihe3) * 19 + column] += ((-1.0 * y[ihe3] * y[ihe4]) * differentiated_rate[irhe3ag].deriv[0]);
    jac[(ihe4) * 19 + column] += ((1.0 * y[ife54] * y[iprot] * y[iprot]) * differentiated_rate[ir5f54].deriv[0]) + ((-1.0 * y[ife52] * y[ihe4]) * differentiated_rate[ir6f54].deriv[0]) + ((-1.0 * y[ife52] * y[ihe4] * y[iprot]) * differentiated_rate[ir7f54].deriv[0]) + ((1.0 * y[ini56] * y[iprot]) * differentiated_rate[ir8f54].deriv[0]) + ((-1.0 * y[ihe4]) * differentiated_rate[iralf1].deriv[0]) + ((1.0 * y[ineut] * y[ineut] * y[iprot] * y[iprot]) * differentiated_rate[iralf2].deriv[0]) + ((1.0 * y[ihe3] * y[ihe4]) * differentiated_rate[irhe3ag].deriv[0]) + ((rate[ifa] * y[ih1] * y[in14]) * differentiated_rate[irnpg].deriv[0]) + ((1.0 * y[ih1] * y[io16]) * differentiated_rate[iropg].deriv[0]);
    jac[(ic12) * 19 + column] += ((rate[ifa] * y[ih1] * y[in14]) * differentiated_rate[irnpg].deriv[0]);
    jac[(in14) * 19 + column] += ((-1.0 * y[ih1] * y[in14]) * differentiated_rate[irnpg].deriv[0]) + ((1.0 * y[ih1] * y[io16]) * differentiated_rate[iropg].deriv[0]);
    jac[(io16) * 19 + column] += ((rate[ifg] * y[ih1] * y[in14]) * differentiated_rate[irnpg].deriv[0]) + ((-1.0 * y[ih1] * y[io16]) * differentiated_rate[iropg].deriv[0]);
    jac[(ife52) * 19 + column] += ((1.0 * y[ife54]) * differentiated_rate[ir1f54].deriv[0]) + ((-1.0 * y[ife52] * y[ineut] * y[ineut]) * differentiated_rate[ir2f54].deriv[0]) + ((1.0 * y[ife54] * y[iprot] * y[iprot]) * differentiated_rate[ir5f54].deriv[0]) + ((-1.0 * y[ife52] * y[ihe4]) * differentiated_rate[ir6f54].deriv[0]) + ((-1.0 * y[ife52] * y[ihe4] * y[iprot]) * differentiated_rate[ir7f54].deriv[0]) + ((1.0 * y[ini56] * y[iprot]) * differentiated_rate[ir8f54].deriv[0]);
    jac[(ife54) * 19 + column] += ((-1.0 * y[ife54]) * differentiated_rate[ir1f54].deriv[0]) + ((1.0 * y[ife52] * y[ineut] * y[ineut]) * differentiated_rate[ir2f54].deriv[0]) + ((-1.0 * y[ife54] * y[iprot] * y[iprot]) * differentiated_rate[ir3f54].deriv[0]) + ((1.0 * y[ini56]) * differentiated_rate[ir4f54].deriv[0]) + ((-1.0 * y[ife54] * y[iprot] * y[iprot]) * differentiated_rate[ir5f54].deriv[0]) + ((1.0 * y[ife52] * y[ihe4]) * differentiated_rate[ir6f54].deriv[0]);
    jac[(ini56) * 19 + column] += ((1.0 * y[ife54] * y[iprot] * y[iprot]) * differentiated_rate[ir3f54].deriv[0]) + ((-1.0 * y[ini56]) * differentiated_rate[ir4f54].deriv[0]) + ((1.0 * y[ife52] * y[ihe4] * y[iprot]) * differentiated_rate[ir7f54].deriv[0]) + ((-1.0 * y[ini56] * y[iprot]) * differentiated_rate[ir8f54].deriv[0]);
    jac[(ineut) * 19 + column] += ((2.0 * y[ife54]) * differentiated_rate[ir1f54].deriv[0]) + ((-2.0 * y[ife52] * y[ineut] * y[ineut]) * differentiated_rate[ir2f54].deriv[0]) + ((2.0 * y[ihe4]) * differentiated_rate[iralf1].deriv[0]) + ((-2.0 * y[ineut] * y[ineut] * y[iprot] * y[iprot]) * differentiated_rate[iralf2].deriv[0]);
    jac[(iprot) * 19 + column] += ((-2.0 * y[ife54] * y[iprot] * y[iprot]) * differentiated_rate[ir3f54].deriv[0]) + ((2.0 * y[ini56]) * differentiated_rate[ir4f54].deriv[0]) + ((-2.0 * y[ife54] * y[iprot] * y[iprot]) * differentiated_rate[ir5f54].deriv[0]) + ((2.0 * y[ife52] * y[ihe4]) * differentiated_rate[ir6f54].deriv[0]) + ((2.0 * y[ihe4]) * differentiated_rate[iralf1].deriv[0]) + ((-2.0 * y[ineut] * y[ineut] * y[iprot] * y[iprot]) * differentiated_rate[iralf2].deriv[0]);
}
#undef TIMMES_JACOBIAN_NOINLINE

// fixed-rate nonzero entries: 112 / 361
// closure-correction rows: 11
