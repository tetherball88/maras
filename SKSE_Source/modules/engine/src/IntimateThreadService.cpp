#include "maras/engine/IntimateThreadService.h"

#include "maras/domain/relationships/GiftAndIntimacyRules.h"
#include "maras/engine/ActorEngineUtils.h"
#include "maras/engine/ActorService.h"
#include "maras/engine/DeferredActorResolver.h"
#include "maras/engine/EngineEventHooks.h"
#include "maras/engine/EngineLogging.h"
#include "maras/engine/FactionReconciler.h"
#include "maras/engine/FormCache.h"
#include "maras/engine/LocalizationService.h"
#include "maras/engine/SpatialWorldIndex.h"
#include "maras/engine/TaskDispatcher.h"
#include "maras/foundation/DomainEnums.h"
#include "maras/foundation/EnumUtils.h"
#include "maras/foundation/StrongIds.h"
#include "maras/state/WorldStore.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace MARAS {

    namespace {

        [[nodiscard]] bool IsPlayerActiveMainPartner(const State::WorldStateSnapshot& snap, RE::FormID id) noexcept {
            if (id == 0 || id == kPlayerFormID || !snap.relationships) {
                return false;
            }
            return Foundation::IsMainTierStatus(
                snap.relationships->GetStatus(Foundation::ActorId{ id }, Foundation::ActorId::Player()));
        }

        void AppendUniqueRawId(std::vector<RE::FormID>& dst, RE::FormID id) {
            if (id != 0 && std::find(dst.begin(), dst.end(), id) == dst.end()) {
                dst.push_back(id);
            }
        }

        void ApplyCheatingCaughtFaction(std::vector<RE::FormID> actorIds, bool add) {
            if (actorIds.empty()) {
                return;
            }
            auto apply = [actorIds = std::move(actorIds), add]() {
                auto* fac = FormCache::GetSingleton().GetCheatingNpcCaughtFaction();
                if (!fac) {
                    return;
                }
                for (const RE::FormID id : actorIds) {
                    if (id == 0 || id == kPlayerFormID) {
                        continue;
                    }
                    if (add) {
                        Utils::AddToFaction(id, fac, 0);
                    } else {
                        Utils::RemoveFromFaction(id, fac);
                    }
                }
            };
            if (DeferredActorResolver::GetSingleton().CanSyncEngineInline()) {
                apply();
            } else {
                AddCurrentEpochTask("IntimateThreadService::ApplyCheatingCaughtFaction", std::move(apply));
            }
        }

        void RecordCheatingIncidentDirect(RE::FormID cheaterID,
                                          RE::FormID victimID,
                                          RE::FormID affairPartnerID,
                                          bool isSecret) {
            if (cheaterID == 0 || victimID == 0 || affairPartnerID == 0) {
                return;
            }
            if (auto* hooks = Engine::GetEngineEventHooks()) {
                (void)hooks->OnRecordIntimateCheating(cheaterID, victimID, affairPartnerID, isSecret);
            } else {
                MARAS_LOG_WARN(
                    "RecordCheatingIncidentDirect: EngineEventHooks unavailable for cheater={:08X}, victim={:08X}, partner={:08X}",
                    cheaterID, victimID, affairPartnerID);
            }
        }

    }  // namespace

    IntimateThreadService& IntimateThreadService::GetSingleton() {
        static IntimateThreadService instance;
        return instance;
    }

    void IntimateThreadService::Initialize() {
        lastPollTime_ = {};
        // Children flee from the cells where intimate scenes are running.
        ChildrenFleeService::GetSingleton().SetThreatProvider(
            [](ChildrenFleeService::ThreatMap& threats) { GetSingleton().CollectSceneThreats(threats); });
        MARAS_LOG_INFO("IntimateThreadService initialized");
    }

    void IntimateThreadService::Shutdown() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            activeThreads_.clear();
            hasActiveThreads_.store(false, std::memory_order_release);
        }
        lastPollTime_ = {};
        MARAS_LOG_INFO("IntimateThreadService shutdown");
    }

    void IntimateThreadService::ClearAll() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            activeThreads_.clear();
            hasActiveThreads_.store(false, std::memory_order_release);
        }
        lastPollTime_ = {};
        MARAS_LOG_INFO("IntimateThreadService: Cleared all active threads on session reset");
    }

    void IntimateThreadService::Tick(std::chrono::steady_clock::time_point now) {
        if (!hasActiveThreads_.load(std::memory_order_acquire)) {
            return;
        }
        if (now - lastPollTime_ < kPollInterval) {
            return;
        }
        lastPollTime_ = now;
        PollActiveThreads();
    }

    void IntimateThreadService::EvaluateParticipants(const std::vector<RE::FormID>& actorIDs,
                                                     std::vector<PotentialCheater>& outCheaters,
                                                     std::vector<OpenRelationshipWatcher>& outOpenWatchers,
                                                     bool& outHasCheatingPotential,
                                                     bool& outHasOpenRelationshipPotential) {
        outCheaters.clear();
        outOpenWatchers.clear();
        outHasCheatingPotential = false;
        outHasOpenRelationshipPotential = false;

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap || !snap->relationships) {
            return;
        }

        const auto& allEdges = snap->relationships->AllEdges();
        const bool enablePolygamy = snap->settings ? snap->settings->enablePlayerPolygamy : false;

        for (RE::FormID actorID : actorIDs) {
            const Foundation::ActorId actor{ actorID };
            std::vector<RE::FormID> victims;
            std::vector<RE::FormID> openPartners;

            for (const auto& [pairKey, pairData] : allEdges) {
                if (pairKey.Contains(actor) && Foundation::IsMainTierStatus(pairData.status)) {
                    const RE::FormID partnerID = pairKey.Other(actor).raw();
                    if (partnerID == 0) {
                        continue;
                    }

                    // 1. Group scene exemption: partner is participating in the thread
                    if (std::find(actorIDs.begin(), actorIDs.end(), partnerID) != actorIDs.end()) {
                        continue;
                    }

                    // 2. Co-spouse intimacy exemption: if two or more co-spouses of the Player are intimate
                    // with each other (and no non-co-spouse third party is in the thread), it is not cheating on the Player.
                    if (partnerID == kPlayerFormID && IsPlayerActiveMainPartner(*snap, actorID)) {
                        bool hasNonCoSpouseOther = false;
                        for (RE::FormID otherID : actorIDs) {
                            if (otherID == 0 || otherID == actorID || otherID == kPlayerFormID) {
                                continue;
                            }
                            if (!IsPlayerActiveMainPartner(*snap, otherID)) {
                                hasNonCoSpouseOther = true;
                                break;
                            }
                        }
                        if (!hasNonCoSpouseOther) {
                            MARAS_LOG_DEBUG(
                                "EvaluateParticipants: Co-spouse intimacy exemption applied for {:08X} ({}) — all other thread participants are Player co-spouses",
                                actorID, ActorSvc::GetName(actorID));
                            continue;
                        }
                    }

                    // 3. Open relationship
                    if (pairData.isOpen) {
                        openPartners.push_back(partnerID);
                        continue;
                    }

                    // 4. Player polygamy exemption
                    if (actorID == kPlayerFormID && enablePolygamy) {
                        continue;
                    }

                    // Absent committed partner: potential victim of infidelity
                    victims.push_back(partnerID);
                }
            }

            if (!victims.empty()) {
                outCheaters.push_back(PotentialCheater{ actorID, std::move(victims) });
                outHasCheatingPotential = true;
            }

            if (!openPartners.empty()) {
                outOpenWatchers.push_back(OpenRelationshipWatcher{ actorID, std::move(openPartners) });
                outHasOpenRelationshipPotential = true;
            }
        }
    }

    bool IntimateThreadService::RegisterIntimateThread(const std::string& framework,
                                                       std::int32_t threadID,
                                                       const std::vector<RE::Actor*>& actors) {
        if (actors.empty()) {
            MARAS_LOG_WARN("RegisterIntimateThread: Called with 0 actors (framework={}, threadID={})",
                           framework, threadID);
            return false;
        }

        std::vector<RE::FormID> actorIDs;
        actorIDs.reserve(actors.size());
        for (auto* actor : actors) {
            if (actor) {
                const RE::FormID id = actor->GetFormID();
                if (std::find(actorIDs.begin(), actorIDs.end(), id) == actorIDs.end()) {
                    actorIDs.push_back(id);
                }
            }
        }

        if (actorIDs.empty()) {
            MARAS_LOG_WARN("RegisterIntimateThread: No valid actors provided (framework={}, threadID={})",
                           framework, threadID);
            return false;
        }

        std::vector<PotentialCheater> evaluatedCheaters;
        std::vector<OpenRelationshipWatcher> evaluatedOpenWatchers;
        bool hasCheatingPotential = false;
        bool hasOpenRelationshipPotential = false;
        EvaluateParticipants(actorIDs, evaluatedCheaters, evaluatedOpenWatchers, hasCheatingPotential,
                             hasOpenRelationshipPotential);

        std::vector<Foundation::ActorId> strongActorIds;
        std::vector<Foundation::ActorId> engineFamilyWithPlayer;
        strongActorIds.reserve(actorIDs.size());
        for (const RE::FormID id : actorIDs) {
            strongActorIds.emplace_back(id);
            if (id != kPlayerFormID && Utils::AreVanillaEngineFamily(id, kPlayerFormID)) {
                engineFamilyWithPlayer.emplace_back(id);
            }
        }
        Domain::Relationships::IntimateThreadParticipantEvaluation participantEval{};
        if (const auto snap = State::WorldStore::GetSingleton().Snapshot()) {
            participantEval = Domain::Relationships::EvaluateIntimateThreadParticipants(
                *snap,
                strongActorIds,
                hasCheatingPotential || hasOpenRelationshipPotential,
                engineFamilyWithPlayer);
        }

        std::vector<RE::FormID> actorsToAddFaction;
        ThreadKey key{ framework, threadID };

        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = activeThreads_.find(key);
            if (it != activeThreads_.end()) {
                // Update existing thread actors and potential cheaters/open watchers while preserving existing sexualPairs/hadSex
                it->second.actorIDs = std::move(actorIDs);
                it->second.potentialCheaters = std::move(evaluatedCheaters);
                it->second.openRelationshipWatchers = std::move(evaluatedOpenWatchers);
                it->second.hasCheatingPotential = hasCheatingPotential;
                it->second.hasOpenRelationshipPotential = hasOpenRelationshipPotential;
                for (const auto partnerId : participantEval.playerIntimacyPartners) {
                    AppendUniqueRawId(it->second.playerIntimacyPartners, partnerId.raw());
                }
                for (const auto cheaterId : participantEval.cheatersCaughtMidAct) {
                    const RE::FormID rawId = cheaterId.raw();
                    if (std::find(it->second.caughtMidActFactionActors.begin(),
                                  it->second.caughtMidActFactionActors.end(),
                                  rawId) == it->second.caughtMidActFactionActors.end()) {
                        it->second.caughtMidActFactionActors.push_back(rawId);
                        actorsToAddFaction.push_back(rawId);
                    }
                }
                MARAS_LOG_INFO(
                    "RegisterIntimateThread: Updated existing thread {} #{} (participants={}, hasCheaters={}, hasOpenWatchers={}, playerPartners={}, caughtFactions={})",
                    framework, threadID, it->second.actorIDs.size(), hasCheatingPotential,
                    hasOpenRelationshipPotential, it->second.playerIntimacyPartners.size(),
                    it->second.caughtMidActFactionActors.size());
            } else {
                TrackedIntimateThread thread;
                thread.framework = framework;
                thread.threadID = threadID;
                thread.actorIDs = std::move(actorIDs);
                thread.potentialCheaters = std::move(evaluatedCheaters);
                thread.openRelationshipWatchers = std::move(evaluatedOpenWatchers);
                for (const auto partnerId : participantEval.playerIntimacyPartners) {
                    AppendUniqueRawId(thread.playerIntimacyPartners, partnerId.raw());
                }
                for (const auto cheaterId : participantEval.cheatersCaughtMidAct) {
                    const RE::FormID rawId = cheaterId.raw();
                    AppendUniqueRawId(thread.caughtMidActFactionActors, rawId);
                }
                actorsToAddFaction = thread.caughtMidActFactionActors;
                thread.hasCheatingPotential = hasCheatingPotential;
                thread.hasOpenRelationshipPotential = hasOpenRelationshipPotential;
                auto [insertedIt, _] = activeThreads_.emplace(key, std::move(thread));
                MARAS_LOG_INFO(
                    "RegisterIntimateThread: Registered new thread {} #{} (participants={}, hasCheaters={}, hasOpenWatchers={}, playerPartners={}, caughtFactions={})",
                    framework, threadID, insertedIt->second.actorIDs.size(), hasCheatingPotential,
                    hasOpenRelationshipPotential, insertedIt->second.playerIntimacyPartners.size(),
                    insertedIt->second.caughtMidActFactionActors.size());
            }

            hasActiveThreads_.store(!activeThreads_.empty(), std::memory_order_release);
        }

        ApplyCheatingCaughtFaction(std::move(actorsToAddFaction), /*add=*/true);
        ChildrenFleeService::GetSingleton().QueueRefresh();

        return hasCheatingPotential || hasOpenRelationshipPotential;
    }

    void IntimateThreadService::ReportIntimateInteraction(const std::string& framework,
                                                          std::int32_t threadID,
                                                          RE::Actor* actorA,
                                                          RE::Actor* actorB) {
        if (!actorA || !actorB || actorA == actorB) {
            return;
        }

        const RE::FormID idA = actorA->GetFormID();
        const RE::FormID idB = actorB->GetFormID();
        if (idA == idB) {
            return;
        }

        ThreadKey key{ framework, threadID };
        std::vector<RE::FormID> currentActorIDs;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = activeThreads_.find(key);
            if (it == activeThreads_.end()) {
                MARAS_LOG_DEBUG("ReportIntimateInteraction: Thread {} #{} not registered, skipping",
                                framework, threadID);
                return;
            }

            // Deduplicate: if this sexual pair was already recorded in this thread, skip re-evaluating
            // so we don't increment the affair incident count multiple times across scenes in the same session.
            SexualPairRecord pairRec{ idA, idB };
            if (std::find(it->second.sexualPairs.begin(), it->second.sexualPairs.end(), pairRec) !=
                it->second.sexualPairs.end()) {
                return;
            }
            it->second.sexualPairs.push_back(pairRec);

            // Ensure both interacting actors are recorded in the thread's participant list
            // even if one joined dynamically before RegisterIntimateThread was re-called.
            if (std::find(it->second.actorIDs.begin(), it->second.actorIDs.end(), idA) ==
                it->second.actorIDs.end()) {
                it->second.actorIDs.push_back(idA);
            }
            if (std::find(it->second.actorIDs.begin(), it->second.actorIDs.end(), idB) ==
                it->second.actorIDs.end()) {
                it->second.actorIDs.push_back(idB);
            }
            currentActorIDs = it->second.actorIDs;
        }

        // Re-evaluate participant relationships against live WorldStore state so any
        // relationship changes that occurred since RegisterIntimateThread are reflected accurately.
        std::vector<PotentialCheater> refreshedCheaters;
        std::vector<OpenRelationshipWatcher> refreshedOpenWatchers;
        bool hasCheatingPotential = false;
        bool hasOpenRelationshipPotential = false;
        EvaluateParticipants(currentActorIDs, refreshedCheaters, refreshedOpenWatchers, hasCheatingPotential,
                             hasOpenRelationshipPotential);

        TrackedIntimateThread threadCopy;
        bool found = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = activeThreads_.find(key);
            if (it != activeThreads_.end()) {
                it->second.potentialCheaters = std::move(refreshedCheaters);
                it->second.openRelationshipWatchers = std::move(refreshedOpenWatchers);
                it->second.hasCheatingPotential = hasCheatingPotential;
                it->second.hasOpenRelationshipPotential = hasOpenRelationshipPotential;
                threadCopy = it->second;
                found = true;
                hasActiveThreads_.store(!activeThreads_.empty(), std::memory_order_release);
            }
        }

        if (!found || (!threadCopy.hasCheatingPotential && !threadCopy.hasOpenRelationshipPotential)) {
            return;
        }

        AddCurrentEpochTask(
            "IntimateThreadService::ReportIntimateInteraction",
            [this, key = std::move(key), idA, idB, threadCopy = std::move(threadCopy)]() {
                std::lock_guard papyrusLock(GetPapyrusMutex());
                const auto snap = State::WorldStore::GetSingleton().Snapshot();
                if (!snap || !snap->relationships) {
                    return;
                }

                const bool enablePolygamy = snap->settings ? snap->settings->enablePlayerPolygamy : false;
                auto* actorA = RE::TESForm::LookupByID<RE::Actor>(idA);
                auto* actorB = RE::TESForm::LookupByID<RE::Actor>(idB);

                // 1. Process potential cheating
                if (threadCopy.hasCheatingPotential) {
                    auto processCheater = [&](RE::Actor* cheaterActor, RE::Actor* partnerActor,
                                              RE::FormID cheaterID, RE::FormID partnerID) {
                        if (cheaterID == kPlayerFormID && enablePolygamy) {
                            return;
                        }

                        for (const auto& pc : threadCopy.potentialCheaters) {
                            if (pc.cheaterID != cheaterID) {
                                continue;
                            }

                            for (RE::FormID victimID : pc.cachedVictimIDs) {
                                if (victimID == 0 || victimID == partnerID || victimID == cheaterID) {
                                    continue;
                                }
                                if (std::find(threadCopy.actorIDs.begin(), threadCopy.actorIDs.end(), victimID) !=
                                    threadCopy.actorIDs.end()) {
                                    continue;
                                }
                                // Co-spouse intimacy exemption: two co-spouses of the Player being intimate is never cheating on the Player
                                if (victimID == kPlayerFormID &&
                                    IsPlayerActiveMainPartner(*snap, cheaterID) &&
                                    IsPlayerActiveMainPartner(*snap, partnerID)) {
                                    continue;
                                }

                                const auto* pairData = snap->relationships->FindEdge(
                                    Foundation::ActorId{ cheaterID }, Foundation::ActorId{ victimID });
                                if (!pairData || !Foundation::IsMainTierStatus(pairData->status) ||
                                    pairData->isOpen) {
                                    continue;
                                }

                                auto* victimActor = RE::TESForm::LookupByID<RE::Actor>(victimID);
                                const bool isVictimPresent = CheckVictimExposure(victimActor, cheaterActor);

                                if (isVictimPresent) {
                                    RecordCheatingIncidentDirect(cheaterID, victimID, partnerID, /*isSecret=*/false);
                                    {
                                        std::lock_guard<std::mutex> lock(mutex_);
                                        auto tIt = activeThreads_.find(key);
                                        if (tIt != activeThreads_.end()) {
                                            tIt->second.caughtEvents.insert(
                                                CaughtEventKey{ cheaterID, victimID, partnerID });
                                        }
                                    }
                                    MARAS_LOG_INFO(
                                        "ReportIntimateInteraction: Caught in the act! {:08X} with {:08X} exposed to victim {:08X}",
                                        cheaterID, partnerID, victimID);

                                    if (victimID == kPlayerFormID) {
                                        std::string cName = cheaterActor ? cheaterActor->GetDisplayFullName()
                                                                         : ActorSvc::GetName(cheaterID);
                                        std::string pName = partnerActor ? partnerActor->GetDisplayFullName()
                                                                         : ActorSvc::GetName(partnerID);
                                        std::string msg = TrFmt("hud.caught_cheating",
                                                                { { "cheater", cName }, { "partner", pName } });
                                        QueueHUDMessage(std::move(msg));
                                    }

                                    if (victimActor && cheaterActor) {
                                        SendCheatingCaughtEvent(victimActor, cheaterActor, partnerActor,
                                                                key.framework, key.threadID, /*isWalkIn=*/false,
                                                                partnerID);
                                    }
                                } else {
                                    RecordCheatingIncidentDirect(cheaterID, victimID, partnerID, /*isSecret=*/true);
                                    MARAS_LOG_INFO(
                                        "ReportIntimateInteraction: Secret affair registered: {:08X} cheated on {:08X} with {:08X}",
                                        cheaterID, victimID, partnerID);
                                }
                            }
                        }
                    };

                    processCheater(actorA, actorB, idA, idB);
                    processCheater(actorB, actorA, idB, idA);
                }

                // 2. Process open relationship detection
                if (threadCopy.hasOpenRelationshipPotential) {
                    auto processOpenWatcher = [&](RE::Actor* partnerActor, RE::Actor* sexPartnerActor,
                                                  RE::FormID partnerID, RE::FormID sexPartnerID) {
                        for (const auto& ow : threadCopy.openRelationshipWatchers) {
                            if (ow.actorID != partnerID) {
                                continue;
                            }

                            for (RE::FormID witnessID : ow.cachedPartnerIDs) {
                                if (witnessID == 0 || witnessID == sexPartnerID || witnessID == partnerID) {
                                    continue;
                                }
                                if (std::find(threadCopy.actorIDs.begin(), threadCopy.actorIDs.end(), witnessID) !=
                                    threadCopy.actorIDs.end()) {
                                    continue;
                                }
                                // Co-spouse intimacy exemption
                                if (witnessID == kPlayerFormID &&
                                    IsPlayerActiveMainPartner(*snap, partnerID) &&
                                    IsPlayerActiveMainPartner(*snap, sexPartnerID)) {
                                    continue;
                                }

                                const auto* pairData = snap->relationships->FindEdge(
                                    Foundation::ActorId{ partnerID }, Foundation::ActorId{ witnessID });
                                if (!pairData || !Foundation::IsMainTierStatus(pairData->status) ||
                                    !pairData->isOpen) {
                                    continue;
                                }

                                auto* witnessActor = RE::TESForm::LookupByID<RE::Actor>(witnessID);
                                const bool isWitnessPresent = CheckVictimExposure(witnessActor, partnerActor);

                                if (isWitnessPresent) {
                                    CaughtEventKey witnessKey{ partnerID, witnessID, sexPartnerID };
                                    bool alreadySent = false;
                                    {
                                        std::lock_guard<std::mutex> lock(mutex_);
                                        auto tIt = activeThreads_.find(key);
                                        if (tIt != activeThreads_.end()) {
                                            if (tIt->second.openRelationshipWitnessEvents.contains(witnessKey)) {
                                                alreadySent = true;
                                            } else {
                                                tIt->second.openRelationshipWitnessEvents.insert(witnessKey);
                                            }
                                        }
                                    }

                                    if (!alreadySent && witnessActor && partnerActor && sexPartnerActor) {
                                        MARAS_LOG_INFO(
                                            "ReportIntimateInteraction: Open relationship witnessed! Partner {:08X} with {:08X} witnessed by {:08X}",
                                            partnerID, sexPartnerID, witnessID);

                                        SendOpenRelationshipWalkInEvent(witnessActor, partnerActor, sexPartnerActor,
                                                                         key.framework, key.threadID,
                                                                         /*isWalkIn=*/false);
                                    }
                                }
                            }
                        }
                    };

                    processOpenWatcher(actorA, actorB, idA, idB);
                    processOpenWatcher(actorB, actorA, idB, idA);
                }
            });
    }

    bool IntimateThreadService::IsIntimateThreadTracked(const std::string& framework, std::int32_t threadID) const {
        ThreadKey key{ framework, threadID };
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = activeThreads_.find(key);
        return it != activeThreads_.end() &&
               (it->second.hasCheatingPotential || it->second.hasOpenRelationshipPotential);
    }

    bool IntimateThreadService::HasIntimateThreadPlayerParticipants(const std::string& framework,
                                                                    std::int32_t threadID) const {
        ThreadKey key{ framework, threadID };
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = activeThreads_.find(key);
        return it != activeThreads_.end() && !it->second.playerIntimacyPartners.empty();
    }

    void IntimateThreadService::MarkIntimateThreadSexual(const std::string& framework, std::int32_t threadID) {
        ThreadKey key{ framework, threadID };
        std::lock_guard<std::mutex> lock(mutex_);
        if (auto it = activeThreads_.find(key); it != activeThreads_.end()) {
            it->second.hadSex = true;
        }
    }

    void IntimateThreadService::ClearIntimateThread(const std::string& framework,
                                                    std::int32_t threadID,
                                                    bool hadSex) {
        ThreadKey key{ framework, threadID };
        bool found = false;
        bool finalHadSex = hadSex;
        std::vector<RE::FormID> recordedPartners;
        std::vector<RE::FormID> actorsToClearFaction;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = activeThreads_.find(key);
            if (it != activeThreads_.end()) {
                found = true;
                finalHadSex = it->second.hadSex || hadSex;
                recordedPartners = std::move(it->second.playerIntimacyPartners);
                std::vector<RE::FormID> candidateClear = std::move(it->second.caughtMidActFactionActors);
                activeThreads_.erase(it);
                hasActiveThreads_.store(!activeThreads_.empty(), std::memory_order_release);

                for (const RE::FormID actorId : candidateClear) {
                    bool stillCaughtElsewhere = false;
                    for (const auto& [otherKey, otherThread] : activeThreads_) {
                        (void)otherKey;
                        if (std::find(otherThread.caughtMidActFactionActors.begin(),
                                      otherThread.caughtMidActFactionActors.end(),
                                      actorId) != otherThread.caughtMidActFactionActors.end()) {
                            stillCaughtElsewhere = true;
                            break;
                        }
                    }
                    if (!stillCaughtElsewhere) {
                        actorsToClearFaction.push_back(actorId);
                    }
                }
            }
        }

        if (!found) {
            MARAS_LOG_DEBUG("ClearIntimateThread: Thread {} #{} already cleared", framework, threadID);
            return;
        }

        ApplyCheatingCaughtFaction(std::move(actorsToClearFaction), /*add=*/false);
        if (auto* hooks = Engine::GetEngineEventHooks()) {
            hooks->OnIntimateThreadEnded(recordedPartners, finalHadSex);
        }
        MARAS_LOG_INFO("ClearIntimateThread: Cleared thread {} #{} (partners={}, hadSex={})",
                       framework, threadID, recordedPartners.size(), finalHadSex);
        ChildrenFleeService::GetSingleton().QueueRefresh();
    }

    void IntimateThreadService::CollectSceneThreats(ChildrenFleeService::ThreatMap& threats) {
        std::vector<RE::FormID> actorIDs;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& [key, thread] : activeThreads_) {
                (void)key;
                actorIDs.insert(actorIDs.end(), thread.actorIDs.begin(), thread.actorIDs.end());
            }
        }
        for (RE::FormID id : actorIDs) {
            auto* participant = RE::TESForm::LookupByID<RE::Actor>(id);
            auto* cell = (participant && !participant->IsDeleted() && participant->Is3DLoaded())
                             ? participant->GetParentCell()
                             : nullptr;
            if (cell) {
                threats[cell->GetFormID()].push_back(participant->GetPosition());
            }
        }
    }

    bool IntimateThreadService::CheckVictimExposure(RE::Actor* victimActor, RE::Actor* cheaterActor) {
        if (!victimActor || !cheaterActor || victimActor == cheaterActor) {
            return false;
        }
        if (!victimActor->Is3DLoaded() || !cheaterActor->Is3DLoaded()) {
            return false;
        }
        if (victimActor->IsDead()) {
            return false;
        }
        auto sitSleep = victimActor->GetSitSleepState();
        if (sitSleep == RE::SIT_SLEEP_STATE::kIsSleeping ||
            sitSleep == RE::SIT_SLEEP_STATE::kWantToSleep ||
            sitSleep == RE::SIT_SLEEP_STATE::kWaitingForSleepAnim) {
            return false;
        }
        if (victimActor->GetParentCell() != cheaterActor->GetParentCell()) {
            return false;
        }

        const float dist = victimActor->GetPosition().GetDistance(cheaterActor->GetPosition());
        if (dist <= kCloseProximityDistance) {
            return true;
        }

        if (dist <= kVisualDistance) {
            bool hasLOS = false;
            if (victimActor->HasLineOfSight(cheaterActor, hasLOS) && hasLOS) {
                return true;
            }
        }
        return false;
    }

    void IntimateThreadService::PollActiveThreads() {
        if (auto* ui = RE::UI::GetSingleton(); ui && ui->GameIsPaused()) {
            return;
        }

        // Safety net for lost framework end events: ask Papyrus (which can query the framework API)
        // to confirm each tracked thread is still running. Papyrus clears the ones that are gone.
        std::vector<ThreadKey> trackedKeys;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            trackedKeys.reserve(activeThreads_.size());
            for (const auto& [key, thread] : activeThreads_) {
                (void)thread;
                trackedKeys.push_back(key);
            }
        }
        if (!trackedKeys.empty()) {
            auto* player = RE::PlayerCharacter::GetSingleton();
            const RE::FormID playerID = player ? player->GetFormID() : 0;
            for (const auto& key : trackedKeys) {
                EmitModCallbackEvent("maras_verify_intimate_thread",
                                     key.framework,
                                     static_cast<float>(key.threadID),
                                     playerID);
            }
        }

        std::vector<TrackedIntimateThread> threadsSnapshot;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            threadsSnapshot.reserve(activeThreads_.size());
            for (const auto& [key, thread] : activeThreads_) {
                (void)key;
                if ((thread.hasCheatingPotential || thread.hasOpenRelationshipPotential) &&
                    !thread.sexualPairs.empty()) {
                    threadsSnapshot.push_back(thread);
                }
            }
        }

        if (threadsSnapshot.empty()) {
            return;
        }

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap || !snap->relationships || !snap->affairs) {
            return;
        }

        const bool enablePolygamy = snap->settings ? snap->settings->enablePlayerPolygamy : false;

        for (const auto& thread : threadsSnapshot) {
            ThreadKey threadKey{ thread.framework, thread.threadID };

            // 1. Evaluate Cheaters
            for (const auto& pc : thread.potentialCheaters) {
                if (pc.cheaterID == kPlayerFormID && enablePolygamy) {
                    continue;
                }

                // Find all partners this cheater engaged in sexual activity with in this thread
                std::unordered_set<RE::FormID> activeAffairPartners;
                for (const auto& sp : thread.sexualPairs) {
                    if (sp.actorA == pc.cheaterID) {
                        activeAffairPartners.insert(sp.actorB);
                    } else if (sp.actorB == pc.cheaterID) {
                        activeAffairPartners.insert(sp.actorA);
                    }
                }

                if (activeAffairPartners.empty()) {
                    continue;  // Scene hasn't reached sexual activity for this actor yet
                }

                auto* cheaterActor = RE::TESForm::LookupByID<RE::Actor>(pc.cheaterID);
                if (!cheaterActor) {
                    continue;
                }

                for (RE::FormID victimID : pc.cachedVictimIDs) {
                    if (victimID == 0 || victimID == pc.cheaterID) {
                        continue;
                    }
                    if (std::find(thread.actorIDs.begin(), thread.actorIDs.end(), victimID) !=
                        thread.actorIDs.end()) {
                        continue;
                    }

                    // Re-verify that (cheaterID, victimID) is still a closed main-tier relationship
                    const auto* pairData = snap->relationships->FindEdge(
                        Foundation::ActorId{ pc.cheaterID }, Foundation::ActorId{ victimID });
                    if (!pairData || !Foundation::IsMainTierStatus(pairData->status) || pairData->isOpen) {
                        continue;
                    }

                    auto* victimActor = RE::TESForm::LookupByID<RE::Actor>(victimID);
                    if (!CheckVictimExposure(victimActor, cheaterActor)) {
                        continue;
                    }

                    // Victim is present and has LOS or close proximity
                    for (RE::FormID affairPartnerID : activeAffairPartners) {
                        if (affairPartnerID == 0 || affairPartnerID == victimID || affairPartnerID == pc.cheaterID) {
                            continue;
                        }
                        if (victimID == kPlayerFormID &&
                            IsPlayerActiveMainPartner(*snap, pc.cheaterID) &&
                            IsPlayerActiveMainPartner(*snap, affairPartnerID)) {
                            continue;
                        }

                        CaughtEventKey caughtKey{ pc.cheaterID, victimID, affairPartnerID };

                        // Deduplicate within the same thread session
                        {
                            std::lock_guard<std::mutex> lock(mutex_);
                            auto it = activeThreads_.find(threadKey);
                            if (it != activeThreads_.end()) {
                                if (it->second.caughtEvents.contains(caughtKey)) {
                                    continue;
                                }
                                it->second.caughtEvents.insert(caughtKey);
                            }
                        }

                        // Check if the affair is actually secret from this victim
                        bool isSecretAffair = false;
                        const Foundation::CanonicalPairKey couplePair{
                            Foundation::ActorId{ pc.cheaterID },
                            Foundation::ActorId{ victimID },
                        };
                        for (const auto& rec : snap->affairs->GetForCouplePair(couplePair)) {
                            if (rec.cheater.raw() == pc.cheaterID &&
                                rec.paramour.raw() == affairPartnerID &&
                                rec.affairStatus == State::AffairStatus::Ongoing &&
                                rec.isSecret) {
                                isSecretAffair = true;
                                break;
                            }
                        }

                        if (!isSecretAffair) {
                            // Affair was already discovered / known, do not re-expose or re-log
                            continue;
                        }

                        MARAS_LOG_INFO(
                            "PollActiveThreads: Walk-in caught! Cheater {:08X} with {:08X} caught by victim {:08X} in thread {} #{}",
                            pc.cheaterID, affairPartnerID, victimID, thread.framework, thread.threadID);

                        auto* partnerActor = RE::TESForm::LookupByID<RE::Actor>(affairPartnerID);
                        if (victimID == kPlayerFormID) {
                            std::string cName = cheaterActor->GetDisplayFullName();
                            std::string pName = partnerActor ? partnerActor->GetDisplayFullName()
                                                             : ActorSvc::GetName(affairPartnerID);
                            std::string msg = TrFmt("hud.caught_cheating",
                                                    { { "cheater", cName }, { "partner", pName } });
                            QueueHUDMessage(std::move(msg));
                        }

                        // Notify Layer 4 + emit maras_cheating_caught event with victim as sender
                        SendCheatingCaughtEvent(victimActor, cheaterActor, partnerActor, thread.framework,
                                                thread.threadID, /*isWalkIn=*/true, affairPartnerID);
                    }
                }
            }

            // 2. Evaluate Open Relationship Watchers
            for (const auto& ow : thread.openRelationshipWatchers) {
                // Find all partners this actor engaged in sexual activity with in this thread
                std::unordered_set<RE::FormID> activeSexPartners;
                for (const auto& sp : thread.sexualPairs) {
                    if (sp.actorA == ow.actorID) {
                        activeSexPartners.insert(sp.actorB);
                    } else if (sp.actorB == ow.actorID) {
                        activeSexPartners.insert(sp.actorA);
                    }
                }

                if (activeSexPartners.empty()) {
                    continue;  // Scene hasn't reached sexual activity for this actor yet
                }

                auto* partnerActor = RE::TESForm::LookupByID<RE::Actor>(ow.actorID);
                if (!partnerActor) {
                    continue;
                }

                for (RE::FormID witnessID : ow.cachedPartnerIDs) {
                    if (witnessID == 0 || witnessID == ow.actorID) {
                        continue;
                    }
                    if (std::find(thread.actorIDs.begin(), thread.actorIDs.end(), witnessID) !=
                        thread.actorIDs.end()) {
                        continue;
                    }

                    // Re-verify that (ow.actorID, witnessID) is still an open main-tier relationship
                    const auto* pairData = snap->relationships->FindEdge(
                        Foundation::ActorId{ ow.actorID }, Foundation::ActorId{ witnessID });
                    if (!pairData || !Foundation::IsMainTierStatus(pairData->status) || !pairData->isOpen) {
                        continue;
                    }

                    auto* witnessActor = RE::TESForm::LookupByID<RE::Actor>(witnessID);
                    if (!CheckVictimExposure(witnessActor, partnerActor)) {
                        continue;
                    }

                    // Open partner is present and has LOS or close proximity
                    for (RE::FormID sexPartnerID : activeSexPartners) {
                        if (sexPartnerID == 0 || sexPartnerID == witnessID || sexPartnerID == ow.actorID) {
                            continue;
                        }
                        if (witnessID == kPlayerFormID &&
                            IsPlayerActiveMainPartner(*snap, ow.actorID) &&
                            IsPlayerActiveMainPartner(*snap, sexPartnerID)) {
                            continue;
                        }

                        CaughtEventKey witnessKey{ ow.actorID, witnessID, sexPartnerID };

                        // Deduplicate within the same thread session
                        {
                            std::lock_guard<std::mutex> lock(mutex_);
                            auto it = activeThreads_.find(threadKey);
                            if (it != activeThreads_.end()) {
                                if (it->second.openRelationshipWitnessEvents.contains(witnessKey)) {
                                    continue;
                                }
                                it->second.openRelationshipWitnessEvents.insert(witnessKey);
                            }
                        }

                        auto* sexPartnerActor = RE::TESForm::LookupByID<RE::Actor>(sexPartnerID);
                        MARAS_LOG_INFO(
                            "PollActiveThreads: Open relationship walk-in detected! Partner {:08X} with {:08X} detected by partner {:08X} in thread {} #{}",
                            ow.actorID, sexPartnerID, witnessID, thread.framework, thread.threadID);

                        // Send Papyrus event with witness as sender
                        SendOpenRelationshipWalkInEvent(witnessActor, partnerActor, sexPartnerActor,
                                                        thread.framework, thread.threadID, /*isWalkIn=*/true);
                    }
                }
            }
        }
    }

    void IntimateThreadService::SendCheatingCaughtEvent(RE::Actor* victimActor,
                                                        RE::Actor* cheaterActor,
                                                        RE::Actor* partnerActor,
                                                        const std::string& framework,
                                                        std::int32_t threadID,
                                                        bool isWalkIn,
                                                        RE::FormID fallbackPartnerID) {
        if (!victimActor) {
            return;
        }

        const RE::FormID victimID = victimActor->GetFormID();
        const RE::FormID cheaterID = cheaterActor ? cheaterActor->GetFormID() : 0;
        const RE::FormID partnerID = partnerActor ? partnerActor->GetFormID() : fallbackPartnerID;

        nlohmann::json payload;
        payload["victim"] = fmt::format("0x{:08X}", victimID);
        payload["victimName"] = victimActor->GetDisplayFullName();
        payload["cheater"] = fmt::format("0x{:08X}", cheaterID);
        payload["cheaterName"] = cheaterActor ? cheaterActor->GetDisplayFullName() : ActorSvc::GetName(cheaterID);
        payload["affairPartner"] = fmt::format("0x{:08X}", partnerID);
        payload["affairPartnerName"] = partnerActor ? partnerActor->GetDisplayFullName() : ActorSvc::GetName(partnerID);
        payload["framework"] = framework;
        payload["threadID"] = threadID;
        payload["isWalkIn"] = isWalkIn;

        std::string jsonStr = payload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        EmitModCallbackEvent("maras_cheating_caught", std::move(jsonStr), static_cast<float>(threadID), victimID);
        MARAS_LOG_INFO(
            "IntimateThreadService: Sent maras_cheating_caught event: victim={:08X}, cheater={:08X}, partner={:08X}, walkIn={}",
            victimID, cheaterID, partnerID, isWalkIn);

        if (auto* hooks = Engine::GetEngineEventHooks()) {
            hooks->OnIntimateCheatingCaught(cheaterID, victimID, partnerID, framework, threadID, isWalkIn);
        }

        if (victimID == kPlayerFormID && cheaterID != 0 && cheaterID != kPlayerFormID) {
            bool needAddFaction = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (auto tIt = activeThreads_.find(ThreadKey{ framework, threadID });
                    tIt != activeThreads_.end()) {
                    if (std::find(tIt->second.caughtMidActFactionActors.begin(),
                                  tIt->second.caughtMidActFactionActors.end(),
                                  cheaterID) == tIt->second.caughtMidActFactionActors.end()) {
                        tIt->second.caughtMidActFactionActors.push_back(cheaterID);
                        needAddFaction = true;
                    }
                }
            }
            if (needAddFaction) {
                ApplyCheatingCaughtFaction({ cheaterID }, /*add=*/true);
            }
        }
    }

    void IntimateThreadService::SendOpenRelationshipWalkInEvent(RE::Actor* witnessActor,
                                                                RE::Actor* partnerActor,
                                                                RE::Actor* sexPartnerActor,
                                                                const std::string& framework,
                                                                std::int32_t threadID,
                                                                bool isWalkIn) {
        if (!witnessActor) {
            return;
        }

        const RE::FormID witnessID = witnessActor->GetFormID();
        const RE::FormID partnerID = partnerActor ? partnerActor->GetFormID() : 0;
        const RE::FormID sexPartnerID = sexPartnerActor ? sexPartnerActor->GetFormID() : 0;

        nlohmann::json payload;
        payload["witness"] = fmt::format("0x{:08X}", witnessID);
        payload["witnessName"] = witnessActor->GetDisplayFullName();
        payload["partner"] = fmt::format("0x{:08X}", partnerID);
        payload["partnerName"] = partnerActor ? partnerActor->GetDisplayFullName() : ActorSvc::GetName(partnerID);
        payload["sexPartner"] = fmt::format("0x{:08X}", sexPartnerID);
        payload["sexPartnerName"] =
            sexPartnerActor ? sexPartnerActor->GetDisplayFullName() : ActorSvc::GetName(sexPartnerID);
        payload["framework"] = framework;
        payload["threadID"] = threadID;
        payload["isWalkIn"] = isWalkIn;

        std::string jsonStr = payload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        EmitModCallbackEvent("maras_open_relationship_walkin", std::move(jsonStr), static_cast<float>(threadID),
                             witnessID);
        MARAS_LOG_INFO(
            "IntimateThreadService: Sent maras_open_relationship_walkin event: witness={:08X}, partner={:08X}, sexPartner={:08X}, walkIn={}",
            witnessID, partnerID, sexPartnerID, isWalkIn);

        if (auto* hooks = Engine::GetEngineEventHooks()) {
            hooks->OnIntimateOpenRelationshipWalkIn(
                partnerID, witnessID, sexPartnerID, framework, threadID, isWalkIn);
        }
    }

}  // namespace MARAS
