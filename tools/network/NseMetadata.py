"""Certify the nuclear-data and reaction invariants needed by generated NSE.

This is a generation-time contract, not a second equilibrium solver. Exact
stoichiometric rank rules out conserved quantities beyond baryon number and
charge. Only unmodified detailed-balance pairs with ground-state partition
functions are enabled until the EOS includes nuclear excitation consistently.
"""

import json
import math
from collections import Counter
from fractions import Fraction


SCHEMA_VERSION = 1


def exact_rank(rows):
    """Rank over the rationals; a sampled floating-point rank is not a proof."""
    basis = {}
    for row in rows:
        remaining = {i: Fraction(value) for i, value in enumerate(row) if value}
        while remaining:
            pivot = min(remaining)
            if pivot not in basis:
                scale = remaining[pivot]
                basis[pivot] = {i: value / scale for i, value in remaining.items()}
                break
            scale = remaining[pivot]
            for i, value in basis[pivot].items():
                difference = remaining.get(i, Fraction(0)) - scale * value
                if difference:
                    remaining[i] = difference
                else:
                    remaining.pop(i, None)
    return len(basis)


def _finite(value, *, positive=False):
    return isinstance(value, (int, float)) and math.isfinite(value) and (
        not positive or value > 0)


def _nucleus_key(nucleus):
    return nucleus.A, nucleus.Z, nucleus.short_spec_name.lower()


def _matching_source(source, rates):
    for candidate in rates:
        if candidate is source:
            return candidate
    # RateCollection copies rate objects. Equality of a reaction name alone is
    # insufficient: the emitted forward expression must also be identical.
    if not callable(getattr(source, 'function_string_cxx', None)):
        return None
    matches = [candidate for candidate in rates
               if type(candidate) is type(source)
               and getattr(candidate, 'fname', None) == getattr(source, 'fname', None)
               and callable(getattr(candidate, 'function_string_cxx', None))
               and candidate.function_string_cxx() == source.function_string_cxx()]
    return matches[0] if len(matches) == 1 else None


def inspect_network(network, nuclei, *, constants=None, derived_rate_type=None,
                    forward_rate_type=None):
    """Return explicit eligibility and data provenance without changing rates.

    ``constants`` and both recognized rate types come from the same imported
    pynucastro package that constructs the network. Missing data disable NSE
    while leaving a valid ordinary reaction-network package usable.
    """
    reasons = []
    species = []
    missing = []
    for nucleus in nuclei:
        name = nucleus.short_spec_name.lower()
        binding = getattr(nucleus, 'nucbind', None)
        spin = getattr(nucleus, 'spin_states', None)
        mass_number = getattr(nucleus, 'A_nuc', None)
        mass_mev = getattr(nucleus, 'mass', None)
        complete = (_finite(binding) and _finite(spin, positive=True)
                    and _finite(mass_number, positive=True)
                    and _finite(mass_mev, positive=True))
        if not complete:
            missing.append(name)
        if not bool(getattr(nucleus, 'spin_reliable', False)):
            reasons.append('unreliable_spin_data')
        partition = getattr(nucleus, 'partition_function', None)
        partition_data = None
        if partition is not None:
            temperatures = [float(x) for x in getattr(partition, 'T9_points', [])]
            logarithms = [float(x) for x in getattr(partition, 'log_pf_data', [])]
            if (len(temperatures) < 2 or len(temperatures) != len(logarithms)
                    or any(not _finite(x, positive=True) for x in temperatures)
                    or any(not _finite(x) for x in logarithms)
                    or any(a >= b for a, b in zip(temperatures, temperatures[1:]))):
                reasons.append('unrecognized_partition_data')
            else:
                partition_data = {'temperature_gk': temperatures, 'log_partition': logarithms,
                                  'interpolation': 'linear_log_partition_in_temperature_gk'}
        species.append({'name': name, 'A': nucleus.A, 'Z': nucleus.Z,
                        'mass_mev': mass_mev if _finite(mass_mev) else None,
                        'mass_amu': mass_number if _finite(mass_number) else None,
                        'binding_mev': binding * nucleus.A if _finite(binding) else None,
                        'spin_weight': spin if _finite(spin) else None,
                        'spin_reliable': bool(getattr(nucleus, 'spin_reliable', False)),
                        'partition_data': partition_data})
    if missing:
        reasons.append('missing_nuclear_data')
    if bool(getattr(network, 'do_screening', True)):
        reasons.append('screening_model_not_supported')

    a = [n.A for n in nuclei]
    z = [n.Z for n in nuclei]
    constraint_rank = exact_rank([a, z])
    indices = {id(n): i for i, n in enumerate(nuclei)}
    # Species identities normally share the upstream cache. The stable A/Z/name
    # tuple also admits upstream rate objects containing equivalent instances.
    keys = {_nucleus_key(n): i for i, n in enumerate(nuclei)}
    strong_rows = []
    rates = list(network.rates)
    if not rates:
        reasons.append('no_strong_reactions')
    for rate in rates:
        if bool(getattr(rate, 'weak', False)):
            reasons.append('weak_rates_present')
            continue
        if getattr(rate, 'stoichiometry', None) is not None:
            reasons.append('modified_stoichiometry_not_supported')
        row = [0] * len(nuclei)
        for sign, entries in ((-1, rate.reactants), (1, rate.products)):
            for n in entries:
                index = indices.get(id(n), keys.get(_nucleus_key(n)))
                if index is None:
                    reasons.append('reaction_species_mismatch')
                else:
                    row[index] += sign
        if sum(x * y for x, y in zip(row, a)) or sum(x * y for x, y in zip(row, z)):
            reasons.append('strong_reaction_conservation_invalid')
        strong_rows.append(row)
    strong_rank = exact_rank(strong_rows)
    if strong_rank != len(nuclei) - constraint_rank:
        reasons.append('extra_conserved_quantities')

    paired = set()
    rate_ids = {id(rate) for rate in rates}
    if derived_rate_type is not None:
        for rate in rates:
            if type(rate) is not derived_rate_type:
                if isinstance(rate, derived_rate_type):
                    reasons.append('unrecognized_detailed_balance_rate_type')
                continue
            source = rate.source_rate
            if forward_rate_type is None or type(source) is not forward_rate_type:
                reasons.append('unrecognized_forward_rate_type')
            selected_source = _matching_source(source, rates)
            if selected_source is None:
                reasons.append('detailed_balance_source_missing')
                continue
            if (Counter(_nucleus_key(n) for n in rate.reactants) != Counter(_nucleus_key(n) for n in source.products)
                    or Counter(_nucleus_key(n) for n in rate.products) != Counter(_nucleus_key(n) for n in source.reactants)):
                reasons.append('detailed_balance_stoichiometry_mismatch')
                continue
            if bool(getattr(rate, 'use_pf', True)):
                # Upstream PF metadata are retained below, but thermal NSE must
                # not claim excitation energy absent from the selected EOS.
                reasons.append('partition_excitation_eos_not_supported')
            if getattr(rate, 'underlying_rate', source) is not source:
                reasons.append('modified_detailed_balance_source_not_supported')
            paired.update((id(rate), id(selected_source)))
    if rate_ids - paired:
        reasons.append('rates_not_certified_detailed_balance')

    constant_names = {
        'NSE_AVOGADRO': 'N_A', 'NSE_K_BOLTZMANN': 'k',
        'NSE_K_BOLTZMANN_MEV': 'k_MeV', 'NSE_PLANCK': 'h',
        'NSE_HBAR': 'hbar', 'NSE_ATOMIC_MASS_UNIT': 'm_u_C18',
        'NSE_MEV_TO_ERG': 'MeV2erg',
    }
    values = {name: getattr(constants, upstream, None)
              for name, upstream in constant_names.items()}
    if any(not _finite(value, positive=True) for value in values.values()):
        reasons.append('missing_nse_data_constants')
    values = {name: float(value) for name, value in values.items() if _finite(value, positive=True)}
    reasons = list(dict.fromkeys(reasons))
    return {'schema_version': SCHEMA_VERSION, 'eligible': not reasons,
            'reason': ','.join(reasons) if reasons else 'eligible_ground_state_detailed_balance',
            'reasons': reasons, 'missing_species': missing,
            'constraint_rank': constraint_rank, 'strong_stoichiometric_rank': strong_rank,
            'species_count': len(nuclei), 'species': species, 'constants': values,
            'mass_convention': 'pynucastro_Nucleus_A_nuc_and_nucbind_same_package',
            'energy_reference': 'generated_mion_conserved_baryon_gauge',
            'partition_policy': ('temperature_dependent' if
                                 'partition_excitation_eos_not_supported' in reasons else 'ground_state_only'),
            'screening_policy': 'unsupported' if bool(getattr(network, 'do_screening', True)) else 'none',
            'weak_policy': 'unsupported' if 'weak_rates_present' in reasons else 'none',
            'temperature_min': 0.0, 'temperature_max': None}


def cpp_interface(metadata):
    """Emit immutable scalar accessors usable by the shared CPU/device solver."""
    lines = [f'    static constexpr int NSE_DATA_VERSION = {SCHEMA_VERSION};',
             f'    static constexpr int NSE_CONSTRAINT_RANK = {metadata["constraint_rank"]};',
             f'    static constexpr const char* NSE_INELIGIBILITY_REASON = {json.dumps(metadata["reason"])};']
    if not metadata['eligible']:
        return '\n'.join(lines)
    for name, value in metadata['constants'].items():
        lines.append(f'    static constexpr double {name} = {value:.17g};')
    lines += ['    static constexpr double NSE_T_MIN = 0.0;',
              '    static constexpr double NSE_T_MAX = std::numeric_limits<double>::max();']
    for name, key in (('zion', 'Z'), ('binding_energy', 'binding_mev'),
                      ('spin_weight', 'spin_weight'), ('nse_mass_number', 'mass_amu')):
        lines.append(f'    ARCH_HOST_DEVICE static double {name}(int index) {{')
        lines.append('        switch (index) {')
        for i, species in enumerate(metadata['species']):
            lines.append(f'        case {i}: return {float(species[key]):.17g};')
        lines += ['        default: return std::numeric_limits<double>::quiet_NaN();', '        }', '    }']
    lines += ['    // DerivedRate(use_pf=false) uses ground-state degeneracy only.',
              '    ARCH_HOST_DEVICE static double nse_log_partition(int, double) { return 0.0; }',
              '    ARCH_HOST_DEVICE static double nse_partition_dlog_dT(int, double) { return 0.0; }']
    return '\n'.join(lines)
