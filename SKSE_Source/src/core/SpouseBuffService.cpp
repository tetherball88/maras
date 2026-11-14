#include "core/SpouseBuffService.h"

#include "core/AffectionService.h"
#include "core/BonusesService.h"
#include "core/DeceasedRegistry.h"
#include "core/NPCRelationshipManager.h"
#include "core/PollingService.h"
#include "core/SpouseHierarchyManager.h"
#include "maras/state/EngineSyncPlan.h"
#include "services/ActorService.h"

namespace MARAS {

    namespace {
        constexpr std::size_t kSocialClassCount = State::BuffMultipliersPlan::kSocialClassCount;

        inline float ComputeSpouseMultiplierByID(RE::FormID fid, int spouseCount,
                                                 const SpouseHierarchyManager& hierarchy,
                                                 const AffectionService& affection) {
            if (fid == 0) return 0.0f;
            const int spouseRank = hierarchy.GetRank(fid);
            const float affectionBuffMult =
                State::ComputeAffectionMultiplier(affection.GetPermanentAffection(fid));
            return State::ComputeHierarchyRankMultiplier(spouseRank, spouseCount) * affectionBuffMult;
        }

        inline bool IndexInBounds(std::size_t index, std::size_t size) { return index < size; }

        inline std::vector<RE::FormID> GetLivingRegisteredMarriedSpouses(
            const NPCRelationshipManager& relationships) {
            auto spouses = relationships.GetAllMarried(kPlayerFormID);
            const auto& deceased = DeceasedRegistry::GetSingleton();
            std::erase_if(spouses, [&](RE::FormID fid) {
                return fid == 0 || !relationships.IsRegistered(fid) || deceased.IsDeceased(fid);
            });
            return spouses;
        }
    }  // namespace

    float SpouseBuffService::GetSpouseMultiplier(const RE::Actor* spouse) {
        if (!spouse) return 0.0f;

        const auto& hierarchy = SpouseHierarchyManager::GetSingleton();
        const auto& relationships = NPCRelationshipManager::GetSingleton();
        const auto& affection = AffectionService::GetSingleton();

        const int spouseCount = static_cast<int>(relationships.GetMarriedCount(kPlayerFormID));
        return ComputeSpouseMultiplierByID(spouse->GetFormID(), spouseCount, hierarchy, affection);
    }

    std::vector<float> SpouseBuffService::GetFollowersMultipliers(const std::vector<RE::Actor*>& followers) {
        // Six skill-type buckets (Warrior, Mage, Rogue, Craftsman, Ranger, Orator)
        std::vector<float> multipliers(static_cast<size_t>(MARAS::SkillType::Orator) + 1, 0.0f);

        const auto& relationships = NPCRelationshipManager::GetSingleton();
        const auto& affection = AffectionService::GetSingleton();
        const auto& deceased = DeceasedRegistry::GetSingleton();

        for (const auto* actor : followers) {
            if (!actor) continue;

            // Only count followers who are living, registered spouses
            const RE::FormID fid = actor->GetFormID();
            if (!relationships.IsMarried(fid) || !relationships.IsRegistered(fid) || deceased.IsDeceased(fid)) {
                continue;
            }

            // Determine skill type for this NPC
            const auto skill = relationships.GetSkillType(fid);
            const std::size_t idx = static_cast<std::size_t>(skill);
            if (!IndexInBounds(idx, multipliers.size())) {
                MARAS_LOG_DEBUG("GetFollowersMultipliers: invalid skill index {} for {}", idx, fid);
                continue;
            }

            // Add affection multiplier from shared pure formula
            const float affectionMultFollower =
                State::ComputeAffectionMultiplier(affection.GetPermanentAffection(fid));

            multipliers[idx] += affectionMultFollower;
        }

        return multipliers;
    }

    std::vector<float> SpouseBuffService::GetPermanentMultipliers() {
        // Social-class buckets
        std::vector<float> multipliers(kSocialClassCount, 0.0f);

        const auto& relationships = NPCRelationshipManager::GetSingleton();
        const auto& hierarchy = SpouseHierarchyManager::GetSingleton();
        const auto& affection = AffectionService::GetSingleton();

        const auto marriedSpouses = GetLivingRegisteredMarriedSpouses(relationships);
        const int spouseCount = static_cast<int>(marriedSpouses.size());

        for (const RE::FormID fid : marriedSpouses) {
            if (fid == 0) continue;

            const std::size_t socialIndex = static_cast<std::size_t>(relationships.GetSocialClass(fid));
            if (!IndexInBounds(socialIndex, multipliers.size())) {
                MARAS_LOG_DEBUG("GetPermanentMultipliers: spouse {:08X} has invalid social index {}", fid, socialIndex);
                continue;
            }

            multipliers[socialIndex] += ComputeSpouseMultiplierByID(fid, spouseCount, hierarchy, affection);
        }

        return multipliers;
    }

    nlohmann::json SpouseBuffService::GetHouseholdBuffsSnapshot() {
        nlohmann::json root = nlohmann::json::object();
        nlohmann::json permArray = nlohmann::json::array();
        nlohmann::json follArray = nlohmann::json::array();

        const auto& bonuses = BonusesService::GetSingleton();
        const auto& relMgr = NPCRelationshipManager::GetSingleton();
        const auto& hierarchy = SpouseHierarchyManager::GetSingleton();
        const auto& affection = AffectionService::GetSingleton();
        auto teammates = PollingService::GetSingleton().GetCurrentTeammates();

        auto marriedSpouses = GetLivingRegisteredMarriedSpouses(relMgr);
        int spouseCount = static_cast<int>(marriedSpouses.size());

        // 1. Permanent Buffs (8 Social Classes)
        struct SocialClassDef {
            SocialClass sc;
            const char* key;
            const char* label;
        };

        const std::array<SocialClassDef, 8> socialClasses = {{
            { SocialClass::Outcast, "outcast", "Outcast" },
            { SocialClass::Poverty, "poverty", "Poverty" },
            { SocialClass::Working, "working", "Working" },
            { SocialClass::Middle, "middle", "Middle" },
            { SocialClass::Wealthy, "wealthy", "Wealthy" },
            { SocialClass::Religious, "religious", "Religious" },
            { SocialClass::Nobles, "nobles", "Nobles" },
            { SocialClass::Rulers, "rulers", "Rulers" }
        }};

        int activePermCount = 0;
        for (const auto& def : socialClasses) {
            nlohmann::json item = nlohmann::json::object();
            item["id"] = def.key;
            item["category"] = def.label;
            item["categoryType"] = "permanent";

            const BonusEntry* be = bonuses.GetBonusEntry(def.key, 0);
            item["name"] = be && !be->name.empty() ? be->name : def.label;
            item["description"] = be ? be->description : "";
            item["unit"] = be ? be->unit : "";
            item["type"] = be ? be->type : "spell";
            float baseValue = be ? be->value : 0.0f;
            item["baseValue"] = baseValue;

            float totalMult = 0.0f;
            nlohmann::json contributors = nlohmann::json::array();

            for (auto fid : marriedSpouses) {
                if (relMgr.GetSocialClass(fid) == def.sc) {
                    int rank = hierarchy.GetRank(fid);
                    float rankMult = State::ComputeHierarchyRankMultiplier(rank, spouseCount);
                    int affVal = affection.GetPermanentAffection(fid);
                    float affMult = State::ComputeAffectionMultiplier(affVal);
                    float netMult = rankMult * affMult;
                    totalMult += netMult;

                    nlohmann::json c = nlohmann::json::object();
                    c["formId"] = std::format("{:08X}", fid);
                    c["name"] = ActorSvc::GetName(fid);
                    c["rank"] = rank;
                    c["rankMultiplier"] = rankMult;
                    c["affection"] = affVal;
                    c["affectionMultiplier"] = affMult;
                    c["multiplier"] = netMult;
                    c["isFollower"] = teammates.contains(fid);
                    contributors.push_back(c);
                }
            }

            item["multiplier"] = totalMult;
            item["effectiveValue"] = baseValue * totalMult;
            item["isActive"] = (totalMult > 0.0f);
            if (totalMult > 0.0f) {
                activePermCount++;
            }
            item["contributors"] = contributors;
            item["potentialSpouses"] = nlohmann::json::array();
            permArray.push_back(item);
        }

        // 2. Follower Buffs (6 Skill Types)
        struct SkillTypeDef {
            SkillType st;
            const char* key;
            const char* label;
        };

        const std::array<SkillTypeDef, 6> skillTypes = {{
            { SkillType::Warrior, "warrior", "Warrior" },
            { SkillType::Mage, "mage", "Mage" },
            { SkillType::Rogue, "rogue", "Rogue" },
            { SkillType::Craftsman, "craftsman", "Craftsman" },
            { SkillType::Ranger, "ranger", "Ranger" },
            { SkillType::Orator, "orator", "Orator" }
        }};

        int activeFollCount = 0;
        for (const auto& def : skillTypes) {
            nlohmann::json item = nlohmann::json::object();
            item["id"] = def.key;
            item["category"] = def.label;
            item["categoryType"] = "follower";

            const BonusEntry* be = bonuses.GetBonusEntry(def.key, 0);
            item["name"] = be && !be->name.empty() ? be->name : def.label;
            item["description"] = be ? be->description : "";
            item["unit"] = be ? be->unit : "";
            item["type"] = be ? be->type : "spell";
            float baseValue = be ? be->value : 0.0f;
            item["baseValue"] = baseValue;

            float totalMult = 0.0f;
            nlohmann::json activeContributors = nlohmann::json::array();
            nlohmann::json potentialSpouses = nlohmann::json::array();

            for (auto fid : marriedSpouses) {
                if (relMgr.GetSkillType(fid) == def.st) {
                    int affVal = affection.GetPermanentAffection(fid);
                    float affMult = affection.GetMultiplierForNPC(fid);

                    if (teammates.contains(fid)) {
                        totalMult += affMult;

                        nlohmann::json c = nlohmann::json::object();
                        c["formId"] = std::format("{:08X}", fid);
                        c["name"] = ActorSvc::GetName(fid);
                        c["affection"] = affVal;
                        c["affectionMultiplier"] = affMult;
                        c["multiplier"] = affMult;
                        c["isFollower"] = true;
                        activeContributors.push_back(c);
                    } else {
                        nlohmann::json p = nlohmann::json::object();
                        p["formId"] = std::format("{:08X}", fid);
                        p["name"] = ActorSvc::GetName(fid);
                        p["affection"] = affVal;
                        p["affectionMultiplier"] = affMult;
                        p["isFollower"] = false;
                        potentialSpouses.push_back(p);
                    }
                }
            }

            item["multiplier"] = totalMult;
            item["effectiveValue"] = baseValue * totalMult;
            item["isActive"] = (totalMult > 0.0f);
            if (totalMult > 0.0f) {
                activeFollCount++;
            }
            item["contributors"] = activeContributors;
            item["potentialSpouses"] = potentialSpouses;
            follArray.push_back(item);
        }

        root["permanentBuffs"] = permArray;
        root["followerBuffs"] = follArray;
        root["activePermanentCount"] = activePermCount;
        root["activeFollowerCount"] = activeFollCount;
        return root;
    }

}  // namespace MARAS
