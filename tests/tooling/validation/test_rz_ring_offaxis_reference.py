"""Independent reference identities, not production scientific acceptance."""
import sys
import unittest
from pathlib import Path
from decimal import Decimal, localcontext
sys.path.insert(0, str(Path(__file__).resolve().parents[3]/"validation/gravity"))
import rz_ring_offaxis_reference as rz
from rz_ring_axis_reference import axis_reference

class OffAxisTests(unittest.TestCase):
    def case(self, **extra):
        return dict(rz.CASES[1], **extra)

    def test_agm_known_values_and_modulus_convention(self):
        with localcontext() as ctx:
            ctx.prec=80
            k,e=rz.elliptic_ke(Decimal(0))
            self.assertEqual(k, rz.PI/2)
            self.assertEqual(e,k)
            k,e=rz.elliptic_ke(Decimal(".5"))
            self.assertEqual(float(k),1.8540746773013719)
            self.assertEqual(float(e),1.3506438810476755)
            for bad in [Decimal(-1),Decimal(1)]:
                with self.assertRaises(ValueError):rz.elliptic_ke(bad)

    def test_gauss_polynomial_moments(self):
        with localcontext() as ctx:
            ctx.prec=60
            for n in [2,8,16]:
                nodes=rz.decimal_gauss(n,60)
                self.assertEqual(len(nodes),n)
                for power in range(2*n):
                    actual=sum(w*x**power for x,w in nodes)
                    exact=Decimal(2)/(power+1) if power%2==0 else Decimal(0)
                    self.assertLess(abs(actual-exact),Decimal("1e-50"))

    def test_axis_matches_closed_form(self):
        case=self.case(r_observer=0,z_observer=3)
        actual=rz.finite_volume_reference(case,32,80)
        expected=axis_reference(0,2,-1,1,3,3,80)
        for k,v in expected.items():
            if v:self.assertAlmostEqual(float(actual[k])/v,1,places=13)
            else:self.assertEqual(actual[k],0)

    def test_symmetry_density_translation_and_direction(self):
        a=rz.finite_volume_reference(self.case(),16,60)
        b=rz.finite_volume_reference(self.case(z_observer=-2),16,60)
        self.assertAlmostEqual(float(a["potential"]/b["potential"]),1,places=14)
        self.assertAlmostEqual(float(a["radial_acceleration"]/b["radial_acceleration"]),1,places=14)
        self.assertAlmostEqual(float(a["axial_acceleration"]/b["axial_acceleration"]),-1,places=14)
        self.assertLess(a["radial_acceleration"],0);self.assertLess(a["axial_acceleration"],0)
        translated=rz.finite_volume_reference(self.case(z_lower=4,z_upper=6,z_observer=7),16,60)
        doubled=rz.finite_volume_reference(self.case(density=6),16,60)
        for key in a:
            self.assertAlmostEqual(float(translated[key]/a[key]),1,places=14)
            self.assertAlmostEqual(float(doubled[key]/a[key]),2,places=14)
        cavity=rz.finite_volume_reference(rz.CASES[3],16,60)
        self.assertGreater(cavity["radial_acceleration"],0)
        self.assertEqual(cavity["axial_acceleration"],0)
        for case in [rz.CASES[0],rz.CASES[3],rz.CASES[4]]:
            low=rz.finite_volume_reference(case,16,60)
            high=rz.finite_volume_reference(case,16,100)
            self.assertEqual({k:float(v) for k,v in low.items()},
                             {k:float(v) for k,v in high.items()})

    def test_force_is_negative_potential_gradient(self):
        case=self.case()
        base=rz.finite_volume_reference(case,16,60)
        with localcontext() as ctx:
            ctx.prec=60
            h=Decimal("1e-6")
            for coordinate,force in [("r_observer","radial_acceleration"),
                                     ("z_observer","axial_acceleration")]:
                c=Decimal(case[coordinate])
                left=rz.finite_volume_reference(dict(case,**{coordinate:c-h}),16,60)["potential"]
                right=rz.finite_volume_reference(dict(case,**{coordinate:c+h}),16,60)["potential"]
                derivative=-(right-left)/(2*h)
                self.assertAlmostEqual(float(derivative/base[force]),1,places=10)

    def test_partition_preserves_full_source_mass_and_symmetry(self):
        case=rz.CASES[0]
        whole=rz.finite_volume_reference(case,32,80)
        divided=rz.partitioned_reference(case,16,80)
        self.assertEqual(float(whole["mass"]),float(divided["mass"]))
        self.assertEqual(whole["axial_acceleration"],0)
        # Opposite source halves must cancel, including Decimal accumulation.
        self.assertEqual(divided["axial_acceleration"],0)
        for key in ["potential","radial_acceleration"]:
            self.assertAlmostEqual(float(divided[key]/whole[key]),1,places=13)

    def test_full_volume_multipole_force_gradient_and_trace(self):
        case=rz.CASES[-1]
        result=rz.finite_source_multipoles(case)
        self.assertTrue(result["available"])
        q=result["quadrupole"]
        self.assertEqual(q["Qxx"]+q["Qyy"]+q["Qzz"],0)
        h=1e-3
        for key,force in [("r_observer","radial_acceleration"),
                          ("z_observer","axial_acceleration")]:
            left=rz.finite_source_multipoles(dict(case,**{key:case[key]-h}))
            right=rz.finite_source_multipoles(dict(case,**{key:case[key]+h}))
            gradient=-(right["through_quadrupole"]["potential"]-left["through_quadrupole"]["potential"])/(2*h)
            self.assertAlmostEqual(gradient/result["through_quadrupole"][force],1,places=8)
        self.assertFalse(rz.finite_source_multipoles(rz.CASES[3])["available"])

    def test_invalid_or_singular_observers_rejected(self):
        for kwargs in [dict(r_observer=1,z_observer=0),
                       dict(r_observer=2,z_observer=1),
                       dict(r_observer=-1),dict(density=0),
                       dict(z_observer=float("nan"))]:
            with self.assertRaises(ValueError):
                rz.finite_volume_reference(self.case(**kwargs),8,60)
        # A hollow cavity is outside source despite being within outer cylinder.
        self.assertLess(rz.finite_volume_reference(rz.CASES[3],8,60)["potential"],0)



# The existing Decimal off-axis owner remains intact. Exact-key/resource tests
# below are engineering only; one real optional arb method is explicitly opted
# in, with no missing-backend skip or manufactured numerical PASS.
import json
import os
from fractions import Fraction
from types import SimpleNamespace
from unittest.mock import patch
import rz_ring_surface_reference as surface


class SurfaceGeometryMemoEngineeringTests(unittest.TestCase):
    def geometry(self):
        """Use exact supplied rational geometry, never production/fixed-layout claims."""
        return (surface.Leaf("actual",Fraction(1),Fraction(3),Fraction(-1),Fraction(1),Fraction(2)),
                surface.Observer("observer",Fraction(2),Fraction(2)))

    def test_translation_reflection_and_exact_key_collision(self):
        leaf,observer = self.geometry()
        precision = (surface.PROFILE,"engineering-no-backend",70,233)
        widths = (Fraction(1,10**20),)*3
        key,unit,origin,sign = surface._unit_geometry(leaf,observer,precision,widths)
        shift = Fraction(10**200)
        shifted = surface.Leaf("other",leaf.L,leaf.H,leaf.A+shift,leaf.B+shift,Fraction(17))
        moved = surface.Observer("other-observer",observer.R,observer.Z+shift)
        self.assertEqual(surface._unit_geometry(shifted,moved,precision,widths)[0],key)
        self.assertEqual((unit.rho,origin.Z),(1,0))
        mirrored = surface.Observer("reflected",observer.R,-observer.Z)
        reflected = surface._unit_geometry(leaf,mirrored,precision,widths)
        self.assertEqual(reflected[0],key)
        self.assertEqual(reflected[3],-sign)
        # These different rational radii collapse to the SAME float; memo must not.
        delta = Fraction(1,2**100)
        near = surface.Observer("different",observer.R+delta,observer.Z)
        self.assertEqual(float(near.R),float(observer.R))
        self.assertNotEqual(surface._unit_geometry(leaf,near,precision,widths)[0],key)
        self.assertNotEqual(surface._unit_geometry(leaf,observer,precision,tuple(w/2 for w in widths))[0],key)
        different_method = ("different-method",)+precision[1:]
        self.assertNotEqual(surface._unit_geometry(leaf,observer,different_method,widths)[0],key)

    def test_same_budget_ownership_expiry_and_context_drift(self):
        backend = SimpleNamespace(ctx=SimpleNamespace(dps=70,prec=233),__version__="ENGINEERING")
        budget = surface.Budget(started=1.,calls=17)
        with patch.object(surface.time,"monotonic",return_value=2.):
            memo = budget._memo_for(backend)
            self.assertIs(budget._memo_for(backend),memo)
            self.assertEqual((budget.started,budget.calls),(1.,17))
            backend.ctx.prec += 1
            with self.assertRaisesRegex(surface.ReferenceFailure,"drift"):
                budget._memo_for(backend)
            backend.ctx.prec -= 1
        with patch.object(surface.time,"monotonic",return_value=92.), \
             patch.object(surface,"integrate_leaf",side_effect=AssertionError("kernel reached")) as kernel:
            leaf,observer = self.geometry()
            with self.assertRaises(surface.WorkLimit):
                memo.integrate(leaf,observer,(Fraction(1),)*3,(Fraction(1),)*3)
            kernel.assert_not_called()
        self.assertEqual((budget.started,budget.calls),(1.,17))

    def test_capacity_invalid_and_exhausted_budget_are_not_new_profiles(self):
        backend = SimpleNamespace(ctx=SimpleNamespace(dps=70,prec=233),__version__="ENGINEERING")
        budget = surface.Budget.start()
        for capacity in (-1,65537,True,1.5):
            with self.subTest(capacity=capacity),self.assertRaises(surface.ReferenceFailure):
                surface._LeafGeometryMemo(backend,budget,capacity=capacity)
        memo = surface._LeafGeometryMemo(backend,budget,capacity=1)
        leaf,observer = self.geometry(); budget.calls=budget.max_calls
        started = budget.started
        def charge_actual_same_budget(*args):
            self.assertIs(args[-1],budget)
            budget.take()
        with patch.object(surface,"integrate_leaf",side_effect=charge_actual_same_budget):
            with self.assertRaises(surface.WorkLimit):
                memo.integrate(leaf,observer,(Fraction(1),)*3,(Fraction(1),)*3)
        self.assertEqual((budget.started,budget.calls,budget.timeout_seconds),
                         (started,100000,90.))
        self.assertEqual(memo.record()["entries"],0)


class SurfaceGeometryMemoOptionalArbTests(unittest.TestCase):
    def test_optional_actual_arb_geometry_density_contact_and_width_guards(self):
        """Real unchanged kernels under ONE90s/100000 budget; no Core science grant."""
        self.assertEqual(os.environ.get("ARCH_VALIDATE_OPTIONAL_REFERENCES"),"1")
        backend = surface.load_optional_flint()  # Requested absence is an ERROR.
        original_precision = backend.ctx.prec
        backend.ctx.dps = 70
        try:
            budget = surface.Budget.start()
            memo = budget._memo_for(backend)
            widths = (surface.CGS_G/Fraction(10**12),)*3
            unit = tuple(w/8 for w in widths)
            leaf = surface.Leaf("actual",Fraction(1),Fraction(3),Fraction(-1),Fraction(1),Fraction(2))
            observer = surface.Observer("exterior",Fraction(4),Fraction(2))
            direct = surface._LeafGeometryMemo(backend,budget,capacity=0)
            first = memo.integrate(leaf,observer,widths,unit)
            raw = direct.integrate(leaf,observer,widths,unit)
            for a,b in zip(first,raw): self.assertTrue(a.overlaps(b))
            self.assertTrue(all(value < 0 for value in first))
            charged = budget.calls
            shifted = surface.Leaf("translated",leaf.L,leaf.H,leaf.A+5,leaf.B+5,Fraction(8))
            moved = surface.Observer("translated-observer",observer.R,observer.Z+5)
            translated = memo.integrate(shifted,moved,widths,unit)
            reflected = memo.integrate(leaf,surface.Observer("reflected",observer.R,-observer.Z),widths,unit)
            self.assertEqual(budget.calls,charged)
            for a,b in zip(translated,first): self.assertTrue(a.overlaps(b*4))
            for index,(a,b) in enumerate(zip(reflected,first)):
                self.assertTrue(a.overlaps(-b if index==2 else b))
            self.assertTrue(reflected[2] > 0)
            # Actual axis/cap/cylinder/contact branches remain the old formulas.
            for R,Z in ((0,3),(2,1),(3,0),(1,-1)):
                site=surface.Observer("contact-"+str((R,Z)),Fraction(R),Fraction(Z))
                cached=memo.integrate(leaf,site,widths,unit)
                plain=direct.integrate(leaf,site,widths,unit)
                for a,b in zip(cached,plain): self.assertTrue(a.overlaps(b))
                self.assertTrue(surface._widths_meet(cached,widths))
                self.assertTrue(cached[0] < 0)
                if not R: self.assertEqual(cached[1],backend.arb(0));self.assertTrue(cached[2] < 0)
                if Z==1: self.assertTrue(cached[2] < 0)
            # Widen a genuine enclosure, then request a tighter width: actual
            # bounds remain truthful, but cannot be reused merely by its label.
            loose=(surface.CGS_G/Fraction(10**6),)*3
            refine=surface._LeafGeometryMemo(backend,budget,capacity=1)
            original_kernel=surface.integrate_leaf
            def genuine_wider_kernel(*args):
                actual=original_kernel(*args)
                radius=surface._exact(backend.arb,loose[0]/32)
                return tuple(value+backend.arb(0,radius) for value in actual)
            with patch.object(surface,"integrate_leaf",side_effect=genuine_wider_kernel):
                refine.integrate(leaf,observer,loose,loose)
            calls=budget.calls
            tighter=tuple(w/1000 for w in loose)
            narrowed=refine.integrate(leaf,observer,tighter,tighter)
            self.assertGreater(budget.calls,calls)
            self.assertGreater(refine.record()["refinements"],0)
            self.assertTrue(surface._widths_meet(narrowed,tighter))
            for a,b in zip(narrowed,raw): self.assertTrue(a.overlaps(b))
            # Saturation declines only a memo admission, not a physical source.
            before=refine.record()["entries"]
            refine.integrate(leaf,surface.Observer("new",Fraction(5),Fraction(2)),widths,unit)
            self.assertEqual((before,refine.record()["entries"]),(1,1))
            self.assertGreater(refine.record()["capacity_refusals"],0)
            self.assertEqual(direct.record()["entries"],0)
            # Real evaluate_reference calls borrow the SAME memo from this Budget.
            source={"sourceId":"actual-axis", "leaves":[dict(id="one",r_lower=1,r_upper=3,z_lower=-1,z_upper=1,density=2)]}
            identity=dict(topology=1,operator_revision=1,boundary_revision=1,accuracy_revision=1,
                          generation=1,time=0,G=surface.CGS_G,inputs=[dict(uid=1,epoch=1,slot=0,version=1,storage_generation=1)])
            observers=[dict(id="axis",r_observer=0,z_observer=3)]
            encoded_widths=dict(zip(("Phi","g_r","g_z"),map(str,widths)))
            # Exact rationals are represented as strings only for the original JSON stamp.
            identity["G"]=str(identity["G"])
            a=surface.evaluate_reference(source,[1,3,-1,1],identity,observers,encoded_widths,_shared_budget=budget)
            self.assertTrue(a["certified"],a.get("failure"))
            prior=budget.calls
            source2={"sourceId":"translated-axis", "leaves":[dict(id="two",r_lower=1,r_upper=3,z_lower=4,z_upper=6,density=2)]}
            b=surface.evaluate_reference(source2,[1,3,4,6],identity,[dict(id="axis-shift",r_observer=0,z_observer=8)],
                                         encoded_widths,_shared_budget=budget)
            self.assertTrue(b["certified"],b.get("failure"))
            self.assertEqual(budget.calls,prior)
            self.assertFalse(a["science_accepted"]);self.assertFalse(b["core_binding_qualified"])
            # Keep ORIGINAL final-sum gate: an injected wide real enclosure is
            # an explicit engineering refusal, never a synthetic physical PASS.
            with patch.object(surface._LeafGeometryMemo,"integrate",return_value=(backend.arb(0,1),)*3):
                bad=surface.evaluate_reference(source,[1,3,-1,1],identity,observers,encoded_widths,_shared_budget=budget)
            self.assertEqual(bad["status"],"Unverified");self.assertFalse(bad["certified"])
        finally:
            backend.ctx.prec=original_precision



class SurfaceDensityContrastEngineeringTests(unittest.TestCase):
    """Exact rational identity checks; no integral, Runtime or scientific grant."""
    def identity(self):
        return dict(topology=1,operator_revision=1,boundary_revision=1,accuracy_revision=1,
            generation=1,time=0,G=str(surface.CGS_G),
            inputs=[dict(uid=1,epoch=1,slot=0,version=1,storage_generation=1)])

    def source(self, densities=(1,3,3)):
        return dict(sourceId="engineering-dense",leaves=[dict(id=str(i),r_lower=i+1,
            r_upper=i+2,z_lower=-1,z_upper=1,density=str(rho)) for i,rho in enumerate(densities)])

    def test_exact_modal_volume_signed_identity_preserves_original_positive_source(self):
        import copy
        source=self.source();before=copy.deepcopy(source);root=[1,4,-1,1]
        budget=surface.Budget.full_domain_extended()
        leaves=surface.validate_dense_source(source,root,self.identity(),budget)
        terms,metadata=surface._exact_density_contrast(leaves,root,budget)
        self.assertEqual(metadata["background_density_exact"],"3")
        self.assertEqual((len(terms),metadata["negative_contrast_terms"],metadata["zero_contrast_leaves"]),(2,1,2))
        self.assertTrue(all(leaf.rho > 0 for leaf,sign in terms))
        for original in leaves:
            r=(original.L+original.H)/2;z=(original.A+original.B)/2
            actual=sum(sign*leaf.rho for leaf,sign in terms if leaf.L < r < leaf.H and leaf.A < z < leaf.B)
            self.assertEqual(actual,original.rho)
        self.assertEqual(metadata["original_mass_over_pi_exact"],metadata["represented_mass_over_pi_exact"])
        self.assertEqual(source,before)

    def test_exact_tie_zero_and_sub_fp64_contrast_are_not_clamped(self):
        budget=surface.Budget.full_domain_extended();identity=self.identity()
        source=dict(sourceId="tied",leaves=[dict(id="a",r_lower=1,r_upper=2,z_lower=-1,z_upper=0,density=3),
            dict(id="b",r_lower=1,r_upper=2,z_lower=0,z_upper=1,density=1)])
        leaves=surface.validate_dense_source(source,[1,2,-1,1],identity,budget)
        terms,metadata=surface._exact_density_contrast(leaves,[1,2,-1,1],budget)
        self.assertEqual(metadata["background_density_exact"],"1")
        self.assertEqual(surface._exact_density_contrast(tuple(reversed(leaves)),[1,2,-1,1],budget)[1],metadata)
        for rho,count in ((0,0),(7,1)):
            source=self.source((rho,rho,rho));leaves=surface.validate_dense_source(source,[1,4,-1,1],identity,budget)
            self.assertEqual(len(surface._exact_density_contrast(leaves,[1,4,-1,1],budget)[0]),count)
        tiny=Fraction(1,2**1100)
        source=self.source((1,1,1+tiny));leaves=surface.validate_dense_source(source,[1,4,-1,1],identity,budget)
        terms,metadata=surface._exact_density_contrast(leaves,[1,4,-1,1],budget)
        self.assertEqual(metadata["background_density_exact"],"1")
        self.assertEqual(terms[-1][0].rho,tiny)

    def test_invalid_physical_density_coverage_and_original_profiles_reject_before_contrast(self):
        source=self.source();observers=[dict(id="axis",r_observer=0,z_observer=3)]
        widths={key:str(surface.CGS_G/Fraction(10**12)) for key in ("Phi","g_r","g_z")}
        for fault in ("negative","hole","overlap"):
            import copy
            changed=copy.deepcopy(source)
            if fault=="negative":changed["leaves"][0]["density"]="-1"
            elif fault=="hole":changed["leaves"].pop()
            else:changed["leaves"][1]["r_lower"]=1
            with self.subTest(fault=fault),patch.object(surface,"load_optional_flint",side_effect=AssertionError("backend reached")) as load:
                result=surface.evaluate_reference(changed,[1,4,-1,1],self.identity(),observers,widths,
                    _shared_budget=surface.Budget.full_domain_extended())
            self.assertFalse(result["certified"]);self.assertNotIn("density_decomposition",result);load.assert_not_called()
        for budget in (surface.Budget.start(),surface.Budget.full_domain_diagnostic(),surface.Budget.matched_resolution(1)):
            with patch.object(surface,"_exact_density_contrast",side_effect=AssertionError("ordinary source changed")) as contrast, \
                 patch.object(surface,"load_optional_flint",side_effect=surface.ReferenceFailure("engineering stop")):
                result=surface.evaluate_reference(source,[1,4,-1,1],self.identity(),observers,widths,_shared_budget=budget)
            contrast.assert_not_called();self.assertFalse(result["certified"])


class SurfaceDensityContrastOptionalArbTests(unittest.TestCase):
    def test_optional_actual_arb_signed_axis_against_original_primitive(self):
        """Independent log antiderivative and original positive leaves at 70dps."""
        self.assertEqual(os.environ.get("ARCH_VALIDATE_OPTIONAL_REFERENCES"),"1")
        backend=surface.load_optional_flint();precision=backend.ctx.prec;backend.ctx.dps=70
        try:
            fixture=SurfaceDensityContrastEngineeringTests();source=fixture.source();root=[1,4,-1,1]
            identity=fixture.identity();observers=[dict(id="axis",r_observer=0,z_observer=3)]
            widths={key:str(surface.CGS_G/Fraction(10**12)) for key in ("Phi","g_r","g_z")}
            budget=surface.Budget.full_domain_extended();started=budget.started
            result=surface.evaluate_reference(source,root,identity,observers,widths,_shared_budget=budget)
            self.assertTrue(result["certified"],result.get("failure"))
            self.assertEqual(result["identity"],surface.input_stamp(source,root,identity,observers))
            self.assertEqual(result["density_decomposition"]["negative_contrast_terms"],1)
            leaves=surface.validate_dense_source(source,root,identity,budget)
            arb=backend.arb;exact=lambda q:surface._exact(arb,Fraction(q))
            phi=gz=arb(0)
            def primitive(radius,u):
                # Integral sqrt(r²+u²) du, using log(u+sqrt) rather than asinh(u/r).
                radius,u=exact(radius),exact(u);distance=(radius*radius+u*u).sqrt()
                return (u*distance+radius*radius*(u+distance).log())/2
            for leaf in leaves:
                budget.check_time();a,b=leaf.A-3,leaf.B-3
                scale=2*arb.pi()*exact(surface.CGS_G*leaf.rho)
                phi-=scale*(primitive(leaf.H,b)-primitive(leaf.H,a)-primitive(leaf.L,b)+primitive(leaf.L,a))
                section=lambda u:(exact(leaf.H)**2+exact(u)**2).sqrt()-(exact(leaf.L)**2+exact(u)**2).sqrt()
                gz+=scale*(section(a)-section(b))
            intervals=result["rows"][0]["intervals"]
            for name,expected in (("Phi",phi),("g_r",arb(0)),("g_z",gz)):
                interval=intervals[name]
                lower,upper=exact(interval["lower_rational"]),exact(interval["upper_rational"])
                # Compare two genuine outward enclosures, not rounded midpoints.
                self.assertFalse(expected.upper() < lower.lower())
                self.assertFalse(upper.upper() < expected.lower())
                self.assertLessEqual(Fraction(interval["upper_rational"])-Fraction(interval["lower_rational"]),Fraction(widths[name]))
            self.assertEqual(budget.started,started)  # Same genuine request owns all work.
            self.assertFalse(result["science_accepted"]);self.assertFalse(result["core_binding_qualified"])
        finally:backend.ctx.prec=precision

def load_tests(loader,existing,pattern):
    """Keep original/default owners; select genuine arb witnesses only by explicit flag."""
    flag=os.environ.get("ARCH_VALIDATE_OPTIONAL_REFERENCES","0")
    if flag not in ("0","1"): raise ValueError("ARCH_VALIDATE_OPTIONAL_REFERENCES must be0 or1")
    requested=flag=="1";selected=loader.suiteClass();count=0
    optional_id=(SurfaceGeometryMemoOptionalArbTests.__module__+"."+SurfaceGeometryMemoOptionalArbTests.__name__+
                 ".test_optional_actual_arb_geometry_density_contact_and_width_guards")
    contrast_id=(SurfaceDensityContrastOptionalArbTests.__module__+"."+SurfaceDensityContrastOptionalArbTests.__name__+
                 ".test_optional_actual_arb_signed_axis_against_original_primitive")
    seen=set()
    def append(suite):
        nonlocal count
        for case in suite:
            if isinstance(case,unittest.TestSuite): append(case)
            elif case.id() in (optional_id,contrast_id):
                if case.id() in seen:raise RuntimeError("Optional reference witness discovered more than once")
                seen.add(case.id());count+=1
                if requested:selected.addTest(case)
            else:selected.addTest(case)
    append(existing)
    if count>2: raise RuntimeError("Optional reference witnesses discovered more than once")
    print("OPTIONAL_REFERENCE_SELECTION "+json.dumps(dict(owner="test_rz_ring_offaxis_reference",
          requested=requested,selected_tests=selected.countTestCases(),optional_method="test_optional_actual_arb_geometry_density_contact_and_width_guards",
          optional_methods=["test_optional_actual_arb_geometry_density_contact_and_width_guards",
                            "test_optional_actual_arb_signed_axis_against_original_primitive"],
          selected_optional_tests=count if requested else 0,
          selection_status="REQUESTED" if requested else"NOT_REQUESTED",
          execution_status="PENDING" if requested and count else"NOT_RUN",science_accepted=False),sort_keys=True),file=sys.stderr)
    return selected


if __name__ == "__main__":unittest.main()
