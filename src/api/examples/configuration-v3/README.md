# Actual configuration v3 responses

These are captured from the linked CPU ARCH, not the hand-authored candidate.
capture.json records the executable hash, source base plus modified implementation
hashes, exact arguments and exit codes. Build under the integration branch; call
ARCH with those arguments and feed the corresponding .par on stdin. Compare parsed
JSON, not whitespace. Source identity fields can change after case implementation
changes and must not be blindly rewritten to make a test pass.

Fixtures cover schema, valid Sod, empty input and aggregated invalid input. They
contain configuration metadata only, no field arrays, scientific output or EOS
table content. They are intended to be shared by Core and Host migration tests.

The configuration_v3_contract actual-binary test independently asserts missing,
invalid, repeated, default, case-defined and derived semantics, identity, budget
and absence of filesystem side effects. These fixtures are evidence for this
static boundary, not full simulation readiness or finished Host/Studio acceptance.
