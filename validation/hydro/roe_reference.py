"""Independent ideal-gas flux fixtures for the shared hydro leaf regression.

The Euler Jacobian eigenbasis defines Roe dissipation by a high-precision linear
solve, not the production wave-amplitude formulas. HLL integrates two waves;
HLLC solves contact pressure/velocity from Rankine-Hugoniot and then energy from
the moving-wave conservation equation. No ARCH code or sampled output is read.
The 70/90-digit agreement and Roe conservative jump identity check this oracle.
"""
import json
from pathlib import Path
import re
import mpmath as mp


def references(digits):
    with mp.workdps(digits):
        gamma = mp.mpf('1.4')

        def thermodynamics(q):
            rho = q[0]
            velocity = q[1:4, 0] / rho
            pressure = (gamma - 1) * (q[4] - rho * mp.fdot(velocity, velocity) / 2)
            enthalpy = (q[4] + pressure) / rho
            flux = mp.matrix([q[1], q[1] * velocity[0] + pressure,
                              q[1] * velocity[1], q[1] * velocity[2],
                              velocity[0] * (q[4] + pressure)])
            return rho, velocity, pressure, enthalpy, flux

        def face(left, right, composition, entropy_coefficient='.1'):
            ql, qr = (mp.matrix([mp.mpf(v) for v in q]) for q in (left, right))
            rl, vl, pl, hl, fl = thermodynamics(ql)
            rr, vr, pr, hr, fr = thermodynamics(qr)
            wl, wr = mp.sqrt(rl), mp.sqrt(rr)
            v = (wl * vl + wr * vr) / (wl + wr)
            h = (wl * hl + wr * hr) / (wl + wr)
            kinetic = mp.fdot(v, v) / 2
            c = mp.sqrt((gamma - 1) * (h - kinetic))
            u, transverse1, transverse2 = v
            columns = ([1, u-c, transverse1, transverse2, h-u*c],
                       [1, u, transverse1, transverse2, kinetic],
                       [0, 0, 1, 0, transverse1],
                       [0, 0, 0, 1, transverse2],
                       [1, u+c, transverse1, transverse2, h+u*c])
            basis = mp.matrix(columns).T
            eigenvalues = mp.matrix([u-c, u, u, u, u+c])
            amplitudes = mp.lu_solve(basis, qr-ql)
            jump = basis * mp.diag(eigenvalues) * amplitudes - (fr-fl)
            jump_error = max(abs(value) for value in jump)
            assert jump_error < mp.mpf(10) ** (10-digits)
            width = mp.mpf(entropy_coefficient) * (abs(u) + c)
            magnitudes = [abs(value) if abs(value) >= width
                          else (value*value + width*width)/(2*width)
                          for value in eigenvalues]
            roe = (fl + fr - basis * mp.diag(magnitudes) * amplitudes) / 2
            sl = min(vl[0] - mp.sqrt(gamma*pl/rl), u-c)
            sr = max(vr[0] + mp.sqrt(gamma*pr/rr), u+c)
            assert sl < 0 < sr
            hll = (sr*fl - sl*fr + sl*sr*(qr-ql)) / (sr-sl)
            # Solve the two momentum jumps for the common contact state.
            ml, mr = rl*(sl-vl[0]), rr*(sr-vr[0])
            contact, pressure = mp.lu_solve(mp.matrix([[ml, -1], [mr, -1]]),
                                          mp.matrix([ml*vl[0]-pl, mr*vr[0]-pr]))
            q, density, velocity, p, f, speed = (
                (ql, rl, vl, pl, fl, sl) if contact >= 0 else (qr, rr, vr, pr, fr, sr))
            star_density = density*(speed-velocity[0])/(speed-contact)
            star_energy = ((speed-velocity[0])*q[4] - p*velocity[0]
                           + pressure*contact)/(speed-contact)
            star = mp.matrix([star_density, star_density*contact,
                              star_density*velocity[1], star_density*velocity[2], star_energy])
            hllc = f + speed*(star-q)
            result = {}
            for name, flux in (('hll', hll), ('hllc', hllc), ('roe', roe)):
                fractions = composition[0 if flux[0] >= 0 else 1]
                result[name] = [mp.nstr(value, 60) for value in flux]
                result[name + '_species'] = [mp.nstr(flux[0]*mp.mpf(x), 60) for x in fractions]
            result['roe_jump_error'] = mp.nstr(jump_error, 8)
            return result

        result = dict(discontinuous=face(
            ['1', '.75', '-.2', '.1', '2.80625'],
            ['.8', '-.2', '.24', '-.12', '1.82'], [['.4', '.6'], ['.7', '.3']]),
            linear_pcm=face(['1', '.2', '.1', '.05', '3'],
                            ['1.05', '.23', '.09', '.07', '3.1'],
                            [['.35', '.65'], ['.37', '.63']]))
        result['policy_resolution'] = face(
            ['1', '.7', '.1', '-.05', '3.4'],
            ['.42', '-.08', '.02', '.03', '1.1'], [[], []], '.9')
        # Independent limited slopes for the existing polynomial route stencil.
        # These algebraic minmod/MC/superbee/harmonic definitions do not invoke
        # ARCH reconstruction or registry dispatch.
        coefficients = (('1.2', '.06', '.013'), ('.31', '.047', '-.009'),
                        ('-.08', '.019', '.004'), ('.04', '-.011', '.002'),
                        ('3.7', '.21', '.027'))
        stencil = [[mp.mpf(a) + mp.mpf(b)*x + mp.mpf(c)*x*x for a, b, c in coefficients]
                   for x in (-1, 0, 1, 2)]
        def slope(a, b, limiter):
            if a*b <= 0:
                return mp.mpf(0)
            sign, a, b = mp.sign(a), abs(a), abs(b)
            limited = (min(a, b), min(2*a, (a+b)/2, 2*b),
                       max(min(2*a, b), min(a, 2*b)), 2*a*b/(a+b))[limiter]
            return sign * limited / 2
        for limiter in range(4):
            left = [stencil[1][j] + slope(stencil[1][j]-stencil[0][j],
                                         stencil[2][j]-stencil[1][j], limiter) for j in range(5)]
            right = [stencil[2][j] - slope(stencil[2][j]-stencil[1][j],
                                          stencil[3][j]-stencil[2][j], limiter) for j in range(5)]
            record = face(left, right, [[], []])
            record['left'] = [mp.nstr(v, 60) for v in left]
            record['right'] = [mp.nstr(v, 60) for v in right]
            result['route_' + str(limiter)] = record
        return result


def main():
    low, high = references(70), references(90)
    for case in high:
        for field in high[case]:
            if field == 'roe_jump_error' or not high[case][field]:
                continue
            with mp.workdps(90):
                assert max(abs(mp.mpf(a)-mp.mpf(b)) for a, b in
                           zip(low[case][field], high[case][field])) < mp.mpf('1e-55')
    fixture = Path(__file__).resolve().parents[2] / 'tests/fixtures/RoeFluxReference.h'
    declarations = dict(re.findall(r'double (\w+)\[7\] = \{([^}]+)\}', fixture.read_text()))
    assert set(declarations) == {'hll', 'hllc', 'roe', 'linear_pcm'}
    for name, declaration in declarations.items():
        case, scheme = ('linear_pcm', 'hll') if name == 'linear_pcm' else ('discontinuous', name)
        expected = high[case][scheme] + high[case][scheme + '_species']
        actual = [token.strip() for token in declaration.split(',')]
        with mp.workdps(90):
            assert len(actual) == 7 and all(abs(mp.mpf(a)-mp.mpf(b)) < mp.mpf('1e-29')
                                          for a, b in zip(actual, expected))
    for name, fields, extent in (('route_flux', ('roe', 'hll', 'hllc'), 3),
                                 ('route_state', ('left', 'right'), 2)):
        match = re.search(r'double ' + name + r'\[4\]\[' + str(extent)
                          + r'\]\[5\] = \{(.*?)\};', fixture.read_text(), re.S)
        assert match is not None
        actual = re.findall(r'[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?', match[1])
        expected = [v for limiter in range(4) for field in fields for v in high['route_'+str(limiter)][field]]
        # The route fixture stores round-trip binary64 decimals. Its rounding
        # must match the high-precision oracle exactly, independently of ARCH.
        assert len(actual) == len(expected) and all(float(a) == float(b) for a, b in zip(actual, expected))
    policy = re.search(r'double policy_flux\[3\]\[5\] = \{(.*?)\};', fixture.read_text(), re.S)
    assert policy is not None
    actual = re.findall(r'[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?', policy[1])
    expected = [v for field in ('roe', 'hll', 'hllc') for v in high['policy_resolution'][field]]
    assert len(actual) == len(expected) and all(float(a) == float(b) for a, b in zip(actual, expected))
    print(json.dumps(dict(precision_agreement=True, fixture_agreement=True, references=high), indent=2))


if __name__ == '__main__':
    main()
