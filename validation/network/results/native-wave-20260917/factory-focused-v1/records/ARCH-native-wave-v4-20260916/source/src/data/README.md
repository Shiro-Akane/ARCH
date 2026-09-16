# State and configuration records

[FluidState.h](FluidState.h) defines fluid-state storage and access;
[UserTypes.h](UserTypes.h) defines initialization records and callback signatures.
[GlobalDefs.h](GlobalDefs.h) holds shared configuration and numerical policy types.

These files establish common interfaces and data structures, and do not contain any secondary physics implementations. Remember that universal constants should be placed in [physics/constant](../physics/constant/README.md), and algorithms belong in [numerics](../numerics/README.md). Any changes to these core data types must be carefully coordinated with both the [driver](../driver/README.md) and the shared [IO](../io/README.md) consumers.
