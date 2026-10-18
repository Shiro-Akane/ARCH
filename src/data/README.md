# State and configuration records

[FluidState.h](FluidState.h) defines fluid-state storage and access;
[UserTypes.h](UserTypes.h) defines initialization records and callback signatures.
[GlobalDefs.h](GlobalDefs.h) holds shared configuration and numerical policy types.

These files establish common interfaces and data structures, and do not contain any secondary physics implementations. Remember that universal constants should be placed in [physics/constant](../physics/constant/README.md), and algorithms belong in [numerics](../numerics/README.md). Any changes to these core data types must be carefully coordinated with both the [driver](../driver/README.md) and the shared [IO](../io/README.md) consumers.

`FluidState` forward-declares its two existing boundary-control and flux-observer
attachments. Consumers that dereference them include the actual boundary headers
directly. They remain transient per-block resources, with the existing sharing
between stage slots and explicit rollback/regrid handling; this header boundary
does not move ownership or add serialized/evolved fields. Common state must not
import a concrete optional solver merely to carry a borrowed declaration.
