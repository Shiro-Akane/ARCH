# Generated-network recipes

These are generator inputs, not emitted network packages:

- [audit31.py](audit31.py): the mixed network used for production sparse-path
  checks; total equation count also includes temperature and any declared
  auxiliary state.
- [weak_urca.py](weak_urca.py): a compact Na23/Ne23 weak-table pair for interpolation,
  energy-loss and trajectory checks.
- [audit150.py](audit150.py) and [audit200.py](audit200.py): retained larger-network
  recipes for separate scaling and capacity qualification. Their presence does
  not imply those workloads have completed validation.

Follow the [tested environment and generation setup](../README.md#reproduce-the-records)
before invoking [GenerateNetwork.py](../../../tools/network/GenerateNetwork.py).
The [custom-network contract](../../../src/physics/network/custom/README.md)
describes package contents and registration.

Keep the recipe, generator version, emitted manifest and upstream table identity
with a result. Generated output belongs in its configured package directory,
not beside these maintained recipes.
