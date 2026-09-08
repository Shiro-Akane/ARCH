# HDF5 serialization

[HDF5Writer.h](HDF5Writer.h) and [HDF5Writer.cpp](HDF5Writer.cpp) implement plot
and checkpoint serialization for both backends, including mesh metadata and
restart-critical scientific identity.

The caller supplies synchronized state. Do not add CUDA kernels, a second
checkpoint schema or numerical state repair here. Coordinate changes with
[checkpoint compatibility](../chk/README.md) and the
[format reference](../../../docs/Reference.md).
