#ifndef NETWORK_PROPERTIES_H
#define NETWORK_PROPERTIES_H
#include <vector>
#include <string>
#include <amrex_bridge.H>

constexpr int NumSpec = 6;

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
 };

constexpr Real aion_inv[NumSpecTotal] = {
    1.0/   4.0, // 0 : He4
    1.0/  12.0, // 1 : C12
    1.0/  16.0, // 2 : O16
    1.0/  20.0, // 3 : Ne20
    1.0/  24.0, // 4 : Mg24
    1.0/  28.0, // 5 : Si28
 };

constexpr Real zion[NumSpecTotal] = {
       2.0, // 0 : He4
       6.0, // 1 : C12
       8.0, // 2 : O16
      10.0, // 3 : Ne20
      12.0, // 4 : Mg24
      14.0, // 5 : Si28
 };

static const std::vector<std::string> spec_names = {
    "He4", // 0
    "C12", // 1
    "O16", // 2
    "Ne20", // 3
    "Mg24", // 4
    "Si28", // 5
 };

namespace Species {
  enum NetworkSpecies {
    He4=1,
    C12,
    O16,
    Ne20,
    Mg24,
    Si28,
  };
}

#endif
