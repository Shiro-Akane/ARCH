#ifndef actual_network_H
#define actual_network_H

#include <amrex_bridge.H>

#include <fundamental_constants.H>
#include <network_properties.H>

inline void actual_network_init() {}

const std::string network_name = "pynucastro-cxx";

namespace network
{

    // nuclei masses (in grams)

    inline Array1D<Real, 1, NumSpec> mion{
        1.674927498034172e-24_rt,  // n
        1.6735328377636005e-24_rt,  // p
        5.008234515140786e-24_rt,  // He3
        6.646479071584587e-24_rt,  // He4
        1.99264687992e-23_rt,  // C12
        2.6560180592333686e-23_rt,  // O16
        3.3198227947612416e-23_rt,  // Ne20
        3.9828098739467446e-23_rt,  // Mg24
        4.6456779473820677e-23_rt,  // Si28
        5.309087322384128e-23_rt,  // S32
        5.972551377884467e-23_rt,  // Ar36
        6.635944331004904e-23_rt,  // Ca40
        7.299678247096977e-23_rt,  // Ti44
        7.962953983065421e-23_rt,  // Cr48
        8.626187166893794e-23_rt,  // Fe52
        9.289408870379396e-23_rt,  // Ni56
    };
}

namespace Rates
{

    enum NetworkRates
    {
        k_n_to_p_reaclib = 1,
        k_He4_to_n_He3_reaclib = 2,
        k_O16_to_He4_C12_reaclib = 3,
        k_Ne20_to_He4_O16_reaclib = 4,
        k_Mg24_to_He4_Ne20_reaclib = 5,
        k_Si28_to_He4_Mg24_reaclib = 6,
        k_S32_to_He4_Si28_reaclib = 7,
        k_Ar36_to_He4_S32_reaclib = 8,
        k_Ca40_to_He4_Ar36_reaclib = 9,
        k_Ti44_to_He4_Ca40_reaclib = 10,
        k_Cr48_to_He4_Ti44_reaclib = 11,
        k_Fe52_to_He4_Cr48_reaclib = 12,
        k_Ni56_to_He4_Fe52_reaclib = 13,
        k_C12_to_He4_He4_He4_reaclib = 14,
        k_n_He3_to_He4_reaclib = 15,
        k_p_He3_to_He4_reaclib = 16,
        k_He4_C12_to_O16_reaclib = 17,
        k_He4_O16_to_Ne20_reaclib = 18,
        k_He4_Ne20_to_Mg24_reaclib = 19,
        k_He4_Mg24_to_Si28_reaclib = 20,
        k_He4_Si28_to_S32_reaclib = 21,
        k_He4_S32_to_Ar36_reaclib = 22,
        k_He4_Ar36_to_Ca40_reaclib = 23,
        k_He4_Ca40_to_Ti44_reaclib = 24,
        k_He4_Ti44_to_Cr48_reaclib = 25,
        k_He4_Cr48_to_Fe52_reaclib = 26,
        k_He4_Fe52_to_Ni56_reaclib = 27,
        k_C12_C12_to_He4_Ne20_reaclib = 28,
        k_C12_O16_to_He4_Mg24_reaclib = 29,
        k_O16_O16_to_He4_Si28_reaclib = 30,
        k_He4_Ne20_to_C12_C12_reaclib = 31,
        k_C12_Ne20_to_He4_Si28_reaclib = 32,
        k_He4_Mg24_to_C12_O16_reaclib = 33,
        k_He4_Si28_to_C12_Ne20_reaclib = 34,
        k_He4_Si28_to_O16_O16_reaclib = 35,
        k_He3_He3_to_p_p_He4_reaclib = 36,
        k_He4_He4_He4_to_C12_reaclib = 37,
        k_p_p_He4_to_He3_He3_reaclib = 38,
        NumRates = k_p_p_He4_to_He3_He3_reaclib
    };

    enum ScreenPairs
    {
        k_He2_He4 = 1,
        k_He3_He3 = 2,
        k_He4_O16 = 3,
        k_He4_Ca40 = 4,
        k_He4_Ar36 = 5,
        k_He4_C12 = 6,
        k_p_p = 7,
        k_He4_Ti44 = 8,
        k_He4_Mg24 = 9,
        k_C12_O16 = 10,
        k_p_He3 = 11,
        k_He4_Cr48 = 12,
        k_C12_C12 = 13,
        k_He4_Ne20 = 14,
        k_He4_Fe52 = 15,
        k_He4_S32 = 16,
        k_He4_He4 = 17,
        k_He4_Si28 = 18,
        k_C12_Ne20 = 19,
        k_O16_O16 = 20,
        k_He4_Be8 = 21,
        NumScreenPairs = k_He4_Be8
    };

    // rate names -- note: the rates are 1-based, not zero-based, so we pad
    // this vector with rate_names[0] = "" so the indices line up with the
    // NetworkRates enum

    static const std::vector<std::string> rate_names = {
        "",   // 0
        "n_to_p_reaclib",  // 1,
        "He4_to_n_He3_reaclib",  // 2,
        "O16_to_He4_C12_reaclib",  // 3,
        "Ne20_to_He4_O16_reaclib",  // 4,
        "Mg24_to_He4_Ne20_reaclib",  // 5,
        "Si28_to_He4_Mg24_reaclib",  // 6,
        "S32_to_He4_Si28_reaclib",  // 7,
        "Ar36_to_He4_S32_reaclib",  // 8,
        "Ca40_to_He4_Ar36_reaclib",  // 9,
        "Ti44_to_He4_Ca40_reaclib",  // 10,
        "Cr48_to_He4_Ti44_reaclib",  // 11,
        "Fe52_to_He4_Cr48_reaclib",  // 12,
        "Ni56_to_He4_Fe52_reaclib",  // 13,
        "C12_to_He4_He4_He4_reaclib",  // 14,
        "n_He3_to_He4_reaclib",  // 15,
        "p_He3_to_He4_reaclib",  // 16,
        "He4_C12_to_O16_reaclib",  // 17,
        "He4_O16_to_Ne20_reaclib",  // 18,
        "He4_Ne20_to_Mg24_reaclib",  // 19,
        "He4_Mg24_to_Si28_reaclib",  // 20,
        "He4_Si28_to_S32_reaclib",  // 21,
        "He4_S32_to_Ar36_reaclib",  // 22,
        "He4_Ar36_to_Ca40_reaclib",  // 23,
        "He4_Ca40_to_Ti44_reaclib",  // 24,
        "He4_Ti44_to_Cr48_reaclib",  // 25,
        "He4_Cr48_to_Fe52_reaclib",  // 26,
        "He4_Fe52_to_Ni56_reaclib",  // 27,
        "C12_C12_to_He4_Ne20_reaclib",  // 28,
        "C12_O16_to_He4_Mg24_reaclib",  // 29,
        "O16_O16_to_He4_Si28_reaclib",  // 30,
        "He4_Ne20_to_C12_C12_reaclib",  // 31,
        "C12_Ne20_to_He4_Si28_reaclib",  // 32,
        "He4_Mg24_to_C12_O16_reaclib",  // 33,
        "He4_Si28_to_C12_Ne20_reaclib",  // 34,
        "He4_Si28_to_O16_O16_reaclib",  // 35,
        "He3_He3_to_p_p_He4_reaclib",  // 36,
        "He4_He4_He4_to_C12_reaclib",  // 37,
        "p_p_He4_to_He3_He3_reaclib"  // 38,
    };

}

#endif
