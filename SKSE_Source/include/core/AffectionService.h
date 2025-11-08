#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/Serialization.h"

namespace MARAS {

    class AffectionService {
    public:
        using FormID = RE::FormID;

        static AffectionService& GetSingleton();

        // Daily accumulation (called from Papyrus)
        void AddAffection(FormID npcFormID, float amount, const std::string& type);
        float GetDailyAffection(FormID npcFormID, const std::string& type) const;

        // Permanent accessors
        int GetPermanentAffection(FormID npcFormID) const;
        void SetPermanentAffection(FormID npcFormID, int amount);

        // Min/Max per-type clamp configuration
        void SetAffectionMinMax(const std::string& type, int minVal, int maxVal);
        bool HasMinMaxForType(const std::string& type) const;
        std::pair<int, int> GetMinMaxForType(const std::string& type) const;

        // Apply accumulated daily affection to permanent for all registered NPCs
        void ApplyDailyAffectionsForAll();

        // Serialization
        bool Save(SKSE::SerializationInterface* serialization) const;
        bool Load(SKSE::SerializationInterface* serialization);
        void Revert();

    // Affection multiplier helpers
    // Compute multiplier factor from a permanent affection value (0..100)
    float GetMultiplierForValue(int permanentAffection) const;

    // Convenience: get permanent affection for NPC and return its multiplier
    float GetMultiplierForNPC(FormID npcFormID) const;

    private:
        AffectionService() = default;

        // permanent affection per NPC
        std::unordered_map<FormID, int> permanentAffection_;

        // daily affection per NPC -> type -> amount
        std::unordered_map<FormID, std::unordered_map<std::string, float>> dailyAffection_;

        // per-type min/max clamp settings (type -> {min, max})
        std::unordered_map<std::string, std::pair<int, int>> minMaxByType_;
    };

}  // namespace MARAS
