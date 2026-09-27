/**
 * @file InspectionEosCache.h
 * @brief Reuse immutable EOS tables for bounded inspection requests.
 *
 * Workflow:
 * 1. Receive a resolved request at the owning module boundary.
 * 2. Reuse immutable EOS tables for bounded inspection requests.
 * 3. Return bounded data through the established interface.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <variant>

#include "physics/eos/HelmEos.h"
#include "physics/eos/tabular/Tabular3DEOS.h"
#include "physics/eos/tabular/Tabular4DEOS.h"

// CPU inspection only. Retain one table owner, with an owned copy of its
// species metadata. Request-local views are rebound to the live registry;
// neither a previous model nor a borrowed SpeciesManager survives a request.
class InspectionEosCache {
    using Owners = std::variant<std::monostate, std::unique_ptr<HelmEos>,
        std::unique_ptr<Tabular3DEOS>, std::unique_ptr<Tabular4DEOS>>;
    SpeciesManager species_;
    Owners owner_;
    std::string key_, digest_;
public:
    std::uint64_t loads = 0, hits = 0;
    InspectionEosCache() = default;
    InspectionEosCache(const InspectionEosCache&) = delete;
    InspectionEosCache& operator=(const InspectionEosCache&) = delete;

    static bool same_values(const SpeciesManager& a, const SpeciesManager& b) {
        if (a.count() != b.count()) return false;
        for (std::size_t i = 0; i < a.species_list.size(); ++i) {
            const auto& x = a.species_list[i]; const auto& y = b.species_list[i];
            if (x.name != y.name || x.A != y.A || x.Z != y.Z ||
                x.gamma_ref != y.gamma_ref || x.Cv_ref != y.Cv_ref) return false;
        }
        return true;
    }
    bool resident() const { return owner_.index() != 0; }
    void clear() {
        owner_.emplace<std::monostate>();
        species_.species_list.clear(); key_.clear(); digest_.clear();
    }
    template<class Owner, class Load, class Fingerprint>
    Owner& owner(const std::string& key, const std::string& digest,
              const SpeciesManager& species, Load&& load, Fingerprint&& fingerprint) {
        auto* retained = std::get_if<std::unique_ptr<Owner>>(&owner_);
        if (!retained || !*retained || key_ != key || digest_ != digest ||
            !same_values(species_, species)) {
            // Evict before loading: a transition never retains two large tables.
            clear();
            try {
                species_ = species;
                auto replacement = load(&species_);
                if (fingerprint() != digest)
                    throw std::runtime_error("EOS table changed while the inspection resource was loading");
                if (!same_values(species_, species))
                    throw std::runtime_error("Species changed while the inspection resource was loading");
                owner_ = std::move(replacement);
                key_ = key; digest_ = digest; ++loads;
            } catch (...) { clear(); throw; }
        } else ++hits;
        return *std::get<std::unique_ptr<Owner>>(owner_);
    }
    template<class Owner, class Load, class Fingerprint>
    auto view(const std::string& key, const std::string& digest,
              const SpeciesManager& species, Load&& load, Fingerprint&& fingerprint) {
        auto result = owner<Owner>(key,digest,species,std::forward<Load>(load),
            std::forward<Fingerprint>(fingerprint)).get_view();
        result.specs = species.get_host_view();
        return result;
    }
    // Helm dispatch historically supplies the owner type (including to CUDA
    // factories). Preserve that callback signature; restore its owned registry
    // before the request-local registry goes out of scope, including on error.
    class HelmBinding {
        HelmEos& owner_;
        SpeciesHostView previous_;
    public:
        HelmBinding(HelmEos& owner,const SpeciesManager& live) : owner_(owner),previous_(owner.specs) {
            owner_.specs=live.get_host_view();
        }
        ~HelmBinding() { owner_.specs=previous_; }
        HelmBinding(const HelmBinding&)=delete;
        HelmBinding& operator=(const HelmBinding&)=delete;
    };
};
