# Interrupted read-only index preflight 927

The resource guard stopped this read-only indexing attempt with exit 125 after
109.555 seconds because system-wide swap grew beyond its unchanged 256 MiB
allowance. No `index.json` was produced; this attempt has no acceptance result.
The concurrent curved application racecheck 906 completed successfully under
its separate guard. Neither result is substituted for the other.

The index's peak owned-process RSS was 39,768 KiB, minimum system available
memory was 5,040,128 KiB, and system swap changed from 201,316 to 464,368 KiB.
System counters do not attribute that swap growth to the index process. Its
guard stopped only its owned command tree. No numerical, source or artifact
change is made in response; the successor runs separately with the same guard
thresholds and a new output directory.

The original guard log was recorded at `build/index-preflight-927.log` with
SHA-256 `142bf4453affa324cc4c644ebca2876aa68969fe53fc9dfbb34eed3418cfb39a`.
That raw log is not present in the current archive. This note preserves its
reported identity and interrupted status; no other run's log is substituted.
The archived [index recipe](review_index.py) has the executed SHA-256
`cfcb143e3488f8983c21781fda7ae7b328f8383087d77f61681f6478ca4a0f4d`.
It was run with Python 3.11 and `--output-dir` set to this directory under the
usual 1536 MiB available-memory, 256 MiB swap-growth and sustained-PSI guard.
This note was created after the stop to retain the failed attempt; it does not
reconstruct missing per-gate output or a verified-after-run identity.
The recipe archive is byte-verified against that execution hash and retained
for inspection, not execution from its deeper archival directory.
