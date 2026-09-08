# Built-in time-integration control

The retained [CTest transcript](ctest.log) passes all twelve combinations of
`aprox13`, `aprox19`, `aprox21`, `iso7` and BE_NR / BD / ROS4, including checker
negative controls. The immutable expected endpoints are corroborated by the
[independent time/EOS reference review](../independent-time-20260906/evidence.json).

The largest species Linf is `7.760566758663323e-9`, relative total-energy error
`8.65707006081351e-9`, and abundance closure `1.1102230246251565e-16`. They meet
the fixed `1e-8`, `1e-8` and `1e-12` scientific budgets. Strict validation inputs
are `rtol=1e-13`, `atol=1e-17`, `max_substeps=3000000`; application defaults
are unchanged. Local error tolerance is not a global-accuracy guarantee.

This is a focused Host integrator test, not independent nuclear-rate validation,
complete CPU/CUDA application qualification or a release certificate. The log
was retained after CTest completion; it does not assert a before/after observation
of all final release artifacts. Reproduce with the current testing-enabled build:
`ctest --test-dir <build> -R '^burn_mainline_reference$' --output-on-failure`,
under the shared memory guard. Final-profile reruns remain required.
