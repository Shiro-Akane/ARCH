"""ARCH validation recipe: 200-isotope C-through-As band."""

NETWORK_ID = "audit200"


def build_network(pyna):
    nuclei = [pyna.Nucleus.from_cache(name) for name in ("n", "p", "he4")]
    # Seven consecutive isotopes per element for C through As gives 199 total;
    # append one Se isotope to exercise exactly 200 species.
    for z in range(6, 34):
        center = round(2.0*z + 0.008*z*z)
        nuclei.extend(pyna.Nucleus.from_Z_A(z, center + offset)
                      for offset in (-3, -2, -1, 0, 1, 2, 3))
    z = 34
    center = round(2.0*z + 0.008*z*z)
    nuclei.append(pyna.Nucleus.from_Z_A(z, center))

    library = pyna.ReacLibLibrary().linking_nuclei(
        nuclei, with_reverse=True, print_warning=False)
    return pyna.SimpleCxxNetwork(
        libraries=library, inert_nuclei=nuclei, do_screening=True)
