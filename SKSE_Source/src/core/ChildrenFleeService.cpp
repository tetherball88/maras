#include "core/ChildrenFleeService.h"

#include "core/FormCache.h"
#include "core/NPCRelationshipManager.h"
#include "services/ActorService.h"
#include "utils/Common.h"
#include "utils/ProcessUtils.h"

namespace MARAS {

    namespace {
        bool IsSleeping(RE::Actor* actor) {
            const auto state = actor->GetSitSleepState();
            return state == RE::SIT_SLEEP_STATE::kIsSleeping || state == RE::SIT_SLEEP_STATE::kWaitingForSleepAnim;
        }

        // A child that may flee: alive and awake (child race or in the MARAS child registry).
        bool IsFleeCandidate(RE::Actor* actor) {
            if (actor->IsDead() || IsSleeping(actor)) return false;
            return actor->IsChild() || NPCRelationshipManager::GetSingleton().IsChildNPC(actor->GetFormID());
        }

        // Closest navmesh triangle center to `target` that is at least `minThreatDistance` from the threat.
        std::optional<RE::NiPoint3> FindWalkableSpot(RE::TESObjectCELL* cell, const RE::NiPoint3& target,
                                                     const RE::NiPoint3& threatCenter, float minThreatDistance) {
            auto* navMeshes = cell->GetRuntimeData().navMeshes;
            if (!navMeshes) return std::nullopt;

            std::optional<RE::NiPoint3> best;
            float bestDist = (std::numeric_limits<float>::max)();
            for (const auto& navMesh : navMeshes->navMeshes) {
                if (!navMesh) continue;
                for (const auto& tri : navMesh->triangles) {
                    if (tri.triangleFlags.any(RE::BSNavmeshTriangle::TriangleFlag::kDeleted)) continue;
                    const RE::NiPoint3 center = (navMesh->vertices[tri.vertices[0]].location +
                                                 navMesh->vertices[tri.vertices[1]].location +
                                                 navMesh->vertices[tri.vertices[2]].location) / 3.0f;
                    if (center.GetDistance(threatCenter) < minThreatDistance) continue;
                    if (const float dist = center.GetDistance(target); dist < bestDist) {
                        bestDist = dist;
                        best = center;
                    }
                }
            }
            return best;
        }
    }  // namespace

    ChildrenFleeService& ChildrenFleeService::GetSingleton() {
        static ChildrenFleeService instance;
        return instance;
    }

    ChildrenFleeService::~ChildrenFleeService() {
        // Static destruction (loader lock held): only signal the worker to stop, never join.
        StopWorker();
    }

    void ChildrenFleeService::Initialize() {
        StartWorker();
        MARAS_LOG_INFO("ChildrenFleeService initialized");
    }

    void ChildrenFleeService::Shutdown() {
        StopWorker();
        Reset();
        MARAS_LOG_INFO("ChildrenFleeService shutdown");
    }

    void ChildrenFleeService::Reset() {
        {
            std::lock_guard lock(sourceMutex_);
            manualThreats_.clear();
        }
        {
            std::unique_lock lock(fleeMutex_);
            fleeingChildren_.clear();
            fleeActive_.store(false, std::memory_order_release);
        }
        exitDoorCache_.clear();
        monitoring_.store(false, std::memory_order_release);
    }

    void ChildrenFleeService::SetThreatProvider(ThreatProvider provider) {
        std::lock_guard lock(sourceMutex_);
        threatProvider_ = std::move(provider);
    }

    void ChildrenFleeService::FleeFromPlayerCell() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* cell = player ? player->GetParentCell() : nullptr;
        if (!cell) {
            MARAS_LOG_WARN("ChildrenFleeService: cannot flee, player has no cell");
            return;
        }
        {
            std::lock_guard lock(sourceMutex_);
            manualThreats_[cell->GetFormID()] = { player->GetPosition() };
        }
        MARAS_LOG_INFO("ChildrenFleeService: children flee from cell {:08X}", cell->GetFormID());
        QueueRefresh();
    }

    void ChildrenFleeService::StopFleeing() {
        {
            std::lock_guard lock(sourceMutex_);
            manualThreats_.clear();
        }
        QueueRefresh();
    }

    bool ChildrenFleeService::IsFleeing(RE::FormID actorID) const {
        if (!fleeActive_.load(std::memory_order_acquire)) return false;
        std::shared_lock lock(fleeMutex_);
        return fleeingChildren_.contains(actorID);
    }

    // ─── Worker ──────────────────────────────────────────────────────────────────

    void ChildrenFleeService::StartWorker() {
        std::lock_guard lock(workerMutex_);
        if (worker_ && !worker_->stopRequested.load(std::memory_order_acquire)) return;
        worker_ = std::make_shared<Worker>();
        std::thread(&ChildrenFleeService::WorkerLoop, worker_).detach();
    }

    void ChildrenFleeService::StopWorker() {
        std::lock_guard lock(workerMutex_);
        if (!worker_) return;
        {
            std::lock_guard workerLock(worker_->mutex);
            worker_->stopRequested.store(true, std::memory_order_release);
        }
        worker_->cv.notify_all();
        worker_.reset();
    }

    void ChildrenFleeService::WorkerLoop(std::shared_ptr<Worker> worker) {
        std::unique_lock lock(worker->mutex);
        while (!worker->stopRequested.load(std::memory_order_acquire)) {
            worker->cv.wait_for(lock, kPollInterval, [&] { return worker->stopRequested.load(std::memory_order_acquire); });
            if (worker->stopRequested.load(std::memory_order_acquire)) break;
            if (!GetSingleton().monitoring_.load(std::memory_order_acquire)) continue;

            if (auto* taskIF = SKSE::GetTaskInterface()) {
                taskIF->AddTask([worker]() {
                    if (worker->stopRequested.load(std::memory_order_acquire)) return;
                    if (auto* ui = RE::UI::GetSingleton(); ui && ui->GameIsPaused()) return;
                    GetSingleton().Refresh();
                });
            }
        }
    }

    void ChildrenFleeService::QueueRefresh() {
        monitoring_.store(true, std::memory_order_release);
        if (auto* taskIF = SKSE::GetTaskInterface()) {
            taskIF->AddTask([]() { GetSingleton().Refresh(); });
        }
    }

    // ─── Teleporting blocked children ────────────────────────────────────────────

    RE::TESObjectREFR* ChildrenFleeService::FindExitDestination(RE::TESObjectCELL* cell, const RE::NiPoint3& from) {
        // Load doors are static: scan a cell's references only the first time and reuse the list.
        auto [it, inserted] = exitDoorCache_.try_emplace(cell->GetFormID());
        if (inserted) {
            cell->ForEachReference([&](RE::TESObjectREFR* ref) {
                auto* teleport = ref ? ref->extraList.GetByType<RE::ExtraTeleport>() : nullptr;
                if (teleport && teleport->teleportData) {
                    if (auto linked = teleport->teleportData->linkedDoor.get()) {
                        it->second.push_back({ ref->GetPosition(), linked->GetFormID() });
                    }
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });
        }

        const ExitDoor* nearest = nullptr;
        float nearestDist = (std::numeric_limits<float>::max)();
        for (const auto& door : it->second) {
            if (const float dist = door.pos.GetDistance(from); dist < nearestDist) {
                nearestDist = dist;
                nearest = &door;
            }
        }
        return nearest ? RE::TESForm::LookupByID<RE::TESObjectREFR>(nearest->destinationID) : nullptr;
    }

    bool ChildrenFleeService::TeleportChildAwayFromThreat(RE::Actor* child, const RE::NiPoint3& threatCenter) {
        auto* cell = child->GetParentCell();
        if (!cell) return false;

        // Indoors: out through the nearest load door. Outdoors: onto walkable ground away from the threat.
        if (cell->IsInteriorCell()) {
            if (auto* destination = FindExitDestination(cell, child->GetPosition())) {
                child->MoveTo(destination);
                return true;
            }
        } else {
            RE::NiPoint3 away = child->GetPosition() - threatCenter;
            away.z = 0.0f;
            away = away.Length() < 1.0f ? RE::NiPoint3(1.0f, 0.0f, 0.0f) : away / away.Length();
            const auto spot = FindWalkableSpot(cell, child->GetPosition() + away * kFleeTeleportDistance, threatCenter,
                                               kFleeTeleportDistance * 0.5f);
            if (spot) {
                child->SetPosition(*spot, true);
                return true;
            }
        }

        MARAS_LOG_WARN("ChildrenFleeService: no exit or walkable spot found to move blocked child {:08X}", child->GetFormID());
        return false;
    }

    // ─── Refresh ─────────────────────────────────────────────────────────────────

    ChildrenFleeService::ThreatMap ChildrenFleeService::CollectThreats() {
        ThreatMap threats;
        ThreatProvider provider;
        {
            std::lock_guard lock(sourceMutex_);
            // A manual flee ends once the player leaves the cell it was requested in.
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* playerCell = player ? player->GetParentCell() : nullptr;
            const RE::FormID playerCellID = playerCell ? playerCell->GetFormID() : 0;
            std::erase_if(manualThreats_, [&](const auto& entry) { return entry.first != playerCellID; });
            provider = threatProvider_;
            threats = manualThreats_;
        }
        if (provider) provider(threats);
        return threats;
    }

    void ChildrenFleeService::Refresh() {
        // 1. Where are the threats right now? Cells come live from the sources, so children only flee
        //    from cells that still contain a threat.
        const ThreatMap threats = CollectThreats();

        auto* fleePkg = FormCache::GetSingleton().GetChildrenFleeIntimateScenePackage();
        if (!threats.empty() && !fleePkg) {
            MARAS_LOG_WARN("ChildrenFleeService: threat present but flee package (0x5DE) is not resolved");
        }

        // Only this (game) thread modifies the flee state, so a snapshot stays valid during the refresh.
        std::unordered_map<RE::FormID, FleeState> previous;
        {
            std::shared_lock lock(fleeMutex_);
            previous = fleeingChildren_;
        }

        // 2. Build the new flee set. A child keeps its FleeState (origin cell + start time) while it
        //    keeps fleeing from the same cell.
        const auto now = std::chrono::steady_clock::now();
        std::unordered_map<RE::FormID, FleeState> next;
        if (!threats.empty() && fleePkg) {
            // Loaded children standing in a threatened cell.
            ProcessUtils::ForEachLoadedActor([&](RE::Actor* actor) {
                auto* cell = actor->Is3DLoaded() ? actor->GetParentCell() : nullptr;
                if (!cell || !threats.contains(cell->GetFormID()) || !IsFleeCandidate(actor)) return false;
                const auto old = previous.find(actor->GetFormID());
                const bool sameCell = old != previous.end() && old->second.fromCellID == cell->GetFormID();
                next[actor->GetFormID()] = sameCell ? old->second : FleeState{ cell->GetFormID(), now };
                return false;
            });

            // Sticky: a child already fleeing from a cell keeps fleeing while a threat remains there,
            // even if the flee package carried it out of the cell, so it doesn't flip back and forth.
            for (const auto& [childID, state] : previous) {
                auto* child = RE::TESForm::LookupByID<RE::Actor>(childID);
                if (!next.contains(childID) && threats.contains(state.fromCellID) && child && IsFleeCandidate(child)) {
                    next[childID] = state;
                }
            }

            // Blocked: still in the threatened cell and next to the threat after the delay -> teleport
            // away. The child keeps fleeing (stays in the set) so it doesn't walk straight back in; its
            // timer restarts so a still-blocked child is retried every delay.
            // (Not under a lock: MoveTo triggers AI hooks that read the flee set.)
            for (auto& [childID, state] : next) {
                if (now - state.since < kFleeTeleportDelay) continue;
                auto* child = RE::TESForm::LookupByID<RE::Actor>(childID);
                auto* cell = child ? child->GetParentCell() : nullptr;
                const auto positions = threats.find(state.fromCellID);
                if (!cell || cell->GetFormID() != state.fromCellID || positions == threats.end()) continue;

                RE::NiPoint3 center{};
                float nearest = (std::numeric_limits<float>::max)();
                for (const auto& pos : positions->second) {
                    center += pos;
                    nearest = (std::min)(nearest, child->GetPosition().GetDistance(pos));
                }
                center /= static_cast<float>(positions->second.size());
                if (nearest > kFleeBlockedDistance || !TeleportChildAwayFromThreat(child, center)) continue;

                state.since = now;
                MARAS_LOG_INFO("ChildrenFleeService: child {:08X} ({}) was blocked while fleeing, teleported away",
                               childID, ActorSvc::GetName(childID));
            }
        }

        // The worker only needs to keep polling while there are threats or fleeing children.
        monitoring_.store(!threats.empty() || !next.empty(), std::memory_order_release);

        // 3. Publish the new set and make the AI pick up the changes (the package hook starts/stops
        //    returning the flee package for those actors).
        std::vector<RE::FormID> started, stopped;
        for (const auto& [id, state] : next) {
            if (!previous.contains(id)) started.push_back(id);
        }
        for (const auto& [id, state] : previous) {
            if (!next.contains(id)) stopped.push_back(id);
        }
        if (started.empty() && stopped.empty()) return;

        {
            std::unique_lock lock(fleeMutex_);
            fleeingChildren_ = std::move(next);
            fleeActive_.store(!fleeingChildren_.empty(), std::memory_order_release);
        }

        const auto notify = [](const std::vector<RE::FormID>& ids, bool fleeing) {
            for (RE::FormID id : ids) {
                if (auto* child = RE::TESForm::LookupByID<RE::Actor>(id)) {
                    MARAS_LOG_INFO("ChildrenFleeService: child {:08X} ({}) {}", id, ActorSvc::GetName(id),
                                   fleeing ? "fleeing" : "no longer fleeing");
                    child->EvaluatePackage(false, !fleeing);
                }
            }
        };
        notify(started, true);
        notify(stopped, false);
    }

}  // namespace MARAS

