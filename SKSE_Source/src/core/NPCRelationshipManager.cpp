#include "core/NPCRelationshipManager.h"

#include "core/AdultChildProgression.h"
#include "core/AffectionService.h"
#include "core/DeceasedRegistry.h"
#include "core/FactionService.h"
#include "core/FormCache.h"
#include "core/NPCIndex.h"
#include "core/NPCTypeDeterminer.h"
#include "core/PackageOverrideService.h"
#include "core/PlayerChildrenService.h"
#include "core/PlayerHouseService.h"
#include "core/Serialization.h"
#include "core/SkyrimNetIntegration.h"
#include "core/SkyrimNetMemoryService.h"
#include "core/SkyrimNetProfileService.h"
#include "ui/PrismaModalManager.h"
#include "ui/PrismaUIDataBridge.h"
#include "core/SpouseAssetsService.h"
#include "core/SpouseHierarchyManager.h"
#include "core/PerspectiveEngineAdapters.h"
#include "core/WorldStateLegacyAdapters.h"
#include "maras/foundation/IRng.h"
#include "maras/projection/PerspectiveView.h"
#include "maras/state/SpatialActorSnapshot.h"
#include "maras/state/WorldStore.h"
#include "services/ActorService.h"
#include "utils/ActorUtils.h"
#include "utils/Common.h"
#include "utils/EnumUtils.h"
#include "utils/FormUtils.h"
#include "utils/JsonOverrideLoader.h"
#include "utils/JsonService.h"
#include "utils/Settings.h"

#include <RE/E/ExtraLinkedRef.h>
#include <RE/S/SendHUDMessage.h>

#include <cmath>
#include <random>

namespace MARAS {

    namespace Detail {

        class CachedActorMetadataProvider final : public State::IActorMetadataProvider {
        public:
            struct Entry {
                State::ActorSex sex{ State::ActorSex::Unknown };
                bool aliveAndValid{ false };
                bool dynamicPresent{ false };
                std::string name{};
                State::FormRawId raceId{};
                State::BaseId baseId{};
            };

            void Capture(RE::FormID rawId) {
                if (rawId == 0 || entries_.contains(rawId)) {
                    return;
                }
                Entry e{};
                const State::ActorId actorId{ rawId };
                if (actorId.IsUnknownFather() || actorId.IsSyntheticChild()) {
                    entries_.emplace(rawId, std::move(e));
                    return;
                }
                if (actorId.IsPlayer()) {
                    e.aliveAndValid = true;
                    if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                        if (auto* base = player->GetActorBase()) {
                            e.sex = static_cast<State::ActorSex>(static_cast<std::int8_t>(base->GetSex()));
                        }
                        if (auto* race = player->GetRace()) {
                            e.raceId = State::FormRawId{ race->GetFormID() };
                        }
                        const char* nm = player->GetName();
                        e.name = (nm && nm[0] != '\0') ? nm : "Player";
                    } else {
                        e.name = "Player";
                    }
                    e.baseId = State::BaseId{ kPlayerBaseID };
                    entries_.emplace(rawId, std::move(e));
                    return;
                }

                const std::uint8_t s = ActorSvc::GetSex(rawId);
                if (s == 0) {
                    e.sex = State::ActorSex::Male;
                } else if (s == 1) {
                    e.sex = State::ActorSex::Female;
                }

                e.name = ActorSvc::GetName(rawId);
                if (auto* race = ActorSvc::GetRace(rawId)) {
                    e.raceId = State::FormRawId{ race->GetFormID() };
                }
                if (Foundation::IsDynamicRuntimeActorRawId(rawId)) {
                    e.baseId = State::BaseId{ rawId };
                    if (auto* form = RE::TESForm::LookupByID(rawId); form && !form->IsDeleted()) {
                        if (auto* act = form->As<RE::Actor>()) {
                            e.dynamicPresent = true;
                            e.aliveAndValid = !act->IsDead();
                        }
                    }
                } else {
                    RE::FormID bId = NPCIndex::GetSingleton().GetBaseID(rawId);
                    if (bId == 0) {
                        if (auto* base = ActorSvc::GetActorBase(rawId)) {
                            bId = base->GetFormID();
                        }
                    }
                    if (bId != 0) {
                        e.baseId = State::BaseId{ bId };
                    }
                    if (auto* loaded = NPCIndex::GetSingleton().GetActorFromFormId(rawId)) {
                        e.aliveAndValid = !loaded->IsDead() && !loaded->IsDeleted();
                    } else if (auto* form = RE::TESForm::LookupByID(rawId)) {
                        if (!form->IsDeleted()) {
                            if (auto* act = form->As<RE::Actor>()) {
                                e.aliveAndValid = !act->IsDead();
                            } else {
                                e.aliveAndValid = ActorSvc::IsValid(rawId);
                            }
                        }
                    } else {
                        e.aliveAndValid = ActorSvc::IsValid(rawId);
                    }
                }
                entries_.emplace(rawId, std::move(e));
            }

            State::ActorSex GetSex(State::ActorId actor) const noexcept override {
                if (const auto it = entries_.find(actor.raw()); it != entries_.end()) {
                    return it->second.sex;
                }
                return State::ActorSex::Unknown;
            }

            bool IsAliveAndValid(State::ActorId actor) const noexcept override {
                if (const auto it = entries_.find(actor.raw()); it != entries_.end()) {
                    return it->second.aliveAndValid;
                }
                return false;
            }

            bool IsDynamicActorPresent(State::RefId refId) const noexcept override {
                if (const auto it = entries_.find(refId.raw()); it != entries_.end()) {
                    return it->second.dynamicPresent;
                }
                return false;
            }

            std::string GetActorName(State::ActorId actor) const override {
                if (const auto it = entries_.find(actor.raw()); it != entries_.end()) {
                    return it->second.name;
                }
                return {};
            }

            State::FormRawId GetActorRaceId(State::ActorId actor) const noexcept override {
                if (const auto it = entries_.find(actor.raw()); it != entries_.end()) {
                    return it->second.raceId;
                }
                return {};
            }

            State::BaseId GetActorBaseId(State::ActorId actor) const noexcept override {
                if (const auto it = entries_.find(actor.raw()); it != entries_.end()) {
                    return it->second.baseId;
                }
                return {};
            }

        private:
            std::unordered_map<RE::FormID, Entry> entries_{};
        };

        std::unique_ptr<State::IActorMetadataProvider> CaptureActorMetadata(const std::vector<RE::FormID>& actorIDs) {
            auto provider = std::make_unique<CachedActorMetadataProvider>();
            for (const RE::FormID id : actorIDs) {
                provider->Capture(id);
            }
            return provider;
        }

        std::unique_ptr<State::IActorMetadataProvider> CaptureWorldActorMetadata(
            const std::shared_ptr<const State::WorldStateSnapshot>& snap,
            const std::vector<RE::FormID>& extraActorIDs) {
            auto provider = std::make_unique<CachedActorMetadataProvider>();
            provider->Capture(kPlayerFormID);
            for (const RE::FormID id : extraActorIDs) {
                provider->Capture(id);
            }
            if (snap) {
                for (const auto& [refId, prof] : snap->actors->AllProfiles()) {
                    (void)prof;
                    provider->Capture(refId.raw());
                }
                for (const auto& [pair, edge] : snap->relationships->AllEdges()) {
                    (void)edge;
                    provider->Capture(pair.first.raw());
                    provider->Capture(pair.second.raw());
                }
                for (const auto& [cId, rec] : snap->lineage->AllChildren()) {
                    (void)cId;
                    provider->Capture(rec.childId.raw());
                    provider->Capture(rec.guardians.slotA.raw());
                    provider->Capture(rec.guardians.slotB.raw());
                    provider->Capture(rec.bioMother.raw());
                    provider->Capture(rec.bioFather.raw());
                    provider->Capture(rec.presumedFather.raw());
                }
            }
            return provider;
        }

        Foundation::DeterministicRng MakeEngineRng() {
            std::random_device rd;
            const std::uint32_t seed =
                static_cast<std::uint32_t>(rd()) ^ (static_cast<std::uint32_t>(rd()) << 16);
            return Foundation::DeterministicRng(seed != 0 ? seed : 0x517CC1B7u);
        }

    }  // namespace Detail

    RE::FormID NPCRelationshipManager::DetermineDefaultInitiator(RE::FormID actorA, RE::FormID actorB) {
        auto getSex = [](RE::FormID id) -> uint8_t {
            if (id == kPlayerFormID) {
                if (auto player = RE::PlayerCharacter::GetSingleton()) {
                    if (auto base = player->GetActorBase()) {
                        return static_cast<uint8_t>(base->GetSex());
                    }
                }
            }
            return ActorSvc::GetSex(id);
        };

        uint8_t sexA = getSex(actorA);
        uint8_t sexB = getSex(actorB);

        // 0 = Male, 1 = Female
        if (sexA == 0 && sexB != 0) return actorA;
        if (sexB == 0 && sexA != 0) return actorB;
        return (actorA != 0) ? actorA : actorB;
    }

    NPCRelationshipManager& NPCRelationshipManager::GetSingleton() {
        static NPCRelationshipManager instance;
        return instance;
    }

    // Private helper methods
    void NPCRelationshipManager::SetLinkedRefForHomeMarker(RE::FormID npcFormID, RE::FormID markerFormID) {
        auto homeSandboxKeyword = FormCache::GetSingleton().GetHomeSandboxKeyword();
        if (!Utils::SetLinkedRef(npcFormID, markerFormID, homeSandboxKeyword)) {
            MARAS_LOG_WARN("Failed to set linked ref for NPC {:08X}: marker={:08X}, keyword={}",
                          npcFormID, markerFormID, homeSandboxKeyword != nullptr);
        }
    }

    void NPCRelationshipManager::UpdateTrackedFactionRank(RE::FormID npcFormID, RelationshipStatus status) {
        auto trackedFaction = FormCache::GetSingleton().GetTrackedFaction();
        if (!trackedFaction) {
            return;
        }

        std::int8_t rank = static_cast<std::int8_t>(status);
        AddToFaction(npcFormID, trackedFaction, rank);

        MARAS_LOG_INFO("Set tracked faction rank {} for NPC {:08X} (status: {})", rank, npcFormID,
                       Utils::RelationshipStatusToString(status));
    }

    void NPCRelationshipManager::SendStatusChangedEvent(RE::FormID npcFormID, RE::FormID targetFormID, RelationshipStatus status) {
        RE::Actor* npcActor = RE::TESForm::LookupByID<RE::Actor>(npcFormID);
        if (!npcActor) {
            npcActor = NPCIndex::GetSingleton().GetActorFromFormId(npcFormID);
        }
        if (!npcActor) {
            MARAS_LOG_DEBUG("SendStatusChangedEvent: Actor {:08X} is not currently loaded; dispatching events with null sender",
                            npcFormID);
        }
        RE::TESForm* senderForm = npcActor ? static_cast<RE::TESForm*>(npcActor) : nullptr;

        std::int32_t statusEnum = static_cast<std::int32_t>(status);
        std::string statusString(Utils::RelationshipStatusToString(status));

        std::transform(statusString.begin(), statusString.end(), statusString.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        auto eventSource = SKSE::GetModCallbackEventSource();
        if (eventSource) {
            SKSE::ModCallbackEvent modEvent("maras_status_changed", statusString, static_cast<float>(statusEnum),
                                            senderForm);
            eventSource->SendEvent(&modEvent);
            MARAS_LOG_INFO("Sent maras_status_changed event for NPC {:08X} (target {:08X}): status={} ({})",
                           npcFormID, targetFormID, statusEnum, statusString);

            nlohmann::json fullJson;
            fullJson["initiator"] = fmt::format("0x{:08X}", npcFormID);
            fullJson["acceptor"] = fmt::format("0x{:08X}", targetFormID);
            fullJson["initiatorName"] = ActorSvc::GetName(npcFormID);
            fullJson["acceptorName"] = ActorSvc::GetName(targetFormID);
            fullJson["status"] = statusString;

            std::string fullJsonStr = fullJson.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            SKSE::ModCallbackEvent fullModEvent("maras_status_changed_full", fullJsonStr, static_cast<float>(statusEnum),
                                                senderForm);
            eventSource->SendEvent(&fullModEvent);
            MARAS_LOG_INFO("Sent maras_status_changed_full event: {}", fullJsonStr);
        } else {
            MARAS_LOG_ERROR("Could not get ModCallbackEventSource to send event");
        }
    }

    void NPCRelationshipManager::DispatchPlayerBuffMaintenance() const {
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm) {
            return;
        }
        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> nullCallback;
        vm->DispatchStaticCall("TTM_ServiceBuff", "Maintenance", RE::MakeFunctionArguments(), nullCallback);
        MARAS_LOG_DEBUG("Dispatched TTM_ServiceBuff.Maintenance() to synchronize player marriage buffs");
    }

    size_t NPCRelationshipManager::CountPartnersWithStatus(RE::FormID npcFormID, RelationshipStatus status) const {
        if (npcFormID == 0) return 0;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        return snap->relationships->GetPartnersWithStatus(
            State::ActorId{ npcFormID },
            static_cast<State::RelationshipStatus>(status)).size();
    }

    void NPCRelationshipManager::RefreshPartnerRelationshipFactions(RE::FormID partnerID) {
        if (partnerID == 0) return;

        if (partnerID == kPlayerFormID) {
            if (GetMarriedCount(kPlayerFormID) == 0) {
                if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                    if (auto* marriedFaction = FormCache::GetSingleton().GetMarriedFaction()) {
                        FactionService::InternalFactionScope scope;
                        Utils::RemoveFromFaction(player, marriedFaction);
                    }
                }
            }
            UpdateCanFormNonCheatingMainFaction(kPlayerFormID);
            return;
        }

        if (!IsRegistered(partnerID)) {
            return;
        }

        RelationshipStatus playerStatus = GetRelationshipStatus(partnerID, kPlayerFormID);
        if (playerStatus != RelationshipStatus::Unknown) {
            UpdateTrackedFactionRank(partnerID, playerStatus);
            ManageFactions(partnerID, kPlayerFormID, playerStatus);
        }

        UpdateSpouseCheatingFaction(partnerID);
        UpdateSpouseOpenRelationshipFaction(partnerID);
        UpdateCanFormNonCheatingMainFaction(partnerID);
        UpdateSpouseChildrenFaction(partnerID);
    }

    bool NPCRelationshipManager::IsMainTier(RelationshipStatus status) {
        return status == RelationshipStatus::Dating ||
               status == RelationshipStatus::Engaged ||
               status == RelationshipStatus::Married;
    }

    bool NPCRelationshipManager::IsActivePartnerStatus(RelationshipStatus status) {
        return IsMainTier(status) || status == RelationshipStatus::Lover;
    }

    bool NPCRelationshipManager::IsBreakupStatus(RelationshipStatus status) {
        return status == RelationshipStatus::Divorced ||
               status == RelationshipStatus::Jilted ||
               status == RelationshipStatus::ExDating ||
               status == RelationshipStatus::ExLover ||
               status == RelationshipStatus::Candidate;
    }

    bool NPCRelationshipManager::HasAnyOpenMainPair(RE::FormID actorID) const {
        if (actorID == 0) return false;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        return snap->relationships->HasAnyOpenMainPair(State::ActorId{ actorID });
    }

    int NPCRelationshipManager::BreakAllLoverConnections(RE::FormID actorID, bool onlyCheating, bool sendEvent) {
        if (actorID == 0) return 0;
        std::vector<RE::FormID> loversToBreak;
        {
            const auto snap = State::WorldStore::GetSingleton().Snapshot();
            const State::ActorId actor{ actorID };
            for (const auto& key : snap->relationships->GetAdjacentPairs(actor)) {
                const auto* edge = snap->relationships->FindEdge(key);
                if (!edge || edge->status != State::RelationshipStatus::Lover) continue;
                const RE::FormID partnerID = key.Other(actor).raw();
                if (partnerID == 0) continue;

                if (onlyCheating) {
                    if (IsRelationshipCheating(actorID, partnerID)) {
                        loversToBreak.push_back(partnerID);
                    }
                } else {
                    loversToBreak.push_back(partnerID);
                }
            }
        }

        int brokenCount = 0;
        for (RE::FormID partnerID : loversToBreak) {
            if (PromoteToExLover(actorID, partnerID, sendEvent)) {
                ++brokenCount;
            }
        }
        MARAS_LOG_INFO("BreakAllLoverConnections for {:08X}: brokenCount={}, onlyCheating={}", actorID, brokenCount, onlyCheating);
        return brokenCount;
    }

    bool NPCRelationshipManager::ChangeStatusCommon(RE::FormID npcFormID, RE::FormID targetFormID, RelationshipStatus status,
                                                    const std::function<void(RE::FormID, RE::FormID)>& postAction,
                                                    bool sendEvent) {
        if (npcFormID != kPlayerFormID && npcFormID != 0 && npcFormID != 0x07) {
            if (!EnsureRegistered(npcFormID)) {
                MARAS_LOG_WARN("Cannot change status for unregistered NPC {:08X}", npcFormID);
                return false;
            }
        }

        if (targetFormID != kPlayerFormID && targetFormID != 0 && targetFormID != 0x07) {
            if (!EnsureRegistered(targetFormID)) {
                MARAS_LOG_WARN("Cannot change status for unregistered NPC {:08X}", targetFormID);
                return false;
            }
        }

        const State::ActorId actorA{ npcFormID };
        const State::ActorId actorB{ targetFormID };
        const State::CanonicalPairKey pairKey{ actorA, actorB };

        RelationshipStatus oldStatus = RelationshipStatus::Candidate;
        {
            const auto snap = State::WorldStore::GetSingleton().Snapshot();
            const auto* existingEdge = snap->relationships->FindEdge(pairKey);
            oldStatus = existingEdge ? static_cast<RelationshipStatus>(existingEdge->status) : RelationshipStatus::Candidate;
            if (existingEdge != nullptr && oldStatus == status) {
                MARAS_LOG_INFO("ChangeStatusCommon skipped for {:08X} / {:08X}: already {}",
                               npcFormID, targetFormID, Utils::RelationshipStatusToString(status));
                return false;
            }
        }

        if (oldStatus != status) {
            std::string blockReason;
            if (!CanFormRelationship(npcFormID, targetFormID, status, &blockReason)) {
                MARAS_LOG_WARN("ChangeStatusCommon blocked for {:08X} / {:08X} ({}) -> {}",
                               npcFormID, targetFormID, Utils::RelationshipStatusToString(status), blockReason);
                if (!blockReason.empty() && (npcFormID == kPlayerFormID || targetFormID == kPlayerFormID)) {
                    RE::SendHUDMessage::ShowHUDMessage(blockReason.c_str());
                }
                return false;
            }
        }

        auto* calendar = RE::Calendar::GetSingleton();
        const float currentDays = calendar ? calendar->GetDaysPassed() : 0.0f;
        RE::FormID initiator = 0;
        if (currentDays >= 0.0f) {
            if (npcFormID == kPlayerFormID || targetFormID == kPlayerFormID) {
                initiator = kPlayerFormID;
            } else if (npcFormID != 0) {
                initiator = npcFormID;
            } else {
                initiator = DetermineDefaultInitiator(npcFormID, targetFormID);
            }
        } else {
            initiator = DetermineDefaultInitiator(npcFormID, targetFormID);
        }

        bool skippedDuplicate = false;
        bool recordedTransition = false;
        bool appliedAutoOpen = false;
        bool resetOpenOnBreakup = false;

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            auto& rels = ws.relationships.Write();
            const auto* preEdge = rels.FindEdge(pairKey);
            const RelationshipStatus preStatus =
                preEdge ? static_cast<RelationshipStatus>(preEdge->status) : RelationshipStatus::Candidate;
            if (preEdge != nullptr && preStatus == status) {
                skippedDuplicate = true;
                return;
            }

            const bool openMainExists =
                (preStatus != status && IsMainTier(status)) &&
                (rels.HasAnyOpenMainPair(actorA) || rels.HasAnyOpenMainPair(actorB));

            auto& edge = rels.GetOrCreateEdge(actorA, actorB);
            const auto targetStateStatus = static_cast<State::RelationshipStatus>(status);

            if (preStatus != status) {
                recordedTransition = true;
                edge.statusHistory.push_back(State::StatusTransitionEntry{
                    .fromStatus = static_cast<State::RelationshipStatus>(preStatus),
                    .toStatus = targetStateStatus,
                    .timestamp = currentDays,
                    .initiator = State::ActorId{ initiator },
                });

                if (IsMainTier(status) && !edge.isOpen && openMainExists) {
                    edge.isOpen = true;
                    appliedAutoOpen = true;
                } else if (IsBreakupStatus(status) && edge.isOpen) {
                    edge.isOpen = false;
                    resetOpenOnBreakup = true;
                }
            }

            edge.status = targetStateStatus;
            oldStatus = preStatus;
        });

        if (skippedDuplicate) {
            MARAS_LOG_INFO("ChangeStatusCommon skipped for {:08X} / {:08X}: already {}",
                           npcFormID, targetFormID, Utils::RelationshipStatusToString(status));
            return false;
        }

        if (recordedTransition) {
            if (status == RelationshipStatus::Engaged && npcFormID != kPlayerFormID && targetFormID != kPlayerFormID && npcFormID != 0 && targetFormID != 0) {
                MARAS_LOG_INFO("NPC-NPC pair {:08X} - {:08X} became engaged at game day {:.2f}; scheduled for auto-marriage in 5 days",
                               npcFormID, targetFormID, currentDays);
            }
            if (appliedAutoOpen) {
                MARAS_LOG_INFO("Forward-only auto-open applied for pair {:08X} - {:08X}", npcFormID, targetFormID);
            } else if (resetOpenOnBreakup) {
                MARAS_LOG_INFO("Reset isOpen to false on breakup ({}) for pair {:08X} - {:08X}",
                               Utils::RelationshipStatusToString(status), npcFormID, targetFormID);
            }
        }

        if (targetFormID == kPlayerFormID) {
            if (oldStatus != status && IsActivePartnerStatus(status) && npcFormID != 0 && npcFormID != kPlayerFormID) {
                AffectionService::GetSingleton().RecordAffectionInteraction(npcFormID);
            }
            UpdateTrackedFactionRank(npcFormID, status);
            UpdateSpouseCheatingFaction(npcFormID);
            UpdateSpouseOpenRelationshipFaction(npcFormID);
            UpdateCanFormNonCheatingMainFaction(npcFormID);
            UpdateCanFormNonCheatingMainFaction(kPlayerFormID);
            ManageFactions(npcFormID, kPlayerFormID, status);
        } else if (npcFormID == kPlayerFormID) {
            if (oldStatus != status && IsActivePartnerStatus(status) && targetFormID != 0 && targetFormID != kPlayerFormID) {
                AffectionService::GetSingleton().RecordAffectionInteraction(targetFormID);
            }
            UpdateTrackedFactionRank(targetFormID, status);
            UpdateSpouseCheatingFaction(targetFormID);
            UpdateSpouseOpenRelationshipFaction(targetFormID);
            UpdateCanFormNonCheatingMainFaction(targetFormID);
            UpdateCanFormNonCheatingMainFaction(kPlayerFormID);
            ManageFactions(targetFormID, kPlayerFormID, status);
        } else {
            UpdateCanFormNonCheatingMainFaction(npcFormID);
            UpdateCanFormNonCheatingMainFaction(targetFormID);
            ManageFactions(npcFormID, targetFormID, status);
            ManageFactions(targetFormID, npcFormID, status);
        }

        if (sendEvent) {
            SendStatusChangedEvent(npcFormID, targetFormID, status);
        }

        MARAS_LOG_INFO("Set status between {:08X} and {:08X} to {}", npcFormID, targetFormID,
                       Utils::RelationshipStatusToString(status));
        EvaluateCheatingOnStatusChange(npcFormID, targetFormID, status);
        if ((sendEvent || SkyrimNetMemoryService::IsMatchmakingApplyActive()) &&
            oldStatus != status &&
            status != RelationshipStatus::Candidate && status != RelationshipStatus::Unknown) {
            SkyrimNetMemoryService::GetSingleton().RecordRelationshipStatusChange(
                npcFormID, targetFormID, oldStatus, status);
        }

        if (postAction) {
            try {
                postAction(npcFormID, targetFormID);
            } catch (const std::exception& e) {
                MARAS_LOG_ERROR("Exception in ChangeStatusCommon postAction for {:08X} / {:08X}: {}",
                               npcFormID, targetFormID, e.what());
            }
        }

        RecalculateAndUpdateGlobals();
        if (oldStatus != status && (npcFormID == kPlayerFormID || targetFormID == kPlayerFormID)) {
            RE::FormID partnerID = (npcFormID == kPlayerFormID) ? targetFormID : npcFormID;
            RE::Actor* partnerActor = RE::TESForm::LookupByID<RE::Actor>(partnerID);
            if (!partnerActor) {
                partnerActor = NPCIndex::GetSingleton().GetActorFromFormId(partnerID);
            }

            // When Papyrus MakeNpc* will not run (either sendEvent == false or partnerActor is unloaded),
            // synchronize permanent affection in C++ so unloaded promotions/breakups never retain stale affection.
            if (partnerID != 0 && partnerID != kPlayerFormID && (!sendEvent || !partnerActor)) {
                auto& affSvc = AffectionService::GetSingleton();
                switch (status) {
                    case RelationshipStatus::Married:
                    case RelationshipStatus::Engaged:
                        affSvc.SetPermanentAffection(partnerID, 100, /*sendEvent=*/false);
                        break;
                    case RelationshipStatus::Dating:
                        if (affSvc.GetPermanentAffection(partnerID) < 80) {
                            affSvc.SetPermanentAffection(partnerID, 80, /*sendEvent=*/false);
                        }
                        break;
                    case RelationshipStatus::Lover:
                        if (affSvc.GetPermanentAffection(partnerID) < 70) {
                            affSvc.SetPermanentAffection(partnerID, 70, /*sendEvent=*/false);
                        }
                        break;
                    case RelationshipStatus::Divorced:
                    case RelationshipStatus::Jilted:
                    case RelationshipStatus::ExDating:
                    case RelationshipStatus::ExLover:
                        affSvc.SetPermanentAffection(partnerID, 0, /*sendEvent=*/false);
                        break;
                    case RelationshipStatus::Candidate:
                        if (oldStatus != RelationshipStatus::Unknown) {
                            affSvc.SetPermanentAffection(partnerID, 50, /*sendEvent=*/false);
                        }
                        break;
                    default:
                        break;
                }
            }

            if (oldStatus == RelationshipStatus::Married || status == RelationshipStatus::Married) {
                const bool papyrusHandlesBuffSync =
                    sendEvent &&
                    ((partnerActor != nullptr &&
                      (status == RelationshipStatus::Married || status == RelationshipStatus::Divorced)) ||
                     (partnerActor == nullptr &&
                      (status == RelationshipStatus::Married || status == RelationshipStatus::Divorced ||
                       status == RelationshipStatus::Candidate)));
                if (!papyrusHandlesBuffSync) {
                    DispatchPlayerBuffMaintenance();
                }
            }
        }
        PrismaUIDataBridge::GetSingleton().OnStatusChanged(npcFormID, targetFormID, static_cast<uint8_t>(status));
        return true;
    }

    // Registration and unregistration
    bool NPCRelationshipManager::RegisterAsCandidate(RE::FormID inputFormID) {
        MARAS_LOG_INFO("RegisterAsCandidate entered: inputFormID={:08X}", inputFormID);
        if (inputFormID == kPlayerFormID || inputFormID == 0 || inputFormID == 0x07) {
            return false;
        }

        auto& npcIndex = NPCIndex::GetSingleton();
        RE::FormID npcFormID = npcIndex.GetRefID(inputFormID);
        if (npcFormID == 0) {
            npcFormID = inputFormID;
        }

        if (npcFormID == kPlayerFormID || npcFormID == 0 || npcFormID == 0x07) {
            return false;
        }

        if (!EnsureRegistered(npcFormID)) {
            return false;
        }

        RelationshipStatus currentStatus = GetRelationshipStatus(npcFormID, kPlayerFormID);
        if (currentStatus != RelationshipStatus::Unknown && currentStatus != RelationshipStatus::Candidate) {
            MARAS_LOG_INFO("RegisterAsCandidate: NPC {:08X} already has relationship status {} with player",
                           npcFormID, Utils::RelationshipStatusToString(currentStatus));
            UpdateTrackedFactionRank(npcFormID, currentStatus);
            return false;
        }

        return DemoteToCandidate(npcFormID, kPlayerFormID, /*sendEvent=*/true);
    }

    bool NPCRelationshipManager::UnregisterNPC(MARAS::FormID npcFormID) {
        if (!IsRegistered(npcFormID)) {
            MARAS_LOG_WARN("NPC {:08X} is not registered", npcFormID);
            return false;
        }

        const bool wasMarriedToPlayer = (GetRelationshipStatus(npcFormID, kPlayerFormID) == RelationshipStatus::Married);
        std::unordered_set<RE::FormID> survivingPartners;
        {
            const auto snap = State::WorldStore::GetSingleton().Snapshot();
            const State::ActorId actor{ npcFormID };
            for (const auto& key : snap->relationships->GetAdjacentPairs(actor)) {
                const RE::FormID otherID = key.Other(actor).raw();
                if (otherID != 0) {
                    survivingPartners.insert(otherID);
                }
            }
        }

        CleanupActorIntentsAndThirdPartyAffairs(npcFormID, /*eraseRecords=*/true, survivingPartners);

        SpouseHierarchyManager::GetSingleton().OnSpouseRemoved(npcFormID);
        SpouseAssetsService::GetSingleton().StopShareHouseWithPlayer(npcFormID);
        (void)PlayerHouseService::GetSingleton().RemoveTenantFromPlayerHouse(npcFormID);
        AffectionService::GetSingleton().RemoveNPCData(npcFormID);

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            const State::ActorId actor{ npcFormID };
            for (const State::ActorId affected : ws.affairs.Write().EraseAllInvolvingActor(actor)) {
                if (affected.raw() != 0 && affected.raw() != npcFormID) {
                    survivingPartners.insert(affected.raw());
                }
            }
            const auto pairsCopy = ws.relationships->GetAdjacentPairs(actor);
            auto& rels = ws.relationships.Write();
            for (const auto& pair : pairsCopy) {
                const RE::FormID otherID = pair.Other(actor).raw();
                if (otherID != 0) {
                    survivingPartners.insert(otherID);
                }
                rels.EraseEdge(pair);
            }
            rels.ClearIntentsInvolving(actor);
            ws.cooldowns.Write().EraseActor(actor);
            if (actor.HasPhysicalActor()) {
                ws.actors.Write().Erase(actor.AsRefId());
            }
        });

        // Clean up engine factions on the unregistered NPC
        {
            FactionService::InternalFactionScope scope;
            auto& cache = FormCache::GetSingleton();
            Utils::RemoveFromFaction(npcFormID, cache.GetTrackedFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetSpouseSocialClassFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetSpouseSkillTypeFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetSpouseTemperamentFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetSpouseCheatingFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetSpouseOpenRelationshipFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetCanFormNonCheatingMainFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetSpouseChildrenFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetMarriageAskedFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetCourtingFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetMarriedFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetPlayerFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetPlayerBedOwnershipFaction());
        }

        for (RE::FormID partnerID : survivingPartners) {
            RefreshPartnerRelationshipFactions(partnerID);
        }

        RecalculateAndUpdateGlobals();
        if (wasMarriedToPlayer) {
            DispatchPlayerBuffMaintenance();
        }
        MARAS_LOG_INFO("Unregistered NPC {} ({:08X})", ActorSvc::GetName(npcFormID), npcFormID);
        return true;
    }

    bool NPCRelationshipManager::HandleNPCDeath(MARAS::FormID npcFormID) {
        if (!IsRegistered(npcFormID)) {
            MARAS_LOG_WARN("HandleNPCDeath: NPC {:08X} is not registered", npcFormID);
            return false;
        }

        const bool wasMarriedToPlayer = (GetRelationshipStatus(npcFormID, kPlayerFormID) == RelationshipStatus::Married);

        // Evaluate custody of any children left behind by the deceased parent
        EvaluateChildCustodyOnDeath(npcFormID);

        std::unordered_set<RE::FormID> survivingPartners;
        {
            const auto snap = State::WorldStore::GetSingleton().Snapshot();
            const State::ActorId actor{ npcFormID };
            for (const auto& key : snap->relationships->GetAdjacentPairs(actor)) {
                const RE::FormID otherID = key.Other(actor).raw();
                if (otherID != 0) {
                    survivingPartners.insert(otherID);
                }
            }
        }

        CleanupActorIntentsAndThirdPartyAffairs(npcFormID, /*eraseRecords=*/false, survivingPartners);

        SpouseHierarchyManager::GetSingleton().OnSpouseRemoved(npcFormID);
        SpouseAssetsService::GetSingleton().StopShareHouseWithPlayer(npcFormID);
        (void)PlayerHouseService::GetSingleton().RemoveTenantFromPlayerHouse(npcFormID);
        AffectionService::GetSingleton().RemoveNPCData(npcFormID);

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            const State::ActorId actor{ npcFormID };
            const auto pairsCopy = ws.relationships->GetAdjacentPairs(actor);
            auto& rels = ws.relationships.Write();
            for (const auto& pair : pairsCopy) {
                const RE::FormID otherID = pair.Other(actor).raw();
                if (otherID != 0) {
                    survivingPartners.insert(otherID);
                }
                rels.EraseEdge(pair);
            }
            rels.ClearIntentsInvolving(actor);
            ws.cooldowns.Write().EraseActor(actor);
            if (actor.HasPhysicalActor()) {
                ws.actors.Write().Erase(actor.AsRefId());
            }
        });

        // Clean up engine factions on the deceased NPC
        {
            FactionService::InternalFactionScope scope;
            auto& cache = FormCache::GetSingleton();
            Utils::RemoveFromFaction(npcFormID, cache.GetTrackedFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetSpouseSocialClassFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetSpouseSkillTypeFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetSpouseTemperamentFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetSpouseCheatingFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetSpouseOpenRelationshipFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetCanFormNonCheatingMainFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetSpouseChildrenFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetMarriageAskedFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetCourtingFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetMarriedFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetPlayerFaction());
            Utils::RemoveFromFaction(npcFormID, cache.GetPlayerBedOwnershipFaction());
        }

        for (RE::FormID partnerID : survivingPartners) {
            RefreshPartnerRelationshipFactions(partnerID);
        }

        RecalculateAndUpdateGlobals();
        if (wasMarriedToPlayer) {
            DispatchPlayerBuffMaintenance();
        }
        MARAS_LOG_INFO("HandleNPCDeath: Cleaned up active state for deceased NPC {} ({:08X})",
                       ActorSvc::GetName(npcFormID), npcFormID);
        return true;
    }

    std::vector<std::pair<RE::FormID, RelationshipStatus>> NPCRelationshipManager::GetActiveRelationships(RE::FormID actorID) const {
        std::vector<std::pair<RE::FormID, RelationshipStatus>> result;
        if (actorID == 0) return result;

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const State::ActorId actor{ actorID };
        for (const auto& key : snap->relationships->GetAdjacentPairs(actor)) {
            const auto* edge = snap->relationships->FindEdge(key);
            if (!edge) continue;
            const auto status = static_cast<RelationshipStatus>(edge->status);
            if (IsActivePartnerStatus(status)) {
                const RE::FormID partnerID = key.Other(actor).raw();
                if (partnerID != 0) {
                    result.emplace_back(partnerID, status);
                }
            }
        }
        return result;
    }

    void NPCRelationshipManager::TryResolvePendingChildren(RE::FormID baseID, RE::FormID refID) {
        if (baseID == 0 || refID == 0) return;

        auto resolveBioParents = [](RE::FormID guardianA, RE::FormID guardianB, bool isAdopted) -> std::pair<RE::FormID, RE::FormID> {
            if (isAdopted) {
                return { 0, 0 };
            }
            RE::FormID bioMother = 0;
            RE::FormID bioFather = 0;
            auto assignParentBySex = [&](RE::FormID parentID) {
                if (parentID == 0) return;
                uint8_t sex = ActorSvc::GetSex(parentID);
                if (sex == 1 && bioMother == 0) {
                    bioMother = parentID;
                } else if (sex == 0 && bioFather == 0) {
                    bioFather = parentID;
                }
            };
            assignParentBySex(guardianA);
            assignParentBySex(guardianB);
            return { bioMother, bioFather };
        };

        // Snapshot pending children before mutating WorldStore / registering actors
        std::optional<PendingChildData> matchingSelfPending;
        std::vector<PendingChildData> parentPendingSnapshot;
        {
            const auto snap = State::WorldStore::GetSingleton().Snapshot();
            const auto& allPending = snap->lineage->AllPendingChildren();
            if (const auto it = allPending.find(State::BaseId{ baseID }); it != allPending.end()) {
                matchingSelfPending = LegacyAdapters::ToLegacyPendingChild(it->second);
            }
            for (const auto& [cBase, entry] : allPending) {
                if (entry.childBaseId.raw() != baseID &&
                    (entry.parentABaseId.raw() == baseID || entry.parentBBaseId.raw() == baseID)) {
                    parentPendingSnapshot.push_back(LegacyAdapters::ToLegacyPendingChild(entry));
                }
            }
        }

        // 1. Check if incoming actor is a pending child
        auto& npcIndex = NPCIndex::GetSingleton();
        SkyrimNetMemoryService::SuppressScope suppressMemories;
        if (matchingSelfPending.has_value()) {
            PendingChildData pending = *matchingSelfPending;
            pending.childRefID = refID;

            RE::FormID pARef = pending.parentARefID != 0 ? pending.parentARefID : npcIndex.GetRefID(pending.parentABaseID);
            RE::FormID pBRef = pending.parentBRefID != 0 ? pending.parentBRefID : npcIndex.GetRefID(pending.parentBBaseID);

            if (pARef == 0 && pending.parentABaseID == kPlayerFormID) pARef = kPlayerFormID;
            if (pBRef == 0 && pending.parentBBaseID == kPlayerFormID) pBRef = kPlayerFormID;

            pending.parentARefID = pARef;
            pending.parentBRefID = pBRef;

            if (pARef != 0 || pBRef != 0) {
                const bool awaitingOtherParent = (pending.parentABaseID != 0 && pARef == 0) ||
                                                 (pending.parentBBaseID != 0 && pBRef == 0);
                State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                    if (awaitingOtherParent) {
                        ws.lineage.Write().AddPendingChild(State::BaseId{ baseID }, LegacyAdapters::ToPendingChildEntry(pending));
                    } else {
                        (void)ws.lineage.Write().ErasePendingChild(State::BaseId{ baseID });
                    }
                });

                auto [bioMother, bioFather] = resolveBioParents(pARef, pBRef, pending.isAdopted);
                RE::FormID gA = pARef;
                RE::FormID gB = pBRef;
                if (!awaitingOtherParent && ((gA != 0 && gB == 0) || (gA == 0 && gB != 0))) {
                    const RE::FormID soleG = (gA != 0) ? gA : gB;
                    const RE::FormID spouseID = GetPrimaryPartner(soleG);
                    if (spouseID != 0 && IsMarried(soleG, spouseID)) {
                        gA = soleG;
                        gB = spouseID;
                    }
                }
                RegisterChildActor(refID, gA, gB, 0, bioMother, bioFather, 0, static_cast<uint8_t>(ChildStage::Child), pending.isAdopted, /*sendEvent=*/false);
                MARAS_LOG_INFO("TryResolvePendingChildren: Resolved pending child {:08X} for guardians {:08X} / {:08X} (bioM: {:08X}, bioF: {:08X}, adopted: {})",
                               refID, gA, gB, bioMother, bioFather, pending.isAdopted);
            } else {
                State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                    ws.lineage.Write().AddPendingChild(State::BaseId{ baseID }, LegacyAdapters::ToPendingChildEntry(pending));
                });
            }
        }

        // 2. Check if incoming actor is a parent of any pending children
        for (PendingChildData pending : parentPendingSnapshot) {
            if (pending.parentABaseID == baseID) {
                pending.parentARefID = refID;
            }
            if (pending.parentBBaseID == baseID) {
                pending.parentBRefID = refID;
            }

            RE::FormID cRef = pending.childRefID != 0 ? pending.childRefID : npcIndex.GetRefID(pending.childBaseID);
            pending.childRefID = cRef;

            if (cRef != 0 && (pending.parentARefID != 0 || pending.parentBRefID != 0)) {
                const bool awaitingOtherParent = (pending.parentABaseID != 0 && pending.parentARefID == 0) ||
                                                 (pending.parentBBaseID != 0 && pending.parentBRefID == 0);
                State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                    if (awaitingOtherParent) {
                        ws.lineage.Write().AddPendingChild(State::BaseId{ pending.childBaseID }, LegacyAdapters::ToPendingChildEntry(pending));
                    } else {
                        (void)ws.lineage.Write().ErasePendingChild(State::BaseId{ pending.childBaseID });
                    }
                });

                auto [bioMother, bioFather] = resolveBioParents(pending.parentARefID, pending.parentBRefID, pending.isAdopted);
                RE::FormID gA = pending.parentARefID;
                RE::FormID gB = pending.parentBRefID;
                if (!awaitingOtherParent && ((gA != 0 && gB == 0) || (gA == 0 && gB != 0))) {
                    const RE::FormID soleG = (gA != 0) ? gA : gB;
                    const RE::FormID spouseID = GetPrimaryPartner(soleG);
                    if (spouseID != 0 && IsMarried(soleG, spouseID)) {
                        gA = soleG;
                        gB = spouseID;
                    }
                }
                RegisterChildActor(cRef, gA, gB, 0, bioMother, bioFather, 0, static_cast<uint8_t>(ChildStage::Child), pending.isAdopted, /*sendEvent=*/false);
                MARAS_LOG_INFO("TryResolvePendingChildren: Resolved pending child {:08X} on parent load (guardians {:08X} / {:08X}, bioM: {:08X}, bioF: {:08X}, adopted: {})",
                               cRef, gA, gB, bioMother, bioFather, pending.isAdopted);
            } else {
                State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                    ws.lineage.Write().AddPendingChild(State::BaseId{ pending.childBaseID }, LegacyAdapters::ToPendingChildEntry(pending));
                });
            }
        }
    }

    void NPCRelationshipManager::TryResolvePendingRelationships(RE::FormID baseID, RE::FormID refID) {
        if (baseID == 0 || refID == 0) return;

        if (IsRegistered(refID)) {
            UpdateCanFormNonCheatingMainFaction(refID);
        }

        SkyrimNetMemoryService::SuppressScope suppressMemories;
        TryResolvePendingChildren(baseID, refID);

        std::vector<PendingRelationshipData> pendingList;
        {
            const auto snap = State::WorldStore::GetSingleton().Snapshot();
            const auto& allPending = snap->actors->AllPendingRelationships();
            if (const auto it = allPending.find(State::BaseId{ baseID }); it != allPending.end()) {
                pendingList.reserve(it->second.size());
                for (const auto& entry : it->second) {
                    pendingList.push_back(LegacyAdapters::ToLegacyPendingRelationship(entry));
                }
            }
        }
        if (pendingList.empty()) {
            return;
        }

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            (void)ws.actors.Write().ErasePendingRelationshipsForBase(State::BaseId{ baseID });
        });

        MARAS_LOG_INFO("TryResolvePendingRelationships: Resolving {} pending relationship(s) for '{}' (Base ID {:08X} -> Ref ID {:08X})",
                       pendingList.size(), ActorSvc::GetName(refID), baseID, refID);

        EnsureRegistered(refID);

        auto& npcIndex = NPCIndex::GetSingleton();
        for (const auto& pending : pendingList) {
            RE::FormID partnerRefID = pending.partnerRefID;

            if (partnerRefID == 0 && pending.partnerBaseID != 0) {
                partnerRefID = npcIndex.GetRefID(pending.partnerBaseID);
                if (partnerRefID == pending.partnerBaseID) {
                    if (auto* form = RE::TESForm::LookupByID(partnerRefID)) {
                        if (form->GetFormType() != RE::FormType::ActorCharacter && form->GetFormType() != RE::FormType::Reference) {
                            partnerRefID = 0;
                        }
                    } else {
                        partnerRefID = 0;
                    }
                }
            }

            if (partnerRefID != 0) {
                EnsureRegistered(partnerRefID);
                SetOriginalRelationshipStatus(refID, partnerRefID, pending.originalStatus);

                auto currentStatus = GetRelationshipStatus(refID, partnerRefID);
                if (currentStatus == RelationshipStatus::Candidate || currentStatus == RelationshipStatus::Unknown) {
                    if (pending.targetStatus != RelationshipStatus::Unknown) {
                        ApplyImportedVanillaStatus(refID, partnerRefID, pending.originalStatus, pending.targetStatus);

                        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                            auto& edge = ws.relationships.Write().GetOrCreateEdge(State::ActorId{ refID }, State::ActorId{ partnerRefID });
                            for (auto& hist : edge.statusHistory) {
                                if (hist.timestamp >= 0.0f) {
                                    hist.timestamp = -1.0f;
                                }
                            }
                        });

                        MARAS_LOG_INFO("TryResolvePendingRelationships: Assigned status {} between '{}' ({:08X}) and '{}' ({:08X})",
                                       Utils::RelationshipStatusToString(pending.targetStatus),
                                       ActorSvc::GetName(refID), refID,
                                       ActorSvc::GetName(partnerRefID), partnerRefID);
                    }
                }
            } else {
                const PendingRelationshipData remaining(baseID, refID, pending.targetStatus, pending.originalStatus);
                State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                    ws.actors.Write().AddPendingRelationship(
                        State::BaseId{ pending.partnerBaseID },
                        LegacyAdapters::ToPendingRelationshipEntry(remaining));
                });
                MARAS_LOG_INFO("TryResolvePendingRelationships: Resolved actor '{}' ({:08X}), partner base '{}' ({:08X}) remains pending",
                               ActorSvc::GetName(refID), refID,
                               ActorSvc::GetName(pending.partnerBaseID), pending.partnerBaseID);
            }
        }
    }

    void NPCRelationshipManager::TryResolvePendingRelationships(RE::Actor* actor) {
        if (!actor) {
            MARAS_LOG_WARN("TryResolvePendingRelationships: null actor passed");
            return;
        }
        if (auto* base = actor->GetActorBase()) {
            RE::FormID baseID = base->GetFormID();
            RE::FormID refID = actor->GetFormID();
            TryResolvePendingRelationships(baseID, refID);
        } else {
            MARAS_LOG_WARN("TryResolvePendingRelationships: actor '{}' ({:08X}) has null actorBase", actor->GetName(), actor->GetFormID());
        }
    }

    // Status checks
    bool NPCRelationshipManager::IsRegistered(RE::FormID npcFormID) const {
        if (npcFormID == kPlayerFormID || npcFormID == 0 || npcFormID == 0x07) return false;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        return snap->actors->IsRegisteredAdult(State::RefId{ npcFormID });
    }

    bool NPCRelationshipManager::IsCandidate(RE::FormID npcFormID, RE::FormID targetFormID) const {
        return GetRelationshipStatus(npcFormID, targetFormID) == RelationshipStatus::Candidate;
    }

    bool NPCRelationshipManager::IsEngaged(RE::FormID npcFormID, RE::FormID targetFormID) const {
        return GetRelationshipStatus(npcFormID, targetFormID) == RelationshipStatus::Engaged;
    }

    bool NPCRelationshipManager::IsMarried(RE::FormID npcFormID, RE::FormID targetFormID) const {
        return GetRelationshipStatus(npcFormID, targetFormID) == RelationshipStatus::Married;
    }

    bool NPCRelationshipManager::IsDivorced(RE::FormID npcFormID, RE::FormID targetFormID) const {
        return GetRelationshipStatus(npcFormID, targetFormID) == RelationshipStatus::Divorced;
    }

    bool NPCRelationshipManager::IsMostRecentRelationshipDivorced(RE::FormID actorA, RE::FormID actorB) const {
        if (actorA == 0 || actorB == 0 || actorA == actorB) return false;

        const auto data = GetNPCData(actorA, actorB);
        if (!data) return false;

        // Inspect only the single most recent status transition if history exists, otherwise current status
        if (!data->statusHistory.empty()) {
            return data->statusHistory.back().toStatus == RelationshipStatus::Divorced;
        }

        return data->status == RelationshipStatus::Divorced;
    }

    bool NPCRelationshipManager::IsJilted(RE::FormID npcFormID, RE::FormID targetFormID) const {
        return GetRelationshipStatus(npcFormID, targetFormID) == RelationshipStatus::Jilted;
    }

    bool NPCRelationshipManager::IsDating(RE::FormID npcFormID, RE::FormID targetFormID) const {
        return GetRelationshipStatus(npcFormID, targetFormID) == RelationshipStatus::Dating;
    }

    bool NPCRelationshipManager::IsLover(RE::FormID npcFormID, RE::FormID targetFormID) const {
        return GetRelationshipStatus(npcFormID, targetFormID) == RelationshipStatus::Lover;
    }

    bool NPCRelationshipManager::IsExDating(RE::FormID npcFormID, RE::FormID targetFormID) const {
        return GetRelationshipStatus(npcFormID, targetFormID) == RelationshipStatus::ExDating;
    }

    bool NPCRelationshipManager::IsExLover(RE::FormID npcFormID, RE::FormID targetFormID) const {
        return GetRelationshipStatus(npcFormID, targetFormID) == RelationshipStatus::ExLover;
    }

    bool NPCRelationshipManager::IsPlayerActiveMainPartner(RE::FormID npcFormID) const {
        if (npcFormID == 0 || npcFormID == kPlayerFormID || npcFormID == 0x07) return false;

        auto isMainWithPlayer = [this](RE::FormID id) {
            if (id == 0 || id == kPlayerFormID || id == 0x07) return false;
            auto st = GetRelationshipStatus(id, kPlayerFormID);
            return st == RelationshipStatus::Dating ||
                   st == RelationshipStatus::Engaged ||
                   st == RelationshipStatus::Married;
        };

        if (isMainWithPlayer(npcFormID)) return true;

        // Check resolved ref ID if npcFormID is base ID
        RE::FormID ref = NPCIndex::GetSingleton().GetRefID(npcFormID);
        if (ref != 0 && ref != npcFormID && isMainWithPlayer(ref)) return true;

        // Check resolved base ID if npcFormID is ref ID
        RE::FormID base = NPCIndex::GetSingleton().GetBaseID(npcFormID);
        if (base != 0 && base != npcFormID && isMainWithPlayer(base)) return true;

        return false;
    }

    // Status availability checks
    bool NPCRelationshipManager::IsAvailableForDating(RE::FormID npcFormID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (npcFormID == kPlayerFormID && snap->settings->enablePlayerPolygamy) {
            return true;
        }
        for (const auto& key : snap->relationships->GetAdjacentPairs(State::ActorId{ npcFormID })) {
            const auto* edge = snap->relationships->FindEdge(key);
            if (edge && IsMainTier(static_cast<RelationshipStatus>(edge->status))) {
                return false;
            }
        }
        return true;
    }

    bool NPCRelationshipManager::IsAvailableForDating(RE::FormID npcFormID, RE::FormID targetFormID) const {
        if (targetFormID == 0) {
            return IsAvailableForDating(npcFormID);
        }
        return CanFormRelationship(npcFormID, targetFormID, RelationshipStatus::Dating);
    }

    bool NPCRelationshipManager::IsAvailableForEngaged(RE::FormID npcFormID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (npcFormID == kPlayerFormID && snap->settings->enablePlayerPolygamy) {
            return true;
        }
        for (const auto& key : snap->relationships->GetAdjacentPairs(State::ActorId{ npcFormID })) {
            const auto* edge = snap->relationships->FindEdge(key);
            if (edge &&
                (edge->status == State::RelationshipStatus::Engaged || edge->status == State::RelationshipStatus::Married)) {
                return false;
            }
        }
        return true;
    }

    bool NPCRelationshipManager::IsAvailableForEngaged(RE::FormID npcFormID, RE::FormID targetFormID) const {
        if (targetFormID == 0) {
            return IsAvailableForEngaged(npcFormID);
        }
        return CanFormRelationship(npcFormID, targetFormID, RelationshipStatus::Engaged);
    }

    bool NPCRelationshipManager::IsAvailableForMarried(RE::FormID npcFormID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (npcFormID == kPlayerFormID && snap->settings->enablePlayerPolygamy) {
            return true;
        }
        for (const auto& key : snap->relationships->GetAdjacentPairs(State::ActorId{ npcFormID })) {
            const auto* edge = snap->relationships->FindEdge(key);
            if (edge && edge->status == State::RelationshipStatus::Married) {
                return false;
            }
        }
        return true;
    }

    bool NPCRelationshipManager::IsAvailableForMarried(RE::FormID npcFormID, RE::FormID targetFormID) const {
        if (targetFormID == 0) {
            return IsAvailableForMarried(npcFormID);
        }
        return CanFormRelationship(npcFormID, targetFormID, RelationshipStatus::Married);
    }

    bool NPCRelationshipManager::IsAvailableForLover(RE::FormID /*npcFormID*/) const {
        return true;
    }

    bool NPCRelationshipManager::IsAvailableForLover(RE::FormID npcFormID, RE::FormID targetFormID) const {
        if (targetFormID == 0) {
            return IsAvailableForLover(npcFormID);
        }
        return CanFormRelationship(npcFormID, targetFormID, RelationshipStatus::Lover);
    }

    bool NPCRelationshipManager::IsAvailableForDivorced(RE::FormID npcFormID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        for (const auto& key : snap->relationships->GetAdjacentPairs(State::ActorId{ npcFormID })) {
            const auto* edge = snap->relationships->FindEdge(key);
            if (edge && edge->status == State::RelationshipStatus::Married) {
                return true;
            }
        }
        return false;
    }

    bool NPCRelationshipManager::IsAvailableForDivorced(RE::FormID npcFormID, RE::FormID targetFormID) const {
        if (targetFormID == 0) {
            return IsAvailableForDivorced(npcFormID);
        }
        return GetRelationshipStatus(npcFormID, targetFormID) == RelationshipStatus::Married;
    }

    bool NPCRelationshipManager::IsAvailableForJilted(RE::FormID npcFormID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        for (const auto& key : snap->relationships->GetAdjacentPairs(State::ActorId{ npcFormID })) {
            const auto* edge = snap->relationships->FindEdge(key);
            if (edge && edge->status == State::RelationshipStatus::Engaged) {
                return true;
            }
        }
        return false;
    }

    bool NPCRelationshipManager::IsAvailableForJilted(RE::FormID npcFormID, RE::FormID targetFormID) const {
        if (targetFormID == 0) {
            return IsAvailableForJilted(npcFormID);
        }
        return GetRelationshipStatus(npcFormID, targetFormID) == RelationshipStatus::Engaged;
    }

    bool NPCRelationshipManager::IsAvailableForExDating(RE::FormID npcFormID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        for (const auto& key : snap->relationships->GetAdjacentPairs(State::ActorId{ npcFormID })) {
            const auto* edge = snap->relationships->FindEdge(key);
            if (edge && edge->status == State::RelationshipStatus::Dating) {
                return true;
            }
        }
        return false;
    }

    bool NPCRelationshipManager::IsAvailableForExDating(RE::FormID npcFormID, RE::FormID targetFormID) const {
        if (targetFormID == 0) {
            return IsAvailableForExDating(npcFormID);
        }
        return GetRelationshipStatus(npcFormID, targetFormID) == RelationshipStatus::Dating;
    }

    bool NPCRelationshipManager::IsAvailableForExLover(RE::FormID npcFormID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        for (const auto& key : snap->relationships->GetAdjacentPairs(State::ActorId{ npcFormID })) {
            const auto* edge = snap->relationships->FindEdge(key);
            if (edge && edge->status == State::RelationshipStatus::Lover) {
                return true;
            }
        }
        return false;
    }

    bool NPCRelationshipManager::IsAvailableForExLover(RE::FormID npcFormID, RE::FormID targetFormID) const {
        if (targetFormID == 0) {
            return IsAvailableForExLover(npcFormID);
        }
        return GetRelationshipStatus(npcFormID, targetFormID) == RelationshipStatus::Lover;
    }

    bool NPCRelationshipManager::IsAvailableForBreakup(RE::FormID npcFormID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        for (const auto& key : snap->relationships->GetAdjacentPairs(State::ActorId{ npcFormID })) {
            const auto* edge = snap->relationships->FindEdge(key);
            if (edge && IsActivePartnerStatus(static_cast<RelationshipStatus>(edge->status))) {
                return true;
            }
        }
        return false;
    }

    bool NPCRelationshipManager::IsAvailableForBreakup(RE::FormID npcFormID, RE::FormID targetFormID) const {
        if (targetFormID == 0) {
            return IsAvailableForBreakup(npcFormID);
        }
        return IsActivePartnerStatus(GetRelationshipStatus(npcFormID, targetFormID));
    }

    bool NPCRelationshipManager::IsAvailableForBreakupDating(RE::FormID npcFormID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        for (const auto& key : snap->relationships->GetAdjacentPairs(State::ActorId{ npcFormID })) {
            const auto* edge = snap->relationships->FindEdge(key);
            if (edge && edge->status == State::RelationshipStatus::Dating) {
                return true;
            }
        }
        return false;
    }

    bool NPCRelationshipManager::IsAvailableForBreakupDating(RE::FormID npcFormID, RE::FormID targetFormID) const {
        if (targetFormID == 0) {
            return IsAvailableForBreakupDating(npcFormID);
        }
        return GetRelationshipStatus(npcFormID, targetFormID) == RelationshipStatus::Dating;
    }

    bool NPCRelationshipManager::IsAvailableForBreakupLover(RE::FormID npcFormID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        for (const auto& key : snap->relationships->GetAdjacentPairs(State::ActorId{ npcFormID })) {
            const auto* edge = snap->relationships->FindEdge(key);
            if (edge && edge->status == State::RelationshipStatus::Lover) {
                return true;
            }
        }
        return false;
    }

    bool NPCRelationshipManager::IsAvailableForBreakupLover(RE::FormID npcFormID, RE::FormID targetFormID) const {
        if (targetFormID == 0) {
            return IsAvailableForBreakupLover(npcFormID);
        }
        return GetRelationshipStatus(npcFormID, targetFormID) == RelationshipStatus::Lover;
    }

    bool NPCRelationshipManager::IsAvailableForOpenRelationship(RE::FormID npcFormID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        for (const auto& key : snap->relationships->GetAdjacentPairs(State::ActorId{ npcFormID })) {
            const auto* edge = snap->relationships->FindEdge(key);
            if (edge && IsMainTier(static_cast<RelationshipStatus>(edge->status)) && !edge->isOpen) {
                return true;
            }
        }
        return false;
    }

    bool NPCRelationshipManager::IsAvailableForOpenRelationship(RE::FormID npcFormID, RE::FormID targetFormID) const {
        if (targetFormID == 0) {
            return IsAvailableForOpenRelationship(npcFormID);
        }
        return CanOpenRelationship(npcFormID, targetFormID);
    }

    bool NPCRelationshipManager::IsAvailableForCloseRelationship(RE::FormID npcFormID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        for (const auto& key : snap->relationships->GetAdjacentPairs(State::ActorId{ npcFormID })) {
            const auto* edge = snap->relationships->FindEdge(key);
            if (edge && IsMainTier(static_cast<RelationshipStatus>(edge->status)) && edge->isOpen) {
                return true;
            }
        }
        return false;
    }

    bool NPCRelationshipManager::IsAvailableForCloseRelationship(RE::FormID npcFormID, RE::FormID targetFormID) const {
        if (targetFormID == 0) {
            return IsAvailableForCloseRelationship(npcFormID);
        }
        return CanCloseRelationship(npcFormID, targetFormID);
    }

    bool NPCRelationshipManager::IsAvailableForStatus(RE::FormID npcFormID, RelationshipStatus status) const {
        switch (status) {
            case RelationshipStatus::Dating:
                return IsAvailableForDating(npcFormID);
            case RelationshipStatus::Engaged:
                return IsAvailableForEngaged(npcFormID);
            case RelationshipStatus::Married:
                return IsAvailableForMarried(npcFormID);
            case RelationshipStatus::Lover:
                return IsAvailableForLover(npcFormID);
            case RelationshipStatus::Divorced:
                return IsAvailableForDivorced(npcFormID);
            case RelationshipStatus::Jilted:
                return IsAvailableForJilted(npcFormID);
            case RelationshipStatus::ExDating:
                return IsAvailableForExDating(npcFormID);
            case RelationshipStatus::ExLover:
                return IsAvailableForExLover(npcFormID);
            case RelationshipStatus::Candidate:
                return true;
            default:
                return true;
        }
    }

    bool NPCRelationshipManager::IsAvailableForStatus(RE::FormID npcFormID, const std::string& statusName) const {
        if (StringUtils::CaseInsensitiveEquals(statusName, "breakup")) {
            return IsAvailableForBreakup(npcFormID);
        }
        if (StringUtils::CaseInsensitiveEquals(statusName, "open_relationship") ||
            StringUtils::CaseInsensitiveEquals(statusName, "open")) {
            return IsAvailableForOpenRelationship(npcFormID);
        }
        if (StringUtils::CaseInsensitiveEquals(statusName, "close_relationship") ||
            StringUtils::CaseInsensitiveEquals(statusName, "close")) {
            return IsAvailableForCloseRelationship(npcFormID);
        }
        return IsAvailableForStatus(npcFormID, Utils::StringToRelationshipStatus(statusName));
    }

    bool NPCRelationshipManager::IsAvailableForStatus(RE::FormID npcFormID, RE::FormID targetFormID, RelationshipStatus status) const {
        if (targetFormID == 0) {
            return IsAvailableForStatus(npcFormID, status);
        }
        switch (status) {
            case RelationshipStatus::Dating:
                return IsAvailableForDating(npcFormID, targetFormID);
            case RelationshipStatus::Engaged:
                return IsAvailableForEngaged(npcFormID, targetFormID);
            case RelationshipStatus::Married:
                return IsAvailableForMarried(npcFormID, targetFormID);
            case RelationshipStatus::Lover:
                return IsAvailableForLover(npcFormID, targetFormID);
            case RelationshipStatus::Divorced:
                return IsAvailableForDivorced(npcFormID, targetFormID);
            case RelationshipStatus::Jilted:
                return IsAvailableForJilted(npcFormID, targetFormID);
            case RelationshipStatus::ExDating:
                return IsAvailableForExDating(npcFormID, targetFormID);
            case RelationshipStatus::ExLover:
                return IsAvailableForExLover(npcFormID, targetFormID);
            case RelationshipStatus::Candidate:
                return true;
            default:
                return true;
        }
    }

    bool NPCRelationshipManager::IsAvailableForStatus(RE::FormID npcFormID, RE::FormID targetFormID, const std::string& statusName) const {
        if (targetFormID == 0) {
            return IsAvailableForStatus(npcFormID, statusName);
        }
        if (StringUtils::CaseInsensitiveEquals(statusName, "breakup")) {
            return IsAvailableForBreakup(npcFormID, targetFormID);
        }
        if (StringUtils::CaseInsensitiveEquals(statusName, "open_relationship") ||
            StringUtils::CaseInsensitiveEquals(statusName, "open")) {
            return CanOpenRelationship(npcFormID, targetFormID);
        }
        if (StringUtils::CaseInsensitiveEquals(statusName, "close_relationship") ||
            StringUtils::CaseInsensitiveEquals(statusName, "close")) {
            return CanCloseRelationship(npcFormID, targetFormID);
        }
        return IsAvailableForStatus(npcFormID, targetFormID, Utils::StringToRelationshipStatus(statusName));
    }

    bool NPCRelationshipManager::CanOpenRelationship(RE::FormID actorA, RE::FormID actorB) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* edge = snap->relationships->FindEdge(State::ActorId{ actorA }, State::ActorId{ actorB });
        if (edge) {
            return IsMainTier(static_cast<RelationshipStatus>(edge->status)) && !edge->isOpen;
        }
        return false;
    }

    bool NPCRelationshipManager::CanCloseRelationship(RE::FormID actorA, RE::FormID actorB) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* edge = snap->relationships->FindEdge(State::ActorId{ actorA }, State::ActorId{ actorB });
        if (edge) {
            return IsMainTier(static_cast<RelationshipStatus>(edge->status)) && edge->isOpen;
        }
        return false;
    }

    bool NPCRelationshipManager::CanFormRelationship(RE::FormID actorA, RE::FormID actorB, RelationshipStatus status, std::string* outReason) const {
        if (outReason) {
            outReason->clear();
        }

        if (actorA == actorB && actorA != 0) {
            if (outReason) {
                *outReason = "An actor cannot form a relationship with themselves.";
            }
            return false;
        }

        // Child check
        if (IsChildNPC(actorA) || IsChildNPC(actorB)) {
            if (outReason) {
                *outReason = "Children cannot enter adult relationships.";
            }
            return false;
        }

        // Family / consanguinity check for active romantic relationships
        if (IsActivePartnerStatus(status) && AreFamilyMembers(actorA, actorB, outReason)) {
            return false;
        }

        std::string nameA = ActorSvc::GetName(actorA);
        std::string nameB = ActorSvc::GetName(actorB);
        if (nameA.empty() || nameA == "None") nameA = (actorA == kPlayerFormID) ? "Player" : fmt::format("0x{:08X}", actorA);
        if (nameB.empty() || nameB == "None") nameB = (actorB == kPlayerFormID) ? "Player" : fmt::format("0x{:08X}", actorB);

        const auto snap = State::WorldStore::GetSingleton().Snapshot();

        // Validate existing direct relationship between actorA and actorB
        const auto* directEdge = snap->relationships->FindEdge(State::ActorId{ actorA }, State::ActorId{ actorB });
        if (directEdge) {
            const auto directStatus = static_cast<RelationshipStatus>(directEdge->status);
            if (status == RelationshipStatus::Lover) {
                if (directStatus == RelationshipStatus::Lover) {
                    if (outReason) {
                        *outReason = fmt::format("{} and {} are already lovers.", nameA, nameB);
                    }
                    return false;
                }
                if (IsMainTier(directStatus)) {
                    if (outReason) {
                        *outReason = fmt::format("{} and {} already have an active relationship ({}).", nameA, nameB, Utils::RelationshipStatusToString(directStatus));
                    }
                    return false;
                }
            }
            if (status == RelationshipStatus::Dating && IsMainTier(directStatus)) {
                if (outReason) {
                    *outReason = fmt::format("{} and {} already have an active relationship ({}).", nameA, nameB, Utils::RelationshipStatusToString(directStatus));
                }
                return false;
            }
            if (status == RelationshipStatus::Engaged && (directStatus == RelationshipStatus::Engaged || directStatus == RelationshipStatus::Married)) {
                if (outReason) {
                    *outReason = fmt::format("{} and {} are already engaged or married.", nameA, nameB);
                }
                return false;
            }
            if (status == RelationshipStatus::Married && directStatus == RelationshipStatus::Married) {
                if (outReason) {
                    *outReason = fmt::format("{} and {} are already married.", nameA, nameB);
                }
                return false;
            }
        }

        if (status == RelationshipStatus::Lover) {
            return true;
        }

        if (status == RelationshipStatus::Divorced) {
            if (!directEdge || directEdge->status != State::RelationshipStatus::Married) {
                if (outReason) *outReason = "Can only divorce if currently Married.";
                return false;
            }
            return true;
        }
        if (status == RelationshipStatus::Jilted) {
            if (!directEdge || directEdge->status != State::RelationshipStatus::Engaged) {
                if (outReason) *outReason = "Can only be jilted if currently Engaged.";
                return false;
            }
            return true;
        }
        if (status == RelationshipStatus::ExDating) {
            if (!directEdge || directEdge->status != State::RelationshipStatus::Dating) {
                if (outReason) *outReason = "Can only transition to Ex-Dating if currently Dating.";
                return false;
            }
            return true;
        }
        if (status == RelationshipStatus::ExLover) {
            if (!directEdge || directEdge->status != State::RelationshipStatus::Lover) {
                if (outReason) *outReason = "Can only transition to Ex-Lover if currently Lovers.";
                return false;
            }
            return true;
        }

        if (!IsMainTier(status)) {
            return true;
        }

        bool aIsPlayer = (actorA == kPlayerFormID);
        bool bIsPlayer = (actorB == kPlayerFormID);
        bool polygamyEnabled = snap->settings->enablePlayerPolygamy;

        auto validateActorWithOtherPartners = [&](RE::FormID self, RE::FormID other, bool isPlayer, const std::string& name) -> bool {
            if (isPlayer && polygamyEnabled) {
                return true;
            }

            const State::ActorId selfId{ self };
            const State::ActorId otherId{ other };
            for (const auto& key : snap->relationships->GetAdjacentPairs(selfId)) {
                if (!key.Contains(otherId)) {
                    const auto* edge = snap->relationships->FindEdge(key);
                    if (!edge) continue;
                    const auto edgeStatus = static_cast<RelationshipStatus>(edge->status);

                    RE::FormID existingPartnerID = key.Other(selfId).raw();
                    std::string partnerName = ActorSvc::GetName(existingPartnerID);
                    if (partnerName.empty() || partnerName == "None") {
                        partnerName = (existingPartnerID == kPlayerFormID) ? "Player" : fmt::format("0x{:08X}", existingPartnerID);
                    }

                    if (status == RelationshipStatus::Dating || status == RelationshipStatus::Married) {
                        if (IsMainTier(edgeStatus)) {
                            if (outReason) {
                                *outReason = isPlayer
                                    ? "Cannot enter another main relationship: Polygamy is disabled in settings."
                                    : fmt::format("{} already has a committed main partner ({}).", name, partnerName);
                            }
                            return false;
                        }
                    } else if (status == RelationshipStatus::Engaged) {
                        if (edgeStatus == RelationshipStatus::Engaged || edgeStatus == RelationshipStatus::Married) {
                            if (outReason) {
                                *outReason = isPlayer
                                    ? "Cannot enter another engagement/marriage: Polygamy is disabled in settings."
                                    : fmt::format("{} is already engaged or married to {}.", name, partnerName);
                            }
                            return false;
                        }
                        if (edgeStatus == RelationshipStatus::Dating) {
                            if (outReason) {
                                *outReason = isPlayer
                                    ? "Cannot propose engagement to this actor while dating someone else."
                                    : fmt::format("{} is dating {} and can only get engaged to their dating partner.", name, partnerName);
                            }
                            return false;
                        }
                    }
                }
            }
            return true;
        };

        if (!validateActorWithOtherPartners(actorA, actorB, aIsPlayer, nameA)) {
            return false;
        }
        if (!validateActorWithOtherPartners(actorB, actorA, bIsPlayer, nameB)) {
            return false;
        }

        return true;
    }

    bool NPCRelationshipManager::CanFormRelationship(RE::FormID actorA, RE::FormID actorB, const std::string& statusName, std::string* outReason) const {
        if (outReason) {
            outReason->clear();
        }
        if (StringUtils::CaseInsensitiveEquals(statusName, "breakup")) {
            if (!IsAvailableForBreakup(actorA, actorB)) {
                if (outReason) {
                    *outReason = "Actors do not have an active relationship to break up.";
                }
                return false;
            }
            return true;
        }
        if (StringUtils::CaseInsensitiveEquals(statusName, "open_relationship") ||
            StringUtils::CaseInsensitiveEquals(statusName, "open")) {
            if (!CanOpenRelationship(actorA, actorB)) {
                if (outReason) {
                    *outReason = "Can only open an active main relationship that is currently closed.";
                }
                return false;
            }
            return true;
        }
        if (StringUtils::CaseInsensitiveEquals(statusName, "close_relationship") ||
            StringUtils::CaseInsensitiveEquals(statusName, "close")) {
            if (!CanCloseRelationship(actorA, actorB)) {
                if (outReason) {
                    *outReason = "Can only close an active main relationship that is currently open.";
                }
                return false;
            }
            return true;
        }
        return CanFormRelationship(actorA, actorB, Utils::StringToRelationshipStatus(statusName), outReason);
    }

    bool NPCRelationshipManager::AreFamilyMembers(RE::FormID actorA, RE::FormID actorB, std::string* outReason) const {
        if (outReason) {
            outReason->clear();
        }

        if (actorA == 0 || actorB == 0 || actorA == actorB) {
            return false;
        }

        std::string nameA = ActorSvc::GetName(actorA);
        std::string nameB = ActorSvc::GetName(actorB);
        if (nameA.empty() || nameA == "None") nameA = (actorA == kPlayerFormID) ? "Player" : fmt::format("0x{:08X}", actorA);
        if (nameB.empty() || nameB == "None") nameB = (actorB == kPlayerFormID) ? "Player" : fmt::format("0x{:08X}", actorB);

        // 1. MARAS Children Registry checks
        auto collectNormalizedParentIDs = [](RE::FormID actorID) -> std::unordered_set<RE::FormID> {
            std::unordered_set<RE::FormID> ids;
            if (actorID == 0 || actorID == kUnknownFatherFormID) {
                return ids;
            }
            ids.insert(actorID);
            if (actorID == kPlayerFormID || actorID == kPlayerBaseID) {
                ids.insert(kPlayerFormID);
                ids.insert(kPlayerBaseID);
                return ids;
            }
            RE::FormID refID = NPCIndex::GetSingleton().GetRefID(actorID);
            if (refID != 0) ids.insert(refID);
            RE::FormID baseID = NPCIndex::GetSingleton().GetBaseID(actorID);
            if (baseID != 0) ids.insert(baseID);
            if (auto* base = ActorSvc::GetActorBase(actorID)) {
                if (base->GetFormID() != 0) ids.insert(base->GetFormID());
            }
            return ids;
        };

        auto collectEntryParentIDs = [&](const ChildRegistryEntry& entry) -> std::unordered_set<RE::FormID> {
            std::unordered_set<RE::FormID> allIDs;
            for (RE::FormID rawParentID : { entry.guardianAID, entry.guardianBID, entry.bioMotherID, entry.bioFatherID }) {
                for (RE::FormID normID : collectNormalizedParentIDs(rawParentID)) {
                    allIDs.insert(normID);
                }
            }
            return allIDs;
        };

        auto checkParentChildMaras = [&](RE::FormID childID, RE::FormID possibleParentID) -> bool {
            auto entryOpt = GetChildEntry(childID);
            if (!entryOpt.has_value()) return false;
            const auto entryParents = collectEntryParentIDs(*entryOpt);
            for (RE::FormID normID : collectNormalizedParentIDs(possibleParentID)) {
                if (entryParents.contains(normID)) {
                    return true;
                }
            }
            return false;
        };

        if (checkParentChildMaras(actorA, actorB) || checkParentChildMaras(actorB, actorA)) {
            if (outReason) {
                *outReason = fmt::format("{} and {} have a parent/child relationship.", nameA, nameB);
            }
            return true;
        }

        auto entryA = GetChildEntry(actorA);
        auto entryB = GetChildEntry(actorB);
        if (entryA.has_value() && entryB.has_value()) {
            const auto parentsA = collectEntryParentIDs(*entryA);
            const auto parentsB = collectEntryParentIDs(*entryB);

            bool shareParent = false;
            for (RE::FormID pID : parentsB) {
                if (parentsA.contains(pID)) {
                    shareParent = true;
                    break;
                }
            }

            if (shareParent) {
                if (outReason) {
                    *outReason = fmt::format("{} and {} are siblings (brother/sister).", nameA, nameB);
                }
                return true;
            }
        }

        // 2. Skyrim Engine BGSRelationship checks
        auto* baseA = ActorSvc::GetActorBase(actorA);
        auto* baseB = ActorSvc::GetActorBase(actorB);
        if (!baseA || !baseB) return false;
        if (baseA == baseB) {
            if (outReason) {
                *outReason = fmt::format("{} and {} share the exact same NPC base form.", nameA, nameB);
            }
            return true;
        }

        constexpr RE::FormID kSiblingsAssocFormID = 0x000142C5;
        constexpr RE::FormID kParentChildAssocFormID = 0x000142C6;
        constexpr RE::FormID kAuntUncleAssocFormID = 0x000142C7;
        constexpr RE::FormID kCousinsAssocFormID = 0x000142C8;
        constexpr RE::FormID kGrandparentGrandchildAssocFormID = 0x0001E803;
        constexpr RE::FormID kGrandAuntUncleAssocFormID = 0x0001E80B;
        constexpr RE::FormID kGreatGrandparentAssocFormID = 0x0001E90B;

        auto checkDirectRel = [&](const RE::BGSRelationship* rel) -> bool {
            if (!rel || !rel->assocType) return false;
            uint32_t assocID = rel->assocType->GetFormID();
            if (assocID == kSiblingsAssocFormID) {
                if (outReason) {
                    *outReason = fmt::format("{} and {} are siblings (brother/sister).", nameA, nameB);
                }
                return true;
            }
            if (assocID == kParentChildAssocFormID) {
                if (outReason) {
                    *outReason = fmt::format("{} and {} have a parent/child relationship.", nameA, nameB);
                }
                return true;
            }
            if (assocID == kAuntUncleAssocFormID) {
                if (outReason) {
                    *outReason = fmt::format("{} and {} have an aunt/uncle and niece/nephew relationship.", nameA, nameB);
                }
                return true;
            }
            if (assocID == kCousinsAssocFormID) {
                if (outReason) {
                    *outReason = fmt::format("{} and {} are cousins.", nameA, nameB);
                }
                return true;
            }
            if (assocID == kGrandparentGrandchildAssocFormID) {
                if (outReason) {
                    *outReason = fmt::format("{} and {} have a grandparent/grandchild relationship.", nameA, nameB);
                }
                return true;
            }
            if (assocID == kGrandAuntUncleAssocFormID) {
                if (outReason) {
                    *outReason = fmt::format("{} and {} have a grand-aunt/uncle relationship.", nameA, nameB);
                }
                return true;
            }
            if (assocID == kGreatGrandparentAssocFormID) {
                if (outReason) {
                    *outReason = fmt::format("{} and {} have a great-grandparent/grandchild relationship.", nameA, nameB);
                }
                return true;
            }
            return false;
        };

        if (auto* rel = RE::BGSRelationship::GetRelationship(baseA, baseB)) {
            if (checkDirectRel(rel)) return true;
        }

        if (baseA->relationships) {
            for (auto* rel : *baseA->relationships) {
                if (!rel) continue;
                if ((rel->npc1 == baseA && rel->npc2 == baseB) || (rel->npc2 == baseA && rel->npc1 == baseB)) {
                    if (checkDirectRel(rel)) return true;
                }
            }
        }

        // 3. Shared parents in engine relationships (identifies siblings without direct Sibling link)
        auto getEngineParents = [&](const RE::TESNPC* npc) -> std::unordered_set<const RE::TESNPC*> {
            std::unordered_set<const RE::TESNPC*> parents;
            if (!npc || !npc->relationships) return parents;
            for (auto* rel : *npc->relationships) {
                if (!rel || !rel->assocType) continue;
                if (rel->assocType->GetFormID() == kParentChildAssocFormID) {
                    if (rel->npc2 == npc && rel->npc1 && rel->npc1 != npc) {
                        parents.insert(rel->npc1);
                    }
                }
            }
            return parents;
        };

        auto parentsA = getEngineParents(baseA);
        auto parentsB = getEngineParents(baseB);

        if (parentsA.count(baseB) > 0 || parentsB.count(baseA) > 0) {
            if (outReason) {
                *outReason = fmt::format("{} and {} have a parent/child relationship.", nameA, nameB);
            }
            return true;
        }

        for (auto* p : parentsA) {
            if (parentsB.count(p) > 0) {
                if (outReason) {
                    *outReason = fmt::format("{} and {} are siblings (share a parent).", nameA, nameB);
                }
                return true;
            }
        }

        return false;
    }

    // Status transitions
    bool NPCRelationshipManager::PromoteToEngaged(RE::FormID npcFormID, RE::FormID targetFormID, bool sendEvent, bool breakLovers) {
        RelationshipStatus oldStatus = GetRelationshipStatus(npcFormID, targetFormID);
        bool res = ChangeStatusCommon(npcFormID, targetFormID, RelationshipStatus::Engaged, nullptr, sendEvent);
        if (res && breakLovers && !IsMainTier(oldStatus)) {
            if (!(npcFormID == kPlayerFormID && Settings::GetSingleton().GetEnablePolygamy()) && !HasAnyOpenMainPair(npcFormID)) {
                BreakAllLoverConnections(npcFormID, false, sendEvent);
            }
            if (targetFormID != 0 && !(targetFormID == kPlayerFormID && Settings::GetSingleton().GetEnablePolygamy()) && !HasAnyOpenMainPair(targetFormID)) {
                BreakAllLoverConnections(targetFormID, false, sendEvent);
            }
        }
        return res;
    }

    bool NPCRelationshipManager::PromoteToMarried(RE::FormID npcFormID, RE::FormID targetFormID, bool sendEvent, bool breakLovers) {
        RelationshipStatus oldStatus = GetRelationshipStatus(npcFormID, targetFormID);
        bool res = ChangeStatusCommon(npcFormID, targetFormID, RelationshipStatus::Married,
                                  [this](RE::FormID id, RE::FormID target) {
                                      if (target == kPlayerFormID) {
                                          SpouseHierarchyManager::GetSingleton().OnSpouseAdded(id);
                                      } else if (id == kPlayerFormID) {
                                          SpouseHierarchyManager::GetSingleton().OnSpouseAdded(target);
                                      }
                                      EvaluateChildCustodyOnMarriage(id, target);
                                  },
                                  sendEvent);
        if (res && breakLovers && !IsMainTier(oldStatus)) {
            if (!(npcFormID == kPlayerFormID && Settings::GetSingleton().GetEnablePolygamy()) && !HasAnyOpenMainPair(npcFormID)) {
                BreakAllLoverConnections(npcFormID, false, sendEvent);
            }
            if (targetFormID != 0 && !(targetFormID == kPlayerFormID && Settings::GetSingleton().GetEnablePolygamy()) && !HasAnyOpenMainPair(targetFormID)) {
                BreakAllLoverConnections(targetFormID, false, sendEvent);
            }
        }
        return res;
    }

    bool NPCRelationshipManager::PromoteToDivorced(RE::FormID npcFormID, RE::FormID targetFormID, bool sendEvent) {
        return ChangeStatusCommon(npcFormID, targetFormID, RelationshipStatus::Divorced,
                                  [this](RE::FormID id, RE::FormID target) {
                                      if (target == kPlayerFormID || id == kPlayerFormID) {
                                          RE::FormID npcID = (target == kPlayerFormID) ? id : target;
                                          SpouseHierarchyManager::GetSingleton().OnSpouseRemoved(npcID);
                                          MARAS::SpouseAssetsService::GetSingleton().StopShareHouseWithPlayer(npcID);
                                          (void)PlayerHouseService::GetSingleton().RemoveTenantFromPlayerHouse(npcID);
                                          ClearPartnerIntent(npcID);
                                      } else {
                                          if (State::WorldStore::GetSingleton().Snapshot()->relationships->GetPendingIntent(State::ActorId{ id }).relatedLover.raw() == target) {
                                              ClearPartnerIntent(id);
                                          }
                                          if (State::WorldStore::GetSingleton().Snapshot()->relationships->GetPendingIntent(State::ActorId{ target }).relatedLover.raw() == id) {
                                              ClearPartnerIntent(target);
                                          }
                                      }
                                      EvaluateChildCustodyOnDivorce(id, target);
                                  },
                                  sendEvent);
    }

    bool NPCRelationshipManager::PromoteToJilted(RE::FormID npcFormID, RE::FormID targetFormID, bool sendEvent) {
        return ChangeStatusCommon(npcFormID, targetFormID, RelationshipStatus::Jilted,
                                  [this](RE::FormID id, RE::FormID target) {
                                      if (target == kPlayerFormID || id == kPlayerFormID) {
                                          RE::FormID npcID = (target == kPlayerFormID) ? id : target;
                                          SpouseHierarchyManager::GetSingleton().OnSpouseRemoved(npcID);
                                          MARAS::SpouseAssetsService::GetSingleton().StopShareHouseWithPlayer(npcID);
                                          (void)PlayerHouseService::GetSingleton().RemoveTenantFromPlayerHouse(npcID);
                                          ClearPartnerIntent(npcID);
                                      } else {
                                          if (State::WorldStore::GetSingleton().Snapshot()->relationships->GetPendingIntent(State::ActorId{ id }).relatedLover.raw() == target) {
                                              ClearPartnerIntent(id);
                                          }
                                          if (State::WorldStore::GetSingleton().Snapshot()->relationships->GetPendingIntent(State::ActorId{ target }).relatedLover.raw() == id) {
                                              ClearPartnerIntent(target);
                                          }
                                      }
                                      EvaluateChildCustodyOnDivorce(id, target);
                                  },
                                  sendEvent);
    }

    bool NPCRelationshipManager::PromoteToDating(RE::FormID npcFormID, RE::FormID targetFormID, bool sendEvent, bool breakLovers) {
        RelationshipStatus oldStatus = GetRelationshipStatus(npcFormID, targetFormID);
        bool res = ChangeStatusCommon(npcFormID, targetFormID, RelationshipStatus::Dating, nullptr, sendEvent);
        if (res && breakLovers && !IsMainTier(oldStatus)) {
            if (!(npcFormID == kPlayerFormID && Settings::GetSingleton().GetEnablePolygamy()) && !HasAnyOpenMainPair(npcFormID)) {
                BreakAllLoverConnections(npcFormID, false, sendEvent);
            }
            if (targetFormID != 0 && !(targetFormID == kPlayerFormID && Settings::GetSingleton().GetEnablePolygamy()) && !HasAnyOpenMainPair(targetFormID)) {
                BreakAllLoverConnections(targetFormID, false, sendEvent);
            }
        }
        return res;
    }

    bool NPCRelationshipManager::PromoteToLover(RE::FormID npcFormID, RE::FormID targetFormID, bool sendEvent) {
        return ChangeStatusCommon(npcFormID, targetFormID, RelationshipStatus::Lover, nullptr, sendEvent);
    }

    bool NPCRelationshipManager::PromoteToExDating(RE::FormID npcFormID, RE::FormID targetFormID, bool sendEvent) {
        return ChangeStatusCommon(npcFormID, targetFormID, RelationshipStatus::ExDating,
                                  [this](RE::FormID id, RE::FormID target) {
                                      if (target == kPlayerFormID || id == kPlayerFormID) {
                                          RE::FormID npcID = (target == kPlayerFormID) ? id : target;
                                          SpouseHierarchyManager::GetSingleton().OnSpouseRemoved(npcID);
                                          MARAS::SpouseAssetsService::GetSingleton().StopShareHouseWithPlayer(npcID);
                                          (void)PlayerHouseService::GetSingleton().RemoveTenantFromPlayerHouse(npcID);
                                          ClearPartnerIntent(npcID);
                                      } else {
                                          if (State::WorldStore::GetSingleton().Snapshot()->relationships->GetPendingIntent(State::ActorId{ id }).relatedLover.raw() == target) {
                                              ClearPartnerIntent(id);
                                          }
                                          if (State::WorldStore::GetSingleton().Snapshot()->relationships->GetPendingIntent(State::ActorId{ target }).relatedLover.raw() == id) {
                                              ClearPartnerIntent(target);
                                          }
                                      }
                                      EvaluateChildCustodyOnDivorce(id, target);
                                  },
                                  sendEvent);
    }

    bool NPCRelationshipManager::PromoteToExLover(RE::FormID npcFormID, RE::FormID targetFormID, bool sendEvent) {
        return ChangeStatusCommon(npcFormID, targetFormID, RelationshipStatus::ExLover,
                                  [this](RE::FormID id, RE::FormID target) {
                                      if (target == kPlayerFormID || id == kPlayerFormID) {
                                          RE::FormID npcID = (target == kPlayerFormID) ? id : target;
                                          SpouseHierarchyManager::GetSingleton().OnSpouseRemoved(npcID);
                                          MARAS::SpouseAssetsService::GetSingleton().StopShareHouseWithPlayer(npcID);
                                          (void)PlayerHouseService::GetSingleton().RemoveTenantFromPlayerHouse(npcID);
                                          ClearPartnerIntent(npcID);
                                      } else {
                                          if (State::WorldStore::GetSingleton().Snapshot()->relationships->GetPendingIntent(State::ActorId{ id }).relatedLover.raw() == target) {
                                              ClearPartnerIntent(id);
                                          }
                                          if (State::WorldStore::GetSingleton().Snapshot()->relationships->GetPendingIntent(State::ActorId{ target }).relatedLover.raw() == id) {
                                              ClearPartnerIntent(target);
                                          }
                                      }
                                      EvaluateChildCustodyOnDivorce(id, target);
                                  },
                                  sendEvent);
    }

    bool NPCRelationshipManager::DemoteToCandidate(RE::FormID npcFormID, RE::FormID targetFormID, bool sendEvent) {
        const RelationshipStatus oldStatus = GetRelationshipStatus(npcFormID, targetFormID);
        if (!IsActivePartnerStatus(oldStatus)) {
            return ChangeStatusCommon(npcFormID, targetFormID, RelationshipStatus::Candidate, nullptr, sendEvent);
        }
        return ChangeStatusCommon(npcFormID, targetFormID, RelationshipStatus::Candidate,
                                  [this](RE::FormID id, RE::FormID target) {
                                      if (target == kPlayerFormID || id == kPlayerFormID) {
                                          RE::FormID npcID = (target == kPlayerFormID) ? id : target;
                                          SpouseHierarchyManager::GetSingleton().OnSpouseRemoved(npcID);
                                          MARAS::SpouseAssetsService::GetSingleton().StopShareHouseWithPlayer(npcID);
                                          (void)PlayerHouseService::GetSingleton().RemoveTenantFromPlayerHouse(npcID);
                                          ClearPartnerIntent(npcID);
                                      } else {
                                          if (State::WorldStore::GetSingleton().Snapshot()->relationships->GetPendingIntent(State::ActorId{ id }).relatedLover.raw() == target) {
                                              ClearPartnerIntent(id);
                                          }
                                          if (State::WorldStore::GetSingleton().Snapshot()->relationships->GetPendingIntent(State::ActorId{ target }).relatedLover.raw() == id) {
                                              ClearPartnerIntent(target);
                                          }
                                      }
                                      EvaluateChildCustodyOnDivorce(id, target);
                                  },
                                  sendEvent);
    }

    bool NPCRelationshipManager::RemoveRelationship(RE::FormID actorA, RE::FormID actorB) {
        if (actorA == 0 || actorB == 0 || actorA == actorB || actorA == 0x07 || actorB == 0x07) {
            MARAS_LOG_WARN("NPCRelationshipManager::RemoveRelationship: Invalid actor arguments (actorA={:08X}, actorB={:08X})", actorA, actorB);
            return false;
        }

        auto& npcIndex = NPCIndex::GetSingleton();
        if (actorA != kPlayerFormID) {
            if (RE::FormID refA = npcIndex.GetRefID(actorA); refA != 0) {
                actorA = refA;
            }
            EnsureRegistered(actorA);
        }
        if (actorB != kPlayerFormID) {
            if (RE::FormID refB = npcIndex.GetRefID(actorB); refB != 0) {
                actorB = refB;
            }
            EnsureRegistered(actorB);
        }

        if (actorA == actorB) {
            return false;
        }

        const RE::FormID baseA = (actorA == kPlayerFormID) ? kPlayerFormID : npcIndex.GetBaseID(actorA);
        const RE::FormID baseB = (actorB == kPlayerFormID) ? kPlayerFormID : npcIndex.GetBaseID(actorB);
        const bool isPlayerPair = (actorA == kPlayerFormID || actorB == kPlayerFormID);
        const RE::FormID playerPartnerID = isPlayerPair ? ((actorA == kPlayerFormID) ? actorB : actorA) : 0;
        const bool wasMarriedToPlayer =
            isPlayerPair && (GetRelationshipStatus(playerPartnerID, kPlayerFormID) == RelationshipStatus::Married);

        // Pre-clear partner intents and spouse household/hierarchy engine state before edge reset/erase
        if (isPlayerPair) {
            ClearPartnerIntent(playerPartnerID);
            SpouseHierarchyManager::GetSingleton().OnSpouseRemoved(playerPartnerID);
            SpouseAssetsService::GetSingleton().StopShareHouseWithPlayer(playerPartnerID);
            (void)PlayerHouseService::GetSingleton().RemoveTenantFromPlayerHouse(playerPartnerID);
            AffectionService::GetSingleton().RemoveNPCData(playerPartnerID);
        } else {
            if (State::WorldStore::GetSingleton().Snapshot()->relationships->GetPendingIntent(State::ActorId{ actorA }).relatedLover.raw() == actorB) {
                ClearPartnerIntent(actorA);
            }
            if (State::WorldStore::GetSingleton().Snapshot()->relationships->GetPendingIntent(State::ActorId{ actorB }).relatedLover.raw() == actorA) {
                ClearPartnerIntent(actorB);
            }
        }

        // Pre-compute inferred vanilla bio parent strips outside Modify (inspects TESNPC::relationships)
        auto hasEngineParentChildLink = [](RE::FormID childActorID, RE::FormID parentActorID) -> bool {
            constexpr RE::FormID kParentChildAssocFormID = 0x000142C6;
            if (childActorID == 0 || parentActorID == 0) return false;
            auto* childBase = ActorSvc::GetActorBase(childActorID);
            auto* parentBase = (parentActorID == kPlayerFormID)
                                   ? (RE::PlayerCharacter::GetSingleton() ? RE::PlayerCharacter::GetSingleton()->GetActorBase() : nullptr)
                                   : ActorSvc::GetActorBase(parentActorID);
            if (!childBase || !parentBase || !childBase->relationships) return false;
            for (auto* rel : *childBase->relationships) {
                if (!rel || !rel->assocType) continue;
                if (rel->assocType->GetFormID() == kParentChildAssocFormID) {
                    if ((rel->npc1 == parentBase && rel->npc2 == childBase) ||
                        (rel->npc2 == parentBase && rel->npc1 == childBase)) {
                        return true;
                    }
                }
            }
            return false;
        };

        struct InferredBioStrip {
            RE::FormID childID{ 0 };
            RE::FormID actorToStrip{ 0 };
        };
        std::vector<InferredBioStrip> bioStrips;
        {
            const auto snap = State::WorldStore::GetSingleton().Snapshot();
            for (const auto& [cId, rec] : snap->lineage->AllChildren()) {
                if (rec.isBiologicalChild || rec.isAdopted) continue;
                const bool aIsBio = MatchesActor(rec.bioMother.raw(), actorA) || MatchesActor(rec.bioFather.raw(), actorA);
                const bool bIsBio = MatchesActor(rec.bioMother.raw(), actorB) || MatchesActor(rec.bioFather.raw(), actorB);
                if (aIsBio && bIsBio) {
                    const bool aEngineLink = hasEngineParentChildLink(cId.raw(), actorA);
                    const bool bEngineLink = hasEngineParentChildLink(cId.raw(), actorB);
                    if (aEngineLink && !bEngineLink) {
                        bioStrips.push_back({ cId.raw(), actorB });
                    } else if (bEngineLink && !aEngineLink) {
                        bioStrips.push_back({ cId.raw(), actorA });
                    }
                }
            }
        }

        std::unordered_set<RE::FormID> affectedActors{ actorA, actorB };
        std::vector<std::pair<State::CanonicalPairKey, std::size_t>> cleanedCheatLogs;
        std::array<RE::FormID, 2> addedCandidateActors{};

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            auto removePendingFor = [&](RE::FormID ownerBase, RE::FormID partnerBase, RE::FormID partnerRef) {
                if (ownerBase == 0) return;
                const auto& allPending = ws.actors->AllPendingRelationships();
                const auto it = allPending.find(State::BaseId{ ownerBase });
                if (it == allPending.end()) return;
                std::vector<State::PendingRelationshipEntry> kept;
                kept.reserve(it->second.size());
                for (const auto& entry : it->second) {
                    const bool matches = (partnerBase != 0 && entry.partnerBaseId.raw() == partnerBase) ||
                                         (partnerRef != 0 && entry.partnerRefId.raw() == partnerRef);
                    if (!matches) {
                        kept.push_back(entry);
                    }
                }
                (void)ws.actors.Write().ErasePendingRelationshipsForBase(State::BaseId{ ownerBase });
                for (const auto& entry : kept) {
                    ws.actors.Write().AddPendingRelationship(State::BaseId{ ownerBase }, entry);
                }
            };
            removePendingFor(baseA, baseB, actorB);
            removePendingFor(baseB, baseA, actorA);

            // Clean any AffairRecord where (actorA, actorB) is (cheater, paramour) or (cheater, deceivedPartner)
            auto matchesRemovedPair = [actorA, actorB](const State::AffairRecord& rec) -> bool {
                const bool matchAffair = (rec.cheater.raw() == actorA && rec.paramour.raw() == actorB) ||
                                         (rec.cheater.raw() == actorB && rec.paramour.raw() == actorA);
                const bool matchVictim = (rec.cheater.raw() == actorA && rec.deceivedPartner.raw() == actorB) ||
                                         (rec.cheater.raw() == actorB && rec.deceivedPartner.raw() == actorA);
                return matchAffair || matchVictim;
            };

            std::unordered_set<State::CanonicalPairKey, State::CanonicalPairKeyHash> affectedCouplePairs;
            for (const auto& [affId, rec] : ws.affairs->AllRecords()) {
                (void)affId;
                if (matchesRemovedPair(rec)) {
                    if (!rec.cheater.IsNull()) affectedActors.insert(rec.cheater.raw());
                    if (!rec.deceivedPartner.IsNull()) affectedActors.insert(rec.deceivedPartner.raw());
                    if (!rec.paramour.IsNull()) affectedActors.insert(rec.paramour.raw());
                    affectedCouplePairs.insert(rec.CouplePair());
                }
            }
            for (const auto& couplePair : affectedCouplePairs) {
                const auto existing = ws.affairs->GetForCouplePair(couplePair);
                std::vector<State::AffairRecord> kept;
                std::size_t removedCount = 0;
                for (const auto& rec : existing) {
                    if (matchesRemovedPair(rec)) {
                        ++removedCount;
                    } else {
                        kept.push_back(rec);
                    }
                }
                ws.affairs.Write().EraseForCouplePair(couplePair);
                for (const auto& rec : kept) {
                    ws.affairs.Write().ImportSerializedRecord(
                        rec.CouplePair(),
                        rec.cheater,
                        rec.deceivedPartner,
                        rec.paramour,
                        rec.incidentCount,
                        rec.lastTimestamp,
                        rec.isSecret,
                        rec.affairType,
                        rec.affairStatus,
                        rec.affairStage,
                        rec.stageStartDay);
                }
                if (removedCount > 0) {
                    cleanedCheatLogs.emplace_back(couplePair, removedCount);
                }
            }

            if (isPlayerPair) {
                // Reset the (npcID, Player) pair to a clean Candidate record with no history or flags
                auto& edge = ws.relationships.Write().GetOrCreateEdge(State::ActorId{ playerPartnerID }, State::ActorId::Player());
                edge.status = State::RelationshipStatus::Candidate;
                edge.originalStatus = State::RelationshipStatus::Unknown;
                edge.isOpen = false;
                edge.statusHistory.clear();
            } else {
                // NPC-to-NPC pair: completely erase the pair record
                (void)ws.relationships.Write().EraseEdge(State::ActorId{ actorA }, State::ActorId{ actorB });
                std::size_t idx = 0;
                for (RE::FormID npcID : { actorA, actorB }) {
                    if (ws.relationships->FindEdge(State::ActorId{ npcID }, State::ActorId::Player()) == nullptr) {
                        auto& playerEdge = ws.relationships.Write().GetOrCreateEdge(State::ActorId{ npcID }, State::ActorId::Player());
                        playerEdge.status = State::RelationshipStatus::Candidate;
                        addedCandidateActors[idx++] = npcID;
                    }
                }
            }

            for (const auto& strip : bioStrips) {
                if (auto* child = ws.lineage.Write().FindChildMutable(State::ChildId{ strip.childID })) {
                    if (MatchesActor(child->bioMother.raw(), strip.actorToStrip)) {
                        child->bioMother = State::ActorId{};
                    }
                    if (MatchesActor(child->bioFather.raw(), strip.actorToStrip)) {
                        child->bioFather = State::ActorId{};
                    }
                }
            }
        });

        for (const auto& [couplePair, count] : cleanedCheatLogs) {
            MARAS_LOG_INFO("RemoveRelationship: Cleaned {} cheating record(s) from pair ({:08X}, {:08X})",
                           count, couplePair.first.raw(), couplePair.second.raw());
        }

        if (isPlayerPair) {
            UpdateTrackedFactionRank(playerPartnerID, RelationshipStatus::Candidate);
            ManageFactions(playerPartnerID, kPlayerFormID, RelationshipStatus::Candidate);
        } else {
            for (const RE::FormID npcID : addedCandidateActors) {
                if (npcID != 0) {
                    UpdateTrackedFactionRank(npcID, RelationshipStatus::Candidate);
                }
            }
            ManageFactions(actorA, actorB, RelationshipStatus::Candidate);
            ManageFactions(actorB, actorA, RelationshipStatus::Candidate);
        }

        // Resolve custody of any shared children or unborn pregnancies using the Divorce Custody rules
        EvaluateChildCustodyOnDivorce(actorA, actorB);

        for (RE::FormID id : affectedActors) {
            if (id != 0) {
                RefreshPartnerRelationshipFactions(id);
            }
        }

        RecalculateAndUpdateGlobals();
        if (wasMarriedToPlayer) {
            DispatchPlayerBuffMaintenance();
        }
        PrismaUIDataBridge::GetSingleton().OnStatusChanged(actorA, actorB, static_cast<uint8_t>(RelationshipStatus::Candidate));

        MARAS_LOG_INFO("RemoveRelationship: Completely removed relationship record between {} ({:08X}) and {} ({:08X})",
                       ActorSvc::GetName(actorA), actorA, ActorSvc::GetName(actorB), actorB);
        return true;
    }

    bool NPCRelationshipManager::Breakup(RE::FormID actorA, RE::FormID actorB, bool sendEvent) {
        if (actorA == 0 || actorB == 0 || actorA == actorB) {
            MARAS_LOG_WARN("NPCRelationshipManager::Breakup: Invalid actor arguments (actorA={:08X}, actorB={:08X})", actorA, actorB);
            return false;
        }

        RelationshipStatus currentStatus = GetRelationshipStatus(actorA, actorB);
        switch (currentStatus) {
            case RelationshipStatus::Married:
                return PromoteToDivorced(actorA, actorB, sendEvent);
            case RelationshipStatus::Engaged:
                return PromoteToJilted(actorA, actorB, sendEvent);
            case RelationshipStatus::Dating:
                return PromoteToExDating(actorA, actorB, sendEvent);
            case RelationshipStatus::Lover:
                return PromoteToExLover(actorA, actorB, sendEvent);
            default:
                MARAS_LOG_WARN("NPCRelationshipManager::Breakup: Cannot breakup actors {:08X} and {:08X} - current status is {} ({})",
                               actorA, actorB, static_cast<int>(currentStatus), Utils::RelationshipStatusToString(currentStatus));
                return false;
        }
    }

    bool NPCRelationshipManager::PromoteToStatusWith(RE::FormID npcFormID, RE::FormID targetFormID, RelationshipStatus status, bool sendEvent, bool breakLovers) {
        switch (status) {
            case RelationshipStatus::Candidate:
                return DemoteToCandidate(npcFormID, targetFormID, sendEvent);
            case RelationshipStatus::Dating:
                return PromoteToDating(npcFormID, targetFormID, sendEvent, breakLovers);
            case RelationshipStatus::Lover:
                return PromoteToLover(npcFormID, targetFormID, sendEvent);
            case RelationshipStatus::Engaged:
                return PromoteToEngaged(npcFormID, targetFormID, sendEvent, breakLovers);
            case RelationshipStatus::Married:
                return PromoteToMarried(npcFormID, targetFormID, sendEvent, breakLovers);
            case RelationshipStatus::Divorced:
                return PromoteToDivorced(npcFormID, targetFormID, sendEvent);
            case RelationshipStatus::Jilted:
                return PromoteToJilted(npcFormID, targetFormID, sendEvent);
            case RelationshipStatus::ExDating:
                return PromoteToExDating(npcFormID, targetFormID, sendEvent);
            case RelationshipStatus::ExLover:
                return PromoteToExLover(npcFormID, targetFormID, sendEvent);
            default:
                return false;
        }
    }

    // Bulk retrievals
    std::vector<RE::FormID> NPCRelationshipManager::GetAllRegisteredNPCs() const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        std::vector<RE::FormID> result;
        result.reserve(snap->actors->Size());
        for (const auto& [refId, prof] : snap->actors->AllProfiles()) {
            if (prof.isRegisteredAdult && !refId.IsNull() && !refId.IsPlayer()) {
                result.push_back(refId.raw());
            }
        }
        return result;
    }

    std::vector<RE::FormID> NPCRelationshipManager::GetAllCandidates(RE::FormID targetFormID) const {
        return GetPartners(targetFormID, RelationshipStatus::Candidate);
    }

    std::vector<RE::FormID> NPCRelationshipManager::GetAllEngaged(RE::FormID targetFormID) const {
        return GetPartners(targetFormID, RelationshipStatus::Engaged);
    }

    std::vector<RE::FormID> NPCRelationshipManager::GetAllMarried(RE::FormID targetFormID) const {
        return GetPartners(targetFormID, RelationshipStatus::Married);
    }

    std::vector<RE::FormID> NPCRelationshipManager::GetAllDivorced(RE::FormID targetFormID) const {
        return GetPartners(targetFormID, RelationshipStatus::Divorced);
    }

    std::vector<RE::FormID> NPCRelationshipManager::GetAllJilted(RE::FormID targetFormID) const {
        return GetPartners(targetFormID, RelationshipStatus::Jilted);
    }

    std::vector<RE::FormID> NPCRelationshipManager::GetAllDating(RE::FormID targetFormID) const {
        return GetPartners(targetFormID, RelationshipStatus::Dating);
    }

    std::vector<RE::FormID> NPCRelationshipManager::GetAllLovers(RE::FormID targetFormID) const {
        return GetPartners(targetFormID, RelationshipStatus::Lover);
    }

    std::vector<RE::FormID> NPCRelationshipManager::GetAllExDating(RE::FormID targetFormID) const {
        return GetPartners(targetFormID, RelationshipStatus::ExDating);
    }

    std::vector<RE::FormID> NPCRelationshipManager::GetAllExLovers(RE::FormID targetFormID) const {
        return GetPartners(targetFormID, RelationshipStatus::ExLover);
    }

    std::vector<RE::FormID> NPCRelationshipManager::GetPartners(RE::FormID npcFormID, RelationshipStatus status) const {
        std::vector<RE::FormID> result;
        if (npcFormID == 0) return result;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const State::ActorId actor{ npcFormID };
        const auto targetStateStatus = static_cast<State::RelationshipStatus>(status);
        for (const auto& key : snap->relationships->GetAdjacentPairs(actor)) {
            const auto* edge = snap->relationships->FindEdge(key);
            if (edge && edge->status == targetStateStatus) {
                const RE::FormID partner = key.Other(actor).raw();
                if (partner != 0) result.push_back(partner);
            }
        }
        return result;
    }

    std::vector<RE::FormID> NPCRelationshipManager::GetAllPartners(RE::FormID npcFormID) const {
        std::vector<RE::FormID> result;
        if (npcFormID == 0) return result;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const State::ActorId actor{ npcFormID };
        for (const auto& key : snap->relationships->GetAdjacentPairs(actor)) {
            const auto* edge = snap->relationships->FindEdge(key);
            if (edge &&
                edge->status != State::RelationshipStatus::Unknown &&
                edge->status != State::RelationshipStatus::Candidate) {
                const RE::FormID partner = key.Other(actor).raw();
                if (partner != 0) result.push_back(partner);
            }
        }
        return result;
    }

    // Data access
    std::optional<NPCProfileData> NPCRelationshipManager::GetNPCProfile(RE::FormID npcFormID) const {
        if (npcFormID == 0) return std::nullopt;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* rec = snap->actors->Find(State::RefId{ npcFormID });
        if (!rec) return std::nullopt;
        return LegacyAdapters::ToLegacyNPCProfile(*rec);
    }

    void NPCRelationshipManager::UpsertNPCProfile(const NPCProfileData& profile) {
        if (profile.formID == 0) return;

        RE::FormID resolvedBaseId = 0;
        if (profile.formID == kPlayerFormID) {
            resolvedBaseId = kPlayerBaseID;
        } else if (Foundation::IsDynamicRuntimeActorRawId(profile.formID)) {
            resolvedBaseId = profile.formID;
        } else {
            resolvedBaseId = NPCIndex::GetSingleton().GetBaseID(profile.formID);
            if (resolvedBaseId == 0) {
                if (auto* base = ActorSvc::GetActorBase(profile.formID)) {
                    resolvedBaseId = base->GetFormID();
                }
            }
        }

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            const auto* existing = ws.actors->Find(State::RefId{ profile.formID });
            const State::BaseId effectiveBase =
                (resolvedBaseId != 0) ? State::BaseId{ resolvedBaseId } : (existing ? existing->baseId : State::BaseId{});
            const bool isRegAdult = existing ? existing->isRegisteredAdult : true;
            ws.actors.Write().Upsert(LegacyAdapters::ToActorProfileRecord(profile, effectiveBase, isRegAdult));
        });
    }

    std::optional<NPCRelationshipData> NPCRelationshipManager::GetNPCData(RE::FormID npcFormID, RE::FormID targetFormID) const {
        if (npcFormID == 0 || targetFormID == 0) return std::nullopt;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* edge = snap->relationships->FindEdge(State::ActorId{ npcFormID }, State::ActorId{ targetFormID });
        if (!edge) return std::nullopt;
        const auto coupleAffairs = snap->affairs->GetForCouplePair(edge->pair);
        return LegacyAdapters::ToLegacyRelationshipData(*edge, coupleAffairs);
    }

    std::unordered_map<RelationshipPairKey, NPCRelationshipData, RelationshipPairKeyHash>
    NPCRelationshipManager::GetAllRelationshipPairs() const {
        std::unordered_map<RelationshipPairKey, NPCRelationshipData, RelationshipPairKeyHash> result;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        for (const auto& [pairKey, edge] : snap->relationships->AllEdges()) {
            RelationshipPairKey legacyKey(pairKey.first.raw(), pairKey.second.raw());
            const auto coupleAffairs = snap->affairs->GetForCouplePair(edge.pair);
            result.emplace(legacyKey, LegacyAdapters::ToLegacyRelationshipData(edge, coupleAffairs));
        }
        return result;
    }

    RelationshipStatus NPCRelationshipManager::GetRelationshipStatus(RE::FormID npcFormID, RE::FormID targetFormID) const {
        auto data = GetNPCData(npcFormID, targetFormID);
        return data ? data->status : RelationshipStatus::Unknown;
    }

    SocialClass NPCRelationshipManager::GetSocialClass(RE::FormID npcFormID) const {
        auto profile = GetNPCProfile(npcFormID);
        if (profile) return profile->socialClass;
        if (auto dec = DeceasedRegistry::GetSingleton().FindRecord(npcFormID)) {
            return dec->socialClass;
        }
        return SocialClass::Outcast;
    }

    SkillType NPCRelationshipManager::GetSkillType(RE::FormID npcFormID) const {
        auto profile = GetNPCProfile(npcFormID);
        if (profile) return profile->skillType;
        if (auto dec = DeceasedRegistry::GetSingleton().FindRecord(npcFormID)) {
            return dec->skillType;
        }
        return SkillType::Warrior;
    }

    Temperament NPCRelationshipManager::GetTemperament(RE::FormID npcFormID) const {
        auto profile = GetNPCProfile(npcFormID);
        if (profile) return profile->temperament;
        if (auto dec = DeceasedRegistry::GetSingleton().FindRecord(npcFormID)) {
            return dec->temperament;
        }
        return Temperament::Proud;
    }

    // Home Marker Management
    bool NPCRelationshipManager::SetHomeMarker(RE::FormID npcFormID, RE::FormID markerFormID) {
        if (!EnsureRegistered(npcFormID)) {
            MARAS_LOG_WARN("SetHomeMarker: NPC {:08X} not registered", npcFormID);
            return false;
        }

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* prof = ws.actors.Write().FindMutable(State::RefId{ npcFormID })) {
                prof->legacyHomeMarker = State::RefId{ markerFormID };
            }
        });

        if (markerFormID != 0) {
            SetLinkedRefForHomeMarker(npcFormID, markerFormID);
            MARAS_LOG_DEBUG("Set home marker {:08X} for NPC {:08X} and created linked ref", markerFormID, npcFormID);
        }

        return true;
    }

    std::optional<RE::FormID> NPCRelationshipManager::GetHomeMarker(RE::FormID npcFormID) const {
        auto profile = GetNPCProfile(npcFormID);
        return profile ? profile->homeMarker : std::nullopt;
    }

    void NPCRelationshipManager::ClearCurrentHome(RE::FormID npcFormID) {
        if (npcFormID == 0) {
            return;
        }
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* prof = ws.actors.Write().FindMutable(State::RefId{ npcFormID })) {
                prof->legacyCurrentHome.reset();
            }
        });
        if (!IsSyntheticChildID(npcFormID)) {
            if (auto* homeSandboxKeyword = FormCache::GetSingleton().GetHomeSandboxKeyword()) {
                Utils::RemoveLinkedRef(npcFormID, homeSandboxKeyword);
            }
        }
    }

    RE::TESObjectREFR* NPCRelationshipManager::GetOrCreateHomeMarker(RE::Actor* npcActor) {
        if (!npcActor) return nullptr;

        RE::FormID npcFormID = npcActor->GetFormID();
        auto markerOpt = GetHomeMarker(npcFormID);
        if (markerOpt.has_value() && markerOpt.value() != 0) {
            if (auto* existingMarker = RE::TESForm::LookupByID<RE::TESObjectREFR>(markerOpt.value())) {
                SetLinkedRefForHomeMarker(npcFormID, markerOpt.value());
                return existingMarker;
            }
        }

        auto* homeMarkerStatic = FormCache::GetSingleton().GetHomeSandboxMarkerStatic();
        if (!homeMarkerStatic) {
            MARAS_LOG_WARN("GetOrCreateHomeMarker: homeMarkerStatic form not found");
            return nullptr;
        }

        auto markerRef = npcActor->PlaceObjectAtMe(homeMarkerStatic, true);
        if (!markerRef) {
            if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                markerRef = player->PlaceObjectAtMe(homeMarkerStatic, true);
            }
        }
        if (!markerRef) {
            MARAS_LOG_WARN("GetOrCreateHomeMarker: PlaceObjectAtMe failed for NPC {:08X}", npcFormID);
            return nullptr;
        }

        RE::FormID markerID = markerRef->GetFormID();
        SetHomeMarker(npcFormID, markerID);

        MARAS_LOG_INFO("GetOrCreateHomeMarker: Placed new home marker {:08X} for NPC {:08X}", markerID, npcFormID);
        return markerRef.get();
    }

    bool NPCRelationshipManager::AddTrackedKeyword(RE::FormID npcFormID, RE::FormID keywordFormID) {
        if (npcFormID == 0 || keywordFormID == 0) return false;
        bool updated = false;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* prof = ws.actors.Write().FindMutable(State::RefId{ npcFormID })) {
                prof->addedKeywords.insert(State::FormRawId{ keywordFormID });
                updated = true;
            }
        });
        return updated;
    }

    bool NPCRelationshipManager::RemoveTrackedKeyword(RE::FormID npcFormID, RE::FormID keywordFormID) {
        if (npcFormID == 0 || keywordFormID == 0) return false;
        bool updated = false;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* prof = ws.actors.Write().FindMutable(State::RefId{ npcFormID })) {
                prof->addedKeywords.erase(State::FormRawId{ keywordFormID });
                updated = true;
            }
        });
        return updated;
    }

    // Status change history management
    std::vector<StatusChangeRecord> NPCRelationshipManager::GetStatusHistory(RE::FormID npcFormID, RE::FormID targetFormID) const {
        std::vector<StatusChangeRecord> history;
        if (npcFormID == 0 || targetFormID == 0) return history;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* edge = snap->relationships->FindEdge(State::ActorId{ npcFormID }, State::ActorId{ targetFormID });
        if (!edge) return history;
        history.reserve(edge->statusHistory.size());
        for (const auto& entry : edge->statusHistory) {
            history.push_back(LegacyAdapters::ToLegacyStatusChange(entry));
        }
        return history;
    }

    std::optional<StatusChangeRecord> NPCRelationshipManager::GetStatusHistoryEntry(RE::FormID npcFormID, RE::FormID targetFormID, std::size_t index) const {
        const auto history = GetStatusHistory(npcFormID, targetFormID);
        if (index >= history.size()) {
            return std::nullopt;
        }
        return history[index];
    }

    size_t NPCRelationshipManager::GetStatusHistoryCount(RE::FormID npcFormID, RE::FormID targetFormID) const {
        if (npcFormID == 0 || targetFormID == 0) return 0;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* edge = snap->relationships->FindEdge(State::ActorId{ npcFormID }, State::ActorId{ targetFormID });
        return edge ? edge->statusHistory.size() : 0;
    }

    void NPCRelationshipManager::ClearStatusHistory(RE::FormID npcFormID, RE::FormID targetFormID) {
        if (npcFormID == 0 || targetFormID == 0) return;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            const State::CanonicalPairKey key{ State::ActorId{ npcFormID }, State::ActorId{ targetFormID } };
            if (const auto* existing = ws.relationships->FindEdge(key)) {
                auto updated = *existing;
                updated.statusHistory.clear();
                ws.relationships.Write().UpsertEdge(std::move(updated));
            }
        });
    }

    // Statistics
    size_t NPCRelationshipManager::GetTotalRegisteredCount() const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        size_t count = 0;
        for (const auto& [refId, prof] : snap->actors->AllProfiles()) {
            if (prof.isRegisteredAdult && !refId.IsNull() && !refId.IsPlayer()) {
                ++count;
            }
        }
        return count;
    }

    size_t NPCRelationshipManager::GetCandidateCount(RE::FormID targetFormID) const {
        return CountPartnersWithStatus(targetFormID, RelationshipStatus::Candidate);
    }

    size_t NPCRelationshipManager::GetEngagedCount(RE::FormID targetFormID) const {
        return CountPartnersWithStatus(targetFormID, RelationshipStatus::Engaged);
    }

    size_t NPCRelationshipManager::GetMarriedCount(RE::FormID targetFormID) const {
        return CountPartnersWithStatus(targetFormID, RelationshipStatus::Married);
    }

    size_t NPCRelationshipManager::GetDivorcedCount(RE::FormID targetFormID) const {
        return CountPartnersWithStatus(targetFormID, RelationshipStatus::Divorced);
    }

    size_t NPCRelationshipManager::GetJiltedCount(RE::FormID targetFormID) const {
        return CountPartnersWithStatus(targetFormID, RelationshipStatus::Jilted);
    }

    size_t NPCRelationshipManager::GetExDatingCount(RE::FormID targetFormID) const {
        return CountPartnersWithStatus(targetFormID, RelationshipStatus::ExDating);
    }

    size_t NPCRelationshipManager::GetExLoverCount(RE::FormID targetFormID) const {
        return CountPartnersWithStatus(targetFormID, RelationshipStatus::ExLover);
    }

    // Override management
    const Utils::NPCOverrideData* NPCRelationshipManager::FindOverrideData(RE::FormID npcFormID) const {
        auto it = npcOverrides.find(npcFormID);
        if (it != npcOverrides.end()) {
            return &it->second;
        }

        auto npcBase = ActorSvc::GetActorBase(npcFormID);
        if (npcBase) {
            RE::FormID baseFormID = npcBase->GetFormID();
            if (baseFormID != npcFormID) {
                auto baseIt = npcOverrides.find(baseFormID);
                if (baseIt != npcOverrides.end()) {
                    return &baseIt->second;
                }
            }
        }

        return nullptr;
    }

    bool NPCRelationshipManager::LoadOverridesFromFolder(const std::string& folderPath) {
        MARAS_LOG_INFO("Loading NPC type overrides from folder: {}", folderPath);
        npcOverrides.clear();
        return Utils::JsonOverrideLoader::LoadOverridesFromFolder(folderPath, npcOverrides);
    }

    bool NPCRelationshipManager::HasSocialClassOverride(RE::FormID npcFormID) const {
        auto data = FindOverrideData(npcFormID);
        return data && data->HasSocialClassOverride();
    }

    bool NPCRelationshipManager::HasSkillTypeOverride(RE::FormID npcFormID) const {
        auto data = FindOverrideData(npcFormID);
        return data && data->HasSkillTypeOverride();
    }

    bool NPCRelationshipManager::HasTemperamentOverride(RE::FormID npcFormID) const {
        auto data = FindOverrideData(npcFormID);
        return data && data->HasTemperamentOverride();
    }

    std::optional<std::string> NPCRelationshipManager::GetSocialClassOverride(RE::FormID npcFormID) const {
        auto data = FindOverrideData(npcFormID);
        return (data && data->HasSocialClassOverride()) ? std::make_optional(data->socialClass) : std::nullopt;
    }

    std::optional<std::string> NPCRelationshipManager::GetSkillTypeOverride(RE::FormID npcFormID) const {
        auto data = FindOverrideData(npcFormID);
        return (data && data->HasSkillTypeOverride()) ? std::make_optional(data->skillType) : std::nullopt;
    }

    std::optional<std::string> NPCRelationshipManager::GetTemperamentOverride(RE::FormID npcFormID) const {
        auto data = FindOverrideData(npcFormID);
        return (data && data->HasTemperamentOverride()) ? std::make_optional(data->temperament) : std::nullopt;
    }

    size_t NPCRelationshipManager::GetOverrideCount() const { return npcOverrides.size(); }

    Utils::JsonOverrideLoader::LoadStatistics NPCRelationshipManager::GetLastOverrideLoadStats() const {
        return Utils::JsonOverrideLoader::GetLastLoadStatistics();
    }

    // Type Determination Methods
    SocialClass NPCRelationshipManager::DetermineSocialClass(RE::FormID npcFormID) {
        return NPCTypeDeterminer::DetermineSocialClass(npcFormID,
                                                       [this](RE::FormID id) { return GetSocialClassOverride(id); });
    }

    SkillType NPCRelationshipManager::DetermineSkillType(RE::FormID npcFormID) {
        return NPCTypeDeterminer::DetermineSkillType(npcFormID,
                                                     [this](RE::FormID id) { return GetSkillTypeOverride(id); });
    }

    Temperament NPCRelationshipManager::DetermineTemperament(RE::FormID npcFormID) {
        return NPCTypeDeterminer::DetermineTemperament(
            npcFormID, [this](RE::FormID id) { return GetTemperamentOverride(id); },
            [this](RE::FormID id) { return GetSocialClass(id); }, [this](RE::FormID id) { return GetSkillType(id); });
    }

    // Faction Management Methods
    bool NPCRelationshipManager::AddToFaction(RE::FormID npcFormID, RE::TESFaction* faction, std::int8_t rank) {
        if (!faction) return false;
        auto actor = NPCIndex::GetSingleton().GetActorFromFormId(npcFormID);
        if (!actor) return false;
        FactionService::InternalFactionScope scope;
        Utils::AddToFaction(actor, faction, rank);
        return true;
    }

    bool NPCRelationshipManager::AddToSocialClassFaction(RE::FormID npcFormID, std::int8_t rank) {
        auto faction = FormCache::GetSingleton().GetSpouseSocialClassFaction();
        return AddToFaction(npcFormID, faction, rank);
    }

    bool NPCRelationshipManager::AddToSkillTypeFaction(RE::FormID npcFormID, std::int8_t rank) {
        auto faction = FormCache::GetSingleton().GetSpouseSkillTypeFaction();
        return AddToFaction(npcFormID, faction, rank);
    }

    bool NPCRelationshipManager::AddToTemperamentFaction(RE::FormID npcFormID, std::int8_t rank) {
        auto faction = FormCache::GetSingleton().GetSpouseTemperamentFaction();
        return AddToFaction(npcFormID, faction, rank);
    }

    namespace {
        void SafeAddToFaction(RE::Actor* actor, RE::TESFaction* faction) {
            if (!actor || !faction) return;
            MARAS_LOG_INFO("Adding faction {:08X} to actor {}({:08X})", faction->GetFormID(), actor->GetName(), actor->GetFormID());
            Utils::AddToFaction(actor, faction, 0);
        }

        void SafeRemoveFromFaction(RE::Actor* actor, RE::TESFaction* faction) {
            if (!actor || !faction) return;
            MARAS_LOG_INFO("Removing faction {:08X} from actor {}({:08X})", faction->GetFormID(), actor->GetName(), actor->GetFormID());
            Utils::RemoveFromFaction(actor, faction);
        }

        void SafeRemoveKeyword(RE::Actor* actor, RE::BGSKeyword* keyword) {
            if (!actor || !keyword) return;
            MARAS_LOG_INFO("Removing keyword {:08X} from actor {}({:08X})", keyword->GetFormID(), actor->GetName(), actor->GetFormID());
            auto actorBase = ActorSvc::GetActorBase(actor->GetFormID());
            if (actorBase && actorBase->HasKeyword(keyword)) {
                actorBase->RemoveKeyword(keyword);
            }
        }
    }  // namespace

    void NPCRelationshipManager::ManageFactions(RE::FormID npcFormID, RE::FormID targetFormID, RelationshipStatus status) {
        if (npcFormID == kPlayerFormID || npcFormID == 0) return;

        auto actor = NPCIndex::GetSingleton().GetActorFromFormId(npcFormID);
        if (!actor) return;

        FactionService::InternalFactionScope scope;
        auto& cache = FormCache::GetSingleton();

        if (targetFormID == kPlayerFormID) {
            switch (status) {
                case RelationshipStatus::Candidate:
                    SafeRemoveFromFaction(actor, cache.GetMarriageAskedFaction());
                    SafeRemoveFromFaction(actor, cache.GetCourtingFaction());
                    SafeRemoveFromFaction(actor, cache.GetMarriedFaction());
                    SafeRemoveFromFaction(actor, cache.GetPlayerFaction());
                    SafeRemoveFromFaction(actor, cache.GetPlayerBedOwnershipFaction());
                    SafeRemoveKeyword(actor, cache.GetIgnoreProposeKeyword());
                    SafeAddToFaction(actor, cache.GetMarriagePotentialFaction());
                    if (GetMarriedCount(kPlayerFormID) == 0) {
                        if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                            SafeRemoveFromFaction(player, cache.GetMarriedFaction());
                        }
                    }
                    break;
                case RelationshipStatus::Dating:
                case RelationshipStatus::Lover:
                    SafeRemoveKeyword(actor, cache.GetIgnoreProposeKeyword());
                    break;
                case RelationshipStatus::Engaged:
                    SafeAddToFaction(actor, cache.GetMarriageAskedFaction());
                    SafeAddToFaction(actor, cache.GetCourtingFaction());
                    SafeRemoveKeyword(actor, cache.GetIgnoreProposeKeyword());
                    break;
                case RelationshipStatus::Married:
                    SafeRemoveFromFaction(actor, cache.GetMarriageAskedFaction());
                    SafeRemoveFromFaction(actor, cache.GetCourtingFaction());
                    SafeRemoveFromFaction(actor, cache.GetPotentialHirelingFaction());
                    SafeAddToFaction(actor, cache.GetMarriedFaction());
                    SafeAddToFaction(actor, cache.GetPlayerFaction());
                    SafeAddToFaction(actor, cache.GetPlayerBedOwnershipFaction());
                    SafeRemoveKeyword(actor, cache.GetIgnoreProposeKeyword());
                    if (!Settings::GetSingleton().GetEnablePolygamy()) {
                        if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                            SafeAddToFaction(player, cache.GetMarriedFaction());
                        }
                    }
                    break;
                case RelationshipStatus::Divorced:
                    SafeRemoveFromFaction(actor, cache.GetMarriedFaction());
                    SafeRemoveFromFaction(actor, cache.GetPlayerFaction());
                    SafeRemoveFromFaction(actor, cache.GetPlayerBedOwnershipFaction());
                    SafeRemoveKeyword(actor, cache.GetIgnoreProposeKeyword());
                    if (GetMarriedCount(kPlayerFormID) == 0) {
                        if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                            SafeRemoveFromFaction(player, cache.GetMarriedFaction());
                        }
                    }
                    break;
                case RelationshipStatus::Jilted:
                    SafeRemoveFromFaction(actor, cache.GetMarriageAskedFaction());
                    SafeRemoveFromFaction(actor, cache.GetCourtingFaction());
                    SafeRemoveKeyword(actor, cache.GetIgnoreProposeKeyword());
                    break;
                case RelationshipStatus::Unknown:
                default:
                    break;
            }
        } else {
            // NPC x NPC relationship - strictly ensure player marriage factions are NOT applied!
            switch (status) {
                case RelationshipStatus::Married:
                    SafeRemoveFromFaction(actor, cache.GetMarriageAskedFaction());
                    SafeRemoveFromFaction(actor, cache.GetCourtingFaction());
                    SafeRemoveFromFaction(actor, cache.GetMarriedFaction());
                    SafeRemoveFromFaction(actor, cache.GetPlayerFaction());
                    SafeRemoveFromFaction(actor, cache.GetPlayerBedOwnershipFaction());
                    SafeRemoveKeyword(actor, cache.GetIgnoreProposeKeyword());
                    if (!Settings::GetSingleton().GetEnablePolygamy()) {
                        SafeRemoveFromFaction(actor, cache.GetMarriagePotentialFaction());
                    }
                    break;
                case RelationshipStatus::Divorced:
                    SafeRemoveFromFaction(actor, cache.GetMarriedFaction());
                    SafeRemoveFromFaction(actor, cache.GetPlayerFaction());
                    SafeRemoveFromFaction(actor, cache.GetPlayerBedOwnershipFaction());
                    SafeRemoveKeyword(actor, cache.GetIgnoreProposeKeyword());
                    SafeAddToFaction(actor, cache.GetMarriagePotentialFaction());
                    break;
                case RelationshipStatus::Engaged:
                    SafeRemoveFromFaction(actor, cache.GetMarriageAskedFaction());
                    SafeRemoveFromFaction(actor, cache.GetCourtingFaction());
                    SafeRemoveKeyword(actor, cache.GetIgnoreProposeKeyword());
                    break;
                case RelationshipStatus::Candidate:
                    SafeAddToFaction(actor, cache.GetMarriagePotentialFaction());
                    break;
                default:
                    break;
            }
        }
    }

    bool NPCRelationshipManager::SetSocialClass(RE::FormID npcFormID, std::int8_t socialClass) {
        if (socialClass < 0 || socialClass >= static_cast<std::int8_t>(SocialClass::_Count)) return false;
        if (!EnsureRegistered(npcFormID)) return false;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* prof = ws.actors.Write().FindMutable(State::RefId{ npcFormID })) {
                prof->socialClass = static_cast<State::SocialClass>(socialClass);
            }
        });
        AddToSocialClassFaction(npcFormID, socialClass);
        return true;
    }

    bool NPCRelationshipManager::SetSkillType(RE::FormID npcFormID, std::int8_t skillType) {
        if (skillType < 0 || skillType >= static_cast<std::int8_t>(SkillType::_Count)) return false;
        if (!EnsureRegistered(npcFormID)) return false;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* prof = ws.actors.Write().FindMutable(State::RefId{ npcFormID })) {
                prof->skillType = static_cast<State::SkillType>(skillType);
            }
        });
        AddToSkillTypeFaction(npcFormID, skillType);
        return true;
    }

    bool NPCRelationshipManager::SetTemperament(RE::FormID npcFormID, std::int8_t temperament) {
        if (temperament < 0 || temperament >= static_cast<std::int8_t>(Temperament::_Count)) return false;
        if (!EnsureRegistered(npcFormID)) return false;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* prof = ws.actors.Write().FindMutable(State::RefId{ npcFormID })) {
                prof->temperament = static_cast<State::Temperament>(temperament);
            }
        });
        AddToTemperamentFaction(npcFormID, temperament);
        return true;
    }

    bool NPCRelationshipManager::EnsureRegistered(RE::FormID inputFormID) {
        MARAS_LOG_TRACE("EnsureRegistered(FormID={:08X}) entered", inputFormID);
        if (inputFormID == kPlayerFormID || inputFormID == 0 || inputFormID == 0x07) return true;

        auto& npcIndex = NPCIndex::GetSingleton();
        RE::FormID npcFormID = npcIndex.GetRefID(inputFormID);
        if (npcFormID == 0) {
            npcFormID = inputFormID;
        }

        if (npcFormID == kPlayerFormID || npcFormID == 0 || npcFormID == 0x07) return true;
        if (IsRegistered(npcFormID)) {
            MARAS_LOG_TRACE("EnsureRegistered: {:08X} is already registered", npcFormID);
            return true;
        }

        if (DeceasedRegistry::GetSingleton().IsDeceased(npcFormID)) {
            MARAS_LOG_DEBUG("EnsureRegistered: Skipping deceased NPC {:08X}", npcFormID);
            return false;
        }
        if (auto* loadedActor = RE::TESForm::LookupByID<RE::Actor>(npcFormID); loadedActor && loadedActor->IsDead()) {
            MARAS_LOG_DEBUG("EnsureRegistered: Skipping dead loaded actor {:08X}", npcFormID);
            return false;
        }

        auto startTime = std::chrono::high_resolution_clock::now();

        if (!ActorSvc::IsValid(npcFormID)) {
            MARAS_LOG_WARN("EnsureRegistered: Cannot find valid actor/base for FormID {:08X}", npcFormID);
            return false;
        }

        SocialClass socialClass = DetermineSocialClass(npcFormID);
        SkillType skillType = DetermineSkillType(npcFormID);
        Temperament temperament = [this, npcFormID, socialClass, skillType]() {
            if (auto ov = GetTemperamentOverride(npcFormID); ov.has_value()) {
                return Utils::StringToTemperament(ov.value());
            }
            return NPCTypeDeterminer::ComputeTemperament(socialClass, skillType);
        }();

        RE::FormID resolvedBaseID = 0;
        if (Foundation::IsDynamicRuntimeActorRawId(npcFormID)) {
            resolvedBaseID = npcFormID;
        } else {
            resolvedBaseID = npcIndex.GetBaseID(npcFormID);
            if (resolvedBaseID == 0) {
                if (auto* actorBase = ActorSvc::GetActorBase(npcFormID)) {
                    resolvedBaseID = actorBase->GetFormID();
                }
            }
        }

        bool newlyRegistered = false;
        try {
            State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                const State::RefId refId{ npcFormID };
                if (ws.actors->IsRegisteredAdult(refId)) {
                    return;
                }
                NPCProfileData legacyProfile(npcFormID, socialClass, skillType, temperament);
                if (const auto* existing = ws.actors->Find(refId)) {
                    if (existing->legacyOriginalHome.has_value()) {
                        legacyProfile.originalHome = existing->legacyOriginalHome->raw();
                    }
                    if (existing->legacyCurrentHome.has_value()) {
                        legacyProfile.currentHome = existing->legacyCurrentHome->raw();
                    }
                    if (existing->legacyHomeMarker.has_value()) {
                        legacyProfile.homeMarker = existing->legacyHomeMarker->raw();
                    }
                    for (const auto kw : existing->addedKeywords) {
                        legacyProfile.addedKeywords.insert(kw.raw());
                    }
                    if (resolvedBaseID == 0) {
                        resolvedBaseID = existing->baseId.raw();
                    }
                }
                ws.actors.Write().Upsert(
                    LegacyAdapters::ToActorProfileRecord(legacyProfile, State::BaseId{ resolvedBaseID }, true));
                newlyRegistered = true;
            });
        } catch (const std::exception& e) {
            MARAS_LOG_ERROR("Exception while storing NPC profile for {:08X}: {}", npcFormID, e.what());
            return false;
        }

        if (!newlyRegistered) {
            return true;
        }

        MARAS_LOG_INFO("EnsureRegistered: registered {:08X} in WorldStore", npcFormID);
        if (GetRelationshipStatus(npcFormID, kPlayerFormID) == RelationshipStatus::Unknown) {
            UpdateTrackedFactionRank(npcFormID, RelationshipStatus::Candidate);
        }

        try {
            AddToSocialClassFaction(npcFormID, static_cast<std::int8_t>(socialClass));
        } catch (const std::exception& e) {
            MARAS_LOG_ERROR("AddToSocialClassFaction exception for {:08X}: {}", npcFormID, e.what());
        }
        try {
            AddToSkillTypeFaction(npcFormID, static_cast<std::int8_t>(skillType));
        } catch (const std::exception& e) {
            MARAS_LOG_ERROR("AddToSkillTypeFaction exception for {:08X}: {}", npcFormID, e.what());
        }
        try {
            AddToTemperamentFaction(npcFormID, static_cast<std::int8_t>(temperament));
        } catch (const std::exception& e) {
            MARAS_LOG_ERROR("AddToTemperamentFaction exception for {:08X}: {}", npcFormID, e.what());
        }
        try {
            UpdateCanFormNonCheatingMainFaction(npcFormID);
        } catch (const std::exception& e) {
            MARAS_LOG_ERROR("UpdateCanFormNonCheatingMainFaction exception for {:08X}: {}", npcFormID, e.what());
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);

        MARAS_LOG_INFO("Registered NPC {} ({:08X}) profile in {} microseconds (SC: {}, ST: {}, T: {})",
                       ActorSvc::GetName(npcFormID), npcFormID, duration.count(),
                       Utils::SocialClassToString(socialClass), Utils::SkillTypeToString(skillType),
                       Utils::TemperamentToString(temperament));

        if (resolvedBaseID != 0 && resolvedBaseID != npcFormID) {
            TryResolvePendingRelationships(resolvedBaseID, npcFormID);
        }

        RecalculateAndUpdateGlobals();
        return true;
    }

    bool NPCRelationshipManager::HasMarasMaritalHistory(RE::FormID npcFormID) const {
        if (npcFormID == 0) return false;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        for (const auto& key : snap->relationships->GetAdjacentPairs(State::ActorId{ npcFormID })) {
            const auto* edge = snap->relationships->FindEdge(key);
            if (!edge) continue;
            const auto status = static_cast<RelationshipStatus>(edge->status);
            if (status == RelationshipStatus::Engaged ||
                status == RelationshipStatus::Married ||
                status == RelationshipStatus::Divorced ||
                status == RelationshipStatus::Jilted) {
                return true;
            }
            for (const auto& entry : edge->statusHistory) {
                const auto fromSt = static_cast<RelationshipStatus>(entry.fromStatus);
                const auto toSt = static_cast<RelationshipStatus>(entry.toStatus);
                if (fromSt == RelationshipStatus::Engaged || fromSt == RelationshipStatus::Married ||
                    fromSt == RelationshipStatus::Divorced || fromSt == RelationshipStatus::Jilted ||
                    toSt == RelationshipStatus::Engaged || toSt == RelationshipStatus::Married ||
                    toSt == RelationshipStatus::Divorced || toSt == RelationshipStatus::Jilted) {
                    return true;
                }
            }
        }
        return false;
    }

    RelationshipStatus NPCRelationshipManager::GetOriginalRelationshipStatus(RE::FormID actorA, RE::FormID actorB) const {
        if (actorA == 0 || actorB == 0) return RelationshipStatus::Unknown;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* edge = snap->relationships->FindEdge(State::ActorId{ actorA }, State::ActorId{ actorB });
        if (edge) {
            return static_cast<RelationshipStatus>(edge->originalStatus);
        }
        return RelationshipStatus::Unknown;
    }

    bool NPCRelationshipManager::SetOriginalRelationshipStatus(RE::FormID actorA, RE::FormID actorB, RelationshipStatus status) {
        EnsureRegistered(actorA);
        EnsureRegistered(actorB);
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            auto& edge = ws.relationships.Write().GetOrCreateEdge(State::ActorId{ actorA }, State::ActorId{ actorB });
            edge.originalStatus = static_cast<State::RelationshipStatus>(status);
        });

        MARAS_LOG_INFO("Set original relationship status between {:08X} and {:08X} to {}",
                       actorA, actorB, Utils::RelationshipStatusToString(status));
        return true;
    }

    bool NPCRelationshipManager::ApplyImportedVanillaStatus(RE::FormID actorA, RE::FormID actorB,
                                                            RelationshipStatus vanillaStatus, RelationshipStatus targetStatus) {
        // Divorced / ExDating / ExLover are only reachable from Married / Dating / Lover. An imported vanilla
        // relationship that was superseded (e.g. by the Player) must pass through its vanilla state first.
        if (targetStatus == RelationshipStatus::Divorced || targetStatus == RelationshipStatus::ExDating ||
            targetStatus == RelationshipStatus::ExLover) {
            State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                auto& edge = ws.relationships.Write().GetOrCreateEdge(State::ActorId{ actorA }, State::ActorId{ actorB });
                edge.status = static_cast<State::RelationshipStatus>(vanillaStatus);
            });
        }
        SkyrimNetMemoryService::SuppressScope suppressMemories;
        return PromoteToStatusWith(actorA, actorB, targetStatus, /*sendEvent=*/false, /*breakLovers=*/false);
    }

    void NPCRelationshipManager::ScanExistingRelationships(bool isNewGame) {
        auto startTime = std::chrono::steady_clock::now();
        SkyrimNetMemoryService::SuppressScope suppressMemories;
        MARAS_LOG_INFO("Starting scanning of existing in-game relationships (Courting, Spouse, Lover, Parent-Child) [isNewGame={}]...", isNewGame);

        auto dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            MARAS_LOG_ERROR("ScanExistingRelationships: TESDataHandler unavailable");
            return;
        }

        constexpr RE::FormID kCourtingAssocFormID    = 0x0001EE23;
        constexpr RE::FormID kParentChildAssocFormID = 0x000142C6;
        constexpr RE::FormID kSpouseAssocFormID      = 0x000142CA;

        const auto& relArray = dataHandler->GetFormArray<RE::BGSRelationship>();
        auto& npcIndex = NPCIndex::GetSingleton();

        // Buffer for collecting and merging child-parent records during scan
        struct ScannedChildEntry {
            RE::FormID childBaseID{ 0 };
            RE::FormID parentABaseID{ 0 };
            RE::FormID parentBBaseID{ 0 };
            bool isAdopted{ false };
        };
        std::vector<ScannedChildEntry> scannedChildren;

        struct PlayerRelationshipAuditEntry {
            RE::FormID npcBaseID{ 0 };
            RE::FormID npcRefID{ 0 };
            bool isRef{ false };
            std::string npcName;
            std::string pluginName;
            RelationshipStatus vanillaStatus{ RelationshipStatus::Unknown };
            RelationshipStatus targetMarasStatus{ RelationshipStatus::Unknown };
            bool defaultChecked{ false };
        };
        std::vector<PlayerRelationshipAuditEntry> playerAuditEntries;

        auto isPlayerForm = [](RE::FormID id) -> bool {
            return id == kPlayerBaseID || id == kPlayerRefID;
        };

        auto statusPriority = [](RelationshipStatus s) -> int {
            switch (s) {
                case RelationshipStatus::Married: return 3;
                case RelationshipStatus::Dating:  return 2;
                case RelationshipStatus::Lover:   return 1;
                default:                          return 0;
            }
        };

        // Pre-collect set of all vanilla NPC-NPC relationship pair keys
        std::unordered_set<RelationshipPairKey, RelationshipPairKeyHash> vanillaPairKeys;
        for (auto* rel : relArray) {
            if (!rel || !rel->npc1 || !rel->npc2) continue;
            RE::FormID b1 = rel->npc1->GetFormID();
            RE::FormID b2 = rel->npc2->GetFormID();
            if (isPlayerForm(b1) || isPlayerForm(b2)) {
                continue;
            }
            RE::FormID id1 = npcIndex.GetRefID(b1);
            RE::FormID id2 = npcIndex.GetRefID(b2);
            if (id1 != 0 && id2 != 0) {
                vanillaPairKeys.emplace(id1, id2);
            }
        }

        // Check if an actor has an existing MARAS relationship (not Candidate/Unknown) with a non-vanilla partner (e.g. Player)
        auto hasNonVanillaMarasRelationship = [&vanillaPairKeys](RE::FormID npcFormID) -> bool {
            if (npcFormID == 0) return false;
            const auto snap = State::WorldStore::GetSingleton().Snapshot();
            for (const auto& key : snap->relationships->GetAdjacentPairs(State::ActorId{ npcFormID })) {
                const auto* edge = snap->relationships->FindEdge(key);
                if (edge &&
                    edge->status != State::RelationshipStatus::Candidate &&
                    edge->status != State::RelationshipStatus::Unknown) {
                    RelationshipPairKey legacyKey(key.first.raw(), key.second.raw());
                    if (vanillaPairKeys.find(legacyKey) == vanillaPairKeys.end()) {
                        return true;
                    }
                }
            }
            return false;
        };

        // Check if an actor is in an active, non-open main-tier relationship (Dating/Engaged/Married) with a non-vanilla partner
        auto hasNonVanillaMainTierRelationship = [&vanillaPairKeys](RE::FormID npcFormID) -> bool {
            if (npcFormID == 0) return false;
            const auto snap = State::WorldStore::GetSingleton().Snapshot();
            for (const auto& key : snap->relationships->GetAdjacentPairs(State::ActorId{ npcFormID })) {
                const auto* edge = snap->relationships->FindEdge(key);
                if (edge && IsMainTier(static_cast<RelationshipStatus>(edge->status)) && !edge->isOpen) {
                    RelationshipPairKey legacyKey(key.first.raw(), key.second.raw());
                    if (vanillaPairKeys.find(legacyKey) == vanillaPairKeys.end()) {
                        return true;
                    }
                }
            }
            return false;
        };

        auto* marriedFaction = FormCache::GetSingleton().GetMarriedFaction();
        auto* courtingFaction = FormCache::GetSingleton().GetCourtingFaction();

        size_t scannedCount = 0;
        size_t importedCount = 0;

        for (auto* rel : relArray) {
            if (!rel) continue;
            scannedCount++;

            auto* npc1Base = rel->npc1;
            auto* npc2Base = rel->npc2;
            if (!npc1Base || !npc2Base) continue;

            RE::FormID baseID1 = npc1Base->GetFormID();
            RE::FormID baseID2 = npc2Base->GetFormID();

            const bool isPlayerPair = isPlayerForm(baseID1) || isPlayerForm(baseID2);

            auto entry1 = npcIndex.FindByFormID(baseID1);
            auto entry2 = npcIndex.FindByFormID(baseID2);

            // Check if either participant is a child (uses cached isChild flag from index)
            bool isChild1 = entry1 ? entry1->isChild : (npc1Base->GetRace() && npc1Base->GetRace()->data.flags.any(RE::RACE_DATA::Flag::kChild));
            bool isChild2 = entry2 ? entry2->isChild : (npc2Base->GetRace() && npc2Base->GetRace()->data.flags.any(RE::RACE_DATA::Flag::kChild));

            if (isChild1 || isChild2) {
                if (isNewGame && isPlayerPair) {
                    continue;
                }
                // Strictly require an adult-child pair (isChild1 != isChild2) AND a Parent/Child association type (kParentChildAssocFormID)
                if (isChild1 != isChild2 && rel->assocType && rel->assocType->GetFormID() == kParentChildAssocFormID) {
                    RE::FormID cBaseID = (isChild1 ? npc1Base : npc2Base)->GetFormID();
                    RE::FormID pBaseID = (isChild1 ? npc2Base : npc1Base)->GetFormID();

                    auto it = std::find_if(scannedChildren.begin(), scannedChildren.end(),
                                           [cBaseID](const ScannedChildEntry& e) { return e.childBaseID == cBaseID; });
                    if (it == scannedChildren.end()) {
                        scannedChildren.push_back({ cBaseID, pBaseID, 0, false });
                    } else if (it->parentABaseID != pBaseID && it->parentBBaseID == 0) {
                        it->parentBBaseID = pBaseID;
                    }
                }
                // Skip adult relationship processing for child NPCs
                continue;
            }

            // Determine vanilla relationship type
            RelationshipStatus vanillaStatus = RelationshipStatus::Unknown;

            if (rel->assocType) {
                RE::FormID assocID = rel->assocType->GetFormID();
                if (assocID == kCourtingAssocFormID) {
                    vanillaStatus = RelationshipStatus::Dating;
                } else if (assocID == kSpouseAssocFormID) {
                    vanillaStatus = RelationshipStatus::Married;
                }
            }

            if (vanillaStatus == RelationshipStatus::Unknown) {
                // Check if rank 4 lover (kLover = 0 in CommonLib RELATIONSHIP_LEVEL enum)
                if (rel->level.get() == RE::BGSRelationship::RELATIONSHIP_LEVEL::kLover) {
                    vanillaStatus = RelationshipStatus::Lover;
                }
            }

            // Only process Courting, Spouse, or Lover relationships
            if (vanillaStatus == RelationshipStatus::Unknown) {
                continue;
            }

            // Strictly audit Player-NPC romantic relationships via the First-Time Relationship Audit Modal
            if (isPlayerPair) {
                auto* npcBase = isPlayerForm(baseID1) ? npc2Base : npc1Base;
                RE::FormID npcBaseID = npcBase->GetFormID();
                if (isPlayerForm(npcBaseID)) {
                    continue;
                }

                const auto& npcEntry = isPlayerForm(baseID1) ? entry2 : entry1;
                RE::FormID npcRefID = npcEntry ? npcEntry->refFormID : npcIndex.GetRefID(npcBaseID);
                bool isNpcRef = (npcRefID != 0 && npcRefID != npcBaseID);

                if (isNpcRef) {
                    auto existingStatus = GetRelationshipStatus(kPlayerRefID, npcRefID);
                    if (existingStatus != RelationshipStatus::Candidate &&
                        existingStatus != RelationshipStatus::Unknown) {
                        continue;
                    }
                }

                std::string npcName;
                if (isNpcRef) {
                    npcName = ActorSvc::GetName(npcRefID);
                }
                if (npcName.empty() && npcBase->GetName() && npcBase->GetName()[0] != '\0') {
                    npcName = npcBase->GetName();
                }
                if (npcName.empty()) {
                    npcName = fmt::format("NPC ({:08X})", npcBaseID);
                }

                const auto* srcFile = rel->GetFile(0);
                if (!srcFile) {
                    srcFile = npcBase->GetFile(0);
                }
                std::string pluginName = (srcFile && !srcFile->GetFilename().empty())
                    ? std::string(srcFile->GetFilename())
                    : "Savegame / Runtime";

                bool defaultChecked = false;
                if (!isNewGame) {
                    bool hasActiveVanillaFaction = false;
                    if (isNpcRef) {
                        if (auto* npcActor = RE::TESForm::LookupByID<RE::Actor>(npcRefID)) {
                            hasActiveVanillaFaction =
                                (marriedFaction && npcActor->IsInFaction(marriedFaction)) ||
                                (courtingFaction && npcActor->IsInFaction(courtingFaction));
                        }
                    }
                    bool isRuntimeDynamicRel = ((rel->GetFormID() >> 24) == 0xFF);
                    defaultChecked = hasActiveVanillaFaction || isRuntimeDynamicRel;
                }

                auto existingIt = std::find_if(
                    playerAuditEntries.begin(), playerAuditEntries.end(),
                    [npcBaseID](const PlayerRelationshipAuditEntry& e) { return e.npcBaseID == npcBaseID; });

                if (existingIt == playerAuditEntries.end()) {
                    playerAuditEntries.push_back(PlayerRelationshipAuditEntry{
                        npcBaseID,
                        isNpcRef ? npcRefID : 0,
                        isNpcRef,
                        std::move(npcName),
                        std::move(pluginName),
                        vanillaStatus,
                        vanillaStatus,
                        defaultChecked});
                } else if (statusPriority(vanillaStatus) > statusPriority(existingIt->vanillaStatus)) {
                    existingIt->vanillaStatus = vanillaStatus;
                    existingIt->targetMarasStatus = vanillaStatus;
                    existingIt->defaultChecked = existingIt->defaultChecked || defaultChecked;
                }
                continue;
            }

            RE::FormID refID1 = entry1 ? entry1->refFormID : npcIndex.GetRefID(baseID1);
            RE::FormID refID2 = entry2 ? entry2->refFormID : npcIndex.GetRefID(baseID2);

            bool isRef1 = (refID1 != 0 && refID1 != baseID1);
            bool isRef2 = (refID2 != 0 && refID2 != baseID2);

            // Check if either actor is currently in a MARAS relationship with a non-vanilla partner (e.g. Player)
            bool hasNonVanillaHistory = hasNonVanillaMarasRelationship(isRef1 ? refID1 : baseID1) ||
                                         hasNonVanillaMarasRelationship(isRef2 ? refID2 : baseID2);

            RelationshipStatus targetMarasStatus = RelationshipStatus::Unknown;
            if (!hasNonVanillaHistory) {
                targetMarasStatus = vanillaStatus;
            } else {
                if (vanillaStatus == RelationshipStatus::Dating) {
                    targetMarasStatus = RelationshipStatus::ExDating;
                } else if (vanillaStatus == RelationshipStatus::Married) {
                    targetMarasStatus = RelationshipStatus::Divorced;
                } else if (vanillaStatus == RelationshipStatus::Lover) {
                    // A vanilla lover whose partner is committed to a non-vanilla main partner (e.g. Player)
                    // is broken up, mirroring BreakAllLoverConnections on a MARAS marriage/engagement/dating.
                    const bool committedElsewhere = hasNonVanillaMainTierRelationship(isRef1 ? refID1 : baseID1) ||
                                                    hasNonVanillaMainTierRelationship(isRef2 ? refID2 : baseID2);
                    targetMarasStatus = committedElsewhere ? RelationshipStatus::ExLover : RelationshipStatus::Lover;
                }
            }

            // If either NPC is not yet spawned in world as an Actor ref, defer the relationship!
            if (!isRef1 || !isRef2) {
                RE::FormID r1 = isRef1 ? refID1 : 0;
                RE::FormID r2 = isRef2 ? refID2 : 0;

                State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                    if (!isRef1) {
                        ws.actors.Write().AddPendingRelationship(
                            State::BaseId{ baseID1 },
                            LegacyAdapters::ToPendingRelationshipEntry(
                                PendingRelationshipData(baseID2, r2, targetMarasStatus, vanillaStatus)));
                    }
                    if (!isRef2) {
                        ws.actors.Write().AddPendingRelationship(
                            State::BaseId{ baseID2 },
                            LegacyAdapters::ToPendingRelationshipEntry(
                                PendingRelationshipData(baseID1, r1, targetMarasStatus, vanillaStatus)));
                    }
                });
                MARAS_LOG_INFO("ScanExistingRelationships: Deferred relationship between Base {:08X} (ref {:08X}) and Base {:08X} (ref {:08X})",
                               baseID1, r1, baseID2, r2);
                continue;
            }

            // Both are placed Actor Ref IDs: process immediately
            EnsureRegistered(refID1);
            EnsureRegistered(refID2);

            SetOriginalRelationshipStatus(refID1, refID2, vanillaStatus);

            auto currentStatus = GetRelationshipStatus(refID1, refID2);
            if (currentStatus == RelationshipStatus::Candidate || currentStatus == RelationshipStatus::Unknown) {
                if (targetMarasStatus != RelationshipStatus::Unknown) {
                    ApplyImportedVanillaStatus(refID1, refID2, vanillaStatus, targetMarasStatus);

                    RE::FormID defaultInit = DetermineDefaultInitiator(refID1, refID2);
                    State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                        auto& edge = ws.relationships.Write().GetOrCreateEdge(State::ActorId{ refID1 }, State::ActorId{ refID2 });
                        for (auto& hist : edge.statusHistory) {
                            hist.timestamp = -1.0f;
                            hist.fromStatus = State::RelationshipStatus::Unknown;
                            hist.initiator = State::ActorId{ defaultInit };
                        }
                    });

                    MARAS_LOG_INFO("ScanExistingRelationships: Assigned MARAS status {} (original {}) between {} ({:08X}) and {} ({:08X})",
                                   Utils::RelationshipStatusToString(targetMarasStatus),
                                   Utils::RelationshipStatusToString(vanillaStatus),
                                   ActorSvc::GetName(refID1), refID1,
                                   ActorSvc::GetName(refID2), refID2);
                    importedCount++;
                }
            }
        }

        // 2. Process collected child-parent relationships
        size_t childImportedCount = 0;
        for (const auto& entry : scannedChildren) {
            RE::FormID childRefID = npcIndex.GetRefID(entry.childBaseID);
            bool hasChildRef = (childRefID != 0 && childRefID != entry.childBaseID);
            if (!hasChildRef) {
                childRefID = 0;
            }

            RE::FormID parentARefID = entry.parentABaseID != 0 ? npcIndex.GetRefID(entry.parentABaseID) : 0;
            if (parentARefID == entry.parentABaseID && entry.parentABaseID != kPlayerFormID) {
                parentARefID = 0;
            }

            RE::FormID parentBBaseID = entry.parentBBaseID;
            RE::FormID parentBRefID = parentBBaseID != 0 ? npcIndex.GetRefID(parentBBaseID) : 0;
            if (parentBRefID == parentBBaseID && parentBBaseID != kPlayerFormID) {
                parentBRefID = 0;
            }

            // If a second ParentChild BGSRelationship record was not present, look for a romantic partner of parent A
            if (parentBBaseID == 0 && parentBRefID == 0) {
                RE::FormID lookupParentA = parentARefID != 0 ? parentARefID : entry.parentABaseID;
                const auto snap = State::WorldStore::GetSingleton().Snapshot();
                const State::ActorId parentActor{ lookupParentA };
                for (const auto& key : snap->relationships->GetAdjacentPairs(parentActor)) {
                    const auto* edge = snap->relationships->FindEdge(key);
                    if (edge &&
                        (edge->status == State::RelationshipStatus::Married ||
                         edge->status == State::RelationshipStatus::Dating ||
                         edge->status == State::RelationshipStatus::Lover)) {
                        parentBRefID = key.Other(parentActor).raw();
                        parentBBaseID = npcIndex.GetBaseID(parentBRefID);
                        break;
                    }
                }
            }

            // Assign bioMotherID and bioFatherID strictly by actor sex, never by slot position
            RE::FormID bioMotherID = 0;
            RE::FormID bioFatherID = 0;
            if (!entry.isAdopted) {
                auto assignBioBySex = [&](RE::FormID refID, RE::FormID baseID) {
                    RE::FormID effectiveID = refID != 0 ? refID : baseID;
                    if (effectiveID == 0) return;
                    RE::FormID storedID = refID != 0 ? refID : baseID;
                    int sex = ActorSvc::GetSex(effectiveID);
                    if (sex == 1 && bioMotherID == 0) {
                        bioMotherID = storedID;
                    } else if (sex == 0 && bioFatherID == 0) {
                        bioFatherID = storedID;
                    } else if (bioMotherID == 0) {
                        bioMotherID = storedID;
                    } else if (bioFatherID == 0) {
                        bioFatherID = storedID;
                    }
                };
                assignBioBySex(parentARefID, entry.parentABaseID);
                assignBioBySex(parentBRefID, parentBBaseID);
            }

            // If the parents were divorced by this import (one of them committed to Player / non-vanilla partner),
            // the child stays only with the committed parent; bio parents are unchanged.
            if (parentARefID != 0 && parentBRefID != 0 &&
                GetRelationshipStatus(parentARefID, parentBRefID) == RelationshipStatus::Divorced) {
                const bool aCommitted = hasNonVanillaMainTierRelationship(parentARefID);
                const bool bCommitted = hasNonVanillaMainTierRelationship(parentBRefID);
                if (aCommitted && !bCommitted) {
                    parentBRefID = 0;
                    parentBBaseID = 0;
                } else if (bCommitted && !aCommitted) {
                    parentARefID = parentBRefID;
                    parentBRefID = 0;
                    parentBBaseID = 0;
                }
            }

            const bool awaitingParentA = (entry.parentABaseID != 0 && parentARefID == 0);
            const bool awaitingParentB = (parentBBaseID != 0 && parentBRefID == 0);

            if (hasChildRef && (parentARefID != 0 || parentBRefID != 0)) {
                RegisterChildActor(childRefID, parentARefID, parentBRefID, 0,
                                   bioMotherID, bioFatherID, 0,
                                   static_cast<uint8_t>(ChildStage::Child),
                                   entry.isAdopted,
                                   /*sendEvent=*/false);
                childImportedCount++;
                if (!awaitingParentA && !awaitingParentB) {
                    continue;
                }
            }

            // If child is unspawned or either parent is not yet spawned, register pending entry
            const PendingChildData pendingChild(
                entry.childBaseID, childRefID,
                entry.parentABaseID, parentARefID,
                parentBBaseID, parentBRefID,
                entry.isAdopted);
            State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                ws.lineage.Write().AddPendingChild(
                    State::BaseId{ entry.childBaseID },
                    LegacyAdapters::ToPendingChildEntry(pendingChild));
            });
            MARAS_LOG_INFO("ScanExistingRelationships: Deferred child Base {:08X} (parents {:08X} / {:08X}, adopted: {}) to pending",
                           entry.childBaseID, entry.parentABaseID, parentBBaseID, entry.isAdopted);
        }

        SyncAllMarriedCouplesChildCustody();

        // 3. If Player-NPC romantic BGSRelationship records were detected, show the First-Time Relationship Audit Modal
        if (!playerAuditEntries.empty()) {
            std::vector<ModalChecklistItem> items;
            items.reserve(playerAuditEntries.size());

            const std::vector<ModalSelectOption> statusSelectOptions = {
                {"dating", "Dating"},
                {"lover", "Lover"},
                {"engaged", "Engaged"},
                {"married", "Married"}};

            for (size_t i = 0; i < playerAuditEntries.size(); ++i) {
                const auto& entry = playerAuditEntries[i];
                ModalChecklistItem item;
                item.id = fmt::format("rel_{}", i);
                item.label = entry.npcName;
                item.subtitle = fmt::format("Source: {}", entry.pluginName);

                if (entry.vanillaStatus == RelationshipStatus::Married) {
                    item.badgeText = "Spouse";
                    item.badgeTone = "success";
                } else if (entry.vanillaStatus == RelationshipStatus::Dating) {
                    item.badgeText = "Courting";
                    item.badgeTone = "accent";
                } else {
                    item.badgeText = "Lover";
                    item.badgeTone = "warning";
                }

                item.checked = entry.defaultChecked;
                item.selectKey = "status";
                item.selectLabel = "Import As";
                item.selectValue = std::string(Utils::RelationshipStatusToString(entry.targetMarasStatus));
                item.selectOptions = statusSelectOptions;

                items.push_back(std::move(item));
            }

            ModalRequest req;
            req.title = "Detected Player Relationships";
            req.subtitle = "First-Time M.A.R.A.S Relationship Scan";
            req.message = "M.A.R.A.S detected existing or plugin-defined romantic relationships between your character and the following NPCs. "
                          "Select which relationships are genuinely active in this playthrough and choose their MARAS status:";
            req.tone = ModalTone::Default;
            req.size = ModalSize::Lg;
            req.defaultCancelButtonIndex = 1;

            req.controls.push_back(ModalControl::MakeChecklist(
                "detected_player_rels",
                "Player-NPC Romantic Relationships",
                std::move(items),
                true,
                "Unchecked entries (such as pre-configured follower mod Lover records) will be ignored."));

            req.buttons.push_back(ModalButton::Make("import_selected", "Import Selected", ModalButtonVariant::Primary));
            req.buttons.push_back(ModalButton::Make("skip_all", "Skip All", ModalButtonVariant::Secondary));

            MARAS_LOG_INFO("ScanExistingRelationships: Queuing Player-NPC Relationship Audit Modal with {} detected entries (isNewGame={})",
                           playerAuditEntries.size(), isNewGame);

            PrismaModalManager::GetSingleton().ShowModal(
                std::move(req),
                [this, entries = std::move(playerAuditEntries)](const ModalResponse& resp) {
                    if (resp.aborted || (resp.buttonIndex != 0 && resp.buttonId != "import_selected")) {
                        MARAS_LOG_INFO("ScanExistingRelationships (Audit Modal): Player skipped importing all {} detected player-NPC relationships",
                                       entries.size());
                        return;
                    }

                    SkyrimNetMemoryService::SuppressScope suppressAuditMemories;
                    auto& idx = NPCIndex::GetSingleton();
                    size_t auditImported = 0;
                    size_t auditDeferred = 0;

                    for (size_t i = 0; i < entries.size(); ++i) {
                        const std::string itemId = fmt::format("rel_{}", i);
                        if (!resp.IsItemChecked("detected_player_rels", itemId, false)) {
                            continue;
                        }

                        const auto& entry = entries[i];
                        const std::string defaultStatusStr = std::string(Utils::RelationshipStatusToString(entry.targetMarasStatus));
                        const std::string chosenStr = resp.GetItemSelectValue("detected_player_rels", itemId, defaultStatusStr);
                        RelationshipStatus chosenStatus = Utils::StringToRelationshipStatus(chosenStr);
                        if (chosenStatus == RelationshipStatus::Unknown || chosenStatus == RelationshipStatus::Candidate) {
                            chosenStatus = entry.targetMarasStatus;
                        }

                        RE::FormID npcRefID = entry.isRef ? entry.npcRefID : idx.GetRefID(entry.npcBaseID);
                        const bool hasValidRef = (npcRefID != 0 && npcRefID != entry.npcBaseID);

                        if (hasValidRef) {
                            EnsureRegistered(kPlayerRefID);
                            EnsureRegistered(npcRefID);
                            SetOriginalRelationshipStatus(kPlayerRefID, npcRefID, entry.vanillaStatus);

                            auto currentStatus = GetRelationshipStatus(kPlayerRefID, npcRefID);
                            if (currentStatus == RelationshipStatus::Candidate || currentStatus == RelationshipStatus::Unknown) {
                                ApplyImportedVanillaStatus(kPlayerRefID, npcRefID, entry.vanillaStatus, chosenStatus);

                                RE::FormID defaultInit = DetermineDefaultInitiator(kPlayerRefID, npcRefID);
                                State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                                    auto& edge = ws.relationships.Write().GetOrCreateEdge(State::ActorId::Player(), State::ActorId{ npcRefID });
                                    for (auto& hist : edge.statusHistory) {
                                        hist.timestamp = -1.0f;
                                        hist.fromStatus = State::RelationshipStatus::Unknown;
                                        hist.initiator = State::ActorId{ defaultInit };
                                    }
                                });

                                MARAS_LOG_INFO("ScanExistingRelationships (Audit Modal): Imported player relationship {} (original {}) with {} ({:08X})",
                                               Utils::RelationshipStatusToString(chosenStatus),
                                               Utils::RelationshipStatusToString(entry.vanillaStatus),
                                               entry.npcName, npcRefID);
                                auditImported++;
                            }
                        } else {
                            State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                                ws.actors.Write().AddPendingRelationship(
                                    State::BaseId{ entry.npcBaseID },
                                    LegacyAdapters::ToPendingRelationshipEntry(
                                        PendingRelationshipData(kPlayerBaseID, kPlayerRefID, chosenStatus, entry.vanillaStatus)));
                            });
                            MARAS_LOG_INFO("ScanExistingRelationships (Audit Modal): Deferred player relationship {} (original {}) for unspawned NPC '{}' (Base {:08X})",
                                           Utils::RelationshipStatusToString(chosenStatus),
                                           Utils::RelationshipStatusToString(entry.vanillaStatus),
                                           entry.npcName, entry.npcBaseID);
                            auditDeferred++;
                        }
                    }

                    if (auditImported > 0) {
                        SyncAllMarriedCouplesChildCustody();
                        RecalculateAndUpdateGlobals();
                    }
                    MARAS_LOG_INFO("ScanExistingRelationships (Audit Modal): Completed — imported {} active, deferred {} unspawned player-NPC relationships",
                                   auditImported, auditDeferred);
                });
        }

        const auto finalSnap = State::WorldStore::GetSingleton().Snapshot();
        auto endTime = std::chrono::steady_clock::now();
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
        MARAS_LOG_INFO("ScanExistingRelationships: Complete in {} ms — Scanned {} vanilla relationships, imported {} adult pairs, {} child records, queued {} player audit entries, deferred {} pending pairs and {} pending children",
                       elapsedMs, scannedCount, importedCount, childImportedCount, playerAuditEntries.size(),
                       finalSnap->actors->AllPendingRelationships().size(),
                       finalSnap->lineage->AllPendingChildren().size());
    }

    float NPCRelationshipManager::GetEngagementTimestamp(RE::FormID actorA, RE::FormID actorB) const {
        if (actorA == 0 || actorB == 0) return -1.0f;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* edge = snap->relationships->FindEdge(State::ActorId{ actorA }, State::ActorId{ actorB });
        if (!edge || edge->status != State::RelationshipStatus::Engaged) {
            return -1.0f;
        }

        for (auto hIt = edge->statusHistory.rbegin(); hIt != edge->statusHistory.rend(); ++hIt) {
            if (hIt->toStatus == State::RelationshipStatus::Engaged) {
                return hIt->timestamp;
            }
        }
        return -1.0f;
    }

    void NPCRelationshipManager::CheckAutoMarriageForEngagedNPCs(float currentDay) {
        constexpr float kAutoMarriageDaysThreshold = 5.0f;

        // Collect couples ready for backfill or auto-marriage from immutable snapshot
        std::vector<State::CanonicalPairKey> pairsToBackfill;
        std::vector<std::pair<RE::FormID, RE::FormID>> couplesToMarry;

        {
            const auto snap = State::WorldStore::GetSingleton().Snapshot();
            for (const auto& [key, edge] : snap->relationships->AllEdges()) {
                // Strictly NPC-NPC only: ignore if either partner is player or invalid
                if (key.first.IsNull() || key.second.IsNull() || key.first.IsPlayer() || key.second.IsPlayer()) {
                    continue;
                }

                if (edge.status != State::RelationshipStatus::Engaged) {
                    continue;
                }

                float engagementTime = -1.0f;
                for (auto hIt = edge.statusHistory.rbegin(); hIt != edge.statusHistory.rend(); ++hIt) {
                    if (hIt->toStatus == State::RelationshipStatus::Engaged) {
                        engagementTime = hIt->timestamp;
                        break;
                    }
                }

                if (engagementTime < 0.0f) {
                    pairsToBackfill.push_back(key);
                    continue;
                }

                const float elapsed = currentDay - engagementTime;
                if (elapsed >= kAutoMarriageDaysThreshold) {
                    MARAS_LOG_INFO("Engaged NPC pair {:08X} - {:08X} reached {:.2f} days engaged (threshold {:.1f}); queueing for auto-marriage",
                                   key.first.raw(), key.second.raw(), elapsed, kAutoMarriageDaysThreshold);
                    couplesToMarry.emplace_back(key.first.raw(), key.second.raw());
                }
            }
        }

        if (!pairsToBackfill.empty()) {
            State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                auto& rels = ws.relationships.Write();
                for (const auto& key : pairsToBackfill) {
                    auto* edge = const_cast<State::RelationshipEdgeRecord*>(rels.FindEdge(key));
                    if (!edge || edge->status != State::RelationshipStatus::Engaged) continue;
                    edge->statusHistory.push_back(State::StatusTransitionEntry{
                        .fromStatus = State::RelationshipStatus::Unknown,
                        .toStatus = State::RelationshipStatus::Engaged,
                        .timestamp = currentDay,
                        .initiator = key.first,
                    });
                    MARAS_LOG_INFO("Recorded initial engagement timestamp {:.2f} for NPC pair {:08X} - {:08X}",
                                   currentDay, key.first.raw(), key.second.raw());
                }
            });
        }

        for (const auto& [actorA, actorB] : couplesToMarry) {
            // Validate both actors are still alive and not deleted
            auto* a = RE::TESForm::LookupByID<RE::Actor>(actorA);
            auto* b = RE::TESForm::LookupByID<RE::Actor>(actorB);
            if (!a || !b || a->IsDead() || b->IsDead() || a->IsDeleted() || b->IsDeleted()) {
                MARAS_LOG_WARN("Skipping auto-marriage for pair {:08X} - {:08X}: actor missing, dead, or deleted",
                               actorA, actorB);
                continue;
            }

            // Ensure they are still engaged (not broken up meanwhile)
            if (GetRelationshipStatus(actorA, actorB) != RelationshipStatus::Engaged) {
                MARAS_LOG_DEBUG("Pair {:08X} - {:08X} is no longer engaged; skipping auto-marriage", actorA, actorB);
                continue;
            }

            MARAS_LOG_INFO("Auto-marrying engaged NPC couple {:08X} ({}) and {:08X} ({}) after 5 days",
                           actorA, ActorSvc::GetName(actorA), actorB, ActorSvc::GetName(actorB));

            bool success = PromoteToMarried(actorA, actorB, /*sendEvent=*/true, /*breakLovers=*/true);
            if (!success) {
                MARAS_LOG_ERROR("Failed to auto-marry engaged NPC couple {:08X} and {:08X}", actorA, actorB);
            }
        }
    }

    // =========================================================================
    // Lifecycle, Post-Load Engine Sync, Globals, Logging & Diagnostics
    // =========================================================================

    void NPCRelationshipManager::Clear() {
        AdultChildProgression::GetSingleton().RevertRuntimeBases();
        ClearPendingMarriageAdoptions();
        State::WorldStore::GetSingleton().Modify([](State::WorldState& ws) {
            ws.actors.Write().Clear();
            ws.relationships.Write().Clear();
            ws.affairs.Write().Clear();
            ws.lineage.Write().Clear();
        });

        MARAS_LOG_INFO("Cleared all NPC relationship data");
    }

    void NPCRelationshipManager::FinalizeLoad() {
        // CoSaveCodec::LoadWorldState already pruned orphaned 0xFF actors/children, reconciled
        // dead-on-load / deceased adults & children, marked parents/guardians registered, synced
        // married custody in WorldState::lineage, and filled SpouseHierarchyTable gaps.
        // Here we perform the remaining engine-level and child-progression post-load steps.
        const auto snap = State::WorldStore::GetSingleton().Snapshot();

        // 1. Re-apply runtime home markers & added keywords onto loaded engine forms
        for (const auto& [refId, prof] : snap->actors->AllProfiles()) {
            const RE::FormID actorId = refId.raw();
            if (actorId == 0) continue;

            if (prof.legacyHomeMarker.has_value() && prof.legacyHomeMarker->raw() != 0) {
                SetLinkedRefForHomeMarker(actorId, prof.legacyHomeMarker->raw());
            }

            if (!prof.addedKeywords.empty() && actorId != kPlayerFormID) {
                if (auto* base = ActorSvc::GetActorBase(actorId)) {
                    for (const auto kwId : prof.addedKeywords) {
                        if (auto* kw = RE::TESForm::LookupByID<RE::BGSKeyword>(kwId.raw())) {
                            if (!base->HasKeyword(kw)) {
                                base->AddKeyword(kw);
                            }
                        }
                    }
                }
            }
        }

        // 2. Complete any un-evaluated child progression rolls and reset runtime level sync
        std::vector<ChildRegistryEntry> childrenSnapshot;
        childrenSnapshot.reserve(snap->lineage->Size());
        for (const auto& [childId, childRec] : snap->lineage->AllChildren()) {
            (void)childId;
            childrenSnapshot.push_back(LegacyAdapters::ToLegacyChildEntry(childRec));
        }

        for (ChildRegistryEntry& entry : childrenSnapshot) {
            SkyrimNetProfileService::GetSingleton().MarkActorStageUpdated(entry.childID, entry.stage);
            const bool missingBeastblood = !entry.beastbloodEvaluated;
            const bool missingBioLineage = (entry.stage != ChildStage::Unborn && !entry.bioLineageEvaluated);
            const bool missingGuardLineage = (entry.stage == ChildStage::Adult && !entry.guardianLineageEvaluated);
            const bool needsLevelReset = (entry.lastAppliedLevel != 0);

            if (missingBeastblood) {
                EvaluateChildBeastbloodInheritance(entry);
            }
            if (missingBioLineage) {
                AdultChildProgression::GetSingleton().EvaluateChildBioLineageAtBirth(*this, entry);
            }
            if (missingGuardLineage) {
                AdultChildProgression::GetSingleton().EvaluateChildGuardianLineageAtGraduation(*this, entry);
            }
            // Force runtime SetBaseActorValue sync on first 3D load after loading a save
            entry.lastAppliedLevel = 0;

            if (missingBeastblood || missingBioLineage || missingGuardLineage || needsLevelReset) {
                UpsertChildEntry(entry);
            }
        }

        // 3. Re-synchronize globals, cheating/open/non-cheating factions, custody & child AI packages
        RecalculateAndUpdateGlobals();
        UpdateAllCanFormNonCheatingMainFactions();
        SyncAllMarriedCouplesChildCustody();
        ReevaluateAllNonPlayerChildrenPackages();
    }

    void NPCRelationshipManager::Revert() {
        MARAS_LOG_INFO("Reverting NPC relationship data");
        Clear();
    }

    void NPCRelationshipManager::RecalculateAndUpdateGlobals() {
        std::int32_t loveInterests = static_cast<std::int32_t>(GetAllEngaged(kPlayerFormID).size() + GetAllMarried(kPlayerFormID).size());
        std::int32_t spouses = static_cast<std::int32_t>(GetAllMarried(kPlayerFormID).size());

        auto& cache = FormCache::GetSingleton();

        if (auto globalLove = cache.GetLoveInterestsCount()) {
            globalLove->value = static_cast<float>(loveInterests);
            MARAS_LOG_DEBUG("Updated LoveInterestsCount global to {}", loveInterests);
        }

        if (auto globalSpouses = cache.GetSpousesCount()) {
            globalSpouses->value = static_cast<float>(spouses);
            MARAS_LOG_DEBUG("Updated SpousesCount global to {}", spouses);
        }

        UpdateAllSpousesChildrenFaction();
        UpdateAllSpousesCheatingAndOpenFactions();
        UpdateCanFormNonCheatingMainFaction(kPlayerFormID);
    }

    void NPCRelationshipManager::LogStatistics() const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto& allPending = snap->actors->AllPendingRelationships();

        MARAS_LOG_INFO("MARAS NPC Statistics:");
        MARAS_LOG_INFO("  Total Registered: {}", GetTotalRegisteredCount());
        MARAS_LOG_INFO("  Candidates (Player): {}", GetCandidateCount(kPlayerFormID));
        MARAS_LOG_INFO("  Engaged (Player): {}", GetEngagedCount(kPlayerFormID));
        MARAS_LOG_INFO("  Married (Player): {}", GetMarriedCount(kPlayerFormID));
        MARAS_LOG_INFO("  Divorced (Player): {}", GetDivorcedCount(kPlayerFormID));
        MARAS_LOG_INFO("  Jilted (Player): {}", GetJiltedCount(kPlayerFormID));
        MARAS_LOG_INFO("  ExDating (Player): {}", GetExDatingCount(kPlayerFormID));
        MARAS_LOG_INFO("  ExLover (Player): {}", GetExLoverCount(kPlayerFormID));
        MARAS_LOG_INFO("  Pending Deferred Base Entries: {}", allPending.size());

        for (const auto& [baseId, pendingList] : allPending) {
            for (const auto& pending : pendingList) {
                MARAS_LOG_INFO("    [Pending] Base {:08X} <-> Partner Base {:08X} (Ref {:08X}) | Target: {}, Original: {}",
                               baseId.raw(), pending.partnerBaseId.raw(), pending.partnerRefId.raw(),
                               Utils::RelationshipStatusToString(static_cast<RelationshipStatus>(pending.targetStatus)),
                               Utils::RelationshipStatusToString(static_cast<RelationshipStatus>(pending.originalStatus)));
            }
        }
    }

    void NPCRelationshipManager::LogNPCDetails(RE::FormID npcFormID) const {
        auto profile = GetNPCProfile(npcFormID);
        if (!profile) {
            MARAS_LOG_WARN("NPC {:08X} profile not found in MARAS data", npcFormID);
            return;
        }

        auto data = GetNPCData(npcFormID, kPlayerFormID);
        MARAS_LOG_INFO("NPC {:08X} Details:", npcFormID);
        MARAS_LOG_INFO("  Status with Player: {}", data ? static_cast<int>(data->status) : -1);
        MARAS_LOG_INFO("  Social Class: {}", static_cast<int>(profile->socialClass));
        MARAS_LOG_INFO("  Skill Type: {}", static_cast<int>(profile->skillType));
        MARAS_LOG_INFO("  Temperament: {}", static_cast<int>(profile->temperament));
    }

    nlohmann::json NPCRelationshipManager::DumpJson(const Projection::PerspectiveView& view) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        nlohmann::json root;

        auto stageToString = [](State::ChildStage s) -> std::string {
            switch (s) {
                case State::ChildStage::Unborn:   return "unborn";
                case State::ChildStage::Infant:   return "infant";
                case State::ChildStage::Child:    return "child";
                case State::ChildStage::Training: return "training";
                case State::ChildStage::Adult:    return "adult";
            }
            return "child";
        };

        auto packPairKey = [](State::ActorId a, State::ActorId b) noexcept -> uint64_t {
            const State::CanonicalPairKey k{ a, b };
            return (static_cast<uint64_t>(k.first.raw()) << 32) | static_cast<uint64_t>(k.second.raw());
        };

        // 1. Registered NPCs & Profiles
        nlohmann::json profilesArray = nlohmann::json::array();
        std::vector<RE::FormID> actorsToExport;
        actorsToExport.push_back(kPlayerFormID);
        for (const auto& [refId, prof] : view.Actors().AllProfiles()) {
            if (prof.isRegisteredAdult && !refId.IsNull() && !refId.IsPlayer()) {
                actorsToExport.push_back(refId.raw());
            }
        }

        for (auto formID : actorsToExport) {
            nlohmann::json p;
            p["formId"] = std::format("{:08X}", formID);
            if (formID == kPlayerFormID) {
                auto player = RE::PlayerCharacter::GetSingleton();
                p["name"] = ActorSvc::GetName(kPlayerFormID);
                std::string pRace = ActorSvc::GetRaceName(kPlayerFormID);
                p["race"] = !pRace.empty() ? pRace : "Nord";
                p["gender"] = (player && player->GetActorBase()) ? (player->GetActorBase()->GetSex() == RE::SEXES::kFemale ? "female" : "male") : "male";
                p["isChild"] = false;
            } else {
                p["name"] = ActorSvc::GetName(formID);
                p["race"] = ActorSvc::GetRaceName(formID);
                p["isChild"] = IsChildNPC(formID);
                p["gender"] = (ActorSvc::GetSex(formID) == 1) ? "female" : "male";
            }

            if (const auto* stateProf = view.Actors().Find(State::RefId{ formID })) {
                const NPCProfileData prof = LegacyAdapters::ToLegacyNPCProfile(*stateProf);
                p["socialClass"] = {
                    {"id", static_cast<uint8_t>(prof.socialClass)},
                    {"name", Utils::SocialClassToString(prof.socialClass)}
                };
                p["skillType"] = {
                    {"id", static_cast<uint8_t>(prof.skillType)},
                    {"name", Utils::SkillTypeToString(prof.skillType)}
                };
                p["temperament"] = {
                    {"id", static_cast<uint8_t>(prof.temperament)},
                    {"name", Utils::TemperamentToString(prof.temperament)}
                };

                if (prof.originalHome.has_value()) {
                    nlohmann::json h;
                    h["formId"] = std::format("{:08X}", *prof.originalHome);
                    std::string hName = "";
                    if (auto cell = RE::TESForm::LookupByID<RE::TESObjectCELL>(*prof.originalHome)) {
                        if (cell->GetName()) hName = cell->GetName();
                    }
                    h["name"] = hName;
                    p["originalHome"] = h;
                }
                if (prof.currentHome.has_value()) {
                    nlohmann::json h;
                    h["formId"] = std::format("{:08X}", *prof.currentHome);
                    std::string hName = "";
                    if (auto cell = RE::TESForm::LookupByID<RE::TESObjectCELL>(*prof.currentHome)) {
                        if (cell->GetName()) hName = cell->GetName();
                    }
                    h["name"] = hName;
                    p["currentHome"] = h;
                }
                if (prof.homeMarker.has_value()) {
                    p["homeMarker"] = std::format("{:08X}", *prof.homeMarker);
                }
                nlohmann::json kwArray = nlohmann::json::array();
                for (auto kwID : prof.addedKeywords) {
                    kwArray.push_back(std::format("{:08X}", kwID));
                }
                p["addedKeywords"] = kwArray;
            }

            profilesArray.push_back(p);
        }
        root["npcProfiles"] = profilesArray;

        const auto visibleChildren = view.GetVisibleChildren();
        const auto visibleAffairs = view.GetAllVisibleAffairs();

        std::unordered_map<uint64_t, std::vector<const Projection::PerceivedAffair*>> affairsByCouple;
        for (const auto& affair : visibleAffairs) {
            affairsByCouple[packPairKey(affair.cheater, affair.deceivedPartner)].push_back(&affair);
        }

        // 2. Relationship Pairs
        nlohmann::json pairsArray = nlohmann::json::array();
        for (const auto& edge : view.GetVisibleRelationshipEdges()) {
            const RelationshipPairKey key(edge.pair.first.raw(), edge.pair.second.raw());

            nlohmann::json pairObj;
            pairObj["actorA"] = {
                {"formId", std::format("{:08X}", key.actorA)},
                {"name", ActorSvc::GetName(key.actorA)}
            };
            pairObj["actorB"] = {
                {"formId", std::format("{:08X}", key.actorB)},
                {"name", ActorSvc::GetName(key.actorB)}
            };
            pairObj["status"] = {
                {"id", static_cast<uint8_t>(edge.status)},
                {"name", Utils::RelationshipStatusToString(static_cast<RelationshipStatus>(edge.status))}
            };
            pairObj["originalStatus"] = {
                {"id", static_cast<uint8_t>(edge.originalStatus)},
                {"name", Utils::RelationshipStatusToString(static_cast<RelationshipStatus>(edge.originalStatus))}
            };
            pairObj["isOpen"] = edge.isOpen;

            // Status history
            nlohmann::json histArray = nlohmann::json::array();
            for (const auto& h : edge.statusHistory) {
                nlohmann::json hObj;
                auto fromSt = static_cast<RelationshipStatus>(h.fromStatus);
                if (h.timestamp < 0 && fromSt == RelationshipStatus::Candidate) {
                    fromSt = RelationshipStatus::Unknown;
                }
                hObj["fromStatus"] = {
                    {"id", static_cast<uint8_t>(fromSt)},
                    {"name", Utils::RelationshipStatusToString(fromSt)}
                };
                hObj["toStatus"] = {
                    {"id", static_cast<uint8_t>(h.toStatus)},
                    {"name", Utils::RelationshipStatusToString(static_cast<RelationshipStatus>(h.toStatus))}
                };
                if (std::isnan(h.timestamp) || h.timestamp < 0.0f) {
                    hObj["timestamp"] = -1;
                } else {
                    hObj["timestamp"] = h.timestamp;
                }

                RE::FormID initID = h.initiator.raw();
                if (initID == 0 || (h.timestamp < 0 && (key.actorA != kPlayerFormID && key.actorB != kPlayerFormID) && initID == kPlayerFormID)) {
                    initID = DetermineDefaultInitiator(key.actorA, key.actorB);
                }
                std::string initName = ActorSvc::GetName(initID);
                if (initName.empty() && initID != 0) {
                    initName = std::format("Actor {:08X}", initID);
                }
                hObj["initiator"] = {
                    {"formId", std::format("{:08X}", initID)},
                    {"name", initName}
                };
                histArray.push_back(hObj);
            }
            pairObj["statusHistory"] = histArray;

            // Cheating records
            nlohmann::json cheatArray = nlohmann::json::array();
            if (auto itAffairs = affairsByCouple.find(packPairKey(edge.pair.first, edge.pair.second));
                itAffairs != affairsByCouple.end()) {
                for (const auto* affair : itAffairs->second) {
                    const RE::FormID cheaterID = affair->cheater.raw();
                    const RE::FormID victimID = affair->deceivedPartner.raw();
                    const RE::FormID paramourID = affair->paramour.raw();
                    nlohmann::json cObj;
                    cObj["cheater"] = {
                        {"formId", std::format("{:08X}", cheaterID)},
                        {"name", ActorSvc::GetName(cheaterID)}
                    };
                    cObj["victim"] = {
                        {"formId", std::format("{:08X}", victimID)},
                        {"name", ActorSvc::GetName(victimID)}
                    };
                    cObj["affairPartner"] = {
                        {"formId", std::format("{:08X}", paramourID)},
                        {"name", ActorSvc::GetName(paramourID)}
                    };
                    cObj["count"] = affair->incidentCount;
                    cObj["lastTimestamp"] = affair->lastTimestamp;
                    cObj["isSecret"] = affair->isSecret;
                    cObj["affairType"] = {
                        {"id", static_cast<uint8_t>(affair->affairType)},
                        {"name", Utils::AffairTypeToString(static_cast<AffairType>(affair->affairType))}
                    };
                    cObj["affairStatus"] = {
                        {"id", static_cast<uint8_t>(affair->affairStatus)},
                        {"name", Utils::AffairStatusToString(static_cast<AffairStatus>(affair->affairStatus))}
                    };
                    cObj["affairStage"] = {
                        {"id", static_cast<uint8_t>(affair->effectiveAffairStage)},
                        {"name", Utils::AffairStageToString(static_cast<AffairStage>(affair->effectiveAffairStage))}
                    };
                    cObj["stageStartDay"] = affair->stageStartDay;
                    cheatArray.push_back(cObj);
                }
            }
            pairObj["cheatingRecords"] = cheatArray;

            // Children associated with this pair
            nlohmann::json childrenArray = nlohmann::json::array();
            std::unordered_set<RE::FormID> emittedChildIds;
            for (const auto& perceivedChild : visibleChildren) {
                const bool guardianMatch =
                    perceivedChild.guardians.HasGuardian(edge.pair.first) &&
                    perceivedChild.guardians.HasGuardian(edge.pair.second);
                const bool perceivedBioMatch =
                    perceivedChild.IsActorPerceivedBioParent(edge.pair.first) &&
                    perceivedChild.IsActorPerceivedBioParent(edge.pair.second);
                if (!guardianMatch && !perceivedBioMatch) {
                    continue;
                }
                const RE::FormID cID = perceivedChild.childId.raw();
                if (!emittedChildIds.insert(cID).second) {
                    continue;
                }
                nlohmann::json cObj;
                cObj["childId"] = std::format("{:08X}", cID);
                cObj["name"] = perceivedChild.name;
                cObj["timestamp"] = perceivedChild.timestamp;
                cObj["isAdopted"] = perceivedChild.isAdopted;
                cObj["isBiologicalChild"] = perceivedChild.isBiologicalChild;
                cObj["stage"] = {
                    {"id", static_cast<uint8_t>(perceivedChild.stage)},
                    {"name", stageToString(perceivedChild.stage)}
                };
                if (!perceivedChild.trainingLocation.empty()) {
                    cObj["trainingLocation"] = perceivedChild.trainingLocation;
                }
                auto* actor = HasPhysicalActor(cID)
                    ? NPCIndex::GetSingleton().GetActorFromFormId(cID)
                    : nullptr;
                bool hasRealActor = (actor != nullptr && !actor->IsDeleted());
                cObj["hasRefID"] = hasRealActor;
                cObj["isEphemeral"] = !hasRealActor || (perceivedChild.stage == State::ChildStage::Infant) || (perceivedChild.stage == State::ChildStage::Training);
                childrenArray.push_back(cObj);
            }
            pairObj["children"] = childrenArray;

            pairsArray.push_back(pairObj);
        }
        root["relationshipPairs"] = pairsArray;

        // 3. Child Registry
        nlohmann::json childrenRegArray = nlohmann::json::array();
        for (const auto& perceived : visibleChildren) {
            const RE::FormID childID = perceived.childId.raw();
            const auto* rawChild = snap->lineage->FindChild(perceived.childId);
            const RE::FormID gAID = perceived.guardians.slotA.raw();
            const RE::FormID gBID = perceived.guardians.slotB.raw();
            const RE::FormID bioMotherID = perceived.bioMother.raw();
            const RE::FormID bioFatherID = perceived.perceivedBioFather.raw();
            const RE::FormID presumedFatherID = perceived.presumedFather.raw();

            nlohmann::json cObj;
            cObj["child"] = {
                {"formId", std::format("{:08X}", childID)},
                {"name", perceived.name.empty() ? ActorSvc::GetName(childID) : perceived.name}
            };
            cObj["guardianA"] = {
                {"formId", std::format("{:08X}", gAID)},
                {"name", gAID == 0 ? "" : ActorSvc::GetName(gAID)}
            };
            cObj["guardianB"] = {
                {"formId", std::format("{:08X}", gBID)},
                {"name", gBID == 0 ? "" : ActorSvc::GetName(gBID)}
            };
            cObj["timestamp"] = perceived.timestamp;
            cObj["isAdopted"] = perceived.isAdopted;
            cObj["bioMother"] = {
                {"formId", std::format("{:08X}", bioMotherID)},
                {"name", bioMotherID == 0 ? "" : ActorSvc::GetName(bioMotherID)}
            };
            std::string bioFatherName = "";
            if (bioFatherID == kUnknownFatherFormID) {
                bioFatherName = "Unknown";
            } else if (bioFatherID != 0) {
                bioFatherName = ActorSvc::GetName(bioFatherID);
            } else if (!perceived.perceivedBioFatherFallbackName.empty()) {
                bioFatherName = perceived.perceivedBioFatherFallbackName;
            }
            cObj["bioFather"] = {
                {"formId", std::format("{:08X}", bioFatherID)},
                {"name", bioFatherName}
            };
            cObj["presumedFather"] = {
                {"formId", std::format("{:08X}", presumedFatherID)},
                {"name", presumedFatherID == 0 ? "" : ActorSvc::GetName(presumedFatherID)}
            };
            cObj["isBiologicalChild"] = perceived.isBiologicalChild;
            cObj["conceptionTimestamp"] = perceived.conceptionTimestamp;
            cObj["birthTimestamp"] = perceived.birthTimestamp;
            cObj["stage"] = {
                {"id", static_cast<uint8_t>(perceived.stage)},
                {"name", stageToString(perceived.stage)}
            };
            if (!perceived.trainingLocation.empty()) {
                cObj["trainingLocation"] = perceived.trainingLocation;
            }
            if (perceived.trainingClassIndex >= 0) {
                cObj["trainingClassIndex"] = perceived.trainingClassIndex;
                cObj["trainingClassName"] = std::string(GetTrainingClassName(perceived.trainingClassIndex));
            }
            if (perceived.trainingStartGameDay > 0.0f) {
                cObj["trainingStartGameDay"] = perceived.trainingStartGameDay;
            }
            if (!perceived.savedBioTemplateName.empty()) {
                cObj["savedBioTemplateName"] = perceived.savedBioTemplateName;
            }
            if (rawChild) {
                if (rawChild->savedRaceIndex <= 9) {
                    cObj["savedRaceIndex"] = rawChild->savedRaceIndex;
                }
                if (rawChild->hairColorFormId.raw() != 0) {
                    cObj["hairColorFormId"] = std::format("{:08X}", rawChild->hairColorFormId.raw());
                }
                if (rawChild->eyesPartFormId.raw() != 0) {
                    cObj["eyesPartFormId"] = std::format("{:08X}", rawChild->eyesPartFormId.raw());
                }
                if (rawChild->hairPartFormId.raw() != 0) {
                    cObj["hairPartFormId"] = std::format("{:08X}", rawChild->hairPartFormId.raw());
                }
                if (rawChild->facialHairPartFormId.raw() != 0) {
                    cObj["facialHairPartFormId"] = std::format("{:08X}", rawChild->facialHairPartFormId.raw());
                }
                cObj["heightScale"] = rawChild->heightScale;
                cObj["rpgApplied"] = rawChild->rpgApplied;
                cObj["hasBeastblood"] = perceived.hasBeastblood;
                cObj["beastbloodEvaluated"] = view.IsOmniscient() ? rawChild->beastbloodEvaluated : false;
                cObj["bioLineageEvaluated"] = rawChild->bioLineageEvaluated;
                cObj["guardianLineageEvaluated"] = rawChild->guardianLineageEvaluated;
                if (!rawChild->daedricBlessingId.empty()) {
                    cObj["daedricBlessingId"] = rawChild->daedricBlessingId;
                }
                if (rawChild->lastAppliedLevel > 0) {
                    cObj["lastAppliedLevel"] = rawChild->lastAppliedLevel;
                }
            } else {
                cObj["heightScale"] = 1.0f;
                cObj["rpgApplied"] = false;
                cObj["hasBeastblood"] = perceived.hasBeastblood;
                cObj["beastbloodEvaluated"] = false;
                cObj["bioLineageEvaluated"] = false;
                cObj["guardianLineageEvaluated"] = false;
            }
            auto* regActor = HasPhysicalActor(childID)
                ? NPCIndex::GetSingleton().GetActorFromFormId(childID)
                : nullptr;
            bool regHasRealActor = (regActor != nullptr && !regActor->IsDeleted());
            cObj["hasRefID"] = regHasRealActor;
            cObj["isEphemeral"] = !regHasRealActor || (perceived.stage == State::ChildStage::Unborn) || (perceived.stage == State::ChildStage::Infant) || (perceived.stage == State::ChildStage::Training);
            if (perceived.paternitySecret.has_value()) {
                const auto& sec = *perceived.paternitySecret;
                cObj["paternitySecret"] = {
                    {"isSecret", sec.isSecret},
                    {"isSecretForBioFather", sec.isSecretForBioFather},
                    {"isSecretForPartner", sec.isSecretForPartner},
                    {"discovered", sec.discovered},
                    {"discoveredByPartner", sec.discoveredByPartner},
                    {"discoveredByBioFather", sec.discoveredByBioFather},
                    {"hasPhenotypeMismatch", sec.hasPhenotypeMismatch},
                    {"lastCheckTimestamp", (view.IsOmniscient() && rawChild) ? rawChild->paternitySecret.lastCheckTimestamp : 0.0f},
                    {"bioFatherName", sec.trueBioFatherFallbackName},
                    {"bioFatherRaceFormId", sec.trueBioFatherRaceId.raw() != 0 ? std::format("{:08X}", sec.trueBioFatherRaceId.raw()) : ""}
                };
            } else {
                cObj["paternitySecret"] = {
                    {"isSecret", false},
                    {"isSecretForBioFather", false},
                    {"isSecretForPartner", false},
                    {"discovered", false},
                    {"discoveredByPartner", false},
                    {"discoveredByBioFather", false},
                    {"hasPhenotypeMismatch", false},
                    {"lastCheckTimestamp", 0.0f},
                    {"bioFatherName", ""},
                    {"bioFatherRaceFormId", ""}
                };
            }
            childrenRegArray.push_back(cObj);
        }
        root["childrenRegistry"] = childrenRegArray;

        // 4. Pending Relationships
        nlohmann::json pendingRelsArray = nlohmann::json::array();
        for (const auto& [baseId, relsList] : snap->actors->AllPendingRelationships()) {
            for (const auto& entry : relsList) {
                const PendingRelationshipData r = LegacyAdapters::ToLegacyPendingRelationship(entry);
                nlohmann::json rObj;
                rObj["baseFormId"] = std::format("{:08X}", baseId.raw());
                rObj["partnerBaseId"] = std::format("{:08X}", r.partnerBaseID);
                rObj["partnerRefId"] = std::format("{:08X}", r.partnerRefID);
                rObj["targetStatus"] = {
                    {"id", static_cast<uint8_t>(r.targetStatus)},
                    {"name", Utils::RelationshipStatusToString(r.targetStatus)}
                };
                rObj["originalStatus"] = {
                    {"id", static_cast<uint8_t>(r.originalStatus)},
                    {"name", Utils::RelationshipStatusToString(r.originalStatus)}
                };
                pendingRelsArray.push_back(rObj);
            }
        }
        root["pendingRelationships"] = pendingRelsArray;

        // 5. Pending Children
        nlohmann::json pendingChildrenArray = nlohmann::json::array();
        for (const auto& [cBaseId, cEntry] : snap->lineage->AllPendingChildren()) {
            const PendingChildData cData = LegacyAdapters::ToLegacyPendingChild(cEntry);
            nlohmann::json cObj;
            cObj["childBaseId"] = std::format("{:08X}", cBaseId.raw());
            cObj["childRefId"] = std::format("{:08X}", cData.childRefID);
            cObj["parentABaseId"] = std::format("{:08X}", cData.parentABaseID);
            cObj["parentARefId"] = std::format("{:08X}", cData.parentARefID);
            cObj["parentBBaseId"] = std::format("{:08X}", cData.parentBBaseID);
            cObj["parentBRefId"] = std::format("{:08X}", cData.parentBRefID);
            cObj["isAdopted"] = cData.isAdopted;
            pendingChildrenArray.push_back(cObj);
        }
        root["pendingChildren"] = pendingChildrenArray;

        // 6. Active Pregnancies
        nlohmann::json pregArray = nlohmann::json::array();
        for (const auto& perceived : visibleChildren) {
            if (perceived.stage != State::ChildStage::Unborn) continue;
            const RE::FormID bioMotherID = perceived.bioMother.raw();
            const RE::FormID bioFatherID = perceived.perceivedBioFather.raw();
            const RE::FormID presumedFatherID = perceived.presumedFather.raw();
            nlohmann::json pObj;
            pObj["mother"] = {
                {"formId", std::format("{:08X}", bioMotherID)},
                {"name", ActorSvc::GetName(bioMotherID)}
            };
            pObj["isPregnant"] = true;
            pObj["conceptionTimestamp"] = perceived.conceptionTimestamp;
            pObj["presumedFather"] = {
                {"formId", presumedFatherID != 0 ? std::format("{:08X}", presumedFatherID) : ""},
                {"name", presumedFatherID != 0 ? ActorSvc::GetName(presumedFatherID) : ""}
            };
            std::string pregFatherName = "";
            if (bioFatherID == kUnknownFatherFormID) {
                pregFatherName = "Unknown";
            } else if (bioFatherID != 0) {
                pregFatherName = ActorSvc::GetName(bioFatherID);
            } else if (!perceived.perceivedBioFatherFallbackName.empty()) {
                pregFatherName = perceived.perceivedBioFatherFallbackName;
            }
            pObj["bioFather"] = {
                {"formId", std::format("{:08X}", bioFatherID)},
                {"name", pregFatherName}
            };
            pregArray.push_back(pObj);
        }
        root["pregnancyState"] = pregArray;

        // 7. Pending Partner Intents
        nlohmann::json intentsArray = nlohmann::json::array();
        for (const auto& [partnerId, rawIntent] : snap->relationships->AllPendingIntents()) {
            (void)rawIntent;
            const State::PendingPartnerIntentRecord stateIntent = view.GetPerceivedPendingIntent(partnerId);
            const PendingPartnerIntent intent = LegacyAdapters::ToLegacyPendingPartnerIntent(stateIntent);
            if (intent.type == PartnerIntentType::None) continue;
            nlohmann::json iObj;
            iObj["partner"] = {
                {"formId", std::format("{:08X}", partnerId.raw())},
                {"name", ActorSvc::GetName(partnerId.raw())}
            };
            iObj["intent"] = {
                {"id", static_cast<uint8_t>(intent.type)},
                {"name", Utils::PartnerIntentTypeToString(intent.type)}
            };
            iObj["createdGameDay"] = intent.createdGameDay;
            iObj["relatedLover"] = {
                {"formId", intent.relatedLoverID != 0 ? std::format("{:08X}", intent.relatedLoverID) : ""},
                {"name", intent.relatedLoverID != 0 ? ActorSvc::GetName(intent.relatedLoverID) : ""}
            };
            intentsArray.push_back(iObj);
        }
        root["pendingPartnerIntents"] = intentsArray;
        root["nextSyntheticChildId"] = std::format("{:08X}", snap->lineage->NextSyntheticChildRaw());

        return root;
    }

    nlohmann::json NPCRelationshipManager::DumpJson() const {
        return DumpJson(Projection::PerspectiveView::ForDashboard(
            State::WorldStore::GetSingleton().Snapshot(),
            State::SpatialSnapshotStore::GetSingleton().Snapshot(),
            &GetEngineWerewolfStateProvider()));
    }

}  // namespace MARAS


