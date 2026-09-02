/**
 * @file FileFingerprint.h
 * @brief Content fingerprints shared by runtime loaders and restart I/O.
 */

#pragma once

#include <string>

namespace arch::core {

/**
 * Return the lowercase hexadecimal SHA-256 digest of one complete file.
 * This deliberately performs an uncached read so content changes cannot hide
 * behind unchanged path, size, or modification-time metadata.
 */
std::string file_sha256(const std::string& path);

} // namespace arch::core
