#pragma once

#include "maras/foundation/DomainEnums.h"

#include <array>
#include <string>
#include <string_view>
#include <unordered_map>

namespace MARAS::Domain::Relationships {

    using Foundation::SocialClass;

    enum class GuildIndex : std::size_t {
        Companions = 0,
        Thieves = 1,
        Brotherhood = 2,
        College = 3,
        Bards = 4,
        _Count
    };

    struct GuildMembershipFlags {
        bool playerInGuild{ false };
        bool npcInGuild{ false };
    };

    struct MarriageDifficultyConfig {
        std::unordered_map<std::string, float> params{};

        [[nodiscard]] float GetParam(std::string_view key, float fallback = 0.0f) const noexcept;
        [[nodiscard]] static MarriageDifficultyConfig CreateDefault();
    };

    struct MarriageDifficultyInputs {
        int npcLevel{ 1 };
        int playerLevel{ 1 };
        SocialClass npcSocialClass{ SocialClass::Working };

        bool isJilted{ false };
        bool isDivorced{ false };
        bool isDating{ false };
        bool isLover{ false };
        bool playerKiller{ false };

        int marriedCount{ 0 };
        int divorcedCount{ 0 };
        int relationshipRank{ 0 };
        int permanentAffection{ 50 };
        float playerSpeechcraft{ 15.0f };
        float intimacyAdjustment{ 0.0f };

        float mostGold{ 0.0f };
        float housesOwned{ 0.0f };
        float horsesOwned{ 0.0f };
        float questsCompleted{ 0.0f };
        float dungeonsCleared{ 0.0f };
        float dragonSoulsCollected{ 0.0f };

        bool isDragonborn{ false };
        int thaneHoldsCount{ 0 };
        bool isGuildLeader{ false };

        std::array<GuildMembershipFlags, static_cast<std::size_t>(GuildIndex::_Count)> guilds{};
    };

    struct MarriageDifficultyResult {
        float chance{ 0.0f };
        float difficulty{ 0.0f };
        float complexity{ 0.0f };
        float playerPrestige{ 0.0f };
        float guildAlignment{ 0.0f };
    };

    struct NoAiDialogueChances {
        float datingChance{ 0.0f };
        float loverChance{ 0.0f };
        float marriageChance{ 0.0f };
    };

    [[nodiscard]] float CalculatePlayerPrestige(
        const MarriageDifficultyConfig& config,
        const MarriageDifficultyInputs& inputs) noexcept;

    [[nodiscard]] float CalculateGuildAlignment(
        const MarriageDifficultyConfig& config,
        const MarriageDifficultyInputs& inputs);

    [[nodiscard]] MarriageDifficultyResult CalculateMarriageSuccessChance(
        const MarriageDifficultyConfig& config,
        const MarriageDifficultyInputs& inputs);

    [[nodiscard]] float ComputeLoversLedgerIntimacyBonus(
        int exclusiveSexCount,
        int partOfSameGroupSexCount,
        int playerTopLoverRank) noexcept;

    [[nodiscard]] float ComputeIntimacyAdjustment(
        bool hasMarriedPartner,
        bool isSpouseAlsoLover,
        bool hasEngagedPartner,
        bool isFianceAlsoLover,
        bool hasAnyLover,
        float ttllIntimacyBonus) noexcept;

    [[nodiscard]] NoAiDialogueChances ComputeNoAiDialogueChances(
        float rawMarriageSuccessChance01,
        bool alwaysSuccessMarriage,
        Foundation::RelationshipStatus playerNpcStatus,
        bool npcHasClosedMainPair,
        bool playerHasClosedMainPair) noexcept;

}  // namespace MARAS::Domain::Relationships
