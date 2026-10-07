"""Actual-record adapter rejection and geometry identity, not science gates."""
import copy
import sys
import unittest
from decimal import Decimal
from unittest.mock import patch
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[3]/"validation/gravity"))
import rz_matched_native_reference as rz
import rz_ring_surface_reference as surface

class NativeAdapterTests(unittest.TestCase):
    def case(self):
        return dict(source_identity=dict(topology=9,time=0,G=6.6743e-8,
            operator_revision=1,boundary_revision=1,accuracy_revision=1,generation=1,
            inputs=[dict(uid=1,epoch=9,slot=0,version=1,storage_generation=1)]),
            origin=[0,-.5],spacing=[1,1],
            cells=[dict(level=0,index=[0,0],edges=[0,1,-.5,.5],density=3)])
    def test_explicit_density_not_rhs(self):
        c=self.case();source=rz.source_from_case(c)
        self.assertEqual(source["leaves"][0]["density"],3)
        c["source"]=[999] # changing RHS cannot replace explicit density
        self.assertEqual(source,rz.source_from_case(c))
        del c["cells"][0]["density"]
        with self.assertRaises(KeyError):rz.source_from_case(c)
    def test_identity_and_density_change_hash(self):
        c=self.case();before=rz.source_from_case(c)["sourceId"]
        for field in ("generation","time"):
            changed=copy.deepcopy(c);changed["source_identity"][field]+=1
            self.assertNotEqual(before,rz.source_from_case(changed)["sourceId"])
        c["cells"][0]["density"]=4
        self.assertNotEqual(before,rz.source_from_case(c)["sourceId"])
    def test_malformed_stamp_rejected(self):
        for mutate in ("epoch","revision","inputs","G"):
            c=self.case()
            if mutate=="epoch":c["source_identity"]["inputs"][0]["epoch"]=10
            if mutate=="revision":c["source_identity"]["operator_revision"]=0
            if mutate=="inputs":c["source_identity"]["inputs"]=[]
            if mutate=="G":c["source_identity"]["G"]=float("nan")
            with self.assertRaises(ValueError):rz.source_from_case(c)
    def test_cell_reference_uses_actual_potential_and_geometric_centers(self):
        c=self.case()
        c.update(radial_origin=0,mixed=False,potential=[-2.])
        c["cells"].append(dict(level=0,index=[1,0],edges=[1,2,-.5,.5],density=4))
        c["potential"].append(-4.)
        observations=[]
        def ref(source,observer,**kwargs):
            observations.append(observer)
            return dict(potential=Decimal(-1),contactLeaves=1,exteriorLeaves=1,axisLeaves=0)
        with patch.object(rz,"potential_reference",side_effect=ref):
            result=rz.audit_cell_case(c)
        self.assertEqual(observations,[dict(r_observer=.5,z_observer=0.),dict(r_observer=1.5,z_observer=0.)])
        self.assertEqual(result["cellObservers"],2)
        self.assertTrue(all(not row["certified"] for row in result["rows"]))
        # Radial volume weights are 1:3, not equal cell weights.
        ratio=Decimal.from_float(c["source_identity"]["G"])/rz.G
        from decimal import localcontext
        with localcontext() as ctx:
            ctx.prec=80
            ratio=Decimal.from_float(c["source_identity"]["G"])/rz.G
            expected=(((Decimal(2)-ratio)**2+3*(Decimal(4)-ratio)**2)/4).sqrt()
        self.assertEqual(Decimal(result["nativeVolumeRmsPointPotentialDelta"]),expected)
        c["potential"]=[999.,999.]
        with patch.object(rz,"potential_reference",side_effect=ref):
            changed=rz.audit_cell_case(c)
        self.assertEqual(result["sourceId"],changed["sourceId"])
        self.assertNotEqual(result["maximumPointPotentialDelta"],changed["maximumPointPotentialDelta"])

    def test_cell_reference_rejects_missing_nonfinite_potential_before_quadrature(self):
        c=self.case();c.update(radial_origin=0,mixed=False,potential=[])
        for values in ([],[float("nan")],[float("inf")]):
            c["potential"]=values
            with patch.object(rz,"potential_reference") as ref:
                with self.assertRaises(ValueError):rz.audit_cell_case(c)
                ref.assert_not_called()

    def test_actual_edges_must_match_root(self):
        c=self.case();c["cells"][0]["edges"][1]=1.0000000000000002
        with self.assertRaisesRegex(ValueError,"Rounded"):rz.source_from_case(c)

class ExactDenseUnionTests(unittest.TestCase):
    def input(self):
        identity = NativeAdapterTests().case()["source_identity"]
        # Actual rounded nonbinary endpoints, deliberately not an ideal lattice.
        root = [.1, 2.6, -.3, .9]
        leaves = [dict(id="coarse", r_lower=.1, r_upper=1.2,
                       z_lower=-.3, z_upper=.9, density=3.)]
        for n, (a, b) in enumerate([(-.3, .2), (.2, .9)]):
            leaves.append(dict(id="fine"+str(n), r_lower=1.2, r_upper=2.6,
                               z_lower=a, z_upper=b, density=3.))
        return dict(sourceId="explicit-source", leaves=leaves), root, identity

    def test_mixed_actual_bounds_exact_union_preserves_every_density_and_input(self):
        source, root, identity = self.input()
        before = copy.deepcopy((source, root, identity))
        result = rz.coalesce_exact_dense_source(source, root, identity)
        self.assertEqual((source, root, identity), before)
        self.assertEqual(result["coalesced_leaf_count"], 1)
        leaf = result["source"]["leaves"][0]
        self.assertEqual([leaf[k] for k in ("r_lower", "r_upper", "z_lower", "z_upper")], root)
        self.assertEqual(leaf["density"], 3.)
        self.assertEqual(result["exact_partition"][0]["original_leaf_ids"], ["coarse", "fine0", "fine1"])
        from fractions import Fraction as F
        self.assertEqual(F(result["exact_partition"][0]["mass_over_pi_exact"]),
                         3*(F(root[1])**2-F(root[0])**2)*(F(root[3])-F(root[2])))
        self.assertFalse(result["science_accepted"])
        self.assertFalse(result["core_binding_qualified"])

    def test_one_ulp_density_is_not_merged_or_rounded(self):
        source, root, identity = self.input()
        import math
        source["leaves"][1]["density"] = math.nextafter(3., 4.)
        result = rz.coalesce_exact_dense_source(source, root, identity)
        self.assertEqual(result["coalesced_leaf_count"], 3)
        observed = [leaf["density"] for leaf in result["source"]["leaves"]]
        self.assertIn(math.nextafter(3., 4.), observed)

    def test_dense_zero_volume_and_coverage_are_retained(self):
        source, root, identity = self.input()
        for leaf in source["leaves"]: leaf["density"] = 0.
        result = rz.coalesce_exact_dense_source(source, root, identity)
        self.assertEqual(result["coalesced_leaf_count"], 1)
        self.assertEqual(result["exact_partition"][0]["mass_over_pi_exact"], "0")
        self.assertEqual(len(result["exact_partition"][0]["original_leaf_ids"]), 3)

    def test_missing_overlap_stamp_and_density_fail_before_reference(self):
        for fault in ("hole", "overlap", "epoch", "nan", "negative"):
            source, root, identity = self.input()
            if fault == "hole": source["leaves"].pop()
            if fault == "overlap": source["leaves"][1]["z_upper"] = .4
            if fault == "epoch": identity["inputs"][0]["epoch"] += 1
            if fault == "nan": source["leaves"][0]["density"] = float("nan")
            if fault == "negative": source["leaves"][0]["density"] = -1.
            with self.subTest(fault=fault), self.assertRaises(ValueError):
                rz.coalesce_exact_dense_source(source, root, identity)

    def test_exact_union_density_or_source_identity_changes_input_stamp(self):
        source, root, identity = self.input()
        first = rz.coalesce_exact_dense_source(source, root, identity)
        identity["generation"] += 1
        second = rz.coalesce_exact_dense_source(source, root, identity)
        self.assertNotEqual(first["original_dense_input_sha256"], second["original_dense_input_sha256"])
        source["leaves"][0]["density"] = 4.
        third = rz.coalesce_exact_dense_source(source, root, identity)
        self.assertNotEqual(second["original_dense_input_sha256"], third["original_dense_input_sha256"])



class SyntheticMaterializedWireTests(unittest.TestCase):
    """SYNTHETIC transport shape only: no Runtime receipt or science evidence."""

    def wire(self):
        # Required producer-origin strings are wire fixtures, not claims that
        # this record came from Core. No scientific reference is fabricated.
        identity = dict(topology=9, time=0., G=6.6743e-8,
                        operator_revision=1, boundary_revision=1, accuracy_revision=1,
                        generation=3, inputs=[dict(uid=7, epoch=9, slot=0,
                                                  version=4, storage_generation=6)])
        leaf = dict(id="synthetic-cell-0", source_index=0, binding_block_index=0,
                    source_offset=0, r_lower=1., r_upper=2., z_lower=-.5, z_upper=.5,
                    density=3., stored_operator_volume=9.42477796076938,
                    center=[1.5, 0., 0.])
        face = dict(face_index=0, axis=0, boundary_side=1, native_bounds=True,
                    construction=0, left=0, right=-1, area=12.566370614359172,
                    gradient=.25, center=[2., 0., 0.],
                    fragment_lower=[2., -.5, 0.], fragment_upper=[2., .5, 0.],
                    fragment_width=[0., 1., 0.], boundary_datum=-1.,
                    boundary_coefficient=2., anchor_coefficient=0.,
                    value_boundary_coefficient=1., gradient_samples=[0],
                    gradient_coefficients=[-2.], value_samples=[], value_coefficients=[])
        face_observer = dict(id="synthetic-face-0", kind="face-fragment-center",
                             face_index=0, r_observer=2., z_observer=0.)
        for key in ("left", "right", "axis", "boundary_side", "native_bounds", "area",
                    "center", "fragment_lower", "fragment_upper"):
            face_observer[key] = copy.deepcopy(face[key])
        return dict(schema="arch-materialized-native-source-1",
                    test_fixture_scope="SYNTHETIC-WIRE-SHAPE-ONLY; not Runtime or science",
                    source_only_checked=True, physical_qualified=False,
                    scope="materialized_source_only", source_identity=identity,
                    source=dict(sourceId="synthetic-wire-source", leaves=[leaf]),
                    root_bounds=[1., 2., -.5, .5],
                    service_configuration=dict(origin="actual-SelfGravity-constructor-copy",
                        g_x=0., g_y=0., g_z=0., relative_tolerance=1e-11,
                        absolute_tolerance=0., max_cycles=300),
                    native_binding=dict(dimension=2, origin=[1., -.5, 0.],
                        root_upper=[2., .5, 0.], patches=[dict(uid=7, epoch=9,
                            field_memory=0, native_layout=dict(total_size=1),
                            bound_root_identity=dict(bound=True, root_lower=[1., -.5],
                                                     root_upper=[2., .5]))]),
                    field_call=dict(invoke_failed=False, field_solve_failed=False,
                                    actual_candidate_observed=True),
                    candidate_field=dict(physical_qualified=False, source_generation=3,
                        field_generation=11, cell_values=[dict(source_index=0,
                            leaf_id="synthetic-cell-0", potential=-2., acceleration=[-.25, 0., 0.])],
                        face_values=[face], face_gradients=[.25], side_acceleration=[0.]*6,
                        side_acceleration_layout="cell-major: 6*cell+2*axis+side; side0=low, side1=high",
                        face_gradient_semantics="increasing-coordinate derivative; force=-gradient"),
                    observers=[dict(id="synthetic-cell-observer-0", kind="cell-center",
                                    source_index=0, r_observer=1.5, z_observer=0.), face_observer])

    def reject_before_backend(self, record):
        """Exercise the real wire consumer; optional evaluation must be absent."""
        with patch.object(surface, "load_optional_flint", side_effect=AssertionError("backend reached")) as load, \
             patch.object(surface, "evaluate_reference", side_effect=AssertionError("reference reached")) as evaluate, \
             patch.object(surface, "integrate_leaf", side_effect=AssertionError("quadrature reached")) as integrate:
            result = rz.audit_materialized_record(record)
        self.assertFalse(result["reference_complete"])
        self.assertEqual(result["reference_status"], "UNVERIFIED")
        self.assertFalse(result["science_accepted"])
        self.assertFalse(result["physical_qualified"])
        self.assertFalse(result["core_binding_qualified"])
        self.assertEqual(result["budget"]["calls"], 0)
        load.assert_not_called(); evaluate.assert_not_called(); integrate.assert_not_called()

    def test_synthetic_wire_shape_validates_without_runtime_or_backend_grant(self):
        record = self.wire(); before = copy.deepcopy(record)
        budget = surface.Budget.start()
        with patch.object(surface, "load_optional_flint", side_effect=AssertionError("backend reached")) as load, \
             patch.object(surface, "integrate_leaf", side_effect=AssertionError("quadrature reached")) as integrate:
            leaves, cells, faces, by_cell, by_face = rz.validate_materialized_record(record, budget)
            selected = rz.select_materialized_observers(cells, faces, by_cell, by_face)
        self.assertEqual(record, before)
        self.assertEqual((len(leaves), len(cells), len(faces)), (1, 1, 1))
        self.assertEqual([(kind, index, observer["id"]) for kind, index, observer in selected],
                         [("cell", 0, "synthetic-cell-observer-0"), ("face", 0, "synthetic-face-0")])
        self.assertEqual(record["candidate_field"]["source_generation"], 3)
        self.assertEqual(record["candidate_field"]["field_generation"], 11)
        self.assertNotEqual(record["candidate_field"]["source_generation"],
                            record["candidate_field"]["field_generation"])
        self.assertEqual(record["source"]["leaves"][0]["source_offset"], 0)
        self.assertFalse(record["physical_qualified"])
        self.assertIn("SYNTHETIC", record["test_fixture_scope"])
        self.assertEqual(budget.calls, 0)
        load.assert_not_called(); integrate.assert_not_called()

    def test_source_and_field_generations_are_distinct_but_source_must_match(self):
        for field, value in (("source_generation", 4), ("source_generation", True),
                             ("field_generation", 0), ("field_generation", -1),
                             ("field_generation", True)):
            record = self.wire(); record["candidate_field"][field] = value
            with self.subTest(field=field, value=value): self.reject_before_backend(record)

    def test_actual_source_storage_offset_and_patch_mapping_reject_before_backend(self):
        for field, value in (("source_offset", -1), ("source_offset", 1),
                             ("source_offset", True), ("binding_block_index", -1),
                             ("binding_block_index", 1)):
            record = self.wire(); record["source"]["leaves"][0][field] = value
            with self.subTest(field=field, value=value): self.reject_before_backend(record)

    def test_observer_orientation_and_point_identity_reject_before_backend(self):
        for field, value in (("axis", 1), ("boundary_side", 0), ("left", -1),
                             ("right", 0), ("r_observer", 1.5), ("z_observer", .25)):
            record = self.wire(); record["observers"][1][field] = value
            with self.subTest(field=field, value=value): self.reject_before_backend(record)
        record = self.wire(); record["candidate_field"]["face_values"][0]["boundary_side"] = 2
        self.reject_before_backend(record)  # axial side cannot label a radial face

    def test_original_field_array_extents_reject_before_backend(self):
        for fault in ("side", "gradient", "cell_acceleration", "cell_center", "face_center",
                      "fragment_lower", "gradient_coefficients", "value_coefficients",
                      "cell_count", "face_count", "observers"):
            record = self.wire(); field = record["candidate_field"]
            if fault == "side": field["side_acceleration"] = [0.]*5
            if fault == "gradient": field["face_gradients"] = []
            if fault == "cell_acceleration": field["cell_values"][0]["acceleration"] = [0.]*2
            if fault == "cell_center": record["source"]["leaves"][0]["center"] = [1.5, 0.]
            if fault == "face_center": field["face_values"][0]["center"] = [2., 0.]
            if fault == "fragment_lower": field["face_values"][0]["fragment_lower"] = [2., -.5]
            if fault == "gradient_coefficients": field["face_values"][0]["gradient_coefficients"] = []
            if fault == "value_coefficients": field["face_values"][0]["value_coefficients"] = [1.]
            if fault == "cell_count": field["cell_values"] = []
            if fault == "face_count": field["face_values"] = []
            if fault == "observers": record["observers"] = record["observers"][:1]
            with self.subTest(fault=fault): self.reject_before_backend(record)

    def test_expired_budget_prevents_dense_source_preprocessing_without_reset(self):
        record = self.wire(); budget = surface.Budget(started=1., calls=17)
        with patch.object(surface.time, "monotonic", return_value=92.), \
             patch.object(surface.Budget, "start", side_effect=AssertionError("budget reset")) as start, \
             patch.object(surface.Leaf, "from_record", side_effect=AssertionError("dense leaf reached")) as leaf, \
             patch.object(surface, "load_optional_flint", side_effect=AssertionError("backend reached")) as load, \
             patch.object(surface, "integrate_leaf", side_effect=AssertionError("quadrature reached")) as integrate:
            with self.assertRaises(surface.WorkLimit):
                rz.coalesce_exact_dense_source(record["source"], record["root_bounds"],
                                               record["source_identity"], budget)
        self.assertEqual((budget.started, budget.calls), (1., 17))
        start.assert_not_called(); leaf.assert_not_called(); load.assert_not_called(); integrate.assert_not_called()

    def test_reference_preserves_expired_shared_budget_before_dense_validation(self):
        record = self.wire(); budget = surface.Budget(started=1., calls=17)
        widths = {key: str(surface.CGS_G / 10**12) for key in ("Phi", "g_r", "g_z")}
        with patch.object(surface.time, "monotonic", return_value=92.), \
             patch.object(surface.Budget, "start", side_effect=AssertionError("budget reset")) as start, \
             patch.object(surface, "validate_dense_source", side_effect=AssertionError("dense validation reached")) as dense, \
             patch.object(surface, "load_optional_flint", side_effect=AssertionError("backend reached")) as load, \
             patch.object(surface, "integrate_leaf", side_effect=AssertionError("quadrature reached")) as integrate:
            result = surface.evaluate_reference(record["source"], record["root_bounds"],
                record["source_identity"], record["observers"], widths, _shared_budget=budget)
        self.assertEqual(result["status"], "WorkLimit")
        self.assertFalse(result["certified"])
        self.assertFalse(result["science_accepted"])
        self.assertFalse(result["core_binding_qualified"])
        self.assertEqual(result["rows"], [])
        self.assertEqual((budget.started, budget.calls), (1., 17))
        self.assertEqual(result["budget"]["calls"], 17)
        self.assertEqual(result["budget"]["max_calls"], 100000)
        self.assertEqual(result["budget"]["timeout_seconds"], 90)
        start.assert_not_called(); dense.assert_not_called(); load.assert_not_called(); integrate.assert_not_called()

    def test_consumer_starts_once_and_times_out_before_coalescing_or_backend(self):
        record = self.wire(); budget = surface.Budget(started=1., calls=17)
        with patch.object(surface.time, "monotonic", return_value=92.), \
             patch.object(surface.Budget, "start", return_value=budget) as start, \
             patch.object(rz, "coalesce_exact_dense_source", side_effect=AssertionError("coalescing reached")) as dense, \
             patch.object(surface, "load_optional_flint", side_effect=AssertionError("backend reached")) as load, \
             patch.object(surface, "evaluate_reference", side_effect=AssertionError("reference reached")) as evaluate:
            result = rz.audit_materialized_record(record)
        start.assert_called_once_with()
        dense.assert_not_called(); load.assert_not_called(); evaluate.assert_not_called()
        self.assertEqual(result["reference_status"], "WorkLimit")
        self.assertFalse(result["reference_complete"])
        self.assertFalse(result["science_accepted"])
        self.assertFalse(result["physical_qualified"])
        self.assertFalse(result["core_binding_qualified"])
        self.assertEqual((budget.started, budget.calls), (1., 17))
        self.assertEqual(result["budget"]["calls"], 17)
        self.assertEqual(result["budget"]["max_calls"], 100000)
        self.assertEqual(result["budget"]["timeout_seconds"], 90)



class FullDomainEngineeringTests(unittest.TestCase):
    """SYNTHETIC wire/coverage/resource tests; never a Core or science certificate."""

    def symmetric_wire(self):
        record = SyntheticMaterializedWireTests().wire()
        record["source"]["sourceId"] = "synthetic-full-source"
        original = record["source"]["leaves"][0]
        face0 = record["candidate_field"]["face_values"][0]
        leaves, cells, faces, observers = [], [], [], []
        for i, (lower, upper, center) in enumerate(((-.5, 0., -.25), (0., .5, .25))):
            leaf = copy.deepcopy(original)
            leaf.update(id="synthetic-cell-"+str(i), source_index=i, source_offset=i,
                        z_lower=lower, z_upper=upper, center=[1.5, center, 0.],
                        stored_operator_volume=4.71238898038469)
            leaves.append(leaf)
            cells.append(dict(source_index=i, leaf_id=leaf["id"], potential=-2., acceleration=[-.25, 0., 0.]))
            face = copy.deepcopy(face0)
            face.update(face_index=i, left=i, area=6.283185307179586,
                        center=[2., center, 0.], fragment_lower=[2., lower, 0.],
                        fragment_upper=[2., upper, 0.], fragment_width=[0., .5, 0.],
                        gradient_samples=[i])
            faces.append(face)
            observer = dict(id="synthetic-face-"+str(i), kind="face-fragment-center",
                            face_index=i, r_observer=2., z_observer=center)
            for key in ("left", "right", "axis", "boundary_side", "native_bounds", "area",
                        "center", "fragment_lower", "fragment_upper"):
                observer[key] = copy.deepcopy(face[key])
            observers += [dict(id="synthetic-cell-observer-"+str(i), kind="cell-center",
                               source_index=i, r_observer=1.5, z_observer=center), observer]
        record["source"]["leaves"] = leaves
        record["native_binding"]["patches"][0]["native_layout"]["total_size"] = 2
        record["candidate_field"].update(cell_values=cells, face_values=faces,
            face_gradients=[.25]*2, side_acceleration=[0.]*12)
        record["observers"] = observers
        return record

    def schedule(self, record):
        budget = surface.Budget.full_domain_diagnostic()
        leaves, cells, faces, by_cell, by_face = rz.validate_materialized_record(record, budget)
        rz.coalesce_exact_dense_source(record["source"], record["root_bounds"],
                                       record["source_identity"], budget)
        return rz.materialized_full_schedule(record, cells, faces, by_cell, by_face, budget)

    def test_complete_synthetic_targets_are_retained_under_exact_reflection(self):
        record = self.symmetric_wire(); before = copy.deepcopy(record)
        schedule = self.schedule(record)
        self.assertTrue(schedule["exact_z_reflection"])
        self.assertEqual(schedule["original_target_count"], 4)
        self.assertEqual(schedule["unique_site_count"], 2)
        self.assertEqual({row["observer_id"] for row in schedule["targets"]},
                         {row["id"] for row in record["observers"]})
        self.assertEqual([row["g_z_sign"] for row in schedule["targets"]], [-1, 1, -1, 1])
        self.assertEqual(record, before)
        self.assertIn("SYNTHETIC", record["test_fixture_scope"])

    def test_one_ulp_density_breaks_global_reflection_without_dropping_targets(self):
        import math
        record = self.symmetric_wire()
        record["source"]["leaves"][1]["density"] = math.nextafter(3., 4.)
        schedule = self.schedule(record)
        self.assertFalse(schedule["exact_z_reflection"])
        self.assertEqual(schedule["original_target_count"], 4)
        self.assertEqual(schedule["unique_site_count"], 4)
        self.assertTrue(all(row["g_z_sign"] == 1 for row in schedule["targets"]))

    def test_source_identity_and_exact_density_bind_schedule_site_ids(self):
        record = self.symmetric_wire(); first = self.schedule(record)
        changed = copy.deepcopy(record)
        changed["source_identity"]["generation"] += 1
        changed["candidate_field"]["source_generation"] += 1
        second = self.schedule(changed)
        self.assertNotEqual(first["source_input_sha256"], second["source_input_sha256"])
        self.assertNotEqual(first["sites"], second["sites"])
        self.assertEqual([(r["kind"], r["actual_index"]) for r in first["targets"]],
                         [(r["kind"], r["actual_index"]) for r in second["targets"]])

    def test_outward_reflection_uses_endpoints_and_all_components(self):
        intervals = dict(Phi=dict(lower_rational="-5/3", upper_rational="-4/3"),
                         g_r=dict(lower_rational="1/7", upper_rational="2/7"),
                         g_z=dict(lower_rational="-3/5", upper_rational="2/5"))
        reflected = rz.reflected_reference_intervals(intervals, -1)
        self.assertEqual(reflected["g_z"], dict(lower_rational="-2/5", upper_rational="3/5", width_rational="1"))
        self.assertEqual(reflected["Phi"]["lower_rational"], "-5/3")
        self.assertEqual(reflected["g_r"]["upper_rational"], "2/7")
        back = rz.reflected_reference_intervals(reflected, -1)
        self.assertEqual(back, rz.reflected_reference_intervals(intervals, 1))
        with self.assertRaises(ValueError): rz.reflected_reference_intervals(intervals, 0)

    def test_full_budget_is_distinct_and_subset_resource_profile_unchanged(self):
        normal = surface.Budget.start(); full = surface.Budget.full_domain_diagnostic()
        self.assertEqual((normal.max_calls, normal.timeout_seconds), (100000, 90.))
        self.assertEqual((full.max_calls, full.timeout_seconds), (1800000, 240.))
        self.assertEqual((normal.calls, full.calls), (0, 0))
        self.assertEqual(normal.record()["resource_profile"], "bounded-request-1")
        self.assertEqual(full.record()["resource_profile"], "full-domain-diagnostic-1")
        # Only counter boundaries are exercised; no kernel callback is run.
        full.calls = 100000; full.take(); self.assertEqual(full.calls, 100001)
        full.calls = 1800000
        with self.assertRaises(surface.WorkLimit): full.take()
        self.assertEqual(full.calls, 1800000)
        normal.calls = 100000
        with self.assertRaises(surface.WorkLimit): normal.take()
        self.assertEqual(normal.calls, 100000)

    def test_full_expired_budget_rejects_before_schema_or_backend_without_reset(self):
        budget = surface.Budget(started=1., calls=17, _full_domain_diagnostic=True)
        with patch.object(surface.time, "monotonic", return_value=242.), \
             patch.object(surface.Budget, "full_domain_diagnostic", return_value=budget) as factory, \
             patch.object(surface.Budget, "start", side_effect=AssertionError("budget reset")) as start, \
             patch.object(rz, "validate_materialized_record", side_effect=AssertionError("schema reached")) as schema, \
             patch.object(surface, "load_optional_flint", side_effect=AssertionError("backend reached")) as load:
            result = rz.audit_materialized_full_record(self.symmetric_wire())
        factory.assert_called_once_with(); start.assert_not_called(); schema.assert_not_called(); load.assert_not_called()
        self.assertEqual(result["reference_status"], "WorkLimit")
        self.assertFalse(result["reference_complete"]); self.assertFalse(result["coverage_complete"])
        self.assertEqual((budget.started, budget.calls), (1., 17))
        self.assertEqual(result["budget"]["max_calls"], 1800000)
        self.assertFalse(result["science_accepted"])

    def test_invalid_full_wire_fails_before_scheduling_optional_backend_or_integration(self):
        record = self.symmetric_wire(); record["candidate_field"]["face_gradients"].pop()
        with patch.object(rz, "materialized_full_schedule", side_effect=AssertionError("schedule reached")) as schedule, \
             patch.object(surface, "load_optional_flint", side_effect=AssertionError("backend reached")) as load, \
             patch.object(surface, "integrate_leaf", side_effect=AssertionError("quadrature reached")) as integrate:
            result = rz.audit_materialized_full_record(record)
        schedule.assert_not_called(); load.assert_not_called(); integrate.assert_not_called()
        self.assertEqual(result["reference_status"], "UNVERIFIED")
        self.assertFalse(result["coverage_complete"]); self.assertEqual(result["budget"]["calls"], 0)

    def test_same_full_budget_reaches_reference_and_failure_retains_all_targets(self):
        from types import SimpleNamespace
        record = self.symmetric_wire(); budget = surface.Budget.full_domain_diagnostic()
        budget.calls = 17
        with patch.object(surface.Budget, "start", side_effect=AssertionError("budget reset")) as start, \
             patch.object(surface.Budget, "full_domain_diagnostic", side_effect=AssertionError("budget reset")) as factory, \
             patch.object(surface, "load_optional_flint", return_value=SimpleNamespace(ctx=SimpleNamespace(dps=70))), \
             patch.object(surface, "evaluate_reference", side_effect=surface.WorkLimit("synthetic stop before quadrature")) as evaluate, \
             patch.object(surface, "integrate_leaf", side_effect=AssertionError("quadrature reached")) as integrate:
            result = rz.audit_materialized_full_record(record, _shared_budget=budget)
        start.assert_not_called(); factory.assert_not_called(); integrate.assert_not_called()
        self.assertIs(evaluate.call_args.kwargs["_shared_budget"], budget)
        self.assertEqual(result["reference_status"], "WorkLimit")
        self.assertEqual(len(result["targets"]), 4)
        self.assertEqual({row["observer_id"] for row in result["targets"]}, {row["id"] for row in record["observers"]})
        self.assertTrue(all(row["reference_status"] == "UNVERIFIED_NOT_EVALUATED" for row in result["targets"]))
        self.assertEqual(result["budget"]["calls"], 17)
        self.assertFalse(result["coverage_complete"])
        summary = rz.full_materialized_summary(result)
        self.assertEqual(summary["target_count"], 4); self.assertEqual(summary["verified_target_count"], 0)
        self.assertFalse(summary["science_accepted"])

    def test_full_request_rejects_subset_budget_instead_of_silently_resetting(self):
        budget = surface.Budget.start()
        with patch.object(surface.Budget, "full_domain_diagnostic", side_effect=AssertionError("reset")) as factory:
            with self.assertRaisesRegex(ValueError, "frozen resource profile"):
                rz.audit_materialized_full_record(self.symmetric_wire(), _shared_budget=budget)
        factory.assert_not_called(); self.assertEqual(budget.calls, 0)

    def test_exact_integer_density_collision_does_not_authorize_reflection(self):
        record = self.symmetric_wire()
        record["source"]["leaves"][0]["density"] = 2**53
        record["source"]["leaves"][1]["density"] = 2**53+1
        self.assertEqual(float(2**53), float(2**53+1))
        schedule = self.schedule(record)
        self.assertFalse(schedule["exact_z_reflection"])
        self.assertEqual(schedule["original_target_count"], 4)
        self.assertEqual(schedule["unique_site_count"], 4)
        self.assertTrue(all(row["g_z_sign"] == 1 for row in schedule["targets"]))
        # The existing actual wire accepts only int/float, not rational strings.
        record["source"]["leaves"][1]["density"] = "3"
        with self.assertRaisesRegex(ValueError, "Invalid actual numeric value"):
            self.schedule(record)

    def test_full_cli_freezes_precision_before_audit_with_one_unchanged_budget(self):
        import hashlib
        import json
        import tempfile
        from types import SimpleNamespace
        backend = SimpleNamespace(ctx=SimpleNamespace(dps=15))
        budget = surface.Budget.full_domain_diagnostic(); budget.calls = 17
        def auditor(record, dependency_directory, supplied_budget):
            self.assertEqual(backend.ctx.dps, 70)
            self.assertIs(supplied_budget, budget)
            self.assertEqual((budget.max_calls, budget.timeout_seconds, budget.calls),
                             (1800000, 240., 17))
            # Explicitly unverified: this synthetic CLI test runs no reference.
            return dict(profile="actual-materialized-full-domain-diagnostic-1",
                status="FULL_DOMAIN_UNVERIFIED_DIAGNOSTIC", reference_status="UNVERIFIED",
                science_accepted=False, physical_qualified=False, core_binding_qualified=False,
                reference_complete=False, coverage_complete=False, targets=[], sites=[],
                target_widths_exact={key: str(surface.CGS_G/10**12)
                                    for key in ("Phi", "g_r", "g_z")}, budget=budget.record())
        with tempfile.TemporaryDirectory(prefix="arch-full-cli-synthetic-") as temporary:
            directory = Path(temporary); source = directory/"synthetic-input.json"
            output = directory/"complete.json"; summary = directory/"summary.json"
            raw = json.dumps(self.symmetric_wire()).encode(); source.write_bytes(raw)
            argv = ["rz_matched_native_reference.py", "--materialized-record", str(source),
                    "--full-domain", "--output", str(output), "--summary-output", str(summary)]
            with patch.object(sys, "argv", argv), \
                 patch.object(surface.Budget, "full_domain_diagnostic", return_value=budget) as factory, \
                 patch.object(surface.Budget, "start", side_effect=AssertionError("budget reset")) as start, \
                 patch.object(surface, "load_optional_flint", return_value=backend) as load, \
                 patch.object(rz, "audit_materialized_full_record", side_effect=auditor) as audit, \
                 patch.object(surface, "integrate_leaf", side_effect=AssertionError("quadrature reached")) as integrate, \
                 patch("builtins.print"):
                rz.main()
            factory.assert_called_once_with(); start.assert_not_called()
            load.assert_called_once_with(None); audit.assert_called_once(); integrate.assert_not_called()
            self.assertIs(audit.call_args.args[2], budget)
            complete = json.loads(output.read_text()); aggregate = json.loads(summary.read_text())
            self.assertEqual(complete["materializedRecordSha256"], hashlib.sha256(raw).hexdigest())
            self.assertEqual(aggregate["completeLocalResultSha256"], hashlib.sha256(output.read_bytes()).hexdigest())
            self.assertFalse(complete["science_accepted"]); self.assertFalse(aggregate["coverage_complete"])
        self.assertEqual(backend.ctx.dps, 70); self.assertEqual(budget.calls, 17)

    def test_full_cli_backend_or_startup_timeout_writes_explicit_unverified(self):
        import json
        import tempfile
        for expired in (False, True):
            with self.subTest(expired=expired), tempfile.TemporaryDirectory(prefix="arch-full-cli-failure-") as temporary:
                directory = Path(temporary); source = directory/"synthetic-input.json"
                output = directory/"complete.json"; summary = directory/"summary.json"
                source.write_text(json.dumps(self.symmetric_wire()))
                budget = surface.Budget(started=1., calls=17, _full_domain_diagnostic=True)
                argv = ["rz_matched_native_reference.py", "--materialized-record", str(source),
                        "--full-domain", "--output", str(output), "--summary-output", str(summary)]
                with patch.object(sys, "argv", argv), \
                     patch.object(surface.time, "monotonic", return_value=242. if expired else 2.), \
                     patch.object(surface.Budget, "full_domain_diagnostic", return_value=budget) as factory, \
                     patch.object(surface.Budget, "start", side_effect=AssertionError("budget reset")) as start, \
                     patch.object(surface, "load_optional_flint", side_effect=surface.ReferenceFailure("synthetic unavailable backend")) as load, \
                     patch.object(rz, "audit_materialized_full_record", side_effect=AssertionError("audit reached")) as audit, \
                     patch.object(surface, "integrate_leaf", side_effect=AssertionError("quadrature reached")) as integrate, \
                     patch("builtins.print"):
                    rz.main()
                factory.assert_called_once_with(); start.assert_not_called(); audit.assert_not_called(); integrate.assert_not_called()
                self.assertEqual(load.call_count, 0 if expired else 1)
                complete = json.loads(output.read_text()); aggregate = json.loads(summary.read_text())
                self.assertEqual(complete["reference_status"], "WorkLimit" if expired else "UNVERIFIED")
                self.assertEqual(complete["failure_phase"], "optional-backend-precision-freeze")
                self.assertEqual(complete["failure_type"], "WorkLimit" if expired else "ReferenceFailure")
                self.assertEqual(complete["budget"]["calls"], 17)
                self.assertFalse(complete["input_read"]); self.assertNotIn("materializedRecordSha256", complete)
                self.assertFalse(complete["coverage_complete"]); self.assertFalse(complete["science_accepted"])
                self.assertFalse(aggregate["reference_complete"])
                self.assertEqual(aggregate["failure_phase"], complete["failure_phase"])



class CompleteReferenceReuseEngineeringTests(unittest.TestCase):
    """SYNTHETIC interval-transport defenses; NOT a mathematical/Core certificate."""

    def wire(self, axis=False):
        import math
        record = SyntheticMaterializedWireTests().wire()
        lower, upper = (0., 1.) if axis else (1., 2.)
        leaf = record["source"]["leaves"][0]
        leaf.update(r_lower=lower, r_upper=upper, center=[(lower+upper)/2, 0., 0.],
                    stored_operator_volume=math.pi*(upper*upper-lower*lower))
        record["root_bounds"] = [lower, upper, -.5, .5]
        record["native_binding"].update(origin=[lower,-.5,0.], root_upper=[upper,.5,0.])
        bound = record["native_binding"]["patches"][0]["bound_root_identity"]
        bound.update(root_lower=[lower,-.5], root_upper=[upper,.5])
        cell_observer = record["observers"][0]
        cell_observer.update(r_observer=(lower+upper)/2)
        template = record["candidate_field"]["face_values"][0]
        faces, observers = [], [cell_observer]
        for i, (direction, side) in enumerate(((0,0),(0,1),(1,0),(1,1))):
            face = copy.deepcopy(template)
            center = [lower if side==0 else upper,0.,0.] if direction==0 else [(lower+upper)/2,-.5 if side==0 else .5,0.]
            lo = [center[0],-.5,0.] if direction==0 else [lower,center[1],0.]
            hi = [center[0],.5,0.] if direction==0 else [upper,center[1],0.]
            area = 2*math.pi*center[0] if direction==0 else math.pi*(upper*upper-lower*lower)
            face.update(face_index=i, axis=direction, boundary_side=2*direction+side,
                        left=0 if side else -1, right=-1 if side else 0, center=center,
                        area=area, fragment_lower=lo, fragment_upper=hi,
                        fragment_width=[hi[0]-lo[0],hi[1]-lo[1],0.],
                        gradient=0. if axis and i==0 else .25)
            faces.append(face)
            observer=dict(id="synthetic-complete-face-"+str(i),kind="face-fragment-center",face_index=i,
                          r_observer=center[0],z_observer=center[1])
            for key in ("left","right","axis","boundary_side","native_bounds","area","center","fragment_lower","fragment_upper"):
                observer[key]=copy.deepcopy(face[key])
            observers.append(observer)
        record["candidate_field"].update(face_values=faces,face_gradients=[f["gradient"] for f in faces])
        record["observers"]=observers
        return record

    def reference(self, record):
        # Fabricated tiny rational intervals ONLY test mapping/provenance code.
        # This method never calls an integral or supplies scientific evidence.
        from fractions import Fraction as F
        budget=surface.Budget.full_domain_diagnostic()
        values=rz.validate_materialized_record(record,budget)
        combined=rz.coalesce_exact_dense_source(record["source"],record["root_bounds"],record["source_identity"],budget)
        schedule=rz.materialized_full_schedule(record,*values[1:],budget)
        width=surface.CGS_G/F(10**12)
        def interval(lower):
            return dict(lower_rational=str(lower),upper_rational=str(lower+width/2),width_rational=str(width/2))
        sites=[]
        for site in schedule["sites"]:
            intervals=dict(Phi=interval(F(-4)),g_r=interval(F(-1)),g_z=interval(F(-2)))
            if F(site["r_observer"])==0:
                intervals["g_r"]=dict(lower_rational="0",upper_rational="0",width_rational="0")
            sites.append(dict(observer_id=site["id"],observer=dict(R_exact=site["r_observer"],Z_exact=site["z_observer"]),
                              intervals=intervals,math_certificate_meets_target=True))
        by_id={s["observer_id"]:s for s in sites}
        targets=[]
        for target in schedule["targets"]:
            targets.append(dict(target,science_accepted=False,math_interval_meets_original_width=True,
                reference_status="MathematicalIntervalsCertified",
                intervals=rz.reflected_reference_intervals(by_id[target["site_id"]]["intervals"],target["g_z_sign"]),
                comparisons={"MUST_NOT_REUSE_OLD_FIELD":dict(actual_exact="123")}))
        reference_input=dict(source=combined["source"],root_bounds=record["root_bounds"],
                             source_identity=record["source_identity"],observers=schedule["sites"])
        return dict(profile="actual-materialized-full-domain-diagnostic-1",
            resource_limits=dict(max_calls=1800000,wall_seconds=240.),
            status="FULL_DOMAIN_MATHEMATICAL_REFERENCE_DIAGNOSTIC_ONLY",science_accepted=False,
            physical_qualified=False,core_binding_qualified=False,reference_complete=True,coverage_complete=True,
            reference_status="MathematicalIntervalsCertified",failure=None,
            materializedRecordSha256=self.raw_sha(record),actual_record_canonical_sha256=rz._reuse_canonical_sha(record),
            original_source_input_sha256=combined["original_dense_input_sha256"],source_identity=copy.deepcopy(record["source_identity"]),
            field_identity=dict(source_generation=record["candidate_field"]["source_generation"],field_generation=record["candidate_field"]["field_generation"]),
            producer_identity=dict(source_only_checked=record["source_only_checked"],field_call=copy.deepcopy(record["field_call"]),service_configuration=copy.deepcopy(record["service_configuration"])),
            exact_union=combined,target_widths_exact={k:str(width) for k in ("Phi","g_r","g_z")},
            target_mapping=schedule["targets"],target_count=len(targets),unique_site_count=len(sites),
            exact_z_reflection=schedule["exact_z_reflection"],symmetry_midpoint_exact=schedule["symmetry_midpoint_exact"],
            original_source_id=record["source"]["sourceId"],backend=dict(ctx_dps=70,python_flint_version="SYNTHETIC-NO-BACKEND"),
            reference_identity=dict(input=reference_input,sha256=rz._reuse_canonical_sha(reference_input),core_binding_qualified=False),
            budget=dict(calls=31,max_calls=1800000,wall_seconds=1.25,timeout_seconds=240.,resource_profile="full-domain-diagnostic-1"),
            sites=sites,targets=targets)

    def raw_sha(self, record):
        import hashlib,json
        return hashlib.sha256(json.dumps(record,allow_nan=False).encode()).hexdigest()

    def reuse(self, old, new=None, reference=None, expected_pin=None, **kwargs):
        new=copy.deepcopy(old) if new is None else new
        reference=self.reference(old) if reference is None else reference
        with patch.object(surface,"load_optional_flint",side_effect=AssertionError("backend reached")) as backend, \
             patch.object(surface,"evaluate_reference",side_effect=AssertionError("integral reached")) as evaluate, \
             patch.object(surface,"integrate_leaf",side_effect=AssertionError("kernel reached")) as integrate:
            result=rz.reuse_materialized_full_reference(new,old,reference,new_raw_sha256=self.raw_sha(new),
                old_raw_sha256=self.raw_sha(old),reference_raw_sha256=self.raw_sha(reference),
                expected_reference_sha256=self.raw_sha(reference) if expected_pin is None else expected_pin,**kwargs)
        backend.assert_not_called();evaluate.assert_not_called();integrate.assert_not_called()
        return result

    def test_new_values_stencil_and_generations_recomputed_without_old_comparisons(self):
        old=self.wire();new=copy.deepcopy(old)
        new["candidate_field"]["cell_values"][0]["potential"]=-3.
        new["candidate_field"]["cell_values"][0]["acceleration"]=[-.7,-.1,0.]
        new["candidate_field"]["side_acceleration"][0]=-.6
        new["source_identity"]["generation"]+=1
        new["candidate_field"].update(source_generation=4,field_generation=19)
        new["candidate_field"]["face_values"][0].update(boundary_datum=-3.,gradient=.75,
            value_samples=[0],value_coefficients=[.5],value_boundary_coefficient=.5)
        new["candidate_field"]["face_gradients"][0]=.75
        before=copy.deepcopy((old,new));result=self.reuse(old,new)
        self.assertTrue(result["coverage_complete"],result.get("failure"))
        self.assertEqual((old,new),before)
        self.assertEqual(result["field_identity"],dict(source_generation=4,field_generation=19))
        self.assertEqual(result["reference_field_identity"],dict(source_generation=3,field_generation=11))
        self.assertEqual(result["targets"][0]["comparisons"]["point_cell_potential"]["actual_exact"],"-3")
        self.assertEqual(result["targets"][1]["comparisons"]["force_minus_original_gradient"]["actual_exact"],"-3/4")
        self.assertEqual(result["targets"][1]["comparisons"]["derived_original_value_row_vs_point_potential"]["actual_exact"],"-3")
        self.assertEqual(result["reference_history"]["calls"],31)
        self.assertEqual(result["reference_history"]["wall_seconds"],1.25)
        self.assertEqual(result["mapping_budget"]["calls"],0)
        self.assertFalse(result["science_accepted"]);self.assertFalse(result["core_binding_qualified"])
        self.assertNotIn("MUST_NOT_REUSE_OLD_FIELD",repr(result["targets"]))
        self.assertEqual((len(result["side_acceleration_rows"]),len(result["cell_acceleration_rows"])),(6,3))

    def test_negative_interval_and_axis_zero_keep_real_FP64_errors(self):
        from fractions import Fraction as F
        old=self.wire(axis=True);old["candidate_field"]["side_acceleration"][1]=-1.
        result=self.reuse(old);self.assertTrue(result["coverage_complete"],result.get("failure"))
        sides=result["side_acceleration_rows"]
        self.assertEqual(sides[0]["interval"],dict(lower_rational="0",upper_rational="0",width_rational="0"))
        self.assertEqual(F(sides[1]["interval"]["lower_rational"]),-1)
        self.assertGreater(F(sides[1]["error"]["absolute_error_upper_exact"]),0)
        self.assertIn("actual_binary64_hex",sides[1]["error"])
        self.assertEqual(result["cell_acceleration_rows"][2]["interval"]["width_rational"],"0")
        # Both negative endpoints are retained, never abs() normalized.
        self.assertLess(F(result["cell_acceleration_rows"][0]["interval"]["upper_rational"]),0)

    def test_exact_source_G_geometry_observer_one_ulp_rejections(self):
        import math
        for fault in ("rho","G","root","area","fragment","observer","binding"):
            old=self.wire();new=copy.deepcopy(old)
            if fault=="rho":new["source"]["leaves"][0]["density"]=math.nextafter(3.,4.)
            if fault=="G":new["source_identity"]["G"]=math.nextafter(new["source_identity"]["G"],1.)
            if fault=="root":new["root_bounds"][1]=math.nextafter(2.,3.)
            if fault=="area":
                new["candidate_field"]["face_values"][0]["area"]=math.nextafter(new["candidate_field"]["face_values"][0]["area"],100.)
                new["observers"][1]["area"]=new["candidate_field"]["face_values"][0]["area"]
            if fault=="fragment":
                new["candidate_field"]["face_values"][0]["fragment_lower"][1]=math.nextafter(-.5,0.)
                new["observers"][1]["fragment_lower"][1]=new["candidate_field"]["face_values"][0]["fragment_lower"][1]
            if fault=="observer":new["observers"][0]["r_observer"]=math.nextafter(1.5,2.)
            if fault=="binding":new["native_binding"]["patches"][0]["native_layout"]["total_size"]=2
            with self.subTest(fault=fault):
                result=self.reuse(old,new);self.assertFalse(result["coverage_complete"]);self.assertEqual(result["reference_status"],"UNVERIFIED")

    def test_complete_original_provenance_certificate_cannot_be_replaced_by_flags(self):
        for fault in ("missing_target","missing_site","width","status","canonical","raw","source_sha","interval_transport","site_coordinate","ref_input"):
            old=self.wire();ref=self.reference(old)
            if fault=="missing_target":ref["targets"].pop()
            if fault=="missing_site":ref["sites"].pop()
            if fault=="width":ref["sites"][0]["intervals"]["Phi"]["width_rational"]="0"
            if fault=="status":ref["targets"][0]["reference_status"]="UNVERIFIED"
            if fault=="canonical":ref["actual_record_canonical_sha256"]="0"*64
            if fault=="raw":ref["materializedRecordSha256"]="0"*64
            if fault=="source_sha":ref["original_source_input_sha256"]="0"*64
            if fault=="interval_transport":ref["targets"][0]["intervals"]["Phi"]["lower_rational"]="-100"
            if fault=="site_coordinate":ref["sites"][0]["observer"]["R_exact"]="7/4"
            if fault=="ref_input":ref["reference_identity"]["input"]["observers"][0]["r_observer"]="7/4"
            with self.subTest(fault=fault):
                result=self.reuse(old,reference=ref);self.assertFalse(result["coverage_complete"]);self.assertEqual(result["reference_status"],"UNVERIFIED")

    def test_multifragment_negative_force_keeps_FP64_normalization_error(self):
        from fractions import Fraction as F
        record=self.wire();field=record["candidate_field"]
        # SYNTHETIC stored-area row only; no authentication/geometric science
        # grant. Exact FP64 area values are independent transport inputs.
        second=copy.deepcopy(field["face_values"][1]);second.update(face_index=4,area=.2)
        field["face_values"][1]["area"]=.1
        field["face_values"].append(second)
        field["side_acceleration"][1]=-(.1/(.1+.2)*2.+.2/(.1+.2)*4.)
        reference={i:dict(lower_rational="-1",upper_rational="-1",width_rational="0") for i in range(5)}
        reference[1]=dict(lower_rational="-2",upper_rational="-2",width_rational="0")
        reference[4]=dict(lower_rational="-4",upper_rational="-4",width_rational="0")
        before=copy.deepcopy(record)
        sides,_=rz._reuse_acceleration_rows(record,reference,surface.Budget.full_domain_diagnostic())
        expected=-(F(.1)*2+F(.2)*4)/(F(.1)+F(.2))
        self.assertEqual(F(sides[1]["interval"]["lower_rational"]),expected)
        self.assertEqual(F(sides[1]["interval"]["upper_rational"]),expected)
        self.assertGreater(F(sides[1]["error"]["absolute_error_upper_exact"]),0)
        self.assertEqual(record,before)

    def test_reuse_CLI_requires_full_new_record_and_original_record(self):
        import tempfile
        for flags in (("--reuse-full-reference","ref.json"),
                      ("--reuse-full-reference","ref.json","--reference-materialized-record","old.json"),
                      ("--reference-materialized-record","old.json"),
                      ("--reuse-full-reference-sha256","a"*64)):
            with tempfile.TemporaryDirectory() as directory, self.subTest(flags=flags), \
                 patch.object(sys,"argv",["consumer","--materialized-record","new.json","--output",directory+"/new.json",*flags]), \
                 patch.object(surface,"load_optional_flint",side_effect=AssertionError("backend reached")) as load, \
                 patch.object(rz,"_reuse_full_reference_cli",side_effect=AssertionError("reuse reached")) as reuse:
                with self.assertRaises(SystemExit):rz.main()
            load.assert_not_called();reuse.assert_not_called()

    def test_coherent_forged_intervals_still_fail_external_accepted_hash_pin(self):
        from fractions import Fraction as F
        old=self.wire();reference=self.reference(old);accepted_pin=self.raw_sha(reference)
        for site in reference["sites"]:
            interval=site["intervals"]["Phi"]
            interval["lower_rational"]=str(F(interval["lower_rational"])-1)
            interval["upper_rational"]=str(F(interval["upper_rational"])-1)
        sites={row["observer_id"]:row for row in reference["sites"]}
        for target in reference["targets"]:
            target["intervals"]=rz.reflected_reference_intervals(sites[target["site_id"]]["intervals"],target["g_z_sign"])
        # All widths, input digests, statuses and site/target links remain
        # structurally coherent; the caller cannot re-authorize different bytes.
        result=self.reuse(old,reference=reference,expected_pin=accepted_pin)
        self.assertFalse(result["coverage_complete"])
        self.assertIn("externally accepted full-file SHA",result["failure"])
        for pin in ("F"*64,"0"*63,"g"*64):
            with self.subTest(pin=pin):
                self.assertFalse(self.reuse(old,expected_pin=pin)["coverage_complete"])

    def test_mapping_timeout_keeps_history_separate_and_does_not_reset(self):
        old=self.wire();reference=self.reference(old)
        budget=surface.Budget(started=1.,_full_domain_diagnostic=True)
        with patch.object(surface.time,"monotonic",return_value=242.), \
             patch.object(surface.Budget,"full_domain_diagnostic",side_effect=AssertionError("reset")) as reset:
            result=self.reuse(old,reference=reference,_shared_budget=budget)
        reset.assert_not_called();self.assertEqual(result["reference_status"],"WorkLimit")
        self.assertFalse(result["coverage_complete"]);self.assertEqual(budget.calls,0)


class OriginalFullReferenceSiteShapeTests(unittest.TestCase):
    """Original bounds/ball schema transport only; NOT new mathematical evidence."""

    def packet(self):
        fixture=CompleteReferenceReuseEngineeringTests()
        old=fixture.wire();reference=fixture.reference(old)
        for site in reference["sites"]:
            for interval in site["intervals"].values():
                interval.pop("width_rational")
                # Matches actual original schema, with explicit synthetic text.
                # The ball string is descriptive; only rational endpoints bind.
                interval["ball"]="[SYNTHETIC-TRANSPORT-NOT-A-MATH-CERTIFICATE]"
        return fixture,old,reference

    def test_original_bound_only_sites_derive_width_and_optional_width_is_checked(self):
        from fractions import Fraction as F
        fixture,old,reference=self.packet();before=copy.deepcopy(reference)
        result=fixture.reuse(old,reference=reference)
        self.assertTrue(result["coverage_complete"],result.get("failure"))
        self.assertEqual(reference,before)
        self.assertTrue(all("width_rational" in row["intervals"]["Phi"] for row in result["targets"]))
        interval=reference["sites"][0]["intervals"]["Phi"]
        interval["width_rational"]=str(F(interval["upper_rational"])-F(interval["lower_rational"]))
        self.assertTrue(fixture.reuse(old,reference=reference)["coverage_complete"])
        interval["width_rational"]="0"
        rejected=fixture.reuse(old,reference=reference)
        self.assertFalse(rejected["coverage_complete"])
        self.assertEqual(rejected["reference_status"],"UNVERIFIED")

    def test_targets_require_width_and_sites_reject_missing_reversed_or_too_wide_bounds(self):
        from fractions import Fraction as F
        for fault in ("target_width_missing","site_lower_missing","site_upper_missing","site_reversed","site_too_wide"):
            fixture,old,reference=self.packet()
            interval=reference["sites"][0]["intervals"]["Phi"]
            if fault=="target_width_missing":reference["targets"][0]["intervals"]["Phi"].pop("width_rational")
            if fault=="site_lower_missing":interval.pop("lower_rational")
            if fault=="site_upper_missing":interval.pop("upper_rational")
            if fault=="site_reversed":interval["upper_rational"]=str(F(interval["lower_rational"])-1)
            if fault=="site_too_wide":interval["upper_rational"]=str(F(interval["lower_rational"])+2*surface.CGS_G/F(10**12))
            with self.subTest(fault=fault):
                result=fixture.reuse(old,reference=reference)
                self.assertFalse(result["coverage_complete"])
                self.assertEqual(result["reference_status"],"UNVERIFIED")


class AuthenticAxisFaceOmissionTests(unittest.TestCase):
    """Zero-area axis incidence transport, not continuous force certification."""

    def omit_face(self, record, removed):
        faces=record["candidate_field"]["face_values"]
        del faces[removed]
        record["observers"]=[row for row in record["observers"]
            if row.get("face_index") != removed]
        for index, face in enumerate(faces):
            face["face_index"]=index
        for row in record["observers"]:
            if row.get("face_index",-1)>removed:row["face_index"]-=1
        record["candidate_field"]["face_gradients"]=[face["gradient"] for face in faces]

    def test_only_authentic_lower_symmetry_axis_may_omit_zero_measure_face(self):
        fixture=CompleteReferenceReuseEngineeringTests()
        record=fixture.wire(axis=True)
        self.omit_face(record,0)
        result=fixture.reuse(record)
        self.assertTrue(result["coverage_complete"],result.get("failure"))
        lower=result["side_acceleration_rows"][0]
        self.assertEqual(lower["interval"]["lower_rational"],"0")
        self.assertEqual(lower["interval"]["upper_rational"],"0")
        # Preserve the real FP64 stored value difference, rather than forcing
        # an erroneous actual gather value to equal the symmetry reference.
        record["candidate_field"]["side_acceleration"][0]=.125
        difference=fixture.reuse(record)["side_acceleration_rows"][0]["error"]
        self.assertEqual(difference["absolute_error_upper_exact"],"1/8")
        for axis, removed in ((False,0),(True,2),(True,1)):
            record=fixture.wire(axis=axis);self.omit_face(record,removed)
            rejected=fixture.reuse(record)
            self.assertFalse(rejected["coverage_complete"])
            self.assertEqual(rejected["reference_status"],"UNVERIFIED")


class FixedMatchedReferenceProfileEngineeringTests(unittest.TestCase):
    """Fixed policy/supplied-schema checks only; no Runtime or math certificate."""

    def profile_wire(self, level):
        """Independent binary-dyadic layout fixture; never Core provenance."""
        blocks=(2*(1<<level),1<<level); nx,ny=16*blocks[0],16*blocks[1]
        record=SyntheticMaterializedWireTests().wire()
        record["root_bounds"]=[0.,1.,-.5,.5]
        record["service_configuration"].update(type="none",boundary="isolated",
            relative_tolerance=1e-10,absolute_tolerance=0.,max_cycles=200)
        binding=record["native_binding"]
        binding.update(origin=[0.,-.5,0.],root_upper=[1.,.5,1.],
                       root_cells=[nx,ny,1],periodic=[False]*3,patches=[])
        record["source_identity"].update(time=0.,G=6.6743e-8,inputs=[])
        leaves=[]; cells=[]; observers=[]
        for by in range(blocks[1]):
            for bx in range(blocks[0]):
                block=len(binding["patches"]); uid=block+1
                layout=dict(dimension=2,extent=[32,24,1],stride=[1,32,768],
                    active_begin=[4,4,0],active_end=[20,20,1],centering=0)
                binding["patches"].append(dict(uid=uid,epoch=9,field_memory=0,layout=layout,
                    bound_root_identity=dict(bound=True,root_lower=[0.,-.5],root_upper=[1.,.5],
                        root_blocks=list(blocks),level=0,logical_block=[bx,by],periodic_axial=False),
                    native_layout=dict(dimension=2,ng=4,stride_y=32,stride_z=768,total_size=768,
                        origin=[bx/blocks[0],-.5+by/blocks[1],0.],
                        actual_block_upper=[(bx+1)/blocks[0],-.5+(by+1)/blocks[1]])))
                record["source_identity"]["inputs"].append(dict(uid=uid,epoch=9,slot=0,version=1,storage_generation=6))
                for j in range(16):
                    for i in range(16):
                        index=len(leaves); x=16*bx+i; y=16*by+j
                        leaf=dict(id="synthetic-fixed-cell-"+str(index),source_index=index,
                            binding_block_index=block,source_offset=(4+j)*32+4+i,
                            level=0,logical_index=[x,y,0],density=1.,r_lower=x/nx,r_upper=(x+1)/nx,
                            z_lower=-.5+y/ny,z_upper=-.5+(y+1)/ny,
                            center=[(x+.5)/nx,-.5+(y+.5)/ny,0.],stored_operator_volume=1.)
                        leaves.append(leaf)
                        cells.append(dict(source_index=index,leaf_id=leaf["id"],potential=-2.,acceleration=[0.]*3))
                        observers.append(dict(id="synthetic-fixed-cell-observer-"+str(index),kind="cell-center",
                            source_index=index,r_observer=leaf["center"][0],z_observer=leaf["center"][1]))
        record["source"]["leaves"]=leaves
        record["candidate_field"].update(cell_values=cells,side_acceleration=[0.]*(6*len(cells)))
        record["observers"]=observers+[record["observers"][-1]]
        # Retained one-face transport row is deliberately NOT a whole physical
        # face cover. Tests below do not claim full schedule/math qualification.
        return record

    def test_new_fixed_caps_do_not_change_old_profiles_or_counter_boundaries(self):
        for level,calls,seconds in ((1,6000000,600.),(2,22000000,1800.)):
            budget=surface.Budget.matched_resolution(level)
            self.assertEqual((budget.max_calls,budget.timeout_seconds),(calls,seconds))
            self.assertEqual(budget.record()["resource_profile"],"matched-resolution-"+str(level))
            budget.calls=calls
            with self.assertRaises(surface.WorkLimit):budget.take()
            self.assertEqual(budget.calls,calls)
        self.assertEqual((surface.Budget.start().max_calls,surface.Budget.start().timeout_seconds),(100000,90.))
        old=surface.Budget(started=1.,calls=17,_full_domain_diagnostic=True)
        self.assertEqual((old.max_calls,old.timeout_seconds,old.calls),(1800000,240.,17))
        for value in (0,3,-1,True,"1",600):
            with self.subTest(value=value),self.assertRaises(ValueError):surface.Budget.matched_resolution(value)
        for bad in (surface.Budget(1.,_matched_resolution=3),
                    surface.Budget(1.,_matched_resolution=True),
                    surface.Budget(1.,_matched_resolution=1,_full_domain_diagnostic=True)):
            with self.assertRaises(ValueError):rz.full_materialized_resource_profile(bad)

    def test_fixed_schema_real_offsets_complete_active_cover_and_no_input_mutation(self):
        for level in (1,2):
            record=self.profile_wire(level); before=copy.deepcopy(record)
            # Existing generic shape validation is exercised, but supplies no
            # Runtime or continuous reference authorization to this fixture.
            budget=surface.Budget.matched_resolution(level)
            rz.validate_materialized_record(record,budget)
            result=rz.validate_matched_materialized_profile(record,budget)
            self.assertEqual(result["expected_cells"],512*4**level)
            self.assertEqual(result["level"],level);self.assertEqual(record,before)
            self.assertEqual(budget.calls,0)

    def test_fixed_source_geometry_density_G_layout_and_identity_drift_rejects(self):
        import math
        original=self.profile_wire(1)
        mutations={
            "rho_ulp":lambda r:r["source"]["leaves"][0].update(density=math.nextafter(1.,2.)),
            "G_ulp":lambda r:r["source_identity"].update(G=math.nextafter(6.6743e-8,1.)),
            "root_ulp":lambda r:r["root_bounds"].__setitem__(1,math.nextafter(1.,2.)),
            "leaf_level":lambda r:r["source"]["leaves"][0].update(level=1),
            "root_blocks":lambda r:r["native_binding"]["patches"][0]["bound_root_identity"].update(root_blocks=[8,1]),
            "root_cells":lambda r:r["native_binding"].update(root_cells=[128,16,1]),
            "duplicate_uid":lambda r:r["source_identity"]["inputs"][1].update(uid=1),
            "offset":lambda r:r["source"]["leaves"][0].update(source_offset=133),
            "logical":lambda r:r["source"]["leaves"][1].update(logical_index=[0,0,0]),
            "count":lambda r:r["source"]["leaves"].pop(),
            "service_tolerance":lambda r:r["service_configuration"].update(relative_tolerance=1e-9),
            "dt_time":lambda r:r["source_identity"].update(time=.125),
            "boolean_periodic":lambda r:r["native_binding"].update(periodic=[0,0,0])}
        for fault,mutate in mutations.items():
            record=copy.deepcopy(original);mutate(record)
            with self.subTest(fault=fault),self.assertRaises((ValueError,KeyError)):
                rz.validate_matched_materialized_profile(record,surface.Budget.matched_resolution(1))
        with self.assertRaises(ValueError):
            rz.validate_matched_materialized_profile(original,surface.Budget.matched_resolution(2))

    def test_same_expired_matched_budget_rejects_before_schema_and_never_resets(self):
        budget=surface.Budget(started=1.,calls=19,_matched_resolution=1)
        with patch.object(surface.time,"monotonic",return_value=602.), \
             patch.object(surface.Budget,"start",side_effect=AssertionError("reset")) as start, \
             patch.object(surface.Budget,"matched_resolution",side_effect=AssertionError("reset")) as factory, \
             patch.object(rz,"validate_materialized_record",side_effect=AssertionError("schema reached")) as schema, \
             patch.object(surface,"load_optional_flint",side_effect=AssertionError("backend reached")) as load:
            result=rz.audit_materialized_full_record({},_shared_budget=budget)
        self.assertEqual(result["reference_status"],"WorkLimit")
        self.assertEqual(result["profile"],"actual-materialized-matched-resolution-1")
        self.assertEqual(result["budget"]["calls"],19);self.assertEqual(budget.started,1.)
        self.assertEqual(result["resource_limits"],dict(max_calls=6000000,wall_seconds=600.))
        self.assertFalse(result["coverage_complete"])
        for spy in (start,factory,schema,load):spy.assert_not_called()

    def test_matched_layout_rejection_precedes_coalescing_and_integral_backend(self):
        wire=FullDomainEngineeringTests().symmetric_wire()
        with patch.object(rz,"coalesce_exact_dense_source",side_effect=AssertionError("coalescing reached")) as coalesce, \
             patch.object(surface,"load_optional_flint",side_effect=AssertionError("backend reached")) as load, \
             patch.object(surface,"evaluate_reference",side_effect=AssertionError("kernel reached")) as evaluate:
            result=rz.audit_materialized_full_record(wire,_shared_budget=surface.Budget.matched_resolution(2))
        self.assertEqual(result["reference_status"],"UNVERIFIED")
        self.assertIn("cell count",result["failure"]);self.assertEqual(result["budget"]["calls"],0)
        for spy in (coalesce,load,evaluate):spy.assert_not_called()

    def test_same_matched_budget_reaches_reference_and_retains_every_failure_target(self):
        from types import SimpleNamespace
        wire=FullDomainEngineeringTests().symmetric_wire(); budget=surface.Budget.matched_resolution(2)
        budget.calls=23
        # Synthetic transport fixture bypasses ONLY fixed-layout checking for
        # this budget-identity test; it cannot prove the real layout or math.
        with patch.object(rz,"validate_matched_materialized_profile",return_value=dict(test_scope="SYNTHETIC-BUDGET-SPY")), \
             patch.object(surface.Budget,"start",side_effect=AssertionError("reset")) as start, \
             patch.object(surface.Budget,"matched_resolution",side_effect=AssertionError("reset")) as factory, \
             patch.object(surface,"load_optional_flint",return_value=SimpleNamespace(ctx=SimpleNamespace(dps=70))), \
             patch.object(surface,"evaluate_reference",side_effect=surface.WorkLimit("synthetic integration stop")) as evaluate:
            result=rz.audit_materialized_full_record(wire,_shared_budget=budget)
        self.assertIs(evaluate.call_args.kwargs["_shared_budget"],budget)
        self.assertEqual(result["budget"]["calls"],23);self.assertEqual(result["reference_status"],"WorkLimit")
        self.assertEqual(len(result["targets"]),4)
        self.assertTrue(all(row["reference_status"]=="UNVERIFIED_NOT_EVALUATED" for row in result["targets"]))
        start.assert_not_called();factory.assert_not_called()
        self.assertEqual(result["target_widths_exact"],{key:str(surface.CGS_G/10**12) for key in ("Phi","g_r","g_z")})
        self.assertFalse(result["science_accepted"])

    def test_new_CLI_profile_rejects_probe_subset_unknown_and_reference_reuse(self):
        import tempfile
        argv_cases=[ ["--probe-record","in.json","--full-domain","--reference-profile","matched-resolution-1"],
                     ["--materialized-record","in.json","--reference-profile","matched-resolution-2"],
                     ["--materialized-record","in.json","--full-domain","--reference-profile","matched-resolution-3"],
                     ["--materialized-record","in.json","--full-domain","--reference-profile","matched-resolution-1",
                      "--reuse-full-reference","ref.json","--reference-materialized-record","old.json",
                      "--reuse-full-reference-sha256","a"*64] ]
        with tempfile.TemporaryDirectory() as directory:
            for args in argv_cases:
                with self.subTest(args=args),patch.object(sys,"argv",["reference",*args,"--output",directory+"/new.json"]), \
                     patch.object(surface,"load_optional_flint",side_effect=AssertionError("backend reached")) as load, \
                     self.assertRaises(SystemExit) as stopped:
                    rz.main()
                self.assertEqual(stopped.exception.code,2);load.assert_not_called()

    def test_matched_CLI_backend_failure_keeps_actual_profile_and_failure_files(self):
        import tempfile,json
        budget=surface.Budget(started=1.,calls=29,_matched_resolution=1)
        with tempfile.TemporaryDirectory() as directory:
            output=Path(directory)/"full.json";summary=Path(directory)/"summary.json"
            with patch.object(sys,"argv",["reference","--materialized-record","missing.json","--full-domain",
                    "--reference-profile","matched-resolution-1","--output",str(output),"--summary-output",str(summary)]), \
                 patch.object(surface.time,"monotonic",return_value=2.), \
                 patch.object(surface.Budget,"matched_resolution",return_value=budget) as factory, \
                 patch.object(surface,"load_optional_flint",side_effect=ImportError("synthetic missing backend")), \
                 patch.object(surface.Budget,"full_domain_diagnostic",side_effect=AssertionError("old reset")) as old:
                rz.main()
            result=json.loads(output.read_text());small=json.loads(summary.read_text())
        factory.assert_called_once_with(1);old.assert_not_called()
        self.assertEqual(result["profile"],"actual-materialized-matched-resolution-1")
        self.assertEqual(result["resource_limits"],dict(max_calls=6000000,wall_seconds=600.))
        self.assertEqual(result["failure_phase"],"optional-backend-precision-freeze")
        self.assertFalse(result["coverage_complete"]);self.assertFalse(small["reference_complete"])
        self.assertEqual(small["budget"]["calls"],29)

    def test_matched_output_timeout_cannot_publish_complete_request_summary(self):
        import tempfile,json
        budget=surface.Budget(started=1.,calls=31,_matched_resolution=2)
        result=dict(profile="actual-materialized-matched-resolution-2",status="SYNTHETIC-TRANSPORT-NOT-SCIENCE",
            science_accepted=False,physical_qualified=False,core_binding_qualified=False,reference_complete=True,
            coverage_complete=True,target_widths_exact={key:str(surface.CGS_G/10**12) for key in ("Phi","g_r","g_z")},
            targets=[],sites=[],reference_status="SYNTHETIC",budget=budget.record())
        with tempfile.TemporaryDirectory() as directory,patch.object(surface.time,"monotonic",return_value=1802.):
            output=Path(directory)/"full.json";summary=Path(directory)/"small.json"
            rz.write_matched_materialized_outputs(result,output,summary,budget)
            actual=json.loads(output.read_text()); small=json.loads(summary.read_text())
        self.assertEqual(actual["reference_status"],"WorkLimit")
        self.assertEqual(actual["failure_phase"],"complete-output-serialization")
        self.assertFalse(actual["coverage_complete"]);self.assertFalse(small["reference_complete"])
        self.assertEqual(actual["budget"]["calls"],31)




class ExactDenseSweepTests(unittest.TestCase):
    """Finite black-box geometry/budget counterexamples; no integral/science grant."""

    def source(self, rectangles, root=(0, 2, 0, 1)):
        identity = NativeAdapterTests().case()["source_identity"]
        keys = ("r_lower", "r_upper", "z_lower", "z_upper")
        leaves = [dict(id="sweep-cell-"+str(index), density=3.,
                       **dict(zip(keys, rectangle)))
                  for index, rectangle in enumerate(rectangles)]
        return dict(sourceId="exact-sweep-fixture", leaves=leaves), list(root), identity

    def independent_cover(self, rectangles, root):
        # Small independent exact pairwise oracle: retain explicit coordinates,
        # strict positive intersections and containment PLUS exact area equality.
        from fractions import Fraction as F
        root = tuple(map(F, root)); values = [tuple(map(F, row)) for row in rectangles]
        L, H, A, B = root
        for index, (l, h, a, b) in enumerate(values):
            if not (L <= l < h <= H and A <= a < b <= B):
                return False
            for p, q, c, d in values[:index]:
                if max(l, p) < min(h, q) and max(a, c) < min(b, d):
                    return False
        return sum((h-l)*(b-a) for l, h, a, b in values) == (H-L)*(B-A)

    def test_touching_edges_corners_axis_rim_and_small_exact_cover_oracle(self):
        covers = [
            ([(0, 1, 0, 1), (1, 2, 0, 1)], (0, 2, 0, 1)),
            ([(0, 2, 0, ".5"), (0, 2, ".5", 1)], (0, 2, 0, 1)),
            ([(0, 1, 0, ".5"), (0, 1, ".5", 1),
              (1, 2, 0, ".5"), (1, 2, ".5", 1)], (0, 2, 0, 1)),
            ([(".1", ".4", "-.3", ".7"), (".4", "1.3", "-.3", ".2"),
              (".4", "1.3", ".2", ".7")], (".1", "1.3", "-.3", ".7")),
        ]
        for rectangles, root in covers:
            with self.subTest(rectangles=rectangles):
                self.assertTrue(self.independent_cover(rectangles, root))
                source, root, identity = self.source(rectangles, root)
                before = copy.deepcopy((source, root, identity))
                result = rz.coalesce_exact_dense_source(source, root, identity)
                self.assertEqual((source, root, identity), before)
                ids = [leaf_id for group in result["exact_partition"]
                       for leaf_id in group["original_leaf_ids"]]
                self.assertEqual(sorted(ids), sorted(leaf["id"] for leaf in source["leaves"]))
                self.assertEqual(result["original_leaf_count"], len(rectangles))
                self.assertFalse(result["science_accepted"])
                self.assertFalse(result["core_binding_qualified"])

    def test_overlapping_same_radial_start_rejects_even_when_exact_area_matches(self):
        rectangles = [(0, 1, 0, "3/4"), (0, 1, "1/2", "3/4")]
        self.assertFalse(self.independent_cover(rectangles, (0, 1, 0, 1)))
        source, root, identity = self.source(rectangles, (0, 1, 0, 1))
        with self.assertRaisesRegex(surface.ReferenceFailure, "Positive-area source overlap"):
            rz.coalesce_exact_dense_source(source, root, identity)

    def test_nested_rectangles_rejects_positive_area_not_only_equal_boundaries(self):
        source, root, identity = self.source([(0, 2, 0, 1), ("1/2", "3/2", "1/4", "3/4")])
        with self.assertRaisesRegex(surface.ReferenceFailure, "Positive-area source overlap"):
            rz.coalesce_exact_dense_source(source, root, identity)

    def test_binary_rational_radial_and_axial_slivers_are_not_rounded_away(self):
        from fractions import Fraction as F
        epsilon = F(1, 2**80)
        cases = [([(0, str(1+epsilon), 0, 1), (1, 2, 0, 1)], (0, 2, 0, 1)),
                 ([(0, 1, 0, str(F(1, 2)+epsilon)), (0, 1, "1/2", 1)], (0, 1, 0, 1))]
        for rectangles, root in cases:
            with self.subTest(rectangles=rectangles):
                self.assertFalse(self.independent_cover(rectangles, root))
                source, root, identity = self.source(rectangles, root)
                with self.assertRaisesRegex(surface.ReferenceFailure, "Positive-area source overlap"):
                    rz.coalesce_exact_dense_source(source, root, identity)

    def test_overlap_and_hole_cannot_cancel_in_original_exact_area_check(self):
        rectangles = [(0, "3/2", 0, 1), ("1/2", 1, 0, 1)]
        self.assertFalse(self.independent_cover(rectangles, (0, 2, 0, 1)))
        source, root, identity = self.source(rectangles)
        with self.assertRaisesRegex(surface.ReferenceFailure, "Positive-area source overlap"):
            rz.coalesce_exact_dense_source(source, root, identity)

    def test_disjoint_hole_still_fails_original_full_root_coverage(self):
        source, root, identity = self.source([(0, "3/4", 0, 1), (1, 2, 0, 1)])
        with self.assertRaisesRegex(surface.ReferenceFailure, "do not cover root"):
            rz.coalesce_exact_dense_source(source, root, identity)

    def test_dense_validation_retains_one_deadline_during_sweep_without_kernel_or_reset(self):
        from itertools import count
        # 32 valid rectangles; clock advances during validation only, with no
        # integral or invented backend. The SAME original 90-second Budget must
        # stop this request even after successful stamp/containment checks.
        rectangles = [(i, i+1, j, j+1) for i in range(8) for j in range(4)]
        source, root, identity = self.source(rectangles, (0, 8, 0, 4))
        budget = surface.Budget(started=0., calls=17)
        before = copy.deepcopy((source, root, identity))
        with patch.object(surface.time, "monotonic", side_effect=count()) as clock, \
             patch.object(surface.Budget, "start", side_effect=AssertionError("budget reset")) as reset, \
             patch.object(surface, "load_optional_flint", side_effect=AssertionError("backend reached")) as backend:
            with self.assertRaisesRegex(surface.WorkLimit, "global timeout=90"):
                surface.validate_dense_source(source, root, identity, budget)
        self.assertEqual((budget.started, budget.calls, budget.timeout_seconds), (0., 17, 90.))
        self.assertGreater(clock.call_count, len(rectangles))
        self.assertEqual((source, root, identity), before)
        reset.assert_not_called(); backend.assert_not_called()



class KnownFullReferenceProfileReuseTests(unittest.TestCase):
    """SYNTHETIC known-profile/import checks; no Runtime or integral evidence.

    A fixed-layout source retains the existing one-face transport fixture.
    These tests validate the reuse-import schema/schedule only, never claim
    physical all-side coverage or fabricate a successful numerical integral.
    """

    def prepared(self, level=0):
        fixture=CompleteReferenceReuseEngineeringTests()
        old=(fixture.wire() if level==0 else
             FixedMatchedReferenceProfileEngineeringTests().profile_wire(level))
        reference=fixture.reference(old)
        if level:
            policy=surface.Budget.matched_resolution(level)
            resources=rz.full_materialized_resource_profile(policy)
            reference.update(profile=resources["profile"],resource_limits=resources["resource_limits"],
                matched_resolution=rz.validate_matched_materialized_profile(old,policy))
            reference["budget"].update(max_calls=policy.max_calls,timeout_seconds=policy.timeout_seconds,
                resource_profile=policy.resource_profile)
        return old,reference

    def validate(self, old, reference, budget=None):
        fixture=CompleteReferenceReuseEngineeringTests()
        budget=surface.Budget.full_domain_diagnostic() if budget is None else budget
        values=rz.validate_materialized_record(old,budget)
        union=rz.coalesce_exact_dense_source(old["source"],old["root_bounds"],old["source_identity"],budget)
        return rz._reuse_validate_reference(old,reference,values,union,fixture.raw_sha(old),budget)

    def test_only_three_known_profiles_validate_exact_schedule_without_new_budget_or_backend(self):
        for level,count in ((0,1),(1,2048),(2,8192)):
            old,reference=self.prepared(level);before=copy.deepcopy((old,reference))
            budget=surface.Budget.full_domain_diagnostic();started=budget.started
            with self.subTest(level=level), \
                 patch.object(surface.Budget,"start",side_effect=AssertionError("request reset")) as subset, \
                 patch.object(surface.Budget,"full_domain_diagnostic",side_effect=AssertionError("mapping reset")) as full, \
                 patch.object(surface.Budget,"matched_resolution",side_effect=AssertionError("old reference restart")) as matched, \
                 patch.object(surface,"load_optional_flint",side_effect=AssertionError("backend reached")) as backend, \
                 patch.object(surface,"evaluate_reference",side_effect=AssertionError("integral reached")) as integral:
                schedule,verified=self.validate(old,reference,budget)
            self.assertEqual(len(verified),count+len(old["candidate_field"]["face_values"]))
            self.assertEqual(len(schedule["targets"]),len(verified))
            self.assertEqual((old,reference),before)
            self.assertEqual((budget.started,budget.calls,budget.max_calls,budget.timeout_seconds),
                             (started,0,1800000,240.))
            self.assertFalse(reference["science_accepted"]);self.assertFalse(reference["core_binding_qualified"])
            for spy in (subset,full,matched,backend,integral):spy.assert_not_called()

    def test_unknown_profile_wrong_layout_count_and_matched_metadata_reject(self):
        old,reference=self.prepared(1)
        faults={
            "unknown":lambda o,r:r.update(profile="actual-materialized-matched-resolution-3"),
            "known_wrong_level":lambda o,r:r.update(profile="actual-materialized-matched-resolution-2",
                resource_limits=dict(max_calls=22000000,wall_seconds=1800.)),
            "missing_matched":lambda o,r:r.pop("matched_resolution"),
            "forged_count":lambda o,r:r["matched_resolution"].update(expected_cells=2047),
            "wrong_root_blocks":lambda o,r:r["matched_resolution"].update(root_blocks=[8,1]),
            "incomplete_source":lambda o,r:o["source"]["leaves"].pop()}
        for name,mutate in faults.items():
            changed_old,changed_reference=copy.deepcopy((old,reference));mutate(changed_old,changed_reference)
            with self.subTest(fault=name),self.assertRaises((ValueError,KeyError)):
                self.validate(changed_old,changed_reference)

    def test_fixed_resource_limits_and_original_history_forgeries_reject(self):
        old,reference=self.prepared(1)
        faults={
            "missing_limits":lambda r:r.pop("resource_limits"),
            "raised_limit":lambda r:r["resource_limits"].update(max_calls=6000001),
            "lowered_limit":lambda r:r["resource_limits"].update(wall_seconds=240.),
            "calls_over":lambda r:r["budget"].update(calls=6000001),
            "calls_zero":lambda r:r["budget"].update(calls=0),
            "calls_bool":lambda r:r["budget"].update(calls=True),
            "history_max":lambda r:r["budget"].update(max_calls=1800000),
            "history_float_max":lambda r:r["budget"].update(max_calls=6000000.),
            "history_timeout":lambda r:r["budget"].update(timeout_seconds=1800.),
            "history_wall":lambda r:r["budget"].update(wall_seconds=600.),
            "history_negative_wall":lambda r:r["budget"].update(wall_seconds=-1.),
            "history_profile":lambda r:r["budget"].update(resource_profile="full-domain-diagnostic-1")}
        for name,mutate in faults.items():
            changed=copy.deepcopy(reference);mutate(changed)
            with self.subTest(fault=name),self.assertRaises(ValueError):self.validate(old,changed)
        for name in ("raised_limit","history_max","history_timeout"):
            default,default_reference=self.prepared()
            if name=="raised_limit":default_reference["resource_limits"]["max_calls"]=6000000
            if name=="history_max":default_reference["budget"]["max_calls"]=6000000
            if name=="history_timeout":default_reference["budget"]["timeout_seconds"]=600.
            with self.subTest(default_fault=name),self.assertRaises(ValueError):self.validate(default,default_reference)

    def test_matched_schema_checks_borrow_original_mapping_deadline_without_reset(self):
        old,reference=self.prepared(1)
        # Once inside strict matched validation, the original mapping deadline
        # expires. A fresh 600-second reference budget must not hide that expiry.
        budget=surface.Budget(started=1.,_full_domain_diagnostic=True)
        values=(None,old["candidate_field"]["cell_values"],[],{}, {})
        with patch.object(surface.time,"monotonic",return_value=242.), \
             patch.object(surface.Budget,"matched_resolution",side_effect=AssertionError("old request reset")) as factory, \
             patch.object(rz,"materialized_full_schedule",side_effect=AssertionError("schedule reached")) as schedule:
            with self.assertRaises(surface.WorkLimit):
                rz._reuse_validate_reference(old,reference,values,{},"0"*64,budget)
        self.assertEqual((budget.started,budget.calls,budget.timeout_seconds),(1.,0,240.))
        factory.assert_not_called();schedule.assert_not_called()

    def test_matched_reuse_rejects_new_source_change_after_import_without_integrating(self):
        import math
        fixture=CompleteReferenceReuseEngineeringTests()
        old,reference=self.prepared(1);new=copy.deepcopy(old)
        new["source"]["leaves"][0]["density"]=math.nextafter(1.,2.)
        result=fixture.reuse(old,new,reference)
        self.assertEqual(result["reference_status"],"UNVERIFIED")
        self.assertFalse(result["coverage_complete"])
        self.assertIn("Actual source bounds/density/storage changed",result["failure"])
        self.assertEqual((result["mapping_budget"]["calls"],result["budget"]["kernel_evaluations"]),(0,0))

if __name__=="__main__":unittest.main()
