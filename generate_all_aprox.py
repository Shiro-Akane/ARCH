#!/usr/bin/env python3
"""
generate_all_aprox.py
=====================
Generate standard reduced nuclear reaction networks using pynucastro.

Nuclei definitions follow:
  - Timmes (cococubed.com/code_pages/burn_helium.shtml)
  - AMReX-Astro/Microphysics standard aprox networks
  - Weaver, Zimmerman, & Woosley (1978ApJ...225.1021W) for aprox19

Each network is written directly into src/physics/network/<name>/ .
pynucastro's SimpleCxxNetwork is used (no AMReX dependency).
"""

import pynucastro as pyna
import os
import sys

# ============================================================
# Standard intermediate nuclei for (α,p)(p,γ) approximation
# ============================================================
# These odd-Z nuclei sit between alpha-chain species and are
# eliminated via quasi-steady-state (QSS) proton flow assumption.
# The 9 intermediates cover Ne20→Mg24 through Fe52→Ni56.

INTERMEDIATES_9 = [
    "na23",   # between Ne20 and Mg24
    "al27",   # between Mg24 and Si28
    "p31",    # between Si28 and S32
    "cl35",   # between S32  and Ar36
    "k39",    # between Ar36 and Ca40
    "sc43",   # between Ca40 and Ti44
    "v47",    # between Ti44 and Cr48
    "mn51",   # between Cr48 and Fe52
    "co55",   # between Fe52 and Ni56
]

# For aprox13, Timmes uses 8 intermediates (Al27→Co55),
# since the Ne20→Mg24 link via Na23 is handled differently
# in some references. We include all 9 for consistency
# with pynucastro's make_ap_pg_approx implementation.
INTERMEDIATES_8 = INTERMEDIATES_9[1:]  # Al27→Co55 only

# ============================================================
# CNO intermediates for aprox19/21
# ============================================================
# These participate in CNO cycle reactions but are removed
# from the ODE system (not via QSS, but via remove_nuclei
# after linking provides the necessary rate connections).
# Note: for aprox19/21, CNO intermediates are only needed
# if the linking step pulls them in. We list them here
# so they participate in linking but are then removed.

CNO_INTERMEDIATES_19 = [
    # These are CNO cycle intermediates that pynucastro
    # may pull in via linking_nuclei when N14 is included.
    # They will be removed after linking.
]

# ============================================================
# Network definitions
# ============================================================

NETWORKS = {
    # ----------------------------------------------------------
    # iso7: 7 alpha-chain nuclei (Timmes 2000ApJS..129..377T)
    # Simplest possible nuclear burning network.
    # No intermediate nuclei, no approximate rates.
    # ----------------------------------------------------------
    "iso7": {
        "core": [
            "he4", "c12", "o16", "ne20", "mg24", "si28", "ni56",
        ],
        "intermediate": [],
        "cno_extra": [],
    },

    # ----------------------------------------------------------
    # aprox13: 13-isotope alpha-chain (Timmes, cococubed)
    # Full alpha chain from He4 to Ni56.
    # Includes 8 (α,p)(p,γ) sequences via QSS approximation
    # through Al27, P31, Cl35, K39, Sc43, V47, Mn51, Co55.
    # Critical for T > 2.5e9 K.
    # ----------------------------------------------------------
    "aprox13": {
        "core": [
            "he4", "c12", "o16", "ne20", "mg24", "si28",
            "s32", "ar36", "ca40", "ti44", "cr48", "fe52", "ni56",
        ],
        "intermediate": INTERMEDIATES_9,
        "cno_extra": [],
    },

    # ----------------------------------------------------------
    # aprox19: 19-isotope network (Weaver, Zimmerman & Woosley 1978)
    # = aprox13 + n, p, He3, N14, Fe54 for:
    #   - PP chains and steady-state CNO cycles
    #   - Photodisintegration into Fe54
    #   - Weak interactions / neutronization
    # ----------------------------------------------------------
    "aprox19": {
        "core": [
            "n", "p", "he3", "he4",
            "c12", "n14", "o16",
            "ne20", "mg24", "si28", "s32", "ar36", "ca40",
            "ti44", "cr48", "fe52", "fe54", "ni56",
        ],
        "intermediate": INTERMEDIATES_9,
        "cno_extra": [],
    },

    # ----------------------------------------------------------
    # aprox21: 21-isotope network (Timmes, cococubed)
    # = aprox19 + Cr56, Fe56 for lower Ye in presupernova models.
    # "more-or-less the default workhorse network of MESA"
    # ----------------------------------------------------------
    "aprox21": {
        "core": [
            "n", "p", "he3", "he4",
            "c12", "n14", "o16",
            "ne20", "mg24", "si28", "s32", "ar36", "ca40",
            "ti44", "cr48", "cr56", "fe52", "fe54", "fe56", "ni56",
        ],
        "intermediate": INTERMEDIATES_9,
        "cno_extra": [],
    },
}


def generate_network(name, core_nuclei, intermediate_nuclei, cno_extra):
    """Generate a single network using pynucastro SimpleCxxNetwork.

    Parameters
    ----------
    name : str
        Network name (e.g. 'aprox19')
    core_nuclei : list[str]
        Species that will be explicitly evolved in the ODE system.
    intermediate_nuclei : list[str]
        Species whose (α,p)(p,γ) reactions will be folded into
        approximate rates via QSS, then removed from the ODE system.
    cno_extra : list[str]
        Additional CNO intermediates for linking, removed after.
    """
    output_dir = os.path.join("src", "physics", "network", name)

    print(f"\n{'='*60}")
    print(f"Generating {name}")
    print(f"  Core nuclei ({len(core_nuclei)}): {core_nuclei}")
    print(f"  Intermediate ({len(intermediate_nuclei)}): {intermediate_nuclei}")
    print(f"  Output: {output_dir}")
    print(f"{'='*60}")

    # 1. Build the rate library by linking all relevant nuclei
    all_nuclei = core_nuclei + intermediate_nuclei + cno_extra
    rl = pyna.ReacLibLibrary()
    lib = rl.linking_nuclei(all_nuclei)

    # 2. Create SimpleCxxNetwork (no AMReX dependency)
    net = pyna.networks.SimpleCxxNetwork(libraries=[lib])

    # 3. Apply (α,p)(p,γ) approximation if intermediates exist
    if intermediate_nuclei:
        print(f"  Applying make_ap_pg_approx for {len(intermediate_nuclei)} intermediates...")
        net.make_ap_pg_approx(intermediate_nuclei=intermediate_nuclei)
        
        # Remove intermediate + CNO extra nuclei from ODE system
        remove_list = intermediate_nuclei + cno_extra
        if remove_list:
            print(f"  Removing {len(remove_list)} nuclei from ODE: {remove_list}")
            net.remove_nuclei(remove_list)
    elif cno_extra:
        net.remove_nuclei(cno_extra)

    # 4. Write the network to the output directory
    print(f"  Writing network to {output_dir}...")
    net.write_network(output_dir)

    # 5. Print summary
    print(f"\n  ✓ {name} generated successfully!")
    print(f"  Summary:")
    net.summary()
    print()


def main():
    print("=" * 60)
    print("ARCH Standard Network Generator")
    print("Based on Timmes/cococubed & AMReX-Astro/Microphysics")
    print("=" * 60)

    for name, spec in NETWORKS.items():
        try:
            generate_network(
                name,
                spec["core"],
                spec["intermediate"],
                spec["cno_extra"],
            )
        except Exception as e:
            print(f"\n  ✗ FAILED to generate {name}: {e}")
            import traceback
            traceback.print_exc()
            continue

    print("\n" + "=" * 60)
    print("All networks generated. Verify NumSpec values:")
    print("=" * 60)

    # Verify generated NumSpec values
    for name in NETWORKS:
        props_file = os.path.join("src", "physics", "network", name, "network_properties.H")
        if os.path.exists(props_file):
            with open(props_file) as f:
                for line in f:
                    if "NumSpec =" in line and "NumSpecExtra" not in line and "NumSpecTotal" not in line:
                        print(f"  {name}: {line.strip()}")
                        break
        else:
            print(f"  {name}: network_properties.H NOT FOUND!")


if __name__ == "__main__":
    main()
