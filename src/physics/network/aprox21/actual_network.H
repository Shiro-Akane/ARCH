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
        k_C12_to_He4_He4_He4_reaclib = 5,
        k_n_He3_to_He4_reaclib = 6,
        k_p_He3_to_He4_reaclib = 7,
        k_He4_C12_to_O16_reaclib = 8,
        k_He4_O16_to_Ne20_reaclib = 9,
        k_C12_C12_to_He4_Ne20_reaclib = 10,
        k_C12_O16_to_He4_Mg24_reaclib = 11,
        k_O16_O16_to_He4_Si28_reaclib = 12,
        k_He4_Ne20_to_C12_C12_reaclib = 13,
        k_C12_Ne20_to_He4_Si28_reaclib = 14,
        k_He4_Mg24_to_C12_O16_reaclib = 15,
        k_He4_Si28_to_C12_Ne20_reaclib = 16,
        k_He4_Si28_to_O16_O16_reaclib = 17,
        k_He3_He3_to_p_p_He4_reaclib = 18,
        k_He4_He4_He4_to_C12_reaclib = 19,
        k_p_p_He4_to_He3_He3_reaclib = 20,
        k_He4_Ne20_to_Mg24_reaclib = 21,
        k_He4_Ne20_to_p_Na23_reaclib = 22,
        k_p_Na23_to_Mg24_reaclib = 23,
        k_Mg24_to_He4_Ne20_reaclib = 24,
        k_Mg24_to_p_Na23_reaclib = 25,
        k_p_Na23_to_He4_Ne20_reaclib = 26,
        k_He4_Mg24_to_Si28_reaclib = 27,
        k_He4_Mg24_to_p_Al27_reaclib = 28,
        k_p_Al27_to_Si28_reaclib = 29,
        k_Si28_to_He4_Mg24_reaclib = 30,
        k_Si28_to_p_Al27_reaclib = 31,
        k_p_Al27_to_He4_Mg24_reaclib = 32,
        k_He4_Si28_to_S32_reaclib = 33,
        k_He4_Si28_to_p_P31_reaclib = 34,
        k_p_P31_to_S32_reaclib = 35,
        k_S32_to_He4_Si28_reaclib = 36,
        k_S32_to_p_P31_reaclib = 37,
        k_p_P31_to_He4_Si28_reaclib = 38,
        k_He4_S32_to_Ar36_reaclib = 39,
        k_He4_S32_to_p_Cl35_reaclib = 40,
        k_p_Cl35_to_Ar36_reaclib = 41,
        k_Ar36_to_He4_S32_reaclib = 42,
        k_Ar36_to_p_Cl35_reaclib = 43,
        k_p_Cl35_to_He4_S32_reaclib = 44,
        k_He4_Ar36_to_Ca40_reaclib = 45,
        k_He4_Ar36_to_p_K39_reaclib = 46,
        k_p_K39_to_Ca40_reaclib = 47,
        k_Ca40_to_He4_Ar36_reaclib = 48,
        k_Ca40_to_p_K39_reaclib = 49,
        k_p_K39_to_He4_Ar36_reaclib = 50,
        k_He4_Ca40_to_Ti44_reaclib = 51,
        k_He4_Ca40_to_p_Sc43_reaclib = 52,
        k_p_Sc43_to_Ti44_reaclib = 53,
        k_Ti44_to_He4_Ca40_reaclib = 54,
        k_Ti44_to_p_Sc43_reaclib = 55,
        k_p_Sc43_to_He4_Ca40_reaclib = 56,
        k_He4_Ti44_to_Cr48_reaclib = 57,
        k_He4_Ti44_to_p_V47_reaclib = 58,
        k_p_V47_to_Cr48_reaclib = 59,
        k_Cr48_to_He4_Ti44_reaclib = 60,
        k_Cr48_to_p_V47_reaclib = 61,
        k_p_V47_to_He4_Ti44_reaclib = 62,
        k_He4_Cr48_to_Fe52_reaclib = 63,
        k_He4_Cr48_to_p_Mn51_reaclib = 64,
        k_p_Mn51_to_Fe52_reaclib = 65,
        k_Fe52_to_He4_Cr48_reaclib = 66,
        k_Fe52_to_p_Mn51_reaclib = 67,
        k_p_Mn51_to_He4_Cr48_reaclib = 68,
        k_He4_Fe52_to_Ni56_reaclib = 69,
        k_He4_Fe52_to_p_Co55_reaclib = 70,
        k_p_Co55_to_Ni56_reaclib = 71,
        k_Ni56_to_He4_Fe52_reaclib = 72,
        k_Ni56_to_p_Co55_reaclib = 73,
        k_p_Co55_to_He4_Fe52_reaclib = 74,
        k_Ne20_He4_to_Mg24_approx = 75,
        k_Mg24_to_Ne20_He4_approx = 76,
        k_Mg24_He4_to_Si28_approx = 77,
        k_Si28_to_Mg24_He4_approx = 78,
        k_Si28_He4_to_S32_approx = 79,
        k_S32_to_Si28_He4_approx = 80,
        k_S32_He4_to_Ar36_approx = 81,
        k_Ar36_to_S32_He4_approx = 82,
        k_Ar36_He4_to_Ca40_approx = 83,
        k_Ca40_to_Ar36_He4_approx = 84,
        k_Ca40_He4_to_Ti44_approx = 85,
        k_Ti44_to_Ca40_He4_approx = 86,
        k_Ti44_He4_to_Cr48_approx = 87,
        k_Cr48_to_Ti44_He4_approx = 88,
        k_Cr48_He4_to_Fe52_approx = 89,
        k_Fe52_to_Cr48_He4_approx = 90,
        k_Fe52_He4_to_Ni56_approx = 91,
        k_Ni56_to_Fe52_He4_approx = 92,
        NumRates = k_Ni56_to_Fe52_He4_approx
    };

    enum ScreenPairs
    {
        k_He2_He4 = 1,
        k_He3_He3 = 2,
        k_He4_O16 = 3,
        k_p_K39 = 4,
        k_He4_Ca40 = 5,
        k_He4_Ar36 = 6,
        k_p_Sc43 = 7,
        k_He4_C12 = 8,
        k_p_p = 9,
        k_He4_Ti44 = 10,
        k_He4_Mg24 = 11,
        k_C12_O16 = 12,
        k_p_V47 = 13,
        k_p_He3 = 14,
        k_He4_Cr48 = 15,
        k_C12_C12 = 16,
        k_p_Co55 = 17,
        k_He4_Ne20 = 18,
        k_p_Al27 = 19,
        k_p_Mn51 = 20,
        k_p_Na23 = 21,
        k_He4_Fe52 = 22,
        k_He4_S32 = 23,
        k_p_P31 = 24,
        k_He4_He4 = 25,
        k_He4_Si28 = 26,
        k_C12_Ne20 = 27,
        k_p_Cl35 = 28,
        k_O16_O16 = 29,
        k_He4_Be8 = 30,
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
        "C12_to_He4_He4_He4_reaclib",  // 5,
        "n_He3_to_He4_reaclib",  // 6,
        "p_He3_to_He4_reaclib",  // 7,
        "He4_C12_to_O16_reaclib",  // 8,
        "He4_O16_to_Ne20_reaclib",  // 9,
        "C12_C12_to_He4_Ne20_reaclib",  // 10,
        "C12_O16_to_He4_Mg24_reaclib",  // 11,
        "O16_O16_to_He4_Si28_reaclib",  // 12,
        "He4_Ne20_to_C12_C12_reaclib",  // 13,
        "C12_Ne20_to_He4_Si28_reaclib",  // 14,
        "He4_Mg24_to_C12_O16_reaclib",  // 15,
        "He4_Si28_to_C12_Ne20_reaclib",  // 16,
        "He4_Si28_to_O16_O16_reaclib",  // 17,
        "He3_He3_to_p_p_He4_reaclib",  // 18,
        "He4_He4_He4_to_C12_reaclib",  // 19,
        "p_p_He4_to_He3_He3_reaclib",  // 20,
        "He4_Ne20_to_Mg24_reaclib",  // 21,
        "He4_Ne20_to_p_Na23_reaclib",  // 22,
        "p_Na23_to_Mg24_reaclib",  // 23,
        "Mg24_to_He4_Ne20_reaclib",  // 24,
        "Mg24_to_p_Na23_reaclib",  // 25,
        "p_Na23_to_He4_Ne20_reaclib",  // 26,
        "He4_Mg24_to_Si28_reaclib",  // 27,
        "He4_Mg24_to_p_Al27_reaclib",  // 28,
        "p_Al27_to_Si28_reaclib",  // 29,
        "Si28_to_He4_Mg24_reaclib",  // 30,
        "Si28_to_p_Al27_reaclib",  // 31,
        "p_Al27_to_He4_Mg24_reaclib",  // 32,
        "He4_Si28_to_S32_reaclib",  // 33,
        "He4_Si28_to_p_P31_reaclib",  // 34,
        "p_P31_to_S32_reaclib",  // 35,
        "S32_to_He4_Si28_reaclib",  // 36,
        "S32_to_p_P31_reaclib",  // 37,
        "p_P31_to_He4_Si28_reaclib",  // 38,
        "He4_S32_to_Ar36_reaclib",  // 39,
        "He4_S32_to_p_Cl35_reaclib",  // 40,
        "p_Cl35_to_Ar36_reaclib",  // 41,
        "Ar36_to_He4_S32_reaclib",  // 42,
        "Ar36_to_p_Cl35_reaclib",  // 43,
        "p_Cl35_to_He4_S32_reaclib",  // 44,
        "He4_Ar36_to_Ca40_reaclib",  // 45,
        "He4_Ar36_to_p_K39_reaclib",  // 46,
        "p_K39_to_Ca40_reaclib",  // 47,
        "Ca40_to_He4_Ar36_reaclib",  // 48,
        "Ca40_to_p_K39_reaclib",  // 49,
        "p_K39_to_He4_Ar36_reaclib",  // 50,
        "He4_Ca40_to_Ti44_reaclib",  // 51,
        "He4_Ca40_to_p_Sc43_reaclib",  // 52,
        "p_Sc43_to_Ti44_reaclib",  // 53,
        "Ti44_to_He4_Ca40_reaclib",  // 54,
        "Ti44_to_p_Sc43_reaclib",  // 55,
        "p_Sc43_to_He4_Ca40_reaclib",  // 56,
        "He4_Ti44_to_Cr48_reaclib",  // 57,
        "He4_Ti44_to_p_V47_reaclib",  // 58,
        "p_V47_to_Cr48_reaclib",  // 59,
        "Cr48_to_He4_Ti44_reaclib",  // 60,
        "Cr48_to_p_V47_reaclib",  // 61,
        "p_V47_to_He4_Ti44_reaclib",  // 62,
        "He4_Cr48_to_Fe52_reaclib",  // 63,
        "He4_Cr48_to_p_Mn51_reaclib",  // 64,
        "p_Mn51_to_Fe52_reaclib",  // 65,
        "Fe52_to_He4_Cr48_reaclib",  // 66,
        "Fe52_to_p_Mn51_reaclib",  // 67,
        "p_Mn51_to_He4_Cr48_reaclib",  // 68,
        "He4_Fe52_to_Ni56_reaclib",  // 69,
        "He4_Fe52_to_p_Co55_reaclib",  // 70,
        "p_Co55_to_Ni56_reaclib",  // 71,
        "Ni56_to_He4_Fe52_reaclib",  // 72,
        "Ni56_to_p_Co55_reaclib",  // 73,
        "p_Co55_to_He4_Fe52_reaclib",  // 74,
        "Ne20_He4_to_Mg24_approx",  // 75,
        "Mg24_to_Ne20_He4_approx",  // 76,
        "Mg24_He4_to_Si28_approx",  // 77,
        "Si28_to_Mg24_He4_approx",  // 78,
        "Si28_He4_to_S32_approx",  // 79,
        "S32_to_Si28_He4_approx",  // 80,
        "S32_He4_to_Ar36_approx",  // 81,
        "Ar36_to_S32_He4_approx",  // 82,
        "Ar36_He4_to_Ca40_approx",  // 83,
        "Ca40_to_Ar36_He4_approx",  // 84,
        "Ca40_He4_to_Ti44_approx",  // 85,
        "Ti44_to_Ca40_He4_approx",  // 86,
        "Ti44_He4_to_Cr48_approx",  // 87,
        "Cr48_to_Ti44_He4_approx",  // 88,
        "Cr48_He4_to_Fe52_approx",  // 89,
        "Fe52_to_Cr48_He4_approx",  // 90,
        "Fe52_He4_to_Ni56_approx",  // 91,
        "Ni56_to_Fe52_He4_approx"  // 92,
    };

}

#endif
