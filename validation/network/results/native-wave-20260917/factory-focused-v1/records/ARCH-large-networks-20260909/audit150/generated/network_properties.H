#ifndef ARCH_GENERATED_AUDIT150_NETWORK_PROPERTIES_H
#define ARCH_GENERATED_AUDIT150_NETWORK_PROPERTIES_H
#include <vector>
#include <string>
#include "amrex_bridge.H"

constexpr int NumSpec = 150;

constexpr int NumSpecExtra = 0;

constexpr int NumSpecTotal = NumSpec + NumSpecExtra;

// Note: these are 0-based
struct aion_lookup {
    ARCH_HOST_DEVICE static constexpr int size() { return 150; }
    ARCH_HOST_DEVICE static constexpr int lo() { return 0; }
    ARCH_HOST_DEVICE static constexpr int hi() { return lo() + size() - 1; }
    // Small immutable lookups stay inlineable on both backends.
    ARCH_HOST_DEVICE constexpr Real operator()(int index) const {
        switch (index - lo()) {
        case 0: return 1.0;
        case 1: return 1.0;
        case 2: return 4.0;
        case 3: return 11.0;
        case 4: return 12.0;
        case 5: return 13.0;
        case 6: return 14.0;
        case 7: return 15.0;
        case 8: return 12.0;
        case 9: return 13.0;
        case 10: return 14.0;
        case 11: return 15.0;
        case 12: return 16.0;
        case 13: return 17.0;
        case 14: return 15.0;
        case 15: return 16.0;
        case 16: return 17.0;
        case 17: return 18.0;
        case 18: return 19.0;
        case 19: return 20.0;
        case 20: return 17.0;
        case 21: return 18.0;
        case 22: return 19.0;
        case 23: return 20.0;
        case 24: return 21.0;
        case 25: return 22.0;
        case 26: return 19.0;
        case 27: return 20.0;
        case 28: return 21.0;
        case 29: return 22.0;
        case 30: return 23.0;
        case 31: return 24.0;
        case 32: return 21.0;
        case 33: return 22.0;
        case 34: return 23.0;
        case 35: return 24.0;
        case 36: return 25.0;
        case 37: return 26.0;
        case 38: return 23.0;
        case 39: return 24.0;
        case 40: return 25.0;
        case 41: return 26.0;
        case 42: return 27.0;
        case 43: return 28.0;
        case 44: return 25.0;
        case 45: return 26.0;
        case 46: return 27.0;
        case 47: return 28.0;
        case 48: return 29.0;
        case 49: return 30.0;
        case 50: return 28.0;
        case 51: return 29.0;
        case 52: return 30.0;
        case 53: return 31.0;
        case 54: return 32.0;
        case 55: return 33.0;
        case 56: return 30.0;
        case 57: return 31.0;
        case 58: return 32.0;
        case 59: return 33.0;
        case 60: return 34.0;
        case 61: return 35.0;
        case 62: return 32.0;
        case 63: return 33.0;
        case 64: return 34.0;
        case 65: return 35.0;
        case 66: return 36.0;
        case 67: return 37.0;
        case 68: return 34.0;
        case 69: return 35.0;
        case 70: return 36.0;
        case 71: return 37.0;
        case 72: return 38.0;
        case 73: return 39.0;
        case 74: return 37.0;
        case 75: return 38.0;
        case 76: return 39.0;
        case 77: return 40.0;
        case 78: return 41.0;
        case 79: return 42.0;
        case 80: return 39.0;
        case 81: return 40.0;
        case 82: return 41.0;
        case 83: return 42.0;
        case 84: return 43.0;
        case 85: return 44.0;
        case 86: return 41.0;
        case 87: return 42.0;
        case 88: return 43.0;
        case 89: return 44.0;
        case 90: return 45.0;
        case 91: return 46.0;
        case 92: return 44.0;
        case 93: return 45.0;
        case 94: return 46.0;
        case 95: return 47.0;
        case 96: return 48.0;
        case 97: return 49.0;
        case 98: return 46.0;
        case 99: return 47.0;
        case 100: return 48.0;
        case 101: return 49.0;
        case 102: return 50.0;
        case 103: return 51.0;
        case 104: return 48.0;
        case 105: return 49.0;
        case 106: return 50.0;
        case 107: return 51.0;
        case 108: return 52.0;
        case 109: return 53.0;
        case 110: return 51.0;
        case 111: return 52.0;
        case 112: return 53.0;
        case 113: return 54.0;
        case 114: return 55.0;
        case 115: return 56.0;
        case 116: return 53.0;
        case 117: return 54.0;
        case 118: return 55.0;
        case 119: return 56.0;
        case 120: return 57.0;
        case 121: return 58.0;
        case 122: return 55.0;
        case 123: return 56.0;
        case 124: return 57.0;
        case 125: return 58.0;
        case 126: return 59.0;
        case 127: return 60.0;
        case 128: return 58.0;
        case 129: return 59.0;
        case 130: return 60.0;
        case 131: return 61.0;
        case 132: return 62.0;
        case 133: return 63.0;
        case 134: return 60.0;
        case 135: return 61.0;
        case 136: return 62.0;
        case 137: return 63.0;
        case 138: return 64.0;
        case 139: return 65.0;
        case 140: return 63.0;
        case 141: return 64.0;
        case 142: return 65.0;
        case 143: return 66.0;
        case 144: return 67.0;
        case 145: return 68.0;
        case 146: return 66.0;
        case 147: return 67.0;
        case 148: return 68.0;
        case 149: return 10.0;
        default: assert(false); return 0.0_rt;
        }
    }
    ARCH_HOST_DEVICE constexpr Real operator[](int index) const { return (*this)(index); }
};
ARCH_HOST_DEVICE inline constexpr aion_lookup aion_values() { return {}; }

struct aion_inv_lookup {
    ARCH_HOST_DEVICE static constexpr int size() { return 150; }
    ARCH_HOST_DEVICE static constexpr int lo() { return 0; }
    ARCH_HOST_DEVICE static constexpr int hi() { return lo() + size() - 1; }
    // Small immutable lookups stay inlineable on both backends.
    ARCH_HOST_DEVICE constexpr Real operator()(int index) const {
        switch (index - lo()) {
        case 0: return 1.0/   1.0;
        case 1: return 1.0/   1.0;
        case 2: return 1.0/   4.0;
        case 3: return 1.0/  11.0;
        case 4: return 1.0/  12.0;
        case 5: return 1.0/  13.0;
        case 6: return 1.0/  14.0;
        case 7: return 1.0/  15.0;
        case 8: return 1.0/  12.0;
        case 9: return 1.0/  13.0;
        case 10: return 1.0/  14.0;
        case 11: return 1.0/  15.0;
        case 12: return 1.0/  16.0;
        case 13: return 1.0/  17.0;
        case 14: return 1.0/  15.0;
        case 15: return 1.0/  16.0;
        case 16: return 1.0/  17.0;
        case 17: return 1.0/  18.0;
        case 18: return 1.0/  19.0;
        case 19: return 1.0/  20.0;
        case 20: return 1.0/  17.0;
        case 21: return 1.0/  18.0;
        case 22: return 1.0/  19.0;
        case 23: return 1.0/  20.0;
        case 24: return 1.0/  21.0;
        case 25: return 1.0/  22.0;
        case 26: return 1.0/  19.0;
        case 27: return 1.0/  20.0;
        case 28: return 1.0/  21.0;
        case 29: return 1.0/  22.0;
        case 30: return 1.0/  23.0;
        case 31: return 1.0/  24.0;
        case 32: return 1.0/  21.0;
        case 33: return 1.0/  22.0;
        case 34: return 1.0/  23.0;
        case 35: return 1.0/  24.0;
        case 36: return 1.0/  25.0;
        case 37: return 1.0/  26.0;
        case 38: return 1.0/  23.0;
        case 39: return 1.0/  24.0;
        case 40: return 1.0/  25.0;
        case 41: return 1.0/  26.0;
        case 42: return 1.0/  27.0;
        case 43: return 1.0/  28.0;
        case 44: return 1.0/  25.0;
        case 45: return 1.0/  26.0;
        case 46: return 1.0/  27.0;
        case 47: return 1.0/  28.0;
        case 48: return 1.0/  29.0;
        case 49: return 1.0/  30.0;
        case 50: return 1.0/  28.0;
        case 51: return 1.0/  29.0;
        case 52: return 1.0/  30.0;
        case 53: return 1.0/  31.0;
        case 54: return 1.0/  32.0;
        case 55: return 1.0/  33.0;
        case 56: return 1.0/  30.0;
        case 57: return 1.0/  31.0;
        case 58: return 1.0/  32.0;
        case 59: return 1.0/  33.0;
        case 60: return 1.0/  34.0;
        case 61: return 1.0/  35.0;
        case 62: return 1.0/  32.0;
        case 63: return 1.0/  33.0;
        case 64: return 1.0/  34.0;
        case 65: return 1.0/  35.0;
        case 66: return 1.0/  36.0;
        case 67: return 1.0/  37.0;
        case 68: return 1.0/  34.0;
        case 69: return 1.0/  35.0;
        case 70: return 1.0/  36.0;
        case 71: return 1.0/  37.0;
        case 72: return 1.0/  38.0;
        case 73: return 1.0/  39.0;
        case 74: return 1.0/  37.0;
        case 75: return 1.0/  38.0;
        case 76: return 1.0/  39.0;
        case 77: return 1.0/  40.0;
        case 78: return 1.0/  41.0;
        case 79: return 1.0/  42.0;
        case 80: return 1.0/  39.0;
        case 81: return 1.0/  40.0;
        case 82: return 1.0/  41.0;
        case 83: return 1.0/  42.0;
        case 84: return 1.0/  43.0;
        case 85: return 1.0/  44.0;
        case 86: return 1.0/  41.0;
        case 87: return 1.0/  42.0;
        case 88: return 1.0/  43.0;
        case 89: return 1.0/  44.0;
        case 90: return 1.0/  45.0;
        case 91: return 1.0/  46.0;
        case 92: return 1.0/  44.0;
        case 93: return 1.0/  45.0;
        case 94: return 1.0/  46.0;
        case 95: return 1.0/  47.0;
        case 96: return 1.0/  48.0;
        case 97: return 1.0/  49.0;
        case 98: return 1.0/  46.0;
        case 99: return 1.0/  47.0;
        case 100: return 1.0/  48.0;
        case 101: return 1.0/  49.0;
        case 102: return 1.0/  50.0;
        case 103: return 1.0/  51.0;
        case 104: return 1.0/  48.0;
        case 105: return 1.0/  49.0;
        case 106: return 1.0/  50.0;
        case 107: return 1.0/  51.0;
        case 108: return 1.0/  52.0;
        case 109: return 1.0/  53.0;
        case 110: return 1.0/  51.0;
        case 111: return 1.0/  52.0;
        case 112: return 1.0/  53.0;
        case 113: return 1.0/  54.0;
        case 114: return 1.0/  55.0;
        case 115: return 1.0/  56.0;
        case 116: return 1.0/  53.0;
        case 117: return 1.0/  54.0;
        case 118: return 1.0/  55.0;
        case 119: return 1.0/  56.0;
        case 120: return 1.0/  57.0;
        case 121: return 1.0/  58.0;
        case 122: return 1.0/  55.0;
        case 123: return 1.0/  56.0;
        case 124: return 1.0/  57.0;
        case 125: return 1.0/  58.0;
        case 126: return 1.0/  59.0;
        case 127: return 1.0/  60.0;
        case 128: return 1.0/  58.0;
        case 129: return 1.0/  59.0;
        case 130: return 1.0/  60.0;
        case 131: return 1.0/  61.0;
        case 132: return 1.0/  62.0;
        case 133: return 1.0/  63.0;
        case 134: return 1.0/  60.0;
        case 135: return 1.0/  61.0;
        case 136: return 1.0/  62.0;
        case 137: return 1.0/  63.0;
        case 138: return 1.0/  64.0;
        case 139: return 1.0/  65.0;
        case 140: return 1.0/  63.0;
        case 141: return 1.0/  64.0;
        case 142: return 1.0/  65.0;
        case 143: return 1.0/  66.0;
        case 144: return 1.0/  67.0;
        case 145: return 1.0/  68.0;
        case 146: return 1.0/  66.0;
        case 147: return 1.0/  67.0;
        case 148: return 1.0/  68.0;
        case 149: return 1.0/  10.0;
        default: assert(false); return 0.0_rt;
        }
    }
    ARCH_HOST_DEVICE constexpr Real operator[](int index) const { return (*this)(index); }
};
ARCH_HOST_DEVICE inline constexpr aion_inv_lookup aion_inv_values() { return {}; }

struct zion_lookup {
    ARCH_HOST_DEVICE static constexpr int size() { return 150; }
    ARCH_HOST_DEVICE static constexpr int lo() { return 0; }
    ARCH_HOST_DEVICE static constexpr int hi() { return lo() + size() - 1; }
    // Small immutable lookups stay inlineable on both backends.
    ARCH_HOST_DEVICE constexpr Real operator()(int index) const {
        switch (index - lo()) {
        case 0: return 0.0;
        case 1: return 1.0;
        case 2: return 2.0;
        case 3: return 6.0;
        case 4: return 6.0;
        case 5: return 6.0;
        case 6: return 6.0;
        case 7: return 6.0;
        case 8: return 7.0;
        case 9: return 7.0;
        case 10: return 7.0;
        case 11: return 7.0;
        case 12: return 7.0;
        case 13: return 7.0;
        case 14: return 8.0;
        case 15: return 8.0;
        case 16: return 8.0;
        case 17: return 8.0;
        case 18: return 8.0;
        case 19: return 8.0;
        case 20: return 9.0;
        case 21: return 9.0;
        case 22: return 9.0;
        case 23: return 9.0;
        case 24: return 9.0;
        case 25: return 9.0;
        case 26: return 10.0;
        case 27: return 10.0;
        case 28: return 10.0;
        case 29: return 10.0;
        case 30: return 10.0;
        case 31: return 10.0;
        case 32: return 11.0;
        case 33: return 11.0;
        case 34: return 11.0;
        case 35: return 11.0;
        case 36: return 11.0;
        case 37: return 11.0;
        case 38: return 12.0;
        case 39: return 12.0;
        case 40: return 12.0;
        case 41: return 12.0;
        case 42: return 12.0;
        case 43: return 12.0;
        case 44: return 13.0;
        case 45: return 13.0;
        case 46: return 13.0;
        case 47: return 13.0;
        case 48: return 13.0;
        case 49: return 13.0;
        case 50: return 14.0;
        case 51: return 14.0;
        case 52: return 14.0;
        case 53: return 14.0;
        case 54: return 14.0;
        case 55: return 14.0;
        case 56: return 15.0;
        case 57: return 15.0;
        case 58: return 15.0;
        case 59: return 15.0;
        case 60: return 15.0;
        case 61: return 15.0;
        case 62: return 16.0;
        case 63: return 16.0;
        case 64: return 16.0;
        case 65: return 16.0;
        case 66: return 16.0;
        case 67: return 16.0;
        case 68: return 17.0;
        case 69: return 17.0;
        case 70: return 17.0;
        case 71: return 17.0;
        case 72: return 17.0;
        case 73: return 17.0;
        case 74: return 18.0;
        case 75: return 18.0;
        case 76: return 18.0;
        case 77: return 18.0;
        case 78: return 18.0;
        case 79: return 18.0;
        case 80: return 19.0;
        case 81: return 19.0;
        case 82: return 19.0;
        case 83: return 19.0;
        case 84: return 19.0;
        case 85: return 19.0;
        case 86: return 20.0;
        case 87: return 20.0;
        case 88: return 20.0;
        case 89: return 20.0;
        case 90: return 20.0;
        case 91: return 20.0;
        case 92: return 21.0;
        case 93: return 21.0;
        case 94: return 21.0;
        case 95: return 21.0;
        case 96: return 21.0;
        case 97: return 21.0;
        case 98: return 22.0;
        case 99: return 22.0;
        case 100: return 22.0;
        case 101: return 22.0;
        case 102: return 22.0;
        case 103: return 22.0;
        case 104: return 23.0;
        case 105: return 23.0;
        case 106: return 23.0;
        case 107: return 23.0;
        case 108: return 23.0;
        case 109: return 23.0;
        case 110: return 24.0;
        case 111: return 24.0;
        case 112: return 24.0;
        case 113: return 24.0;
        case 114: return 24.0;
        case 115: return 24.0;
        case 116: return 25.0;
        case 117: return 25.0;
        case 118: return 25.0;
        case 119: return 25.0;
        case 120: return 25.0;
        case 121: return 25.0;
        case 122: return 26.0;
        case 123: return 26.0;
        case 124: return 26.0;
        case 125: return 26.0;
        case 126: return 26.0;
        case 127: return 26.0;
        case 128: return 27.0;
        case 129: return 27.0;
        case 130: return 27.0;
        case 131: return 27.0;
        case 132: return 27.0;
        case 133: return 27.0;
        case 134: return 28.0;
        case 135: return 28.0;
        case 136: return 28.0;
        case 137: return 28.0;
        case 138: return 28.0;
        case 139: return 28.0;
        case 140: return 29.0;
        case 141: return 29.0;
        case 142: return 29.0;
        case 143: return 29.0;
        case 144: return 29.0;
        case 145: return 29.0;
        case 146: return 30.0;
        case 147: return 30.0;
        case 148: return 30.0;
        case 149: return 6.0;
        default: assert(false); return 0.0_rt;
        }
    }
    ARCH_HOST_DEVICE constexpr Real operator[](int index) const { return (*this)(index); }
};
ARCH_HOST_DEVICE inline constexpr zion_lookup zion_values() { return {}; }

static const std::vector<std::string> spec_names = {
    "N", // 0
    "H1", // 1
    "He4", // 2
    "C11", // 3
    "C12", // 4
    "C13", // 5
    "C14", // 6
    "C15", // 7
    "N12", // 8
    "N13", // 9
    "N14", // 10
    "N15", // 11
    "N16", // 12
    "N17", // 13
    "O15", // 14
    "O16", // 15
    "O17", // 16
    "O18", // 17
    "O19", // 18
    "O20", // 19
    "F17", // 20
    "F18", // 21
    "F19", // 22
    "F20", // 23
    "F21", // 24
    "F22", // 25
    "Ne19", // 26
    "Ne20", // 27
    "Ne21", // 28
    "Ne22", // 29
    "Ne23", // 30
    "Ne24", // 31
    "Na21", // 32
    "Na22", // 33
    "Na23", // 34
    "Na24", // 35
    "Na25", // 36
    "Na26", // 37
    "Mg23", // 38
    "Mg24", // 39
    "Mg25", // 40
    "Mg26", // 41
    "Mg27", // 42
    "Mg28", // 43
    "Al25", // 44
    "Al26", // 45
    "Al27", // 46
    "Al28", // 47
    "Al29", // 48
    "Al30", // 49
    "Si28", // 50
    "Si29", // 51
    "Si30", // 52
    "Si31", // 53
    "Si32", // 54
    "Si33", // 55
    "P30", // 56
    "P31", // 57
    "P32", // 58
    "P33", // 59
    "P34", // 60
    "P35", // 61
    "S32", // 62
    "S33", // 63
    "S34", // 64
    "S35", // 65
    "S36", // 66
    "S37", // 67
    "Cl34", // 68
    "Cl35", // 69
    "Cl36", // 70
    "Cl37", // 71
    "Cl38", // 72
    "Cl39", // 73
    "Ar37", // 74
    "Ar38", // 75
    "Ar39", // 76
    "Ar40", // 77
    "Ar41", // 78
    "Ar42", // 79
    "K39", // 80
    "K40", // 81
    "K41", // 82
    "K42", // 83
    "K43", // 84
    "K44", // 85
    "Ca41", // 86
    "Ca42", // 87
    "Ca43", // 88
    "Ca44", // 89
    "Ca45", // 90
    "Ca46", // 91
    "Sc44", // 92
    "Sc45", // 93
    "Sc46", // 94
    "Sc47", // 95
    "Sc48", // 96
    "Sc49", // 97
    "Ti46", // 98
    "Ti47", // 99
    "Ti48", // 100
    "Ti49", // 101
    "Ti50", // 102
    "Ti51", // 103
    "V48", // 104
    "V49", // 105
    "V50", // 106
    "V51", // 107
    "V52", // 108
    "V53", // 109
    "Cr51", // 110
    "Cr52", // 111
    "Cr53", // 112
    "Cr54", // 113
    "Cr55", // 114
    "Cr56", // 115
    "Mn53", // 116
    "Mn54", // 117
    "Mn55", // 118
    "Mn56", // 119
    "Mn57", // 120
    "Mn58", // 121
    "Fe55", // 122
    "Fe56", // 123
    "Fe57", // 124
    "Fe58", // 125
    "Fe59", // 126
    "Fe60", // 127
    "Co58", // 128
    "Co59", // 129
    "Co60", // 130
    "Co61", // 131
    "Co62", // 132
    "Co63", // 133
    "Ni60", // 134
    "Ni61", // 135
    "Ni62", // 136
    "Ni63", // 137
    "Ni64", // 138
    "Ni65", // 139
    "Cu63", // 140
    "Cu64", // 141
    "Cu65", // 142
    "Cu66", // 143
    "Cu67", // 144
    "Cu68", // 145
    "Zn66", // 146
    "Zn67", // 147
    "Zn68", // 148
    "C10", // 149
 };

namespace Species {
  enum NetworkSpecies {
    N=1,
    H1,
    He4,
    C11,
    C12,
    C13,
    C14,
    C15,
    N12,
    N13,
    N14,
    N15,
    N16,
    N17,
    O15,
    O16,
    O17,
    O18,
    O19,
    O20,
    F17,
    F18,
    F19,
    F20,
    F21,
    F22,
    Ne19,
    Ne20,
    Ne21,
    Ne22,
    Ne23,
    Ne24,
    Na21,
    Na22,
    Na23,
    Na24,
    Na25,
    Na26,
    Mg23,
    Mg24,
    Mg25,
    Mg26,
    Mg27,
    Mg28,
    Al25,
    Al26,
    Al27,
    Al28,
    Al29,
    Al30,
    Si28,
    Si29,
    Si30,
    Si31,
    Si32,
    Si33,
    P30,
    P31,
    P32,
    P33,
    P34,
    P35,
    S32,
    S33,
    S34,
    S35,
    S36,
    S37,
    Cl34,
    Cl35,
    Cl36,
    Cl37,
    Cl38,
    Cl39,
    Ar37,
    Ar38,
    Ar39,
    Ar40,
    Ar41,
    Ar42,
    K39,
    K40,
    K41,
    K42,
    K43,
    K44,
    Ca41,
    Ca42,
    Ca43,
    Ca44,
    Ca45,
    Ca46,
    Sc44,
    Sc45,
    Sc46,
    Sc47,
    Sc48,
    Sc49,
    Ti46,
    Ti47,
    Ti48,
    Ti49,
    Ti50,
    Ti51,
    V48,
    V49,
    V50,
    V51,
    V52,
    V53,
    Cr51,
    Cr52,
    Cr53,
    Cr54,
    Cr55,
    Cr56,
    Mn53,
    Mn54,
    Mn55,
    Mn56,
    Mn57,
    Mn58,
    Fe55,
    Fe56,
    Fe57,
    Fe58,
    Fe59,
    Fe60,
    Co58,
    Co59,
    Co60,
    Co61,
    Co62,
    Co63,
    Ni60,
    Ni61,
    Ni62,
    Ni63,
    Ni64,
    Ni65,
    Cu63,
    Cu64,
    Cu65,
    Cu66,
    Cu67,
    Cu68,
    Zn66,
    Zn67,
    Zn68,
    C10,
  };
}

#endif
