#pragma once

#include <array>
#include <span>
#include <stdexcept>
#include <string_view>

// Independent Saha-root DATA, not output sampled from NSESolver.
// Reproduce: validation/network/nse_reference.py (mpmath 70 and 90 digits).
// Nuclear arrays: the declared built-in data; SI/CODATA 2022 constants.
// rho=1e7, T=5e9; initial X_i=(i+1)/(N*(N+1)/2), Ye summed in binary64.
// The boundary-energy case starts with X_0=-1e-12. Energy conversion uses the
// built-in network's declared convention, independently of equilibrium constants.
namespace NseReference {

struct Reference {
    std::string_view name;
    std::span<const double> x;
    double enuc;
    double boundary_enuc;
    double ye;
};

// Nuclear data SHA-256: b736664e9224792c44e7f5a2e1460a048fc06cc8a7db2bf9581e2ede8994de29
inline constexpr std::array<double, 13> aprox13_x{
    0x1.52a34d5c8f570p-5, 0x1.981e8db304641p-23, 0x1.95fa8b94f43a4p-21,
    0x1.37e4db416925cp-27, 0x1.12f5daf653dd1p-18, 0x1.0b6c0bbfc55b0p-7,
    0x1.b914cf9d22b38p-7, 0x1.571564586450ep-7, 0x1.4677220cd3123p-6,
    0x1.c61338f6b37cbp-12, 0x1.d80ec5fb7b9dcp-9, 0x1.abfd4954fc50cp-5,
    0x1.b36269cf87875p-1
};

// Nuclear data SHA-256: 7b005cf191aed2bbfca3db2cdb3605547f38eabb8c657ea204d9888c657a1d04
inline constexpr std::array<double, 19> aprox19_x{
    0x0.0p+0, 0x1.41fa02283df9cp-36, 0x1.49ad0ee430448p-5,
    0x1.78913678bdc4cp-23, 0x1.09761ee5017e6p-39, 0x1.6cadbdae6bcdcp-21,
    0x1.10c02e8813c77p-27, 0x1.d42d82d300003p-19, 0x1.bb4ab2f55cf4dp-8,
    0x1.63e76dfc3ab58p-7, 0x1.0d812404b66dcp-7, 0x1.f353ad100234bp-7,
    0x1.52100e4c01ed2p-12, 0x1.5626630a6e470p-9, 0x1.2dffdd439dda8p-5,
    0x1.183a205f22b2fp-2, 0x1.2b16535b7d9f9p-1, 0x1.70257614cdfbfp-25,
    0x1.58e96fcdcbdbep-6
};

// Nuclear data SHA-256: a30fd8495857e1d7e36ae23b86372704d768dd71fb118070ba6a2876b6c009c7
inline constexpr std::array<double, 21> aprox21_x{
    0x0.0p+0, 0x1.7deed4e584294p-37, 0x1.401b949250a15p-5,
    0x1.58b8d5821670fp-23, 0x1.deeb532258808p-40, 0x1.44265527bf463p-21,
    0x1.d6cdc9dc2bdd4p-28, 0x1.88574f31f1bb0p-19, 0x1.68b4556955157p-8,
    0x1.19313e019e984p-7, 0x1.9d7ff7b2ebfb5p-8, 0x1.73f0b4d2b1c8ap-7,
    0x1.e90478025acddp-13, 0x1.e090b60ac379cp-10, 0x1.c645196e8e024p-57,
    0x1.9bdc79d152b3cp-6, 0x1.0010211487143p-1, 0x1.3b94eae665585p-9,
    0x1.8c0d1f8cd0d8dp-2, 0x1.2d58c3f30c293p-24, 0x1.9f357269f7198p-7
};

// Nuclear data SHA-256: 4b03c2c59c7e06ef0f89d38780df13f1d0b460caf6005163de18900b4d1f0a5f
inline constexpr std::array<double, 7> iso7_x{
    0x1.555232862be6ep-5, 0x1.a1e5c7556d82dp-23, 0x1.a2ffdc3eea32bp-21,
    0x1.44729ef418aecp-27, 0x1.204b3c250226fp-18, 0x1.1a9c96cbfe0cfp-7,
    0x1.e63fb94e1dadep-1
};

inline constexpr Reference cases[]{
    {"aprox13", aprox13_x, 0x1.63ac1e71b0764p+56, 0x1.371202b75ec33p+57, 0x1.0000000000000p-1},
    {"aprox19", aprox19_x, 0x1.74921cfa23cc8p+60, 0x1.74921cfa23cc8p+60, 0x1.02cb6719402cbp-1},
    {"aprox21", aprox21_x, 0x1.5b2611dcc1458p+60, 0x1.5b2611dcc1458p+60, 0x1.fcea6d7f511fap-2},
    {"iso7", iso7_x, 0x1.17be2d38f33a6p+58, 0x1.f03f98e653425p+58, 0x1.0000000000000p-1},
};

inline const Reference& find(std::string_view name)
{
    for (const auto& reference : cases)
        if (reference.name == name) return reference;
    throw std::invalid_argument("missing independently reviewed NSE reference");
}

} // namespace NseReference
