#ifndef NETWORK_PROPERTIES_H
#define NETWORK_PROPERTIES_H
#include <vector>
#include <string>
#include <amrex_bridge.H>

constexpr int NumSpec = 13;

constexpr int NumSpecExtra = 0;

constexpr int NumSpecTotal = NumSpec + NumSpecExtra;

// Note: these are 0-based
constexpr Real aion[NumSpecTotal] = {
       4.0, // 0 : He4
      12.0, // 1 : C12
      16.0, // 2 : O16
      20.0, // 3 : Ne20
      24.0, // 4 : Mg24
      28.0, // 5 : Si28
      32.0, // 6 : S32
      36.0, // 7 : Ar36
      40.0, // 8 : Ca40
      44.0, // 9 : Ti44
      48.0, // 10 : Cr48
      52.0, // 11 : Fe52
      56.0, // 12 : Ni56
 };

constexpr Real aion_inv[NumSpecTotal] = {
    1.0/   4.0, // 0 : He4
    1.0/  12.0, // 1 : C12
    1.0/  16.0, // 2 : O16
    1.0/  20.0, // 3 : Ne20
    1.0/  24.0, // 4 : Mg24
    1.0/  28.0, // 5 : Si28
    1.0/  32.0, // 6 : S32
    1.0/  36.0, // 7 : Ar36
    1.0/  40.0, // 8 : Ca40
    1.0/  44.0, // 9 : Ti44
    1.0/  48.0, // 10 : Cr48
    1.0/  52.0, // 11 : Fe52
    1.0/  56.0, // 12 : Ni56
 };

constexpr Real zion[NumSpecTotal] = {
       2.0, // 0 : He4
       6.0, // 1 : C12
       8.0, // 2 : O16
      10.0, // 3 : Ne20
      12.0, // 4 : Mg24
      14.0, // 5 : Si28
      16.0, // 6 : S32
      18.0, // 7 : Ar36
      20.0, // 8 : Ca40
      22.0, // 9 : Ti44
      24.0, // 10 : Cr48
      26.0, // 11 : Fe52
      28.0, // 12 : Ni56
 };

static const std::vector<std::string> spec_names = {
    "He4", // 0
    "C12", // 1
    "O16", // 2
    "Ne20", // 3
    "Mg24", // 4
    "Si28", // 5
    "S32", // 6
    "Ar36", // 7
    "Ca40", // 8
    "Ti44", // 9
    "Cr48", // 10
    "Fe52", // 11
    "Ni56", // 12
 };

namespace Species {
  enum NetworkSpecies {
    He4=1,
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
  };
}

#endif
