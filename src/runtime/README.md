# Optional runtime observers

This directory contains the [read-only AMR recorder](predictive_amr/README.md).
It observes accepted patch state and canonical mesh decisions through the shared
driver. It does not own or replace physical models, numerical methods, Morton
ordering, refinement criteria, balance closure or regrid transactions.

The recorder is disabled by default. Future prediction or resource-management
modules should use this same ownership boundary and remain independently
switchable; they are not current solver features.
