# HDF5 serialization

[HDF5Writer.h](HDF5Writer.h) and [HDF5Writer.cpp](HDF5Writer.cpp) implement plot
and checkpoint serialization for both backends, including mesh metadata and
restart-critical scientific identity.

The caller is entirely responsible for supplying a fully synchronized state. Absolutely do not introduce CUDA kernels, secondary checkpoint schemas, or any form of numerical state repair within this module. Any changes here must be carefully coordinated with [checkpoint compatibility](../chk/README.md) and the formal [format reference](../../../docs/Reference.md).
