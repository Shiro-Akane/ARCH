#ifndef ARCH_GENERATED_AUDIT200_NETWORK_PROPERTIES_H
#define ARCH_GENERATED_AUDIT200_NETWORK_PROPERTIES_H
#include <vector>
#include <string>
#include "amrex_bridge.H"

constexpr int NumSpec = 200;

constexpr int NumSpecExtra = 0;

constexpr int NumSpecTotal = NumSpec + NumSpecExtra;

// Note: these are 0-based
struct aion_lookup {
    ARCH_HOST_DEVICE static constexpr int size() { return 200; }
    ARCH_HOST_DEVICE static constexpr int lo() { return 0; }
    ARCH_HOST_DEVICE static constexpr int hi() { return lo() + size() - 1; }
    // Small immutable lookups stay inlineable on both backends.
    ARCH_HOST_DEVICE constexpr Real operator()(int index) const {
        switch (index - lo()) {
        case 0: return 1.0;
        case 1: return 1.0;
        case 2: return 4.0;
        case 3: return 9.0;
        case 4: return 11.0;
        case 5: return 12.0;
        case 6: return 13.0;
        case 7: return 14.0;
        case 8: return 15.0;
        case 9: return 11.0;
        case 10: return 12.0;
        case 11: return 13.0;
        case 12: return 14.0;
        case 13: return 15.0;
        case 14: return 16.0;
        case 15: return 17.0;
        case 16: return 14.0;
        case 17: return 15.0;
        case 18: return 16.0;
        case 19: return 17.0;
        case 20: return 18.0;
        case 21: return 19.0;
        case 22: return 20.0;
        case 23: return 16.0;
        case 24: return 17.0;
        case 25: return 18.0;
        case 26: return 19.0;
        case 27: return 20.0;
        case 28: return 21.0;
        case 29: return 22.0;
        case 30: return 18.0;
        case 31: return 19.0;
        case 32: return 20.0;
        case 33: return 21.0;
        case 34: return 22.0;
        case 35: return 23.0;
        case 36: return 24.0;
        case 37: return 20.0;
        case 38: return 21.0;
        case 39: return 22.0;
        case 40: return 23.0;
        case 41: return 24.0;
        case 42: return 25.0;
        case 43: return 26.0;
        case 44: return 22.0;
        case 45: return 23.0;
        case 46: return 24.0;
        case 47: return 25.0;
        case 48: return 26.0;
        case 49: return 27.0;
        case 50: return 28.0;
        case 51: return 24.0;
        case 52: return 25.0;
        case 53: return 26.0;
        case 54: return 27.0;
        case 55: return 28.0;
        case 56: return 29.0;
        case 57: return 30.0;
        case 58: return 27.0;
        case 59: return 28.0;
        case 60: return 29.0;
        case 61: return 30.0;
        case 62: return 31.0;
        case 63: return 32.0;
        case 64: return 33.0;
        case 65: return 29.0;
        case 66: return 30.0;
        case 67: return 31.0;
        case 68: return 32.0;
        case 69: return 33.0;
        case 70: return 34.0;
        case 71: return 35.0;
        case 72: return 31.0;
        case 73: return 32.0;
        case 74: return 33.0;
        case 75: return 34.0;
        case 76: return 35.0;
        case 77: return 36.0;
        case 78: return 37.0;
        case 79: return 33.0;
        case 80: return 34.0;
        case 81: return 35.0;
        case 82: return 36.0;
        case 83: return 37.0;
        case 84: return 38.0;
        case 85: return 39.0;
        case 86: return 36.0;
        case 87: return 37.0;
        case 88: return 38.0;
        case 89: return 39.0;
        case 90: return 40.0;
        case 91: return 41.0;
        case 92: return 42.0;
        case 93: return 38.0;
        case 94: return 39.0;
        case 95: return 40.0;
        case 96: return 41.0;
        case 97: return 42.0;
        case 98: return 43.0;
        case 99: return 44.0;
        case 100: return 40.0;
        case 101: return 41.0;
        case 102: return 42.0;
        case 103: return 43.0;
        case 104: return 44.0;
        case 105: return 45.0;
        case 106: return 46.0;
        case 107: return 43.0;
        case 108: return 44.0;
        case 109: return 45.0;
        case 110: return 46.0;
        case 111: return 47.0;
        case 112: return 48.0;
        case 113: return 49.0;
        case 114: return 45.0;
        case 115: return 46.0;
        case 116: return 47.0;
        case 117: return 48.0;
        case 118: return 49.0;
        case 119: return 50.0;
        case 120: return 51.0;
        case 121: return 47.0;
        case 122: return 48.0;
        case 123: return 49.0;
        case 124: return 50.0;
        case 125: return 51.0;
        case 126: return 52.0;
        case 127: return 53.0;
        case 128: return 50.0;
        case 129: return 51.0;
        case 130: return 52.0;
        case 131: return 53.0;
        case 132: return 54.0;
        case 133: return 55.0;
        case 134: return 56.0;
        case 135: return 52.0;
        case 136: return 53.0;
        case 137: return 54.0;
        case 138: return 55.0;
        case 139: return 56.0;
        case 140: return 57.0;
        case 141: return 58.0;
        case 142: return 54.0;
        case 143: return 55.0;
        case 144: return 56.0;
        case 145: return 57.0;
        case 146: return 58.0;
        case 147: return 59.0;
        case 148: return 60.0;
        case 149: return 57.0;
        case 150: return 58.0;
        case 151: return 59.0;
        case 152: return 60.0;
        case 153: return 61.0;
        case 154: return 62.0;
        case 155: return 63.0;
        case 156: return 59.0;
        case 157: return 60.0;
        case 158: return 61.0;
        case 159: return 62.0;
        case 160: return 63.0;
        case 161: return 64.0;
        case 162: return 65.0;
        case 163: return 62.0;
        case 164: return 63.0;
        case 165: return 64.0;
        case 166: return 65.0;
        case 167: return 66.0;
        case 168: return 67.0;
        case 169: return 68.0;
        case 170: return 64.0;
        case 171: return 65.0;
        case 172: return 66.0;
        case 173: return 67.0;
        case 174: return 68.0;
        case 175: return 69.0;
        case 176: return 70.0;
        case 177: return 67.0;
        case 178: return 68.0;
        case 179: return 69.0;
        case 180: return 70.0;
        case 181: return 71.0;
        case 182: return 72.0;
        case 183: return 73.0;
        case 184: return 69.0;
        case 185: return 70.0;
        case 186: return 71.0;
        case 187: return 72.0;
        case 188: return 73.0;
        case 189: return 74.0;
        case 190: return 75.0;
        case 191: return 72.0;
        case 192: return 73.0;
        case 193: return 74.0;
        case 194: return 75.0;
        case 195: return 76.0;
        case 196: return 77.0;
        case 197: return 78.0;
        case 198: return 77.0;
        case 199: return 10.0;
        default: assert(false); return 0.0_rt;
        }
    }
    ARCH_HOST_DEVICE constexpr Real operator[](int index) const { return (*this)(index); }
};
ARCH_HOST_DEVICE inline constexpr aion_lookup aion_values() { return {}; }

struct aion_inv_lookup {
    ARCH_HOST_DEVICE static constexpr int size() { return 200; }
    ARCH_HOST_DEVICE static constexpr int lo() { return 0; }
    ARCH_HOST_DEVICE static constexpr int hi() { return lo() + size() - 1; }
    // Small immutable lookups stay inlineable on both backends.
    ARCH_HOST_DEVICE constexpr Real operator()(int index) const {
        switch (index - lo()) {
        case 0: return 1.0/   1.0;
        case 1: return 1.0/   1.0;
        case 2: return 1.0/   4.0;
        case 3: return 1.0/   9.0;
        case 4: return 1.0/  11.0;
        case 5: return 1.0/  12.0;
        case 6: return 1.0/  13.0;
        case 7: return 1.0/  14.0;
        case 8: return 1.0/  15.0;
        case 9: return 1.0/  11.0;
        case 10: return 1.0/  12.0;
        case 11: return 1.0/  13.0;
        case 12: return 1.0/  14.0;
        case 13: return 1.0/  15.0;
        case 14: return 1.0/  16.0;
        case 15: return 1.0/  17.0;
        case 16: return 1.0/  14.0;
        case 17: return 1.0/  15.0;
        case 18: return 1.0/  16.0;
        case 19: return 1.0/  17.0;
        case 20: return 1.0/  18.0;
        case 21: return 1.0/  19.0;
        case 22: return 1.0/  20.0;
        case 23: return 1.0/  16.0;
        case 24: return 1.0/  17.0;
        case 25: return 1.0/  18.0;
        case 26: return 1.0/  19.0;
        case 27: return 1.0/  20.0;
        case 28: return 1.0/  21.0;
        case 29: return 1.0/  22.0;
        case 30: return 1.0/  18.0;
        case 31: return 1.0/  19.0;
        case 32: return 1.0/  20.0;
        case 33: return 1.0/  21.0;
        case 34: return 1.0/  22.0;
        case 35: return 1.0/  23.0;
        case 36: return 1.0/  24.0;
        case 37: return 1.0/  20.0;
        case 38: return 1.0/  21.0;
        case 39: return 1.0/  22.0;
        case 40: return 1.0/  23.0;
        case 41: return 1.0/  24.0;
        case 42: return 1.0/  25.0;
        case 43: return 1.0/  26.0;
        case 44: return 1.0/  22.0;
        case 45: return 1.0/  23.0;
        case 46: return 1.0/  24.0;
        case 47: return 1.0/  25.0;
        case 48: return 1.0/  26.0;
        case 49: return 1.0/  27.0;
        case 50: return 1.0/  28.0;
        case 51: return 1.0/  24.0;
        case 52: return 1.0/  25.0;
        case 53: return 1.0/  26.0;
        case 54: return 1.0/  27.0;
        case 55: return 1.0/  28.0;
        case 56: return 1.0/  29.0;
        case 57: return 1.0/  30.0;
        case 58: return 1.0/  27.0;
        case 59: return 1.0/  28.0;
        case 60: return 1.0/  29.0;
        case 61: return 1.0/  30.0;
        case 62: return 1.0/  31.0;
        case 63: return 1.0/  32.0;
        case 64: return 1.0/  33.0;
        case 65: return 1.0/  29.0;
        case 66: return 1.0/  30.0;
        case 67: return 1.0/  31.0;
        case 68: return 1.0/  32.0;
        case 69: return 1.0/  33.0;
        case 70: return 1.0/  34.0;
        case 71: return 1.0/  35.0;
        case 72: return 1.0/  31.0;
        case 73: return 1.0/  32.0;
        case 74: return 1.0/  33.0;
        case 75: return 1.0/  34.0;
        case 76: return 1.0/  35.0;
        case 77: return 1.0/  36.0;
        case 78: return 1.0/  37.0;
        case 79: return 1.0/  33.0;
        case 80: return 1.0/  34.0;
        case 81: return 1.0/  35.0;
        case 82: return 1.0/  36.0;
        case 83: return 1.0/  37.0;
        case 84: return 1.0/  38.0;
        case 85: return 1.0/  39.0;
        case 86: return 1.0/  36.0;
        case 87: return 1.0/  37.0;
        case 88: return 1.0/  38.0;
        case 89: return 1.0/  39.0;
        case 90: return 1.0/  40.0;
        case 91: return 1.0/  41.0;
        case 92: return 1.0/  42.0;
        case 93: return 1.0/  38.0;
        case 94: return 1.0/  39.0;
        case 95: return 1.0/  40.0;
        case 96: return 1.0/  41.0;
        case 97: return 1.0/  42.0;
        case 98: return 1.0/  43.0;
        case 99: return 1.0/  44.0;
        case 100: return 1.0/  40.0;
        case 101: return 1.0/  41.0;
        case 102: return 1.0/  42.0;
        case 103: return 1.0/  43.0;
        case 104: return 1.0/  44.0;
        case 105: return 1.0/  45.0;
        case 106: return 1.0/  46.0;
        case 107: return 1.0/  43.0;
        case 108: return 1.0/  44.0;
        case 109: return 1.0/  45.0;
        case 110: return 1.0/  46.0;
        case 111: return 1.0/  47.0;
        case 112: return 1.0/  48.0;
        case 113: return 1.0/  49.0;
        case 114: return 1.0/  45.0;
        case 115: return 1.0/  46.0;
        case 116: return 1.0/  47.0;
        case 117: return 1.0/  48.0;
        case 118: return 1.0/  49.0;
        case 119: return 1.0/  50.0;
        case 120: return 1.0/  51.0;
        case 121: return 1.0/  47.0;
        case 122: return 1.0/  48.0;
        case 123: return 1.0/  49.0;
        case 124: return 1.0/  50.0;
        case 125: return 1.0/  51.0;
        case 126: return 1.0/  52.0;
        case 127: return 1.0/  53.0;
        case 128: return 1.0/  50.0;
        case 129: return 1.0/  51.0;
        case 130: return 1.0/  52.0;
        case 131: return 1.0/  53.0;
        case 132: return 1.0/  54.0;
        case 133: return 1.0/  55.0;
        case 134: return 1.0/  56.0;
        case 135: return 1.0/  52.0;
        case 136: return 1.0/  53.0;
        case 137: return 1.0/  54.0;
        case 138: return 1.0/  55.0;
        case 139: return 1.0/  56.0;
        case 140: return 1.0/  57.0;
        case 141: return 1.0/  58.0;
        case 142: return 1.0/  54.0;
        case 143: return 1.0/  55.0;
        case 144: return 1.0/  56.0;
        case 145: return 1.0/  57.0;
        case 146: return 1.0/  58.0;
        case 147: return 1.0/  59.0;
        case 148: return 1.0/  60.0;
        case 149: return 1.0/  57.0;
        case 150: return 1.0/  58.0;
        case 151: return 1.0/  59.0;
        case 152: return 1.0/  60.0;
        case 153: return 1.0/  61.0;
        case 154: return 1.0/  62.0;
        case 155: return 1.0/  63.0;
        case 156: return 1.0/  59.0;
        case 157: return 1.0/  60.0;
        case 158: return 1.0/  61.0;
        case 159: return 1.0/  62.0;
        case 160: return 1.0/  63.0;
        case 161: return 1.0/  64.0;
        case 162: return 1.0/  65.0;
        case 163: return 1.0/  62.0;
        case 164: return 1.0/  63.0;
        case 165: return 1.0/  64.0;
        case 166: return 1.0/  65.0;
        case 167: return 1.0/  66.0;
        case 168: return 1.0/  67.0;
        case 169: return 1.0/  68.0;
        case 170: return 1.0/  64.0;
        case 171: return 1.0/  65.0;
        case 172: return 1.0/  66.0;
        case 173: return 1.0/  67.0;
        case 174: return 1.0/  68.0;
        case 175: return 1.0/  69.0;
        case 176: return 1.0/  70.0;
        case 177: return 1.0/  67.0;
        case 178: return 1.0/  68.0;
        case 179: return 1.0/  69.0;
        case 180: return 1.0/  70.0;
        case 181: return 1.0/  71.0;
        case 182: return 1.0/  72.0;
        case 183: return 1.0/  73.0;
        case 184: return 1.0/  69.0;
        case 185: return 1.0/  70.0;
        case 186: return 1.0/  71.0;
        case 187: return 1.0/  72.0;
        case 188: return 1.0/  73.0;
        case 189: return 1.0/  74.0;
        case 190: return 1.0/  75.0;
        case 191: return 1.0/  72.0;
        case 192: return 1.0/  73.0;
        case 193: return 1.0/  74.0;
        case 194: return 1.0/  75.0;
        case 195: return 1.0/  76.0;
        case 196: return 1.0/  77.0;
        case 197: return 1.0/  78.0;
        case 198: return 1.0/  77.0;
        case 199: return 1.0/  10.0;
        default: assert(false); return 0.0_rt;
        }
    }
    ARCH_HOST_DEVICE constexpr Real operator[](int index) const { return (*this)(index); }
};
ARCH_HOST_DEVICE inline constexpr aion_inv_lookup aion_inv_values() { return {}; }

struct zion_lookup {
    ARCH_HOST_DEVICE static constexpr int size() { return 200; }
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
        case 8: return 6.0;
        case 9: return 7.0;
        case 10: return 7.0;
        case 11: return 7.0;
        case 12: return 7.0;
        case 13: return 7.0;
        case 14: return 7.0;
        case 15: return 7.0;
        case 16: return 8.0;
        case 17: return 8.0;
        case 18: return 8.0;
        case 19: return 8.0;
        case 20: return 8.0;
        case 21: return 8.0;
        case 22: return 8.0;
        case 23: return 9.0;
        case 24: return 9.0;
        case 25: return 9.0;
        case 26: return 9.0;
        case 27: return 9.0;
        case 28: return 9.0;
        case 29: return 9.0;
        case 30: return 10.0;
        case 31: return 10.0;
        case 32: return 10.0;
        case 33: return 10.0;
        case 34: return 10.0;
        case 35: return 10.0;
        case 36: return 10.0;
        case 37: return 11.0;
        case 38: return 11.0;
        case 39: return 11.0;
        case 40: return 11.0;
        case 41: return 11.0;
        case 42: return 11.0;
        case 43: return 11.0;
        case 44: return 12.0;
        case 45: return 12.0;
        case 46: return 12.0;
        case 47: return 12.0;
        case 48: return 12.0;
        case 49: return 12.0;
        case 50: return 12.0;
        case 51: return 13.0;
        case 52: return 13.0;
        case 53: return 13.0;
        case 54: return 13.0;
        case 55: return 13.0;
        case 56: return 13.0;
        case 57: return 13.0;
        case 58: return 14.0;
        case 59: return 14.0;
        case 60: return 14.0;
        case 61: return 14.0;
        case 62: return 14.0;
        case 63: return 14.0;
        case 64: return 14.0;
        case 65: return 15.0;
        case 66: return 15.0;
        case 67: return 15.0;
        case 68: return 15.0;
        case 69: return 15.0;
        case 70: return 15.0;
        case 71: return 15.0;
        case 72: return 16.0;
        case 73: return 16.0;
        case 74: return 16.0;
        case 75: return 16.0;
        case 76: return 16.0;
        case 77: return 16.0;
        case 78: return 16.0;
        case 79: return 17.0;
        case 80: return 17.0;
        case 81: return 17.0;
        case 82: return 17.0;
        case 83: return 17.0;
        case 84: return 17.0;
        case 85: return 17.0;
        case 86: return 18.0;
        case 87: return 18.0;
        case 88: return 18.0;
        case 89: return 18.0;
        case 90: return 18.0;
        case 91: return 18.0;
        case 92: return 18.0;
        case 93: return 19.0;
        case 94: return 19.0;
        case 95: return 19.0;
        case 96: return 19.0;
        case 97: return 19.0;
        case 98: return 19.0;
        case 99: return 19.0;
        case 100: return 20.0;
        case 101: return 20.0;
        case 102: return 20.0;
        case 103: return 20.0;
        case 104: return 20.0;
        case 105: return 20.0;
        case 106: return 20.0;
        case 107: return 21.0;
        case 108: return 21.0;
        case 109: return 21.0;
        case 110: return 21.0;
        case 111: return 21.0;
        case 112: return 21.0;
        case 113: return 21.0;
        case 114: return 22.0;
        case 115: return 22.0;
        case 116: return 22.0;
        case 117: return 22.0;
        case 118: return 22.0;
        case 119: return 22.0;
        case 120: return 22.0;
        case 121: return 23.0;
        case 122: return 23.0;
        case 123: return 23.0;
        case 124: return 23.0;
        case 125: return 23.0;
        case 126: return 23.0;
        case 127: return 23.0;
        case 128: return 24.0;
        case 129: return 24.0;
        case 130: return 24.0;
        case 131: return 24.0;
        case 132: return 24.0;
        case 133: return 24.0;
        case 134: return 24.0;
        case 135: return 25.0;
        case 136: return 25.0;
        case 137: return 25.0;
        case 138: return 25.0;
        case 139: return 25.0;
        case 140: return 25.0;
        case 141: return 25.0;
        case 142: return 26.0;
        case 143: return 26.0;
        case 144: return 26.0;
        case 145: return 26.0;
        case 146: return 26.0;
        case 147: return 26.0;
        case 148: return 26.0;
        case 149: return 27.0;
        case 150: return 27.0;
        case 151: return 27.0;
        case 152: return 27.0;
        case 153: return 27.0;
        case 154: return 27.0;
        case 155: return 27.0;
        case 156: return 28.0;
        case 157: return 28.0;
        case 158: return 28.0;
        case 159: return 28.0;
        case 160: return 28.0;
        case 161: return 28.0;
        case 162: return 28.0;
        case 163: return 29.0;
        case 164: return 29.0;
        case 165: return 29.0;
        case 166: return 29.0;
        case 167: return 29.0;
        case 168: return 29.0;
        case 169: return 29.0;
        case 170: return 30.0;
        case 171: return 30.0;
        case 172: return 30.0;
        case 173: return 30.0;
        case 174: return 30.0;
        case 175: return 30.0;
        case 176: return 30.0;
        case 177: return 31.0;
        case 178: return 31.0;
        case 179: return 31.0;
        case 180: return 31.0;
        case 181: return 31.0;
        case 182: return 31.0;
        case 183: return 31.0;
        case 184: return 32.0;
        case 185: return 32.0;
        case 186: return 32.0;
        case 187: return 32.0;
        case 188: return 32.0;
        case 189: return 32.0;
        case 190: return 32.0;
        case 191: return 33.0;
        case 192: return 33.0;
        case 193: return 33.0;
        case 194: return 33.0;
        case 195: return 33.0;
        case 196: return 33.0;
        case 197: return 33.0;
        case 198: return 34.0;
        case 199: return 6.0;
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
    "C9", // 3
    "C11", // 4
    "C12", // 5
    "C13", // 6
    "C14", // 7
    "C15", // 8
    "N11", // 9
    "N12", // 10
    "N13", // 11
    "N14", // 12
    "N15", // 13
    "N16", // 14
    "N17", // 15
    "O14", // 16
    "O15", // 17
    "O16", // 18
    "O17", // 19
    "O18", // 20
    "O19", // 21
    "O20", // 22
    "F16", // 23
    "F17", // 24
    "F18", // 25
    "F19", // 26
    "F20", // 27
    "F21", // 28
    "F22", // 29
    "Ne18", // 30
    "Ne19", // 31
    "Ne20", // 32
    "Ne21", // 33
    "Ne22", // 34
    "Ne23", // 35
    "Ne24", // 36
    "Na20", // 37
    "Na21", // 38
    "Na22", // 39
    "Na23", // 40
    "Na24", // 41
    "Na25", // 42
    "Na26", // 43
    "Mg22", // 44
    "Mg23", // 45
    "Mg24", // 46
    "Mg25", // 47
    "Mg26", // 48
    "Mg27", // 49
    "Mg28", // 50
    "Al24", // 51
    "Al25", // 52
    "Al26", // 53
    "Al27", // 54
    "Al28", // 55
    "Al29", // 56
    "Al30", // 57
    "Si27", // 58
    "Si28", // 59
    "Si29", // 60
    "Si30", // 61
    "Si31", // 62
    "Si32", // 63
    "Si33", // 64
    "P29", // 65
    "P30", // 66
    "P31", // 67
    "P32", // 68
    "P33", // 69
    "P34", // 70
    "P35", // 71
    "S31", // 72
    "S32", // 73
    "S33", // 74
    "S34", // 75
    "S35", // 76
    "S36", // 77
    "S37", // 78
    "Cl33", // 79
    "Cl34", // 80
    "Cl35", // 81
    "Cl36", // 82
    "Cl37", // 83
    "Cl38", // 84
    "Cl39", // 85
    "Ar36", // 86
    "Ar37", // 87
    "Ar38", // 88
    "Ar39", // 89
    "Ar40", // 90
    "Ar41", // 91
    "Ar42", // 92
    "K38", // 93
    "K39", // 94
    "K40", // 95
    "K41", // 96
    "K42", // 97
    "K43", // 98
    "K44", // 99
    "Ca40", // 100
    "Ca41", // 101
    "Ca42", // 102
    "Ca43", // 103
    "Ca44", // 104
    "Ca45", // 105
    "Ca46", // 106
    "Sc43", // 107
    "Sc44", // 108
    "Sc45", // 109
    "Sc46", // 110
    "Sc47", // 111
    "Sc48", // 112
    "Sc49", // 113
    "Ti45", // 114
    "Ti46", // 115
    "Ti47", // 116
    "Ti48", // 117
    "Ti49", // 118
    "Ti50", // 119
    "Ti51", // 120
    "V47", // 121
    "V48", // 122
    "V49", // 123
    "V50", // 124
    "V51", // 125
    "V52", // 126
    "V53", // 127
    "Cr50", // 128
    "Cr51", // 129
    "Cr52", // 130
    "Cr53", // 131
    "Cr54", // 132
    "Cr55", // 133
    "Cr56", // 134
    "Mn52", // 135
    "Mn53", // 136
    "Mn54", // 137
    "Mn55", // 138
    "Mn56", // 139
    "Mn57", // 140
    "Mn58", // 141
    "Fe54", // 142
    "Fe55", // 143
    "Fe56", // 144
    "Fe57", // 145
    "Fe58", // 146
    "Fe59", // 147
    "Fe60", // 148
    "Co57", // 149
    "Co58", // 150
    "Co59", // 151
    "Co60", // 152
    "Co61", // 153
    "Co62", // 154
    "Co63", // 155
    "Ni59", // 156
    "Ni60", // 157
    "Ni61", // 158
    "Ni62", // 159
    "Ni63", // 160
    "Ni64", // 161
    "Ni65", // 162
    "Cu62", // 163
    "Cu63", // 164
    "Cu64", // 165
    "Cu65", // 166
    "Cu66", // 167
    "Cu67", // 168
    "Cu68", // 169
    "Zn64", // 170
    "Zn65", // 171
    "Zn66", // 172
    "Zn67", // 173
    "Zn68", // 174
    "Zn69", // 175
    "Zn70", // 176
    "Ga67", // 177
    "Ga68", // 178
    "Ga69", // 179
    "Ga70", // 180
    "Ga71", // 181
    "Ga72", // 182
    "Ga73", // 183
    "Ge69", // 184
    "Ge70", // 185
    "Ge71", // 186
    "Ge72", // 187
    "Ge73", // 188
    "Ge74", // 189
    "Ge75", // 190
    "As72", // 191
    "As73", // 192
    "As74", // 193
    "As75", // 194
    "As76", // 195
    "As77", // 196
    "As78", // 197
    "Se77", // 198
    "C10", // 199
 };

namespace Species {
  enum NetworkSpecies {
    N=1,
    H1,
    He4,
    C9,
    C11,
    C12,
    C13,
    C14,
    C15,
    N11,
    N12,
    N13,
    N14,
    N15,
    N16,
    N17,
    O14,
    O15,
    O16,
    O17,
    O18,
    O19,
    O20,
    F16,
    F17,
    F18,
    F19,
    F20,
    F21,
    F22,
    Ne18,
    Ne19,
    Ne20,
    Ne21,
    Ne22,
    Ne23,
    Ne24,
    Na20,
    Na21,
    Na22,
    Na23,
    Na24,
    Na25,
    Na26,
    Mg22,
    Mg23,
    Mg24,
    Mg25,
    Mg26,
    Mg27,
    Mg28,
    Al24,
    Al25,
    Al26,
    Al27,
    Al28,
    Al29,
    Al30,
    Si27,
    Si28,
    Si29,
    Si30,
    Si31,
    Si32,
    Si33,
    P29,
    P30,
    P31,
    P32,
    P33,
    P34,
    P35,
    S31,
    S32,
    S33,
    S34,
    S35,
    S36,
    S37,
    Cl33,
    Cl34,
    Cl35,
    Cl36,
    Cl37,
    Cl38,
    Cl39,
    Ar36,
    Ar37,
    Ar38,
    Ar39,
    Ar40,
    Ar41,
    Ar42,
    K38,
    K39,
    K40,
    K41,
    K42,
    K43,
    K44,
    Ca40,
    Ca41,
    Ca42,
    Ca43,
    Ca44,
    Ca45,
    Ca46,
    Sc43,
    Sc44,
    Sc45,
    Sc46,
    Sc47,
    Sc48,
    Sc49,
    Ti45,
    Ti46,
    Ti47,
    Ti48,
    Ti49,
    Ti50,
    Ti51,
    V47,
    V48,
    V49,
    V50,
    V51,
    V52,
    V53,
    Cr50,
    Cr51,
    Cr52,
    Cr53,
    Cr54,
    Cr55,
    Cr56,
    Mn52,
    Mn53,
    Mn54,
    Mn55,
    Mn56,
    Mn57,
    Mn58,
    Fe54,
    Fe55,
    Fe56,
    Fe57,
    Fe58,
    Fe59,
    Fe60,
    Co57,
    Co58,
    Co59,
    Co60,
    Co61,
    Co62,
    Co63,
    Ni59,
    Ni60,
    Ni61,
    Ni62,
    Ni63,
    Ni64,
    Ni65,
    Cu62,
    Cu63,
    Cu64,
    Cu65,
    Cu66,
    Cu67,
    Cu68,
    Zn64,
    Zn65,
    Zn66,
    Zn67,
    Zn68,
    Zn69,
    Zn70,
    Ga67,
    Ga68,
    Ga69,
    Ga70,
    Ga71,
    Ga72,
    Ga73,
    Ge69,
    Ge70,
    Ge71,
    Ge72,
    Ge73,
    Ge74,
    Ge75,
    As72,
    As73,
    As74,
    As75,
    As76,
    As77,
    As78,
    Se77,
    C10,
  };
}

#endif
