/**
 * @file UserBoundary.h
 * @brief Named case boundary callbacks and their per-simulation selection.
 *
 * Workflow:
 * 1. A case translation unit registers a named physical or gravity callback
 *    together with its source file and compiled source digest.
 * 2. Resolve() checks the registered source against the case source and its
 *    same-directory sibling file name using lexical path rules only.
 * 3. The driver owns one resolved selection per simulation through
 *    ScopedUserBoundarySelection and reads it via CurrentUserBoundaries().
 *
 * Resolution never performs filesystem IO, executes a callback, or falls back
 * to an unregistered boundary: every mismatch is reported as an error.
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "core/config/StandardParameters.h"
#include "data/GlobalDefs.h"
#include "physics/boundary/BoundaryTypes.h"
#include "physics/species/Species.h"

#ifndef ARCH_BOUNDARY_SOURCE_SHA256
/** Build-time digest of a boundary source file; empty means "not supplied". */
#define ARCH_BOUNDARY_SOURCE_SHA256 ""
#endif

namespace arch::boundary
{
/** Callback form for a case physical boundary; free functions and const
 *  callable classes share this strongly typed signature. Evaluation is a pure
 *  function of its immutable input snapshot: identical state/time/purpose may
 *  reuse completed ghosts rather than invoke the callback again. */
using PhysicalBoundaryFunction = std::function<PhysicalBoundaryData(const PhysicalBoundaryContext &)>;

/** Callback form for a case gravity boundary, kept distinct from the physical
 *  signature so a misused callback type cannot compile. */
using GravityBoundaryFunction = std::function<GravityBoundaryData(const GravityBoundaryContext &)>;

/**
 * @brief One immutable set of callbacks resolved for a single case.
 *
 * @c identity is deterministic text built from the callback name, the base
 * name of the owning source file and its digest; it never embeds host
 * absolute paths and can be compared across builds and machines.
 */
struct ResolvedUserBoundaries
{
    PhysicalBoundaryFunction physical;
    GravityBoundaryFunction gravity;
    std::string identity;
};

namespace detail
{
/** Lexically normalized path text; no filesystem access is performed. */
inline std::string NormalizedLexicalPath(const std::string &path)
{
    if (path.empty())
        return {};
    return std::filesystem::path(path).lexically_normal().generic_string();
}

/** Final path component of an already normalized path. */
inline std::string PathFileName(const std::string &normalized_path)
{
    return std::filesystem::path(normalized_path).filename().generic_string();
}

/** Parent directory text of an already normalized path. */
inline std::string PathDirectory(const std::string &normalized_path)
{
    const std::string parent = std::filesystem::path(normalized_path).parent_path().generic_string();
    return parent.empty() ? std::string(".") : parent;
}

/** Deterministic identity fragment for one resolved boundary kind. */
inline std::string BoundaryIdentityText(const char *kind, const std::string &name,
                                        const std::string &source_file,
                                        const std::string &source_sha256)
{
    const std::string file_name = PathFileName(NormalizedLexicalPath(source_file));
    return std::string(kind) + ":name=" + name + ",file=" + file_name + ",sha=" + source_sha256;
}

/** True only for text that is exactly 64 lowercase hexadecimal characters. */
inline bool IsLowercaseSha256(const std::string &text)
{
    if (text.size() != 64)
        return false;
    for (const char character : text)
    {
        const bool decimal_digit = character >= '0' && character <= '9';
        const bool lowercase_hex_letter = character >= 'a' && character <= 'f';
        if (!decimal_digit && !lowercase_hex_letter)
            return false;
    }
    return true;
}

/**
 * @brief Reject an unusable registration for one resolved entry.
 * @param kind "physical" or "gravity", quoted in every diagnostic.
 * @param expected_file Exact sibling file name required by the contract.
 * @param case_source_file Lexically normalized registered case source path.
 */
template <typename Entry>
void ValidateResolvedEntry(const char *kind, const std::string &expected_file,
                           const std::string &name, const Entry &entry,
                           const std::string &case_source_file)
{
    if (!entry.duplicate_sources.empty())
    {
        std::vector<std::string> sources{entry.source_file};
        sources.insert(sources.end(), entry.duplicate_sources.begin(),
                       entry.duplicate_sources.end());
        for (std::string &source : sources)
            source = NormalizedLexicalPath(source);
        std::sort(sources.begin(), sources.end());
        std::string joined;
        for (std::size_t index = 0; index < sources.size(); ++index)
            joined += (index == 0 ? std::string("'") : std::string(", '")) + sources[index] + "'";
        throw std::runtime_error(
            std::string("boundary registration error: duplicate ") + kind +
            " boundary callback named '" + name + "' from field source_file; registered in " +
            joined);
    }
    if (entry.source_sha256.empty())
        throw std::runtime_error(
            std::string("boundary registration error: ") + kind +
            " boundary callback '" + name + "' has an empty source_sha256 field in file '" +
            NormalizedLexicalPath(entry.source_file) +
            "'; rebuild so CMake defines ARCH_BOUNDARY_SOURCE_SHA256");
    if (!IsLowercaseSha256(entry.source_sha256))
        throw std::runtime_error(
            std::string("boundary registration error: ") + kind +
            " boundary callback '" + name + "' has source_sha256 '" + entry.source_sha256 +
            "' in file '" + NormalizedLexicalPath(entry.source_file) +
            "'; field source_sha256 must be exactly 64 lowercase hexadecimal characters "
            "as supplied by CMake's ARCH_BOUNDARY_SOURCE_SHA256");

    const std::string registered_file = NormalizedLexicalPath(entry.source_file);
    const std::string registered_name = PathFileName(registered_file);
    if (registered_name != expected_file)
        throw std::runtime_error(
            std::string("boundary registration error: ") + kind +
            " boundary callback '" + name + "' has source_file '" + registered_file +
            "' but field source_file must name '" + expected_file +
            "' in the case source directory '" + PathDirectory(case_source_file) + "'");

    const std::string registered_directory = PathDirectory(registered_file);
    const std::string case_directory = PathDirectory(case_source_file);
    if (registered_directory != case_directory)
        throw std::runtime_error(
            std::string("boundary registration error: ") + kind +
            " boundary callback '" + name + "' source_file '" + registered_file +
            "' is in directory '" + registered_directory + "' but case source '" +
            case_source_file + "' expects directory '" + case_directory + "'");
}
} // namespace detail

/**
 * @brief Registration owner for case boundary callbacks.
 *
 * Physical and gravity callbacks live in two independent maps with distinct
 * value types; a name registered for one kind is never visible to the other.
 * Duplicate registrations are recorded and reported by Resolve() instead of
 * terminating static initialization.
 */
class BoundaryRegistry
{
public:
    /** @brief Process-wide registration instance (Meyers' singleton). */
    static BoundaryRegistry &Get()
    {
        static BoundaryRegistry instance;
        return instance;
    }

    /**
     * @brief Register a named physical boundary callback.
     * @param name Case-facing lookup name shared with the problem name.
     * @param function Free function or const callable class instance.
     * @param source_file Compile-time source path (macro supplies __FILE__).
     * @param source_sha256 Compile-time digest (macro supplies CMake value).
     */
    void RegisterPhysical(const std::string &name, PhysicalBoundaryFunction function,
                          const std::string &source_file, const std::string &source_sha256)
    {
        RegisterInto(physical_, name, std::move(function), source_file, source_sha256);
    }

    /** @brief Register a named gravity boundary callback; see RegisterPhysical. */
    void RegisterGravity(const std::string &name, GravityBoundaryFunction function,
                         const std::string &source_file, const std::string &source_sha256)
    {
        RegisterInto(gravity_, name, std::move(function), source_file, source_sha256);
    }

    /** Inspect registration presence without evaluating a callback. */
    bool HasPhysical(const std::string& name) const { return physical_.contains(name); }
    bool HasGravity(const std::string& name) const { return gravity_.contains(name); }

    /**
     * @brief Resolve the callbacks a case needs, or throw a specific error.
     * @param name Registered case name.
     * @param case_source_file Case source path (lexically normalized here).
     * @param need_physical Require a same-directory physical_boundary.cpp.
     * @param need_gravity Require a same-directory gravity_boundary.cpp.
     */
    ResolvedUserBoundaries Resolve(const std::string &name, const std::string &case_source_file,
                                   bool need_physical, bool need_gravity) const
    {
        if (case_source_file.empty())
            throw std::runtime_error(
                "boundary registration error: case source path in field case_source_file is empty");

        const std::string normalized_case = detail::NormalizedLexicalPath(case_source_file);
        ResolvedUserBoundaries resolved;
        std::string identity;

        if (need_physical)
        {
            const auto entry = physical_.find(name);
            if (entry == physical_.end())
                throw std::runtime_error(
                    "boundary registration error: no physical boundary callback registered for "
                    "case '" + name + "' (field name); expected same-directory file "
                    "'physical_boundary.cpp' next to case source '" + normalized_case + "'");
            detail::ValidateResolvedEntry("physical", "physical_boundary.cpp", name, entry->second,
                                          normalized_case);
            resolved.physical = entry->second.callback;
            identity += detail::BoundaryIdentityText("physical", name, entry->second.source_file,
                                                     entry->second.source_sha256);
        }
        else
        {
            identity += "physical:none";
        }
        identity += ";";

        if (need_gravity)
        {
            const auto entry = gravity_.find(name);
            if (entry == gravity_.end())
                throw std::runtime_error(
                    "boundary registration error: no gravity boundary callback registered for "
                    "case '" + name + "' (field name); expected same-directory file "
                    "'gravity_boundary.cpp' next to case source '" + normalized_case + "'");
            detail::ValidateResolvedEntry("gravity", "gravity_boundary.cpp", name, entry->second,
                                          normalized_case);
            resolved.gravity = entry->second.callback;
            identity += detail::BoundaryIdentityText("gravity", name, entry->second.source_file,
                                                     entry->second.source_sha256);
        }
        else
        {
            identity += "gravity:none";
        }

        resolved.identity = std::move(identity);
        return resolved;
    }

private:
    template <typename Function>
    struct Entry
    {
        Function callback;
        std::string source_file;
        std::string source_sha256;
        std::vector<std::string> duplicate_sources;
    };

    template <typename Function>
    static void RegisterInto(std::map<std::string, Entry<Function>> &map, const std::string &name,
                             Function function, const std::string &source_file,
                             const std::string &source_sha256)
    {
        const auto existing = map.find(name);
        if (existing == map.end())
        {
            Entry<Function> entry;
            entry.callback = std::move(function);
            entry.source_file = source_file;
            entry.source_sha256 = source_sha256;
            map.emplace(name, std::move(entry));
            return;
        }
        existing->second.duplicate_sources.push_back(source_file);
    }

    std::map<std::string, Entry<PhysicalBoundaryFunction>> physical_;
    std::map<std::string, Entry<GravityBoundaryFunction>> gravity_;

    BoundaryRegistry() = default;
    ~BoundaryRegistry() = default;
    BoundaryRegistry(const BoundaryRegistry &) = delete;
    BoundaryRegistry &operator=(const BoundaryRegistry &) = delete;
};

/**
 * @brief Per-simulation boundary selection read by the driver and its workers.
 *
 * @c config and @c species are borrowed references owned by the running
 * simulation; the callbacks are owned copies independent of registry state.
 */
struct UserBoundarySelection
{
    ResolvedUserBoundaries callbacks;
    const SimConfig *config = nullptr;
    const SpeciesManager *species = nullptr;
};

/** Resolve compiled same-directory callbacks needed by this configuration. */
inline ResolvedUserBoundaries ResolveCaseBoundaries(const std::string& name,
    const std::string& source_file, const SimConfig& config)
{
    const auto& registry = BoundaryRegistry::Get();
    bool physical = config.physics.diffusion.use_diffusion && registry.HasPhysical(name);
    const std::string faces[]{config.grid.x1l_boundary_type, config.grid.x1r_boundary_type,
        config.grid.x2l_boundary_type, config.grid.x2r_boundary_type,
        config.grid.x3l_boundary_type, config.grid.x3r_boundary_type};
    for (int face = 0; face < 2*config.grid.dim; ++face)
        physical = physical || faces[face] == "user" || faces[face] == "inflow" || faces[face] == "dirichlet";
    const bool gravity = config.physics.gravity.type == "self" && config.physics.gravity.boundary == "user";
    if (!physical && !gravity) return {};
    return registry.Resolve(name, source_file, physical, gravity);
}

/** Portable scientific identity; source paths and output/backend settings are excluded. */
inline std::string BoundaryRestartIdentity(const SimConfig& config, const ResolvedUserBoundaries& callbacks)
{
    std::ostringstream text;
    text << "boundary-v1;" << callbacks.identity << ";faces=";
    const std::string faces[]{config.grid.x1l_boundary_type, config.grid.x1r_boundary_type,
        config.grid.x2l_boundary_type, config.grid.x2r_boundary_type,
        config.grid.x3l_boundary_type, config.grid.x3r_boundary_type};
    for (int face = 0; face < 2*config.grid.dim; ++face) text << faces[face] << ';';
    if (callbacks.physical || callbacks.gravity) {
        const auto scientific_custom = [](std::string_view key) {
            return key != "log_dir" && std::none_of(arch::config::standard_parameters.begin(),
                arch::config::standard_parameters.end(), [&](const auto& field){ return field.key == key; });
        };
        text << std::hexfloat;
        std::map<std::string, double> numeric;
        std::map<std::string, std::string> strings;
        for (const auto& [key, token] : config.DeclaredInputTokens()) {
            if (!scientific_custom(key)) continue;
            try { numeric.emplace(key, ConfigParser::ParseNumber(key, token)); }
            catch (const ConfigValueError&) { strings.emplace(key, token); }
        }
        // Retain boundary-v1 ordering/encoding from the former custom maps.
        for (const auto& [key, value] : numeric)
            text << key.size() << ':' << key << '=' << value << ';';
        for (const auto& [key, value] : strings)
            text << key.size() << ':' << key << '=' << value.size() << ':' << value << ';';
    }
    return text.str();
}

namespace detail
{
/** Active selection for the calling thread, or null when none is bound. */
inline thread_local const UserBoundarySelection *current_user_boundaries = nullptr;
} // namespace detail

/** @brief Active selection for this thread, or null when no scope is bound. */
inline const UserBoundarySelection *CurrentUserBoundaries()
{
    return detail::current_user_boundaries;
}

/**
 * @brief RAII owner that publishes one immutable selection and restores the
 *        previously bound selection on destruction.
 */
class ScopedUserBoundarySelection
{
public:
    ScopedUserBoundarySelection(ResolvedUserBoundaries callbacks, const SimConfig &config,
                                const SpeciesManager &species)
        : selection_{std::move(callbacks), &config, &species},
          previous_(detail::current_user_boundaries)
    {
        detail::current_user_boundaries = &selection_;
    }

    ~ScopedUserBoundarySelection()
    {
        detail::current_user_boundaries = previous_;
    }

    ScopedUserBoundarySelection(const ScopedUserBoundarySelection &) = delete;
    ScopedUserBoundarySelection &operator=(const ScopedUserBoundarySelection &) = delete;
    ScopedUserBoundarySelection(ScopedUserBoundarySelection &&) = delete;
    ScopedUserBoundarySelection &operator=(ScopedUserBoundarySelection &&) = delete;

    /** @brief The immutable selection owned by this scope. */
    const UserBoundarySelection &selection() const { return selection_; }

private:
    UserBoundarySelection selection_;
    const UserBoundarySelection *previous_;
};
} // namespace arch::boundary
