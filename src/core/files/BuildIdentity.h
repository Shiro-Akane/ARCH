/**
 * @file BuildIdentity.h
 * @brief Immutable project-source and configured compiler-profile provenance.
 *
 * Workflow:
 * 1. Read the manifest embedded by CMake in one narrow translation unit.
 * 2. Fingerprint its project source and configured build profile independently.
 * 3. Reuse these immutable records with the separate running-ELF fingerprint.
 *
 * The scope excludes runtime shared-library closure and is not a signature.
 */
#pragma once

#include <string>

namespace arch::core {
/** One configured project-source/compiler profile; Git is an optional annotation. */
struct BuildIdentity {
    std::string version, scope;
    std::string source_manifest_sha256, profile_sha256, build_id;
    std::string source_git_head, source_git_dirty;
    std::string source_manifest, profile_record;
};

/** Return process-immutable CMake provenance; reject absent generated evidence. */
const BuildIdentity& compiled_build_identity();
} // namespace arch::core
