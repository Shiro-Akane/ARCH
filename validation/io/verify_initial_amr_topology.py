#!/usr/bin/env python3
"""Read-only exact Cartesian hierarchy checks; no field/physics or timestep oracle."""
import argparse
import hashlib
import json
import math
import re
from collections import Counter
from fractions import Fraction
from itertools import combinations
from pathlib import Path


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--response', required=True, type=Path)
    p.add_argument('--input', required=True, type=Path)
    p.add_argument('--case', required=True)
    p.add_argument('--request-id', required=True)
    args = p.parse_args()
    raw = args.input.read_bytes()
    response = json.loads(args.response.read_text())
    assert response['identity']['configRevision'] == hashlib.sha256(raw).hexdigest()
    assert response['kind'] == 'initial-amr-preview' and response['schemaVersion'] == '1.0'
    assert response['identity']['caseId'] == args.case
    assert response['identity']['requestId'] == args.request_id
    mesh = response['data']
    assert (response['status'] == 'ok') == mesh['complete']
    assert mesh['snapshot'] == 'last-completed-balanced-hierarchy'
    assert mesh['geometry'] == 'cartesian' and mesh['dimension'] in (1, 2, 3)
    assert response['execution']['timeStepping'] == 'not_executed'
    assert response['execution']['scientificOutput'] == 'not_created'
    # Explicit decimal domain from the external input, not reconstructed from leaves.
    def supplied(key):
        values = re.findall(r'^\s*' + re.escape(key) + r'\s*=\s*([^#\n]+)', raw.decode(), re.M)
        assert len(values) == 1, key
        return Fraction(values[0].strip())
    dim = mesh['dimension']
    domain = [(supplied(f'x{i+1}_min'), supplied(f'x{i+1}_max')) for i in range(dim)]
    volume = lambda bounds: math.prod(hi - lo for lo, hi in bounds)
    leaves = mesh['leaves']
    assert leaves and len(leaves) == mesh['leafCount'] <= 1024
    assert all(len(l['lower']) == len(l['upper']) == dim for l in leaves)
    boxes = [[(Fraction(lo), Fraction(hi)) for lo, hi in zip(l['lower'], l['upper'])] for l in leaves]
    assert all(len(box) == dim for box in boxes)
    assert all(dlo <= lo < hi <= dhi for box in boxes for (lo, hi), (dlo, dhi) in zip(box, domain))
    overlap_pairs, face_pairs, unbalanced_pairs = 0, 0, 0
    for ia, ib in combinations(range(len(boxes)), 2):
        widths = [min(a[1], b[1]) - max(a[0], b[0]) for a, b in zip(boxes[ia], boxes[ib])]
        if all(w > 0 for w in widths):
            overlap_pairs += 1
        if widths.count(0) == 1 and sum(w > 0 for w in widths) == dim - 1:
            face_pairs += 1
            if abs(leaves[ia]['level'] - leaves[ib]['level']) > 1:
                unbalanced_pairs += 1
    actual_volume, expected_volume = sum(map(volume, boxes), Fraction(0)), volume(domain)
    assert overlap_pairs == 0 and unbalanced_pairs == 0
    assert actual_volume == expected_volume, 'Exact stored leaf partition does not cover external Cartesian domain'
    # Exact topology scope: no tolerance introduced. Curved measures and
    # floating endpoint uncertainty require a separate owner-approved verifier.
    counts = Counter(l['level'] for l in leaves)
    declared = {l['level']: l['leafBlocks'] for l in mesh['levelCounts'] if l['leafBlocks']}
    assert dict(counts) == declared
    print(json.dumps({'version': 'cartesian-initial-amr-topology-1',
                      'inputSha256': hashlib.sha256(raw).hexdigest(),
                      'dimension': dim, 'leafCount': len(leaves), 'levelCounts': dict(counts),
                      'overlapPairs': overlap_pairs, 'facePairs': face_pairs,
                      'unbalancedFacePairs': unbalanced_pairs,
                      'exactLeafVolume': str(actual_volume),
                      'exactInputDomainVolume': str(expected_volume),
                      'scope': 'Stored Cartesian box partition and 2:1 topology only; not cell fields or scientific evolution'}, indent=2))


if __name__ == '__main__':
    main()
