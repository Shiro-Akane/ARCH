#ifndef NETWORK_PROPERTIES_H
#define NETWORK_PROPERTIES_H
#include <vector>
#include <string>
#include <amrex_bridge.H>

constexpr int NumSpec = 16;

constexpr int NumSpecExtra = 9;

constexpr int NumSpecTotal = NumSpec + NumSpecExtra;

// Note: these are 0-based
constexpr Real aion[NumSpecTotal] = {
       1.0, // 0 : n
       1.0, // 1 : p
       3.0, // 2 : He3
       4.0, // 3 : He4
      12.0, // 4 : C12
      16.0, // 5 : O16
      20.0, // 6 : Ne20
      24.0, // 7 : Mg24
      28.0, // 8 : Si28
      32.0, // 9 : S32
      36.0, // 10 : Ar36
      40.0, // 11 : Ca40
      44.0, // 12 : Ti44
      48.0, // 13 : Cr48
      52.0, // 14 : Fe52
      56.0, // 15 : Ni56
      23.0, // 16 : Na23
      27.0, // 17 : Al27
      31.0, // 18 : P31
      35.0, // 19 : Cl35
      39.0, // 20 : K39
      43.0, // 21 : Sc43
      47.0, // 22 : V47
      51.0, // 23 : Mn51
      55.0, // 24 : Co55
 };

constexpr Real aion_inv[NumSpecTotal] = {
    1.0/   1.0, // 0 : n
    1.0/   1.0, // 1 : p
    1.0/   3.0, // 2 : He3
    1.0/   4.0, // 3 : He4
    1.0/  12.0, // 4 : C12
    1.0/  16.0, // 5 : O16
    1.0/  20.0, // 6 : Ne20
    1.0/  24.0, // 7 : Mg24
    1.0/  28.0, // 8 : Si28
    1.0/  32.0, // 9 : S32
    1.0/  36.0, // 10 : Ar36
    1.0/  40.0, // 11 : Ca40
    1.0/  44.0, // 12 : Ti44
    1.0/  48.0, // 13 : Cr48
    1.0/  52.0, // 14 : Fe52
    1.0/  56.0, // 15 : Ni56
    1.0/  23.0, // 16 : Na23
    1.0/  27.0, // 17 : Al27
    1.0/  31.0, // 18 : P31
    1.0/  35.0, // 19 : Cl35
    1.0/  39.0, // 20 : K39
    1.0/  43.0, // 21 : Sc43
    1.0/  47.0, // 22 : V47
    1.0/  51.0, // 23 : Mn51
    1.0/  55.0, // 24 : Co55
 };

constexpr Real zion[NumSpecTotal] = {
       0.0, // 0 : n
       1.0, // 1 : p
       2.0, // 2 : He3
       2.0, // 3 : He4
       6.0, // 4 : C12
       8.0, // 5 : O16
      10.0, // 6 : Ne20
      12.0, // 7 : Mg24
      14.0, // 8 : Si28
      16.0, // 9 : S32
      18.0, // 10 : Ar36
      20.0, // 11 : Ca40
      22.0, // 12 : Ti44
      24.0, // 13 : Cr48
      26.0, // 14 : Fe52
      28.0, // 15 : Ni56
      11.0, // 16 : Na23
      13.0, // 17 : Al27
      15.0, // 18 : P31
      17.0, // 19 : Cl35
      19.0, // 20 : K39
      21.0, // 21 : Sc43
      23.0, // 22 : V47
      25.0, // 23 : Mn51
      27.0, // 24 : Co55
 };

static const std::vector<std::string> spec_names = {
    "N", // 0
    "H1", // 1
    "He3", // 2
    "He4", // 3
    "C12", // 4
    "O16", // 5
    "Ne20", // 6
    "Mg24", // 7
    "Si28", // 8
    "S32", // 9
    "Ar36", // 10
    "Ca40", // 11
    "Ti44", // 12
    "Cr48", // 13
    "Fe52", // 14
    "Ni56", // 15
    "Na23", // 16
    "Al27", // 17
    "P31", // 18
    "Cl35", // 19
    "K39", // 20
    "Sc43", // 21
    "V47", // 22
    "Mn51", // 23
    "Co55", // 24
 };

namespace Species {
  enum NetworkSpecies {
    N=1,
    H1,
    He3,
    He4,
    C12,
    O16,
    Ne20,
    Mg24,
    Si28,
    S32,
    Ar36,
    Ca40,
    Ti44,
    Cr48,
    Fe52,
    Ni56,
    Na23,
    Al27,
    P31,
    Cl35,
    K39,
    Sc43,
    V47,
    Mn51,
    Co55,
  };
}

#endif
