# Built-in EOS route bindings

These translation units bind the existing burn launch interface to Ideal,
Helmholtz, Tabular3D and Tabular4D EOS types. The tabular network-specific units
keep expensive instantiations in their existing functional compilation owners.

These files exist solely to perform binding and dispatch—they absolutely do not contain any new network or ODE algorithms. Their target names, combinations, and associated optimization policies are explicitly declared in [CMakeLists.txt](../../../../../CMakeLists.txt). It is important to note that all generated dense, sparse, and custom bindings are constructed separately via [CMake templates](../../../../../cmake/README.md) directly into the build directory. For a higher-level view, please consult the [burn runtime index](../README.md).
