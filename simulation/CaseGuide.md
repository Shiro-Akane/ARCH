# ARCH Simulation Case Development Guide

Welcome to the simulation directory! This is where you define your physics problems.
ARCH uses a **plugin-style architecture**, meaning you can add new simulation cases simply by creating a new `.cpp` file here. CMake will automatically compile it.

---

## 🚀 How to Add a New Problem

1. **Copy the Template**: Copy `_Template.cpp` to a new file, e.g., `MySupernova.cpp`.
2. **Implement Logic**: Fill in the `Setup` and `Init` functions (details below).
3. **Register**: Use the `REGISTER_PROBLEM` macro at the bottom of your file.
4. **Compile**: Re-run `cmake --build .` or `make`.
5. **Run**: `./ARCH MyProblemName my_config.par`.

---

## 📚 Interface Reference

To create a problem, you need to implement two callback functions and register them.

### 1. Setup Function (`SetupFunc`)
**Signature:** `void MySetup(SimConfig &config, SpeciesManager &specs)`

This function runs **once** at the beginning. Use it to:
* Read global parameters from the `.par` file using `RuntimeParams::Get<T>()`.
* Configure the Grid (`nx`, `domain_len`).
* Configure Time Integration (`cfl`, `tmax`).
* Define Material Species (`specs.add_species(...)`).

**Example:**

void MySetup(SimConfig &cfg, SpeciesManager &specs) {
    cfg.nx = RuntimeParams::Get<int>("nx", 100);
    // Add Carbon-12
    specs.add_species("C12", 1.4, 12.0, 6.0); 
}

2. Initialization Function (InitFunc)
Signature: void MyInit(double x, double y, double z, PrimitiveData &out)

This function runs for every cell. The system gives you the coordinates (x, y, z), and you fill the out structure.

PrimitiveData Structure Members:

out.rho (double): Density

out.u (double): Velocity (x-direction)

out.p (double): Pressure

out.SetMassFraction(int id, double value): Helper to set species composition.

Example:
void MyInit(double x, double y, double z, PrimitiveData &out) {
    out.rho = 1.0 + 0.1 * sin(2 * M_PI * x); // Perturbation
    out.u   = 0.0;
    out.p   = 1.0;
    out.SetMassFraction(0, 1.0); // 100% Species 0
}

🔗 Registration Macro
At the end of your .cpp file, you must call this macro to register your problem into the system kernel.
// Arguments: "ProblemName", SetupFunction, InitFunction
REGISTER_PROBLEM("CCSN", MySetup, MyInit);

💡 Tips for Complex Cases (e.g., CCSNE)
RuntimeParams: You can add any custom parameter in your .par file (e.g., core_radius = 1.5). Just access it via RuntimeParams::Get<double>("core_radius", 1.0) in your Setup or Init function.

Math Library: <cmath> is available. Feel free to use std::exp, std::sin, etc.

External Data: If your initialization requires reading a table (e.g., an existing stellar profile), you can implement a standard file reader inside the Setup function, store the data in a global or static variable, and interpolate it in Init.

### 2. Blank `simulation/_Template.cpp`
```cpp
/**
 * @file _Template.cpp
 * @brief A blank template for creating new simulation problems.
 * * Usage:
 * 1. Copy this file to "MyProblem.cpp".
 * 2. Rename the functions (optional, but recommended for clarity).
 * 3. Implement Setup and Init logic.
 * 4. Register the problem at the bottom.
 */

#include "../../src/core/UserInterface.h"
#include "../../src/core/RuntimeParams.h"
#include <cmath>

// ============================================================================
// 1. Setup Phase
// Read config (Grid, Time) and define Species.
// ============================================================================
void Template_Setup(SimConfig &config, SpeciesManager &specs)
{
    // -- Grid & Time --
    // config.nx   = RuntimeParams::Get<int>("nx", 100);
    // config.tmax = ...

    // -- Species --
    // specs.add_species("Hydrogen", 1.4);
}

// ============================================================================
// 2. Initialization Phase
// Set initial primitive variables (rho, u, p, species) at coordinate x.
// ============================================================================
void Template_Init(double x, double y, double z, PrimitiveData &out)
{
    // -- Physics Logic --
    // out.rho = ...
    // out.u   = ...
    // out.p   = ...

    // -- Species Composition --
    // out.SetMassFraction(0, 1.0);
}

// ============================================================================
// Registration
// Name your problem here (e.g., "MyCase"). 
// This name is used in the command line: ./ARCH MyCase ...
// ============================================================================
REGISTER_PROBLEM("Template", Template_Setup, Template_Init);