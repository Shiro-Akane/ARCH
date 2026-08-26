import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from audit_combination_v2 import audit_tree


class CombinationAuditTests(unittest.TestCase):
    protected = {
        "src/physics/diffusionCoe/diffusion_math.hpp": "double vie = iec * zbar * ymas * cint;",
        "src/io/ConfigParser.h": "expects true or false",
        "src/core/RuntimeParams.h": "parser.GetBool",
        "src/driver/DriverControl.h": "1.0e-12",
        "src/main.cpp": "config.Get<std::string>(\"log_dir\", config.io.out_dir)",
        "src/physics/eos/eos_Utils.h": "get_isentropic_state_at_pressure_factor",
        "src/physics/eos/Tabular3DEOS.h": "eos_utils",
        "src/physics/eos/Tabular4DEOS.h": "eos_utils",
        "src/core/UserInterface.h": "ProblemHelper.h",
        "src/core/ProblemHelper.cpp": "eos_utils::get_isentropic_state_at_pressure_factor",
        "src/core/ProblemHelper.h": "GetRootCellWidth",
        "simulation/SmoothAdvection/SmoothAdvection.cpp": "ProblemHelper::GetRootCellWidth",
        "simulation/DiffusionMode/DiffusionMode.cpp": "ProblemHelper::GetRootCellWidth",
        "simulation/Cellular/Cellular.par": "EOS_toolkit/tables/helmholtz/helm_table.dat",
    }

    @classmethod
    def populate_protected(cls, root):
        for relative, contents in cls.protected.items():
            target = root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(contents, encoding="utf-8")

    def assert_rejected(self, relative_path, contents):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            self.populate_protected(root)
            target = root / relative_path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(contents, encoding="utf-8")
            self.assertTrue(audit_tree(root), relative_path)

    def assert_accepted(self, files):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            self.populate_protected(root)
            for relative_path, contents in files.items():
                target = root / relative_path
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text(contents, encoding="utf-8")
            self.assertEqual(audit_tree(root), [])

    def test_rejects_core_formula_copy(self):
        self.assert_rejected("src/cuda/HydroCore.cuh", "double flux = rho * velocity;")

    def test_rejects_adapter_formula_copy(self):
        self.assert_rejected("src/cuda/EosAdapter.cuh", "return pressure_from_rho_T(rho, T);")

    def test_accepts_exact_include_only_diagnostic_adapter(self):
        self.assert_accepted({
            "src/cuda/common/DiffusionConfigViewAdapter.h":
                "#pragma once\n"
                "#pragma GCC system_header\n"
                "#include \"numerics/diffusion/DiffFlux.h\"\n"
        })

    def test_rejects_diagnostic_adapter_with_owned_declaration(self):
        self.assert_rejected(
            "src/cuda/common/DiffusionConfigViewAdapter.h",
            "#pragma GCC system_header\n"
            "#include \"numerics/diffusion/DiffFlux.h\"\n"
            "inline int copied_formula(int value) { return value + 1; }\n")

    def test_rejects_device_formula_copy(self):
        self.assert_rejected("src/cuda/diffusion_device.cuh", "double vie = iec * zbar;")

    def test_rejects_second_runner(self):
        self.assert_rejected("src/cuda/CudaRunner.cu", "int main() { return 0; }")

    def test_rejects_complete_cuda_rk_controller(self):
        self.assert_rejected("src/cuda/RKController.cu", "for (int stage = 0; stage < 3; ++stage) {}")

    def test_rejects_complete_cuda_rkl_controller(self):
        self.assert_rejected("src/cuda/RKLController.cu", "for (int stage = 0; stage < 3; ++stage) {}")

    def test_rejects_cuda_boundary_duplication(self):
        self.assert_rejected("src/cuda/Boundary.cuh", "void fill_boundary() {}")

    def test_rejects_production_cuda_glob(self):
        self.assert_rejected("CMakeLists.txt", "file(GLOB_RECURSE cuda *.cu)\nadd_executable(ARCH ${cuda})")

    def test_rejects_object_injection(self):
        self.assert_rejected("CMakeLists.txt", "target_sources(ARCH PRIVATE $<TARGET_OBJECTS:cuda>)")

    def test_rejects_legacy_cuda_entrypoints(self):
        self.assert_rejected("src/cuda/main_cuda.cu", "int main() { return 0; }")

    def test_rejects_hidden_fallback_and_authority_regression(self):
        self.assert_rejected("src/driver/DriverControl.h", "if (cuda_failed) use_cpu_fallback();\n1e-14")

    def test_rejects_missing_protected_file(self):
        with tempfile.TemporaryDirectory() as temporary:
            self.assertTrue(audit_tree(pathlib.Path(temporary)))

    def test_rejects_retired_helm_path(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            self.populate_protected(root)
            (root / "simulation/Cellular/Cellular.par").write_text(
                "EOS_toolkit/eos_tabular/helmholtz/helm_table.dat", encoding="utf-8")
            self.assertTrue(audit_tree(root))

    def test_rejects_historical_controller_identifier(self):
        self.assert_rejected("src/cuda/primitive.cu", "CudaAuthorityIntegrator loop;")

    def test_rejects_cuda_boundary_semantic_copy(self):
        self.assert_rejected("src/cuda/primitive.cu", "boundary_type = outflow")

    def test_rejects_generic_cpu_fallback(self):
        self.assert_rejected("src/driver/Execution.cpp", "if (failed) cpu fallback")

    def test_rejects_arch_direct_cuda_source(self):
        self.assert_rejected("CMakeLists.txt", "add_executable(ARCH src/cuda/primitive.cu)")

    def test_rejects_neutral_stage_controller(self):
        self.assert_rejected("src/cuda/solver.cuh", "for (int stage = 0; stage < 3; ++stage) launch();")

    def test_rejects_neutral_boundary_mapping(self):
        self.assert_rejected("src/cuda/halo.cuh", "if (boundary_type == outflow) update();")

    def test_rejects_cuda_object_and_duplicate_ownership(self):
        self.assert_rejected("CMakeLists.txt", "add_library(x OBJECT cuda.cu)\nadd_library(y STATIC cuda.cu)")

    def test_rejects_two_nonobject_cuda_source_owners(self):
        self.assert_rejected(
            "CMakeLists.txt",
            "add_library(x STATIC cuda.cu)\nadd_library(y STATIC cuda.cu)")

    def test_rejects_duplicate_cuda_source_within_one_target(self):
        self.assert_rejected(
            "CMakeLists.txt",
            "add_library(x STATIC cuda.cu cuda.cu)")

    def test_rejects_unconditional_cmake_feature_guard(self):
        self.assert_rejected(
            "CMakeLists.txt",
            "if(TRUE)\nadd_executable(arch_cuda_test test.cu)\nendif()")

    def test_rejects_production_cuda_backend_under_testing_guard(self):
        self.assert_rejected(
            "CMakeLists.txt",
            "if(BUILD_TESTING)\nadd_library(arch_cuda_backend STATIC x.cu)\nendif()")

    def test_rejects_duplicate_runtime_probe_owner(self):
        self.assert_rejected(
            "CMakeLists.txt",
            "target_sources(ARCH PRIVATE src/driver/RuntimeProbe.cpp)")

    def test_rejects_e3_resolution_regression_markers(self):
        for marker in (
                "resolve_execution_plan_bypassed",
                "handwritten_network_mapping",
                "parse_ode_again",
                "probe_after_backend_construction",
                "allocate_backend_before_resolution",
                "requested_omitted=",
                "mutate_plan_after_sidecar"):
            with self.subTest(marker=marker):
                self.assert_rejected(
                    "src/driver/SolverDispatch.cpp", marker)

    def test_rejects_e3_ownership_regression_markers(self):
        for marker in (
                "retained_block",
                "retained_pool_index",
                "upload_current_fluid_state",
                "allocate_and_initialize",
                "leaked_helm_owner",
                "recomputed_boundary_sources",
                "get_rkl_coeffs_drifted"):
            with self.subTest(marker=marker):
                self.assert_rejected(
                    "src/cuda/runtime/CudaBackend.cu", marker)

    def test_rejects_incomplete_bounded_hydro_lowering(self):
        self.assert_rejected(
            "src/cuda/hydro/HydroIntegratorPolicies.cuh",
            "void launch_bounded_hydro_stage();")

    def test_rejects_explicit_cuda_owned_stage_loops(self):
        for marker in ("stage_loop_owned_by_cuda", "cuda_owned_rkl_loop"):
            with self.subTest(marker=marker):
                self.assert_rejected("src/cuda/stage.cuh", marker)

    def test_rejects_cuda_runtime_accepting_mutable_config(self):
        self.assert_rejected(
            "src/cuda/runtime/CudaBackend.h",
            "void make(const SimConfig& launch);")

    def test_accepts_one_owner_with_repeated_source_metadata(self):
        self.assert_accepted({
            "CMakeLists.txt":
                "add_executable(one tests/cuda/test_cuda_hydro_block.cu)\n"
                "set_source_files_properties(tests/cuda/test_cuda_hydro_block.cu "
                "PROPERTIES LANGUAGE CXX)"
        })

    def test_accepts_bounded_e3_stage_seams_without_a_controller(self):
        self.assert_accepted({
            "src/cuda/hydro/HydroIntegratorPolicies.cuh":
                "/** @file HydroIntegratorPolicies.cuh */\n"
                "void launch_one_bounded_hydro_stage();",
            "src/cuda/diffusion/DiffusionSolver.cuh":
                "/** @file DiffusionSolver.cuh */\n"
                "void launch_one_bounded_diffusion_stage();",
            "src/cuda/runtime/CudaBackend.cu":
                "#include \"cuda/hydro/HydroIntegratorPolicies.cuh\"\n"
                "#include \"cuda/diffusion/DiffusionSolver.cuh\""
        })

    def test_rejects_arch_cuda_variable_injection(self):
        self.assert_rejected("CMakeLists.txt", "target_sources(ARCH PRIVATE ${cuda_sources})")

    def test_rejects_noncanonical_object_injection(self):
        self.assert_rejected("CMakeLists.txt", "target_sources(ARCH PRIVATE $<TARGET_OBJECTS:backend>)")

    def test_accepts_only_central_resolver_fallback_reason_field(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            self.populate_protected(root)
            target = root / "src/driver/dispatch/BackendCapabilities.h"
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(
                "enum class ComputeBackend { Cpu, Cuda }; "
                "struct BackendResolution { ComputeBackend requested_backend; ComputeBackend resolved_backend; "
                "const char* fallback_reason; };",
                encoding="utf-8")
            self.assertEqual(audit_tree(root), [])

    def test_accepts_resolved_fallback_reason_sidecar_read(self):
        self.assert_accepted({
            "src/driver/SolverDispatch.cpp":
                "output << resolution.fallback_reason; "
                "if (resolution.resolved_backend == ComputeBackend::Cpu) {}"
        })

    def test_rejects_fallback_reason_field_outside_central_resolver(self):
        self.assert_rejected(
            "src/driver/Execution.cpp",
            "struct Result { const char* fallback_reason; }; // cpu cuda")

    def test_rejects_generic_fallback_inside_central_resolver_path(self):
        self.assert_rejected(
            "src/driver/dispatch/BackendCapabilities.h",
            "if (cuda_failed) cpu_fallback();")

    def test_rejects_cuda_burn_route_without_caller_workspace(self):
        self.assert_rejected(
            "src/cuda/runtime/CudaBackend.cu",
            "launch(nullptr, impl_->burn_candidates.get());")

    def test_rejects_cuda_burn_route_without_failed_nse_continuation(self):
        self.assert_rejected(
            "src/cuda/runtime/CudaBackend.cu",
            "execute_burn_policy_cell_without_failed_nse_continuation();")

    def test_rejects_cuda_burn_limiter_using_full_host_state(self):
        self.assert_rejected(
            "src/driver/Driver.h",
            "DriverBurn::combine_full_host_state_minimum();")


if __name__ == "__main__":
    unittest.main()
