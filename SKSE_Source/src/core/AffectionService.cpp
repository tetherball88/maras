#include "core/AffectionService.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>

#include "core/NPCRelationshipManager.h"
#include "utils/Common.h"
#include "utils/EnumUtils.h"
#include "utils/FormUtils.h"

namespace MARAS {

    AffectionService& AffectionService::GetSingleton() {
        static AffectionService instance;
        return instance;
    }

    void AffectionService::AddAffection(FormID npcFormID, float amount, const std::string& type) {
        if (!Utils::IsValidNPC(npcFormID)) {
            MARAS_LOG_WARN("AddAffection: invalid NPC {:08X}", npcFormID);
            return;
        }

        auto key = Utils::ToLower(type);
        dailyAffection_[npcFormID][key] += amount;
        MARAS_LOG_DEBUG("AddAffection: NPC {:08X} += {} ({}) (daily now {})", npcFormID, amount, key,
                        dailyAffection_[npcFormID][key]);
    }

    float AffectionService::GetDailyAffection(FormID npcFormID, const std::string& type) const {
        auto key = Utils::ToLower(type);
        auto it = dailyAffection_.find(npcFormID);
        if (it == dailyAffection_.end()) return 0.0f;
        auto it2 = it->second.find(key);
        return (it2 == it->second.end()) ? 0.0f : it2->second;
    }

    int AffectionService::GetPermanentAffection(FormID npcFormID) const {
        auto it = permanentAffection_.find(npcFormID);
        return (it == permanentAffection_.end()) ? 0 : it->second;
    }

    void AffectionService::SetPermanentAffection(FormID npcFormID, int amount) {
        if (!Utils::IsValidNPC(npcFormID)) {
            MARAS_LOG_WARN("SetPermanentAffection: invalid NPC {:08X}", npcFormID);
            return;
        }
        // clamp to 0..100
        int clamped = std::clamp(amount, 0, 100);

        // determine old/new thresholds
        auto oldIt = permanentAffection_.find(npcFormID);
        int oldVal = (oldIt == permanentAffection_.end()) ? 0 : oldIt->second;

        auto getThreshold = [](int v) -> std::string {
            if (v >= 75) return "happy";
            if (v >= 50) return "content";
            if (v >= 25) return "troubled";
            return "estranged";
        };

        std::string oldThreshold = getThreshold(oldVal);
        std::string newThreshold = getThreshold(clamped);

        // store
        permanentAffection_[npcFormID] = clamped;
        MARAS_LOG_INFO("SetPermanentAffection: NPC {:08X} = {} (clamped from {})", npcFormID, clamped, amount);

        // Update faction rank for affection faction (0x119 in TT_MARAS.esp)
        auto faction = Utils::LookupForm<RE::TESFaction>(0x119, "TT_MARAS.esp");
        if (faction) {
            // Lookup runtime actor and set faction rank
            auto actor = RE::TESForm::LookupByID<RE::Actor>(npcFormID);
            if (actor) {
                actor->AddToFaction(faction, static_cast<std::int8_t>(clamped));
                MARAS_LOG_DEBUG("SetPermanentAffection: set faction {:08X} rank {} for NPC {:08X}",
                                faction->GetFormID(), clamped, npcFormID);
            } else {
                MARAS_LOG_WARN("SetPermanentAffection: could not find runtime actor for {:08X} to set faction rank",
                               npcFormID);
            }
        } else {
            MARAS_LOG_WARN("SetPermanentAffection: could not find affection faction (0x119) in TT_MARAS.esp");
        }

        // If threshold changed, send mod event to Papyrus (float = diff new-old, signed)
        if (oldThreshold != newThreshold) {
            auto npcForm = RE::TESForm::LookupByID(npcFormID);
            float diff = static_cast<float>(clamped - oldVal);
            auto eventSource = SKSE::GetModCallbackEventSource();
            if (eventSource) {
                SKSE::ModCallbackEvent modEvent("maras_change_affection", newThreshold, diff, npcForm);
                eventSource->SendEvent(&modEvent);
                MARAS_LOG_INFO("Sent maras_change_affection event for NPC {:08X}: threshold={} diff={}", npcFormID,
                               newThreshold, diff);
            } else {
                MARAS_LOG_ERROR("Could not get ModCallbackEventSource to send affection change event");
            }
        }
    }

    void AffectionService::SetAffectionMinMax(const std::string& type, int minVal, int maxVal) {
        auto key = Utils::ToLower(type);
        minMaxByType_[key] = {minVal, maxVal};
        MARAS_LOG_INFO("SetAffectionMinMax: {} -> [{}, {}]", key, minVal, maxVal);
    }

    bool AffectionService::HasMinMaxForType(const std::string& type) const {
        auto key = Utils::ToLower(type);
        return minMaxByType_.find(key) != minMaxByType_.end();
    }

    std::pair<int, int> AffectionService::GetMinMaxForType(const std::string& type) const {
        auto key = Utils::ToLower(type);
        auto it = minMaxByType_.find(key);
        if (it == minMaxByType_.end()) return {INT_MIN, INT_MAX};
        return it->second;
    }

    void AffectionService::ApplyDailyAffectionsForAll() {
        auto& manager = NPCRelationshipManager::GetSingleton();
        auto all = manager.GetAllRegisteredNPCs();

        for (auto npcFormID : all) {
            auto it = dailyAffection_.find(npcFormID);
            if (it == dailyAffection_.end()) continue;

            int original = GetPermanentAffection(npcFormID);
            int deltaTotal = 0;

            for (auto& [type, value] : it->second) {
                // clamp if configured
                int applied = static_cast<int>(std::lround(value));
                auto mmIt = minMaxByType_.find(type);
                if (mmIt != minMaxByType_.end()) {
                    applied = std::clamp(applied, mmIt->second.first, mmIt->second.second);
                }
                deltaTotal += applied;
                MARAS_LOG_DEBUG("ApplyDaily: NPC {:08X} type {} daily {} -> applied {}", npcFormID, type, value,
                                applied);
            }

            if (deltaTotal != 0) {
                int updated = original + deltaTotal;
                SetPermanentAffection(npcFormID, updated);
                MARAS_LOG_INFO("Applied daily affection for NPC {:08X}: {} -> {} (delta {})", npcFormID, original,
                               updated, deltaTotal);
            }

            // clear daily after applying
            it->second.clear();
        }
    }

    bool AffectionService::Save(SKSE::SerializationInterface* serialization) const {
        if (!serialization) return false;

        // Write count
        std::uint32_t count = static_cast<std::uint32_t>(permanentAffection_.size());
        if (!serialization->WriteRecordData(count)) return false;

        for (const auto& [formID, amount] : permanentAffection_) {
            if (!serialization->WriteRecordData(formID) || !serialization->WriteRecordData(amount)) return false;
        }

        MARAS_LOG_INFO("Saved {} permanent affection records", count);
        return true;
    }

    bool AffectionService::Load(SKSE::SerializationInterface* serialization) {
        if (!serialization) return false;

        Revert();

        std::uint32_t count = 0;
        if (!serialization->ReadRecordData(count)) return false;

        for (std::uint32_t i = 0; i < count; ++i) {
            RE::FormID oldFormID = 0, newFormID = 0;
            int amount = 0;
            if (!serialization->ReadRecordData(oldFormID) || !serialization->ReadRecordData(amount)) return false;
            if (!serialization->ResolveFormID(oldFormID, newFormID)) {
                MARAS_LOG_WARN("AffectionService::Load - could not resolve FormID {:08X}, skipping", oldFormID);
                continue;
            }
            permanentAffection_[newFormID] = amount;
        }

        MARAS_LOG_INFO("Loaded {} permanent affection records", permanentAffection_.size());
        return true;
    }

    void AffectionService::Revert() {
        permanentAffection_.clear();
        dailyAffection_.clear();
        minMaxByType_.clear();
        MARAS_LOG_INFO("Reverted affection service state");
    }

    float AffectionService::GetMultiplierForValue(int permanentAffection) const
    {
        if (permanentAffection >= 75) {
            return 1.25f;
        }
        if (permanentAffection >= 50) {
            return 1.0f;
        }
        if (permanentAffection >= 25) {
            return 0.25f;
        }
        return 0.0f;
    }

    float AffectionService::GetMultiplierForNPC(FormID npcFormID) const
    {
        int perm = GetPermanentAffection(npcFormID);
        return GetMultiplierForValue(perm);
    }

}  // namespace MARAS
