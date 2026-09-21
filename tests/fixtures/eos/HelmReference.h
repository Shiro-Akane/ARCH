#pragma once

#include <array>

// Independent monomial endpoint-fit DATA, not sampled from HelmEos.
// Reproduce: validation/eos/helm_reference.py (mpmath 60 and 80 digits).
// Table SHA-256: c9a57c26c6fd2b2b378b9d5295ca1214022f6fec6289d038b47bf8c8938881a1
// SI/CODATA 2022, unchanged binary64 table data, decimal table grid.
// Composition X={0.25,0.75}, A={1,4}, Z={1,2}.
namespace HelmReference {

inline constexpr double lower_bound_P = 0x1.265fc90c16d69p-3;
inline constexpr double upper_bound_P = 0x1.a4f566773ceb4p+125;
// P,E,cv,cs,analytic dP/drho,analytic dP/dT,pele,xne,eta.
inline constexpr std::array<double, 9> state{
    0x1.165b39cd39317p+75,
    0x1.de090cf0d3a24p+55,
    0x1.11dd49d27b7b9p+26,
    0x1.e35fdb06be6ccp+27,
    0x1.b448c35df197ap+55,
    0x1.479b66b8a1053p+45,
    0x1.fdc71382b3e0bp+74,
    0x1.300a5854b919fp+98,
    0x1.2f0e543b5127ap+4
};

inline constexpr std::array<double, 19> probe{
    0x1.6666666666666p+0,
    0x1.11dd49d27b7b9p+26,
    0x1.165b39cd39317p+75,
    0x1.de090cf0d3a24p+55,
    0x1.7d78400000000p+26,
    0x1.165b39cd39317p+75,
    0x1.e35fdb06be6ccp+27,
    0x1.114a967071b91p+55,
    0x1.323ca8d59ba93p+19,
    0x1.c7e3d99b5b946p+75,
    0x1.165b39cd39317p+75,
    0x1.de090cf0d3a24p+55,
    0x1.11dd49d27b7b9p+26,
    0x1.e35fdb06be6ccp+27,
    0x1.b448c35df197ap+55,
    0x1.479b66b8a1053p+45,
    0x1.fdc71382b3e0bp+74,
    0x1.300a5854b919fp+98,
    0x1.2f0e543b5127ap+4
};
// Independent pressure-coordinate DOP853 path with thermodynamic derivatives.
inline constexpr std::array<double, 4> isentrope{
    0x1.e897ef7879665p+19,
    0x1.7d9f696be747fp+26,
    0x1.16a27c29d839fp+75,
    0x1.e37509236a39bp+27
};

struct Point {
    double rho, temperature;
    std::array<double, 6> values; // P,E,cv,pele,xne,eta
    // P_rho,P_T,e_y,e_z,e_yy,e_yz,e_zz,cv_y,cv_z,cv_T; y=sum(X/A), z=Ye.
    std::array<double, 10> derivatives;
};
inline constexpr Point points[]{
    {1.0e6, 1.0e8,
     {0x1.165b39cd39317p+75, 0x1.de090cf0d3a24p+55, 0x1.11dd49d27b7b9p+26, 0x1.fdc71382b3e0bp+74, 0x1.300a5854b919fp+98, 0x1.2f0e543b5127ap+4},
     {0x1.b448c35df197ap+55, 0x1.479b66b8a1053p+45, 0x1.8e5eb3283f5fep+53, 0x1.15bd4ad5415f5p+57,
      -0x1.aa1f06fe40193p+52, 0x1.65f11a5ab0b90p+52, 0x1.f77e441709355p+56,
      0x1.cb9e0216a02e8p+26, 0x1.23e68b82755dep+24, 0x1.0663b45861553p-3}},
    {0x1.dcd6500000000p+29, 0x1.e848000000000p+19,
     {0x1.0dbe9ee0ea344p+89, 0x1.798aae83b1da9p+60, 0x1.0afc804b0ff72p+27, 0x1.0f4cb6de06f98p+89, 0x1.28ea1a42bcc35p+108, 0x1.63f1d29066a4fp+15},
     {0x1.84996d3c6d6c7p+59, 0x1.ccc4533597f0fp+55, 0x1.fa082ffe64abfp+53, 0x1.a07e1b9794348p+61,
      -0x1.dc2a3bf080341p+55, 0x1.8ffa8444e6922p+55, 0x1.e72924b4cdb04p+60,
      0x1.9698a44da62c4p+27, 0x1.561cd876add0dp+26, -0x1.c0300f837c1e6p+4}},
    {0x1.dcd6500000000p+29, 0x1.7d78400000000p+26,
     {0x1.0f89c477d6b1ep+89, 0x1.7b3b182f36353p+60, 0x1.02795a5a8d4a6p+26, 0x1.0f4e84e1dc666p+89, 0x1.28ea1a42bcc35p+108, 0x1.c79a494e23ed0p+8},
     {0x1.8692aaf086db0p+59, 0x1.21bf085513b52p+55, 0x1.bb274c72cf8a5p+54, 0x1.a0dc8f11b740dp+61,
      -0x1.f983822ed9ae2p+55, 0x1.a8a1aacb31bb4p+55, 0x1.e6bd5494cbd58p+60,
      0x1.dd2aed73b795ep+26, 0x1.708b4f3d0d7a4p+24, -0x1.fc3c9fe08a260p-5}},
    {0x1.0c6f7a0b5ed8dp-20, 0x1.dcd6500000000p+29,
     {0x1.21b001d82de89p+71, 0x1.c2cefe7c1ecf2p+92, 0x1.11b149c92a6ffp+65, 0x1.042e9d6acda1ap+67, 0x1.4e4bc4754ccdcp+58, -0x1.7b834f0c26005p+2},
     {-0x1.c48a93598a9fep+72, 0x1.4afcdb7c0f018p+43, 0x1.bb156569577fap+56, 0x1.9585503ad3043p+84,
      -0x1.e604c21d3b302p+31, 0x1.98416fdb1d3cep+31, -0x1.868671f0459f5p+91,
      0x1.dbc1e2ba9ce14p+26, -0x1.1b89ae52bf2b0p+42, 0x1.154c99771f05ep+37}},
};

} // namespace HelmReference
