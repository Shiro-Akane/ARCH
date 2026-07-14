// Generated only from the uploaded Timmes source.
// public_iso7.f90:422
template <typename Scalar>
inline void rhs_iso7(const Scalar* y, const Scalar* rate, Scalar* dydt)
{
    for (int i = 0; i < 7; ++i) dydt[i] = Scalar(0.0);
    Scalar qray[7]{};
    Scalar a1{}, a2{}, a3{}, a4{}, a5{}, a6{}, a7{}, a8{}, a9{};
    Scalar a10{}, a11{}, a12{}, a13{}, a14{}, a15{}, a16{}, a17{};
    dydt[ihe4] = ((((((((((((((((3.0e0 * y[ic12]) * rate[irg3a]) - ((((0.5e0 * y[ihe4]) * y[ihe4]) * y[ihe4]) * rate[ir3a])) + (y[io16] * rate[iroga])) - ((y[ic12] * y[ihe4]) * rate[ircag])) + (((0.5e0 * y[ic12]) * y[ic12]) * rate[ir1212])) + (((0.5e0 * y[ic12]) * y[io16]) * rate[ir1216])) + (((0.5e0 * y[io16]) * y[io16]) * rate[ir1616])) - ((y[io16] * y[ihe4]) * rate[iroag])) + (y[ine20] * rate[irnega])) + (y[img24] * rate[irmgga])) - ((y[ine20] * y[ihe4]) * rate[irneag])) + (y[isi28] * rate[irsiga])) - ((y[img24] * y[ihe4]) * rate[irmgag])) - ((static_cast<double>(7.0e0f) * rate[irsi2ni]) * y[ihe4])) + ((static_cast<double>(7.0e0f) * rate[irni2si]) * y[ini56]));
    dydt[ic12] = (((((((((sixth * y[ihe4]) * y[ihe4]) * y[ihe4]) * rate[ir3a]) - (y[ic12] * rate[irg3a])) + (y[io16] * rate[iroga])) - ((y[ic12] * y[ihe4]) * rate[ircag])) - ((y[ic12] * y[ic12]) * rate[ir1212])) - ((y[ic12] * y[io16]) * rate[ir1216]));
    dydt[io16] = (((((((-y[io16]) * rate[iroga]) + ((y[ic12] * y[ihe4]) * rate[ircag])) - ((y[ic12] * y[io16]) * rate[ir1216])) - ((y[io16] * y[io16]) * rate[ir1616])) - ((y[io16] * y[ihe4]) * rate[iroag])) + (y[ine20] * rate[irnega]));
    dydt[ine20] = (((((((0.5e0 * y[ic12]) * y[ic12]) * rate[ir1212]) + ((y[io16] * y[ihe4]) * rate[iroag])) - (y[ine20] * rate[irnega])) + (y[img24] * rate[irmgga])) - ((y[ine20] * y[ihe4]) * rate[irneag]));
    dydt[img24] = (((((((0.5e0 * y[ic12]) * y[io16]) * rate[ir1216]) - (y[img24] * rate[irmgga])) + ((y[ine20] * y[ihe4]) * rate[irneag])) + (y[isi28] * rate[irsiga])) - ((y[img24] * y[ihe4]) * rate[irmgag]));
    dydt[isi28] = ((((((((0.5e0 * y[ic12]) * y[io16]) * rate[ir1216]) + (((0.5e0 * y[io16]) * y[io16]) * rate[ir1616])) - (y[isi28] * rate[irsiga])) + ((y[img24] * y[ihe4]) * rate[irmgag])) - (rate[irsi2ni] * y[ihe4])) + (rate[irni2si] * y[ini56]));
    dydt[ini56] = ((rate[irsi2ni] * y[ihe4]) - (rate[irni2si] * y[ini56]));
}
