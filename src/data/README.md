# State and configuration records

[FluidState.h](FluidState.h) defines fluid-state storage and access;
[UserTypes.h](UserTypes.h) defines initialization records and callback signatures.
[GlobalDefs.h](GlobalDefs.h) holds shared configuration and numerical policy types.

These are common interfaces, not a second physics implementation. Universal
constants belong in [physics/constant](../physics/constant/README.md); algorithms
belong in [numerics](../numerics/README.md). Keep type changes coordinated with
the [driver](../driver/README.md) and shared [IO](../io/README.md) consumers.
