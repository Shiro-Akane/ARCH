"""Ground-state detailed-balance NSE example; no screening or weak reactions.

This demonstrates generation eligibility, not an independently qualified
astrophysical isotope set. The recipe explicitly chooses reverse rates from
the same mass and spin data rather than replacing user rates inside ARCH.
"""

NETWORK_ID = "nse_light"


def build_network(pyna):
    nuclei = [pyna.Nucleus.from_cache(name)
              for name in ("n", "p", "h2", "he3", "he4", "c12", "o16")]
    library = pyna.ReacLibLibrary().linking_nuclei(nuclei, with_reverse=False)
    forward = [rate for rate in library.get_rates()
               if not rate.weak and not rate.derived_from_inverse]
    rates = forward + [pyna.DerivedRate(rate, use_pf=False) for rate in forward]
    return pyna.SimpleCxxNetwork(rates=rates, inert_nuclei=nuclei, do_screening=False)
