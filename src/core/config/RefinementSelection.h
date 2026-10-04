/**
 * @file RefinementSelection.h
 * @brief Shared host-only AMR selection parsing and conditional filtering.
 */
#pragma once
#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include "data/GlobalDefs.h"

namespace arch::config {
inline void ResolveRefinementSelection(AmrConfig& a, int dimension,
                                       bool burn_enabled, bool emit_warnings = true) {
        std::replace(a.refine_var.begin(), a.refine_var.end(), '+', ',');
        a.refine_on_rho = false;
        a.refine_on_p = false;
        a.refine_on_temp = false;
        a.refine_on_velx = false;
        a.refine_on_vely = false;
        a.refine_on_velz = false;
        a.refine_on_eng = false;
        a.refine_on_vorticity = false;
        a.refine_on_div_v = false;
        a.refine_on_entropy = false;
        a.refine_on_enuc = false;
        a.refine_on_jeans = false;
        a.refine_on_species = false;
        a.refine_all_species = false;
        a.refine_species_names.clear();
        std::stringstream refine_stream(a.refine_var);
        std::string refine_token;
        while (std::getline(refine_stream, refine_token, ',')) {
            const size_t first = refine_token.find_first_not_of(" \t");
            const size_t last = refine_token.find_last_not_of(" \t");
            if (first == std::string::npos) continue;
            refine_token = refine_token.substr(first, last - first + 1);
            std::string canonical = refine_token;
            std::transform(canonical.begin(), canonical.end(), canonical.begin(),
                [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            if (canonical == "DENS") a.refine_on_rho = true;
            else if (canonical == "PRES") a.refine_on_p = true;
            else if (canonical == "TEMP") a.refine_on_temp = true;
            else if (canonical == "VELX") a.refine_on_velx = true;
            else if (canonical == "VELY") a.refine_on_vely = true;
            else if (canonical == "VELZ") a.refine_on_velz = true;
            else if (canonical == "ENER") a.refine_on_eng = true;
            else if (canonical == "VORT") a.refine_on_vorticity = true;
            else if (canonical == "DIVV") a.refine_on_div_v = true;
            else if (canonical == "ENTR") a.refine_on_entropy = true;
            else if (canonical == "ENUC") a.refine_on_enuc = true;
            else if (canonical == "JENS") a.refine_on_jeans = true;
            else if (canonical == "SPECIES") { a.refine_on_species = true; a.refine_all_species = true; }
            else if (canonical == "VORTICITY" || canonical == "DIV_V" || canonical == "ENTROPY" || canonical == "JEANS" ||
                     canonical == "RHO" || canonical == "P" || canonical == "U" || canonical == "V" || canonical == "W" || canonical == "ENG" || canonical == "ALL")
                throw std::invalid_argument("Use a canonical AMR indicator name instead of: " + refine_token);
            else {
                std::transform(refine_token.begin(), refine_token.end(), refine_token.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                a.refine_on_species = true;
                a.refine_species_names.push_back(refine_token);
            }
        }
        const auto warn_amr_disabled = [emit_warnings](const std::string& name, const std::string& reason) {
            if (emit_warnings) std::cerr << "[RuntimeParams] Warning: AMR indicator " << name << " is disabled: " << reason << std::endl;
        };
        if (a.refine_on_enuc && !burn_enabled) {
            warn_amr_disabled("ENUC", "the nuclear reaction network is not enabled"); a.refine_on_enuc = false;
        }
        if (a.refine_on_vely && dimension < 2) { warn_amr_disabled("VELY", "the simulation is one-dimensional"); a.refine_on_vely = false; }
        if (a.refine_on_velz && dimension < 3) { warn_amr_disabled("VELZ", "the simulation has fewer than three dimensions"); a.refine_on_velz = false; }
        if (a.refine_on_jeans)
            throw std::invalid_argument("Explicit JENS refinement is not enabled until the complete lifecycle qualification passes.");
        const auto has_amr_indicator = [&] {
            return a.refine_on_rho || a.refine_on_p || a.refine_on_temp || a.refine_on_velx ||
                a.refine_on_vely || a.refine_on_velz || a.refine_on_eng || a.refine_on_vorticity ||
                a.refine_on_div_v || a.refine_on_entropy || a.refine_on_enuc || a.refine_on_jeans || a.refine_on_species;
        };
        if (!has_amr_indicator()) throw std::invalid_argument("refine_var has no usable AMR indicator for this configuration.");
}
} // namespace arch::config
