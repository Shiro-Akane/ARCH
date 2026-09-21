#pragma once
#include "Preview.h"
#include "Json.h"
#include "../core/ProblemRegistry.h"
namespace arch::api {
std::string RegisteredCases();
PreviewResponse InspectCase(const PreviewRequest& request);
detail::Json CaseInspectionCapability(const std::string& name, const ProblemRegistration& registration);
void AddAuditedCaseUnits(const std::string& name, const ProblemRegistration& registration,
                         int dimension, preview::ParameterReadTrace& reads);
} // namespace arch::api
