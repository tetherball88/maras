#pragma once

#include "PCH.h"

namespace MARAS {

    class PrismaUIDataBridge {
    public:
        static PrismaUIDataBridge& GetSingleton() {
            static PrismaUIDataBridge singleton;
            return singleton;
        }

        // Action Handler (called from PrismaUIManager when JS executes window.marasAction)
        void HandleAction(const char* actionJsonStr);

        // Data Push Methods
        void SendOverviewData();
        void SendSettingsData();
        void SendNPCList();
        void SendNPCDetail(RE::FormID npcFormID);
        void SendMatchmakingData();
        void SendAllSkyrimNPCs();
        void SendMatchmakingResults(const std::vector<struct MatchmakingResult>& results, bool isGlobal = false);
        void SendGeneratedMemories(RE::FormID partnerFormId, const nlohmann::json& memoriesArray);
        void SendMatchmakingSettings();
        void SendHouseholdBuffs();

        // Event Sync Notifications (pushed to open view)
        void OnAffectionChanged(RE::FormID npcFormID, int value);
        void OnStatusChanged(RE::FormID actorA, RE::FormID actorB, uint8_t status);
        void OnHierarchyChanged();
        void OnChildUpdated(const struct ChildRegistryEntry& entry);
        void OnChildRemoved(RE::FormID childID);
        void OnChildPromoted(RE::FormID oldID, RE::FormID newID);
        nlohmann::json BuildChildJson(const struct ChildRegistryEntry& entry);

        void Revert();

        // FormID Serialization Helpers
        static std::string FormIDToString(RE::FormID formID);
        static RE::FormID StringToFormID(const std::string& str);

    private:
        PrismaUIDataBridge() = default;
        ~PrismaUIDataBridge() = default;
        PrismaUIDataBridge(const PrismaUIDataBridge&) = delete;
        PrismaUIDataBridge& operator=(const PrismaUIDataBridge&) = delete;

        // Action Processors (executed on SKSE Main Task Queue)
        void ProcessSetAffection(const nlohmann::json& payload);
        void ProcessSetSocialClass(const nlohmann::json& payload);
        void ProcessSetSkillType(const nlohmann::json& payload);
        void ProcessSetTemperament(const nlohmann::json& payload);
        void ProcessSetPairIsOpen(const nlohmann::json& payload);
        void ProcessSetHierarchyRank(const nlohmann::json& payload);
        void ProcessAssignRelationship(const nlohmann::json& payload);
        void ProcessRemoveRelationship(const nlohmann::json& payload);
        void ProcessUpdateSetting(const nlohmann::json& payload);
        void ProcessSearchNPCs(const nlohmann::json& payload);
        void ProcessTeleportPlayerToNpc(const nlohmann::json& payload);
        void ProcessTeleportNpcToPlayer(const nlohmann::json& payload);
        void ProcessSetChildCritterBanned(const nlohmann::json& payload);
        void ProcessSetChildPetBanned(const nlohmann::json& payload);
        void ProcessDismissChildCritter(const nlohmann::json& payload);
        void ProcessDismissChildPet(const nlohmann::json& payload);
        void ProcessAddChild(const nlohmann::json& payload);
        void ProcessRemoveChild(const nlohmann::json& payload);
        void ProcessUpdateChild(const nlohmann::json& payload);
        void ProcessSpawnChild(const nlohmann::json& payload);
        void ProcessRelocateFamilyUnit(const nlohmann::json& payload);
        void ProcessSetPaternitySecretEdges(const nlohmann::json& payload);
        void ProcessEvaluateChildSecret(const nlohmann::json& payload);
        void ProcessSetNPCCurrentHome(const nlohmann::json& payload);
        void ProcessSetNPCStayAtHome(const nlohmann::json& payload);
        void ProcessDumpData(const nlohmann::json& payload);
        void ProcessDumpNPCIndex(const nlohmann::json& payload);
        void ProcessSkyrimNetMatchSuggest(const nlohmann::json& payload);
        void ProcessSkyrimNetGlobalMatchSuggest(const nlohmann::json& payload);
        void ProcessGenerateMatchMemories(const nlohmann::json& payload);
        void ProcessUpdateMatchmakingSettings(const nlohmann::json& payload);

        // Helper JSON builders
        nlohmann::json BuildNPCProfileJson(RE::FormID formID);
        nlohmann::json BuildSettingsJson();

        // Cached placed child references discovered across indexed forms
        std::unordered_set<RE::FormID> cachedChildRefIDs;
    };

}  // namespace MARAS
