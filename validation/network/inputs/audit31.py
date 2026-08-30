"""ARCH validation recipe: 31-isotope mixed H/He/CNO/NeNaMg network."""

NETWORK_ID = "audit31"

NUCLEI = [
    "n", "p", "he4",
    "c12", "c13", "n13", "n14", "n15",
    "o14", "o15", "o16", "o17", "o18",
    "f17", "f18", "f19",
    "ne18", "ne19", "ne20", "ne21", "ne22",
    "na21", "na22", "na23",
    "mg22", "mg23", "mg24", "mg25", "mg26",
    "al25", "si28",
]

WITH_REVERSE = True
DO_SCREENING = True
PRINT_RATE_WARNINGS = False
