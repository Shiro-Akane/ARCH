# Built-in EOS route bindings

These translation units bind the existing burn launch interface to Ideal,
Helmholtz, Tabular3D and Tabular4D EOS types. The tabular network-specific units
keep expensive instantiations in their existing functional compilation owners.

They contain binding and dispatch, not another network or ODE algorithm.
Their target names, combinations and optimization policy are declared explicitly
in [CMakeLists.txt](../../../../../CMakeLists.txt). Generated dense, sparse and
custom bindings are produced separately from [CMake templates](../../../../../cmake/README.md)
into the build directory. See the [burn runtime index](../README.md).
