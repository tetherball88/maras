#include "maras/domain/lineage/PaternityActionRules.h"

#include "maras/foundation/StringUtils.h"

namespace MARAS::Domain::Lineage {

    namespace StringUtils = Foundation::StringUtils;

    PaternityRevealDirective ParsePaternityRevealDirective(
        std::string_view rawTargetFather,
        std::string_view childName,
        std::string_view revealMode) {
        const std::string_view trimmedFather = StringUtils::TrimView(rawTargetFather);
        const std::string_view trimmedMode = StringUtils::TrimView(revealMode);
        const std::string_view trimmedChild = StringUtils::TrimView(childName);

        PaternityRevealDirective directive{};
        directive.isBoth = StringUtils::CaseInsensitiveEquals(trimmedFather, "both") ||
                           StringUtils::Contains(trimmedFather, "both");
        directive.isPregnancyOnly =
            StringUtils::CaseInsensitiveEquals(trimmedMode, "pregnancy_only") ||
            StringUtils::CaseInsensitiveEquals(trimmedMode, "announce_pregnancy") ||
            StringUtils::CaseInsensitiveEquals(trimmedMode, "hide_paternity") ||
            StringUtils::Contains(trimmedMode, "pregnancy_only") ||
            StringUtils::Contains(trimmedFather, "pregnancy_only");
        directive.resolveAllMatches =
            !directive.isPregnancyOnly && StringUtils::CaseInsensitiveEquals(trimmedChild, "all");
        if (!directive.isBoth && !trimmedFather.empty()) {
            directive.fatherLookupQuery = std::string(trimmedFather);
        }
        return directive;
    }

    PaternityRevealTarget ClassifyPaternityRevealTarget(
        const PaternityRevealDirective& directive,
        Foundation::ActorId resolvedFatherActorId,
        std::string_view rawTargetFather,
        Foundation::ActorId bioFatherId,
        std::string_view bioFatherName,
        Foundation::ActorId presumedFatherId,
        std::string_view presumedFatherName) {
        if (directive.isBoth) {
            return PaternityRevealTarget::Both;
        }

        const std::string_view trimmedRaw = StringUtils::TrimView(rawTargetFather);
        if ((!resolvedFatherActorId.IsNull() && !bioFatherId.IsNull() && resolvedFatherActorId == bioFatherId) ||
            (!bioFatherName.empty() && StringUtils::CaseInsensitiveEquals(trimmedRaw, bioFatherName))) {
            return PaternityRevealTarget::BioFather;
        }

        if ((!resolvedFatherActorId.IsNull() && !presumedFatherId.IsNull() &&
             resolvedFatherActorId == presumedFatherId) ||
            (!presumedFatherName.empty() && StringUtils::CaseInsensitiveEquals(trimmedRaw, presumedFatherName))) {
            return PaternityRevealTarget::PresumedPartner;
        }

        if (StringUtils::Contains(trimmedRaw, "bio")) {
            return PaternityRevealTarget::BioFather;
        }

        if (StringUtils::Contains(trimmedRaw, "presumed") ||
            StringUtils::Contains(trimmedRaw, "assumed") ||
            StringUtils::Contains(trimmedRaw, "partner")) {
            return PaternityRevealTarget::PresumedPartner;
        }

        return PaternityRevealTarget::Both;
    }

    PaternityDiscoveryTarget ClassifyPaternityDiscoveryTarget(
        Foundation::ActorId discoveringActorId,
        Foundation::ActorId presumedFatherId,
        Foundation::ActorId bioFatherId,
        bool isSecretForPartner) {
        if ((!presumedFatherId.IsNull() && discoveringActorId == presumedFatherId) ||
            (discoveringActorId != bioFatherId && isSecretForPartner)) {
            return PaternityDiscoveryTarget::PresumedPartner;
        }
        return PaternityDiscoveryTarget::BioFather;
    }

}  // namespace MARAS::Domain::Lineage
