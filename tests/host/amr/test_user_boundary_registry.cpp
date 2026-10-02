/**
 * @file test_user_boundary_registry.cpp
 * @brief Verify case boundary registration, resolution and selection binding.
 *
 * Workflow: register callbacks through the public macros and the registry API,
 * then resolve valid and invalid case sources and bind one selection with RAII
 * restore. No test callback may run during registration or resolution.
 *
 * Real cases expand the registration macros inside physical_boundary.cpp and
 * gravity_boundary.cpp; this test lives in tests/host/amr, so its macro-driven
 * entries are checked through the same-directory filename diagnostic while the
 * valid-resolution cases below use explicit same-directory source paths. The
 * digest stands in for the CMake-supplied ARCH_BOUNDARY_SOURCE_SHA256 and must
 * be 64 lowercase hexadecimal characters.
 */
#define ARCH_BOUNDARY_SOURCE_SHA256 "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789"

#include "core/config/UserInterface.h"
#include "core/problem/ProblemRegistry.h"
#include "data/GlobalDefs.h"
#include "physics/boundary/UserBoundary.h"

#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace
{
using namespace arch::boundary;

/** Valid 64-character lowercase hexadecimal digests for the resolution tests. */
constexpr const char *kWrongNameDigest =
    "1111111111111111111111111111111111111111111111111111111111111111";
constexpr const char *kWrongDirDigest =
    "2222222222222222222222222222222222222222222222222222222222222222";
constexpr const char *kDuplicateDigestA =
    "3333333333333333333333333333333333333333333333333333333333333333";
constexpr const char *kDuplicateDigestB =
    "4444444444444444444444444444444444444444444444444444444444444444";
constexpr const char *kGravityOnlyDigest =
    "5555555555555555555555555555555555555555555555555555555555555555";
constexpr const char *kPhysicalOnlyDigest =
    "6666666666666666666666666666666666666666666666666666666666666666";
constexpr const char *kClassPhysicalDigest =
    "7777777777777777777777777777777777777777777777777777777777777777";
constexpr const char *kClassGravityDigest =
    "8888888888888888888888888888888888888888888888888888888888888888";

int physical_call_count = 0;
int gravity_call_count = 0;
int physical_class_call_count = 0;
int gravity_class_call_count = 0;

/** Free function used by both direct registration and the macros. */
PhysicalBoundaryData CountingPhysicalCallback(const PhysicalBoundaryContext &)
{
    ++physical_call_count;
    return PhysicalBoundaryData{};
}

GravityBoundaryData CountingGravityCallback(const GravityBoundaryContext &)
{
    ++gravity_call_count;
    return GravityBoundaryData{};
}

/** Const callable classes accepted by the same strongly typed signatures. */
class CountingPhysicalClass
{
public:
    PhysicalBoundaryData operator()(const PhysicalBoundaryContext &) const
    {
        ++physical_class_call_count;
        return PhysicalBoundaryData{};
    }
};

class CountingGravityClass
{
public:
    GravityBoundaryData operator()(const GravityBoundaryContext &) const
    {
        ++gravity_class_call_count;
        return GravityBoundaryData{};
    }
};

/** Minimal case used only to observe ProblemRegistry identity binding. */
class StubProblemGenerator : public ProblemGenerator
{
public:
    void Setup(SimConfig &, SpeciesManager &) override {}
    void InitializeData(amr::AMRControl &, const SimConfig &, const SpeciesManager &,
                        ProblemInitializationContext) override
    {
    }
};

void require(bool condition, std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string(message));
}

std::string require_rejected(const std::function<void()> &action, std::string_view message)
{
    try
    {
        action();
    }
    catch (const std::exception &error)
    {
        return error.what();
    }
    throw std::runtime_error("expected rejection: " + std::string(message));
}

void require_contains(const std::string &text, std::string_view fragment, std::string_view message)
{
    if (text.find(fragment) == std::string::npos)
        throw std::runtime_error(std::string(message) + " (diagnostic: " + text + ")");
}

void require_absent(const std::string &text, std::string_view fragment, std::string_view message)
{
    if (text.find(fragment) != std::string::npos)
        throw std::runtime_error(std::string(message) + " (diagnostic: " + text + ")");
}
} // namespace

// Macro registrations live at file scope: they publish callbacks during static
// initialization and must never run the callback itself. Two live registrations
// for one name must defer the duplicate diagnostic to Resolve.
REGISTER_PHYSICAL_BOUNDARY("MacroPhysicalCase", CountingPhysicalCallback)
REGISTER_GRAVITY_BOUNDARY("MacroGravityCase", CountingGravityCallback)
REGISTER_PHYSICAL_BOUNDARY_CLASS("MacroPhysicalClassCase", CountingPhysicalClass)
REGISTER_GRAVITY_BOUNDARY_CLASS("MacroGravityClassCase", CountingGravityClass)
REGISTER_PHYSICAL_BOUNDARY("MacroDuplicateCase", CountingPhysicalCallback)
REGISTER_PHYSICAL_BOUNDARY("MacroDuplicateCase", CountingPhysicalCallback)

void test_user_boundary_registry()
{
    static_assert(std::is_same_v<
                  std::invoke_result_t<const PhysicalBoundaryFunction &,
                                       const PhysicalBoundaryContext &>,
                  PhysicalBoundaryData>);
    static_assert(std::is_same_v<
                  std::invoke_result_t<const GravityBoundaryFunction &,
                                       const GravityBoundaryContext &>,
                  GravityBoundaryData>);

    BoundaryRegistry &registry = BoundaryRegistry::Get();
    const std::string test_file = __FILE__;

    {
        // 1. Missing registration reports the expected same-directory file.
        const std::string missing_physical = require_rejected(
            [&] {
                registry.Resolve("AbsentCase", "simulation/AbsentCase/AbsentCase.cpp", true, false);
            },
            "unregistered physical callback must be rejected");
        require_contains(missing_physical, "physical_boundary.cpp",
                         "missing callback names the sibling file");
        require_contains(missing_physical, "AbsentCase", "missing callback names the case");
        require_contains(missing_physical, "field name", "missing callback names the offending field");

        const std::string missing_gravity = require_rejected(
            [&] {
                registry.Resolve("AbsentCase", "simulation/AbsentCase/AbsentCase.cpp", false, true);
            },
            "unregistered gravity callback must be rejected");
        require_contains(missing_gravity, "gravity_boundary.cpp",
                         "missing gravity callback names the sibling file");

        const std::string empty_case = require_rejected(
            [&] { registry.Resolve("AbsentCase", "", false, false); },
            "empty case source path must be rejected");
        require_contains(empty_case, "case_source_file", "empty path names the offending field");
    }

    {
        // 2. Macro registrations carry __FILE__ and the CMake digest; because
        //    this test file is not physical_boundary.cpp they are rejected by
        //    the same-directory filename rule instead of the missing/digest one.
        const std::string macro_physical = require_rejected(
            [&] { registry.Resolve("MacroPhysicalCase", test_file, true, false); },
            "macro physical callback must be registered");
        require_contains(macro_physical, "source_file", "macro diagnostic names the field");
        require_contains(macro_physical, "physical_boundary.cpp", "macro diagnostic names the file");
        require_contains(macro_physical, "test_user_boundary_registry.cpp",
                         "macro recorded the compiling __FILE__");
        require_absent(macro_physical, "source_sha256",
                       "macro supplied ARCH_BOUNDARY_SOURCE_SHA256, not an empty digest");

        const std::string macro_gravity = require_rejected(
            [&] { registry.Resolve("MacroGravityCase", test_file, false, true); },
            "macro gravity callback must be registered");
        require_contains(macro_gravity, "gravity_boundary.cpp", "macro gravity file rule applies");
        require_absent(macro_gravity, "source_sha256", "macro gravity digest was supplied");

        const std::string macro_physical_class = require_rejected(
            [&] { registry.Resolve("MacroPhysicalClassCase", test_file, true, false); },
            "macro class callback must be registered");
        require_contains(macro_physical_class, "physical_boundary.cpp",
                         "class macro registers through the physical registry");
        require_absent(macro_physical_class, "source_sha256", "class macro digest was supplied");

        const std::string macro_gravity_class = require_rejected(
            [&] { registry.Resolve("MacroGravityClassCase", test_file, false, true); },
            "macro class gravity callback must be registered");
        require_contains(macro_gravity_class, "gravity_boundary.cpp",
                         "class macro registers through the gravity registry");
        require_absent(macro_gravity_class, "source_sha256", "class macro gravity digest was supplied");
    }

    {
        // 3. Wrong file name and wrong directory are distinct diagnostics.
        registry.RegisterPhysical("WrongNameCase", CountingPhysicalCallback,
                                  "simulation/WrongNameCase/hydro.cpp", kWrongNameDigest);
        const std::string wrong_name = require_rejected(
            [&] {
                registry.Resolve("WrongNameCase", "simulation/WrongNameCase/WrongNameCase.cpp", true,
                                 false);
            },
            "sibling file with a different name must be rejected");
        require_contains(wrong_name, "physical_boundary.cpp", "names the required file name");
        require_contains(wrong_name, "source_file", "names the offending field");
        require_contains(wrong_name, "WrongNameCase", "names the case directory");

        registry.RegisterGravity("WrongDirCase", CountingGravityCallback,
                                 "simulation/OtherCase/gravity_boundary.cpp", kWrongDirDigest);
        const std::string wrong_dir = require_rejected(
            [&] {
                registry.Resolve("WrongDirCase", "simulation/WrongDirCase/WrongDirCase.cpp", false,
                                 true);
            },
            "sibling file in another directory must be rejected");
        require_contains(wrong_dir, "simulation/OtherCase", "names the registered directory");
        require_contains(wrong_dir, "simulation/WrongDirCase", "names the case directory");
    }

    {
        // 4. Missing digest is rejected instead of falling back silently.
        registry.RegisterPhysical("NoDigestCase", CountingPhysicalCallback,
                                  "simulation/NoDigestCase/physical_boundary.cpp", "");
        const std::string no_digest = require_rejected(
            [&] {
                registry.Resolve("NoDigestCase", "simulation/NoDigestCase/NoDigestCase.cpp", true,
                                 false);
            },
            "empty digest must be rejected");
        require_contains(no_digest, "source_sha256", "names the offending field");
        require_contains(no_digest, "ARCH_BOUNDARY_SOURCE_SHA256", "names the build input");
    }

    {
        // 4b. A non-empty digest must be exactly 64 lowercase hexadecimal
        //     characters; short, uppercase or non-hex digests never resolve,
        //     and a well-formed 64-character digest does.
        registry.RegisterPhysical("ShortDigestCase", CountingPhysicalCallback,
                                  "simulation/ShortDigestCase/physical_boundary.cpp",
                                  "0123456789abcdef");
        const std::string short_digest = require_rejected(
            [&] {
                registry.Resolve("ShortDigestCase", "simulation/ShortDigestCase/ShortDigestCase.cpp",
                                 true, false);
            },
            "short digest must be rejected");
        require_contains(short_digest, "source_sha256", "short digest names the field");
        require_contains(short_digest, "ShortDigestCase", "short digest names the case");
        require_contains(short_digest, "64 lowercase hexadecimal", "short digest names the rule");
        require_absent(short_digest, "field source_file must name",
                       "short digest is not reported as a filename mismatch");

        registry.RegisterPhysical("UppercaseDigestCase", CountingPhysicalCallback,
                                  "simulation/UppercaseDigestCase/physical_boundary.cpp",
                                  "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF");
        const std::string uppercase_digest = require_rejected(
            [&] {
                registry.Resolve("UppercaseDigestCase",
                                 "simulation/UppercaseDigestCase/UppercaseDigestCase.cpp", true, false);
            },
            "uppercase digest must be rejected");
        require_contains(uppercase_digest, "source_sha256", "uppercase digest names the field");
        require_contains(uppercase_digest, "64 lowercase hexadecimal",
                         "uppercase digest names the rule");

        registry.RegisterGravity("NonHexDigestCase", CountingGravityCallback,
                                 "simulation/NonHexDigestCase/gravity_boundary.cpp",
                                 "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdeg");
        const std::string non_hex_digest = require_rejected(
            [&] {
                registry.Resolve("NonHexDigestCase",
                                 "simulation/NonHexDigestCase/NonHexDigestCase.cpp", false, true);
            },
            "non-hex digest must be rejected");
        require_contains(non_hex_digest, "source_sha256", "non-hex digest names the field");
        require_contains(non_hex_digest, "64 lowercase hexadecimal", "non-hex digest names the rule");

        registry.RegisterGravity("WellFormedDigestCase", CountingGravityCallback,
                                 "simulation/WellFormedDigestCase/gravity_boundary.cpp",
                                 kGravityOnlyDigest);
        const ResolvedUserBoundaries well_formed = registry.Resolve(
            "WellFormedDigestCase", "simulation/WellFormedDigestCase/WellFormedDigestCase.cpp", false,
            true);
        require(static_cast<bool>(well_formed.gravity),
                "a well-formed 64-character lowercase digest must resolve");
        require_contains(well_formed.identity, kGravityOnlyDigest,
                         "identity carries the well-formed digest");
    }

    {
        // 5. Duplicate registration is deferred to Resolve, never terminating
        //    static initialization, and is reported deterministically.
        const std::string duplicate = require_rejected(
            [&] { registry.Resolve("MacroDuplicateCase", test_file, true, false); },
            "duplicate registration must be rejected at resolution");
        require_contains(duplicate, "duplicate", "reports a duplicate registration");
        require_contains(duplicate, "field source_file", "names the ambiguous field");
        require_contains(duplicate, "test_user_boundary_registry.cpp", "names the sources");

        const std::string duplicate_repeat = require_rejected(
            [&] { registry.Resolve("MacroDuplicateCase", test_file, true, false); },
            "duplicate diagnostic must be stable");
        require(duplicate_repeat == duplicate, "duplicate diagnostic must be deterministic");

        registry.RegisterGravity("DuplicateGravityCase", CountingGravityCallback,
                                 "simulation/DuplicateGravityCase/gravity_boundary.cpp",
                                 kDuplicateDigestA);
        registry.RegisterGravity("DuplicateGravityCase", CountingGravityCallback,
                                 "simulation/DuplicateGravityCase/gravity_boundary.cpp",
                                 kDuplicateDigestB);
        const std::string duplicate_gravity = require_rejected(
            [&] {
                registry.Resolve("DuplicateGravityCase",
                                 "simulation/DuplicateGravityCase/DuplicateGravityCase.cpp", false,
                                 true);
            },
            "duplicate gravity registration must be rejected at resolution");
        require_contains(duplicate_gravity, "duplicate", "reports a duplicate gravity registration");
    }

    {
        // 6. Physical and gravity registries stay independent per name, and a
        //    const callable class registers through the same typed signature.
        registry.RegisterGravity("GravityOnlyCase", CountingGravityCallback,
                                 "simulation/GravityOnlyCase/gravity_boundary.cpp",
                                 kGravityOnlyDigest);
        registry.RegisterPhysical("PhysicalOnlyCase", CountingPhysicalCallback,
                                  "simulation/PhysicalOnlyCase/physical_boundary.cpp",
                                  kPhysicalOnlyDigest);
        registry.RegisterPhysical("ClassPhysicalCase", CountingPhysicalClass{},
                                  "simulation/ClassPhysicalCase/physical_boundary.cpp",
                                  kClassPhysicalDigest);
        registry.RegisterGravity("ClassGravityCase", CountingGravityClass{},
                                 "simulation/ClassGravityCase/gravity_boundary.cpp",
                                 kClassGravityDigest);

        const ResolvedUserBoundaries gravity_only = registry.Resolve(
            "GravityOnlyCase", "simulation/GravityOnlyCase/GravityOnlyCase.cpp", false, true);
        require(static_cast<bool>(gravity_only.gravity),
                "gravity-only registration resolves a gravity callback");

        const std::string physical_missing = require_rejected(
            [&] {
                registry.Resolve("GravityOnlyCase", "simulation/GravityOnlyCase/GravityOnlyCase.cpp",
                                 true, true);
            },
            "gravity registration must not satisfy a physical request");
        require_contains(physical_missing, "physical_boundary.cpp",
                         "reports the missing physical sibling");

        const std::string gravity_missing = require_rejected(
            [&] {
                registry.Resolve("PhysicalOnlyCase",
                                 "simulation/PhysicalOnlyCase/PhysicalOnlyCase.cpp", false, true);
            },
            "physical registration must not satisfy a gravity request");
        require_contains(gravity_missing, "gravity_boundary.cpp",
                         "reports the missing gravity sibling");

        const ResolvedUserBoundaries class_physical = registry.Resolve(
            "ClassPhysicalCase", "simulation/ClassPhysicalCase/ClassPhysicalCase.cpp", true, false);
        require(static_cast<bool>(class_physical.physical), "callable class resolves as a callback");
    }

    {
        // 7. Lexically normalized paths resolve and the identity stays portable.
        const ResolvedUserBoundaries normalized = registry.Resolve(
            "PhysicalOnlyCase",
            "simulation/PhysicalOnlyCase/./../PhysicalOnlyCase/PhysicalOnlyCase.cpp", true, false);
        require(static_cast<bool>(normalized.physical), "normalized case path must resolve");
        require_contains(normalized.identity, "physical_boundary.cpp",
                         "identity names the boundary file");
        require_contains(normalized.identity, kPhysicalOnlyDigest, "identity carries the digest");
        require_absent(normalized.identity, "simulation/", "identity must not embed directories");
        require_absent(normalized.identity, "/home/", "identity must not embed host absolute paths");

        const ResolvedUserBoundaries repeated = registry.Resolve(
            "PhysicalOnlyCase", "simulation/PhysicalOnlyCase/PhysicalOnlyCase.cpp", true, false);
        require(repeated.identity == normalized.identity, "identity must be deterministic");
    }

    {
        // 8. Registration and Resolve never execute a callback; only the
        //    resolved copy runs one, for both function and class callbacks.
        physical_call_count = 0;
        gravity_call_count = 0;
        physical_class_call_count = 0;
        gravity_class_call_count = 0;

        const ResolvedUserBoundaries physical_case = registry.Resolve(
            "PhysicalOnlyCase", "simulation/PhysicalOnlyCase/PhysicalOnlyCase.cpp", true, false);
        const ResolvedUserBoundaries gravity_case = registry.Resolve(
            "GravityOnlyCase", "simulation/GravityOnlyCase/GravityOnlyCase.cpp", false, true);
        const ResolvedUserBoundaries physical_class_case = registry.Resolve(
            "ClassPhysicalCase", "simulation/ClassPhysicalCase/ClassPhysicalCase.cpp", true, false);
        const ResolvedUserBoundaries gravity_class_case = registry.Resolve(
            "ClassGravityCase", "simulation/ClassGravityCase/ClassGravityCase.cpp", false, true);
        require(physical_call_count == 0 && gravity_call_count == 0 &&
                    physical_class_call_count == 0 && gravity_class_call_count == 0,
                "no callback may run during registration or resolution");

        SimConfig config;
        SpeciesManager species;
        const PhysicalBoundaryData physical_data = physical_case.physical(
            PhysicalBoundaryContext(BoundaryCoordinates{}, config, species, PrimitiveData{}));
        const GravityBoundaryData gravity_data =
            gravity_case.gravity(GravityBoundaryContext(BoundaryCoordinates{}, config, species));
        const PhysicalBoundaryData physical_class_data = physical_class_case.physical(
            PhysicalBoundaryContext(BoundaryCoordinates{}, config, species, PrimitiveData{}));
        const GravityBoundaryData gravity_class_data = gravity_class_case.gravity(
            GravityBoundaryContext(BoundaryCoordinates{}, config, species));
        require(physical_data.hydro.has_value() == false, "callback returns its own data");
        require(gravity_data.kind == GravityBoundaryCondition::Dirichlet,
                "gravity callback returns its own data");
        require(physical_class_data.hydro.has_value() == false, "class callback returns its own data");
        require(gravity_class_data.kind == GravityBoundaryCondition::Dirichlet,
                "gravity class callback returns its own data");
        require(physical_call_count == 1 && gravity_call_count == 1 &&
                    physical_class_call_count == 1 && gravity_class_call_count == 1,
                "resolved callbacks must execute their registered target exactly once");
    }

    {
        // 9. Valid selection binding and RAII restore of the previous selection.
        require(CurrentUserBoundaries() == nullptr, "no selection is bound initially");
        SimConfig outer_config;
        SpeciesManager outer_species;
        const ResolvedUserBoundaries outer_resolved = registry.Resolve(
            "PhysicalOnlyCase", "simulation/PhysicalOnlyCase/PhysicalOnlyCase.cpp", true, false);
        {
            ScopedUserBoundarySelection outer(outer_resolved, outer_config, outer_species);
            require(CurrentUserBoundaries() == &outer.selection(), "scope publishes its selection");
            require(CurrentUserBoundaries()->config == &outer_config,
                    "scope binds the config reference");
            require(CurrentUserBoundaries()->species == &outer_species,
                    "scope binds the species reference");
            require(CurrentUserBoundaries()->callbacks.identity == outer_resolved.identity,
                    "published callbacks match the resolved set");

            SimConfig inner_config;
            SpeciesManager inner_species;
            {
                ScopedUserBoundarySelection inner(
                    registry.Resolve("GravityOnlyCase",
                                     "simulation/GravityOnlyCase/GravityOnlyCase.cpp", false, true),
                    inner_config, inner_species);
                require(CurrentUserBoundaries() == &inner.selection(),
                        "nested scope publishes its own selection");
                require(CurrentUserBoundaries()->config == &inner_config,
                        "nested scope binds its own config");
            }
            require(CurrentUserBoundaries() == &outer.selection(),
                    "destruction restores the previous selection");
        }
        require(CurrentUserBoundaries() == nullptr,
                "destruction restores the empty previous selection");
    }

    {
        // 10. ProblemRegistry::Create stamps the case identity on the instance.
        ProblemRegistry::Get().Register(
            "RegistryBindingCase",
            []() { return std::make_unique<StubProblemGenerator>(); },
            {"simulation/RegistryBindingCase/RegistryBindingCase.cpp", "case-digest", true});
        std::unique_ptr<ProblemGenerator> generator =
            ProblemRegistry::Get().Create("RegistryBindingCase");
        require(generator != nullptr, "registered case must create an instance");
        require(generator->RegisteredName() == "RegistryBindingCase", "identity carries the name");
        require(generator->SourceFile() == "simulation/RegistryBindingCase/RegistryBindingCase.cpp",
                "identity carries the case source file");
        require(generator->SourceSha256() == "case-digest", "identity carries the case digest");
        require(ProblemRegistry::Get().Create("RegistryBindingAbsentCase") == nullptr,
                "unknown case still creates nothing");
    }
}
