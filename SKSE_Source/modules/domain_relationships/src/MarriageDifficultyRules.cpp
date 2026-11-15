#include "maras/domain/relationships/MarriageDifficultyRules.h"

#include "maras/foundation/EnumUtils.h"
#include "maras/foundation/StringUtils.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace MARAS::Domain::Relationships {

    namespace {
        struct GuildModifiers {
            float bestPositive{ 0.0f };
            float bestNegative{ 0.0f };
            float sameGuild{ 0.0f };

            void UpdateWith(float modifier) noexcept {
                if (modifier > 0.0f) {
                    bestPositive = std::max(bestPositive, modifier);
                } else if (modifier < 0.0f) {
                    bestNegative = std::min(bestNegative, modifier);
                }
            }

            [[nodiscard]] float GetStrongest() const noexcept {
                return (-bestNegative > bestPositive) ? bestNegative : bestPositive;
            }
        };

        [[nodiscard]] constexpr const char* GuildPrefix(GuildIndex idx) noexcept {
            switch (idx) {
                case GuildIndex::Companions:
                    return "guildCompanions";
                case GuildIndex::Thieves:
                    return "guildThieves";
                case GuildIndex::Brotherhood:
                    return "guildBrotherhood";
                case GuildIndex::College:
                    return "guildCollege";
                case GuildIndex::Bards:
                    return "guildBards";
                default:
                    return "";
            }
        }
    }  // namespace

    float MarriageDifficultyConfig::GetParam(std::string_view key, float fallback) const noexcept {
        const auto it = params.find(std::string(key));
        return it != params.end() ? it->second : fallback;
    }

    MarriageDifficultyConfig MarriageDifficultyConfig::CreateDefault() {
        MarriageDifficultyConfig cfg;
        cfg.params = {
            { "sigmoidSteepness", 0.10f },
            { "sigmoidCenter", 50.0f },
            { "difficultyClampMin", 0.0f },
            { "difficultyClampMax", 100.0f },
            { "initialComplexity", 50.0f },
            { "levelDiffClampMin", -10.0f },
            { "levelDiffClampMax", 10.0f },
            { "prestigeDragonbornBonus", 25.0f },
            { "prestigeThaneHoldValue", 4.0f },
            { "prestigeMaxThaneHolds", 9.0f },
            { "prestigeGuildLeaderBonus", 8.0f },
            { "prestigeMostGoldDivisor", 5000.0f },
            { "prestigeMostGoldClampMax", 15.0f },
            { "prestigeHouseUnitMultiplier", 3.0f },
            { "prestigeHorseUnitMultiplier", 1.0f },
            { "prestigeHouseHorseClampMax", 15.0f },
            { "prestigeQuestsMultiplier", 0.2f },
            { "prestigeDungeonsMultiplier", 0.1f },
            { "prestigeSoulsMultiplier", 0.5f },
            { "prestigeRenownClampMax", 25.0f },
            { "prestigePrestigeClamp_min", 0.0f },
            { "prestigePrestigeClamp_max", 100.0f },
            { "prestigeTargetBase", 10.0f },
            { "prestigeTargetPerSocialIndexMultiplier", 10.0f },
            { "prestigeDeltaMultiplier", 0.30f },
            { "jiltedPenalty", 10.0f },
            { "divorcedPenalty", 20.0f },
            { "playerKillerPenalty", 50.0f },
            { "datingBonus", -15.0f },
            { "loverBonus", -30.0f },
            { "marriedCountMultiplier", 2.0f },
            { "divorcedCountMultiplier", 1.0f },
            { "levelDiffMultiplier", 0.50f },
            { "speechcraftMultiplier", -0.1f },
            { "relationshipRankMultiplier", -3.0f },
            { "affectionMultiplier", 0.50f },
            { "guild_sameGuildBonus", -4.0f },
            { "guildCompanions_outcast", 0.0f },
            { "guildCompanions_poverty", 0.0f },
            { "guildCompanions_working", -2.0f },
            { "guildCompanions_middle", -1.0f },
            { "guildCompanions_wealthy", 0.0f },
            { "guildCompanions_religious", 1.0f },
            { "guildCompanions_nobles", -1.0f },
            { "guildCompanions_rulers", -2.0f },
            { "guildThieves_outcast", -2.0f },
            { "guildThieves_poverty", -1.0f },
            { "guildThieves_working", 0.0f },
            { "guildThieves_middle", 1.0f },
            { "guildThieves_wealthy", 2.0f },
            { "guildThieves_religious", 2.0f },
            { "guildThieves_nobles", 2.0f },
            { "guildThieves_rulers", 3.0f },
            { "guildBrotherhood_outcast", -3.0f },
            { "guildBrotherhood_poverty", -2.0f },
            { "guildBrotherhood_working", 1.0f },
            { "guildBrotherhood_middle", 2.0f },
            { "guildBrotherhood_wealthy", 3.0f },
            { "guildBrotherhood_religious", 4.0f },
            { "guildBrotherhood_nobles", 3.0f },
            { "guildBrotherhood_rulers", 4.0f },
            { "guildCollege_outcast", 1.0f },
            { "guildCollege_poverty", 0.0f },
            { "guildCollege_working", 1.0f },
            { "guildCollege_middle", 0.0f },
            { "guildCollege_wealthy", 1.0f },
            { "guildCollege_religious", -2.0f },
            { "guildCollege_nobles", 0.0f },
            { "guildCollege_rulers", -3.0f },
            { "guildBards_outcast", 1.0f },
            { "guildBards_poverty", 0.0f },
            { "guildBards_working", 0.0f },
            { "guildBards_middle", -1.0f },
            { "guildBards_wealthy", -1.0f },
            { "guildBards_religious", 0.0f },
            { "guildBards_nobles", -2.0f },
            { "guildBards_rulers", -1.0f }
        };
        return cfg;
    }

    float CalculatePlayerPrestige(
        const MarriageDifficultyConfig& config,
        const MarriageDifficultyInputs& inputs) noexcept {
        float result = 0.0f;

        if (inputs.isDragonborn) {
            result += config.GetParam("prestigeDragonbornBonus");
        }

        float thaneHolds = static_cast<float>(inputs.thaneHoldsCount);
        const float maxThaneHolds = config.GetParam("prestigeMaxThaneHolds");
        if (maxThaneHolds > 0.0f) {
            thaneHolds = std::min(thaneHolds, maxThaneHolds);
        }
        result += thaneHolds * config.GetParam("prestigeThaneHoldValue");

        if (inputs.isGuildLeader) {
            result += config.GetParam("prestigeGuildLeaderBonus");
        }

        const float goldDivisor = config.GetParam("prestigeMostGoldDivisor");
        if (goldDivisor > 0.0f) {
            result += std::clamp(inputs.mostGold / goldDivisor, 0.0f, config.GetParam("prestigeMostGoldClampMax"));
        }

        const float houseHorseScore =
            inputs.housesOwned * config.GetParam("prestigeHouseUnitMultiplier") +
            inputs.horsesOwned * config.GetParam("prestigeHorseUnitMultiplier");
        result += std::clamp(houseHorseScore, 0.0f, config.GetParam("prestigeHouseHorseClampMax"));

        const float heroicScore =
            inputs.questsCompleted * config.GetParam("prestigeQuestsMultiplier") +
            inputs.dungeonsCleared * config.GetParam("prestigeDungeonsMultiplier") +
            inputs.dragonSoulsCollected * config.GetParam("prestigeSoulsMultiplier");
        result += std::clamp(heroicScore, 0.0f, config.GetParam("prestigeRenownClampMax"));

        const float minP = config.GetParam("prestigePrestigeClamp_min");
        const float maxP = config.GetParam("prestigePrestigeClamp_max");
        return std::clamp(result, minP, std::max(minP, maxP));
    }

    float CalculateGuildAlignment(
        const MarriageDifficultyConfig& config,
        const MarriageDifficultyInputs& inputs) {
        const std::string spouseClass =
            Foundation::StringUtils::ToLowerStd(Foundation::SocialClassToString(inputs.npcSocialClass));
        const float sameGuildBonus = config.GetParam("guild_sameGuildBonus");

        GuildModifiers modifiers;
        for (std::size_t i = 0; i < static_cast<std::size_t>(GuildIndex::_Count); ++i) {
            const auto& flags = inputs.guilds[i];
            if (!flags.playerInGuild) {
                continue;
            }
            const std::string key = std::string(GuildPrefix(static_cast<GuildIndex>(i))) + "_" + spouseClass;
            modifiers.UpdateWith(config.GetParam(key));
            if (flags.npcInGuild) {
                modifiers.sameGuild += sameGuildBonus;
            }
        }
        return modifiers.GetStrongest() + modifiers.sameGuild;
    }

    MarriageDifficultyResult CalculateMarriageSuccessChance(
        const MarriageDifficultyConfig& config,
        const MarriageDifficultyInputs& inputs) {
        MarriageDifficultyResult out;

        const float socialClassIndex = static_cast<float>(static_cast<int>(inputs.npcSocialClass));
        const float targetPrestige =
            config.GetParam("prestigeTargetBase") +
            config.GetParam("prestigeTargetPerSocialIndexMultiplier") * socialClassIndex;
        out.playerPrestige = CalculatePlayerPrestige(config, inputs);

        float complexity = config.GetParam("initialComplexity");
        complexity += (targetPrestige - out.playerPrestige) * config.GetParam("prestigeDeltaMultiplier");

        if (inputs.isJilted) {
            complexity += config.GetParam("jiltedPenalty");
        }
        if (inputs.isDivorced) {
            complexity += config.GetParam("divorcedPenalty");
        }
        if (inputs.isDating) {
            float datingBonus = config.GetParam("datingBonus", -15.0f);
            if (datingBonus == 0.0f) {
                datingBonus = -15.0f;
            }
            complexity += datingBonus;
        }
        if (inputs.isLover) {
            float loverBonus = config.GetParam("loverBonus", -30.0f);
            if (loverBonus == 0.0f) {
                loverBonus = -30.0f;
            }
            complexity += loverBonus;
        }
        if (inputs.playerKiller) {
            complexity += config.GetParam("playerKillerPenalty");
        }

        complexity += static_cast<float>(inputs.marriedCount) * config.GetParam("marriedCountMultiplier");
        complexity += static_cast<float>(inputs.divorcedCount) * config.GetParam("divorcedCountMultiplier");

        const float levelDiff = static_cast<float>(inputs.npcLevel - inputs.playerLevel);
        const float ldMin = config.GetParam("levelDiffClampMin");
        const float ldMax = config.GetParam("levelDiffClampMax");
        complexity += std::clamp(
            levelDiff * config.GetParam("levelDiffMultiplier"),
            ldMin,
            std::max(ldMin, ldMax));

        complexity += config.GetParam("speechcraftMultiplier") * inputs.playerSpeechcraft;
        complexity += config.GetParam("relationshipRankMultiplier") * static_cast<float>(inputs.relationshipRank);

        out.guildAlignment = CalculateGuildAlignment(config, inputs);
        complexity += out.guildAlignment;
        complexity += inputs.intimacyAdjustment;

        const float affectionAdjustment =
            (static_cast<float>(inputs.permanentAffection) - 50.0f) * config.GetParam("affectionMultiplier");
        complexity -= affectionAdjustment;

        out.complexity = complexity;
        const float diffMin = config.GetParam("difficultyClampMin");
        const float diffMax = config.GetParam("difficultyClampMax", 100.0f);
        out.difficulty = std::clamp(complexity, diffMin, std::max(diffMin, diffMax));

        const float steepness = config.GetParam("sigmoidSteepness");
        const float center = config.GetParam("sigmoidCenter");
        const float exponent = steepness * (out.difficulty - center);
        out.chance = 1.0f / (1.0f + std::exp(exponent));
        return out;
    }

    float ComputeLoversLedgerIntimacyBonus(
        int exclusiveSexCount,
        int partOfSameGroupSexCount,
        int playerTopLoverRank) noexcept {
        float res = 0.0f;
        if (exclusiveSexCount >= 1) {
            res += 10.0f;
        } else if (partOfSameGroupSexCount >= 1) {
            res += 6.0f;
        }
        if (playerTopLoverRank >= 0 && playerTopLoverRank < 3) {
            res += 2.0f * static_cast<float>(3 - playerTopLoverRank);
        }
        return res;
    }

    float ComputeIntimacyAdjustment(
        bool hasMarriedPartner,
        bool isSpouseAlsoLover,
        bool hasEngagedPartner,
        bool isFianceAlsoLover,
        bool hasAnyLover,
        float ttllIntimacyBonus) noexcept {
        float res = 0.0f;
        if (hasMarriedPartner) {
            res += isSpouseAlsoLover ? 30.0f : 25.0f;
        } else if (hasEngagedPartner) {
            res += isFianceAlsoLover ? 20.0f : 15.0f;
        } else if (hasAnyLover) {
            res += 10.0f;
        }
        return res - ttllIntimacyBonus;
    }

    NoAiDialogueChances ComputeNoAiDialogueChances(
        float rawMarriageSuccessChance01,
        bool alwaysSuccessMarriage,
        Foundation::RelationshipStatus playerNpcStatus,
        bool npcHasClosedMainPair,
        bool playerHasClosedMainPair) noexcept {
        if (alwaysSuccessMarriage) {
            return NoAiDialogueChances{ 100.0f, 100.0f, 100.0f };
        }

        float basePct = std::clamp(std::round(rawMarriageSuccessChance01 * 100.0f), 0.0f, 100.0f);
        if (npcHasClosedMainPair || playerHasClosedMainPair) {
            basePct *= 0.6f;
        }
        if (Foundation::IsBreakupStatus(playerNpcStatus)) {
            basePct *= 0.75f;
        }

        float lover = basePct * 1.3f;
        float dating = basePct * 1.15f;
        float marriage = basePct;

        if (playerNpcStatus == Foundation::RelationshipStatus::Lover) {
            marriage *= 1.2f;
            dating *= 1.5f;
        }
        if (playerNpcStatus == Foundation::RelationshipStatus::Dating) {
            marriage *= 1.5f;
        }

        return NoAiDialogueChances{
            std::clamp(dating, 0.0f, 100.0f),
            std::clamp(lover, 0.0f, 100.0f),
            std::clamp(marriage, 0.0f, 100.0f),
        };
    }

}  // namespace MARAS::Domain::Relationships
