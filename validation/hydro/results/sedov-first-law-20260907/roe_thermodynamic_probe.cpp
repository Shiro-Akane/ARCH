#include <iomanip>
#include <iostream>
#include "math/RoeThermodynamicCases.h"

int main() {
    const auto result = RoeThermodynamicCases::evaluate();
    std::cout << std::setprecision(17)
              << "reflection=" << result.reflection
              << "\nrotation=" << result.rotation
              << "\nideal_sound_speed=" << result.ideal_sound_speed
              << "\nideal_pressure_jump=" << result.ideal_pressure_jump
              << "\nstationary_transport=" << result.stationary_transport << '\n';
    return result.passed() ? 0 : 1;
}
