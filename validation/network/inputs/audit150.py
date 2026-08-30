"""ARCH validation recipe: 150-isotope C-through-Zn band."""

NETWORK_ID = "audit150"


def build_network(pyna):
    nuclei = [pyna.Nucleus.from_cache(name) for name in ("n", "p", "he4")]
    # Six consecutive isotopes around a smooth approximation to the valley of
    # stability for C through Cu, then three Zn nuclei: 3 + 24*6 + 3 = 150.
    for z in range(6, 30):
        center = round(2.0*z + 0.008*z*z)
        nuclei.extend(pyna.Nucleus.from_Z_A(z, center + offset)
                      for offset in (-2, -1, 0, 1, 2, 3))
    z = 30
    center = round(2.0*z + 0.008*z*z)
    nuclei.extend(pyna.Nucleus.from_Z_A(z, center + offset)
                  for offset in (-1, 0, 1))

    library = pyna.ReacLibLibrary().linking_nuclei(
        nuclei, with_reverse=True, print_warning=False)
    return pyna.SimpleCxxNetwork(
        libraries=library, inert_nuclei=nuclei, do_screening=True)
