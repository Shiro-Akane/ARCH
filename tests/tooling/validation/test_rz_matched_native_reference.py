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



if __name__=="__main__":unittest.main()
