# Case-facing headers

User cases include the two stable public entry points:

```cpp
#include <UserInterface.h>
#include <GlobalDefs.h>
```

CMake's `arch_build_contract` supplies this directory and the internal `src/`
search root. Build cases through the ARCH project; moving a checkout requires
fresh CMake configuration, not edits to case includes. These headers forward to
the single internal owners and contain no duplicated declarations or physics.
They are not a standalone installed SDK. `<GlobalDefs.h>` also exports the
shared `arch::constants` CGS namespaces, including `math::pi` and the
gravity constant; cases never include `physics/constant/PhysicalConstants.h`.

Inputs and outputs use CGS, including IdealGas. Explicit heat capacities use
`erg/(g K)`; ARCH does not implicitly convert user values. See the
[case guide](../docs/guides/SimulationCase.md) and
[header convention](../docs/development/layout/README.zh-CN.md).
