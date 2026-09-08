"""Two-isotope weak-table witness: the Suzuki Na23/Ne23 Urca pair.

This is an interface/loss-integration validation recipe, not a complete stellar
burning network. Pin the pynucastro release and emitted table hashes in evidence.
"""

from pathlib import Path

NETWORK_ID = "weak_urca"


def build_network(pynucastro):
    source = Path(pynucastro.__file__).resolve().parent / "library/tabular/suzuki"
    rates = [pynucastro.rates.TabularRate(rfile=source / name) for name in (
        "suzuki-23na-23ne_electroncapture.dat",
        "suzuki-23ne-23na_betadecay.dat",
    )]
    return pynucastro.SimpleCxxNetwork(rates=rates, do_screening=False)
