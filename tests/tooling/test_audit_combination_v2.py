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

    def assert_rejected_with(self, files, expected):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            self.populate_protected(root)
            for relative_path, contents in files.items():
                target = root / relative_path
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text(contents, encoding="utf-8")
            violations = audit_tree(root)
            self.assertTrue(
                any(expected in violation for violation in violations),
                violations)

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

    def test_accepts_reviewed_cuda_infrastructure_owner_names(self):
        self.assert_accepted({
            "src/cuda/runtime/CudaBackendCore.cpp":
                "void quiesce_runtime();",
            "src/cuda/microphysics/helm_eos_device_owner.h":
                "struct HelmTableOwner;",
            "src/cuda/microphysics/helm_eos_device_owner.cpp":
                "void upload_helm_table();",
            "src/cuda/microphysics/tabular3_eos_device_owner.h":
                "struct Tabular3TableOwner;",
            "src/cuda/microphysics/tabular3_eos_device_owner.cpp":
                "void upload_tabular3_table();",
            "src/cuda/microphysics/tabular4_eos_device_owner.h":
                "struct Tabular4TableOwner;",
            "src/cuda/microphysics/tabular4_eos_device_owner.cpp":
                "void upload_tabular4_table();",
        })

    def test_rejects_unreviewed_cuda_device_owner_name(self):
        self.assert_rejected(
            "src/cuda/microphysics/custom_eos_device_owner.cpp",
            "void upload_custom_table();")

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
        markers = {
            "retained_block": "src/cuda/runtime/CudaBackendStore.cpp",
            "retained_pool_index": "src/cuda/runtime/CudaBackendStore.cpp",
            "upload_current_fluid_state":
                "src/cuda/runtime/CudaBackendStore.cpp",
            "allocate_and_initialize":
                "src/cuda/runtime/CudaBackendResources.cpp",
            "leaked_helm_owner":
                "src/cuda/microphysics/helm_eos_device_owner.cpp",
            "recomputed_boundary_sources":
                "src/cuda/runtime/CudaBackendResources.cpp",
            "get_rkl_coeffs_drifted":
                "src/cuda/runtime/CudaBackendMicrophysicsControl.cpp",
        }
        for marker, owner in markers.items():
            with self.subTest(marker=marker, owner=owner):
                self.assert_rejected(owner, marker)

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
            "src/cuda/runtime/CudaBackendHydroControl.cpp":
                "#include \"cuda/hydro/HydroIntegratorPolicies.cuh\"\n"
                "#include \"cuda/diffusion/DiffusionSolver.cuh\""
        })

    def test_rejects_arch_cuda_variable_injection(self):
        self.assert_rejected("CMakeLists.txt", "target_sources(ARCH PRIVATE ${cuda_sources})")

    def test_rejects_noncanonical_object_injection(self):
        self.assert_rejected("CMakeLists.txt", "target_sources(ARCH PRIVATE $<TARGET_OBJECTS:backend>)")

    def test_accepts_split_cuda_backend_object_assembly(self):
        self.assert_accepted({
            "CMakeLists.txt":
                "add_library(arch_cuda_backend_burn_ideal OBJECT "
                "src/cuda/runtime/CudaBackendBurnIdeal.cu)\n"
                "add_library(arch_cuda_backend STATIC runtime.cu "
                "$<TARGET_OBJECTS:arch_cuda_backend_burn_ideal>)\n"
                "target_link_libraries(ARCH PRIVATE arch_cuda_backend)\n"
        })

    def test_accepts_canonical_split_cuda_backend_helper(self):
        self.assert_accepted({
            "CMakeLists.txt":
                "function(arch_configure_cuda_backend_object target source)\n"
                "  add_library(${target} OBJECT ${source})\n"
                "endfunction()\n"
                "arch_configure_cuda_backend_object("
                "arch_cuda_backend_burn_ideal "
                "src/cuda/runtime/CudaBackendBurnIdeal.cu)\n"
                "add_library(arch_cuda_backend STATIC runtime.cu "
                "$<TARGET_OBJECTS:arch_cuda_backend_burn_ideal>)\n"
        })

    def test_accepts_canonical_functional_device_objects(self):
        self.assert_accepted({
            "CMakeLists.txt":
                "function(arch_configure_cuda_backend_object target source)\n"
                "  add_library(${target} OBJECT ${source})\n"
                "endfunction()\n"
                "arch_configure_cuda_backend_object("
                "arch_cuda_backend_hydro "
                "src/cuda/runtime/CudaBackendHydro.cu)\n"
                "arch_configure_cuda_backend_object("
                "arch_cuda_backend_diffusion "
                "src/cuda/runtime/CudaBackendDiffusion.cu)\n"
                "arch_configure_cuda_backend_object("
                "arch_cuda_backend_exchange "
                "src/cuda/runtime/CudaBackendExchange.cu)\n"
                "arch_configure_cuda_backend_object("
                "arch_cuda_backend_amr_flux "
                "src/cuda/runtime/CudaBackendAmrFlux.cu)\n"
                "arch_configure_cuda_backend_object("
                "arch_cuda_backend_burn_tabular3d_aprox13 "
                "src/cuda/runtime/CudaBackendBurnTabular3DAprox13.cu)\n"
                "arch_configure_cuda_backend_object("
                "arch_cuda_backend_burn_tabular4d_iso7 "
                "src/cuda/runtime/CudaBackendBurnTabular4DIso7.cu)\n"
                "add_library(arch_cuda_backend STATIC runtime.cu "
                "$<TARGET_OBJECTS:arch_cuda_backend_hydro> "
                "$<TARGET_OBJECTS:arch_cuda_backend_diffusion> "
                "$<TARGET_OBJECTS:arch_cuda_backend_exchange> "
                "$<TARGET_OBJECTS:arch_cuda_backend_amr_flux> "
                "$<TARGET_OBJECTS:arch_cuda_backend_burn_tabular3d_aprox13> "
                "$<TARGET_OBJECTS:arch_cuda_backend_burn_tabular4d_iso7>)\n"
        })

    def test_accepts_canonical_functional_host_objects(self):
        self.assert_accepted({
            "CMakeLists.txt":
                "function(arch_configure_cuda_host_object "
                "target source debug_level)\n"
                "  add_library(${target} OBJECT ${source})\n"
                "endfunction()\n"
                "arch_configure_cuda_host_object("
                "arch_cuda_backend_eos_helm "
                "src/cuda/microphysics/helm_eos_device_owner.cpp -g0)\n"
                "arch_configure_cuda_host_object("
                "arch_cuda_backend_core "
                "src/cuda/runtime/CudaBackendCore.cpp -g1)\n"
                "arch_configure_cuda_host_object("
                "arch_cuda_backend_hydro_control "
                "src/cuda/runtime/CudaBackendHydroControl.cpp -g1)\n"
                "add_library(arch_cuda_backend STATIC "
                "$<TARGET_OBJECTS:arch_cuda_backend_eos_helm> "
                "$<TARGET_OBJECTS:arch_cuda_backend_core> "
                "$<TARGET_OBJECTS:arch_cuda_backend_hydro_control>)\n"
        })

    def test_rejects_swapped_canonical_backend_object_source(self):
        self.assert_rejected(
            "CMakeLists.txt",
            "function(arch_configure_cuda_backend_object target source)\n"
            "  add_library(${target} OBJECT ${source})\n"
            "endfunction()\n"
            "arch_configure_cuda_backend_object("
            "arch_cuda_backend_hydro "
            "src/cuda/runtime/CudaBackendDiffusion.cu)\n")

    def test_rejects_swapped_canonical_host_object_source(self):
        self.assert_rejected_with({
            "CMakeLists.txt":
                "function(arch_configure_cuda_host_object "
                "target source debug_level)\n"
                "  add_library(${target} OBJECT ${source})\n"
                "endfunction()\n"
                "arch_configure_cuda_host_object("
                "arch_cuda_backend_core "
                "src/cuda/runtime/CudaBackendResources.cpp -g1)\n"
        }, "CUDA Host OBJECT libraries must keep their canonical owners")

    def test_rejects_wrong_canonical_host_object_debug_policy(self):
        self.assert_rejected_with({
            "CMakeLists.txt":
                "function(arch_configure_cuda_host_object "
                "target source debug_level)\n"
                "  add_library(${target} OBJECT ${source})\n"
                "endfunction()\n"
                "arch_configure_cuda_host_object("
                "arch_cuda_backend_core "
                "src/cuda/runtime/CudaBackendCore.cpp -g0)\n"
        }, "CUDA Host OBJECT libraries must keep their canonical owners")

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
        self.assert_rejected_with({
            "src/cuda/runtime/CudaBackendMicrophysicsControl.cpp":
                "void CudaBackend::execute_burn() {\n"
                "  launch_cuda_burn_route(nullptr, block.burn_candidates.get(), "
                "block.burn_statuses.get(), block.burn_summary.get());\n"
                "  quiesce();\n"
                "  impl_->runtime_counters.kernel_count += 2;\n"
                "}\n"
        }, "CUDA burn routes must consume the caller workspace")

    def test_accepts_split_runtime_completion_contracts(self):
        self.assert_accepted({
            "src/cuda/runtime/CudaBackendHydroControl.cpp":
                "double CudaBackend::compute_hydro_dt() {\n"
                "  launch_cuda_backend_hydro_dt(); quiesce();\n"
                "  impl_->runtime_counters.kernel_count += 2; return 1.0;\n"
                "}\n"
                "void CudaBackend::execute_hydro_stage() {\n"
                "  launch_cuda_backend_hydro_stage(); quiesce();\n"
                "  impl_->runtime_counters.kernel_count += 1;\n"
                "}\n"
                "void CudaBackend::execute_physical_boundary() {\n"
                "  launch_cuda_backend_boundary_plan(); quiesce();\n"
                "  for (const auto& phase : block.boundary.phases) use(phase);\n"
                "}\n",
            "src/cuda/runtime/CudaBackendMicrophysicsControl.cpp":
                "double CudaBackend::compute_diffusion_dt() {\n"
                "  launch_cuda_backend_diffusion_dt(); quiesce();\n"
                "  impl_->runtime_counters.kernel_count += 1; return 1.0;\n"
                "}\n"
                "void CudaBackend::execute_diffusion_stage() {\n"
                "  launch_cuda_backend_diffusion_stage(); quiesce();\n"
                "  impl_->runtime_counters.kernel_count += 1;\n"
                "}\n"
                "void CudaBackend::execute_burn() {\n"
                "  launch_cuda_burn_route("
                "block.burn_workspace_storage.get(), "
                "block.burn_candidates.get(), block.burn_statuses.get(), "
                "block.burn_summary.get());\n"
                "  quiesce(); impl_->runtime_counters.kernel_count += 2;\n"
                "}\n",
        })

    def test_rejects_split_runtime_completion_before_quiescence(self):
        cases = {
            "compute_hydro_dt": (
                "src/cuda/runtime/CudaBackendHydroControl.cpp",
                "launch_cuda_backend_hydro_dt();"),
            "execute_hydro_stage": (
                "src/cuda/runtime/CudaBackendHydroControl.cpp",
                "launch_cuda_backend_hydro_stage();"),
            "compute_diffusion_dt": (
                "src/cuda/runtime/CudaBackendMicrophysicsControl.cpp",
                "launch_cuda_backend_diffusion_dt();"),
            "execute_diffusion_stage": (
                "src/cuda/runtime/CudaBackendMicrophysicsControl.cpp",
                "launch_cuda_backend_diffusion_stage();"),
            "execute_burn": (
                "src/cuda/runtime/CudaBackendMicrophysicsControl.cpp",
                "launch_cuda_burn_route("
                "block.burn_workspace_storage.get(), "
                "block.burn_candidates.get(), block.burn_statuses.get(), "
                "block.burn_summary.get());"),
        }
        for function_name, (owner, launch) in cases.items():
            with self.subTest(function_name=function_name):
                self.assert_rejected_with({
                    owner:
                        f"void CudaBackend::{function_name}() {{\n"
                        f"  {launch}\n"
                        "  impl_->runtime_counters.kernel_count += 1;\n"
                        "  quiesce();\n"
                        "}\n"
                }, f"CUDA bounded work must quiesce before completion: "
                   f"{function_name}")

    def test_rejects_boundary_quiescence_from_another_function(self):
        self.assert_rejected_with({
            "src/cuda/runtime/CudaBackendHydroControl.cpp":
                "void unrelated() { launch_cuda_backend_boundary_plan(); "
                "quiesce(); for (const auto& phase : "
                "block.boundary.phases) use(phase); }\n"
                "void CudaBackend::execute_physical_boundary() {\n"
                "  launch_cuda_backend_boundary_plan();\n"
                "  for (const auto& phase : block.boundary.phases) use(phase);\n"
                "}\n"
        }, "CUDA boundary completion must follow stream quiescence")

    def test_rejects_runtime_function_in_legacy_monolith(self):
        self.assert_rejected_with({
            "src/cuda/runtime/CudaBackend.cu":
                "void CudaBackend::execute_hydro_stage() {\n"
                "  launch_cuda_backend_hydro_stage(); quiesce();\n"
                "  impl_->runtime_counters.kernel_count += 1;\n"
                "}\n"
        }, "CUDA runtime function has the wrong functional owner")

    def test_rejects_cuda_burn_route_without_failed_nse_continuation(self):
        self.assert_rejected(
            "src/cuda/runtime/CudaBackendMicrophysicsControl.cpp",
            "execute_burn_policy_cell_without_failed_nse_continuation();")

    def test_rejects_cuda_burn_limiter_using_full_host_state(self):
        self.assert_rejected(
            "src/driver/Driver.h",
            "DriverBurn::combine_full_host_state_minimum();")

    def test_repository_tree_passes_audit(self):
        self.assertEqual(audit_tree(ROOT), [])


if __name__ == "__main__":
    unittest.main()
