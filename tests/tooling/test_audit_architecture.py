import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from audit_architecture import audit_tree


class CombinationAuditTests(unittest.TestCase):
    object_helpers = """
function(arch_configure_cuda_backend_object target source)
    add_library(${target} OBJECT ${source})
endfunction()
function(arch_configure_cuda_host_object target source debug_level)
    add_library(${target} OBJECT ${source})
endfunction()
"""

    def generated_route_files(self, kind="dense"):
        if kind == "custom":
            template = "cmake/CudaCustomDenseRoute.cu.in"
            target = "arch_cuda_burn_${custom_eos_tag}_${custom_id}"
            source = "${ARCH_CUSTOM_REGISTRY_DIR}/burn_${custom_eos_tag}_${custom_id}.cu"
            directory = ""
        else:
            template = ("cmake/templates/CudaBurnDenseRoute.cu.in" if kind == "dense"
                        else "cmake/templates/CudaBurnSparseOwner.cu.in")
            target = ("arch_cuda_backend_burn_${eos_token}_${network_name}"
                      if kind == "dense"
                      else "arch_cuda_backend_sparse_${network_name}_${eos_token}")
            source = ("${route_dir}/${eos_token}_${network_name}.cu" if kind == "dense"
                      else "${route_dir}/${network_name}_${eos_token}.cu")
            directory = f'set(route_dir "${{CMAKE_CURRENT_BINARY_DIR}}/generated/cuda_{kind}_burn")'
        return {
            "CMakeLists.txt": self.object_helpers + "include(cmake/Routes.cmake)\n",
            "cmake/Routes.cmake": directory + f"""
set(route_target "{target}")
set(route_source "{source}")
configure_file({template} "${{route_source}}" @ONLY)
arch_configure_cuda_backend_object(${{route_target}} "${{route_source}}")
target_sources(arch_cuda_backend PRIVATE $<TARGET_OBJECTS:${{route_target}}>)
""",
            template: (ROOT / template).read_text(encoding="utf-8"),
        }

    def test_rejects_fast_math_for_shared_physics(self):
        for flag in ("-ffast-math", "-Ofast", "--use_fast_math",
                     "-fassociative-math", "-funsafe-math-optimizations"):
            with self.subTest(flag=flag):
                self.assert_rejected_with(
                    {"CMakeLists.txt": f'add_compile_options({flag})'},
                    "shared mathematics forbids")

    def test_header_guards_do_not_excuse_include_cycles(self):
        self.assert_rejected_with({
            "src/physics/A.h": '#pragma once\n#include "B.h"\n',
            "src/physics/B.h": '#pragma once\n#include "A.h"\n',
        }, "local header include cycle")

    def test_rejects_missing_explicit_relative_source_header(self):
        self.assert_rejected_with({
            "src/physics/A.h": '#include "../numerics/Missing.h"\n',
        }, "unresolved relative source include")

    def test_generated_bare_header_is_left_to_configured_compiler(self):
        self.assert_accepted({
            "src/physics/A.h": '#include "CustomNetworks.generated.h"\n',
        })

    def test_missing_project_header_is_not_mistaken_for_external_dependency(self):
        self.assert_rejected_with({
            "src/physics/A.h": '#include "physics/Retired.h"\n',
        }, "unresolved project source include")

    def test_sdk_prefix_is_not_guessed_from_a_matching_basename(self):
        self.assert_accepted({
            "src/physics/A.h": '#include "sdk/Properties.h"\n',
            "src/physics/Properties.h": '// unrelated project record\n',
        })

    def test_generic_burn_cannot_reintroduce_catalogue_through_helper(self):
        self.assert_rejected_with({
            "src/numerics/burnsolver/ode_bd.h": '#include "Helper.h"\n',
            "src/numerics/burnsolver/Helper.h": '#include "Networks.h"\n',
            "src/numerics/burnsolver/Networks.h": '// fixture catalogue\n',
        }, "generic burn mathematics must not include the network catalogue")

    def test_factory_may_include_catalogue_but_unrelated_basenames_are_not_edges(self):
        self.assert_accepted({
            "src/numerics/burnsolver/BurnDispatch.h": '#include "Networks.h"\n',
            "src/numerics/burnsolver/Networks.h": '// fixture catalogue\n',
            "src/numerics/burnsolver/ode_bd.h": '#include "external/Networks.h"\n',
        })

    def test_declaration_abi_cannot_import_eos_indirectly(self):
        self.assert_rejected_with({
            "src/cuda/runtime/burn/CudaBackendBurn.h": '#include "Helper.h"\n',
            "src/cuda/runtime/burn/Helper.h": '#include "physics/eos/HelmEos.h"\n',
            "src/physics/eos/HelmEos.h": '// fixture implementation\n',
        }, "CUDA declaration ABI must not import EOS implementations")

    def test_launch_types_cannot_import_operators_through_forwarding_header(self):
        for header in ("cuda/common/CudaLaunchConfig.h",
                       "cuda/runtime/diffusion/CudaBackendDiffusion.h",
                       "numerics/diffusion/DiffusionTypes.h"):
            for body in ("grid/Grid.h", "numerics/diffusion/DiffFlux.h",
                         "numerics/integrator/TimeIntegratorHelper.h"):
                with self.subTest(header=header, body=body):
                    self.assert_rejected_with({
                        "src/" + header: '#include "Bridge.h"\n',
                        "src/Bridge.h": f'#include "{body}"\n',
                        "src/" + body: '// fixture body\n',
                    }, "must not import grid or numerical operators")

    def test_cell_policy_and_workspace_must_not_import_host_or_nse(self):
        for header, imported in (
                ("driver/DriverBurnPolicy.h", "core/RuntimeParams.h"),
                ("cuda/microphysics/common.h", "physics/nse/nse_solver.h")):
            with self.subTest(header=header):
                self.assert_rejected_with({
                    "src/" + header: f'#include "{imported}"\n',
                    "src/" + imported: '// fixture body\n',
                }, "must not import host iteration, parsing or NSE")

    def test_duck_typed_network_support_does_not_own_linear_provider(self):
        for body in ("core/RuntimeParams.h", "numerics/linalg/DenseWrap.h",
                     "numerics/linalg/SparseWrap.h"):
            with self.subTest(body=body):
                self.assert_rejected_with({
                    "src/physics/network/timmes_common/TimmesNetworkSupport.h":
                        f'#include "{body}"\n',
                    "src/" + body: '// fixture body\n',
                }, "network support must not import parsing or linear providers")

    def test_complete_eos_is_allowed_in_actual_owner(self):
        self.assert_accepted({
            "src/cuda/runtime/control/CudaBackendInternal.h": '#include "physics/eos/HelmEos.h"\n',
            "src/physics/eos/HelmEos.h": '// fixture body\n',
            "src/cuda/runtime/burn/CudaBackendBurn.h": '#include "physics/eos/eos.h"\n',
            "src/physics/eos/eos.h": 'struct IdealGasView;\n',
        })

    def test_sparse_allocation_reuse_must_not_import_complete_runtime(self):
        self.assert_rejected_with({
            "src/cuda/runtime/burn/CudaBackendBurnSparseImpl.cuh": '#include "cuda/runtime/control/CudaBackendInternal.h"\n',
            "src/cuda/runtime/control/CudaBackendInternal.h": '// fixture runtime owner\n',
        }, "typed sparse/allocation owner must not import the complete runtime layout")

    def test_shared_build_contract_requires_strict_host_and_device(self):
        self.assert_rejected_with(
            {"CMakeLists.txt": "add_library(arch_build_contract INTERFACE)"},
            "one strict Host/CUDA build contract")

    def test_accepts_shared_strict_floating_point_contract(self):
        self.assert_accepted({"CMakeLists.txt": """
add_library(arch_build_contract INTERFACE)
target_compile_options(arch_build_contract INTERFACE
    $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-fno-fast-math;-ffp-contract=off>
    $<$<COMPILE_LANG_AND_ID:CUDA,NVIDIA>:--fmad=false;--ftz=false;--prec-div=true;--prec-sqrt=true;-Xcompiler=-fno-fast-math,-ffp-contract=off>)
target_link_options(arch_build_contract INTERFACE
    $<$<LINK_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-fno-fast-math;-ffp-contract=off>
    $<$<LINK_LANG_AND_ID:CUDA,NVIDIA>:$<HOST_LINK:-fno-fast-math;-ffp-contract=off>>)
"""})

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

    def test_rejects_retired_include_only_diagnostic_adapter(self):
        self.assert_rejected_with({
            "src/cuda/common/DiffusionConfigViewAdapter.h":
                "#pragma once\n"
                "#pragma GCC system_header\n"
                "#include \"numerics/diffusion/DiffFlux.h\"\n"
        }, "formula-copy filename")

    def test_rejects_diagnostic_adapter_with_owned_declaration(self):
        self.assert_rejected(
            "src/cuda/common/DiffusionConfigViewAdapter.h",
            "#pragma GCC system_header\n"
            "#include \"numerics/diffusion/DiffFlux.h\"\n"
            "inline int copied_formula(int value) { return value + 1; }\n")

    def test_accepts_only_pod_geometry_field_binding(self):
        relative = "src/cuda/hydro/GridGeometryAdapter.cuh"
        source = (ROOT / relative).read_text(encoding="utf-8")
        dependencies = {
            "src/cuda/common/CudaCommon.cuh": "// fixture device records\n",
            "src/grid/GridMetrics.h": "// fixture shared metric interface\n",
        }
        self.assert_accepted({**dependencies, relative: source})
        for mutation in (
                source + "inline double copied_volume(double r) { return r * r; }",
                source.replace("grid.dx1,", "grid.dx1 * grid.dx1,"),
                source.replace("Geometry::Unsupported", "Geometry::Cartesian"),
                source.replace("grid.x1_min, grid.x2_min", "grid.x2_min, grid.x1_min")):
            with self.subTest(mutation=mutation[-100:]):
                self.assert_rejected_with({**dependencies, relative: mutation}, "formula-copy filename")

    def test_diagnostics_and_comments_do_not_define_backend_rules(self):
        self.assert_accepted({
            "src/cuda/runtime/amr/CudaBackendMigration.cpp": """
// CPU fallback is forbidden; physical boundary mapping lives in shared plans.
void stage() { check(launch_plan(), "launch staged CUDA physical boundary"); }
""",
            "src/cuda/microphysics/CuDssSparseSolver.cpp": """
// Destructor cleanup is not a CPU fallback.
void cleanup() { throw Error("CUDA failure, no CPU fallback"); }
const char* explanation = R"note(physical boundary "outflow"
    fallback CPU)note";
""",
        })

    def test_diagnostic_stripping_keeps_actual_backend_fallback_and_rules(self):
        for source, expected in (
                ('// no CPU fallback\nvoid run() { cpu_fallback(); }', "hidden CUDA fallback"),
                ('void fill() { if (mode == "reflect") update(); }', "boundary-rule duplication"),
                ('void fill() { if ("periodic" != mode) update(); }', "boundary-rule duplication"),
                ('// physical boundary error\nvoid fill() { if (boundary_type == outflow) update(); }',
                 "boundary-rule duplication")):
            with self.subTest(source=source):
                self.assert_rejected_with({"src/cuda/transport.cu": source}, expected)

    def test_rejects_device_formula_copy(self):
        self.assert_rejected("src/cuda/diffusion_device.cuh", "double vie = iec * zbar;")

    def test_accepts_reviewed_cuda_infrastructure_owner_names(self):
        self.assert_accepted({
            "src/cuda/runtime/control/CudaBackendCore.cpp":
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

    def test_cmake_modules_are_audited_for_flags_and_source_globs(self):
        for contents, expected in (
                ("target_compile_options(component PRIVATE --use_fast_math)", "shared mathematics forbids"),
                ("file(GLOB sources *.cu)", "source glob is forbidden"),
                ("target_sources(ARCH PRIVATE $<TARGET_OBJECTS:foreign>)", "canonical owner")):
            with self.subTest(contents=contents):
                self.assert_rejected_with({"cmake/Module.cmake": contents}, expected)
        self.assert_accepted({"cmake/Module.cmake":
            '# --use_fast_math is forbidden\n#[=[ do not use -Ofast ]=]\nset(label "#metadata")'})

    def test_rejects_cross_module_duplicate_cuda_compilation(self):
        self.assert_rejected_with({
            "CMakeLists.txt": "add_library(one STATIC src/cuda/transport.cu)",
            "cmake/Duplicate.cmake":
                'add_library(two STATIC "${CMAKE_CURRENT_SOURCE_DIR}/src/cuda/transport.cu")',
        }, "multiple target owners")

    def test_helper_and_direct_compilation_share_global_ownership(self):
        self.assert_rejected_with({
            "CMakeLists.txt": self.object_helpers + """
arch_configure_cuda_backend_object(arch_cuda_backend_amr_migration
    src/cuda/amr/RegridMigration.cu)
""",
            "cmake/Duplicate.cmake": "add_executable(copy src/cuda/amr/RegridMigration.cu)",
        }, "multiple target owners")

    def test_duplicate_helper_compilation_is_not_object_reuse(self):
        registration = """
arch_configure_cuda_backend_object(arch_cuda_backend_amr_migration
    src/cuda/amr/RegridMigration.cu)
"""
        self.assert_rejected_with({
            "CMakeLists.txt": self.object_helpers + registration,
            "cmake/Duplicate.cmake": registration,
        }, "only once per target")

    def test_accepts_exact_focused_production_object_reuse(self):
        self.assert_accepted({"CMakeLists.txt": self.object_helpers + """
arch_configure_cuda_backend_object(arch_cuda_backend_amr_migration
    src/cuda/amr/RegridMigration.cu)
arch_configure_cuda_backend_object(arch_cuda_backend_amr_indicators
    src/cuda/amr/RefinementIndicators.cu)
add_library(arch_cuda_backend STATIC
    $<TARGET_OBJECTS:arch_cuda_backend_amr_migration>
    $<TARGET_OBJECTS:arch_cuda_backend_amr_indicators>)
add_executable(arch_cuda_regrid_migration tests/cuda/test_cuda_regrid_migration.cu
    $<TARGET_OBJECTS:arch_cuda_backend_amr_migration>)
add_executable(arch_cuda_refinement_indicators tests/cuda/test_refinement_indicators.cpp
    $<TARGET_OBJECTS:arch_cuda_backend_amr_indicators>)
"""})

    def test_rejects_object_reuse_by_unrelated_executable_or_test_source(self):
        valid = """add_executable(arch_cuda_regrid_migration
    tests/cuda/test_cuda_regrid_migration.cu
    $<TARGET_OBJECTS:arch_cuda_backend_amr_migration>)"""
        for invalid in (
                valid.replace("add_executable(arch_cuda_regrid_migration", "add_executable(other"),
                valid.replace("tests/cuda/test_cuda_regrid_migration.cu", "src/main.cpp"),
                valid.replace("arch_cuda_backend_amr_migration", "arch_cuda_backend_amr_indicators"),
                valid.replace("add_executable", "add_library"),
                "target_link_libraries(other PRIVATE arch_cuda_backend_amr_migration)"):
            with self.subTest(invalid=invalid):
                self.assert_rejected_with({"CMakeLists.txt": invalid}, "canonical owner")

    def test_accepts_exact_grid_metrics_production_object_reuse(self):
        self.assert_accepted({"CMakeLists.txt": self.object_helpers + """
arch_configure_cuda_backend_object(arch_cuda_backend_grid_metrics
    src/cuda/common/GridMetricsCache.cu)
add_library(arch_cuda_backend STATIC $<TARGET_OBJECTS:arch_cuda_backend_grid_metrics>)
add_executable(arch_cuda_grid_metrics_cache tests/cuda/test_grid_metrics_cache.cu
    $<TARGET_OBJECTS:arch_cuda_backend_grid_metrics>)
"""})

    def test_grid_metrics_object_requires_exact_source_owner_and_consumer(self):
        registration = """arch_configure_cuda_backend_object(arch_cuda_backend_grid_metrics
    src/cuda/common/GridMetricsCache.cu)
"""
        consumer = """add_executable(arch_cuda_grid_metrics_cache
    tests/cuda/test_grid_metrics_cache.cu
    $<TARGET_OBJECTS:arch_cuda_backend_grid_metrics>)"""
        for invalid in (
                registration.replace("common/GridMetricsCache.cu", "common/OtherCache.cu") + consumer,
                registration.replace("arch_cuda_backend_grid_metrics", "other_metrics") + consumer,
                registration + consumer.replace("add_executable(arch_cuda_grid_metrics_cache", "add_executable(other"),
                registration + consumer.replace("tests/cuda/test_grid_metrics_cache.cu", "src/main.cpp"),
                registration + consumer.replace("arch_cuda_backend_grid_metrics", "arch_cuda_backend_amr_migration"),
                registration + consumer.replace("add_executable", "add_library"),
                registration + consumer.replace("tests/cuda/test_grid_metrics_cache.cu", "tests/cuda/test_grid_metrics_cache.cu extra.cpp"),
                registration + "target_link_libraries(other PRIVATE arch_cuda_backend_grid_metrics)"):
            with self.subTest(invalid=invalid):
                self.assert_rejected_with({"CMakeLists.txt": self.object_helpers + invalid},
                                          "canonical owner")

    def test_grid_metrics_test_cannot_recompile_the_production_source(self):
        self.assert_rejected_with({
            "CMakeLists.txt": self.object_helpers + """
arch_configure_cuda_backend_object(arch_cuda_backend_grid_metrics
    src/cuda/common/GridMetricsCache.cu)
""",
            "cmake/Tests.cmake": """add_executable(arch_cuda_grid_metrics_cache
    tests/cuda/test_grid_metrics_cache.cu src/cuda/common/GridMetricsCache.cu)""",
        }, "multiple target owners")

    def test_accepts_exact_burn_resource_object_links(self):
        for name, extra in (("policy", " src/core/FileFingerprint.cpp"),
                            ("controller", "")):
            target = f"arch_cuda_burn_{name}_parity"
            source = f"tests/cuda/test_burn_{name}_parity.cu"
            definition = f"add_executable({target} {source}{extra})"
            linkage = f"""target_link_libraries({target} PRIVATE
                arch_cuda_backend_eos_helm arch_cuda_backend_eos_species
                arch_cuda_backend_eos_utils arch_build_contract CUDA::cudart)"""
            with self.subTest(target=target):
                self.assert_accepted({"CMakeLists.txt": definition,
                                      "cmake/Tests.cmake": linkage})
            for invalid_definition, invalid_linkage in (
                    (definition.replace(source, "src/main.cpp"), linkage),
                    (definition.replace(source, source + " extra.cpp"), linkage),
                    (definition, linkage.replace("arch_cuda_backend_eos_helm", "arch_cuda_backend_core")),
                    (definition, linkage.replace(target, "other"))):
                with self.subTest(linkage=invalid_linkage, definition=invalid_definition):
                    self.assert_rejected_with({"CMakeLists.txt": invalid_definition,
                                               "cmake/Tests.cmake": invalid_linkage}, "canonical owner")

    def test_accepts_module_host_helper_with_root_definition(self):
        self.assert_accepted({
            "CMakeLists.txt": self.object_helpers,
            "cmake/Hosts.cmake": """
arch_configure_cuda_host_object(arch_cuda_backend_sparse_factory
    "${CMAKE_CURRENT_SOURCE_DIR}/src/cuda/runtime/burn/CudaBackendBurnSparseFactory.cpp" -g1)
arch_configure_cuda_host_object(arch_cuda_backend_migration
    src/cuda/runtime/amr/CudaBackendMigration.cpp -g1)
arch_configure_cuda_host_object(arch_cuda_backend_indicators
    src/cuda/runtime/amr/CudaBackendIndicators.cpp -g1)
target_sources(arch_cuda_backend PRIVATE $<TARGET_OBJECTS:arch_cuda_backend_sparse_factory>
    $<TARGET_OBJECTS:arch_cuda_backend_migration> $<TARGET_OBJECTS:arch_cuda_backend_indicators>)
""",
        })

    def test_rejects_cross_module_helper_source_swap(self):
        self.assert_rejected_with({
            "CMakeLists.txt": self.object_helpers,
            "cmake/Hosts.cmake": """
arch_configure_cuda_host_object(arch_cuda_backend_sparse_factory
    "${CMAKE_CURRENT_SOURCE_DIR}/src/cuda/runtime/amr/CudaBackendMigration.cpp" -g1)
""",
        }, "Host OBJECT libraries must keep their canonical owners")

    def test_accepts_generated_dense_sparse_custom_backend_delegates(self):
        for kind in ("dense", "sparse", "custom"):
            with self.subTest(kind=kind):
                self.assert_accepted(self.generated_route_files(kind))

    def test_dense_delegates_cannot_revert_to_species_only_cutoff(self):
        for kind in ("dense", "custom"):
            files = self.generated_route_files(kind)
            for path in tuple(files):
                if path.endswith('.cu.in'):
                    files[path] = files[path].replace(
                        'BurnLimits::uses_compact_matrix(Network::ODE_NEQ)',
                        'Network::NUM_SPECIES <= BurnLimits::MAX_SPECIES')
            self.assert_rejected_with(files, 'shared-implementation delegates')

    def test_generated_routes_require_matching_template_source_and_target(self):
        for kind in ("dense", "sparse", "custom"):
            for before, after in (
                    ("set(route_target \"arch_cuda", "set(route_target \"other_cuda"),
                    (".cu\")", "_copy.cu\")"),
                    ("target_sources(arch_cuda_backend", "target_sources(ARCH"),
                    ("${route_source}\" @ONLY", "${unrelated}\" @ONLY"),
                    ("@ONLY", "COPYONLY")):
                with self.subTest(kind=kind, before=before):
                    files = self.generated_route_files(kind)
                    files["cmake/Routes.cmake"] = files["cmake/Routes.cmake"].replace(before, after)
                    self.assert_rejected_with(files, "generated CUDA binding")

    def test_generated_route_reassignment_and_extra_compilation_fail_closed(self):
        for extra in (
                'set(route_target "other")',
                'set(route_source "${route_dir}/second.cu")',
                'add_executable(copy "${route_source}")',
                'target_sources(arch_cuda_backend PRIVATE "${route_source}")',
                'arch_configure_cuda_backend_object(${route_target} "${route_source}")',
                'target_sources(other PRIVATE $<TARGET_OBJECTS:${route_target}>)'):
            with self.subTest(extra=extra):
                files = self.generated_route_files()
                files["cmake/Routes.cmake"] += extra
                self.assert_rejected_with(files, "generated CUDA binding")

    def test_generated_route_cannot_be_linked_into_second_owner(self):
        for argument in ("${route_target}", "$<TARGET_OBJECTS:${route_target}>"):
            with self.subTest(argument=argument):
                files = self.generated_route_files()
                files["cmake/Routes.cmake"] += f"target_link_libraries(other PRIVATE {argument})"
                self.assert_rejected_with(files, "canonical owner")

    def test_generated_routes_require_reviewed_existing_template(self):
        files = self.generated_route_files()
        del files["cmake/templates/CudaBurnDenseRoute.cu.in"]
        self.assert_rejected_with(files, "generated CUDA binding")
        files = self.generated_route_files()
        files["cmake/Routes.cmake"] = files["cmake/Routes.cmake"].replace(
            "cmake/templates/CudaBurnDenseRoute.cu.in", "cmake/templates/Other.cu.in")
        self.assert_rejected_with(files, "generated CUDA binding")

    def test_generated_templates_cannot_copy_numerics_or_skip_shared_route(self):
        for kind in ("dense", "sparse", "custom"):
            original = self.generated_route_files(kind)
            template = next(path for path in original if path.endswith(".cu.in"))
            for mutation in (
                    original[template] + "\ndouble own_rhs(double y) { return y * y; }\n",
                    original[template] + "\nconst double own_energy = 3.0 * 2.0;\n",
                    original[template].replace("return ", "return alternate_", 1)):
                with self.subTest(kind=kind, mutation=mutation[-100:]):
                    files = dict(original)
                    files[template] = mutation
                    self.assert_rejected_with(files, "templates must remain shared-implementation delegates")

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
            "retained_block": "src/cuda/runtime/control/CudaBackendStore.cpp",
            "retained_pool_index": "src/cuda/runtime/control/CudaBackendStore.cpp",
            "upload_current_fluid_state":
                "src/cuda/runtime/control/CudaBackendStore.cpp",
            "allocate_and_initialize":
                "src/cuda/runtime/control/CudaBackendResources.cpp",
            "leaked_helm_owner":
                "src/cuda/microphysics/helm_eos_device_owner.cpp",
            "recomputed_boundary_sources":
                "src/cuda/runtime/control/CudaBackendResources.cpp",
            "get_rkl_coeffs_drifted":
                "src/cuda/runtime/control/CudaBackendMicrophysicsControl.cpp",
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
            "src/cuda/runtime/hydro/CudaBackendHydroControl.cpp":
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
                "src/cuda/runtime/burn/routes/CudaBackendBurnIdeal.cu)\n"
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
                "src/cuda/runtime/burn/routes/CudaBackendBurnIdeal.cu)\n"
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
                "arch_cuda_backend_hydro_ideal "
                "src/cuda/runtime/hydro/CudaBackendHydroIdeal.cu)\n"
                "arch_configure_cuda_backend_object("
                "arch_cuda_backend_hydro_helm "
                "src/cuda/runtime/hydro/CudaBackendHydroHelm.cu)\n"
                "arch_configure_cuda_backend_object("
                "arch_cuda_backend_hydro_tabular3 "
                "src/cuda/runtime/hydro/CudaBackendHydroTabular3.cu)\n"
                "arch_configure_cuda_backend_object("
                "arch_cuda_backend_hydro_tabular4 "
                "src/cuda/runtime/hydro/CudaBackendHydroTabular4.cu)\n"
                "arch_configure_cuda_backend_object("
                "arch_cuda_backend_diffusion "
                "src/cuda/runtime/diffusion/CudaBackendDiffusion.cu)\n"
                "arch_configure_cuda_backend_object("
                "arch_cuda_backend_exchange "
                "src/cuda/runtime/amr/CudaBackendExchange.cu)\n"
                "arch_configure_cuda_backend_object("
                "arch_cuda_backend_amr_flux "
                "src/cuda/runtime/amr/CudaBackendAmrFlux.cu)\n"
                "arch_configure_cuda_backend_object("
                "arch_cuda_backend_burn_tabular3d_aprox13 "
                "src/cuda/runtime/burn/routes/CudaBackendBurnTabular3DAprox13.cu)\n"
                "arch_configure_cuda_backend_object("
                "arch_cuda_backend_burn_tabular4d_iso7 "
                "src/cuda/runtime/burn/routes/CudaBackendBurnTabular4DIso7.cu)\n"
                "add_library(arch_cuda_backend STATIC runtime.cu "
                "$<TARGET_OBJECTS:arch_cuda_backend_hydro_ideal> "
                "$<TARGET_OBJECTS:arch_cuda_backend_hydro_helm> "
                "$<TARGET_OBJECTS:arch_cuda_backend_hydro_tabular3> "
                "$<TARGET_OBJECTS:arch_cuda_backend_hydro_tabular4> "
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
                "src/cuda/runtime/control/CudaBackendCore.cpp -g1)\n"
                "arch_configure_cuda_host_object("
                "arch_cuda_backend_hydro_control "
                "src/cuda/runtime/hydro/CudaBackendHydroControl.cpp -g1)\n"
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
            "arch_cuda_backend_hydro_ideal "
            "src/cuda/runtime/diffusion/CudaBackendDiffusion.cu)\n")

    def test_rejects_swapped_canonical_host_object_source(self):
        self.assert_rejected_with({
            "CMakeLists.txt":
                "function(arch_configure_cuda_host_object "
                "target source debug_level)\n"
                "  add_library(${target} OBJECT ${source})\n"
                "endfunction()\n"
                "arch_configure_cuda_host_object("
                "arch_cuda_backend_core "
                "src/cuda/runtime/control/CudaBackendResources.cpp -g1)\n"
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
                "src/cuda/runtime/control/CudaBackendCore.cpp -g0)\n"
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
            "src/cuda/runtime/control/CudaBackendMicrophysicsControl.cpp":
                "void CudaBackend::execute_burn() {\n"
                "  launch_cuda_burn_route(nullptr, block.burn_candidates.get(), "
                "block.burn_statuses.get(), block.burn_summary.get());\n"
                "  quiesce();\n"
                "  impl_->runtime_counters.kernel_count += 2;\n"
                "}\n"
        }, "CUDA burn routes must consume the caller workspace")

    def test_accepts_split_runtime_completion_contracts(self):
        self.assert_accepted({
            "src/cuda/runtime/hydro/CudaBackendHydroControl.cpp":
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
            "src/cuda/runtime/control/CudaBackendMicrophysicsControl.cpp":
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
            "compute_hydro_dt_batch": (
                "src/cuda/runtime/hydro/CudaBackendHydroControl.cpp",
                "launch_cuda_backend_hydro_dt();"),
            "execute_hydro_stage_batch": (
                "src/cuda/runtime/hydro/CudaBackendHydroControl.cpp",
                "launch_cuda_backend_hydro_stage();"),
            "compute_hydro_dt": (
                "src/cuda/runtime/hydro/CudaBackendHydroControl.cpp",
                "launch_cuda_backend_hydro_dt();"),
            "execute_hydro_stage": (
                "src/cuda/runtime/hydro/CudaBackendHydroControl.cpp",
                "launch_cuda_backend_hydro_stage();"),
            "compute_diffusion_dt": (
                "src/cuda/runtime/control/CudaBackendMicrophysicsControl.cpp",
                "launch_cuda_backend_diffusion_dt();"),
            "execute_diffusion_stage": (
                "src/cuda/runtime/control/CudaBackendMicrophysicsControl.cpp",
                "launch_cuda_backend_diffusion_stage();"),
            "execute_burn": (
                "src/cuda/runtime/control/CudaBackendMicrophysicsControl.cpp",
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

    def test_accepts_exact_scalar_batch_delegates(self):
        self.assert_accepted({
            "src/cuda/runtime/hydro/CudaBackendHydroControl.cpp":
                "double CudaBackend::compute_hydro_dt() {\n"
                "return compute_hydro_dt_batch({&current, 1}, cfl).front(); }\n"
                "auto CudaBackend::compute_hydro_dt_batch() {\n"
                "launch_cuda_backend_hydro_dt(); quiesce();\n"
                "impl_->runtime_counters.kernel_count += 2; }\n"
                "auto CudaBackend::execute_hydro_stage() {\n"
                "return execute_hydro_stage_batch({&current, 1}, descriptor, dt, expected); }\n"
                "auto CudaBackend::execute_hydro_stage_batch() {\n"
                "launch_cuda_backend_hydro_stage(); quiesce();\n"
                "impl_->runtime_counters.kernel_count += 1; }\n",
        })

    def test_rejects_scalar_delegate_without_batch_owner(self):
        self.assert_rejected_with({
            "src/cuda/runtime/hydro/CudaBackendHydroControl.cpp":
                "double CudaBackend::compute_hydro_dt() {\n"
                "return compute_hydro_dt_batch({&current, 1}, cfl).front(); }\n",
        }, "CUDA scalar delegate requires its batch owner")

    def test_rejects_enqueue_hidden_before_scalar_delegate(self):
        self.assert_rejected_with({
            "src/cuda/runtime/hydro/CudaBackendHydroControl.cpp":
                "double CudaBackend::compute_hydro_dt() {\n"
                "launch_cuda_backend_hydro_dt();\n"
                "return compute_hydro_dt_batch({&current, 1}, cfl).front(); }\n",
        }, "CUDA bounded work must quiesce before completion")

    def test_rejects_boundary_quiescence_from_another_function(self):
        self.assert_rejected_with({
            "src/cuda/runtime/hydro/CudaBackendHydroControl.cpp":
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
            "src/cuda/runtime/control/CudaBackendMicrophysicsControl.cpp",
            "execute_burn_policy_cell_without_failed_nse_continuation();")

    def test_rejects_cuda_burn_limiter_using_full_host_state(self):
        self.assert_rejected(
            "src/driver/Driver.h",
            "DriverBurn::combine_full_host_state_minimum();")

    def test_repository_tree_passes_audit(self):
        self.assertEqual(audit_tree(ROOT), [])


if __name__ == "__main__":
    unittest.main()
