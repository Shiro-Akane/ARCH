/**
 * @file CaseParameterValues.h
 * @brief Preserve case/auxiliary lexical values for strict Setup reads.
 *
 * This populates only the case-value store; it does not validate a complete
 * configuration or authorize Setup, Preview or evolution.
 */
#pragma once
#include "data/GlobalDefs.h"
#include "io/ConfigParser.h"

namespace arch::config {
inline void CaptureCaseParameterValues(const ConfigParser& parser, SimConfig& cfg) {
    for (const auto &[key, val_str] : parser.GetAllParams())
    {
        // Standard values live only in their typed Core sections. Retain
        // raw input in the parser/trace, never as a second mutable authority.
        if (arch::config::IsStandardInputKey(key)) continue;
        // Preserve lexical identity for every case read, also outside Preview.
        cfg.custom_string_params[key] = val_str;
        try
        {
            // Require the complete numeric token; expressions remain strings.
            double val = ConfigParser::ParseNumber(key, val_str);
            cfg.custom_params[key] = val;
        }
        catch (...)
        {
            // Non-numeric tokens remain available for strict typed/string reads.
        }
    }
}
} // namespace arch::config
