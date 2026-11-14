#include "maras/domain/lineage/ChildGrowthRules.h"

#include "maras/foundation/Logging.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <random>

namespace MARAS::Domain::Lineage {

    std::optional<SkillIndex> ParseSkillIndex(std::string_view rawSkillName) {
        std::string clean;
        clean.reserve(rawSkillName.size());
        for (char c : rawSkillName) {
            if (c != '_' && c != '-' && c != ' ') {
                clean.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
        }
        if (clean == "onehanded") return SkillIndex::OneHanded;
        if (clean == "twohanded") return SkillIndex::TwoHanded;
        if (clean == "archery" || clean == "marksman") return SkillIndex::Archery;
        if (clean == "block") return SkillIndex::Block;
        if (clean == "smithing") return SkillIndex::Smithing;
        if (clean == "heavyarmor") return SkillIndex::HeavyArmor;
        if (clean == "lightarmor") return SkillIndex::LightArmor;
        if (clean == "pickpocket") return SkillIndex::Pickpocket;
        if (clean == "lockpicking") return SkillIndex::Lockpicking;
        if (clean == "sneak") return SkillIndex::Sneak;
        if (clean == "alchemy") return SkillIndex::Alchemy;
        if (clean == "speech" || clean == "speechcraft") return SkillIndex::Speech;
        if (clean == "alteration") return SkillIndex::Alteration;
        if (clean == "conjuration") return SkillIndex::Conjuration;
        if (clean == "destruction") return SkillIndex::Destruction;
        if (clean == "illusion") return SkillIndex::Illusion;
        if (clean == "restoration") return SkillIndex::Restoration;
        if (clean == "enchanting") return SkillIndex::Enchanting;
        return std::nullopt;
    }

    std::array<SkillIndex, 4> GetSignatureSkillsForSkillType(SkillType skillType) noexcept {
        switch (skillType) {
            case SkillType::Warrior:
                return { SkillIndex::OneHanded, SkillIndex::TwoHanded, SkillIndex::HeavyArmor, SkillIndex::Block };
            case SkillType::Mage:
                return { SkillIndex::Destruction, SkillIndex::Restoration, SkillIndex::Alteration, SkillIndex::Conjuration };
            case SkillType::Rogue:
                return { SkillIndex::Sneak, SkillIndex::OneHanded, SkillIndex::LightArmor, SkillIndex::Pickpocket };
            case SkillType::Craftsman:
                return { SkillIndex::Smithing, SkillIndex::Enchanting, SkillIndex::Alchemy, SkillIndex::HeavyArmor };
            case SkillType::Ranger:
                return { SkillIndex::Archery, SkillIndex::LightArmor, SkillIndex::Sneak, SkillIndex::Alchemy };
            case SkillType::Orator:
                return { SkillIndex::Speech, SkillIndex::Illusion, SkillIndex::Restoration, SkillIndex::OneHanded };
            default:
                return { SkillIndex::OneHanded, SkillIndex::Block, SkillIndex::HeavyArmor, SkillIndex::LightArmor };
        }
    }

    std::array<SkillIndex, 4> ResolveSignatureSkillsFromArray(
        const std::array<float, 18>& baseSkills,
        SkillType fallbackSkillType) noexcept {
        std::array<std::pair<float, SkillIndex>, 18> scored{};
        float minSkill = 10000.0f;
        float maxSkill = -10000.0f;
        for (std::size_t i = 0; i < 18u; ++i) {
            scored[i] = { baseSkills[i], static_cast<SkillIndex>(i) };
            minSkill = std::min(minSkill, baseSkills[i]);
            maxSkill = std::max(maxSkill, baseSkills[i]);
        }
        if (maxSkill > minSkill + 0.5f) {
            std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) {
                return a.first > b.first;
            });
            return { scored[0].second, scored[1].second, scored[2].second, scored[3].second };
        }
        return GetSignatureSkillsForSkillType(fallbackSkillType);
    }

    std::array<float, 3> GetHMSWeightsForSkillType(SkillType skillType) noexcept {
        switch (skillType) {
            case SkillType::Warrior:
                return { 0.65f, 0.05f, 0.30f };
            case SkillType::Mage:
                return { 0.25f, 0.65f, 0.10f };
            case SkillType::Rogue:
                return { 0.30f, 0.10f, 0.60f };
            case SkillType::Craftsman:
                return { 0.50f, 0.20f, 0.30f };
            case SkillType::Ranger:
                return { 0.35f, 0.10f, 0.55f };
            case SkillType::Orator:
                return { 0.40f, 0.40f, 0.20f };
            default:
                return { 0.50f, 0.20f, 0.30f };
        }
    }

    int GetSocialClassRankOrder(SocialClass socialClass) noexcept {
        switch (socialClass) {
            case SocialClass::Outcast:   return 0;
            case SocialClass::Poverty:   return 1;
            case SocialClass::Working:   return 2;
            case SocialClass::Middle:    return 3;
            case SocialClass::Religious: return 4;
            case SocialClass::Wealthy:   return 5;
            case SocialClass::Nobles:    return 6;
            case SocialClass::Rulers:    return 7;
            default:                     return 1;
        }
    }

    float NormalizeClassSkillWeight(float rawBonus) noexcept {
        if (rawBonus <= 0.0f) {
            return 0.0f;
        }
        return std::clamp(rawBonus, 1.0f, 5.0f);
    }

    std::uint16_t ComputeEffectiveChildLevel(std::uint16_t playerLevel) noexcept {
        return static_cast<std::uint16_t>(std::clamp<int>(static_cast<int>(playerLevel), 10, 50));
    }

    std::uint8_t ComputeBioParentSkillBonus(std::uint16_t parentLevel) noexcept {
        const float t = std::clamp((static_cast<float>(parentLevel) - 1.0f) / 49.0f, 0.0f, 1.0f);
        return static_cast<std::uint8_t>(std::lround(2.0f + 6.0f * t));
    }

    std::uint8_t ComputeGuardianSkillBonus(std::uint16_t guardianLevel) noexcept {
        const float t = std::clamp((static_cast<float>(guardianLevel) - 1.0f) / 49.0f, 0.0f, 1.0f);
        return static_cast<std::uint8_t>(std::lround(2.0f + 5.0f * t));
    }

    void EvaluateChildBioLineageAtBirth(
        State::ChildRecord& record,
        const ParentLineageInput* bioMother,
        const ParentLineageInput* bioFather) noexcept {
        if (record.bioLineageEvaluated) {
            return;
        }
        record.bioSkillBonuses.fill(0);

        auto applyParent = [&](const ParentLineageInput* parent) {
            if (!parent || parent->actorId.IsNull() || parent->actorId.IsUnknownFather()) {
                return;
            }
            const std::uint8_t bonusPts = ComputeBioParentSkillBonus(parent->level);
            for (SkillIndex sig : parent->signatureSkills) {
                const auto sIdx = static_cast<std::size_t>(sig);
                if (sIdx < record.bioSkillBonuses.size()) {
                    const int nextVal = std::clamp(
                        static_cast<int>(record.bioSkillBonuses[sIdx]) + static_cast<int>(bonusPts),
                        0, 32);
                    record.bioSkillBonuses[sIdx] = static_cast<std::uint8_t>(nextVal);
                }
            }
        };

        applyParent(bioMother);
        applyParent(bioFather);
        record.bioLineageEvaluated = true;

        Foundation::LogInfo(
            "[ChildTraining] Evaluated bio-parent skill snapshot for child {:08X} ('{}')",
            record.childId.raw(), record.name);
    }

    void EvaluateChildGuardianLineageAtGraduation(
        State::ChildRecord& record,
        const ParentLineageInput* bioMother,
        const ParentLineageInput* bioFather,
        std::span<const ParentLineageInput> guardians,
        std::span<const std::string> completedDaedricBlessingIds) {
        if (!record.bioLineageEvaluated) {
            EvaluateChildBioLineageAtBirth(record, bioMother, bioFather);
        }

        if (!record.guardianLineageEvaluated) {
            record.guardianSkillBonuses.fill(0);
            for (const auto& g : guardians) {
                if (g.actorId.IsNull() || g.actorId.IsUnknownFather()) {
                    continue;
                }
                const std::uint8_t bonusPts = ComputeGuardianSkillBonus(g.level);
                for (SkillIndex sig : g.signatureSkills) {
                    const auto sIdx = static_cast<std::size_t>(sig);
                    if (sIdx < record.guardianSkillBonuses.size()) {
                        const int nextVal = std::clamp(
                            static_cast<int>(record.guardianSkillBonuses[sIdx]) + static_cast<int>(bonusPts),
                            0, 30);
                        record.guardianSkillBonuses[sIdx] = static_cast<std::uint8_t>(nextVal);
                    }
                }
            }
            record.guardianLineageEvaluated = true;
            Foundation::LogInfo(
                "[ChildTraining] Evaluated guardian mentorship skill snapshot for child {:08X} ('{}')",
                record.childId.raw(), record.name);
        }

        if (record.daedricBlessingId.empty()) {
            const bool playerIsBioParent =
                record.bioMother.IsPlayer() || record.bioFather.IsPlayer();
            const bool playerIsGuardian =
                record.guardians.HasGuardian(ActorId::Player());
            if ((playerIsBioParent || playerIsGuardian) && !completedDaedricBlessingIds.empty()) {
                const std::uint32_t seed =
                    record.childId.raw() ^
                    (record.bioMother.raw() << 1) ^
                    (record.bioFather.raw() << 2) ^
                    static_cast<std::uint32_t>(std::hash<std::string>{}(record.name));
                std::mt19937 blessingRng(seed ^ 0x9E3779B9u);
                std::uniform_int_distribution<std::size_t> dist(0, completedDaedricBlessingIds.size() - 1);
                record.daedricBlessingId = completedDaedricBlessingIds[dist(blessingRng)];
            } else {
                record.daedricBlessingId = "none";
            }
        }
    }

}  // namespace MARAS::Domain::Lineage
