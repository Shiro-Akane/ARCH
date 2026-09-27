/**
 * @file FileFingerprint.h
 * @brief Content fingerprints shared by runtime loaders and restart I/O.
 * Workflow:
 * 1. Receive a path or buffer from a read-only inspection request.
 * 2. Fingerprint or verify the underlying file identity.
 * 3. Reject stale reads before producing inspection evidence.
 */

#pragma once

#include <string>
#include <string_view>

namespace arch::core {

/**
 * Return the lowercase hexadecimal SHA-256 digest of one complete file.
 * This always reads complete contents, so changes cannot hide behind path,
 * size or modification-time metadata. A CPU preview session may retain bytes
 * and reuse a digest after exact full-content comparison; normal callers hash.
 */
std::string file_sha256(const std::string& path);

/** Hash a canonical interpretation record with the same SHA-256 authority. */
std::string string_sha256(std::string_view bytes);

} // namespace arch::core
