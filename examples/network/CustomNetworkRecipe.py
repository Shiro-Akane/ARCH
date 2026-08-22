"""Copy this recipe, then edit the ID, nuclei, and rate-selection policy."""

# Runtime selection will be: network_name = custom:my_cno
# Use a new lowercase ID for every independent network package.
NETWORK_ID = "my_cno"

# The default builder links all ReacLib rates connecting only these nuclei.
NUCLEI = ["h1", "he4", "c12", "n13", "c13", "n14", "o15", "o16"]
WITH_REVERSE = True
DO_SCREENING = True
PRINT_RATE_WARNINGS = True

# Advanced recipes may replace NUCLEI with:
#
# def build_network(pyna):
#     library = ...
#     return pyna.SimpleCxxNetwork(
#         libraries=library,
#         inert_nuclei=[...],
#         do_screening=True,
#     )
