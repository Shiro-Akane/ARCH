/**
 * @file CaseInspection.h
 * @brief Declare case inspection and discovery results for the public request boundary.
 *
 * Workflow:
 * 1. Accept a bounded, verified request at the read-only API boundary.
 * 2. Declare case inspection and discovery results for the public request boundary.
 * 3. Return typed evidence or an explicit error; do not start the simulation Driver.
 */

#pragma once

#include "api/Preview.h"
#include "api/protocol/Json.h"
#include "core/problem/ProblemRegistry.h"

namespace arch::api {
std::string RegisteredCases();
PreviewResponse InspectCase(const PreviewRequest& request);
detail::Json CaseInspectionCapability(const std::string& name, const ProblemRegistration& registration);
void AddAuditedCaseUnits(const std::string& name, const ProblemRegistration& registration,
                         int dimension, preview::ParameterReadTrace& reads);
} // namespace arch::api
