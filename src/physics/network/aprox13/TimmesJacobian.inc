// Mechanically adapted from Frank Timmes's public_aprox13.f90:1048-1362.
// Upstream index: https://cococubed.com/code_pages/burn.shtml.
// The screened rates are held fixed, matching dfdy_isotopes_aprox13.
TIMMES_HD inline void jacobian_aprox13_molar(
    const double* y, const double* rate, double* jac)
{
    for (int i = 0; i < 13 * 13; ++i) jac[i] = 0.0;
    jac[(ihe4) * 13 + (ihe4)] = -1.5e0 * y[ihe4] * y[ihe4] * rate[ir3a] - y[ic12] * rate[ircag] - y[io16] * rate[iroag] - y[ine20] * rate[irneag] - y[img24] * rate[irmgag] - y[isi28] * rate[irsiag] - y[is32] * rate[irsag] - y[iar36] * rate[irarag] - y[ica40] * rate[ircaag] - y[iti44] * rate[irtiag] - y[icr48] * rate[ircrag] - y[ife52] * rate[irfeag];
    jac[(ihe4) * 13 + (ihe4)] = jac[(ihe4) * 13 + (ihe4)] - y[img24] * rate[irmgap] * (1.0e0-rate[irr1]) - y[isi28] * rate[irsiap] * (1.0e0-rate[irs1]) - y[is32] * rate[irsap] * (1.0e0-rate[irt1]) - y[iar36] * rate[irarap] * (1.0e0-rate[iru1]) - y[ica40] * rate[ircaap] * (1.0e0-rate[irv1]) - y[iti44] * rate[irtiap] * (1.0e0-rate[irw1]) - y[icr48] * rate[ircrap] * (1.0e0-rate[irx1]) - y[ife52] * rate[irfeap] * (1.0e0-rate[iry1]);
    jac[(ihe4) * 13 + (ic12)] = y[ic12] * rate[ir1212] + 0.5e0 * y[io16] * rate[ir1216] + 3.0e0 * rate[irg3a] - y[ihe4] * rate[ircag];
    jac[(ihe4) * 13 + (io16)] = 0.5e0 * y[ic12] * rate[ir1216] + 1.12e0 * 0.5e0 * y[io16] * rate[ir1616] + 0.68e0*rate[irs1]*0.5e0*y[io16]*rate[ir1616] + rate[iroga] - y[ihe4] * rate[iroag];
    jac[(ihe4) * 13 + (ine20)] = rate[irnega] - y[ihe4] * rate[irneag];
    jac[(ihe4) * 13 + (img24)] = rate[irmgga] - y[ihe4] * rate[irmgag] - y[ihe4] * rate[irmgap] * (1.0e0-rate[irr1]);
    jac[(ihe4) * 13 + (isi28)] = rate[irsiga] - y[ihe4] * rate[irsiag] - y[ihe4] * rate[irsiap] * (1.0e0-rate[irs1]) + rate[irr1] * rate[irsigp];
    jac[(ihe4) * 13 + (is32)] = rate[irsga] - y[ihe4] * rate[irsag] - y[ihe4] * rate[irsap] * (1.0e0-rate[irt1]) + rate[irs1] * rate[irsgp];
    jac[(ihe4) * 13 + (iar36)] = rate[irarga] - y[ihe4] * rate[irarag] - y[ihe4] * rate[irarap] * (1.0e0-rate[iru1]) + rate[irt1] * rate[irargp];
    jac[(ihe4) * 13 + (ica40)] = rate[ircaga] - y[ihe4] * rate[ircaag] - y[ihe4] * rate[ircaap] * (1.0e0-rate[irv1]) + rate[iru1] * rate[ircagp];
    jac[(ihe4) * 13 + (iti44)] = rate[irtiga] - y[ihe4] * rate[irtiag] - y[ihe4] * rate[irtiap] * (1.0e0-rate[irw1]) + rate[irv1] * rate[irtigp];
    jac[(ihe4) * 13 + (icr48)] = rate[ircrga] - y[ihe4] * rate[ircrag] - y[ihe4] * rate[ircrap] * (1.0e0-rate[irx1]) + rate[irw1] * rate[ircrgp];
    jac[(ihe4) * 13 + (ife52)] = rate[irfega] - y[ihe4] * rate[irfeag] - y[ihe4] * rate[irfeap] * (1.0e0-rate[iry1]) + rate[irx1] * rate[irfegp];
    jac[(ihe4) * 13 + (ini56)] = rate[irniga] + rate[iry1] * rate[irnigp];
    jac[(ic12) * 13 + (ihe4)] = 0.5e0*y[ihe4]*y[ihe4]*rate[ir3a] - y[ic12] * rate[ircag];
    jac[(ic12) * 13 + (ic12)] = -2.0e0 * y[ic12] * rate[ir1212] - y[io16] * rate[ir1216] - rate[irg3a] - y[ihe4] * rate[ircag];
    jac[(ic12) * 13 + (io16)] = -y[ic12] * rate[ir1216] + rate[iroga];
    jac[(io16) * 13 + (ihe4)] = y[ic12]*rate[ircag] - y[io16]*rate[iroag];
    jac[(io16) * 13 + (ic12)] = -y[io16]*rate[ir1216] + y[ihe4]*rate[ircag];
    jac[(io16) * 13 + (io16)] = -y[ic12] * rate[ir1216] - 2.0e0 * y[io16] * rate[ir1616] - y[ihe4] * rate[iroag] - rate[iroga];
    jac[(io16) * 13 + (ine20)] = rate[irnega];
    jac[(ine20) * 13 + (ihe4)] = y[io16] * rate[iroag] - y[ine20] * rate[irneag];
    jac[(ine20) * 13 + (ic12)] = y[ic12] * rate[ir1212];
    jac[(ine20) * 13 + (io16)] = y[ihe4] * rate[iroag];
    jac[(ine20) * 13 + (ine20)] = -y[ihe4] * rate[irneag] - rate[irnega];
    jac[(ine20) * 13 + (img24)] = rate[irmgga];
    jac[(img24) * 13 + (ihe4)] = y[ine20] * rate[irneag] -y[img24] * rate[irmgag] -y[img24] * rate[irmgap] * (1.0e0-rate[irr1]);
    jac[(img24) * 13 + (ic12)] = 0.5e0 * y[io16] * rate[ir1216];
    jac[(img24) * 13 + (io16)] = 0.5e0 * y[ic12] * rate[ir1216];
    jac[(img24) * 13 + (ine20)] = y[ihe4] * rate[irneag];
    jac[(img24) * 13 + (img24)] = -y[ihe4] * rate[irmgag] - rate[irmgga] - y[ihe4] * rate[irmgap] * (1.0e0-rate[irr1]);
    jac[(img24) * 13 + (isi28)] = rate[irsiga] + rate[irr1] * rate[irsigp];
    jac[(isi28) * 13 + (ihe4)] = y[img24] * rate[irmgag] - y[isi28] * rate[irsiag] + y[img24] * rate[irmgap] * (1.0e0-rate[irr1]) - y[isi28] * rate[irsiap] * (1.0e0-rate[irs1]);
    jac[(isi28) * 13 + (ic12)] = 0.5e0 * y[io16] * rate[ir1216];
    jac[(isi28) * 13 + (io16)] = 0.5e0 * y[ic12] * rate[ir1216] + 1.12e0 * 0.5e0*y[io16] * rate[ir1616] + 0.68e0*0.5e0*y[io16]*rate[irs1]*rate[ir1616];
    jac[(isi28) * 13 + (img24)] = y[ihe4] * rate[irmgag] + y[ihe4] * rate[irmgap] * (1.0e0-rate[irr1]);
    jac[(isi28) * 13 + (isi28)] = -y[ihe4] * rate[irsiag] - rate[irsiga] - rate[irr1] * rate[irsigp] - y[ihe4] * rate[irsiap] * (1.0e0-rate[irs1]);
    jac[(isi28) * 13 + (is32)] = rate[irsga] + rate[irs1] * rate[irsgp];
    jac[(is32) * 13 + (ihe4)] = y[isi28] * rate[irsiag] - y[is32] * rate[irsag] + y[isi28] * rate[irsiap] * (1.0e0-rate[irs1]) - y[is32] * rate[irsap] * (1.0e0-rate[irt1]);
    jac[(is32) * 13 + (io16)] = 0.68e0*0.5e0*y[io16]*rate[ir1616]*(1.0e0-rate[irs1]) + 0.2e0 * 0.5e0*y[io16] * rate[ir1616];
    jac[(is32) * 13 + (isi28)] = y[ihe4] * rate[irsiag] + y[ihe4] * rate[irsiap] * (1.0e0-rate[irs1]);
    jac[(is32) * 13 + (is32)] = -y[ihe4] * rate[irsag] - rate[irsga] - rate[irs1] * rate[irsgp] - y[ihe4] * rate[irsap] * (1.0e0-rate[irt1]);
    jac[(is32) * 13 + (iar36)] = rate[irarga] + rate[irt1] * rate[irargp];
    jac[(iar36) * 13 + (ihe4)] = y[is32] * rate[irsag] - y[iar36] * rate[irarag] + y[is32] * rate[irsap] * (1.0e0-rate[irt1]) - y[iar36] * rate[irarap] * (1.0e0-rate[iru1]);
    jac[(iar36) * 13 + (is32)] = y[ihe4] * rate[irsag] + y[ihe4] * rate[irsap] * (1.0e0-rate[irt1]);
    jac[(iar36) * 13 + (iar36)] = -y[ihe4] * rate[irarag] - rate[irarga] - rate[irt1] * rate[irargp] - y[ihe4] * rate[irarap] * (1.0e0-rate[iru1]);
    jac[(iar36) * 13 + (ica40)] = rate[ircaga] + rate[ircagp] * rate[iru1];
    jac[(ica40) * 13 + (ihe4)] = y[iar36] * rate[irarag] - y[ica40] * rate[ircaag] + y[iar36] * rate[irarap]*(1.0e0-rate[iru1]) - y[ica40] * rate[ircaap]*(1.0e0-rate[irv1]);
    jac[(ica40) * 13 + (iar36)] = y[ihe4] * rate[irarag] + y[ihe4] * rate[irarap]*(1.0e0-rate[iru1]);
    jac[(ica40) * 13 + (ica40)] = -y[ihe4] * rate[ircaag] - rate[ircaga] - rate[ircagp] * rate[iru1] - y[ihe4] * rate[ircaap]*(1.0e0-rate[irv1]);
    jac[(ica40) * 13 + (iti44)] = rate[irtiga] + rate[irtigp] * rate[irv1];
    jac[(iti44) * 13 + (ihe4)] = y[ica40] * rate[ircaag] - y[iti44] * rate[irtiag] + y[ica40] * rate[ircaap]*(1.0e0-rate[irv1]) - y[iti44] * rate[irtiap]*(1.0e0-rate[irw1]);
    jac[(iti44) * 13 + (ica40)] = y[ihe4] * rate[ircaag] + y[ihe4] * rate[ircaap]*(1.0e0-rate[irv1]);
    jac[(iti44) * 13 + (iti44)] = -y[ihe4] * rate[irtiag] - rate[irtiga] - rate[irv1] * rate[irtigp] - y[ihe4] * rate[irtiap]*(1.0e0-rate[irw1]);
    jac[(iti44) * 13 + (icr48)] = rate[ircrga] + rate[irw1] * rate[ircrgp];
    jac[(icr48) * 13 + (ihe4)] = y[iti44] * rate[irtiag] - y[icr48] * rate[ircrag] + y[iti44] * rate[irtiap]*(1.0e0-rate[irw1]) - y[icr48] * rate[ircrap]*(1.0e0-rate[irx1]);
    jac[(icr48) * 13 + (iti44)] = y[ihe4] * rate[irtiag] + y[ihe4] * rate[irtiap]*(1.0e0-rate[irw1]);
    jac[(icr48) * 13 + (icr48)] = -y[ihe4] * rate[ircrag] - rate[ircrga] - rate[irw1] * rate[ircrgp] - y[ihe4] * rate[ircrap]*(1.0e0-rate[irx1]);
    jac[(icr48) * 13 + (ife52)] = rate[irfega] + rate[irx1] * rate[irfegp];
    jac[(ife52) * 13 + (ihe4)] = y[icr48] * rate[ircrag] - y[ife52] * rate[irfeag] + y[icr48] * rate[ircrap] * (1.0e0-rate[irx1]) - y[ife52] * rate[irfeap] * (1.0e0-rate[iry1]);
    jac[(ife52) * 13 + (icr48)] = y[ihe4] * rate[ircrag] + y[ihe4] * rate[ircrap] * (1.0e0-rate[irx1]);
    jac[(ife52) * 13 + (ife52)] = -y[ihe4] * rate[irfeag] - rate[irfega] - rate[irx1] * rate[irfegp] - y[ihe4] * rate[irfeap] * (1.0e0-rate[iry1]);
    jac[(ife52) * 13 + (ini56)] = rate[irniga] + rate[iry1] * rate[irnigp];
    jac[(ini56) * 13 + (ihe4)] = y[ife52] * rate[irfeag] + y[ife52] * rate[irfeap] * (1.0e0-rate[iry1]);
    jac[(ini56) * 13 + (ife52)] = y[ihe4] * rate[irfeag] + y[ihe4] * rate[irfeap] * (1.0e0-rate[iry1]);
    jac[(ini56) * 13 + (ini56)] = -rate[irniga] - rate[iry1] * rate[irnigp];
}
