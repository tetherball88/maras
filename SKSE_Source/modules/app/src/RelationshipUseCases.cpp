#include "maras/app/RelationshipUseCases.h"

#include "maras/app/AffectionEventCoordinator.h"
#include "maras/app/AppContext.h"
#include "maras/app/BanterSceneCoordinator.h"
#include "maras/app/LineageUseCases.h"
#include "maras/app/ModalTypes.h"
#include "maras/app/SettingsCoordinator.h"
#include "maras/domain/relationships/AffectionRules.h"
#include "maras/domain/relationships/InfidelityRules.h"
#include "maras/domain/relationships/MarriageDifficultyRules.h"
#include "maras/domain/relationships/RelationshipRules.h"
#include "maras/domain/relationships/RelationshipSettlementRules.h"
#include "maras/domain/relationships/SpouseHierarchyRules.h"
#include "maras/engine/ActorEngineUtils.h"
#include "maras/engine/ActorService.h"
#include "maras/engine/BedOwnershipReconciler.h"
#include "maras/engine/BuffReconciler.h"
#include "maras/engine/DeferredActorResolver.h"
#include "maras/engine/EngineLogging.h"
#include "maras/engine/FactionReconciler.h"
#include "maras/engine/FormCache.h"
#include "maras/engine/FormUtils.h"
#include "maras/engine/GlobalVarReconciler.h"
#include "maras/engine/HouseholdEngineOps.h"
#include "maras/engine/LocalizationService.h"
#include "maras/engine/MarriageDifficultyLoader.h"
#include "maras/engine/NPCTypeDeterminer.h"
#include "maras/engine/PackageReconciler.h"
#include "maras/engine/RelationshipEngineReconciler.h"
#include "maras/engine/SpatialWorldIndex.h"
#include "maras/engine/TaskDispatcher.h"
#include "maras/engine/TrainingCatalogLoader.h"
#include "maras/foundation/IRng.h"
#include "maras/foundation/StringUtils.h"
#include "maras/state/SpatialActorSnapshot.h"
#include "maras/state/WorldStore.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <format>
#include <mutex>
#include <shared_mutex>
#include <unordered_set>

namespace MARAS::App {

    namespace {

        constexpr RE::FormID kSiblingsAssocFormID = 0x000142C5;
        constexpr RE::FormID kParentChildAssocFormID = 0x000142C6;
        constexpr RE::FormID kAuntUncleAssocFormID = 0x000142C7;
        constexpr RE::FormID kCousinsAssocFormID = 0x000142C8;
        constexpr RE::FormID kSpouseAssocFormID = 0x000142CA;
        constexpr RE::FormID kGrandparentGrandchildAssocFormID = 0x0001E803;
        constexpr RE::FormID kGrandAuntUncleAssocFormID = 0x0001E80B;
        constexpr RE::FormID kGreatGrandparentAssocFormID = 0x0001E90B;
        constexpr RE::FormID kCourtingAssocFormID = 0x0001EE23;

        [[nodiscard]] float GetCurrentGameDays() noexcept {
            if (auto* calendar = RE::Calendar::GetSingleton()) {
                return calendar->GetDaysPassed();
            }
            return 0.0f;
        }

        [[nodiscard]] bool IsPlayerFormID(RE::FormID id) noexcept {
            return id == RelationshipUseCases::kPlayerFormID ||
                   id == RelationshipUseCases::kPlayerBaseFormID;
        }

        [[nodiscard]] bool QuestReachedStage(RE::TESQuest* quest, std::uint32_t targetStage) noexcept {
            return quest && quest->GetCurrentStageID() >= targetStage;
        }

        [[nodiscard]] std::string StageToString(State::ChildStage stage) {
            switch (stage) {
                case State::ChildStage::Unborn:
                    return "unborn";
                case State::ChildStage::Infant:
                    return "infant";
                case State::ChildStage::Child:
                    return "child";
                case State::ChildStage::Training:
                    return "training";
                case State::ChildStage::Adult:
                    return "adult";
                default:
                    return "child";
            }
        }

        [[nodiscard]] StatusChangeRecord ToPublicStatusRecord(const State::StatusTransitionEntry& src) {
            return StatusChangeRecord{
                .fromStatus = src.fromStatus,
                .toStatus = src.toStatus,
                .timestamp = src.timestamp,
                .initiatorFormID = src.initiator.raw()
            };
        }

        [[nodiscard]] CheatingRecord ToPublicCheatingRecord(const State::AffairRecord& src) {
            return CheatingRecord{
                .cheaterID = src.cheater.raw(),
                .victimID = src.deceivedPartner.raw(),
                .affairPartnerID = src.paramour.raw(),
                .count = src.incidentCount,
                .lastTimestamp = src.lastTimestamp,
                .isSecret = src.isSecret,
                .affairType = src.affairType,
                .affairStatus = src.affairStatus,
                .affairStage = src.affairStage,
                .stageStartDay = src.stageStartDay
            };
        }

        [[nodiscard]] NPCProfileData ToPublicProfile(const State::ActorProfileRecord& src) {
            NPCProfileData dst;
            dst.formID = src.refId.raw();
            dst.socialClass = src.socialClass;
            dst.skillType = src.skillType;
            dst.temperament = src.temperament;
            if (src.legacyOriginalHome.has_value()) {
                dst.originalHome = src.legacyOriginalHome->raw();
            }
            if (src.legacyCurrentHome.has_value()) {
                dst.currentHome = src.legacyCurrentHome->raw();
            }
            if (src.legacyHomeMarker.has_value()) {
                dst.homeMarker = src.legacyHomeMarker->raw();
            }
            for (const auto kw : src.addedKeywords) {
                dst.addedKeywords.insert(kw.raw());
            }
            return dst;
        }

        [[nodiscard]] NPCRelationshipData ToPublicRelationshipData(
            const State::RelationshipEdgeRecord& edge,
            const std::vector<State::AffairRecord>& affairs) {
            NPCRelationshipData dst;
            dst.pairKey = edge.pair;
            dst.status = edge.status;
            dst.originalStatus = edge.originalStatus;
            dst.isOpen = edge.isOpen;
            dst.statusHistory.reserve(edge.statusHistory.size());
            for (const auto& entry : edge.statusHistory) {
                dst.statusHistory.push_back(ToPublicStatusRecord(entry));
            }
            dst.cheatingRecords.reserve(affairs.size());
            for (const auto& affair : affairs) {
                dst.cheatingRecords.push_back(ToPublicCheatingRecord(affair));
            }
            return dst;
        }

        struct CachedActorMeta {
            State::ActorSex sex{ State::ActorSex::Unknown };
            bool aliveAndValid{ false };
            bool dynamicPresent{ true };
            std::string name{};
            State::FormRawId raceId{};
            State::BaseId baseId{};
        };

        // Pre-captures Skyrim TESForm/SpatialWorldIndex data BEFORE WorldStore::Modify
        // so write locks never call back into the game engine.
        class CachedActorMetadataProvider final : public State::IActorMetadataProvider {
        public:
            void CaptureActors(
                const std::shared_ptr<const State::WorldStateSnapshot>& snap,
                std::initializer_list<RE::FormID> explicitIds) {
                for (const RE::FormID id : explicitIds) {
                    CaptureSingle(snap, id);
                }
                if (snap && snap->actors) {
                    for (const auto& [refId, prof] : snap->actors->AllProfiles()) {
                        if (prof.isRegisteredAdult && !refId.IsNull()) {
                            CaptureSingle(snap, refId.raw());
                        }
                    }
                }
            }

            void CaptureSingle(
                const std::shared_ptr<const State::WorldStateSnapshot>& snap,
                RE::FormID rawId) {
                if (rawId == 0) {
                    return;
                }
                const State::ActorId actorId{ rawId };
                if (entries_.contains(actorId)) {
                    return;
                }

                CachedActorMeta meta;
                if (rawId == RelationshipUseCases::kPlayerFormID ||
                    rawId == RelationshipUseCases::kPlayerBaseFormID) {
                    meta.aliveAndValid = true;
                    meta.dynamicPresent = true;
                    meta.baseId = State::BaseId{ RelationshipUseCases::kPlayerBaseFormID };
                    if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                        meta.name = player->GetName() ? player->GetName() : "Player";
                        if (auto* base = player->GetActorBase()) {
                            meta.sex = (base->GetSex() == RE::SEXES::kFemale) ? State::ActorSex::Female : State::ActorSex::Male;
                        }
                        if (auto* race = player->GetRace()) {
                            meta.raceId = State::FormRawId{ race->GetFormID() };
                        }
                    }
                    if (meta.name.empty()) {
                        meta.name = "Player";
                    }
                    entries_.emplace(actorId, std::move(meta));
                    return;
                }

                const auto& idx = SpatialWorldIndex::GetSingleton();
                const RE::FormID baseIdRaw = idx.GetBaseID(rawId);
                meta.baseId = State::BaseId{ baseIdRaw };

                const bool isDeceased =
                    snap && snap->deceased &&
                    snap->deceased->IsDeceased(actorId, meta.baseId);

                auto* liveActor = RE::TESForm::LookupByID<RE::Actor>(rawId);
                if (!liveActor) {
                    liveActor = idx.GetActorFromFormId(rawId);
                }

                if (actorId.IsDynamicRuntimeActor()) {
                    meta.dynamicPresent = (liveActor != nullptr && !liveActor->IsDeleted());
                } else {
                    meta.dynamicPresent = true;
                }

                const bool engineDeadOrDeleted =
                    (liveActor && (liveActor->IsDead() || liveActor->IsDeleted()));
                meta.aliveAndValid =
                    !actorId.IsNull() &&
                    !actorId.IsUnknownFather() &&
                    !actorId.IsSyntheticChild() &&
                    !isDeceased &&
                    !engineDeadOrDeleted &&
                    meta.dynamicPresent;

                const std::uint8_t sexVal = ActorSvc::GetSex(rawId);
                meta.sex = (sexVal == 1) ? State::ActorSex::Female
                         : (sexVal == 0) ? State::ActorSex::Male
                                         : State::ActorSex::Unknown;
                meta.name = ActorSvc::GetName(rawId);
                if (auto* race = ActorSvc::GetRace(rawId)) {
                    meta.raceId = State::FormRawId{ race->GetFormID() };
                }

                entries_.emplace(actorId, std::move(meta));
            }

            [[nodiscard]] State::ActorSex GetSex(State::ActorId actor) const noexcept override {
                if (const auto it = entries_.find(actor); it != entries_.end()) {
                    return it->second.sex;
                }
                return State::ActorSex::Unknown;
            }

            [[nodiscard]] bool IsAliveAndValid(State::ActorId actor) const noexcept override {
                if (const auto it = entries_.find(actor); it != entries_.end()) {
                    return it->second.aliveAndValid;
                }
                return !actor.IsNull() && !actor.IsUnknownFather() && !actor.IsSyntheticChild();
            }

            [[nodiscard]] bool IsDynamicActorPresent(State::RefId refId) const noexcept override {
                if (const auto it = entries_.find(State::ActorId{ refId.raw() }); it != entries_.end()) {
                    return it->second.dynamicPresent;
                }
                return !refId.IsNull();
            }

            [[nodiscard]] std::string GetActorName(State::ActorId actor) const override {
                if (const auto it = entries_.find(actor); it != entries_.end() && !it->second.name.empty()) {
                    return it->second.name;
                }
                return IActorMetadataProvider::GetActorName(actor);
            }

            [[nodiscard]] State::FormRawId GetActorRaceId(State::ActorId actor) const noexcept override {
                if (const auto it = entries_.find(actor); it != entries_.end()) {
                    return it->second.raceId;
                }
                return {};
            }

            [[nodiscard]] State::BaseId GetActorBaseId(State::ActorId actor) const noexcept override {
                if (const auto it = entries_.find(actor); it != entries_.end()) {
                    return it->second.baseId;
                }
                return IActorMetadataProvider::GetActorBaseId(actor);
            }

        private:
            std::unordered_map<State::ActorId, CachedActorMeta> entries_{};
        };

        [[nodiscard]] std::string FormatFormIdUpper(RE::FormID id) {
            return std::format("{:08X}", id);
        }

        [[nodiscard]] std::string FormatCellName(RE::FormID cellId) {
            if (cellId == 0) {
                return {};
            }
            if (auto* cell = RE::TESForm::LookupByID<RE::TESObjectCELL>(cellId)) {
                return cell->GetName() ? std::string(cell->GetName()) : std::string{};
            }
            return {};
        }

        void EmitStatusChangedModEvents(RE::FormID npcFormID,
                                        RE::FormID targetFormID,
                                        Foundation::RelationshipStatus status) {
            auto* npcActor = RE::TESForm::LookupByID<RE::Actor>(npcFormID);
            if (!npcActor) {
                npcActor = SpatialWorldIndex::GetSingleton().GetActorFromFormId(npcFormID);
            }
            const RE::FormID senderID = npcActor ? npcActor->GetFormID() : 0;

            const std::int32_t statusEnum = static_cast<std::int32_t>(status);
            const std::string statusString =
                Foundation::StringUtils::ToLowerStd(Foundation::RelationshipStatusToString(status));

            EmitModCallbackEvent("maras_status_changed", statusString, static_cast<float>(statusEnum), senderID);

            nlohmann::json fullJson;
            fullJson["initiator"] = FormatFormIdUpper(npcFormID);
            fullJson["acceptor"] = FormatFormIdUpper(targetFormID);
            fullJson["initiatorName"] = ActorSvc::GetName(npcFormID);
            fullJson["acceptorName"] = ActorSvc::GetName(targetFormID);
            fullJson["status"] = statusString;

            const std::string fullJsonStr =
                fullJson.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            EmitModCallbackEvent("maras_status_changed_full", fullJsonStr, static_cast<float>(statusEnum), senderID);
            if (GetSkyrimNetGateway().IsAvailable()) {
                GetSkyrimNetGateway().EmitMaritalStatusChange(npcFormID, targetFormID, statusString);
            }
        }

        void EmitAffectionChangeModEvent(RE::FormID npcFormID, std::string_view threshold, int delta) {
            if (npcFormID == 0) {
                return;
            }
            if (!RE::TESForm::LookupByID(npcFormID)) {
                MARAS_LOG_WARN("SendAffectionChangeEvent: Form {:08X} not found", npcFormID);
                return;
            }
            RelationshipEngineReconciler::GetSingleton().NotifyAffectionThresholdChange(
                npcFormID, threshold, static_cast<float>(delta));
            EmitModCallbackEvent("maras_change_affection",
                                 std::string(threshold),
                                 static_cast<float>(delta),
                                 npcFormID);
            if (GetSkyrimNetGateway().IsAvailable()) {
                const std::string_view direction =
                    (delta > 0) ? "improved" : ((delta < 0) ? "worsened" : "");
                GetSkyrimNetGateway().EmitSpouseAffectionChange(npcFormID, threshold, direction);
            }
        }

        void EmitHierarchyChangedModEvent(RE::FormID npcFormID, std::string_view action, float diff) {
            if (npcFormID == 0) {
                return;
            }
            EmitModCallbackEvent("maras_hierarchy_changed", std::string(action), diff, npcFormID);
            if (diff != 0.0f && GetSkyrimNetGateway().IsAvailable()) {
                GetSkyrimNetGateway().EmitSpouseHierarchyChange(npcFormID, diff < 0.0f);
            }
        }

        void ReconcileDirtyActorsAndChildren(const State::WorldMutationResult& mutations,
                                             bool reconcilePackages) {
            auto& factionRec = FactionReconciler::GetSingleton();
            auto& pkgRec = PackageReconciler::GetSingleton();
            std::unordered_set<RE::FormID> seen;
            for (const auto actorId : mutations.dirtyActors) {
                if (actorId.IsNull() || !seen.insert(actorId.raw()).second) {
                    continue;
                }
                if (actorId.IsPlayer()) {
                    factionRec.ReconcilePlayer();
                } else {
                    factionRec.ReconcileActor(actorId.raw());
                    if (reconcilePackages) {
                        pkgRec.ReconcileActor(actorId.raw());
                    }
                }
            }
            for (const auto childId : mutations.dirtyChildren) {
                if (!childId.HasPhysicalActor() || !seen.insert(childId.raw()).second) {
                    continue;
                }
                factionRec.ReconcileActor(childId.raw());
                if (reconcilePackages) {
                    pkgRec.ReconcileActor(childId.raw());
                }
            }
        }

        [[nodiscard]] bool HasEngineParentChildLink(RE::FormID childActorID, RE::FormID parentActorID) {
            if (childActorID == 0 || parentActorID == 0) {
                return false;
            }
            auto* childBase = ActorSvc::GetActorBase(childActorID);
            auto* parentBase = IsPlayerFormID(parentActorID)
                                   ? RE::TESForm::LookupByID<RE::TESNPC>(RelationshipUseCases::kPlayerBaseFormID)
                                   : ActorSvc::GetActorBase(parentActorID);
            if (!childBase || !parentBase) {
                return false;
            }
            if (auto* rel = RE::BGSRelationship::GetRelationship(parentBase, childBase);
                rel && rel->assocType && rel->assocType->GetFormID() == kParentChildAssocFormID) {
                if ((rel->npc1 == parentBase && rel->npc2 == childBase) ||
                    (rel->npc2 == parentBase && rel->npc1 == childBase)) {
                    return true;
                }
            }
            return false;
        }

        void CheckVanillaEngineFamily(RE::FormID actorA,
                                      RE::FormID actorB,
                                      bool& outIsFamily,
                                      std::string& outReason) {
            outIsFamily = false;
            outReason.clear();

            auto* baseA = IsPlayerFormID(actorA)
                              ? ActorSvc::GetActorBase(RelationshipUseCases::kPlayerFormID)
                              : ActorSvc::GetActorBase(actorA);
            auto* baseB = IsPlayerFormID(actorB)
                              ? ActorSvc::GetActorBase(RelationshipUseCases::kPlayerFormID)
                              : ActorSvc::GetActorBase(actorB);
            if (!baseA || !baseB || baseA == baseB) {
                return;
            }

            std::string nameA = ActorSvc::GetName(actorA);
            std::string nameB = ActorSvc::GetName(actorB);
            if (nameA.empty()) {
                nameA = std::format("{:08X}", actorA);
            }
            if (nameB.empty()) {
                nameB = std::format("{:08X}", actorB);
            }

            auto describeAssoc = [&](const RE::BGSRelationship* rel) -> bool {
                if (!rel || !rel->assocType) {
                    return false;
                }
                const RE::FormID assocID = rel->assocType->GetFormID();
                if (assocID == kSiblingsAssocFormID) {
                    outReason = std::format("{} and {} are siblings (brother/sister).", nameA, nameB);
                    return true;
                }
                if (assocID == kParentChildAssocFormID) {
                    outReason = std::format("{} and {} have a parent/child relationship.", nameA, nameB);
                    return true;
                }
                if (assocID == kAuntUncleAssocFormID) {
                    outReason = std::format("{} and {} have an aunt/uncle and niece/nephew relationship.", nameA, nameB);
                    return true;
                }
                if (assocID == kCousinsAssocFormID) {
                    outReason = std::format("{} and {} are cousins.", nameA, nameB);
                    return true;
                }
                if (assocID == kGrandparentGrandchildAssocFormID) {
                    outReason = std::format("{} and {} have a grandparent/grandchild relationship.", nameA, nameB);
                    return true;
                }
                if (assocID == kGrandAuntUncleAssocFormID) {
                    outReason = std::format("{} and {} have a grand-aunt/uncle relationship.", nameA, nameB);
                    return true;
                }
                if (assocID == kGreatGrandparentAssocFormID) {
                    outReason = std::format("{} and {} have a great-grandparent/grandchild relationship.", nameA, nameB);
                    return true;
                }
                return false;
            };

            if (describeAssoc(RE::BGSRelationship::GetRelationship(baseA, baseB)) ||
                describeAssoc(RE::BGSRelationship::GetRelationship(baseB, baseA))) {
                outIsFamily = true;
                return;
            }

            // Detect implicit vanilla siblings sharing a ParentChild BGSRelationship parent
            if (auto* dataHandler = RE::TESDataHandler::GetSingleton()) {
                std::unordered_set<const RE::TESNPC*> parentsA;
                std::unordered_set<const RE::TESNPC*> parentsB;
                for (const auto* rel : dataHandler->GetFormArray<RE::BGSRelationship>()) {
                    if (!rel || !rel->assocType || rel->assocType->GetFormID() != kParentChildAssocFormID) {
                        continue;
                    }
                    if (rel->npc2 == baseA && rel->npc1 && rel->npc1 != baseA) {
                        parentsA.insert(rel->npc1);
                    }
                    if (rel->npc2 == baseB && rel->npc1 && rel->npc1 != baseB) {
                        parentsB.insert(rel->npc1);
                    }
                }
                for (const auto* p : parentsA) {
                    if (parentsB.contains(p)) {
                        outIsFamily = true;
                        outReason = std::format("{} and {} share a vanilla parent (half/full siblings).", nameA, nameB);
                        return;
                    }
                }
            }
        }

        struct RelationshipCheckData {
            CachedActorMetadataProvider meta{};
            Domain::Relationships::RelationshipCheckContext ctx{};
        };

        [[nodiscard]] RelationshipCheckData CaptureRelationshipCheckData(
            const std::shared_ptr<const State::WorldStateSnapshot>& snap,
            const RelationshipUseCases& self,
            RE::FormID actorA,
            RE::FormID actorB) {
            RelationshipCheckData data;
            data.meta.CaptureActors(snap, { actorA, actorB });
            data.ctx.isChildA = self.IsChildNPC(actorA);
            data.ctx.isChildB = self.IsChildNPC(actorB);
            if (IsPlayerFormID(actorA) || IsPlayerFormID(actorB)) {
                const RE::FormID partnerID = IsPlayerFormID(actorA) ? actorB : actorA;
                data.ctx.playerRelationshipRank =
                    RelationshipEngineReconciler::GetSingleton().GetVanillaRelationshipRank(
                        partnerID, RelationshipUseCases::kPlayerFormID);
            }
            CheckVanillaEngineFamily(actorA, actorB, data.ctx.areEngineFamily, data.ctx.engineFamilyReason);
            return data;
        }

        void ReconcileHierarchySlotsAndEvents(const Domain::Relationships::HierarchyMutationOutcome& outcome,
                                              bool emitModEvents = true) {
            if (!outcome.validRequest && !outcome.slotsChanged) {
                return;
            }
            auto& factionRec = FactionReconciler::GetSingleton();
            std::unordered_set<RE::FormID> seen;
            auto syncActor = [&](RE::FormID id) {
                if (id != 0 && seen.insert(id).second) {
                    factionRec.ReconcileActor(id);
                }
            };
            for (const auto id : outcome.clearedActors) {
                syncActor(id.raw());
            }
            for (const auto id : outcome.newSlots) {
                syncActor(id.raw());
            }
            for (const auto id : outcome.dirtyActors) {
                syncActor(id.raw());
            }
            if (emitModEvents) {
                for (const auto& ev : outcome.events) {
                    if (ev.rankDiff != 0.0f) {
                        AffectionEventCoordinator::GetSingleton().OnHierarchyRankChanged(
                            ev.spouse.raw(), static_cast<std::int32_t>(ev.rankDiff));
                        RelationshipEngineReconciler::GetSingleton().ReconcileHierarchyRankChange(
                            ev.spouse.raw(), ev.rankDiff);
                    }
                    EmitHierarchyChangedModEvent(ev.spouse.raw(),
                                                 ev.isPromotion ? "promote" : "demote",
                                                 ev.rankDiff);
                }
            }
            BuffReconciler::GetSingleton().ReconcilePlayerBuffs();
            GlobalVarReconciler::GetSingleton().ReconcileGlobals();
            GetUiBridgeGateway().OnHierarchyChanged();
        }

    }  // namespace

    RelationshipUseCases& RelationshipUseCases::GetSingleton() {
        static RelationshipUseCases instance;
        return instance;
    }

    // =========================================================================
    // Lifecycle, Load & Engine Hooks
    // =========================================================================

    void RelationshipUseCases::FinalizeCoSaveLoad(bool loadedAny) {
        (void)loadedAny;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        auto& idx = SpatialWorldIndex::GetSingleton();

        // 1. Re-apply persisted runtime keywords on loaded TESNPC base forms
        for (const auto& [refId, profile] : snap->actors->AllProfiles()) {
            if (profile.addedKeywords.empty() || !profile.isRegisteredAdult) {
                continue;
            }
            auto* baseForm = RE::TESForm::LookupByID<RE::TESNPC>(profile.baseId.raw());
            if (!baseForm) {
                if (auto* actor = idx.GetActorFromFormId(refId.raw())) {
                    baseForm = actor->GetActorBase();
                }
            }
            if (!baseForm) {
                continue;
            }
            for (const auto kwId : profile.addedKeywords) {
                if (auto* kw = RE::TESForm::LookupByID<RE::BGSKeyword>(kwId.raw())) {
                    if (!baseForm->HasKeyword(kw)) {
                        baseForm->AddKeyword(kw);
                    }
                }
            }
        }

        // 2. Re-resolve any pending relationships whose actors are already in the spatial index
        std::vector<std::pair<RE::FormID, RE::FormID>> resolvedBases;
        for (const auto& [baseId, entries] : snap->actors->AllPendingRelationships()) {
            if (entries.empty()) {
                continue;
            }
            const RE::FormID refId = idx.GetRefID(baseId.raw());
            if (refId != 0 && refId != baseId.raw()) {
                resolvedBases.emplace_back(baseId.raw(), refId);
            }
        }
        for (const auto& [baseId, refId] : resolvedBases) {
            TryResolvePendingRelationships(baseId, refId);
        }

        // 3. Reconcile adult/child factions, shared beds, world packages, buffs, and globals
        FactionReconciler::GetSingleton().ReconcileAllOnLoad();
        UpdateAllSpousesCheatingAndOpenFactions();
        UpdateAllCanFormNonCheatingMainFactions();
        UpdateAllSpousesChildrenFaction();
        BedOwnershipReconciler::GetSingleton().ReapplyAllSharedCellsOnLoad();
        PackageReconciler::GetSingleton().ReconcileWorld();
        GlobalVarReconciler::GetSingleton().ReconcileGlobals();
        BuffReconciler::GetSingleton().ReconcilePlayerBuffs();

        MARAS_LOG_INFO("RelationshipUseCases::FinalizeCoSaveLoad completed (profiles={}, edges={}, loadedAny={})",
                       snap->actors->Size(), snap->relationships->EdgeCount(), loadedAny);
    }

    void RelationshipUseCases::FinalizeLoad() {
        FinalizeCoSaveLoad(true);
    }

    void RelationshipUseCases::Revert() {
        Clear();
    }

    void RelationshipUseCases::Clear() {
        // WorldStore reset is performed centrally by WorldStore::Revert(); keep local caches clean if needed.
    }

    void RelationshipUseCases::SetVanillaChildScanHandler(VanillaChildScanHandler handler) {
        std::unique_lock lock(childScanMutex_);
        vanillaChildScanHandler_ = std::move(handler);
    }

    void RelationshipUseCases::OnActorLoaded(RE::FormID actorId, bool fromInitScript) {
        if (!Foundation::HasPhysicalActorRawId(actorId)) {
            return;
        }
        auto* actor = RE::TESForm::LookupByID<RE::Actor>(actorId);
        if (!actor || actor->IsDeleted()) {
            return;
        }
        auto* base = actor->GetActorBase();
        if (!base) {
            return;
        }

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (const auto* prof = snap->actors->Find(State::RefId{ actorId })) {
            for (const auto kwId : prof->addedKeywords) {
                if (auto* kw = RE::TESForm::LookupByID<RE::BGSKeyword>(kwId.raw())) {
                    if (!base->HasKeyword(kw)) {
                        base->AddKeyword(kw);
                    }
                }
            }
        }

        TryResolvePendingRelationships(base->GetFormID(), actorId);

        if (fromInitScript || snap->actors->IsRegisteredAdult(State::RefId{ actorId })) {
            FactionReconciler::GetSingleton().ReconcileLoadedActor(actor);
        }
    }

    void RelationshipUseCases::TryResolvePendingRelationships(RE::Actor* actor) {
        if (!actor) {
            return;
        }
        if (auto* base = actor->GetActorBase()) {
            TryResolvePendingRelationships(base->GetFormID(), actor->GetFormID());
        }
    }

    void RelationshipUseCases::TryResolvePendingRelationships(RE::FormID baseID, RE::FormID refID) {
        if (baseID == 0 || refID == 0 || baseID == refID) {
            return;
        }

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto& pendingMap = snap->actors->AllPendingRelationships();
        const auto it = pendingMap.find(State::BaseId{ baseID });
        if (it == pendingMap.end() || it->second.empty()) {
            return;
        }

        std::vector<State::PendingRelationshipEntry> entries = it->second;
        auto& idx = SpatialWorldIndex::GetSingleton();
        bool anyImported = false;

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            (void)ws.actors.Write().ErasePendingRelationshipsForBase(State::BaseId{ baseID });
        });

        for (const auto& entry : entries) {
            RE::FormID partnerRef = entry.partnerRefId.raw();
            if (partnerRef == 0 && !entry.partnerBaseId.IsNull()) {
                const RE::FormID lookedUp = idx.GetRefID(entry.partnerBaseId.raw());
                if (lookedUp != 0 && lookedUp != entry.partnerBaseId.raw()) {
                    partnerRef = lookedUp;
                }
            }

            if (partnerRef == 0 || partnerRef == entry.partnerBaseId.raw()) {
                // Re-queue if partner also remains unspawned
                State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                    ws.actors.Write().AddPendingRelationship(State::BaseId{ baseID }, entry);
                });
                continue;
            }

            EnsureRegistered(refID);
            EnsureRegistered(partnerRef);
            SetOriginalRelationshipStatus(refID, partnerRef, entry.originalStatus);

            const auto currentStatus = GetRelationshipStatus(refID, partnerRef);
            if (currentStatus == Foundation::RelationshipStatus::Candidate ||
                currentStatus == Foundation::RelationshipStatus::Unknown) {
                if (ApplyImportedVanillaStatus(refID, partnerRef, entry.originalStatus, entry.targetStatus)) {
                    const RE::FormID defaultInit = DetermineDefaultInitiator(refID, partnerRef);
                    State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                        auto& edge = ws.relationships.Write().GetOrCreateEdge(
                            State::ActorId{ refID }, State::ActorId{ partnerRef });
                        for (auto& hist : edge.statusHistory) {
                            hist.timestamp = -1.0f;
                            hist.fromStatus = Foundation::RelationshipStatus::Unknown;
                            hist.initiator = State::ActorId{ defaultInit };
                        }
                    });
                    anyImported = true;
                    MARAS_LOG_INFO(
                        "TryResolvePendingRelationships: Imported {} (original {}) between {:08X} and {:08X}",
                        Foundation::RelationshipStatusToString(entry.targetStatus),
                        Foundation::RelationshipStatusToString(entry.originalStatus),
                        refID, partnerRef);
                }
            }
        }

        if (anyImported) {
            SyncAllMarriedCouplesChildCustody();
            GlobalVarReconciler::GetSingleton().ReconcileGlobals();
        }
    }

    void RelationshipUseCases::ProcessPollingAutoMarriageTick(float currentGameDay) {
        CheckAutoMarriageForEngagedNPCs(currentGameDay);
    }

    void RelationshipUseCases::CheckAutoMarriageForEngagedNPCs(float currentDay) {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto candidates = Domain::Relationships::CollectAutoMarriageCandidates(*snap, currentDay, 5.0f);
        if (candidates.empty()) {
            return;
        }

        std::vector<State::CanonicalPairKey> toBackfill;
        std::vector<State::CanonicalPairKey> toMarry;
        for (const auto& c : candidates) {
            if (c.needsTimestampBackfill) {
                toBackfill.push_back(c.pair);
            } else if (c.readyForAutoMarriage) {
                toMarry.push_back(c.pair);
            }
        }

        if (!toBackfill.empty()) {
            State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                Domain::Relationships::BackfillMissingEngagementTimestamps(ws, toBackfill, currentDay);
            });
        }

        for (const auto& pair : toMarry) {
            if (PromoteToMarried(pair.first.raw(), pair.second.raw(), /*sendEvent=*/true, /*breakLovers=*/true)) {
                MARAS_LOG_INFO("CheckAutoMarriageForEngagedNPCs: Auto-married engaged pair ({:08X}, {:08X}) on day {:.2f}",
                               pair.first.raw(), pair.second.raw(), currentDay);
            }
        }
    }

    void RelationshipUseCases::ProcessDailyAffectionTick(float currentGameDay) {
        (void)currentGameDay;
        OnAffectionDayChanged();
    }

    bool RelationshipUseCases::ExecuteQuestCommand(std::string_view commandType,
                                                   RE::FormID npcId,
                                                   std::string_view argument,
                                                   std::string_view context) {
        (void)context;
        if (npcId == 0) {
            return false;
        }

        if (Foundation::StringUtils::CaseInsensitiveEquals(commandType, "promoteToStatus")) {
            if (Foundation::StringUtils::CaseInsensitiveEquals(argument, "deceased")) {
                // Returning false allows QuestEventManager to dispatch OnActorDeath cleanly.
                return false;
            }
            const auto targetStatus = Foundation::StringToRelationshipStatus(argument);
            if (targetStatus == Foundation::RelationshipStatus::Unknown) {
                MARAS_LOG_WARN("ExecuteQuestCommand: Invalid promoteToStatus argument '{}' for {:08X}",
                               argument, npcId);
                return false;
            }
            return PromoteToStatus(npcId, targetStatus, /*sendEvent=*/true, /*breakLovers=*/IsMainTier(targetStatus));
        }

        if (Foundation::StringUtils::CaseInsensitiveEquals(commandType, "setAffection")) {
            try {
                const int val = std::stoi(std::string(argument));
                SetPermanentAffection(npcId, val, true);
                return true;
            } catch (...) {
                return false;
            }
        }

        if (Foundation::StringUtils::CaseInsensitiveEquals(commandType, "changeAffection")) {
            try {
                const int delta = std::stoi(std::string(argument));
                ChangeAffection(npcId, delta, true);
                return true;
            } catch (...) {
                return false;
            }
        }

        if (Foundation::StringUtils::CaseInsensitiveEquals(commandType, "addDailyAffection")) {
            std::string type = "quest";
            float amount = 0.0f;
            const auto colonPos = argument.find(':');
            try {
                if (colonPos != std::string_view::npos) {
                    type = std::string(argument.substr(0, colonPos));
                    amount = std::stof(std::string(argument.substr(colonPos + 1)));
                } else {
                    amount = std::stof(std::string(argument));
                }
                AddAffection(npcId, amount, type);
                return true;
            } catch (...) {
                return false;
            }
        }

        if (Foundation::StringUtils::CaseInsensitiveEquals(commandType, "setSocialClass")) {
            const auto sc = Foundation::StringToSocialClass(argument);
            return SetSocialClass(npcId, static_cast<std::int8_t>(sc));
        }

        if (Foundation::StringUtils::CaseInsensitiveEquals(commandType, "setSkillType")) {
            const auto st = Foundation::StringToSkillType(argument);
            return SetSkillType(npcId, static_cast<std::int8_t>(st));
        }

        if (Foundation::StringUtils::CaseInsensitiveEquals(commandType, "setTemperament")) {
            const auto temp = Foundation::StringToTemperament(argument);
            return SetTemperament(npcId, static_cast<std::int8_t>(temp));
        }

        return false;
    }

    bool RelationshipUseCases::CanAcceptExternalMarriage(RE::FormID actorId) const {
        return CanFormRelationship(actorId, kPlayerFormID, Foundation::RelationshipStatus::Married, nullptr);
    }

    void RelationshipUseCases::HandleExternalMarriageFactionAdded(RE::FormID actorId, std::int8_t rank) {
        (void)rank;
        if (actorId == 0 || actorId == kPlayerFormID) {
            return;
        }
        EnsureRegistered(actorId);
        (void)PromoteToMarried(actorId, kPlayerFormID, /*sendEvent=*/true, /*breakLovers=*/true);
        FactionReconciler::GetSingleton().SanitizePlayerMarriedFaction();
    }

    void RelationshipUseCases::HandleExternalMarriageFactionRemoved(RE::FormID actorId) {
        if (actorId == 0 || actorId == kPlayerFormID) {
            return;
        }
        (void)PromoteToDivorced(actorId, kPlayerFormID, /*sendEvent=*/true);
    }

    void RelationshipUseCases::HandleIntimateCheatingCaught(RE::FormID cheaterId,
                                                            RE::FormID victimId,
                                                            RE::FormID affairPartnerId,
                                                            bool isWalkIn) {
        (void)isWalkIn;
        if (cheaterId != 0) {
            UpdateSpouseCheatingFaction(cheaterId);
            UpdateCanFormNonCheatingMainFaction(cheaterId);
        }
        if (victimId != 0) {
            UpdateSpouseCheatingFaction(victimId);
            UpdateCanFormNonCheatingMainFaction(victimId);
        }
        if (affairPartnerId != 0) {
            UpdateSpouseCheatingFaction(affairPartnerId);
        }
        FactionReconciler::GetSingleton().ReconcilePlayer();
        GetUiBridgeGateway().SendOverviewData();
    }

    // =========================================================================
    // Registration, Unregistration & Death
    // =========================================================================

    bool RelationshipUseCases::EnsureRegistered(RE::FormID npcFormID) {
        if (npcFormID == 0) {
            return false;
        }
        if (IsRegistered(npcFormID)) {
            return true;
        }
        return RegisterCandidate(npcFormID);
    }

    bool RelationshipUseCases::RegisterAsCandidate(RE::FormID npcFormID) {
        return RegisterCandidate(npcFormID);
    }

    bool RelationshipUseCases::RegisterCandidate(RE::FormID npcFormID) {
        if (npcFormID == 0) {
            return false;
        }
        if (IsChildNPC(npcFormID)) {
            MARAS_LOG_WARN("RegisterCandidate: Refusing to register child NPC {:08X} as adult candidate", npcFormID);
            return false;
        }
        const auto preSnap = State::WorldStore::GetSingleton().Snapshot();
        if (preSnap->actors->IsRegisteredAdult(State::RefId{ npcFormID })) {
            return true;
        }

        const auto socialClass = DetermineSocialClass(npcFormID);
        const auto skillType = DetermineSkillType(npcFormID);
        const auto temperament = DetermineTemperament(npcFormID);
        const RE::FormID baseIdRaw = SpatialWorldIndex::GetSingleton().GetBaseID(npcFormID);
        const RE::FormID origHome = SpatialWorldIndex::GetSingleton().GetNpcHome(npcFormID);
        const int initialCandidateAffection = (npcFormID != kPlayerFormID)
            ? Domain::Relationships::ComputeInitialCandidateAffection(
                  RelationshipEngineReconciler::GetSingleton().GetVanillaRelationshipRank(
                      npcFormID, kPlayerFormID))
            : kAffectionDefault;

        State::ActorProfileRecord record;
        record.refId = State::RefId{ npcFormID };
        record.baseId = State::BaseId{ baseIdRaw != 0 ? baseIdRaw : npcFormID };
        record.socialClass = socialClass;
        record.skillType = skillType;
        record.temperament = temperament;
        record.isRegisteredAdult = true;
        if (origHome != 0) {
            record.legacyOriginalHome = State::FormRawId{ origHome };
        }

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (const auto* existing = ws.actors.Read().Find(record.refId)) {
                record.legacyHomeMarker = existing->legacyHomeMarker;
                record.legacyCurrentHome = existing->legacyCurrentHome;
                record.addedKeywords = existing->addedKeywords;
                if (existing->legacyOriginalHome.has_value()) {
                    record.legacyOriginalHome = existing->legacyOriginalHome;
                }
            }
            ws.actors.Write().Upsert(std::move(record));
            if (npcFormID != kPlayerFormID && !ws.affection.Read().HasPermanent(State::ActorId{ npcFormID })) {
                ws.affection.Write().SetPermanent(State::ActorId{ npcFormID }, initialCandidateAffection);
            }
        });

        FactionReconciler::GetSingleton().ReconcileActor(npcFormID);
        MARAS_LOG_INFO("Registered NPC '{}' ({:08X}) [class={}, skill={}, temp={}]",
                       ActorSvc::GetName(npcFormID),
                       npcFormID,
                       Foundation::SocialClassToString(socialClass),
                       Foundation::SkillTypeToString(skillType),
                       Foundation::TemperamentToString(temperament));
        return true;
    }

    bool RelationshipUseCases::UnregisterNPC(RE::FormID npcFormID) {
        return UnregisterCandidate(npcFormID);
    }

    bool RelationshipUseCases::UnregisterCandidate(RE::FormID npcFormID) {
        if (npcFormID == 0 || npcFormID == kPlayerFormID) {
            return false;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap->actors->Find(State::RefId{ npcFormID })) {
            return false;
        }

        const bool wasMarriedToPlayer =
            snap->relationships->GetStatus(State::ActorId{ npcFormID }, State::ActorId::Player()) ==
            Foundation::RelationshipStatus::Married;

        const auto activePartners = GetAllPartners(npcFormID);

        State::WorldMutationResult mut;
        Domain::Relationships::CleanupAffairsOutcome affairCleanup;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            mut = ws.UnregisterActor(State::ActorId{ npcFormID });
            affairCleanup = Domain::Relationships::ApplyCleanupActorIntentsAndThirdPartyAffairs(
                ws, State::ActorId{ npcFormID }, /*eraseRecords=*/true);
        });

        BedOwnershipReconciler::GetSingleton().ReconcileStoppedSharingFromMutation(npcFormID, mut, "");
        ClearCurrentHome(npcFormID);
        BanterSceneCoordinator::GetSingleton().CleanupRetiredSceneMarkers(mut.retiredSceneMarkers);

        FactionReconciler::GetSingleton().ReconcileActor(npcFormID);
        PackageReconciler::GetSingleton().UnregisterActor(npcFormID);
        PackageReconciler::GetSingleton().ReconcileActor(npcFormID);

        if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(npcFormID)) {
            auto& fc = FormCache::GetSingleton();
            Utils::RemoveFromFaction(actor, fc.GetMarriageAskedFaction());
            Utils::RemoveFromFaction(actor, fc.GetCourtingFaction());
            Utils::RemoveFromFaction(actor, fc.GetMarriedFaction());
            Utils::RemoveFromFaction(actor, fc.GetPlayerFaction());
            Utils::RemoveFromFaction(actor, fc.GetPlayerBedOwnershipFaction());
        }

        ReconcileDirtyActorsAndChildren(mut, /*reconcilePackages=*/true);
        for (const auto pId : activePartners) {
            if (pId != 0 && pId != kPlayerFormID) {
                FactionReconciler::GetSingleton().ReconcileActor(pId);
            }
        }
        for (const auto affId : affairCleanup.affectedActors) {
            if (!affId.IsNull() && !affId.IsPlayer()) {
                FactionReconciler::GetSingleton().ReconcileActor(affId.raw());
            }
        }
        FactionReconciler::GetSingleton().ReconcilePlayer();
        GlobalVarReconciler::GetSingleton().ReconcileGlobals();
        if (wasMarriedToPlayer) {
            BuffReconciler::GetSingleton().ReconcilePlayerBuffs();
        }
        GetUiBridgeGateway().SendOverviewData();

        MARAS_LOG_INFO("Unregistered candidate NPC {:08X} ({})", npcFormID, ActorSvc::GetName(npcFormID));
        return true;
    }

    bool RelationshipUseCases::HandleNPCDeath(RE::FormID npcFormID) {
        if (npcFormID == 0 || npcFormID == kPlayerFormID) {
            return false;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        CachedActorMetadataProvider meta;
        meta.CaptureActors(snap, { npcFormID });

        const bool wasMarriedToPlayer =
            snap->relationships->GetStatus(State::ActorId{ npcFormID }, State::ActorId::Player()) ==
            Foundation::RelationshipStatus::Married;

        std::unordered_map<RE::FormID, State::ChildRecord> preChildren;
        if (snap->lineage) {
            for (const auto& [cid, rec] : snap->lineage->AllChildren()) {
                preChildren.emplace(cid.raw(), rec);
            }
        }

        State::WorldMutationResult mut;
        Domain::Relationships::CleanupAffairsOutcome cleanup;
        const float currentDay = GetCurrentGameDays();
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            mut = ws.RecordActorDeath(State::ActorId{ npcFormID }, currentDay, false, State::ActorId{}, meta);
            cleanup = Domain::Relationships::ApplyCleanupActorIntentsAndThirdPartyAffairs(
                ws, State::ActorId{ npcFormID }, /*eraseRecords=*/false);
        });

        BedOwnershipReconciler::GetSingleton().ReconcileStoppedSharingFromMutation(npcFormID, mut, "deceased");
        ClearCurrentHome(npcFormID);
        BanterSceneCoordinator::GetSingleton().CleanupRetiredSceneMarkers(mut.retiredSceneMarkers);

        ReconcileDirtyActorsAndChildren(mut, /*reconcilePackages=*/true);
        if (mut.changed && !preChildren.empty()) {
            const auto postSnap = State::WorldStore::GetSingleton().Snapshot();
            LineageUseCases::GetSingleton().HandleDeathCustodySideEffects(preChildren, *postSnap, npcFormID);
        }
        for (const auto affId : cleanup.affectedActors) {
            if (!affId.IsNull() && !affId.IsPlayer()) {
                FactionReconciler::GetSingleton().ReconcileActor(affId.raw());
            }
        }
        FactionReconciler::GetSingleton().ReconcileActor(npcFormID);
        PackageReconciler::GetSingleton().ReconcileActor(npcFormID);
        FactionReconciler::GetSingleton().ReconcilePlayer();
        GlobalVarReconciler::GetSingleton().ReconcileGlobals();
        if (wasMarriedToPlayer) {
            BuffReconciler::GetSingleton().ReconcilePlayerBuffs();
            GetUiBridgeGateway().OnHierarchyChanged();
        }
        GetUiBridgeGateway().SendOverviewData();
        MARAS_LOG_INFO("Handled death for NPC {:08X} ({})", npcFormID, ActorSvc::GetName(npcFormID));
        return mut.changed;
    }

    bool RelationshipUseCases::IsRegistered(RE::FormID npcFormID) const {
        if (npcFormID == 0) {
            return false;
        }
        return State::WorldStore::GetSingleton().Snapshot()->actors->IsRegisteredAdult(State::RefId{ npcFormID });
    }

    bool RelationshipUseCases::IsChildNPC(RE::FormID formID) const {
        if (formID == 0 || IsPlayerFormID(formID)) {
            return false;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (snap->lineage->FindChild(State::ChildId{ formID }) != nullptr) {
            return true;
        }
        if (const auto entry = SpatialWorldIndex::GetSingleton().FindByFormID(formID); entry.has_value()) {
            return entry->isChild;
        }
        if (auto* base = ActorSvc::GetActorBase(formID)) {
            if (auto* race = base->GetRace(); race && race->data.flags.any(RE::RACE_DATA::Flag::kChild)) {
                return true;
            }
        }
        return false;
    }

    // =========================================================================
    // Vanilla Scan & Original Relationship Status
    // =========================================================================

    void RelationshipUseCases::ScanExistingRelationships(bool isNewGame) {
        auto startTime = std::chrono::steady_clock::now();
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            MARAS_LOG_ERROR("ScanExistingRelationships: TESDataHandler is null");
            return;
        }

        const auto& relArray = dataHandler->GetFormArray<RE::BGSRelationship>();
        auto& idx = SpatialWorldIndex::GetSingleton();
        auto& fc = FormCache::GetSingleton();
        auto* marriedFaction = fc.GetMarriedFaction();
        auto* courtingFaction = fc.GetCourtingFaction();

        auto hasNonVanillaMarasRelationship = [&](RE::FormID actorID) -> bool {
            const auto snap = State::WorldStore::GetSingleton().Snapshot();
            for (const auto& [partnerId, st] : snap->relationships->GetActivePartners(State::ActorId{ actorID })) {
                if (st == Foundation::RelationshipStatus::Married ||
                    st == Foundation::RelationshipStatus::Engaged ||
                    st == Foundation::RelationshipStatus::Dating ||
                    st == Foundation::RelationshipStatus::Lover) {
                    const auto orig = snap->relationships->FindEdge(State::ActorId{ actorID }, partnerId);
                    if (!orig || orig->originalStatus == Foundation::RelationshipStatus::Unknown) {
                        return true;
                    }
                }
            }
            return false;
        };

        auto hasNonVanillaMainTierRelationship = [&](RE::FormID actorID) -> bool {
            const auto snap = State::WorldStore::GetSingleton().Snapshot();
            for (const auto& [partnerId, st] : snap->relationships->GetActivePartners(State::ActorId{ actorID })) {
                if (IsMainTier(st)) {
                    const auto orig = snap->relationships->FindEdge(State::ActorId{ actorID }, partnerId);
                    if (!orig || orig->originalStatus == Foundation::RelationshipStatus::Unknown) {
                        return true;
                    }
                }
            }
            return false;
        };

        auto statusPriority = [](Foundation::RelationshipStatus st) -> int {
            switch (st) {
                case Foundation::RelationshipStatus::Married:
                    return 3;
                case Foundation::RelationshipStatus::Dating:
                    return 2;
                case Foundation::RelationshipStatus::Lover:
                    return 1;
                default:
                    return 0;
            }
        };

        struct PlayerRelationshipAuditEntry {
            RE::FormID npcBaseID{ 0 };
            RE::FormID npcRefID{ 0 };
            bool isRef{ false };
            std::string npcName{};
            std::string pluginName{};
            Foundation::RelationshipStatus vanillaStatus{ Foundation::RelationshipStatus::Unknown };
            Foundation::RelationshipStatus targetMarasStatus{ Foundation::RelationshipStatus::Unknown };
            bool defaultChecked{ false };
        };

        std::vector<PlayerRelationshipAuditEntry> playerAuditEntries;
        std::vector<ScannedChildEntry> scannedChildren;
        std::size_t scannedCount = 0;
        std::size_t importedCount = 0;

        for (auto* rel : relArray) {
            if (!rel) {
                continue;
            }
            ++scannedCount;

            auto* npc1Base = rel->npc1;
            auto* npc2Base = rel->npc2;
            if (!npc1Base || !npc2Base) {
                continue;
            }

            const RE::FormID baseID1 = npc1Base->GetFormID();
            const RE::FormID baseID2 = npc2Base->GetFormID();
            const bool isPlayerPair = IsPlayerFormID(baseID1) || IsPlayerFormID(baseID2);

            const auto entry1 = idx.FindByFormID(baseID1);
            const auto entry2 = idx.FindByFormID(baseID2);

            const bool isChild1 = entry1 ? entry1->isChild
                                         : (npc1Base->GetRace() && npc1Base->GetRace()->data.flags.any(RE::RACE_DATA::Flag::kChild));
            const bool isChild2 = entry2 ? entry2->isChild
                                         : (npc2Base->GetRace() && npc2Base->GetRace()->data.flags.any(RE::RACE_DATA::Flag::kChild));

            if (isChild1 || isChild2) {
                if (isNewGame && isPlayerPair) {
                    continue;
                }
                if (isChild1 != isChild2 && rel->assocType && rel->assocType->GetFormID() == kParentChildAssocFormID) {
                    const RE::FormID cBaseID = (isChild1 ? npc1Base : npc2Base)->GetFormID();
                    const RE::FormID pBaseID = (isChild1 ? npc2Base : npc1Base)->GetFormID();
                    auto it = std::find_if(scannedChildren.begin(), scannedChildren.end(),
                                           [cBaseID](const ScannedChildEntry& e) { return e.childBaseID == cBaseID; });
                    if (it == scannedChildren.end()) {
                        scannedChildren.push_back(ScannedChildEntry{ cBaseID, pBaseID, 0, false });
                    } else if (it->parentABaseID != pBaseID && it->parentBBaseID == 0) {
                        it->parentBBaseID = pBaseID;
                    }
                }
                continue;
            }

            Foundation::RelationshipStatus vanillaStatus = Foundation::RelationshipStatus::Unknown;
            if (rel->assocType) {
                const RE::FormID assocID = rel->assocType->GetFormID();
                if (assocID == kCourtingAssocFormID) {
                    vanillaStatus = Foundation::RelationshipStatus::Dating;
                } else if (assocID == kSpouseAssocFormID) {
                    vanillaStatus = Foundation::RelationshipStatus::Married;
                }
            }
            if (vanillaStatus == Foundation::RelationshipStatus::Unknown &&
                rel->level.get() == RE::BGSRelationship::RELATIONSHIP_LEVEL::kLover) {
                vanillaStatus = Foundation::RelationshipStatus::Lover;
            }
            if (vanillaStatus == Foundation::RelationshipStatus::Unknown) {
                continue;
            }

            if (isPlayerPair) {
                auto* npcBase = IsPlayerFormID(baseID1) ? npc2Base : npc1Base;
                const RE::FormID npcBaseID = npcBase->GetFormID();
                if (IsPlayerFormID(npcBaseID)) {
                    continue;
                }
                const auto& npcEntry = IsPlayerFormID(baseID1) ? entry2 : entry1;
                const RE::FormID npcRefID = npcEntry ? npcEntry->refFormID : idx.GetRefID(npcBaseID);
                const bool isNpcRef = (npcRefID != 0 && npcRefID != npcBaseID);

                if (isNpcRef) {
                    const auto existingStatus = GetRelationshipStatus(kPlayerFormID, npcRefID);
                    if (existingStatus != Foundation::RelationshipStatus::Candidate &&
                        existingStatus != Foundation::RelationshipStatus::Unknown) {
                        continue;
                    }
                }

                std::string npcName = isNpcRef ? ActorSvc::GetName(npcRefID) : std::string{};
                if (npcName.empty() && npcBase->GetName() && npcBase->GetName()[0] != '\0') {
                    npcName = npcBase->GetName();
                }
                if (npcName.empty()) {
                    npcName = std::format("NPC ({:08X})", npcBaseID);
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
                    const bool isRuntimeDynamicRel = ((rel->GetFormID() >> 24) == 0xFF);
                    defaultChecked = hasActiveVanillaFaction || isRuntimeDynamicRel;
                }

                auto existingIt = std::find_if(playerAuditEntries.begin(), playerAuditEntries.end(),
                                               [npcBaseID](const PlayerRelationshipAuditEntry& e) {
                                                   return e.npcBaseID == npcBaseID;
                                               });
                if (existingIt == playerAuditEntries.end()) {
                    playerAuditEntries.push_back(PlayerRelationshipAuditEntry{
                        npcBaseID,
                        isNpcRef ? npcRefID : 0,
                        isNpcRef,
                        std::move(npcName),
                        std::move(pluginName),
                        vanillaStatus,
                        vanillaStatus,
                        defaultChecked
                    });
                } else if (statusPriority(vanillaStatus) > statusPriority(existingIt->vanillaStatus)) {
                    existingIt->vanillaStatus = vanillaStatus;
                    existingIt->targetMarasStatus = vanillaStatus;
                    existingIt->defaultChecked = existingIt->defaultChecked || defaultChecked;
                }
                continue;
            }

            const RE::FormID refID1 = entry1 ? entry1->refFormID : idx.GetRefID(baseID1);
            const RE::FormID refID2 = entry2 ? entry2->refFormID : idx.GetRefID(baseID2);
            const bool isRef1 = (refID1 != 0 && refID1 != baseID1);
            const bool isRef2 = (refID2 != 0 && refID2 != baseID2);

            const bool hasNonVanillaHistory =
                hasNonVanillaMarasRelationship(isRef1 ? refID1 : baseID1) ||
                hasNonVanillaMarasRelationship(isRef2 ? refID2 : baseID2);

            Foundation::RelationshipStatus targetStatus = Foundation::RelationshipStatus::Unknown;
            if (!hasNonVanillaHistory) {
                targetStatus = vanillaStatus;
            } else if (vanillaStatus == Foundation::RelationshipStatus::Dating) {
                targetStatus = Foundation::RelationshipStatus::ExDating;
            } else if (vanillaStatus == Foundation::RelationshipStatus::Married) {
                targetStatus = Foundation::RelationshipStatus::Divorced;
            } else if (vanillaStatus == Foundation::RelationshipStatus::Lover) {
                const bool committedElsewhere =
                    hasNonVanillaMainTierRelationship(isRef1 ? refID1 : baseID1) ||
                    hasNonVanillaMainTierRelationship(isRef2 ? refID2 : baseID2);
                targetStatus = committedElsewhere ? Foundation::RelationshipStatus::ExLover
                                                  : Foundation::RelationshipStatus::Lover;
            }

            if (!isRef1 || !isRef2) {
                const RE::FormID r1 = isRef1 ? refID1 : 0;
                const RE::FormID r2 = isRef2 ? refID2 : 0;
                State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                    if (!isRef1) {
                        ws.actors.Write().AddPendingRelationship(
                            State::BaseId{ baseID1 },
                            State::PendingRelationshipEntry{
                                State::BaseId{ baseID2 }, State::RefId{ r2 }, targetStatus, vanillaStatus });
                    }
                    if (!isRef2) {
                        ws.actors.Write().AddPendingRelationship(
                            State::BaseId{ baseID2 },
                            State::PendingRelationshipEntry{
                                State::BaseId{ baseID1 }, State::RefId{ r1 }, targetStatus, vanillaStatus });
                    }
                });
                continue;
            }

            EnsureRegistered(refID1);
            EnsureRegistered(refID2);
            SetOriginalRelationshipStatus(refID1, refID2, vanillaStatus);

            const auto currentStatus = GetRelationshipStatus(refID1, refID2);
            if ((currentStatus == Foundation::RelationshipStatus::Candidate ||
                 currentStatus == Foundation::RelationshipStatus::Unknown) &&
                targetStatus != Foundation::RelationshipStatus::Unknown) {
                if (ApplyImportedVanillaStatus(refID1, refID2, vanillaStatus, targetStatus)) {
                    const RE::FormID defaultInit = DetermineDefaultInitiator(refID1, refID2);
                    State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                        auto& edge = ws.relationships.Write().GetOrCreateEdge(
                            State::ActorId{ refID1 }, State::ActorId{ refID2 });
                        for (auto& hist : edge.statusHistory) {
                            hist.timestamp = -1.0f;
                            hist.fromStatus = Foundation::RelationshipStatus::Unknown;
                            hist.initiator = State::ActorId{ defaultInit };
                        }
                    });
                    ++importedCount;
                }
            }
        }

        // Delegate child-parent scan entries if LineageUseCases registered a handler
        std::size_t childImportedCount = 0;
        if (!scannedChildren.empty()) {
            VanillaChildScanHandler childHandler;
            {
                std::shared_lock lock(childScanMutex_);
                childHandler = vanillaChildScanHandler_;
            }
            if (childHandler) {
                childImportedCount = childHandler(scannedChildren, isNewGame);
            }
        }

        if (!playerAuditEntries.empty()) {
            std::vector<ModalChecklistItem> items;
            items.reserve(playerAuditEntries.size());

            const std::vector<ModalSelectOption> statusSelectOptions = {
                { "dating", Tr("common.status_dating") },
                { "lover", Tr("common.status_lover") },
                { "engaged", Tr("common.status_engaged") },
                { "married", Tr("common.status_married") }
            };

            for (std::size_t i = 0; i < playerAuditEntries.size(); ++i) {
                const auto& entry = playerAuditEntries[i];
                ModalChecklistItem item;
                item.id = std::format("rel_{}", i);
                item.label = entry.npcName;
                item.subtitle = TrFmt("modal.rel_audit.item_source", { { "plugin", entry.pluginName } });
                if (entry.vanillaStatus == Foundation::RelationshipStatus::Married) {
                    item.badgeText = Tr("modal.rel_audit.badge_spouse");
                    item.badgeTone = "success";
                } else if (entry.vanillaStatus == Foundation::RelationshipStatus::Dating) {
                    item.badgeText = Tr("modal.rel_audit.badge_courting");
                    item.badgeTone = "accent";
                } else {
                    item.badgeText = Tr("modal.rel_audit.badge_lover");
                    item.badgeTone = "warning";
                }
                item.checked = entry.defaultChecked;
                item.selectKey = "status";
                item.selectLabel = Tr("modal.rel_audit.select_label");
                item.selectValue = std::string(Foundation::RelationshipStatusToString(entry.targetMarasStatus));
                item.selectOptions = statusSelectOptions;
                items.push_back(std::move(item));
            }

            ModalRequest req;
            req.modalType = "relationship_audit";
            req.title = Tr("modal.rel_audit.title");
            req.subtitle = Tr("modal.rel_audit.subtitle");
            req.message = Tr("modal.rel_audit.message");
            req.tone = ModalTone::Default;
            req.size = ModalSize::Lg;
            req.defaultCancelButtonIndex = 1;
            req.controls.push_back(ModalControl::MakeChecklist(
                "detected_player_rels",
                Tr("modal.rel_audit.checklist_label"),
                std::move(items),
                true,
                Tr("modal.rel_audit.checklist_desc")));
            req.buttons.push_back(ModalButton::Make("import_selected", Tr("modal.rel_audit.btn_import"), ModalButtonVariant::Primary));
            req.buttons.push_back(ModalButton::Make("skip_all", Tr("modal.rel_audit.btn_skip"), ModalButtonVariant::Secondary));

            MARAS_LOG_INFO("ScanExistingRelationships: Queuing Player Relationship Audit Modal ({} entries, isNewGame={})",
                           playerAuditEntries.size(), isNewGame);

            const SessionEpoch expectedEpoch = GetSessionEpoch();
            GetModalGateway().ShowModal(
                std::move(req),
                [this, expectedEpoch, entries = std::move(playerAuditEntries)](const ModalResponse& resp) {
                    if (!IsSessionEpochCurrent(expectedEpoch)) {
                        MARAS_LOG_DEBUG("ScanExistingRelationships (Audit Modal): Ignored stale modal callback (epoch={})",
                                        expectedEpoch);
                        return;
                    }
                    if (resp.aborted || (resp.buttonIndex != 0 && resp.buttonId != "import_selected")) {
                        MARAS_LOG_INFO("ScanExistingRelationships (Audit Modal): Player skipped importing {} entries",
                                       entries.size());
                        return;
                    }

                    std::lock_guard papyrusLock(GetPapyrusMutex());
                    ScopedMemorySuppression suppressAuditMemories;
                    auto& spatial = SpatialWorldIndex::GetSingleton();
                    std::size_t auditImported = 0;
                    std::size_t auditDeferred = 0;

                    for (std::size_t i = 0; i < entries.size(); ++i) {
                        const std::string itemId = std::format("rel_{}", i);
                        if (!resp.IsItemChecked("detected_player_rels", itemId, false)) {
                            continue;
                        }
                        const auto& entry = entries[i];
                        const std::string defaultStatusStr =
                            std::string(Foundation::RelationshipStatusToString(entry.targetMarasStatus));
                        const std::string chosenStr =
                            resp.GetItemSelectValue("detected_player_rels", itemId, defaultStatusStr);
                        Foundation::RelationshipStatus chosenStatus = Foundation::StringToRelationshipStatus(chosenStr);
                        if (chosenStatus == Foundation::RelationshipStatus::Unknown ||
                            chosenStatus == Foundation::RelationshipStatus::Candidate) {
                            chosenStatus = entry.targetMarasStatus;
                        }

                        const RE::FormID npcRefID = entry.isRef ? entry.npcRefID : spatial.GetRefID(entry.npcBaseID);
                        const bool hasValidRef = (npcRefID != 0 && npcRefID != entry.npcBaseID);
                        if (hasValidRef) {
                            EnsureRegistered(kPlayerFormID);
                            EnsureRegistered(npcRefID);
                            SetOriginalRelationshipStatus(kPlayerFormID, npcRefID, entry.vanillaStatus);
                            const auto curSt = GetRelationshipStatus(kPlayerFormID, npcRefID);
                            if (curSt == Foundation::RelationshipStatus::Candidate ||
                                curSt == Foundation::RelationshipStatus::Unknown) {
                                ApplyImportedVanillaStatus(kPlayerFormID, npcRefID, entry.vanillaStatus, chosenStatus);
                                const RE::FormID defaultInit = DetermineDefaultInitiator(kPlayerFormID, npcRefID);
                                State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                                    auto& edge = ws.relationships.Write().GetOrCreateEdge(
                                        State::ActorId::Player(), State::ActorId{ npcRefID });
                                    for (auto& hist : edge.statusHistory) {
                                        hist.timestamp = -1.0f;
                                        hist.fromStatus = Foundation::RelationshipStatus::Unknown;
                                        hist.initiator = State::ActorId{ defaultInit };
                                    }
                                });
                                ++auditImported;
                            }
                        } else {
                            State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
                                ws.actors.Write().AddPendingRelationship(
                                    State::BaseId{ entry.npcBaseID },
                                    State::PendingRelationshipEntry{
                                        State::BaseId{ kPlayerBaseFormID },
                                        State::RefId{ kPlayerFormID },
                                        chosenStatus,
                                        entry.vanillaStatus });
                            });
                            ++auditDeferred;
                        }
                    }

                    if (auditImported > 0) {
                        SyncAllMarriedCouplesChildCustody();
                        GlobalVarReconciler::GetSingleton().ReconcileGlobals();
                        GetUiBridgeGateway().SendOverviewData();
                    }
                    MARAS_LOG_INFO("ScanExistingRelationships (Audit Modal): Imported {}, deferred {}",
                                   auditImported, auditDeferred);
                });
        }

        const auto endTime = std::chrono::steady_clock::now();
        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
        MARAS_LOG_INFO(
            "ScanExistingRelationships: Completed in {} ms (scanned={}, importedAdults={}, importedChildren={}, playerAuditQueued={})",
            elapsedMs, scannedCount, importedCount, childImportedCount, playerAuditEntries.size());
    }

    bool RelationshipUseCases::ApplyImportedVanillaStatus(RE::FormID actorA,
                                                          RE::FormID actorB,
                                                          Foundation::RelationshipStatus vanillaStatus,
                                                          Foundation::RelationshipStatus targetStatus) {
        if (actorA == 0 || actorB == 0 || actorA == actorB) {
            return false;
        }
        if (!EnsureRegistered(actorA) || !EnsureRegistered(actorB)) {
            return false;
        }

        if (targetStatus == vanillaStatus) {
            return PromoteToStatusWith(actorA, actorB, targetStatus, /*sendEvent=*/false, /*breakLovers=*/true);
        }

        // Breakup imports (e.g. Married -> Divorced) must first mirror the pre-existing vanilla bond.
        if (IsBreakupStatus(targetStatus) &&
            (vanillaStatus == Foundation::RelationshipStatus::Married ||
             vanillaStatus == Foundation::RelationshipStatus::Dating ||
             vanillaStatus == Foundation::RelationshipStatus::Lover)) {
            if (!PromoteToStatusWith(actorA, actorB, vanillaStatus, /*sendEvent=*/false, /*breakLovers=*/false)) {
                return false;
            }
            return PromoteToStatusWith(actorA, actorB, targetStatus, /*sendEvent=*/false, /*breakLovers=*/false);
        }

        return PromoteToStatusWith(actorA, actorB, targetStatus, /*sendEvent=*/false, /*breakLovers=*/true);
    }

    bool RelationshipUseCases::HasMarasMaritalHistory(RE::FormID npcFormID) const {
        if (npcFormID == 0) {
            return false;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const State::ActorId actor{ npcFormID };
        for (const auto& key : snap->relationships->GetAdjacentPairs(actor)) {
            const auto* edge = snap->relationships->FindEdge(key);
            if (!edge) {
                continue;
            }
            if (edge->status == Foundation::RelationshipStatus::Married ||
                edge->status == Foundation::RelationshipStatus::Divorced) {
                return true;
            }
            for (const auto& hist : edge->statusHistory) {
                if (hist.toStatus == Foundation::RelationshipStatus::Married) {
                    return true;
                }
            }
        }
        return false;
    }

    Foundation::RelationshipStatus RelationshipUseCases::GetOriginalRelationshipStatus(
        RE::FormID actorA, RE::FormID actorB) const {
        if (actorA == 0 || actorB == 0) {
            return Foundation::RelationshipStatus::Unknown;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* edge = snap->relationships->FindEdge(State::ActorId{ actorA }, State::ActorId{ actorB });
        return edge ? edge->originalStatus : Foundation::RelationshipStatus::Unknown;
    }

    bool RelationshipUseCases::SetOriginalRelationshipStatus(
        RE::FormID actorA, RE::FormID actorB, Foundation::RelationshipStatus status) {
        if (actorA == 0 || actorB == 0 || actorA == actorB) {
            return false;
        }
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            auto& edge = ws.relationships.Write().GetOrCreateEdge(State::ActorId{ actorA }, State::ActorId{ actorB });
            edge.originalStatus = status;
        });
        return true;
    }

    // =========================================================================
    // Status Queries & Availability
    // =========================================================================

    bool RelationshipUseCases::IsCandidate(RE::FormID npcFormID, RE::FormID targetFormID) const {
        return GetRelationshipStatus(npcFormID, targetFormID) == Foundation::RelationshipStatus::Candidate;
    }

    bool RelationshipUseCases::IsEngaged(RE::FormID npcFormID, RE::FormID targetFormID) const {
        return GetRelationshipStatus(npcFormID, targetFormID) == Foundation::RelationshipStatus::Engaged;
    }

    bool RelationshipUseCases::IsMarried(RE::FormID npcFormID, RE::FormID targetFormID) const {
        return GetRelationshipStatus(npcFormID, targetFormID) == Foundation::RelationshipStatus::Married;
    }

    bool RelationshipUseCases::IsDivorced(RE::FormID npcFormID, RE::FormID targetFormID) const {
        return GetRelationshipStatus(npcFormID, targetFormID) == Foundation::RelationshipStatus::Divorced;
    }

    bool RelationshipUseCases::IsMostRecentRelationshipDivorced(RE::FormID actorA, RE::FormID actorB) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* edge = snap->relationships->FindEdge(State::ActorId{ actorA }, State::ActorId{ actorB });
        if (!edge || edge->status != Foundation::RelationshipStatus::Divorced) {
            return false;
        }
        return true;
    }

    bool RelationshipUseCases::IsJilted(RE::FormID npcFormID, RE::FormID targetFormID) const {
        return GetRelationshipStatus(npcFormID, targetFormID) == Foundation::RelationshipStatus::Jilted;
    }

    bool RelationshipUseCases::IsDating(RE::FormID npcFormID, RE::FormID targetFormID) const {
        return GetRelationshipStatus(npcFormID, targetFormID) == Foundation::RelationshipStatus::Dating;
    }

    bool RelationshipUseCases::IsLover(RE::FormID npcFormID, RE::FormID targetFormID) const {
        return GetRelationshipStatus(npcFormID, targetFormID) == Foundation::RelationshipStatus::Lover;
    }

    bool RelationshipUseCases::IsExDating(RE::FormID npcFormID, RE::FormID targetFormID) const {
        return GetRelationshipStatus(npcFormID, targetFormID) == Foundation::RelationshipStatus::ExDating;
    }

    bool RelationshipUseCases::IsExLover(RE::FormID npcFormID, RE::FormID targetFormID) const {
        return GetRelationshipStatus(npcFormID, targetFormID) == Foundation::RelationshipStatus::ExLover;
    }

    bool RelationshipUseCases::IsPlayerActiveMainPartner(RE::FormID npcFormID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        return Domain::Relationships::IsPlayerActiveMainPartner(*snap, State::ActorId{ npcFormID });
    }

    bool RelationshipUseCases::IsAvailableForDating(RE::FormID npcFormID) const {
        return IsAvailableForStatus(npcFormID, Foundation::RelationshipStatus::Dating);
    }

    bool RelationshipUseCases::IsAvailableForDating(RE::FormID npcFormID, RE::FormID targetFormID) const {
        return CanFormRelationship(npcFormID, targetFormID, Foundation::RelationshipStatus::Dating);
    }

    bool RelationshipUseCases::IsAvailableForEngaged(RE::FormID npcFormID) const {
        return IsAvailableForStatus(npcFormID, Foundation::RelationshipStatus::Engaged);
    }

    bool RelationshipUseCases::IsAvailableForEngaged(RE::FormID npcFormID, RE::FormID targetFormID) const {
        return CanFormRelationship(npcFormID, targetFormID, Foundation::RelationshipStatus::Engaged);
    }

    bool RelationshipUseCases::IsAvailableForMarried(RE::FormID npcFormID) const {
        return IsAvailableForStatus(npcFormID, Foundation::RelationshipStatus::Married);
    }

    bool RelationshipUseCases::IsAvailableForMarried(RE::FormID npcFormID, RE::FormID targetFormID) const {
        return CanFormRelationship(npcFormID, targetFormID, Foundation::RelationshipStatus::Married);
    }

    bool RelationshipUseCases::IsAvailableForLover(RE::FormID npcFormID) const {
        return IsAvailableForStatus(npcFormID, Foundation::RelationshipStatus::Lover);
    }

    bool RelationshipUseCases::IsAvailableForLover(RE::FormID npcFormID, RE::FormID targetFormID) const {
        return CanFormRelationship(npcFormID, targetFormID, Foundation::RelationshipStatus::Lover);
    }

    bool RelationshipUseCases::IsAvailableForDivorced(RE::FormID npcFormID) const {
        return IsAvailableForStatus(npcFormID, Foundation::RelationshipStatus::Divorced);
    }

    bool RelationshipUseCases::IsAvailableForDivorced(RE::FormID npcFormID, RE::FormID targetFormID) const {
        return CanFormRelationship(npcFormID, targetFormID, Foundation::RelationshipStatus::Divorced);
    }

    bool RelationshipUseCases::IsAvailableForJilted(RE::FormID npcFormID) const {
        return IsAvailableForStatus(npcFormID, Foundation::RelationshipStatus::Jilted);
    }

    bool RelationshipUseCases::IsAvailableForJilted(RE::FormID npcFormID, RE::FormID targetFormID) const {
        return CanFormRelationship(npcFormID, targetFormID, Foundation::RelationshipStatus::Jilted);
    }

    bool RelationshipUseCases::IsAvailableForExDating(RE::FormID npcFormID) const {
        return IsAvailableForStatus(npcFormID, Foundation::RelationshipStatus::ExDating);
    }

    bool RelationshipUseCases::IsAvailableForExDating(RE::FormID npcFormID, RE::FormID targetFormID) const {
        return CanFormRelationship(npcFormID, targetFormID, Foundation::RelationshipStatus::ExDating);
    }

    bool RelationshipUseCases::IsAvailableForExLover(RE::FormID npcFormID) const {
        return IsAvailableForStatus(npcFormID, Foundation::RelationshipStatus::ExLover);
    }

    bool RelationshipUseCases::IsAvailableForExLover(RE::FormID npcFormID, RE::FormID targetFormID) const {
        return CanFormRelationship(npcFormID, targetFormID, Foundation::RelationshipStatus::ExLover);
    }

    bool RelationshipUseCases::IsAvailableForBreakup(RE::FormID npcFormID) const {
        return IsAvailableForBreakup(npcFormID, 0);
    }

    bool RelationshipUseCases::IsAvailableForBreakup(RE::FormID npcFormID, RE::FormID targetFormID) const {
        if (targetFormID == 0) {
            const auto snap = State::WorldStore::GetSingleton().Snapshot();
            for (const auto& key : snap->relationships->GetAdjacentPairs(State::ActorId{ npcFormID })) {
                const auto* edge = snap->relationships->FindEdge(key);
                if (edge && IsActivePartnerStatus(edge->status)) {
                    return true;
                }
            }
            return false;
        }
        const auto st = GetRelationshipStatus(npcFormID, targetFormID);
        return IsActivePartnerStatus(st);
    }

    bool RelationshipUseCases::IsAvailableForBreakupDating(RE::FormID npcFormID) const {
        return IsAvailableForExDating(npcFormID);
    }

    bool RelationshipUseCases::IsAvailableForBreakupDating(RE::FormID npcFormID, RE::FormID targetFormID) const {
        return targetFormID == 0 ? IsAvailableForExDating(npcFormID)
                                 : IsDating(npcFormID, targetFormID);
    }

    bool RelationshipUseCases::IsAvailableForBreakupLover(RE::FormID npcFormID) const {
        return IsAvailableForExLover(npcFormID);
    }

    bool RelationshipUseCases::IsAvailableForBreakupLover(RE::FormID npcFormID, RE::FormID targetFormID) const {
        return targetFormID == 0 ? IsAvailableForExLover(npcFormID)
                                 : IsLover(npcFormID, targetFormID);
    }

    bool RelationshipUseCases::IsAvailableForOpenRelationship(RE::FormID npcFormID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        for (const auto& key : snap->relationships->GetAdjacentPairs(State::ActorId{ npcFormID })) {
            const auto* edge = snap->relationships->FindEdge(key);
            if (edge && IsMainTier(edge->status) && !edge->isOpen) {
                return true;
            }
        }
        return false;
    }

    bool RelationshipUseCases::IsAvailableForOpenRelationship(RE::FormID npcFormID, RE::FormID targetFormID) const {
        return targetFormID == 0 ? IsAvailableForOpenRelationship(npcFormID)
                                 : CanOpenRelationship(npcFormID, targetFormID);
    }

    bool RelationshipUseCases::IsAvailableForCloseRelationship(RE::FormID npcFormID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        for (const auto& key : snap->relationships->GetAdjacentPairs(State::ActorId{ npcFormID })) {
            const auto* edge = snap->relationships->FindEdge(key);
            if (edge && IsMainTier(edge->status) && edge->isOpen) {
                return true;
            }
        }
        return false;
    }

    bool RelationshipUseCases::IsAvailableForCloseRelationship(RE::FormID npcFormID, RE::FormID targetFormID) const {
        return targetFormID == 0 ? IsAvailableForCloseRelationship(npcFormID)
                                 : CanCloseRelationship(npcFormID, targetFormID);
    }

    bool RelationshipUseCases::IsAvailableForStatus(RE::FormID npcFormID, Foundation::RelationshipStatus status) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        return Domain::Relationships::IsAvailableForStatus(*snap, State::ActorId{ npcFormID }, status);
    }

    bool RelationshipUseCases::IsAvailableForStatus(RE::FormID npcFormID, std::string_view statusName) const {
        if (Foundation::StringUtils::CaseInsensitiveEquals(statusName, "breakup")) {
            return IsAvailableForBreakup(npcFormID, 0);
        }
        if (Foundation::StringUtils::CaseInsensitiveEquals(statusName, "open_relationship") ||
            Foundation::StringUtils::CaseInsensitiveEquals(statusName, "open")) {
            return IsAvailableForOpenRelationship(npcFormID);
        }
        if (Foundation::StringUtils::CaseInsensitiveEquals(statusName, "close_relationship") ||
            Foundation::StringUtils::CaseInsensitiveEquals(statusName, "close")) {
            return IsAvailableForCloseRelationship(npcFormID);
        }
        return IsAvailableForStatus(npcFormID, Foundation::StringToRelationshipStatus(statusName));
    }

    bool RelationshipUseCases::IsAvailableForStatus(RE::FormID npcFormID,
                                                    RE::FormID targetFormID,
                                                    Foundation::RelationshipStatus status) const {
        if (targetFormID == 0) {
            return IsAvailableForStatus(npcFormID, status);
        }
        return CanFormRelationship(npcFormID, targetFormID, status, nullptr);
    }

    bool RelationshipUseCases::IsAvailableForStatus(RE::FormID npcFormID,
                                                    RE::FormID targetFormID,
                                                    std::string_view statusName) const {
        if (targetFormID == 0) {
            return IsAvailableForStatus(npcFormID, statusName);
        }
        if (Foundation::StringUtils::CaseInsensitiveEquals(statusName, "breakup")) {
            return IsAvailableForBreakup(npcFormID, targetFormID);
        }
        if (Foundation::StringUtils::CaseInsensitiveEquals(statusName, "open_relationship") ||
            Foundation::StringUtils::CaseInsensitiveEquals(statusName, "open")) {
            return CanOpenRelationship(npcFormID, targetFormID);
        }
        if (Foundation::StringUtils::CaseInsensitiveEquals(statusName, "close_relationship") ||
            Foundation::StringUtils::CaseInsensitiveEquals(statusName, "close")) {
            return CanCloseRelationship(npcFormID, targetFormID);
        }
        return CanFormRelationship(npcFormID, targetFormID, Foundation::StringToRelationshipStatus(statusName));
    }

    bool RelationshipUseCases::AreFamilyMembers(RE::FormID actorA,
                                                RE::FormID actorB,
                                                std::string* outReason) const {
        if (outReason) {
            outReason->clear();
        }
        if (actorA == 0 || actorB == 0 || actorA == actorB) {
            return false;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        CachedActorMetadataProvider meta;
        meta.CaptureActors(snap, { actorA, actorB });
        if (Domain::Relationships::AreMarasFamilyMembers(*snap, meta, State::ActorId{ actorA }, State::ActorId{ actorB }, outReason)) {
            return true;
        }
        bool engineFamily = false;
        std::string engineReason;
        CheckVanillaEngineFamily(actorA, actorB, engineFamily, engineReason);
        if (engineFamily) {
            if (outReason) {
                *outReason = std::move(engineReason);
            }
            return true;
        }
        return false;
    }

    bool RelationshipUseCases::CanFormRelationship(RE::FormID actorA,
                                                   RE::FormID actorB,
                                                   Foundation::RelationshipStatus status,
                                                   std::string* outReason) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto data = CaptureRelationshipCheckData(snap, *this, actorA, actorB);
        return Domain::Relationships::CanFormRelationship(
            *snap, data.meta, State::ActorId{ actorA }, State::ActorId{ actorB }, status, data.ctx, outReason);
    }

    bool RelationshipUseCases::CanFormRelationship(RE::FormID actorA,
                                                   RE::FormID actorB,
                                                   std::string_view statusName,
                                                   std::string* outReason) const {
        if (Foundation::StringUtils::CaseInsensitiveEquals(statusName, "breakup")) {
            return IsAvailableForBreakup(actorA, actorB);
        }
        if (Foundation::StringUtils::CaseInsensitiveEquals(statusName, "open_relationship") ||
            Foundation::StringUtils::CaseInsensitiveEquals(statusName, "open")) {
            return CanOpenRelationship(actorA, actorB);
        }
        if (Foundation::StringUtils::CaseInsensitiveEquals(statusName, "close_relationship") ||
            Foundation::StringUtils::CaseInsensitiveEquals(statusName, "close")) {
            return CanCloseRelationship(actorA, actorB);
        }
        return CanFormRelationship(actorA, actorB, Foundation::StringToRelationshipStatus(statusName), outReason);
    }

    bool RelationshipUseCases::CanOpenRelationship(RE::FormID actorA, RE::FormID actorB) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        return Domain::Relationships::CanOpenRelationship(*snap, State::ActorId{ actorA }, State::ActorId{ actorB });
    }

    bool RelationshipUseCases::CanCloseRelationship(RE::FormID actorA, RE::FormID actorB) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        return Domain::Relationships::CanCloseRelationship(*snap, State::ActorId{ actorA }, State::ActorId{ actorB });
    }

    // =========================================================================
    // Status Transitions & Post-Transition Engine Reconciliation
    // =========================================================================

    RE::FormID RelationshipUseCases::DetermineDefaultInitiator(RE::FormID actorA, RE::FormID actorB) {
        if (IsPlayerFormID(actorA) || IsPlayerFormID(actorB)) {
            return kPlayerFormID;
        }
        return (actorA <= actorB) ? actorA : actorB;
    }

    void RelationshipUseCases::ReconcileBreakupOrDivorceEngineState(RE::FormID actorA,
                                                                     RE::FormID actorB,
                                                                     Foundation::RelationshipStatus oldStatus,
                                                                     Foundation::RelationshipStatus newStatus,
                                                                     const State::WorldMutationResult& mut) {
        const bool isPlayerPair = (actorA == kPlayerFormID || actorB == kPlayerFormID);
        if (isPlayerPair) {
            const RE::FormID npcID = (actorA == kPlayerFormID) ? actorB : actorA;
            if (oldStatus == Foundation::RelationshipStatus::Married ||
                newStatus == Foundation::RelationshipStatus::Divorced ||
                newStatus == Foundation::RelationshipStatus::Jilted ||
                IsBreakupStatus(newStatus) ||
                newStatus == Foundation::RelationshipStatus::Candidate) {
                const std::string_view stopReason =
                    (newStatus == Foundation::RelationshipStatus::Divorced) ? "divorce" : "";
                BedOwnershipReconciler::GetSingleton().ReconcileStoppedSharingFromMutation(
                    npcID, mut, stopReason);
                ClearCurrentHome(npcID);
                PackageReconciler::GetSingleton().ReconcileActor(npcID);
            }
            if (oldStatus == Foundation::RelationshipStatus::Married) {
                OnSpouseRemovedFromHierarchy(npcID);
            }
        } else {
            PackageReconciler::GetSingleton().ReconcileActor(actorA);
            PackageReconciler::GetSingleton().ReconcileActor(actorB);
        }
    }

    bool RelationshipUseCases::ChangeStatusCommon(RE::FormID npcFormID,
                                                  RE::FormID targetFormID,
                                                  Foundation::RelationshipStatus status,
                                                  RE::FormID explicitInitiatorID,
                                                  bool sendEvent,
                                                  bool breakLovers,
                                                  bool partnerCheatedOnPlayer) {
        if (npcFormID == 0 || targetFormID == 0 || npcFormID == targetFormID) {
            MARAS_LOG_WARN("ChangeStatusCommon: Invalid actor arguments (npc={:08X}, target={:08X})",
                           npcFormID, targetFormID);
            return false;
        }
        if (status == Foundation::RelationshipStatus::Unknown) {
            return false;
        }

        if (status != Foundation::RelationshipStatus::Candidate && !IsBreakupStatus(status)) {
            EnsureRegistered(npcFormID);
            EnsureRegistered(targetFormID);
        }

        const auto preSnap = State::WorldStore::GetSingleton().Snapshot();
        auto checkData = CaptureRelationshipCheckData(preSnap, *this, npcFormID, targetFormID);
        checkData.ctx.partnerCheatedOnPlayer = partnerCheatedOnPlayer;
        std::unordered_map<RE::FormID, State::ChildRecord> preChildren;
        if (preSnap->lineage &&
            (status == Foundation::RelationshipStatus::Married ||
             IsBreakupStatus(status) ||
             status == Foundation::RelationshipStatus::Candidate)) {
            for (const auto& [cid, rec] : preSnap->lineage->AllChildren()) {
                preChildren.emplace(cid.raw(), rec);
            }
        }
        const float currentGameDay = GetCurrentGameDays();
        const RE::FormID initiator = (explicitInitiatorID != 0)
                                         ? explicitInitiatorID
                                         : DetermineDefaultInitiator(npcFormID, targetFormID);

        const bool isPlayerPair = (npcFormID == kPlayerFormID || targetFormID == kPlayerFormID);
        const RE::FormID playerPartnerID = isPlayerPair
                                               ? ((npcFormID == kPlayerFormID) ? targetFormID : npcFormID)
                                               : 0;

        Foundation::DeterministicRng rng(
            static_cast<std::uint32_t>(npcFormID ^ (targetFormID * 0x9E3779B9u) ^
                                       static_cast<std::uint32_t>(currentGameDay * 100.0f)));

        Domain::Relationships::RelationshipTransitionOutcome outcome;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            outcome = Domain::Relationships::ApplyRelationshipTransition(
                ws,
                State::ActorId{ npcFormID },
                State::ActorId{ targetFormID },
                status,
                currentGameDay,
                State::ActorId{ initiator },
                checkData.meta,
                checkData.ctx,
                &rng);
        });

        if (outcome.skippedDuplicate) {
            FactionReconciler::GetSingleton().ReconcileActor(npcFormID);
            if (targetFormID != kPlayerFormID) {
                FactionReconciler::GetSingleton().ReconcileActor(targetFormID);
            }
            return true;
        }

        if (!outcome.allowed) {
            MARAS_LOG_WARN("ChangeStatusCommon: Blocked transition ({:08X} <-> {:08X}) -> {}: {}",
                           npcFormID, targetFormID,
                           Foundation::RelationshipStatusToString(status),
                           outcome.blockReason);
            return false;
        }

        if (breakLovers && IsMainTier(status) && !IsMainTier(outcome.oldStatus)) {
            const bool polygamy = SettingsCoordinator::GetSingleton().GetEnablePolygamy();
            if (!(npcFormID == kPlayerFormID && polygamy) && !HasAnyOpenMainPair(npcFormID)) {
                BreakAllLoverConnections(npcFormID, /*onlyCheating=*/false, sendEvent);
            }
            if (!(targetFormID == kPlayerFormID && polygamy) && !HasAnyOpenMainPair(targetFormID)) {
                BreakAllLoverConnections(targetFormID, /*onlyCheating=*/false, sendEvent);
            }
        }

        if (status == Foundation::RelationshipStatus::Dating || status == Foundation::RelationshipStatus::Lover) {
            EvaluateCheatingOnStatusChange(npcFormID, targetFormID, status);
        }

        if (IsBreakupStatus(status) ||
            (status == Foundation::RelationshipStatus::Candidate && IsActivePartnerStatus(outcome.oldStatus))) {
            ReconcileBreakupOrDivorceEngineState(
                npcFormID, targetFormID, outcome.oldStatus, status, outcome.mutations);
        }

        // Reconcile all affected actors, children, factions, packages, globals, and player buffs
        FactionReconciler::GetSingleton().ReconcileActor(npcFormID);
        if (targetFormID != kPlayerFormID) {
            FactionReconciler::GetSingleton().ReconcileActor(targetFormID);
        }
        ReconcileDirtyActorsAndChildren(outcome.mutations, /*reconcilePackages=*/true);

        if (!preChildren.empty()) {
            if (status == Foundation::RelationshipStatus::Married &&
                outcome.oldStatus != Foundation::RelationshipStatus::Married) {
                const auto postSnap = State::WorldStore::GetSingleton().Snapshot();
                LineageUseCases::GetSingleton().HandleMarriageCustodySideEffects(
                    preChildren, *postSnap, npcFormID, targetFormID);
            } else if (outcome.oldStatus == Foundation::RelationshipStatus::Married &&
                       (IsBreakupStatus(status) || status == Foundation::RelationshipStatus::Candidate)) {
                const auto postSnap = State::WorldStore::GetSingleton().Snapshot();
                LineageUseCases::GetSingleton().HandleDivorceCustodySideEffects(
                    preChildren, *postSnap, npcFormID, targetFormID);
            }
        }

        if (isPlayerPair) {
            FactionReconciler::GetSingleton().ReconcilePlayer();
            RelationshipEngineReconciler::GetSingleton().ReconcilePlayerStatusTransition(
                playerPartnerID, status, outcome.appliedBreakupCooldown, sendEvent);
            if (status == Foundation::RelationshipStatus::Married &&
                outcome.oldStatus != Foundation::RelationshipStatus::Married) {
                FactionReconciler::GetSingleton().SanitizePlayerMarriedFaction();
                GetUiBridgeGateway().OnHierarchyChanged();
            }
            if (outcome.offlineSyncedAffection.has_value()) {
                GetUiBridgeGateway().OnAffectionChanged(playerPartnerID, *outcome.offlineSyncedAffection);
            }
        }

        UpdateAllSpousesCheatingAndOpenFactions();
        UpdateAllCanFormNonCheatingMainFactions();
        UpdateAllSpousesChildrenFaction();
        GlobalVarReconciler::GetSingleton().ReconcileGlobals();
        if (outcome.reconcilePlayerBuffs) {
            BuffReconciler::GetSingleton().ReconcilePlayerBuffs();
        }

        GetUiBridgeGateway().OnStatusChanged(npcFormID, targetFormID, static_cast<std::uint8_t>(status));

        if (sendEvent) {
            EmitStatusChangedModEvents(npcFormID, targetFormID, status);
        }

        if (outcome.recordMemory &&
            GetSkyrimNetGateway().IsAvailable() &&
            (sendEvent || GetSkyrimNetGateway().IsMatchmakingApplyActive())) {
            const RE::FormID memInitiator = initiator;
            const RE::FormID memPartner = (initiator == npcFormID) ? targetFormID : npcFormID;
            GetSkyrimNetGateway().RecordRelationshipStatusChange(
                memInitiator, memPartner, outcome.oldStatus, outcome.newStatus);
        }

        MARAS_LOG_INFO("Transitioned ({:08X} <-> {:08X}): {} -> {}",
                       npcFormID, targetFormID,
                       Foundation::RelationshipStatusToString(outcome.oldStatus),
                       Foundation::RelationshipStatusToString(status));
        return true;
    }

    bool RelationshipUseCases::PromoteToStatus(RE::FormID npcFormID,
                                               Foundation::RelationshipStatus status,
                                               bool sendEvent,
                                               bool breakLovers) {
        return ChangeStatusCommon(npcFormID, kPlayerFormID, status, kPlayerFormID, sendEvent, breakLovers);
    }

    bool RelationshipUseCases::PromoteToStatus(RE::FormID actorA,
                                               RE::FormID actorB,
                                               Foundation::RelationshipStatus targetStatus,
                                               RE::FormID initiatorID,
                                               bool bypassDifficulty,
                                               bool suppressModal) {
        (void)bypassDifficulty;
        (void)suppressModal;
        return ChangeStatusCommon(actorA,
                                  actorB,
                                  targetStatus,
                                  initiatorID,
                                  /*sendEvent=*/true,
                                  /*breakLovers=*/IsMainTier(targetStatus));
    }

    bool RelationshipUseCases::PromoteToStatus(Foundation::ActorId actorA,
                                               Foundation::ActorId actorB,
                                               Foundation::RelationshipStatus targetStatus,
                                               Foundation::ActorId initiatorID,
                                               bool bypassDifficulty,
                                               bool suppressModal) {
        return PromoteToStatus(actorA.raw(),
                               actorB.raw(),
                               targetStatus,
                               initiatorID.raw(),
                               bypassDifficulty,
                               suppressModal);
    }

    bool RelationshipUseCases::PromoteToStatusWith(RE::FormID npcFormID,
                                                   RE::FormID targetFormID,
                                                   Foundation::RelationshipStatus status,
                                                   bool sendEvent,
                                                   bool breakLovers) {
        return ChangeStatusCommon(npcFormID, targetFormID, status, 0, sendEvent, breakLovers);
    }

    bool RelationshipUseCases::PromoteToEngaged(RE::FormID npcFormID,
                                                RE::FormID targetFormID,
                                                bool sendEvent,
                                                bool breakLovers) {
        return ChangeStatusCommon(npcFormID, targetFormID, Foundation::RelationshipStatus::Engaged, 0, sendEvent, breakLovers);
    }

    bool RelationshipUseCases::PromoteToMarried(RE::FormID npcFormID,
                                                RE::FormID targetFormID,
                                                bool sendEvent,
                                                bool breakLovers) {
        return ChangeStatusCommon(npcFormID, targetFormID, Foundation::RelationshipStatus::Married, 0, sendEvent, breakLovers);
    }

    bool RelationshipUseCases::PromoteToDivorced(RE::FormID npcFormID,
                                                 RE::FormID targetFormID,
                                                 bool sendEvent) {
        return ChangeStatusCommon(npcFormID, targetFormID, Foundation::RelationshipStatus::Divorced, 0, sendEvent, false);
    }

    bool RelationshipUseCases::PromoteToJilted(RE::FormID npcFormID,
                                               RE::FormID targetFormID,
                                               bool sendEvent) {
        return ChangeStatusCommon(npcFormID, targetFormID, Foundation::RelationshipStatus::Jilted, 0, sendEvent, false);
    }

    bool RelationshipUseCases::PromoteToDating(RE::FormID npcFormID,
                                               RE::FormID targetFormID,
                                               bool sendEvent,
                                               bool breakLovers) {
        return ChangeStatusCommon(npcFormID, targetFormID, Foundation::RelationshipStatus::Dating, 0, sendEvent, breakLovers);
    }

    bool RelationshipUseCases::PromoteToLover(RE::FormID npcFormID,
                                              RE::FormID targetFormID,
                                              bool sendEvent) {
        return ChangeStatusCommon(npcFormID, targetFormID, Foundation::RelationshipStatus::Lover, 0, sendEvent, false);
    }

    bool RelationshipUseCases::PromoteToExDating(RE::FormID npcFormID,
                                                 RE::FormID targetFormID,
                                                 bool sendEvent) {
        return ChangeStatusCommon(npcFormID, targetFormID, Foundation::RelationshipStatus::ExDating, 0, sendEvent, false);
    }

    bool RelationshipUseCases::PromoteToExLover(RE::FormID npcFormID,
                                                RE::FormID targetFormID,
                                                bool sendEvent) {
        return ChangeStatusCommon(npcFormID, targetFormID, Foundation::RelationshipStatus::ExLover, 0, sendEvent, false);
    }

    bool RelationshipUseCases::DemoteToCandidate(RE::FormID npcFormID,
                                                   RE::FormID targetFormID,
                                                   bool sendEvent) {
        return ChangeStatusCommon(npcFormID, targetFormID, Foundation::RelationshipStatus::Candidate, 0, sendEvent, false);
    }

    bool RelationshipUseCases::Breakup(RE::FormID actorA, RE::FormID actorB, bool sendEvent) {
        if (actorA == 0 || actorB == 0 || actorA == actorB) {
            return false;
        }
        const auto currentStatus = GetRelationshipStatus(actorA, actorB);
        switch (currentStatus) {
            case Foundation::RelationshipStatus::Married:
                return PromoteToDivorced(actorA, actorB, sendEvent);
            case Foundation::RelationshipStatus::Engaged:
                return PromoteToJilted(actorA, actorB, sendEvent);
            case Foundation::RelationshipStatus::Dating:
                return PromoteToExDating(actorA, actorB, sendEvent);
            case Foundation::RelationshipStatus::Lover:
                return PromoteToExLover(actorA, actorB, sendEvent);
            default:
                return false;
        }
    }

    bool RelationshipUseCases::RemoveRelationship(RE::FormID actorA, RE::FormID actorB) {
        if (actorA == 0 || actorB == 0 || actorA == actorB) {
            return false;
        }
        const auto preSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto* existingEdge = preSnap->relationships->FindEdge(State::ActorId{ actorA }, State::ActorId{ actorB });
        const bool wasMarriedToPlayer =
            existingEdge && existingEdge->status == Foundation::RelationshipStatus::Married &&
            (actorA == kPlayerFormID || actorB == kPlayerFormID);
        const bool isPlayerPair = (actorA == kPlayerFormID || actorB == kPlayerFormID);
        const RE::FormID playerPartnerID = isPlayerPair ? ((actorA == kPlayerFormID) ? actorB : actorA) : 0;

        const RE::FormID baseA = SpatialWorldIndex::GetSingleton().GetBaseID(actorA);
        const RE::FormID baseB = SpatialWorldIndex::GetSingleton().GetBaseID(actorB);

        CachedActorMetadataProvider meta;
        meta.CaptureActors(preSnap, { actorA, actorB });
        const float gameDay = GetCurrentGameDays();
        Foundation::DeterministicRng rng(static_cast<std::uint32_t>(actorA ^ actorB ^ 0x517CC1B7u));

        struct ParentChildLinkFlags {
            bool aLink{ false };
            bool bLink{ false };
        };
        std::unordered_map<RE::FormID, ParentChildLinkFlags> parentChildLinks;
        if (preSnap->lineage) {
            for (const auto& [cId, childRec] : preSnap->lineage->AllChildren()) {
                if (cId.HasPhysicalActor() &&
                    (childRec.bioMother.raw() == actorA || childRec.bioMother.raw() == actorB ||
                     childRec.bioFather.raw() == actorA || childRec.bioFather.raw() == actorB)) {
                    parentChildLinks.emplace(
                        cId.raw(),
                        ParentChildLinkFlags{
                            HasEngineParentChildLink(cId.raw(), actorA),
                            HasEngineParentChildLink(cId.raw(), actorB) });
                }
            }
        }

        std::unordered_map<RE::FormID, State::ChildRecord> preChildren;
        if (preSnap->lineage &&
            existingEdge && Foundation::IsMainTierStatus(existingEdge->status)) {
            for (const auto& [cId, childRec] : preSnap->lineage->AllChildren()) {
                preChildren.emplace(cId.raw(), childRec);
            }
        }

        std::unordered_set<RE::FormID> affectedActors{ actorA, actorB };
        State::WorldMutationResult custodyMut;

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            auto removePendingFor = [&](RE::FormID ownerBase, RE::FormID partnerBase, RE::FormID partnerRef) {
                if (ownerBase == 0) {
                    return;
                }
                const auto& allPending = ws.actors->AllPendingRelationships();
                const auto it = allPending.find(State::BaseId{ ownerBase });
                if (it == allPending.end()) {
                    return;
                }
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
                    if (!rec.cheater.IsNull()) {
                        affectedActors.insert(rec.cheater.raw());
                    }
                    if (!rec.deceivedPartner.IsNull()) {
                        affectedActors.insert(rec.deceivedPartner.raw());
                    }
                    if (!rec.paramour.IsNull()) {
                        affectedActors.insert(rec.paramour.raw());
                    }
                    affectedCouplePairs.insert(rec.CouplePair());
                }
            }
            for (const auto& couplePair : affectedCouplePairs) {
                const auto existing = ws.affairs->GetForCouplePair(couplePair);
                std::vector<State::AffairRecord> kept;
                for (const auto& rec : existing) {
                    if (!matchesRemovedPair(rec)) {
                        kept.push_back(rec);
                    }
                }
                ws.affairs.Write().EraseForCouplePair(couplePair);
                for (const auto& rec : kept) {
                    ws.affairs.Write().ImportSerializedRecord(
                        rec.CouplePair(), rec.cheater, rec.deceivedPartner, rec.paramour,
                        rec.incidentCount, rec.lastTimestamp, rec.isSecret,
                        rec.affairType, rec.affairStatus, rec.affairStage, rec.stageStartDay);
                }
            }

            if (isPlayerPair) {
                ws.household.Write().EvictTenant(playerPartnerID);
                if (wasMarriedToPlayer) {
                    (void)Domain::Relationships::ApplyOnSpouseRemoved(ws, State::ActorId{ playerPartnerID });
                }
            }

            (void)ws.relationships.Write().EraseEdge(State::ActorId{ actorA }, State::ActorId{ actorB });
            (void)Domain::Relationships::EvaluateBreakupAffairsAndIntents(
                ws, State::ActorId{ actorA }, State::ActorId{ actorB });

            // Strip ambiguous biological parent attribution if only the other actor holds an engine ParentChild link
            const auto& allChildren = ws.lineage->AllChildren();
            std::vector<State::ChildId> childIds;
            childIds.reserve(allChildren.size());
            for (const auto& [cId, _] : allChildren) {
                childIds.push_back(cId);
            }
            for (const auto cId : childIds) {
                const auto* child = ws.lineage->FindChild(cId);
                if (!child) {
                    continue;
                }
                if (child->bioMother.raw() == actorA || child->bioMother.raw() == actorB ||
                    child->bioFather.raw() == actorA || child->bioFather.raw() == actorB) {
                    if (cId.HasPhysicalActor()) {
                        if (const auto linkIt = parentChildLinks.find(cId.raw()); linkIt != parentChildLinks.end()) {
                            const bool aLink = linkIt->second.aLink;
                            const bool bLink = linkIt->second.bLink;
                            if (aLink && !bLink) {
                                if (auto* mutChild = ws.lineage.Write().FindChildMutable(cId)) {
                                    if (mutChild->bioMother.raw() == actorB) mutChild->bioMother = State::ActorId{};
                                    if (mutChild->bioFather.raw() == actorB) mutChild->bioFather = State::ActorId{};
                                }
                            } else if (bLink && !aLink) {
                                if (auto* mutChild = ws.lineage.Write().FindChildMutable(cId)) {
                                    if (mutChild->bioMother.raw() == actorA) mutChild->bioMother = State::ActorId{};
                                    if (mutChild->bioFather.raw() == actorA) mutChild->bioFather = State::ActorId{};
                                }
                            }
                        }
                    }
                }
            }

            custodyMut = ws.EvaluateCustodyOnDivorce(State::ActorId{ actorA }, State::ActorId{ actorB }, meta, rng);
            if (isPlayerPair) {
                custodyMut.Merge(ws.StopSharingForSpouseWithDeltas(State::ActorId{ playerPartnerID }));
            }
            (void)gameDay;
        });

        if (isPlayerPair) {
            BedOwnershipReconciler::GetSingleton().ReconcileStoppedSharingFromMutation(
                playerPartnerID, custodyMut, "divorce");
            ClearCurrentHome(playerPartnerID);
            PackageReconciler::GetSingleton().ReconcileActor(playerPartnerID);
        } else {
            PackageReconciler::GetSingleton().ReconcileActor(actorA);
            PackageReconciler::GetSingleton().ReconcileActor(actorB);
        }
        BanterSceneCoordinator::GetSingleton().CleanupRetiredSceneMarkers(custodyMut.retiredSceneMarkers);

        ReconcileDirtyActorsAndChildren(custodyMut, /*reconcilePackages=*/true);
        if (custodyMut.changed && !preChildren.empty()) {
            const auto postSnap = State::WorldStore::GetSingleton().Snapshot();
            LineageUseCases::GetSingleton().HandleDivorceCustodySideEffects(
                preChildren, *postSnap, actorA, actorB);
        }
        for (const RE::FormID id : affectedActors) {
            if (id != 0) {
                if (id == kPlayerFormID) {
                    FactionReconciler::GetSingleton().ReconcilePlayer();
                } else {
                    FactionReconciler::GetSingleton().ReconcileActor(id);
                }
            }
        }
        GlobalVarReconciler::GetSingleton().ReconcileGlobals();
        UpdateAllSpousesChildrenFaction();
        UpdateAllSpousesCheatingAndOpenFactions();
        FactionReconciler::GetSingleton().ReconcilePlayer();
        if (wasMarriedToPlayer) {
            BuffReconciler::GetSingleton().ReconcilePlayerBuffs();
            GetUiBridgeGateway().OnHierarchyChanged();
        }
        GetUiBridgeGateway().OnStatusChanged(
            actorA, actorB, static_cast<std::uint8_t>(Foundation::RelationshipStatus::Candidate));

        MARAS_LOG_INFO("RemoveRelationship: Removed relationship record between {:08X} and {:08X}", actorA, actorB);
        return true;
    }

    int RelationshipUseCases::BreakAllLoverConnections(RE::FormID actorID, bool onlyCheating, bool sendEvent) {
        if (actorID == 0) {
            return 0;
        }
        const auto lovers = GetPartners(actorID, Foundation::RelationshipStatus::Lover);
        int brokenCount = 0;
        for (const RE::FormID loverID : lovers) {
            if (loverID == 0) {
                continue;
            }
            if (onlyCheating) {
                const bool isCheating = IsRelationshipCheating(actorID, loverID);
                if (!isCheating) {
                    continue;
                }
            }
            if (PromoteToExLover(actorID, loverID, sendEvent)) {
                ++brokenCount;
            }
        }
        return brokenCount;
    }

    // =========================================================================
    // Partner, Profile & Edge Queries
    // =========================================================================

    std::vector<std::pair<RE::FormID, Foundation::RelationshipStatus>> RelationshipUseCases::GetActiveRelationships(
        RE::FormID actorID) const {
        std::vector<std::pair<RE::FormID, Foundation::RelationshipStatus>> result;
        if (actorID == 0) {
            return result;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        for (const auto& [partnerId, st] : snap->relationships->GetActivePartners(State::ActorId{ actorID })) {
            result.emplace_back(partnerId.raw(), st);
        }
        return result;
    }

    std::vector<RE::FormID> RelationshipUseCases::GetPartners(RE::FormID npcFormID,
                                                              Foundation::RelationshipStatus status) const {
        std::vector<RE::FormID> result;
        if (npcFormID == 0) {
            return result;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        for (const auto partner : snap->relationships->GetPartnersWithStatus(State::ActorId{ npcFormID }, status)) {
            result.push_back(partner.raw());
        }
        return result;
    }

    std::vector<RE::FormID> RelationshipUseCases::GetAllPartners(RE::FormID npcFormID) const {
        std::vector<RE::FormID> result;
        if (npcFormID == 0) {
            return result;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const State::ActorId selfId{ npcFormID };
        for (const auto& key : snap->relationships->GetAdjacentPairs(selfId)) {
            const auto other = key.Other(selfId);
            if (!other.IsNull()) {
                result.push_back(other.raw());
            }
        }
        return result;
    }

    RE::FormID RelationshipUseCases::GetPrimaryPartner(RE::FormID actorID) const {
        if (actorID == 0) {
            return 0;
        }
        return State::WorldStore::GetSingleton().Snapshot()->relationships->GetPrimaryPartner(
            State::ActorId{ actorID }).raw();
    }

    float RelationshipUseCases::GetEngagementTimestamp(RE::FormID actorA, RE::FormID actorB) const {
        if (actorA == 0 || actorB == 0) {
            return -1.0f;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* edge = snap->relationships->FindEdge(State::ActorId{ actorA }, State::ActorId{ actorB });
        if (!edge) {
            return -1.0f;
        }
        for (auto it = edge->statusHistory.rbegin(); it != edge->statusHistory.rend(); ++it) {
            if (it->toStatus == Foundation::RelationshipStatus::Engaged) {
                return it->timestamp;
            }
        }
        return -1.0f;
    }

    std::vector<RE::FormID> RelationshipUseCases::GetAllRegisteredNPCs() const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        std::vector<RE::FormID> out;
        for (const auto& [refId, prof] : snap->actors->AllProfiles()) {
            if (prof.isRegisteredAdult && !refId.IsNull() && !refId.IsPlayer()) {
                out.push_back(refId.raw());
            }
        }
        return out;
    }

    std::vector<RE::FormID> RelationshipUseCases::GetAllCandidates(RE::FormID targetFormID) const {
        if (targetFormID == kPlayerFormID) {
            const auto snap = State::WorldStore::GetSingleton().Snapshot();
            std::vector<RE::FormID> result;
            for (const auto& [refId, prof] : snap->actors->AllProfiles()) {
                if (!prof.isRegisteredAdult || refId.IsNull() || refId.IsPlayer()) {
                    continue;
                }
                const auto st = snap->relationships->GetStatus(State::ActorId{ refId.raw() }, State::ActorId::Player());
                if (st == Foundation::RelationshipStatus::Candidate) {
                    result.push_back(refId.raw());
                }
            }
            return result;
        }
        return GetPartners(targetFormID, Foundation::RelationshipStatus::Candidate);
    }

    std::vector<RE::FormID> RelationshipUseCases::GetAllEngaged(RE::FormID targetFormID) const {
        return GetPartners(targetFormID, Foundation::RelationshipStatus::Engaged);
    }

    std::vector<RE::FormID> RelationshipUseCases::GetAllMarried(RE::FormID targetFormID) const {
        return GetPartners(targetFormID, Foundation::RelationshipStatus::Married);
    }

    std::vector<RE::FormID> RelationshipUseCases::GetAllDivorced(RE::FormID targetFormID) const {
        return GetPartners(targetFormID, Foundation::RelationshipStatus::Divorced);
    }

    std::vector<RE::FormID> RelationshipUseCases::GetAllJilted(RE::FormID targetFormID) const {
        return GetPartners(targetFormID, Foundation::RelationshipStatus::Jilted);
    }

    std::vector<RE::FormID> RelationshipUseCases::GetAllDating(RE::FormID targetFormID) const {
        return GetPartners(targetFormID, Foundation::RelationshipStatus::Dating);
    }

    std::vector<RE::FormID> RelationshipUseCases::GetAllLovers(RE::FormID targetFormID) const {
        return GetPartners(targetFormID, Foundation::RelationshipStatus::Lover);
    }

    std::vector<RE::FormID> RelationshipUseCases::GetAllExDating(RE::FormID targetFormID) const {
        return GetPartners(targetFormID, Foundation::RelationshipStatus::ExDating);
    }

    std::vector<RE::FormID> RelationshipUseCases::GetAllExLovers(RE::FormID targetFormID) const {
        return GetPartners(targetFormID, Foundation::RelationshipStatus::ExLover);
    }

    std::optional<NPCProfileData> RelationshipUseCases::GetNPCProfile(RE::FormID npcFormID) const {
        if (npcFormID == 0) {
            return std::nullopt;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* prof = snap->actors->Find(State::RefId{ npcFormID });
        if (!prof || !prof->isRegisteredAdult) {
            return std::nullopt;
        }
        return ToPublicProfile(*prof);
    }

    void RelationshipUseCases::UpsertNPCProfile(const NPCProfileData& profile) {
        if (profile.formID == 0) {
            return;
        }
        const RE::FormID baseIdRaw = SpatialWorldIndex::GetSingleton().GetBaseID(profile.formID);
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            State::ActorProfileRecord rec;
            if (const auto* existing = ws.actors.Read().Find(State::RefId{ profile.formID })) {
                rec = *existing;
            }
            rec.refId = State::RefId{ profile.formID };
            if (rec.baseId.IsNull()) {
                rec.baseId = State::BaseId{ baseIdRaw != 0 ? baseIdRaw : profile.formID };
            }
            rec.socialClass = profile.socialClass;
            rec.skillType = profile.skillType;
            rec.temperament = profile.temperament;
            rec.isRegisteredAdult = true;
            if (profile.originalHome.has_value()) {
                rec.legacyOriginalHome = State::FormRawId{ *profile.originalHome };
            } else {
                rec.legacyOriginalHome.reset();
            }
            if (profile.currentHome.has_value()) {
                rec.legacyCurrentHome = State::FormRawId{ *profile.currentHome };
            } else {
                rec.legacyCurrentHome.reset();
            }
            if (profile.homeMarker.has_value()) {
                rec.legacyHomeMarker = State::RefId{ *profile.homeMarker };
            } else {
                rec.legacyHomeMarker.reset();
            }
            rec.addedKeywords.clear();
            for (const RE::FormID kw : profile.addedKeywords) {
                rec.addedKeywords.insert(State::FormRawId{ kw });
            }
            ws.actors.Write().Upsert(std::move(rec));
        });
    }

    std::optional<NPCRelationshipData> RelationshipUseCases::GetNPCData(
        RE::FormID npcFormID, RE::FormID targetFormID) const {
        if (npcFormID == 0 || targetFormID == 0) {
            return std::nullopt;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* edge = snap->relationships->FindEdge(State::ActorId{ npcFormID }, State::ActorId{ targetFormID });
        if (!edge) {
            return std::nullopt;
        }
        const auto affairs = snap->affairs->GetForCouplePair(edge->pair);
        return ToPublicRelationshipData(*edge, affairs);
    }

    std::unordered_map<Foundation::CanonicalPairKey, NPCRelationshipData, Foundation::CanonicalPairKeyHash>
    RelationshipUseCases::GetAllRelationshipPairs() const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        std::unordered_map<Foundation::CanonicalPairKey, NPCRelationshipData, Foundation::CanonicalPairKeyHash> out;
        for (const auto& [key, edge] : snap->relationships->AllEdges()) {
            const auto affairs = snap->affairs->GetForCouplePair(key);
            out.emplace(key, ToPublicRelationshipData(edge, affairs));
        }
        return out;
    }

    Foundation::RelationshipStatus RelationshipUseCases::GetRelationshipStatus(
        RE::FormID npcFormID, RE::FormID targetFormID) const {
        if (npcFormID == 0 || targetFormID == 0) {
            return Foundation::RelationshipStatus::Unknown;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (const auto* edge = snap->relationships->FindEdge(State::ActorId{ npcFormID }, State::ActorId{ targetFormID })) {
            return edge->status;
        }
        if (IsRegistered(npcFormID) && (targetFormID == kPlayerFormID || IsRegistered(targetFormID))) {
            return Foundation::RelationshipStatus::Candidate;
        }
        return Foundation::RelationshipStatus::Unknown;
    }

    std::vector<StatusChangeRecord> RelationshipUseCases::GetStatusHistory(
        RE::FormID npcFormID, RE::FormID targetFormID) const {
        std::vector<StatusChangeRecord> out;
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (const auto* edge = snap->relationships->FindEdge(State::ActorId{ npcFormID }, State::ActorId{ targetFormID })) {
            out.reserve(edge->statusHistory.size());
            for (const auto& h : edge->statusHistory) {
                out.push_back(ToPublicStatusRecord(h));
            }
        }
        return out;
    }

    std::optional<StatusChangeRecord> RelationshipUseCases::GetStatusHistoryEntry(
        RE::FormID npcFormID, RE::FormID targetFormID, std::size_t index) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* edge = snap->relationships->FindEdge(State::ActorId{ npcFormID }, State::ActorId{ targetFormID });
        if (!edge || index >= edge->statusHistory.size()) {
            return std::nullopt;
        }
        return ToPublicStatusRecord(edge->statusHistory[index]);
    }

    std::size_t RelationshipUseCases::GetStatusHistoryCount(
        RE::FormID npcFormID, RE::FormID targetFormID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto* edge = snap->relationships->FindEdge(State::ActorId{ npcFormID }, State::ActorId{ targetFormID });
        return edge ? edge->statusHistory.size() : 0;
    }

    void RelationshipUseCases::ClearStatusHistory(RE::FormID npcFormID, RE::FormID targetFormID) {
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (const auto* existing = ws.relationships.Read().FindEdge(State::ActorId{ npcFormID }, State::ActorId{ targetFormID })) {
                auto copy = *existing;
                copy.statusHistory.clear();
                ws.relationships.Write().UpsertEdge(std::move(copy));
            }
        });
    }

    std::size_t RelationshipUseCases::CountPartnersWithStatus(
        RE::FormID npcFormID, Foundation::RelationshipStatus status) const {
        if (npcFormID == 0) {
            return 0;
        }
        return State::WorldStore::GetSingleton().Snapshot()->relationships->GetPartnersWithStatus(
            State::ActorId{ npcFormID }, status).size();
    }

    std::size_t RelationshipUseCases::GetTotalRegisteredCount() const {
        return GetAllRegisteredNPCs().size();
    }
    std::size_t RelationshipUseCases::GetCandidateCount(RE::FormID targetFormID) const {
        return GetAllCandidates(targetFormID).size();
    }
    std::size_t RelationshipUseCases::GetEngagedCount(RE::FormID targetFormID) const {
        return CountPartnersWithStatus(targetFormID, Foundation::RelationshipStatus::Engaged);
    }
    std::size_t RelationshipUseCases::GetMarriedCount(RE::FormID targetFormID) const {
        return CountPartnersWithStatus(targetFormID, Foundation::RelationshipStatus::Married);
    }
    std::size_t RelationshipUseCases::GetDivorcedCount(RE::FormID targetFormID) const {
        return CountPartnersWithStatus(targetFormID, Foundation::RelationshipStatus::Divorced);
    }
    std::size_t RelationshipUseCases::GetJiltedCount(RE::FormID targetFormID) const {
        return CountPartnersWithStatus(targetFormID, Foundation::RelationshipStatus::Jilted);
    }
    std::size_t RelationshipUseCases::GetDatingCount(RE::FormID targetFormID) const {
        return CountPartnersWithStatus(targetFormID, Foundation::RelationshipStatus::Dating);
    }
    std::size_t RelationshipUseCases::GetLoverCount(RE::FormID targetFormID) const {
        return CountPartnersWithStatus(targetFormID, Foundation::RelationshipStatus::Lover);
    }
    std::size_t RelationshipUseCases::GetExDatingCount(RE::FormID targetFormID) const {
        return CountPartnersWithStatus(targetFormID, Foundation::RelationshipStatus::ExDating);
    }
    std::size_t RelationshipUseCases::GetExLoverCount(RE::FormID targetFormID) const {
        return CountPartnersWithStatus(targetFormID, Foundation::RelationshipStatus::ExLover);
    }

    // =========================================================================
    // Classification, Overrides, Factions, Home Markers & Keywords
    // =========================================================================

    const Engine::NPCOverrideData* RelationshipUseCases::FindOverrideData(RE::FormID npcFormID,
                                                                          RE::FormID resolvedBaseID) const {
        if (auto it = npcOverrides_.find(npcFormID); it != npcOverrides_.end()) {
            return &it->second;
        }
        if (resolvedBaseID != 0 && resolvedBaseID != npcFormID) {
            if (auto it = npcOverrides_.find(resolvedBaseID); it != npcOverrides_.end()) {
                return &it->second;
            }
        }
        return nullptr;
    }

    Foundation::SocialClass RelationshipUseCases::GetSocialClass(RE::FormID npcFormID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (const auto* prof = snap->actors->Find(State::RefId{ npcFormID })) {
            return prof->socialClass;
        }
        if (const auto* dec = snap->deceased->FindByActor(
                State::ActorId{ npcFormID },
                State::BaseId{ SpatialWorldIndex::GetSingleton().GetBaseID(npcFormID) })) {
            return dec->socialClass;
        }
        return Foundation::SocialClass::Outcast;
    }

    Foundation::SkillType RelationshipUseCases::GetSkillType(RE::FormID npcFormID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (const auto* prof = snap->actors->Find(State::RefId{ npcFormID })) {
            return prof->skillType;
        }
        if (const auto* dec = snap->deceased->FindByActor(
                State::ActorId{ npcFormID },
                State::BaseId{ SpatialWorldIndex::GetSingleton().GetBaseID(npcFormID) })) {
            return dec->skillType;
        }
        return Foundation::SkillType::Warrior;
    }

    Foundation::Temperament RelationshipUseCases::GetTemperament(RE::FormID npcFormID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (const auto* prof = snap->actors->Find(State::RefId{ npcFormID })) {
            return prof->temperament;
        }
        if (const auto* dec = snap->deceased->FindByActor(
                State::ActorId{ npcFormID },
                State::BaseId{ SpatialWorldIndex::GetSingleton().GetBaseID(npcFormID) })) {
            return dec->temperament;
        }
        return Foundation::Temperament::Proud;
    }

    Foundation::SocialClass RelationshipUseCases::DetermineSocialClass(RE::FormID npcFormID) {
        return NPCTypeDeterminer::DetermineSocialClass(
            npcFormID, [this](RE::FormID id) { return GetSocialClassOverride(id); });
    }

    Foundation::SkillType RelationshipUseCases::DetermineSkillType(RE::FormID npcFormID) {
        return NPCTypeDeterminer::DetermineSkillType(
            npcFormID, [this](RE::FormID id) { return GetSkillTypeOverride(id); });
    }

    Foundation::Temperament RelationshipUseCases::DetermineTemperament(RE::FormID npcFormID) {
        return NPCTypeDeterminer::DetermineTemperament(
            npcFormID,
            [this](RE::FormID id) { return GetTemperamentOverride(id); },
            [this](RE::FormID id) { return DetermineSocialClass(id); },
            [this](RE::FormID id) { return DetermineSkillType(id); });
    }

    bool RelationshipUseCases::SetSocialClass(RE::FormID npcFormID, std::int8_t socialClass) {
        if (npcFormID == 0 || socialClass < 0 ||
            socialClass >= static_cast<std::int8_t>(Foundation::SocialClass::_Count)) {
            return false;
        }
        EnsureRegistered(npcFormID);
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* prof = ws.actors.Write().FindMutable(State::RefId{ npcFormID })) {
                prof->socialClass = static_cast<Foundation::SocialClass>(socialClass);
            }
        });
        FactionReconciler::GetSingleton().ReconcileActor(npcFormID);
        if (IsMarried(npcFormID, kPlayerFormID)) {
            BuffReconciler::GetSingleton().ReconcilePlayerBuffs();
        }
        GetUiBridgeGateway().SendNPCDetail(npcFormID);
        return true;
    }

    bool RelationshipUseCases::SetSkillType(RE::FormID npcFormID, std::int8_t skillType) {
        if (npcFormID == 0 || skillType < 0 ||
            skillType >= static_cast<std::int8_t>(Foundation::SkillType::_Count)) {
            return false;
        }
        EnsureRegistered(npcFormID);
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* prof = ws.actors.Write().FindMutable(State::RefId{ npcFormID })) {
                prof->skillType = static_cast<Foundation::SkillType>(skillType);
            }
        });
        FactionReconciler::GetSingleton().ReconcileActor(npcFormID);
        if (IsMarried(npcFormID, kPlayerFormID)) {
            BuffReconciler::GetSingleton().ReconcilePlayerBuffs();
        }
        GetUiBridgeGateway().SendNPCDetail(npcFormID);
        return true;
    }

    bool RelationshipUseCases::SetTemperament(RE::FormID npcFormID, std::int8_t temperament) {
        if (npcFormID == 0 || temperament < 0 ||
            temperament >= static_cast<std::int8_t>(Foundation::Temperament::_Count)) {
            return false;
        }
        EnsureRegistered(npcFormID);
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* prof = ws.actors.Write().FindMutable(State::RefId{ npcFormID })) {
                prof->temperament = static_cast<Foundation::Temperament>(temperament);
            }
        });
        FactionReconciler::GetSingleton().ReconcileActor(npcFormID);
        GetUiBridgeGateway().SendNPCDetail(npcFormID);
        return true;
    }

    bool RelationshipUseCases::AddToSocialClassFaction(RE::FormID npcFormID, std::int8_t rank) {
        return SetSocialClass(npcFormID, rank);
    }

    bool RelationshipUseCases::AddToSkillTypeFaction(RE::FormID npcFormID, std::int8_t rank) {
        return SetSkillType(npcFormID, rank);
    }

    bool RelationshipUseCases::AddToTemperamentFaction(RE::FormID npcFormID, std::int8_t rank) {
        return SetTemperament(npcFormID, rank);
    }

    void RelationshipUseCases::ManageFactions(RE::FormID npcFormID,
                                              RE::FormID targetFormID,
                                              Foundation::RelationshipStatus status) {
        (void)status;
        if (npcFormID != 0 && npcFormID != kPlayerFormID) {
            FactionReconciler::GetSingleton().ReconcileActor(npcFormID);
        }
        if (targetFormID != 0 && targetFormID != kPlayerFormID) {
            FactionReconciler::GetSingleton().ReconcileActor(targetFormID);
        }
        if (npcFormID == kPlayerFormID || targetFormID == kPlayerFormID) {
            FactionReconciler::GetSingleton().ReconcilePlayer();
        }
    }

    bool RelationshipUseCases::LoadOverridesFromFolder(const std::string& folderPath) {
        Engine::OverrideMap loaded;
        const bool ok = Engine::JsonOverrideLoader::LoadOverridesFromFolder(folderPath, loaded);
        {
            std::unique_lock lock(overridesMutex_);
            npcOverrides_ = std::move(loaded);
        }
        return ok;
    }

    bool RelationshipUseCases::HasSocialClassOverride(RE::FormID npcFormID) const {
        return GetSocialClassOverride(npcFormID).has_value();
    }

    bool RelationshipUseCases::HasSkillTypeOverride(RE::FormID npcFormID) const {
        return GetSkillTypeOverride(npcFormID).has_value();
    }

    bool RelationshipUseCases::HasTemperamentOverride(RE::FormID npcFormID) const {
        return GetTemperamentOverride(npcFormID).has_value();
    }

    std::optional<std::string> RelationshipUseCases::GetSocialClassOverride(RE::FormID npcFormID) const {
        const RE::FormID baseId = SpatialWorldIndex::GetSingleton().GetBaseID(npcFormID);
        std::shared_lock lock(overridesMutex_);
        if (const auto* ov = FindOverrideData(npcFormID, baseId)) {
            return ov->socialClass;
        }
        return std::nullopt;
    }

    std::optional<std::string> RelationshipUseCases::GetSkillTypeOverride(RE::FormID npcFormID) const {
        const RE::FormID baseId = SpatialWorldIndex::GetSingleton().GetBaseID(npcFormID);
        std::shared_lock lock(overridesMutex_);
        if (const auto* ov = FindOverrideData(npcFormID, baseId)) {
            return ov->skillType;
        }
        return std::nullopt;
    }

    std::optional<std::string> RelationshipUseCases::GetTemperamentOverride(RE::FormID npcFormID) const {
        const RE::FormID baseId = SpatialWorldIndex::GetSingleton().GetBaseID(npcFormID);
        std::shared_lock lock(overridesMutex_);
        if (const auto* ov = FindOverrideData(npcFormID, baseId)) {
            return ov->temperament;
        }
        return std::nullopt;
    }

    std::size_t RelationshipUseCases::GetOverrideCount() const {
        std::shared_lock lock(overridesMutex_);
        return npcOverrides_.size();
    }

    Engine::JsonOverrideLoader::LoadStatistics RelationshipUseCases::GetLastOverrideLoadStats() const {
        return Engine::JsonOverrideLoader::GetLastLoadStatistics();
    }

    bool RelationshipUseCases::SetHomeMarker(RE::FormID npcFormID, RE::FormID markerFormID) {
        if (npcFormID == 0 || markerFormID == 0) {
            return false;
        }
        EnsureRegistered(npcFormID);
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* prof = ws.actors.Write().FindMutable(State::RefId{ npcFormID })) {
                prof->legacyHomeMarker = State::RefId{ markerFormID };
            }
        });
        HouseholdEngineOps::GetSingleton().SetLinkedRefForHomeMarker(npcFormID, markerFormID);
        return true;
    }

    std::optional<RE::FormID> RelationshipUseCases::GetHomeMarker(RE::FormID npcFormID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (const auto* prof = snap->actors->Find(State::RefId{ npcFormID });
            prof && prof->legacyHomeMarker.has_value()) {
            return prof->legacyHomeMarker->raw();
        }
        return std::nullopt;
    }

    RE::TESObjectREFR* RelationshipUseCases::GetOrCreateHomeMarker(RE::Actor* npcActor) {
        return HouseholdEngineOps::GetSingleton().GetOrCreateHomeMarker(npcActor);
    }

    void RelationshipUseCases::ClearCurrentHome(RE::FormID npcFormID) {
        if (npcFormID == 0) {
            return;
        }
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* prof = ws.actors.Write().FindMutable(State::RefId{ npcFormID })) {
                prof->legacyCurrentHome.reset();
            }
        });
        if (!Foundation::IsSyntheticChildRawId(npcFormID)) {
            HouseholdEngineOps::GetSingleton().ClearHomeMarkerLinkedRef(npcFormID);
        }
    }

    bool RelationshipUseCases::AddTrackedKeyword(RE::FormID npcFormID, RE::FormID keywordFormID) {
        if (npcFormID == 0 || keywordFormID == 0) {
            return false;
        }
        EnsureRegistered(npcFormID);
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* prof = ws.actors.Write().FindMutable(State::RefId{ npcFormID })) {
                prof->addedKeywords.insert(State::FormRawId{ keywordFormID });
            }
        });
        if (auto* kw = RE::TESForm::LookupByID<RE::BGSKeyword>(keywordFormID)) {
            if (auto* base = ActorSvc::GetActorBase(npcFormID); base && !base->HasKeyword(kw)) {
                base->AddKeyword(kw);
            }
        }
        return true;
    }

    bool RelationshipUseCases::RemoveTrackedKeyword(RE::FormID npcFormID, RE::FormID keywordFormID) {
        if (npcFormID == 0 || keywordFormID == 0) {
            return false;
        }
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (auto* prof = ws.actors.Write().FindMutable(State::RefId{ npcFormID })) {
                prof->addedKeywords.erase(State::FormRawId{ keywordFormID });
            }
        });
        return true;
    }

    // =========================================================================
    // Open Relationships & Infidelity
    // =========================================================================

    bool RelationshipUseCases::IsNPCInOpenRelationship(RE::FormID actorID) const {
        if (actorID == 0) {
            return false;
        }
        return State::WorldStore::GetSingleton().Snapshot()->relationships->IsInAnyOpenRelationship(
            State::ActorId{ actorID });
    }

    bool RelationshipUseCases::GetPairIsOpen(RE::FormID actorA, RE::FormID actorB) const {
        if (actorA == 0 || actorB == 0) {
            return false;
        }
        return State::WorldStore::GetSingleton().Snapshot()->relationships->IsPairOpen(
            State::ActorId{ actorA }, State::ActorId{ actorB });
    }

    bool RelationshipUseCases::HasAnyOpenMainPair(RE::FormID actorID) const {
        if (actorID == 0) {
            return false;
        }
        return State::WorldStore::GetSingleton().Snapshot()->relationships->HasAnyOpenMainPair(
            State::ActorId{ actorID });
    }

    bool RelationshipUseCases::SetPairIsOpen(Foundation::ActorId actorA,
                                             Foundation::ActorId actorB,
                                             bool isOpen,
                                             bool sendEvent) {
        return SetPairIsOpen(actorA.raw(), actorB.raw(), isOpen, sendEvent);
    }

    bool RelationshipUseCases::SetPairIsOpen(RE::FormID actorA,
                                             RE::FormID actorB,
                                             bool isOpen,
                                             bool sendEvent) {
        if (actorA == 0 || actorB == 0 || actorA == actorB) {
            return false;
        }
        const float currentDay = GetCurrentGameDays();
        Domain::Relationships::PairOpenOutcome outcome;
        std::vector<RE::FormID> playerVictimCheatersToRefresh;

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            if (isOpen && (actorA == kPlayerFormID || actorB == kPlayerFormID)) {
                const RE::FormID partnerID = (actorA == kPlayerFormID) ? actorB : actorA;
                for (const auto& rec : ws.affairs.Read().GetByCheater(State::ActorId{ partnerID })) {
                    if (rec.deceivedPartner.IsPlayer() && rec.affairStatus == Foundation::AffairStatus::Ongoing) {
                        playerVictimCheatersToRefresh.push_back(rec.paramour.raw());
                    }
                }
            }
            outcome = Domain::Relationships::ApplySetPairOpen(
                ws, State::ActorId{ actorA }, State::ActorId{ actorB }, isOpen, currentDay);
        });

        for (const auto& [member, lover] : outcome.brokenLovers) {
            (void)PromoteToExLover(member.raw(), lover.raw(), sendEvent);
        }

        UpdateSpouseOpenRelationshipFaction(actorA);
        UpdateSpouseOpenRelationshipFaction(actorB);
        if (isOpen) {
            UpdateSpouseCheatingFaction(actorA);
            UpdateSpouseCheatingFaction(actorB);
            UpdateCanFormNonCheatingMainFaction(actorA);
            UpdateCanFormNonCheatingMainFaction(actorB);
            for (const RE::FormID extraId : playerVictimCheatersToRefresh) {
                if (extraId != 0 && extraId != kPlayerFormID) {
                    UpdateCanFormNonCheatingMainFaction(extraId);
                }
            }
            if (actorA == kPlayerFormID || actorB == kPlayerFormID) {
                FactionReconciler::GetSingleton().ReconcilePlayer();
            }
        }

        GetUiBridgeGateway().OnStatusChanged(actorA, actorB, static_cast<std::uint8_t>(outcome.pairStatus));

        if (outcome.wasOpen != isOpen &&
            GetSkyrimNetGateway().IsAvailable() &&
            (sendEvent || GetSkyrimNetGateway().IsMatchmakingApplyActive())) {
            GetSkyrimNetGateway().RecordOpenRelationshipToggle(actorA, actorB, isOpen);
        }

        if (sendEvent) {
            AffectionEventCoordinator::GetSingleton().OnOpenRelationshipToggled(actorA, actorB, isOpen);

            nlohmann::json payload;
            payload["actorA"] = FormatFormIdUpper(actorA);
            payload["actorB"] = FormatFormIdUpper(actorB);
            payload["actorAName"] = ActorSvc::GetName(actorA);
            payload["actorBName"] = ActorSvc::GetName(actorB);
            payload["isOpen"] = isOpen;
            const std::string jsonStr =
                payload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            auto* senderActor = SpatialWorldIndex::GetSingleton().GetActorFromFormId(actorA);
            EmitModCallbackEvent("maras_open_relationship_changed",
                                 jsonStr,
                                 0.0f,
                                 senderActor ? senderActor->GetFormID() : 0);
            if (GetSkyrimNetGateway().IsAvailable()) {
                GetSkyrimNetGateway().EmitOpenRelationshipChange(actorA, actorB, isOpen);
            }
        }

        return outcome.changed || (outcome.wasOpen == isOpen);
    }

    bool RelationshipUseCases::RecordCheatingRecord(RE::FormID cheaterID,
                                                    RE::FormID victimID,
                                                    RE::FormID affairPartnerID,
                                                    bool isSecret,
                                                    Foundation::AffairType affairType,
                                                    Foundation::AffairStatus affairStatus,
                                                    Foundation::AffairStage affairStage) {
        return RecordCheating(cheaterID, victimID, affairPartnerID, isSecret, affairType, affairStatus, affairStage);
    }

    bool RelationshipUseCases::RecordCheating(RE::FormID cheaterID,
                                              RE::FormID victimID,
                                              RE::FormID affairPartnerID,
                                              bool isSecret,
                                              Foundation::AffairType affairType,
                                              Foundation::AffairStatus affairStatus,
                                              Foundation::AffairStage affairStage) {
        if (cheaterID == 0 || victimID == 0 || affairPartnerID == 0) {
            return false;
        }
        const float currentDay = GetCurrentGameDays();
        Domain::Relationships::CheatingIncidentOutcome outcome;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            outcome = Domain::Relationships::ApplyRecordCheating(
                ws,
                State::ActorId{ cheaterID },
                State::ActorId{ victimID },
                State::ActorId{ affairPartnerID },
                isSecret,
                affairType,
                affairStatus,
                affairStage,
                currentDay);
        });
        if (outcome.recorded) {
            UpdateSpouseCheatingFaction(cheaterID);
            UpdateSpouseCheatingFaction(victimID);
            UpdateCanFormNonCheatingMainFaction(cheaterID);
            UpdateCanFormNonCheatingMainFaction(victimID);
            UpdateCanFormNonCheatingMainFaction(affairPartnerID);
            if (victimID == kPlayerFormID || cheaterID == kPlayerFormID) {
                FactionReconciler::GetSingleton().ReconcilePlayer();
            }
            GetUiBridgeGateway().SendOverviewData();
        }
        return outcome.recorded;
    }

    bool RelationshipUseCases::HasCheatedOn(RE::FormID cheaterID, RE::FormID victimID) const {
        if (cheaterID == 0 || victimID == 0) {
            return false;
        }
        return State::WorldStore::GetSingleton().Snapshot()->affairs->HasCheatedOn(
            State::ActorId{ cheaterID }, State::ActorId{ victimID });
    }

    bool RelationshipUseCases::IsNPCCheating(RE::FormID actorID) const {
        if (actorID == 0) {
            return false;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        return Domain::Relationships::IsActorCheating(*snap, State::ActorId{ actorID });
    }

    bool RelationshipUseCases::IsPairCheating(RE::FormID actorA, RE::FormID actorB) const {
        return IsRelationshipCheating(actorA, actorB);
    }

    bool RelationshipUseCases::IsRelationshipCheating(RE::FormID actorA, RE::FormID actorB) const {
        if (actorA == 0 || actorB == 0) {
            return false;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        return Domain::Relationships::IsRelationshipCheating(
            *snap, State::ActorId{ actorA }, State::ActorId{ actorB });
    }

    std::uint32_t RelationshipUseCases::GetCheatingCount(RE::FormID cheaterID, RE::FormID victimID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        std::uint32_t total = 0;
        for (const auto& rec : snap->affairs->GetForCouplePair(
                 State::CanonicalPairKey{ State::ActorId{ cheaterID }, State::ActorId{ victimID } })) {
            if (rec.cheater.raw() == cheaterID && rec.deceivedPartner.raw() == victimID) {
                total += rec.incidentCount;
            }
        }
        return total;
    }

    std::vector<RE::FormID> RelationshipUseCases::GetCheatedOnPartners(RE::FormID cheaterID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        std::unordered_set<RE::FormID> seen;
        std::vector<RE::FormID> result;
        for (const auto& rec : snap->affairs->GetByCheater(State::ActorId{ cheaterID })) {
            const RE::FormID vId = rec.deceivedPartner.raw();
            if (vId != 0 && seen.insert(vId).second) {
                result.push_back(vId);
            }
        }
        return result;
    }

    std::vector<RE::FormID> RelationshipUseCases::GetCheatersWhoCheatedOn(RE::FormID victimID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        std::unordered_set<RE::FormID> seen;
        std::vector<RE::FormID> result;
        for (const auto& rec : snap->affairs->GetByDeceivedPartner(State::ActorId{ victimID })) {
            const RE::FormID cId = rec.cheater.raw();
            if (cId != 0 && seen.insert(cId).second) {
                result.push_back(cId);
            }
        }
        return result;
    }

    std::vector<CheatingRecord> RelationshipUseCases::GetCheatingRecords(
        RE::FormID actorA, RE::FormID actorB) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        std::vector<CheatingRecord> result;
        for (const auto& rec : snap->affairs->GetForCouplePair(
                 State::CanonicalPairKey{ State::ActorId{ actorA }, State::ActorId{ actorB } })) {
            result.push_back(ToPublicCheatingRecord(rec));
        }
        return result;
    }

    std::vector<CheatingRecord> RelationshipUseCases::GetCheatingRecordsForActor(RE::FormID actorID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        std::vector<CheatingRecord> result;
        for (const auto& rec : snap->affairs->GetAllInvolving(State::ActorId{ actorID })) {
            result.push_back(ToPublicCheatingRecord(rec));
        }
        return result;
    }

    bool RelationshipUseCases::SetCheatingAffairStatus(RE::FormID cheaterID,
                                                       RE::FormID victimID,
                                                       RE::FormID affairPartnerID,
                                                       Foundation::AffairStatus status) {
        bool updated = false;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            updated = ws.affairs.Write().SetStatus(
                State::ActorId{ cheaterID }, State::ActorId{ victimID }, State::ActorId{ affairPartnerID }, status);
        });
        if (updated) {
            UpdateSpouseCheatingFaction(cheaterID);
            UpdateSpouseCheatingFaction(victimID);
            UpdateCanFormNonCheatingMainFaction(cheaterID);
        }
        return updated;
    }

    Foundation::AffairStatus RelationshipUseCases::GetCheatingAffairStatus(RE::FormID cheaterID,
                                                                           RE::FormID victimID,
                                                                           RE::FormID affairPartnerID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (const auto* rec = snap->affairs->FindTriple(
                State::ActorId{ cheaterID }, State::ActorId{ victimID }, State::ActorId{ affairPartnerID })) {
            return rec->affairStatus;
        }
        return Foundation::AffairStatus::Ongoing;
    }

    bool RelationshipUseCases::SetCheatingAffairStage(RE::FormID cheaterID,
                                                      RE::FormID victimID,
                                                      RE::FormID affairPartnerID,
                                                      Foundation::AffairStage stage) {
        const float day = GetCurrentGameDays();
        bool updated = false;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            updated = ws.affairs.Write().SetStage(
                State::ActorId{ cheaterID }, State::ActorId{ victimID }, State::ActorId{ affairPartnerID }, stage, day);
        });
        return updated;
    }

    Foundation::AffairStage RelationshipUseCases::GetCheatingAffairStage(RE::FormID cheaterID,
                                                                         RE::FormID victimID,
                                                                         RE::FormID affairPartnerID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (const auto* rec = snap->affairs->FindTriple(
                State::ActorId{ cheaterID }, State::ActorId{ victimID }, State::ActorId{ affairPartnerID })) {
            return rec->affairStage;
        }
        return Foundation::AffairStage::Physical;
    }

    void RelationshipUseCases::SetPartnerPendingIntent(RE::FormID partnerID,
                                                       Foundation::PartnerIntentType type,
                                                       RE::FormID relatedLoverID) {
        if (partnerID == 0 || partnerID == kPlayerFormID) {
            return;
        }
        const float day = GetCurrentGameDays();
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            ws.relationships.Write().SetPendingIntent(
                State::ActorId{ partnerID },
                State::PendingPartnerIntentRecord{ type, day, State::ActorId{ relatedLoverID } });
        });
    }

    PendingPartnerIntent RelationshipUseCases::GetPartnerPendingIntent(RE::FormID partnerID) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto rec = snap->relationships->GetPendingIntent(State::ActorId{ partnerID });
        return PendingPartnerIntent{ rec.type, rec.createdGameDay, rec.relatedLover.raw() };
    }

    void RelationshipUseCases::ClearPartnerIntent(RE::FormID partnerID) {
        if (partnerID == 0) {
            return;
        }
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            (void)ws.relationships.Write().ClearPendingIntent(State::ActorId{ partnerID });
        });
    }

    void RelationshipUseCases::MarkCheatingAllowedForPair(RE::FormID actorA, RE::FormID actorB) {
        if (actorA == 0 || actorB == 0) {
            return;
        }
        std::vector<Foundation::ActorId> affected;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            affected = Domain::Relationships::MarkCheatingAllowedForPair(
                ws, State::ActorId{ actorA }, State::ActorId{ actorB });
        });
        UpdateSpouseCheatingFaction(actorA);
        UpdateSpouseCheatingFaction(actorB);
        for (const auto id : affected) {
            if (!id.IsNull() && !id.IsPlayer()) {
                UpdateCanFormNonCheatingMainFaction(id.raw());
            }
        }
    }

    void RelationshipUseCases::MarkPlayerCheatingAllowedOnPolygamy() {
        std::size_t updatedCount = 0;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            updatedCount = Domain::Relationships::MarkPlayerCheatingAllowedOnPolygamy(ws);
        });
        if (updatedCount > 0) {
            UpdateAllSpousesCheatingAndOpenFactions();
            UpdateAllCanFormNonCheatingMainFactions();
            FactionReconciler::GetSingleton().ReconcilePlayer();
        }
    }

    void RelationshipUseCases::EvaluateCheatingOnStatusChange(RE::FormID npcFormID,
                                                              RE::FormID targetFormID,
                                                              Foundation::RelationshipStatus newStatus) {
        if (npcFormID == 0) {
            return;
        }
        if (newStatus != Foundation::RelationshipStatus::Dating &&
            newStatus != Foundation::RelationshipStatus::Lover) {
            return;
        }
        const Foundation::AffairType aType = (newStatus == Foundation::RelationshipStatus::Dating)
                                                 ? Foundation::AffairType::Dating
                                                 : Foundation::AffairType::Lover;
        const bool polygamy = SettingsCoordinator::GetSingleton().GetEnablePolygamy();

        auto handleActor = [&](RE::FormID cheaterID, RE::FormID affairPartnerID) {
            if (cheaterID == kPlayerFormID && polygamy) {
                return;
            }
            for (const RE::FormID pID : GetAllPartners(cheaterID)) {
                if (pID == affairPartnerID || pID == 0) {
                    continue;
                }
                if (pID == kPlayerFormID &&
                    IsPlayerActiveMainPartner(cheaterID) &&
                    IsPlayerActiveMainPartner(affairPartnerID)) {
                    continue;
                }
                const auto rData = GetNPCData(cheaterID, pID);
                if (rData && IsMainTier(rData->status) && !rData->isOpen) {
                    const Foundation::AffairStage initStage = (pID == kPlayerFormID)
                                                                  ? Foundation::AffairStage::Talking
                                                                  : Foundation::AffairStage::Physical;
                    (void)RecordCheating(cheaterID,
                                         pID,
                                         affairPartnerID,
                                         /*isSecret=*/true,
                                         aType,
                                         Foundation::AffairStatus::Ongoing,
                                         initStage);
                }
            }
        };

        handleActor(npcFormID, targetFormID);
        if (targetFormID != 0) {
            handleActor(targetFormID, npcFormID);
        }
    }

    bool RelationshipUseCases::SetCheatingDiscovered(RE::FormID cheaterID,
                                                     RE::FormID affairPartnerID,
                                                     Foundation::AffairDiscovery discovery) {
        return SetCheatingDiscovered(
            cheaterID, affairPartnerID, Foundation::AffairDiscoveryToString(discovery), 0);
    }

    bool RelationshipUseCases::SetCheatingDiscovered(RE::FormID cheaterID,
                                                     RE::FormID affairPartnerID,
                                                     Foundation::AffairResolution resolution) {
        return SetCheatingDiscovered(
            cheaterID, affairPartnerID, Foundation::AffairResolutionToString(resolution), 0);
    }

    bool RelationshipUseCases::SetCheatingDiscovered(RE::FormID cheaterID,
                                                     RE::FormID affairPartnerID,
                                                     std::string_view statusOrResolution,
                                                     RE::FormID victimID) {
        if (cheaterID == 0) {
            return false;
        }
        Domain::Relationships::ExposeAffairOutcome outcome;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            outcome = Domain::Relationships::ApplyAffairDiscoveryOrResolution(
                ws,
                State::ActorId{ cheaterID },
                State::ActorId{ affairPartnerID },
                statusOrResolution,
                State::ActorId{ victimID });
        });

        if (!outcome.updated) {
            return false;
        }

        for (const auto& ev : outcome.events) {
            nlohmann::json payload;
            payload["cheater"] = FormatFormIdUpper(ev.cheater.raw());
            payload["cheaterName"] = ActorSvc::GetName(ev.cheater.raw());
            payload["victim"] = FormatFormIdUpper(ev.victim.raw());
            payload["victimName"] = ActorSvc::GetName(ev.victim.raw());
            payload["affairPartner"] = FormatFormIdUpper(ev.paramour.raw());
            payload["affairPartnerName"] = ActorSvc::GetName(ev.paramour.raw());

            if (outcome.isDiscovery) {
                AffectionEventCoordinator::GetSingleton().OnAffairDiscovered(
                    ev.cheater.raw(), ev.victim.raw(), ev.paramour.raw(), outcome.discovery);
                const auto discStr = Foundation::AffairDiscoveryToString(outcome.discovery);
                payload["type"] = discStr;
                const std::string jsonStr =
                    payload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
                const RE::FormID preferredId =
                    (outcome.discovery == Foundation::AffairDiscovery::Exposed)
                        ? ev.paramour.raw()
                        : ev.cheater.raw();
                auto* senderActor = SpatialWorldIndex::GetSingleton().GetActorFromFormId(preferredId);
                EmitModCallbackEvent("maras_affair_exposed",
                                     jsonStr,
                                     0.0f,
                                     senderActor ? senderActor->GetFormID() : 0);
                if (GetSkyrimNetGateway().IsAvailable()) {
                    if (outcome.discovery == Foundation::AffairDiscovery::Confessed) {
                        GetSkyrimNetGateway().EmitAffairConfessed(
                            ev.cheater.raw(), ev.victim.raw(), ev.paramour.raw());
                        GetSkyrimNetGateway().RecordInfidelityEvent(
                            ev.cheater.raw(),
                            ev.victim.raw(),
                            ev.paramour.raw(),
                            std::string(discStr));
                    } else if (outcome.discovery == Foundation::AffairDiscovery::Exposed) {
                        GetSkyrimNetGateway().EmitAffairExposed(
                            ev.paramour.raw(), ev.victim.raw(), ev.cheater.raw());
                        GetSkyrimNetGateway().RecordInfidelityEvent(
                            ev.cheater.raw(),
                            ev.victim.raw(),
                            ev.paramour.raw(),
                            std::string(discStr));
                    }
                }
            } else if (outcome.resolution != Foundation::AffairResolution::None) {
                AffectionEventCoordinator::GetSingleton().OnAffairResolved(
                    ev.cheater.raw(), ev.victim.raw(), statusOrResolution);
                payload["resolution"] = statusOrResolution;
                const std::string jsonStr =
                    payload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
                auto* senderActor = SpatialWorldIndex::GetSingleton().GetActorFromFormId(ev.cheater.raw());
                EmitModCallbackEvent("maras_affair_resolved",
                                     jsonStr,
                                     0.0f,
                                     senderActor ? senderActor->GetFormID() : 0);
                if (GetSkyrimNetGateway().IsAvailable()) {
                    GetSkyrimNetGateway().EmitAffairResolved(
                        ev.cheater.raw(), ev.victim.raw(), ev.paramour.raw(), statusOrResolution);
                    GetSkyrimNetGateway().RecordInfidelityEvent(
                        ev.cheater.raw(),
                        ev.victim.raw(),
                        ev.paramour.raw(),
                        std::string(statusOrResolution));
                }
            }
        }

        for (const auto victimOpen : outcome.victimsToOpen) {
            (void)SetPairIsOpen(cheaterID, victimOpen.raw(), /*isOpen=*/true, /*sendEvent=*/true);
        }
        for (const auto victimSplit : outcome.victimsToSplit) {
            if (victimSplit.raw() == kPlayerFormID) {
                const auto st = GetRelationshipStatus(cheaterID, kPlayerFormID);
                std::optional<Foundation::RelationshipStatus> targetBreakup;
                switch (st) {
                    case Foundation::RelationshipStatus::Married:
                        targetBreakup = Foundation::RelationshipStatus::Divorced;
                        break;
                    case Foundation::RelationshipStatus::Engaged:
                        targetBreakup = Foundation::RelationshipStatus::Jilted;
                        break;
                    case Foundation::RelationshipStatus::Dating:
                        targetBreakup = Foundation::RelationshipStatus::ExDating;
                        break;
                    case Foundation::RelationshipStatus::Lover:
                        targetBreakup = Foundation::RelationshipStatus::ExLover;
                        break;
                    default:
                        break;
                }
                if (targetBreakup.has_value()) {
                    (void)ChangeStatusCommon(
                        cheaterID,
                        kPlayerFormID,
                        *targetBreakup,
                        kPlayerFormID,
                        /*sendEvent=*/true,
                        /*breakLovers=*/false,
                        /*partnerCheatedOnPlayer=*/true);
                }
            } else {
                (void)Breakup(cheaterID, victimSplit.raw(), /*sendEvent=*/true);
            }
        }
        for (const auto partnerBreak : outcome.partnersToBreak) {
            const auto st = GetRelationshipStatus(cheaterID, partnerBreak.raw());
            if (st == Foundation::RelationshipStatus::Lover) {
                (void)PromoteToExLover(cheaterID, partnerBreak.raw(), /*sendEvent=*/true);
            } else if (st == Foundation::RelationshipStatus::Dating) {
                (void)PromoteToExDating(cheaterID, partnerBreak.raw(), /*sendEvent=*/true);
            }
        }

        UpdateSpouseCheatingFaction(cheaterID);
        UpdateCanFormNonCheatingMainFaction(cheaterID);
        for (const auto v : outcome.victimsToUpdate) {
            UpdateSpouseCheatingFaction(v.raw());
            UpdateCanFormNonCheatingMainFaction(v.raw());
        }

        GetUiBridgeGateway().SendOverviewData();
        return true;
    }

    void RelationshipUseCases::UpdateSpouseCheatingFaction(RE::FormID npcFormID) {
        if (npcFormID != 0 && npcFormID != kPlayerFormID) {
            FactionReconciler::GetSingleton().ReconcileActor(npcFormID);
        }
    }

    void RelationshipUseCases::UpdateSpouseOpenRelationshipFaction(RE::FormID npcFormID) {
        if (npcFormID != 0 && npcFormID != kPlayerFormID) {
            FactionReconciler::GetSingleton().ReconcileActor(npcFormID);
        }
    }

    void RelationshipUseCases::UpdateCanFormNonCheatingMainFaction(RE::FormID npcFormID) {
        if (npcFormID == kPlayerFormID) {
            FactionReconciler::GetSingleton().ReconcilePlayer();
        } else if (npcFormID != 0) {
            FactionReconciler::GetSingleton().ReconcileActor(npcFormID);
        }
    }

    void RelationshipUseCases::UpdateAllSpousesCheatingAndOpenFactions() {
        for (const RE::FormID id : GetAllRegisteredNPCs()) {
            FactionReconciler::GetSingleton().ReconcileActor(id);
        }
    }

    void RelationshipUseCases::UpdateAllCanFormNonCheatingMainFactions() {
        FactionReconciler::GetSingleton().ReconcilePlayer();
        for (const RE::FormID id : GetAllRegisteredNPCs()) {
            FactionReconciler::GetSingleton().ReconcileActor(id);
        }
    }

    // =========================================================================
    // Affection Progression
    // =========================================================================

    void RelationshipUseCases::AddDailyAffection(RE::FormID npcFormID, float amount, std::string_view type) {
        AddAffection(npcFormID, amount, type);
    }

    void RelationshipUseCases::AddAffection(RE::FormID npcFormID, float amount, std::string_view type) {
        if (npcFormID == 0 || npcFormID == kPlayerFormID || IsChildNPC(npcFormID)) {
            return;
        }
        const std::string normType = Domain::Relationships::NormalizeAffectionType(type);
        const float currentDay = GetCurrentGameDays();
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            auto& aff = ws.affection.Write();
            const float prev = aff.GetDailyAccumulation(State::ActorId{ npcFormID }, normType);
            aff.SetDailyAccumulation(State::ActorId{ npcFormID }, normType, prev + amount);
            if (amount > 0.0f) {
                aff.SetLastInteractionDay(State::ActorId{ npcFormID }, currentDay);
            }
        });
    }

    float RelationshipUseCases::GetDailyAffection(RE::FormID npcFormID, std::string_view type) const {
        if (npcFormID == 0) {
            return 0.0f;
        }
        const std::string normType = Domain::Relationships::NormalizeAffectionType(type);
        return State::WorldStore::GetSingleton().Snapshot()->affection->GetDailyAccumulation(
            State::ActorId{ npcFormID }, normType);
    }

    int RelationshipUseCases::GetAffection(RE::FormID npcFormID) const {
        return GetPermanentAffection(npcFormID);
    }

    int RelationshipUseCases::GetPermanentAffection(RE::FormID npcFormID) const {
        if (npcFormID == 0) {
            return kAffectionDefault;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        return snap->affection->GetPermanent(State::ActorId{ npcFormID }, kAffectionDefault);
    }

    void RelationshipUseCases::SetAffection(RE::FormID npcFormID, int amount, bool sendEvent) {
        SetPermanentAffection(npcFormID, amount, sendEvent);
    }

    void RelationshipUseCases::SetPermanentAffection(RE::FormID npcFormID, int amount, bool sendEvent) {
        if (npcFormID == 0 || npcFormID == kPlayerFormID || IsChildNPC(npcFormID)) {
            return;
        }
        const int clamped = State::AffectionTable::ClampScore(amount);
        int oldVal = kAffectionDefault;
        bool isMarried = false;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            oldVal = ws.affection.Read().GetPermanent(State::ActorId{ npcFormID }, kAffectionDefault);
            ws.affection.Write().SetPermanent(State::ActorId{ npcFormID }, clamped);
            isMarried = ws.relationships.Read().GetStatus(
                            State::ActorId::Player(), State::ActorId{ npcFormID }) ==
                        Foundation::RelationshipStatus::Married;
        });

        FactionReconciler::GetSingleton().ReconcileActor(npcFormID);
        if (isMarried && oldVal != clamped) {
            BuffReconciler::GetSingleton().ReconcilePlayerBuffs();
        }
        GetUiBridgeGateway().OnAffectionChanged(npcFormID, clamped);
        if (sendEvent) {
            const auto oldThreshold = Domain::Relationships::GetAffectionThreshold(oldVal);
            const auto newThreshold = Domain::Relationships::GetAffectionThreshold(clamped);
            if (oldThreshold != newThreshold) {
                EmitAffectionChangeModEvent(npcFormID, newThreshold, clamped - oldVal);
            }
        }
    }

    void RelationshipUseCases::ChangeAffection(RE::FormID npcFormID, int delta, bool sendEvent) {
        const int current = GetPermanentAffection(npcFormID);
        SetPermanentAffection(npcFormID, current + delta, sendEvent);
    }

    void RelationshipUseCases::SetAffectionMinMax(std::string_view type, int minVal, int maxVal) {
        const std::string normType = Domain::Relationships::NormalizeAffectionType(type);
        if (normType.empty() || minVal > maxVal) {
            return;
        }
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            ws.affection.Write().SetMinMaxForType(normType, minVal, maxVal);
        });
    }

    bool RelationshipUseCases::HasMinMaxForType(std::string_view type) const {
        const std::string normType = Domain::Relationships::NormalizeAffectionType(type);
        return State::WorldStore::GetSingleton().Snapshot()->affection->GetMinMaxForType(normType).has_value();
    }

    std::pair<int, int> RelationshipUseCases::GetMinMaxForType(std::string_view type) const {
        const std::string normType = Domain::Relationships::NormalizeAffectionType(type);
        if (auto found = State::WorldStore::GetSingleton().Snapshot()->affection->GetMinMaxForType(normType)) {
            return *found;
        }
        return { 0, 0 };
    }

    void RelationshipUseCases::ApplyDailyAffectionsForAll() {
        const auto preSnap = State::WorldStore::GetSingleton().Snapshot();
        CachedActorMetadataProvider meta;
        meta.CaptureActors(preSnap, {});

        std::vector<Domain::Relationships::DailyAffectionChange> changes;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            changes = Domain::Relationships::ApplyDailyAffections(ws, meta);
        });

        bool anyMarriedSpouseChanged = false;
        for (const auto& change : changes) {
            const RE::FormID id = change.actorId.raw();
            FactionReconciler::GetSingleton().ReconcileActor(id);
            if (change.marriedSpouseChanged) {
                anyMarriedSpouseChanged = true;
            }
            if (change.newScore != change.oldScore) {
                GetUiBridgeGateway().OnAffectionChanged(id, change.newScore);
            }
            if (change.newThreshold != change.oldThreshold) {
                EmitAffectionChangeModEvent(id, change.newThreshold, change.newScore - change.oldScore);
            }
        }
        if (anyMarriedSpouseChanged) {
            BuffReconciler::GetSingleton().ReconcilePlayerBuffs();
        }
    }

    void RelationshipUseCases::OnAffectionDayChanged() {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const float lonelinessStartDays = snap->settings->GetLonelinessDecayStartDays();
        const auto uniquePartners = Domain::Relationships::CollectUniquePlayerActivePartners(*snap);
        const std::size_t activeCount = uniquePartners.size();

        for (const auto partnerId : uniquePartners) {
            const RE::FormID npcFormID = partnerId.raw();
            auto* actor = SpatialWorldIndex::GetSingleton().GetActorFromFormId(npcFormID);
            const bool isFollowing = (actor != nullptr) && Utils::IsPlayerFollower(actor);
            const float daysSince = GetDaysSinceLastAffection(npcFormID);

            if (!isFollowing && daysSince <= lonelinessStartDays) {
                continue;
            }
            const float decay = Domain::Relationships::CalculateLonelinessDecay(
                *snap, partnerId, isFollowing, activeCount);
            if (decay != 0.0f) {
                AddAffection(npcFormID, decay, "loneliness");
            }
        }

        ApplyDailyAffectionsForAll();
    }

    float RelationshipUseCases::GetAffectionMultiplierForValue(int permanentAffection) const {
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

    float RelationshipUseCases::GetAffectionMultiplierForNPC(RE::FormID npcFormID) const {
        return GetAffectionMultiplierForValue(GetPermanentAffection(npcFormID));
    }

    void RelationshipUseCases::RecordAffectionInteraction(RE::FormID npcFormID) {
        if (npcFormID == 0 || npcFormID == kPlayerFormID) {
            return;
        }
        const float currentDay = GetCurrentGameDays();
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            ws.affection.Write().SetLastInteractionDay(State::ActorId{ npcFormID }, currentDay);
        });
    }

    float RelationshipUseCases::GetDaysSinceLastAffection(RE::FormID npcFormID) const {
        auto* calendar = RE::Calendar::GetSingleton();
        if (!calendar || npcFormID == 0) {
            return 0.0f;
        }
        const float currentDay = calendar->GetDaysPassed();
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto dayOpt = snap->affection->GetLastInteractionDay(State::ActorId{ npcFormID });
        if (!dayOpt.has_value()) {
            return kNeverRecordedDaysSince;
        }
        return currentDay - *dayOpt;
    }

    void RelationshipUseCases::RemoveNPCAffectionData(RE::FormID npcFormID) {
        if (npcFormID == 0) {
            return;
        }
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            ws.affection.Write().EraseActor(State::ActorId{ npcFormID });
        });
    }

    void RelationshipUseCases::SetAffectionDecayMultiplier(float multiplier) {
        const float clamped = std::clamp(multiplier, 0.0f, 2.0f);
        State::WorldStore::GetSingleton().Modify([clamped](State::WorldState& ws) {
            ws.settings.Write().affectionDecayMultiplier = clamped;
        });
    }

    float RelationshipUseCases::GetAffectionDecayMultiplier() const {
        return SettingsCoordinator::GetSingleton().Snapshot()->affectionDecayMultiplier;
    }

    // =========================================================================
    // Spouse Hierarchy
    // =========================================================================

    int RelationshipUseCases::GetSpouseRank(RE::FormID npcFormID) const {
        if (npcFormID == 0) {
            return 4;
        }
        return State::WorldStore::GetSingleton().Snapshot()->hierarchy->GetRank(State::ActorId{ npcFormID });
    }

    RE::FormID RelationshipUseCases::GetSpouseAtRank(int rank) const {
        if (rank < 0 || rank >= static_cast<int>(State::SpouseHierarchyTable::kSlotCount)) {
            return 0;
        }
        return State::WorldStore::GetSingleton().Snapshot()->hierarchy->GetSpouseAtSlot(
            static_cast<std::size_t>(rank)).raw();
    }

    bool RelationshipUseCases::SetSpouseRank(RE::FormID npcFormID, int rank) {
        if (npcFormID == 0) {
            return false;
        }
        Domain::Relationships::HierarchyMutationOutcome outcome;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            outcome = Domain::Relationships::ApplySetHierarchyRank(ws, State::ActorId{ npcFormID }, rank);
        });
        if (!outcome.validRequest) {
            return false;
        }
        ReconcileHierarchySlotsAndEvents(outcome, /*emitModEvents=*/true);
        return true;
    }

    bool RelationshipUseCases::PromoteSpouse(RE::FormID npcFormID) {
        const int curRank = GetSpouseRank(npcFormID);
        if (curRank <= 0) {
            return false;
        }
        const int targetRank = std::min(curRank - 1, static_cast<int>(State::SpouseHierarchyTable::kSlotCount) - 1);
        return SetSpouseRank(npcFormID, targetRank);
    }

    bool RelationshipUseCases::DemoteSpouse(RE::FormID npcFormID) {
        const int curRank = GetSpouseRank(npcFormID);
        if (curRank >= static_cast<int>(State::SpouseHierarchyTable::kSlotCount) - 1) {
            return false;
        }
        return SetSpouseRank(npcFormID, curRank + 1);
    }

    bool RelationshipUseCases::SwapSpouseRanks(RE::FormID spouseA, RE::FormID spouseB) {
        if (spouseA == 0 || spouseB == 0 || spouseA == spouseB) {
            return false;
        }
        const int rankA = GetSpouseRank(spouseA);
        const int rankB = GetSpouseRank(spouseB);
        if (rankA == rankB) {
            return false;
        }
        if (rankA <= 2) {
            return SetSpouseRank(spouseB, rankA);
        }
        if (rankB <= 2) {
            return SetSpouseRank(spouseA, rankB);
        }
        return false;
    }

    void RelationshipUseCases::RecomputeHierarchy() {
        FillHierarchyGaps(0, /*sendEvent=*/false);
    }

    void RelationshipUseCases::OnSpouseAddedToHierarchy(RE::FormID npcFormID) {
        if (npcFormID == 0) {
            return;
        }
        Domain::Relationships::HierarchyMutationOutcome outcome;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            outcome = Domain::Relationships::ApplyOnSpouseAdded(ws, State::ActorId{ npcFormID });
        });
        if (outcome.assignedSlot >= 0) {
            FactionReconciler::GetSingleton().ReconcileActor(npcFormID);
            BuffReconciler::GetSingleton().ReconcilePlayerBuffs();
            GetUiBridgeGateway().OnHierarchyChanged();
        }
    }

    void RelationshipUseCases::OnSpouseRemovedFromHierarchy(RE::FormID npcFormID) {
        if (npcFormID == 0) {
            return;
        }
        Domain::Relationships::HierarchyMutationOutcome outcome;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            outcome = Domain::Relationships::ApplyOnSpouseRemoved(ws, State::ActorId{ npcFormID });
        });
        if (outcome.slotsChanged || !outcome.clearedActors.empty()) {
            ReconcileHierarchySlotsAndEvents(outcome, /*emitModEvents=*/true);
        }
    }

    void RelationshipUseCases::FillHierarchyGaps(RE::FormID excludeNpcFormID, bool sendEvent) {
        Domain::Relationships::HierarchyMutationOutcome outcome;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            outcome = Domain::Relationships::ApplyFillHierarchyGaps(
                ws, State::ActorId{ excludeNpcFormID }, sendEvent);
        });
        if (outcome.slotsChanged || !outcome.clearedActors.empty() || !outcome.dirtyActors.empty()) {
            ReconcileHierarchySlotsAndEvents(outcome, /*emitModEvents=*/sendEvent);
        }
    }

    void RelationshipUseCases::SyncAllHierarchyFactionsAndGlobals() {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        auto& factionRec = FactionReconciler::GetSingleton();
        for (const auto id : snap->hierarchy->Slots()) {
            if (!id.IsNull()) {
                factionRec.ReconcileActor(id.raw());
            }
        }
        GlobalVarReconciler::GetSingleton().ReconcileGlobals();
        BuffReconciler::GetSingleton().ReconcilePlayerBuffs();
    }

    // =========================================================================
    // Marriage Difficulty
    // =========================================================================

    bool RelationshipUseCases::LoadMarriageDifficultyConfig() {
        return Engine::MarriageDifficultyLoader::GetSingleton().LoadConfig();
    }

    Domain::Relationships::MarriageDifficultyInputs RelationshipUseCases::BuildDifficultyInputs(
        RE::Actor* npc,
        float intimacyAdjustment,
        float mostGold,
        float housesOwned,
        float horsesOwned,
        float questsCompleted,
        float dungeonsCleared,
        float dragonSoulsCollected,
        bool playerKiller) const {
        Domain::Relationships::MarriageDifficultyInputs inputs;
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto& fc = FormCache::GetSingleton();

        inputs.npcLevel = npc ? npc->GetLevel() : 1;
        inputs.playerLevel = player ? player->GetLevel() : 1;
        inputs.npcSocialClass = npc ? GetSocialClass(npc->GetFormID()) : Foundation::SocialClass::Working;

        const RE::FormID npcID = npc ? npc->GetFormID() : 0;
        inputs.isJilted = (npcID != 0) && IsJilted(npcID);
        inputs.isDivorced = (npcID != 0) && IsDivorced(npcID);
        inputs.isDating = (npcID != 0) && IsDating(npcID);
        inputs.isLover = (npcID != 0) && IsLover(npcID);
        inputs.playerKiller = playerKiller;

        inputs.marriedCount = static_cast<int>(GetMarriedCount(kPlayerFormID));
        inputs.divorcedCount = static_cast<int>(GetDivorcedCount(kPlayerFormID));

        if (npcID != 0) {
            inputs.relationshipRank =
                RelationshipEngineReconciler::GetSingleton().GetVanillaRelationshipRank(npcID, kPlayerFormID);
        }

        inputs.permanentAffection = (npcID != 0) ? GetPermanentAffection(npcID) : kAffectionDefault;
        inputs.playerSpeechcraft = player ? player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kSpeech) : 15.0f;
        inputs.intimacyAdjustment = intimacyAdjustment;

        inputs.mostGold = mostGold;
        inputs.housesOwned = housesOwned;
        inputs.horsesOwned = horsesOwned;
        inputs.questsCompleted = questsCompleted;
        inputs.dungeonsCleared = dungeonsCleared;
        inputs.dragonSoulsCollected = dragonSoulsCollected;

        inputs.isDragonborn = QuestReachedStage(fc.GetDragonbornQuest(), 90);

        constexpr std::uint32_t kThaneStage = 25;
        const std::array<RE::TESQuest*, 9> thaneQuests = {
            fc.GetEastmarchThane(), fc.GetFalkreathThane(), fc.GetHaafingarThane(),
            fc.GetHjaalmarchThane(), fc.GetPaleThane(), fc.GetReachThane(),
            fc.GetRiftThane(), fc.GetWhiterunThane(), fc.GetWinterholdThane()
        };
        int thaneCount = 0;
        for (auto* quest : thaneQuests) {
            if (QuestReachedStage(quest, kThaneStage)) {
                ++thaneCount;
            }
        }
        inputs.thaneHoldsCount = thaneCount;

        inputs.isGuildLeader =
            QuestReachedStage(fc.GetCompanionsQuest(), 200) ||
            QuestReachedStage(fc.GetCollegeQuest(), 200) ||
            QuestReachedStage(fc.GetThievesQuest(), 40);

        auto fillGuild = [&](Domain::Relationships::GuildIndex idx,
                             RE::TESFaction* faction,
                             bool questBased,
                             RE::TESQuest* quest,
                             std::uint32_t stage) {
            const bool playerIn = questBased
                                      ? QuestReachedStage(quest, stage)
                                      : (player && faction && player->IsInFaction(faction));
            const bool npcIn = npc && faction && npc->IsInFaction(faction);
            inputs.guilds[static_cast<std::size_t>(idx)] = { playerIn, npcIn };
        };

        fillGuild(Domain::Relationships::GuildIndex::Companions, fc.GetCompanionsFaction(), false, nullptr, 0);
        fillGuild(Domain::Relationships::GuildIndex::Thieves, fc.GetThievesFaction(), false, nullptr, 0);
        fillGuild(Domain::Relationships::GuildIndex::Brotherhood, fc.GetBrotherhoodFaction(), false, nullptr, 0);
        fillGuild(Domain::Relationships::GuildIndex::College, fc.GetCollegeFaction(), false, nullptr, 0);
        fillGuild(Domain::Relationships::GuildIndex::Bards, fc.GetBardsFaction(), true, fc.GetBardsQuest(), 300);

        return inputs;
    }

    Domain::Relationships::MarriageDifficultyResult RelationshipUseCases::EvaluateMarriageDifficulty(
        RE::Actor* npc,
        float intimacyAdjustment,
        float mostGold,
        float housesOwned,
        float horsesOwned,
        float questsCompleted,
        float dungeonsCleared,
        float dragonSoulsCollected,
        bool playerKiller) const {
        if (!npc) {
            return {};
        }
        auto& loader = Engine::MarriageDifficultyLoader::GetSingleton();
        if (!loader.IsLoaded()) {
            (void)loader.LoadConfig();
        }
        const auto inputs = BuildDifficultyInputs(
            npc, intimacyAdjustment, mostGold, housesOwned, horsesOwned,
            questsCompleted, dungeonsCleared, dragonSoulsCollected, playerKiller);
        return Domain::Relationships::CalculateMarriageSuccessChance(loader.GetConfig(), inputs);
    }

    float RelationshipUseCases::CalculatePlayerPrestige(float mostGold,
                                                        float housesOwned,
                                                        float horsesOwned,
                                                        float questsCompleted,
                                                        float dungeonsCleared,
                                                        float dragonSoulsCollected) const {
        auto& loader = Engine::MarriageDifficultyLoader::GetSingleton();
        if (!loader.IsLoaded()) {
            (void)loader.LoadConfig();
        }
        const auto inputs = BuildDifficultyInputs(
            nullptr, 0.0f, mostGold, housesOwned, horsesOwned,
            questsCompleted, dungeonsCleared, dragonSoulsCollected, false);
        return Domain::Relationships::CalculatePlayerPrestige(loader.GetConfig(), inputs);
    }

    float RelationshipUseCases::CalculateGuildAlignmentMod(RE::Actor* npc) const {
        if (!npc) {
            return 0.0f;
        }
        auto& loader = Engine::MarriageDifficultyLoader::GetSingleton();
        if (!loader.IsLoaded()) {
            (void)loader.LoadConfig();
        }
        const auto inputs = BuildDifficultyInputs(npc, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false);
        return Domain::Relationships::CalculateGuildAlignment(loader.GetConfig(), inputs);
    }

    // =========================================================================
    // Strict Read-Only JSON Dumps & Diagnostics (CQS)
    // =========================================================================

    nlohmann::json RelationshipUseCases::DumpJson() const {
        return DumpJson(Projection::PerspectiveView::ForDashboard(
            State::WorldStore::GetSingleton().Snapshot(),
            State::SpatialSnapshotStore::GetSingleton().Snapshot(),
            &GetEngineWerewolfStateProvider()));
    }

    nlohmann::json RelationshipUseCases::DumpJson(const Projection::PerspectiveView& view) const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        nlohmann::json root;

        auto packPairKey = [](State::ActorId a, State::ActorId b) noexcept -> std::uint64_t {
            const State::CanonicalPairKey k{ a, b };
            return (static_cast<std::uint64_t>(k.first.raw()) << 32) | static_cast<std::uint64_t>(k.second.raw());
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

        for (const RE::FormID formID : actorsToExport) {
            nlohmann::json p;
            p["formId"] = FormatFormIdUpper(formID);
            if (formID == kPlayerFormID) {
                auto* player = RE::PlayerCharacter::GetSingleton();
                p["name"] = ActorSvc::GetName(kPlayerFormID);
                const std::string pRace = ActorSvc::GetRaceName(kPlayerFormID);
                p["race"] = !pRace.empty() ? pRace : "Nord";
                p["gender"] = (player && player->GetActorBase())
                                  ? (player->GetActorBase()->GetSex() == RE::SEXES::kFemale ? "female" : "male")
                                  : "male";
                p["isChild"] = false;
            } else {
                p["name"] = ActorSvc::GetName(formID);
                p["race"] = ActorSvc::GetRaceName(formID);
                p["isChild"] = IsChildNPC(formID);
                p["gender"] = (ActorSvc::GetSex(formID) == 1) ? "female" : "male";
            }

            if (const auto* stateProf = view.Actors().Find(State::RefId{ formID })) {
                const auto prof = ToPublicProfile(*stateProf);
                p["socialClass"] = {
                    { "id", static_cast<std::uint8_t>(prof.socialClass) },
                    { "name", Foundation::SocialClassToString(prof.socialClass) }
                };
                p["skillType"] = {
                    { "id", static_cast<std::uint8_t>(prof.skillType) },
                    { "name", Foundation::SkillTypeToString(prof.skillType) }
                };
                p["temperament"] = {
                    { "id", static_cast<std::uint8_t>(prof.temperament) },
                    { "name", Foundation::TemperamentToString(prof.temperament) }
                };
                if (prof.originalHome.has_value()) {
                    p["originalHome"] = {
                        { "formId", FormatFormIdUpper(*prof.originalHome) },
                        { "name", FormatCellName(*prof.originalHome) }
                    };
                }
                if (prof.currentHome.has_value()) {
                    p["currentHome"] = {
                        { "formId", FormatFormIdUpper(*prof.currentHome) },
                        { "name", FormatCellName(*prof.currentHome) }
                    };
                }
                if (prof.homeMarker.has_value()) {
                    p["homeMarker"] = FormatFormIdUpper(*prof.homeMarker);
                }
                nlohmann::json kwArray = nlohmann::json::array();
                for (const RE::FormID kwID : prof.addedKeywords) {
                    kwArray.push_back(FormatFormIdUpper(kwID));
                }
                p["addedKeywords"] = kwArray;
            }
            profilesArray.push_back(std::move(p));
        }
        root["npcProfiles"] = std::move(profilesArray);

        // 2. Visible Relationship Pairs, Affairs & Associated Children
        const auto visibleChildren = view.GetVisibleChildren();
        const auto visibleAffairs = view.GetAllVisibleAffairs();
        std::unordered_map<std::uint64_t, std::vector<const Projection::PerceivedAffair*>> affairsByCouple;
        for (const auto& affair : visibleAffairs) {
            affairsByCouple[packPairKey(affair.cheater, affair.deceivedPartner)].push_back(&affair);
        }

        auto& spatialIdx = SpatialWorldIndex::GetSingleton();
        nlohmann::json pairsArray = nlohmann::json::array();
        for (const auto& edge : view.GetVisibleRelationshipEdges()) {
            const RE::FormID actorA = edge.pair.first.raw();
            const RE::FormID actorB = edge.pair.second.raw();

            nlohmann::json pairObj;
            pairObj["actorA"] = { { "formId", FormatFormIdUpper(actorA) }, { "name", ActorSvc::GetName(actorA) } };
            pairObj["actorB"] = { { "formId", FormatFormIdUpper(actorB) }, { "name", ActorSvc::GetName(actorB) } };
            pairObj["status"] = {
                { "id", static_cast<std::uint8_t>(edge.status) },
                { "name", Foundation::RelationshipStatusToString(edge.status) }
            };
            pairObj["originalStatus"] = {
                { "id", static_cast<std::uint8_t>(edge.originalStatus) },
                { "name", Foundation::RelationshipStatusToString(edge.originalStatus) }
            };
            pairObj["isOpen"] = edge.isOpen;

            nlohmann::json histArray = nlohmann::json::array();
            for (const auto& h : edge.statusHistory) {
                nlohmann::json hObj;
                auto fromSt = h.fromStatus;
                if (h.timestamp < 0.0f && fromSt == Foundation::RelationshipStatus::Candidate) {
                    fromSt = Foundation::RelationshipStatus::Unknown;
                }
                hObj["fromStatus"] = {
                    { "id", static_cast<std::uint8_t>(fromSt) },
                    { "name", Foundation::RelationshipStatusToString(fromSt) }
                };
                hObj["toStatus"] = {
                    { "id", static_cast<std::uint8_t>(h.toStatus) },
                    { "name", Foundation::RelationshipStatusToString(h.toStatus) }
                };
                if (std::isnan(h.timestamp) || h.timestamp < 0.0f) {
                    hObj["timestamp"] = -1;
                } else {
                    hObj["timestamp"] = h.timestamp;
                }
                RE::FormID initID = h.initiator.raw();
                if (initID == 0 ||
                    (h.timestamp < 0.0f && actorA != kPlayerFormID && actorB != kPlayerFormID &&
                     initID == kPlayerFormID)) {
                    initID = DetermineDefaultInitiator(actorA, actorB);
                }
                std::string initName = ActorSvc::GetName(initID);
                if (initName.empty() && initID != 0) {
                    initName = std::format("Actor {:08X}", initID);
                }
                hObj["initiator"] = { { "formId", FormatFormIdUpper(initID) }, { "name", initName } };
                histArray.push_back(std::move(hObj));
            }
            pairObj["statusHistory"] = std::move(histArray);

            nlohmann::json cheatArray = nlohmann::json::array();
            if (const auto itAff = affairsByCouple.find(packPairKey(edge.pair.first, edge.pair.second));
                itAff != affairsByCouple.end()) {
                for (const auto* affair : itAff->second) {
                    nlohmann::json cObj;
                    cObj["cheater"] = {
                        { "formId", FormatFormIdUpper(affair->cheater.raw()) },
                        { "name", ActorSvc::GetName(affair->cheater.raw()) }
                    };
                    cObj["victim"] = {
                        { "formId", FormatFormIdUpper(affair->deceivedPartner.raw()) },
                        { "name", ActorSvc::GetName(affair->deceivedPartner.raw()) }
                    };
                    cObj["affairPartner"] = {
                        { "formId", FormatFormIdUpper(affair->paramour.raw()) },
                        { "name", ActorSvc::GetName(affair->paramour.raw()) }
                    };
                    cObj["count"] = affair->incidentCount;
                    cObj["lastTimestamp"] = affair->lastTimestamp;
                    cObj["isSecret"] = affair->isSecret;
                    cObj["affairType"] = {
                        { "id", static_cast<std::uint8_t>(affair->affairType) },
                        { "name", Foundation::AffairTypeToString(affair->affairType) }
                    };
                    cObj["affairStatus"] = {
                        { "id", static_cast<std::uint8_t>(affair->affairStatus) },
                        { "name", Foundation::AffairStatusToString(affair->affairStatus) }
                    };
                    cObj["affairStage"] = {
                        { "id", static_cast<std::uint8_t>(affair->effectiveAffairStage) },
                        { "name", Foundation::AffairStageToString(affair->effectiveAffairStage) }
                    };
                    cObj["stageStartDay"] = affair->stageStartDay;
                    cheatArray.push_back(std::move(cObj));
                }
            }
            pairObj["cheatingRecords"] = std::move(cheatArray);

            nlohmann::json childrenArray = nlohmann::json::array();
            std::unordered_set<RE::FormID> emittedChildIds;
            for (const auto& perceivedChild : visibleChildren) {
                const bool guardianMatch =
                    perceivedChild.guardians.HasGuardian(edge.pair.first) &&
                    perceivedChild.guardians.HasGuardian(edge.pair.second);
                const bool bioMatch =
                    perceivedChild.IsActorPerceivedBioParent(edge.pair.first) &&
                    perceivedChild.IsActorPerceivedBioParent(edge.pair.second);
                if (!guardianMatch && !bioMatch) {
                    continue;
                }
                const RE::FormID cID = perceivedChild.childId.raw();
                if (!emittedChildIds.insert(cID).second) {
                    continue;
                }
                auto* actor = Foundation::HasPhysicalActorRawId(cID)
                                  ? spatialIdx.GetActorFromFormId(cID)
                                  : nullptr;
                const bool hasRealActor = (actor != nullptr && !actor->IsDeleted());

                nlohmann::json cObj;
                cObj["childId"] = FormatFormIdUpper(cID);
                cObj["name"] = perceivedChild.name;
                cObj["timestamp"] = perceivedChild.timestamp;
                cObj["isAdopted"] = perceivedChild.isAdopted;
                cObj["isBiologicalChild"] = perceivedChild.isBiologicalChild;
                cObj["stage"] = {
                    { "id", static_cast<std::uint8_t>(perceivedChild.stage) },
                    { "name", StageToString(perceivedChild.stage) }
                };
                if (!perceivedChild.trainingLocation.empty()) {
                    cObj["trainingLocation"] = perceivedChild.trainingLocation;
                }
                cObj["hasRefID"] = hasRealActor;
                cObj["isEphemeral"] = !hasRealActor ||
                                      perceivedChild.stage == State::ChildStage::Infant ||
                                      perceivedChild.stage == State::ChildStage::Training;
                childrenArray.push_back(std::move(cObj));
            }
            pairObj["children"] = std::move(childrenArray);
            pairsArray.push_back(std::move(pairObj));
        }
        root["relationshipPairs"] = std::move(pairsArray);

        // 3. Visible Children Registry
        const auto& trainingCatalog = Engine::TrainingCatalogLoader::GetSingleton().GetCatalog();
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
                { "formId", FormatFormIdUpper(childID) },
                { "name", perceived.name.empty() ? ActorSvc::GetName(childID) : perceived.name }
            };
            cObj["guardianA"] = {
                { "formId", FormatFormIdUpper(gAID) },
                { "name", gAID == 0 ? "" : ActorSvc::GetName(gAID) }
            };
            cObj["guardianB"] = {
                { "formId", FormatFormIdUpper(gBID) },
                { "name", gBID == 0 ? "" : ActorSvc::GetName(gBID) }
            };
            cObj["timestamp"] = perceived.timestamp;
            cObj["isAdopted"] = perceived.isAdopted;
            cObj["bioMother"] = {
                { "formId", FormatFormIdUpper(bioMotherID) },
                { "name", bioMotherID == 0 ? "" : ActorSvc::GetName(bioMotherID) }
            };
            std::string bioFatherName;
            if (bioFatherID == kUnknownFatherFormID) {
                bioFatherName = "Unknown";
            } else if (bioFatherID != 0) {
                bioFatherName = ActorSvc::GetName(bioFatherID);
            } else if (!perceived.perceivedBioFatherFallbackName.empty()) {
                bioFatherName = perceived.perceivedBioFatherFallbackName;
            }
            cObj["bioFather"] = {
                { "formId", FormatFormIdUpper(bioFatherID) },
                { "name", bioFatherName }
            };
            cObj["presumedFather"] = {
                { "formId", FormatFormIdUpper(presumedFatherID) },
                { "name", presumedFatherID == 0 ? "" : ActorSvc::GetName(presumedFatherID) }
            };
            cObj["isBiologicalChild"] = perceived.isBiologicalChild;
            cObj["conceptionTimestamp"] = perceived.conceptionTimestamp;
            cObj["birthTimestamp"] = perceived.birthTimestamp;
            cObj["stage"] = {
                { "id", static_cast<std::uint8_t>(perceived.stage) },
                { "name", StageToString(perceived.stage) }
            };
            if (!perceived.trainingLocation.empty()) {
                cObj["trainingLocation"] = perceived.trainingLocation;
            }
            if (perceived.trainingClassIndex >= 0) {
                cObj["trainingClassIndex"] = perceived.trainingClassIndex;
                if (const auto* spec = trainingCatalog.FindClass(perceived.trainingClassIndex)) {
                    cObj["trainingClassName"] = spec->className;
                }
            }
            if (perceived.trainingStartGameDay > 0.0f) {
                cObj["trainingStartGameDay"] = perceived.trainingStartGameDay;
            }
            if (!perceived.savedBioTemplateName.empty()) {
                cObj["savedBioTemplateName"] = perceived.savedBioTemplateName;
            }
            if (perceived.skyrimNetUuid != 0) {
                cObj["skyrimNetUuid"] = std::format("{:016X}", perceived.skyrimNetUuid);
            }
            if (rawChild) {
                if (rawChild->savedRaceIndex <= 9) {
                    cObj["savedRaceIndex"] = rawChild->savedRaceIndex;
                }
                if (rawChild->hairColorFormId.raw() != 0) {
                    cObj["hairColorFormId"] = FormatFormIdUpper(rawChild->hairColorFormId.raw());
                }
                if (rawChild->eyesPartFormId.raw() != 0) {
                    cObj["eyesPartFormId"] = FormatFormIdUpper(rawChild->eyesPartFormId.raw());
                }
                if (rawChild->hairPartFormId.raw() != 0) {
                    cObj["hairPartFormId"] = FormatFormIdUpper(rawChild->hairPartFormId.raw());
                }
                if (rawChild->facialHairPartFormId.raw() != 0) {
                    cObj["facialHairPartFormId"] = FormatFormIdUpper(rawChild->facialHairPartFormId.raw());
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

            auto* regActor = Foundation::HasPhysicalActorRawId(childID)
                                 ? spatialIdx.GetActorFromFormId(childID)
                                 : nullptr;
            const bool regHasRealActor = (regActor != nullptr && !regActor->IsDeleted());
            cObj["hasRefID"] = regHasRealActor;
            cObj["isEphemeral"] = !regHasRealActor ||
                                  perceived.stage == State::ChildStage::Unborn ||
                                  perceived.stage == State::ChildStage::Infant ||
                                  perceived.stage == State::ChildStage::Training;

            if (perceived.paternitySecret.has_value()) {
                const auto& sec = *perceived.paternitySecret;
                cObj["paternitySecret"] = {
                    { "isSecret", sec.isSecret },
                    { "isSecretForBioFather", sec.isSecretForBioFather },
                    { "isSecretForPartner", sec.isSecretForPartner },
                    { "discovered", sec.discovered },
                    { "discoveredByPartner", sec.discoveredByPartner },
                    { "discoveredByBioFather", sec.discoveredByBioFather },
                    { "hasPhenotypeMismatch", sec.hasPhenotypeMismatch },
                    { "lastCheckTimestamp", (view.IsOmniscient() && rawChild) ? rawChild->paternitySecret.lastCheckTimestamp : 0.0f },
                    { "bioFatherName", sec.trueBioFatherFallbackName },
                    { "bioFatherRaceFormId", sec.trueBioFatherRaceId.raw() != 0 ? FormatFormIdUpper(sec.trueBioFatherRaceId.raw()) : "" }
                };
            } else {
                cObj["paternitySecret"] = {
                    { "isSecret", false },
                    { "isSecretForBioFather", false },
                    { "isSecretForPartner", false },
                    { "discovered", false },
                    { "discoveredByPartner", false },
                    { "discoveredByBioFather", false },
                    { "hasPhenotypeMismatch", false },
                    { "lastCheckTimestamp", 0.0f },
                    { "bioFatherName", "" },
                    { "bioFatherRaceFormId", "" }
                };
            }
            childrenRegArray.push_back(std::move(cObj));
        }
        root["childrenRegistry"] = std::move(childrenRegArray);

        // 4. Pending Relationships
        nlohmann::json pendingRelsArray = nlohmann::json::array();
        for (const auto& [baseId, relsList] : snap->actors->AllPendingRelationships()) {
            for (const auto& r : relsList) {
                pendingRelsArray.push_back({
                    { "baseFormId", FormatFormIdUpper(baseId.raw()) },
                    { "partnerBaseId", FormatFormIdUpper(r.partnerBaseId.raw()) },
                    { "partnerRefId", FormatFormIdUpper(r.partnerRefId.raw()) },
                    { "targetStatus", { { "id", static_cast<std::uint8_t>(r.targetStatus) },
                                        { "name", Foundation::RelationshipStatusToString(r.targetStatus) } } },
                    { "originalStatus", { { "id", static_cast<std::uint8_t>(r.originalStatus) },
                                          { "name", Foundation::RelationshipStatusToString(r.originalStatus) } } }
                });
            }
        }
        root["pendingRelationships"] = std::move(pendingRelsArray);

        // 5. Pending Children
        nlohmann::json pendingChildrenArray = nlohmann::json::array();
        for (const auto& [cBaseId, cData] : snap->lineage->AllPendingChildren()) {
            pendingChildrenArray.push_back({
                { "childBaseId", FormatFormIdUpper(cBaseId.raw()) },
                { "childRefId", FormatFormIdUpper(cData.childRefId.raw()) },
                { "parentABaseId", FormatFormIdUpper(cData.parentABaseId.raw()) },
                { "parentARefId", FormatFormIdUpper(cData.parentARefId.raw()) },
                { "parentBBaseId", FormatFormIdUpper(cData.parentBBaseId.raw()) },
                { "parentBRefId", FormatFormIdUpper(cData.parentBRefId.raw()) },
                { "isAdopted", cData.isAdopted }
            });
        }
        root["pendingChildren"] = std::move(pendingChildrenArray);

        // 6. Active Pregnancies
        nlohmann::json pregArray = nlohmann::json::array();
        for (const auto& perceived : visibleChildren) {
            if (perceived.stage != State::ChildStage::Unborn) {
                continue;
            }
            const RE::FormID bioMotherID = perceived.bioMother.raw();
            const RE::FormID bioFatherID = perceived.perceivedBioFather.raw();
            const RE::FormID presumedFatherID = perceived.presumedFather.raw();
            std::string pregFatherName;
            if (bioFatherID == kUnknownFatherFormID) {
                pregFatherName = "Unknown";
            } else if (bioFatherID != 0) {
                pregFatherName = ActorSvc::GetName(bioFatherID);
            } else if (!perceived.perceivedBioFatherFallbackName.empty()) {
                pregFatherName = perceived.perceivedBioFatherFallbackName;
            }
            pregArray.push_back({
                { "mother", { { "formId", FormatFormIdUpper(bioMotherID) }, { "name", ActorSvc::GetName(bioMotherID) } } },
                { "isPregnant", true },
                { "conceptionTimestamp", perceived.conceptionTimestamp },
                { "presumedFather", {
                    { "formId", presumedFatherID != 0 ? FormatFormIdUpper(presumedFatherID) : "" },
                    { "name", presumedFatherID != 0 ? ActorSvc::GetName(presumedFatherID) : "" }
                } },
                { "bioFather", { { "formId", FormatFormIdUpper(bioFatherID) }, { "name", pregFatherName } } }
            });
        }
        root["pregnancyState"] = std::move(pregArray);

        // 7. Pending Partner Intents
        nlohmann::json intentsArray = nlohmann::json::array();
        for (const auto& [partnerId, rawIntent] : snap->relationships->AllPendingIntents()) {
            (void)rawIntent;
            const auto intent = view.GetPerceivedPendingIntent(partnerId);
            if (intent.type == Foundation::PartnerIntentType::None) {
                continue;
            }
            intentsArray.push_back({
                { "partner", { { "formId", FormatFormIdUpper(partnerId.raw()) }, { "name", ActorSvc::GetName(partnerId.raw()) } } },
                { "intent", { { "id", static_cast<std::uint8_t>(intent.type) },
                              { "name", Foundation::PartnerIntentTypeToString(intent.type) } } },
                { "createdGameDay", intent.createdGameDay },
                { "relatedLover", {
                    { "formId", !intent.relatedLover.IsNull() ? FormatFormIdUpper(intent.relatedLover.raw()) : "" },
                    { "name", !intent.relatedLover.IsNull() ? ActorSvc::GetName(intent.relatedLover.raw()) : "" }
                } }
            });
        }
        root["pendingPartnerIntents"] = std::move(intentsArray);
        root["nextSyntheticChildId"] = FormatFormIdUpper(snap->lineage->NextSyntheticChildRaw());

        return root;
    }

    nlohmann::json RelationshipUseCases::DumpAffectionJson() const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        auto* calendar = RE::Calendar::GetSingleton();
        const float currentDay = calendar ? calendar->GetDaysPassed() : 0.0f;

        nlohmann::json j;
        j["decayMultiplier"] = snap->settings->affectionDecayMultiplier;

        nlohmann::json permArray = nlohmann::json::array();
        const auto& dayMap = snap->affection->AllLastDays();
        for (const auto& [actorId, val] : snap->affection->AllPermanent()) {
            const RE::FormID formID = actorId.raw();
            const auto dayIt = dayMap.find(actorId);
            permArray.push_back({
                { "formId", FormatFormIdUpper(formID) },
                { "name", ActorSvc::GetName(formID) },
                { "value", val },
                { "lastAffectionDay", (dayIt != dayMap.end()) ? dayIt->second : -1.0f },
                { "daysSinceLastAffection", !calendar
                                                ? 0.0f
                                                : (dayIt != dayMap.end() ? (currentDay - dayIt->second) : kNeverRecordedDaysSince) }
            });
        }
        j["permanentAffection"] = std::move(permArray);

        nlohmann::json dailyArray = nlohmann::json::array();
        for (const auto& [actorId, typeMap] : snap->affection->AllDaily()) {
            const RE::FormID formID = actorId.raw();
            nlohmann::json typesObj = nlohmann::json::object();
            for (const auto& [tName, amt] : typeMap) {
                typesObj[tName] = amt;
            }
            dailyArray.push_back({
                { "formId", FormatFormIdUpper(formID) },
                { "name", ActorSvc::GetName(formID) },
                { "types", std::move(typesObj) }
            });
        }
        j["dailyAffection"] = std::move(dailyArray);

        nlohmann::json minMaxObj = nlohmann::json::object();
        for (const auto& [tName, range] : snap->affection->AllMinMaxByType()) {
            minMaxObj[tName] = { { "min", range.first }, { "max", range.second } };
        }
        j["minMaxByType"] = std::move(minMaxObj);
        return j;
    }

    nlohmann::json RelationshipUseCases::DumpHierarchyJson() const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const auto& slots = snap->hierarchy->Slots();
        nlohmann::json j = nlohmann::json::array();
        for (std::size_t i = 0; i < slots.size(); ++i) {
            const RE::FormID id = slots[i].raw();
            j.push_back({
                { "rank", static_cast<int>(i) },
                { "formId", id != 0 ? FormatFormIdUpper(id) : "" },
                { "name", id != 0 ? ActorSvc::GetName(id) : "" }
            });
        }
        return j;
    }

    void RelationshipUseCases::SyncAllMarriedCouplesChildCustody() {
        const auto preSnap = State::WorldStore::GetSingleton().Snapshot();
        CachedActorMetadataProvider meta;
        meta.CaptureActors(preSnap, {});
        Foundation::DeterministicRng rng(0xC0570D1Au);

        State::WorldMutationResult mut;
        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            mut = ws.SyncAllMarriedCouplesChildCustody(meta, rng);
        });
        ReconcileDirtyActorsAndChildren(mut, /*reconcilePackages=*/true);
    }

    void RelationshipUseCases::UpdateAllSpousesChildrenFaction() {
        for (const RE::FormID spouseId : GetAllMarried(kPlayerFormID)) {
            if (spouseId != 0) {
                FactionReconciler::GetSingleton().ReconcileActor(spouseId);
            }
        }
    }

    void RelationshipUseCases::LogStatistics() const {
        MARAS_LOG_INFO(
            "RelationshipUseCases Stats — Registered={}, Dating={}, Engaged={}, Married={}, Divorced={}, Jilted={}, Lovers={}",
            GetTotalRegisteredCount(),
            GetDatingCount(),
            GetEngagedCount(),
            GetMarriedCount(),
            GetDivorcedCount(),
            GetJiltedCount(),
            GetLoverCount());
    }

    void RelationshipUseCases::LogNPCDetails(RE::FormID npcFormID) const {
        const auto status = GetRelationshipStatus(npcFormID, kPlayerFormID);
        MARAS_LOG_INFO(
            "NPC Details [{:08X} {}]: status={}, class={}, skill={}, temp={}, affection={}, spouseRank={}, cheating={}",
            npcFormID,
            ActorSvc::GetName(npcFormID),
            Foundation::RelationshipStatusToString(status),
            Foundation::SocialClassToString(GetSocialClass(npcFormID)),
            Foundation::SkillTypeToString(GetSkillType(npcFormID)),
            Foundation::TemperamentToString(GetTemperament(npcFormID)),
            GetPermanentAffection(npcFormID),
            GetSpouseRank(npcFormID),
            IsNPCCheating(npcFormID));
    }

}  // namespace MARAS::App
