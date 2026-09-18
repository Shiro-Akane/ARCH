# Built-in EOS route bindings

These translation units bind the existing burn launch interface to Ideal,
Helmholtz, Tabular3D and Tabular4D EOS types. The tabular network-specific units
keep expensive instantiations in their existing functional compilation owners.

These files bind registered policies; network and ODE algorithms remain in their
shared owners. [CMakeLists.txt](../../../../../CMakeLists.txt) and its modules define
the targets and optimization settings. Generated dense, sparse and custom bindings
are emitted by [CMake templates](../../../../../cmake/README.md) into the build tree.
See the [burn runtime index](../README.md) for the call flow.
