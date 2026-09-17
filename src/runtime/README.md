# Adaptive Runtime boundary

`src/runtime` contains optional prediction and hardware-allocation modules that
sit above the stable Physics/AMR core.

The dependency direction is one way:

```text
Physics / AMR core -> read-only Patch API -> Adaptive Runtime
```

Runtime modules may observe patch state, propose hints, reserve resources, and
select optional backends. They do not own Hydro, EOS, reconstruction,
prolongation, restriction, FluxRegister, refluxing, or the deterministic AMR
criterion. Every module must be independently switchable, and disabling all
runtime modules must reproduce the reference solver.

Phase 0 exposes an observer only. There is deliberately no method that returns
a refinement action.
