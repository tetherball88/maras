#pragma once

#include "maras/foundation/StrongIds.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace MARAS::Domain::Lineage {

    enum class PaternityRevealTarget : std::uint8_t {
        Both = 0,
        BioFather = 1,
        PresumedPartner = 2,
    };

    enum class PaternityDiscoveryTarget : std::uint8_t {
        PresumedPartner = 0,
        BioFather = 1,
    };

    struct PaternityRevealDirective {
        bool isBoth{ false };
        bool isPregnancyOnly{ false };
        bool resolveAllMatches{ false };
        std::string fatherLookupQuery{};
    };

    /// Parses raw SkyrimNet `RevealChildSecret` action parameters into a typed reveal directive.
    [[nodiscard]] PaternityRevealDirective ParsePaternityRevealDirective(
        std::string_view rawTargetFather,
        std::string_view childName,
        std::string_view revealMode);

    /// Classifies whether a reveal targets the biological father, presumed partner, or both.
    /// Exact ActorId/display-name identity always takes precedence over substring role heuristics.
    [[nodiscard]] PaternityRevealTarget ClassifyPaternityRevealTarget(
        const PaternityRevealDirective& directive,
        Foundation::ActorId resolvedFatherActorId,
        std::string_view rawTargetFather,
        Foundation::ActorId bioFatherId,
        std::string_view bioFatherName,
        Foundation::ActorId presumedFatherId,
        std::string_view presumedFatherName);

    /// Classifies which secret edge (`PresumedPartner` vs `BioFather`) is resolved when a father discovers paternity.
    [[nodiscard]] PaternityDiscoveryTarget ClassifyPaternityDiscoveryTarget(
        Foundation::ActorId discoveringActorId,
        Foundation::ActorId presumedFatherId,
        Foundation::ActorId bioFatherId,
        bool isSecretForPartner);

}  // namespace MARAS::Domain::Lineage
