#include "math/RoeThermodynamicCases.h"
#include <iostream>
int main() {
    const auto result = RoeThermodynamicCases::evaluate();
    std::cout << "reflection=" << result.reflection
              << " ideal_sound_speed=" << result.ideal_sound_speed
              << " ideal_pressure_jump=" << result.ideal_pressure_jump
              << " rotation=" << result.rotation << '\n';
    return result.passed() ? 0 : 1;
}
