# Preserved memcheck-902 recipe

`run_sanitizers.py` is a byte-identical archive of the recipe used by attempt
902, with its original 1,200-second allowance per complete route. Its SHA-256 is
`554e2ed2a40b734ea2e466649df8b2f0d7ccc97ec0b968bdf50354ffd8515384`.
The copy preserves the original relative-path expressions; it is evidence,
not an entry point to run from this deeper directory.

The [execution log](../../../../../build/memcheck-final-902.log) records 22
completed routes followed by an audit31 timeout. The [partial application
output](../memcheck-902/audit31_sparse_cells/arch.stdout) and [incomplete sanitizer
report](../memcheck-902/audit31_sparse_cells/sanitizer.log) remain unchanged.
This attempt did not pass the complete focused suite. A replacement must use a
new output directory and finish the unchanged workload.
