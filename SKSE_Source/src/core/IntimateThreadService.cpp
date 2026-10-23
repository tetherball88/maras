#include "core/IntimateThreadService.h"

#include "core/FormCache.h"
#include "core/NPCRelationshipManager.h"
#include "services/ActorService.h"
#include "utils/ActorUtils.h"
#include "utils/Common.h"
#include "utils/ProcessUtils.h"
#include "utils/Settings.h"

namespace MARAS {

    IntimateThreadService& IntimateThreadService::GetSingleton() {
        static IntimateThreadService instance;
        return instance;
    }

    IntimateThreadService::~IntimateThreadService() {
        // During DLL_PROCESS_DETACH (static destruction), joining a std::thread while the
        // Windows Loader Lock is held can deadlock process termination, and LoggingService
        // may already be destroyed. Signal the worker to stop and detach safely.
        StopWorker(/*joinThread=*/false);
    }

    void IntimateThreadService::Initialize() {
        StartWorkerIfNeeded();
        // Children flee from the cells where intimate scenes are running.
        ChildrenFleeService::GetSingleton().SetThreatProvider(
            [](ChildrenFleeService::ThreatMap& threats) { GetSingleton().CollectSceneThreats(threats); });
        MARAS_LOG_INFO("IntimateThreadService initialized");
    }

    void IntimateThreadService::Shutdown() {
        StopWorker(/*joinThread=*/true);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            activeThreads_.clear();
        }
        MARAS_LOG_INFO("IntimateThreadService shutdown");
    }

    void IntimateThreadService::ClearAll() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            activeThreads_.clear();
            RefreshWorkerMonitorStateLocked();
        }
        MARAS_LOG_INFO("IntimateThreadService: Cleared all active threads on session reset");
    }

    void IntimateThreadService::StartWorkerIfNeeded() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!workerSync_ || workerSync_->stopRequested.load(std::memory_order_acquire)) {
            if (workerThread_.joinable()) {
                workerThread_.detach();
            }
            workerSync_ = std::make_shared<WorkerSync>();
            workerSync_->hasMonitorableThreads = HasActiveThreadsToMonitorLocked();
            workerThread_ = std::thread(&IntimateThreadService::WorkerLoop, workerSync_);
        } else {
            RefreshWorkerMonitorStateLocked();
        }
    }

    void IntimateThreadService::StopWorker(bool joinThread) {
        std::shared_ptr<WorkerSync> syncCopy;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            syncCopy = workerSync_;
            workerSync_.reset();
        }

        if (syncCopy) {
            {
                std::lock_guard<std::mutex> syncLock(syncCopy->mutex);
                syncCopy->stopRequested.store(true, std::memory_order_release);
                syncCopy->hasMonitorableThreads = false;
            }
            syncCopy->cv.notify_all();
        }

        if (workerThread_.joinable()) {
            if (joinThread) {
                workerThread_.join();
            } else {
                workerThread_.detach();
            }
        }
    }

    bool IntimateThreadService::HasActiveThreadsToMonitorLocked() const {
        // Every active thread is monitored: cheating/open-relationship detection and
        // missed-end-event verification both rely on the poll loop.
        return !activeThreads_.empty();
    }

    void IntimateThreadService::RefreshWorkerMonitorStateLocked() {
        if (!workerSync_) return;
        const bool shouldMonitor = HasActiveThreadsToMonitorLocked();
        {
            std::lock_guard<std::mutex> syncLock(workerSync_->mutex);
            workerSync_->hasMonitorableThreads = shouldMonitor;
        }
        workerSync_->cv.notify_all();
    }

    void IntimateThreadService::WorkerLoop(std::shared_ptr<WorkerSync> sync) {
        if (!sync) return;

        std::unique_lock<std::mutex> lock(sync->mutex);
        while (!sync->stopRequested.load(std::memory_order_acquire)) {
            // Wait until there's at least one active thread with potential cheaters or open relationship watchers, or shutdown
            sync->cv.wait(lock, [&sync]() {
                return sync->stopRequested.load(std::memory_order_acquire) || sync->hasMonitorableThreads;
            });

            if (sync->stopRequested.load(std::memory_order_acquire)) break;

            // Sleep for the poll interval while waiting for state changes
            sync->cv.wait_for(lock, kPollInterval, [&sync]() {
                return sync->stopRequested.load(std::memory_order_acquire) || !sync->hasMonitorableThreads;
            });

            if (sync->stopRequested.load(std::memory_order_acquire)) break;

            if (sync->hasMonitorableThreads) {
                // Post game-thread task for proximity & LOS evaluation
                if (auto* taskIF = SKSE::GetTaskInterface()) {
                    taskIF->AddTask([sync]() {
                        if (sync->stopRequested.load(std::memory_order_acquire)) {
                            return;
                        }
                        try {
                            IntimateThreadService::GetSingleton().PollActiveThreads();
                        } catch (const std::exception& e) {
                            MARAS_LOG_ERROR("IntimateThreadService::PollActiveThreads exception: {}", e.what());
                        }
                    });
                }
            }
        }
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

        auto& relMgr = NPCRelationshipManager::GetSingleton();
        const auto& allPairs = relMgr.GetAllRelationshipPairs();
        const bool enablePolygamy = Settings::GetSingleton().GetEnablePolygamy();

        for (RE::FormID actorID : actorIDs) {
            std::vector<RE::FormID> victims;
            std::vector<RE::FormID> openPartners;

            for (const auto& [pairKey, pairData] : allPairs) {
                if (pairKey.Contains(actorID) && NPCRelationshipManager::IsMainTier(pairData.status)) {
                    RE::FormID partnerID = pairKey.GetOther(actorID);
                    if (partnerID == 0) continue;

                    // 1. Group scene exemption: partner is participating in the thread
                    if (std::find(actorIDs.begin(), actorIDs.end(), partnerID) != actorIDs.end()) {
                        continue;
                    }

                    // 2. Co-spouse intimacy exemption: if two or more co-spouses of the Player are intimate
                    // with each other (and no non-co-spouse third party is in the thread), it is not cheating on the Player.
                    if (partnerID == kPlayerFormID && relMgr.IsPlayerActiveMainPartner(actorID)) {
                        bool hasNonCoSpouseOther = false;
                        for (RE::FormID otherID : actorIDs) {
                            if (otherID == 0 || otherID == actorID || otherID == kPlayerFormID) continue;
                            if (!relMgr.IsPlayerActiveMainPartner(otherID)) {
                                hasNonCoSpouseOther = true;
                                break;
                            }
                        }
                        if (!hasNonCoSpouseOther) {
                            MARAS_LOG_DEBUG("EvaluateParticipants: Co-spouse intimacy exemption applied for {:08X} ({}) — all other thread participants are Player co-spouses", actorID, ActorSvc::GetName(actorID));
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

    bool IntimateThreadService::RegisterIntimateThread(const std::string& framework, int32_t threadID, const std::vector<RE::Actor*>& actors) {
        if (actors.empty()) {
            MARAS_LOG_WARN("RegisterIntimateThread: Called with 0 actors (framework={}, threadID={})", framework, threadID);
            return false;
        }

        std::vector<RE::FormID> actorIDs;
        actorIDs.reserve(actors.size());
        for (auto* actor : actors) {
            if (actor) {
                RE::FormID id = actor->GetFormID();
                if (std::find(actorIDs.begin(), actorIDs.end(), id) == actorIDs.end()) {
                    actorIDs.push_back(id);
                }
            }
        }

        if (actorIDs.empty()) {
            MARAS_LOG_WARN("RegisterIntimateThread: No valid actors provided (framework={}, threadID={})", framework, threadID);
            return false;
        }

        std::vector<PotentialCheater> evaluatedCheaters;
        std::vector<OpenRelationshipWatcher> evaluatedOpenWatchers;
        bool hasCheatingPotential = false;
        bool hasOpenRelationshipPotential = false;
        EvaluateParticipants(actorIDs, evaluatedCheaters, evaluatedOpenWatchers, hasCheatingPotential, hasOpenRelationshipPotential);

        StartWorkerIfNeeded();

        ThreadKey key{ framework, threadID };

        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = activeThreads_.find(key);
            if (it != activeThreads_.end()) {
                // Update existing thread actors and potential cheaters/open watchers while preserving existing sexualPairs
                it->second.actorIDs = std::move(actorIDs);
                it->second.potentialCheaters = std::move(evaluatedCheaters);
                it->second.openRelationshipWatchers = std::move(evaluatedOpenWatchers);
                it->second.hasCheatingPotential = hasCheatingPotential;
                it->second.hasOpenRelationshipPotential = hasOpenRelationshipPotential;
                MARAS_LOG_INFO("RegisterIntimateThread: Updated existing thread {} #{} (participants={}, hasCheaters={}, hasOpenWatchers={})",
                               framework, threadID, it->second.actorIDs.size(), hasCheatingPotential, hasOpenRelationshipPotential);
            } else {
                TrackedIntimateThread thread;
                thread.framework = framework;
                thread.threadID = threadID;
                thread.actorIDs = std::move(actorIDs);
                thread.potentialCheaters = std::move(evaluatedCheaters);
                thread.openRelationshipWatchers = std::move(evaluatedOpenWatchers);
                thread.hasCheatingPotential = hasCheatingPotential;
                thread.hasOpenRelationshipPotential = hasOpenRelationshipPotential;
                auto [insertedIt, _] = activeThreads_.emplace(key, std::move(thread));
                MARAS_LOG_INFO("RegisterIntimateThread: Registered new thread {} #{} (participants={}, hasCheaters={}, hasOpenWatchers={})",
                               framework, threadID, insertedIt->second.actorIDs.size(), hasCheatingPotential, hasOpenRelationshipPotential);
            }

            RefreshWorkerMonitorStateLocked();
        }

        ChildrenFleeService::GetSingleton().QueueRefresh();

        return hasCheatingPotential || hasOpenRelationshipPotential;
    }

    void IntimateThreadService::ReportIntimateInteraction(const std::string& framework, int32_t threadID, RE::Actor* actorA, RE::Actor* actorB) {
        if (!actorA || !actorB || actorA == actorB) return;

        RE::FormID idA = actorA->GetFormID();
        RE::FormID idB = actorB->GetFormID();
        if (idA == idB) return;

        ThreadKey key{ framework, threadID };
        std::vector<RE::FormID> currentActorIDs;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = activeThreads_.find(key);
            if (it == activeThreads_.end()) {
                MARAS_LOG_DEBUG("ReportIntimateInteraction: Thread {} #{} not registered, skipping", framework, threadID);
                return;
            }

            // Deduplicate: if this sexual pair was already recorded in this thread, skip re-evaluating
            // so we don't increment the affair incident count multiple times across scenes in the same session.
            SexualPairRecord pairRec{ idA, idB };
            if (std::find(it->second.sexualPairs.begin(), it->second.sexualPairs.end(), pairRec) != it->second.sexualPairs.end()) {
                return;
            }
            it->second.sexualPairs.push_back(pairRec);

            // Ensure both interacting actors are recorded in the thread's participant list
            // even if one joined dynamically before RegisterIntimateThread was re-called.
            if (std::find(it->second.actorIDs.begin(), it->second.actorIDs.end(), idA) == it->second.actorIDs.end()) {
                it->second.actorIDs.push_back(idA);
            }
            if (std::find(it->second.actorIDs.begin(), it->second.actorIDs.end(), idB) == it->second.actorIDs.end()) {
                it->second.actorIDs.push_back(idB);
            }
            currentActorIDs = it->second.actorIDs;
        }

        // Re-evaluate participant relationships against live NPCRelationshipManager state so any
        // relationship changes that occurred since RegisterIntimateThread are reflected accurately.
        std::vector<PotentialCheater> refreshedCheaters;
        std::vector<OpenRelationshipWatcher> refreshedOpenWatchers;
        bool hasCheatingPotential = false;
        bool hasOpenRelationshipPotential = false;
        EvaluateParticipants(currentActorIDs, refreshedCheaters, refreshedOpenWatchers, hasCheatingPotential, hasOpenRelationshipPotential);

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
                RefreshWorkerMonitorStateLocked();
            }
        }

        if (!found || (!threadCopy.hasCheatingPotential && !threadCopy.hasOpenRelationshipPotential)) return;

        auto& relMgr = NPCRelationshipManager::GetSingleton();
        const bool enablePolygamy = Settings::GetSingleton().GetEnablePolygamy();

        // 1. Process potential cheating
        if (threadCopy.hasCheatingPotential) {
            auto processCheater = [&](RE::Actor* cheaterActor, RE::Actor* partnerActor, RE::FormID cheaterID, RE::FormID partnerID) {
                if (cheaterID == kPlayerFormID && enablePolygamy) return;

                for (const auto& pc : threadCopy.potentialCheaters) {
                    if (pc.cheaterID != cheaterID) continue;

                    for (RE::FormID victimID : pc.cachedVictimIDs) {
                        if (victimID == 0 || victimID == partnerID || victimID == cheaterID) continue;
                        if (std::find(threadCopy.actorIDs.begin(), threadCopy.actorIDs.end(), victimID) != threadCopy.actorIDs.end()) {
                            continue;
                        }
                        // Co-spouse intimacy exemption: two co-spouses of the Player being intimate is never cheating on the Player
                        if (victimID == kPlayerFormID &&
                            relMgr.IsPlayerActiveMainPartner(cheaterID) &&
                            relMgr.IsPlayerActiveMainPartner(partnerID)) {
                            continue;
                        }

                        const auto* pairData = relMgr.GetNPCData(cheaterID, victimID);
                        if (!pairData || !NPCRelationshipManager::IsMainTier(pairData->status) || pairData->isOpen) {
                            continue;
                        }

                        auto* victimActor = RE::TESForm::LookupByID<RE::Actor>(victimID);
                        bool isVictimPresent = CheckVictimExposure(victimActor, cheaterActor);

                        if (isVictimPresent) {
                            relMgr.RecordCheating(cheaterID, victimID, partnerID, /*isSecret=*/false, AffairType::Casual, AffairStatus::Ongoing);
                            relMgr.SetCheatingDiscovered(cheaterID, partnerID, AffairDiscovery::Caught);
                            {
                                std::lock_guard<std::mutex> lock(mutex_);
                                auto tIt = activeThreads_.find(key);
                                if (tIt != activeThreads_.end()) {
                                    tIt->second.caughtEvents.insert(CaughtEventKey{ cheaterID, victimID, partnerID });
                                }
                            }
                            MARAS_LOG_INFO("ReportIntimateInteraction: Caught in the act! {:08X} with {:08X} exposed to victim {:08X}",
                                           cheaterID, partnerID, victimID);

                            if (victimID == kPlayerFormID) {
                                std::string cName = cheaterActor->GetDisplayFullName();
                                std::string pName = partnerActor->GetDisplayFullName();
                                std::string msg = fmt::format("You caught {} with {}!", cName, pName);
                                RE::SendHUDMessage::ShowHUDMessage(msg.c_str());
                            }

                            // Send Papyrus event with victim as sender
                            SendCheatingCaughtEvent(victimActor, cheaterActor, partnerActor, framework, threadID, /*isWalkIn=*/false);
                        } else {
                            relMgr.RecordCheating(cheaterID, victimID, partnerID, /*isSecret=*/true, AffairType::Casual, AffairStatus::Ongoing);
                            MARAS_LOG_INFO("ReportIntimateInteraction: Secret affair registered: {:08X} cheated on {:08X} with {:08X}",
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
            auto processOpenWatcher = [&](RE::Actor* partnerActor, RE::Actor* sexPartnerActor, RE::FormID partnerID, RE::FormID sexPartnerID) {
                for (const auto& ow : threadCopy.openRelationshipWatchers) {
                    if (ow.actorID != partnerID) continue;

                    for (RE::FormID witnessID : ow.cachedPartnerIDs) {
                        if (witnessID == 0 || witnessID == sexPartnerID || witnessID == partnerID) continue;
                        if (std::find(threadCopy.actorIDs.begin(), threadCopy.actorIDs.end(), witnessID) != threadCopy.actorIDs.end()) {
                            continue;
                        }
                        // Co-spouse intimacy exemption
                        if (witnessID == kPlayerFormID &&
                            relMgr.IsPlayerActiveMainPartner(partnerID) &&
                            relMgr.IsPlayerActiveMainPartner(sexPartnerID)) {
                            continue;
                        }

                        const auto* pairData = relMgr.GetNPCData(partnerID, witnessID);
                        if (!pairData || !NPCRelationshipManager::IsMainTier(pairData->status) || !pairData->isOpen) {
                            continue;
                        }

                        auto* witnessActor = RE::TESForm::LookupByID<RE::Actor>(witnessID);
                        bool isWitnessPresent = CheckVictimExposure(witnessActor, partnerActor);

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

                            if (!alreadySent) {
                                MARAS_LOG_INFO("ReportIntimateInteraction: Open relationship witnessed! Partner {:08X} with {:08X} witnessed by {:08X}",
                                               partnerID, sexPartnerID, witnessID);

                                // Send Papyrus event with witness as sender
                                SendOpenRelationshipWalkInEvent(witnessActor, partnerActor, sexPartnerActor, framework, threadID, /*isWalkIn=*/false);
                            }
                        }
                    }
                }
            };

            processOpenWatcher(actorA, actorB, idA, idB);
            processOpenWatcher(actorB, actorA, idB, idA);
        }
    }

    void IntimateThreadService::ClearIntimateThread(const std::string& framework, int32_t threadID) {
        ThreadKey key{ framework, threadID };
        {
            std::lock_guard<std::mutex> lock(mutex_);
            activeThreads_.erase(key);
            RefreshWorkerMonitorStateLocked();
        }
        MARAS_LOG_INFO("ClearIntimateThread: Cleared thread {} #{}", framework, threadID);
        ChildrenFleeService::GetSingleton().QueueRefresh();
    }

    void IntimateThreadService::CollectSceneThreats(ChildrenFleeService::ThreatMap& threats) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [key, thread] : activeThreads_) {
            for (RE::FormID id : thread.actorIDs) {
                auto* participant = RE::TESForm::LookupByID<RE::Actor>(id);
                auto* cell = participant && participant->Is3DLoaded() ? participant->GetParentCell() : nullptr;
                if (cell) threats[cell->GetFormID()].push_back(participant->GetPosition());
            }
        }
    }

    bool IntimateThreadService::CheckVictimExposure(RE::Actor* victimActor, RE::Actor* cheaterActor) {
        if (!victimActor || !cheaterActor || victimActor == cheaterActor) return false;
        if (!victimActor->Is3DLoaded() || !cheaterActor->Is3DLoaded()) return false;
        if (victimActor->IsDead()) return false;
        auto sitSleep = victimActor->GetSitSleepState();
        if (sitSleep == RE::SIT_SLEEP_STATE::kIsSleeping ||
            sitSleep == RE::SIT_SLEEP_STATE::kWantToSleep ||
            sitSleep == RE::SIT_SLEEP_STATE::kWaitingForSleepAnim) {
            return false;
        }
        if (victimActor->GetParentCell() != cheaterActor->GetParentCell()) return false;

        float dist = victimActor->GetPosition().GetDistance(cheaterActor->GetPosition());
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
            // MARAS_LOG_DEBUG("PollActiveThreads: tick skipped (game paused)");
            return;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            // MARAS_LOG_DEBUG("PollActiveThreads: tick, activeThreads={}", activeThreads_.size());
        }

        // Safety net for lost framework end events: ask Papyrus (which can query the framework API)
        // to confirm each tracked thread is still running. Papyrus clears the ones that are gone.
        std::vector<ThreadKey> trackedKeys;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            trackedKeys.reserve(activeThreads_.size());
            for (const auto& [key, thread] : activeThreads_) {
                trackedKeys.push_back(key);
            }
        }
        if (!trackedKeys.empty()) {
            if (auto* eventSource = SKSE::GetModCallbackEventSource()) {
                auto* player = RE::PlayerCharacter::GetSingleton();
                for (const auto& key : trackedKeys) {
                    SKSE::ModCallbackEvent modEvent("maras_verify_intimate_thread", key.framework.c_str(),
                                                    static_cast<float>(key.threadID), player);
                    eventSource->SendEvent(&modEvent);
                }
            }
        }

        std::vector<TrackedIntimateThread> threadsSnapshot;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            threadsSnapshot.reserve(activeThreads_.size());
            for (const auto& [key, thread] : activeThreads_) {
                if ((thread.hasCheatingPotential || thread.hasOpenRelationshipPotential) && !thread.sexualPairs.empty()) {
                    threadsSnapshot.push_back(thread);
                }
            }
        }

        if (threadsSnapshot.empty()) return;

        auto& relMgr = NPCRelationshipManager::GetSingleton();
        const bool enablePolygamy = Settings::GetSingleton().GetEnablePolygamy();

        for (const auto& thread : threadsSnapshot) {
            ThreadKey threadKey{ thread.framework, thread.threadID };

            // 1. Evaluate Cheaters
            for (const auto& pc : thread.potentialCheaters) {
                if (pc.cheaterID == kPlayerFormID && enablePolygamy) continue;

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
                    continue; // Scene hasn't reached sexual activity for this actor yet
                }

                auto* cheaterActor = RE::TESForm::LookupByID<RE::Actor>(pc.cheaterID);
                if (!cheaterActor) continue;

                for (RE::FormID victimID : pc.cachedVictimIDs) {
                    if (victimID == 0 || victimID == pc.cheaterID) continue;
                    if (std::find(thread.actorIDs.begin(), thread.actorIDs.end(), victimID) != thread.actorIDs.end()) {
                        continue;
                    }

                    // Re-verify that (cheaterID, victimID) is still a closed main-tier relationship
                    const auto* pairData = relMgr.GetNPCData(pc.cheaterID, victimID);
                    if (!pairData || !NPCRelationshipManager::IsMainTier(pairData->status) || pairData->isOpen) {
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
                            relMgr.IsPlayerActiveMainPartner(pc.cheaterID) &&
                            relMgr.IsPlayerActiveMainPartner(affairPartnerID)) {
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
                        for (const auto& rec : relMgr.GetCheatingRecords(pc.cheaterID, victimID)) {
                            if (rec.cheaterID == pc.cheaterID && rec.affairPartnerID == affairPartnerID &&
                                rec.affairStatus == AffairStatus::Ongoing && rec.isSecret) {
                                isSecretAffair = true;
                                break;
                            }
                        }

                        if (!isSecretAffair) {
                            // Affair was already discovered / known, do not re-expose or re-log
                            continue;
                        }

                        relMgr.SetCheatingDiscovered(pc.cheaterID, affairPartnerID, AffairDiscovery::Caught);
                        MARAS_LOG_INFO("PollActiveThreads: Walk-in caught! Cheater {:08X} with {:08X} caught by victim {:08X} in thread {} #{}",
                                       pc.cheaterID, affairPartnerID, victimID, thread.framework, thread.threadID);

                        auto* partnerActor = RE::TESForm::LookupByID<RE::Actor>(affairPartnerID);
                        if (victimID == kPlayerFormID) {
                            std::string cName = cheaterActor->GetDisplayFullName();
                            std::string pName = partnerActor ? partnerActor->GetDisplayFullName() : ActorSvc::GetName(affairPartnerID);
                            std::string msg = fmt::format("You caught {} with {}!", cName, pName);
                            RE::SendHUDMessage::ShowHUDMessage(msg.c_str());
                        }

                        // Send Papyrus event with victim as sender
                        SendCheatingCaughtEvent(victimActor, cheaterActor, partnerActor, thread.framework, thread.threadID, /*isWalkIn=*/true);
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
                    continue; // Scene hasn't reached sexual activity for this actor yet
                }

                auto* partnerActor = RE::TESForm::LookupByID<RE::Actor>(ow.actorID);
                if (!partnerActor) continue;

                for (RE::FormID witnessID : ow.cachedPartnerIDs) {
                    if (witnessID == 0 || witnessID == ow.actorID) continue;
                    if (std::find(thread.actorIDs.begin(), thread.actorIDs.end(), witnessID) != thread.actorIDs.end()) {
                        continue;
                    }

                    // Re-verify that (ow.actorID, witnessID) is still an open main-tier relationship
                    const auto* pairData = relMgr.GetNPCData(ow.actorID, witnessID);
                    if (!pairData || !NPCRelationshipManager::IsMainTier(pairData->status) || !pairData->isOpen) {
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
                            relMgr.IsPlayerActiveMainPartner(ow.actorID) &&
                            relMgr.IsPlayerActiveMainPartner(sexPartnerID)) {
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
                        MARAS_LOG_INFO("PollActiveThreads: Open relationship walk-in detected! Partner {:08X} with {:08X} detected by partner {:08X} in thread {} #{}",
                                       ow.actorID, sexPartnerID, witnessID, thread.framework, thread.threadID);

                        // Send Papyrus event with witness as sender
                        SendOpenRelationshipWalkInEvent(witnessActor, partnerActor, sexPartnerActor, thread.framework, thread.threadID, /*isWalkIn=*/true);
                    }
                }
            }
        }
    }

    void IntimateThreadService::SendCheatingCaughtEvent(RE::Actor* victimActor, RE::Actor* cheaterActor, RE::Actor* partnerActor,
                                                        const std::string& framework, int32_t threadID, bool isWalkIn) {
        if (!victimActor) return;

        auto* eventSource = SKSE::GetModCallbackEventSource();
        if (!eventSource) {
            MARAS_LOG_ERROR("IntimateThreadService: ModCallbackEventSource unavailable to send maras_cheating_caught");
            return;
        }

        RE::FormID victimID = victimActor->GetFormID();
        RE::FormID cheaterID = cheaterActor ? cheaterActor->GetFormID() : 0;
        RE::FormID partnerID = partnerActor ? partnerActor->GetFormID() : 0;

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
        SKSE::ModCallbackEvent modEvent("maras_cheating_caught", jsonStr, static_cast<float>(threadID), victimActor);
        eventSource->SendEvent(&modEvent);
        MARAS_LOG_INFO("IntimateThreadService: Sent maras_cheating_caught event: victim={:08X}, cheater={:08X}, partner={:08X}, walkIn={}",
                       victimID, cheaterID, partnerID, isWalkIn);
    }

    void IntimateThreadService::SendOpenRelationshipWalkInEvent(RE::Actor* witnessActor, RE::Actor* partnerActor, RE::Actor* sexPartnerActor,
                                                                const std::string& framework, int32_t threadID, bool isWalkIn) {
        if (!witnessActor) return;

        auto* eventSource = SKSE::GetModCallbackEventSource();
        if (!eventSource) {
            MARAS_LOG_ERROR("IntimateThreadService: ModCallbackEventSource unavailable to send maras_open_relationship_walkin");
            return;
        }

        RE::FormID witnessID = witnessActor->GetFormID();
        RE::FormID partnerID = partnerActor ? partnerActor->GetFormID() : 0;
        RE::FormID sexPartnerID = sexPartnerActor ? sexPartnerActor->GetFormID() : 0;

        nlohmann::json payload;
        payload["witness"] = fmt::format("0x{:08X}", witnessID);
        payload["witnessName"] = witnessActor->GetDisplayFullName();
        payload["partner"] = fmt::format("0x{:08X}", partnerID);
        payload["partnerName"] = partnerActor ? partnerActor->GetDisplayFullName() : ActorSvc::GetName(partnerID);
        payload["sexPartner"] = fmt::format("0x{:08X}", sexPartnerID);
        payload["sexPartnerName"] = sexPartnerActor ? sexPartnerActor->GetDisplayFullName() : ActorSvc::GetName(sexPartnerID);
        payload["framework"] = framework;
        payload["threadID"] = threadID;
        payload["isWalkIn"] = isWalkIn;

        std::string jsonStr = payload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        SKSE::ModCallbackEvent modEvent("maras_open_relationship_walkin", jsonStr, static_cast<float>(threadID), witnessActor);
        eventSource->SendEvent(&modEvent);
        MARAS_LOG_INFO("IntimateThreadService: Sent maras_open_relationship_walkin event: witness={:08X}, partner={:08X}, sexPartner={:08X}, walkIn={}",
                       witnessID, partnerID, sexPartnerID, isWalkIn);
    }

}  // namespace MARAS
