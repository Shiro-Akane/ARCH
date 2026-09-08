"""Rank-one ground-state NSE witness with an explicitly paired triple-alpha rate."""

NETWORK_ID = "nse_alpha"


def build_network(pyna):
    nuclei = [pyna.Nucleus.from_cache(name) for name in ("he4", "c12")]
    library = pyna.ReacLibLibrary().linking_nuclei(nuclei, with_reverse=False)
    forward = [rate for rate in library.get_rates()
               if not rate.weak and not rate.derived_from_inverse]
    rates = forward + [pyna.DerivedRate(rate, use_pf=False) for rate in forward]
    return pyna.SimpleCxxNetwork(rates=rates, inert_nuclei=nuclei, do_screening=False)
