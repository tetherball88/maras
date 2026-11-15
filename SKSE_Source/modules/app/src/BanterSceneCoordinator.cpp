#include "maras/app/BanterSceneCoordinator.h"

#include "maras/app/AppContext.h"
#include "maras/app/LineageUseCases.h"
#include "maras/app/RelationshipUseCases.h"
#include "maras/domain/agency/BanterPlanner.h"
#include "maras/engine/ActorEngineUtils.h"
#include "maras/engine/EngineLogging.h"
#include "maras/engine/FormCache.h"
#include "maras/foundation/StrongIds.h"
#include "maras/state/WorldStore.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <format>
#include <vector>

namespace MARAS::App {

    namespace {

        [[nodiscard]] std::string FormatHexFormId(RE::FormID id) {
            return (id != 0) ? std::format("0x{:08X}", id) : std::string{};
        }

        [[nodiscard]] RE::Actor* ResolveBanterActor(Foundation::ActorId actorId) noexcept {
            if (actorId.IsNull()) {
                return nullptr;
            }
            if (actorId.IsPlayer()) {
                return RE::PlayerCharacter::GetSingleton();
            }
            if (!actorId.HasPhysicalActor()) {
                return nullptr;
            }
            return RE::TESForm::LookupByID<RE::Actor>(actorId.raw());
        }

        [[nodiscard]] std::string BuildInvalidPrepareJson() {
            nlohmann::json j;
            j["valid"] = false;
            return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        }

    }  // namespace

    BanterSceneCoordinator& BanterSceneCoordinator::GetSingleton() {
        static BanterSceneCoordinator instance;
        return instance;
    }

    std::string BanterSceneCoordinator::PrepareBanterScene(
        RE::Actor* npc1In,
        RE::Actor* npc2In,
        std::string_view forcedScenario,
        bool hasSexFramework,
        bool hasBedsNearby) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!npc1In || !npc2In || !player) {
            return BuildInvalidPrepareJson();
        }

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap || !snap->settings || !snap->relationships) {
            return BuildInvalidPrepareJson();
        }

        const auto* npc1Cell = npc1In->GetParentCell();
        Domain::Agency::BanterSceneSetupInput input{};
        input.npc1 = Foundation::ActorId{ npc1In->GetFormID() };
        input.npc2 = Foundation::ActorId{ npc2In->GetFormID() };
        input.forcedScenario = forcedScenario;
        input.cheatingEnv.hasSexFramework = hasSexFramework;
        input.cheatingEnv.isInterior = npc1Cell && npc1Cell->IsInteriorCell();
        input.cheatingEnv.hasBedsNearby = hasBedsNearby;
        input.cheatingEnv.allowCaught = true;

        auto rng = LineageUseCases::MakeEngineRng();
        const auto plan = Domain::Agency::PlanBanterSceneSetup(*snap, input, rng);
        if (!plan.valid) {
            return BuildInvalidPrepareJson();
        }

        RE::Actor* npc1 = ResolveBanterActor(plan.npc1);
        RE::Actor* npc2 = ResolveBanterActor(plan.npc2);
        RE::Actor* npc1MainPartner = ResolveBanterActor(plan.npc1MainPartner);
        RE::Actor* npc2MainPartner = ResolveBanterActor(plan.npc2MainPartner);
        if (!npc1 || !npc2) {
            return BuildInvalidPrepareJson();
        }

        if (npc1->GetCurrentScene() != nullptr || npc2->GetCurrentScene() != nullptr) {
            MARAS_LOG_DEBUG(
                "[BanterSceneCoordinator] PrepareBanterScene aborted: actor already in scene ({:08X}, {:08X})",
                npc1->GetFormID(),
                npc2->GetFormID());
            return BuildInvalidPrepareJson();
        }
        if (plan.isCheating && npc1MainPartner && npc1MainPartner->GetCurrentScene() != nullptr) {
            MARAS_LOG_DEBUG(
                "[BanterSceneCoordinator] PrepareBanterScene aborted: npc1MainPartner {:08X} already in scene",
                npc1MainPartner->GetFormID());
            return BuildInvalidPrepareJson();
        }

        auto* playerCell = player->GetParentCell();
        const bool npc1Same = (npc1->GetParentCell() == playerCell);
        const bool npc2Same = (npc2->GetParentCell() == playerCell);
        if (!npc1Same && !npc2Same && !plan.isForced) {
            MARAS_LOG_DEBUG(
                "[BanterSceneCoordinator] PrepareBanterScene aborted: neither {:08X} nor {:08X} in player cell",
                npc1->GetFormID(),
                npc2->GetFormID());
            return BuildInvalidPrepareJson();
        }

        nlohmann::json movedActors = nlohmann::json::array();
        auto* xMarker = FormCache::GetSingleton().GetXMarkerStatic();
        auto moveNpc = [&movedActors, xMarker](RE::Actor* npc, RE::Actor* target) {
            if (!npc || !target || npc->IsPlayerRef()) {
                return;
            }
            if (xMarker) {
                if (auto marker = npc->PlaceObjectAtMe(xMarker, true)) {
                    State::WorldStore::GetSingleton().Modify(
                        [npcId = npc->GetFormID(), markerId = marker->GetFormID()](State::WorldState& ws) {
                            ws.cooldowns.Write().movedSceneActors.try_emplace(
                                Foundation::ActorId{ npcId },
                                Foundation::RefId{ markerId });
                        });
                }
            } else {
                MARAS_LOG_WARN("[BanterSceneCoordinator] XMarker static not cached; cannot record return marker for {:08X}",
                               npc->GetFormID());
            }
            npc->MoveTo(target);
            npc->EvaluatePackage();
            movedActors.push_back(FormatHexFormId(npc->GetFormID()));
        };

        if (plan.needsPartnerMove && npc1MainPartner && !npc1MainPartner->IsPlayerRef()) {
            moveNpc(npc1MainPartner, player);
        }

        if (!npc1Same && !npc2Same) {
            moveNpc(npc1, player);
            moveNpc(npc2, npc1);
        } else if (!npc1Same) {
            moveNpc(npc1, npc2);
        } else if (!npc2Same) {
            moveNpc(npc2, npc1);
        }

        playerCell = player->GetParentCell();
        const bool shouldExposeNpc1 =
            plan.shouldExposeNpc1Affair ||
            (plan.isCheating && npc1MainPartner && npc1MainPartner->GetParentCell() == playerCell);
        if (shouldExposeNpc1) {
            if (GetSkyrimNetGateway().IsAvailable()) {
                GetSkyrimNetGateway().SuppressNextActionNarration(5.0f);
            }
            RelationshipUseCases::GetSingleton().SetCheatingDiscovered(
                npc1->GetFormID(), npc2->GetFormID(), "exposed");
        }

        if ((plan.isForced || plan.isCheating) && npc2MainPartner &&
            npc2MainPartner->GetParentCell() == playerCell) {
            if (GetSkyrimNetGateway().IsAvailable()) {
                GetSkyrimNetGateway().SuppressNextActionNarration(5.0f);
            }
            RelationshipUseCases::GetSingleton().SetCheatingDiscovered(
                npc2->GetFormID(), npc1->GetFormID(), "exposed");
        }

        const bool shouldMutePartner =
            plan.shouldSetupSexScene && npc1MainPartner && !npc1MainPartner->IsPlayerRef();

        nlohmann::json result;
        result["valid"] = true;
        result["sceneKind"] = static_cast<std::int32_t>(plan.sceneKind);
        result["sceneIdx"] = plan.sceneIdx;
        result["npc1"] = FormatHexFormId(npc1->GetFormID());
        result["npc2"] = FormatHexFormId(npc2->GetFormID());
        result["npc1MainPartner"] = npc1MainPartner ? FormatHexFormId(npc1MainPartner->GetFormID()) : "";
        result["npc2MainPartner"] = npc2MainPartner ? FormatHexFormId(npc2MainPartner->GetFormID()) : "";
        result["shouldSetupSexScene"] = plan.shouldSetupSexScene;
        result["shouldMutePartner"] = shouldMutePartner;
        result["movedActors"] = std::move(movedActors);
        return result.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }

    std::string BanterSceneCoordinator::PlanBanterCheatingFallback(
        RE::Actor* npc1,
        RE::Actor* npc2,
        bool hasSexFramework,
        bool hasBedsNearby) {
        auto makeFallbackJson = [](Domain::Agency::BanterSceneKind kind, std::int32_t idx) {
            nlohmann::json j;
            j["sceneKind"] = static_cast<std::int32_t>(kind);
            j["sceneIdx"] = idx;
            return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        };

        if (!npc1 || !npc2) {
            return makeFallbackJson(Domain::Agency::BanterSceneKind::None, -1);
        }

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap || !snap->settings) {
            return makeFallbackJson(Domain::Agency::BanterSceneKind::None, -1);
        }

        const Foundation::ActorId npc1Id{ npc1->GetFormID() };
        const Foundation::ActorId npc2Id{ npc2->GetFormID() };
        const auto* cell = npc1->GetParentCell();
        const Domain::Agency::CheatingSceneEnvironment env{
            hasSexFramework,
            cell && cell->IsInteriorCell(),
            hasBedsNearby,
            /*allowCaught=*/false
        };

        auto rng = LineageUseCases::MakeEngineRng();
        const std::int32_t cheatingType =
            Domain::Agency::SelectCheatingSceneKindWithFallbacks(*snap, npc1Id, npc2Id, env, rng);

        Domain::Agency::BanterSceneKind kind = Domain::Agency::BanterSceneKind::None;
        switch (cheatingType) {
            case 0: kind = Domain::Agency::BanterSceneKind::CheatingSecret; break;
            case 1: kind = Domain::Agency::BanterSceneKind::CheatingDiscovered; break;
            default: kind = Domain::Agency::BanterSceneKind::None; break;
        }

        const std::int32_t sceneIdx =
            (cheatingType >= 0)
                ? Domain::Agency::SelectCheatingSceneVariantIndex(*snap, npc1Id, npc2Id, cheatingType, 0, rng)
                : -1;
        return makeFallbackJson(kind, sceneIdx);
    }

    std::int32_t BanterSceneCoordinator::PlanCaughtCheatingSceneIndex(
        RE::Actor* cheater,
        RE::Actor* affairPartner) {
        if (!cheater || !affairPartner || cheater->IsPlayerRef()) {
            return -1;
        }
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap) {
            return -1;
        }
        auto rng = LineageUseCases::MakeEngineRng();
        return Domain::Agency::SelectCheatingSceneVariantIndex(
            *snap,
            Foundation::ActorId{ cheater->GetFormID() },
            Foundation::ActorId{ affairPartner->GetFormID() },
            2,
            0,
            rng);
    }

    void BanterSceneCoordinator::MuteBanterSceneParticipants(
        RE::Actor* npc1,
        RE::Actor* npc2,
        RE::Actor* npc1Partner,
        bool shouldMutePartner) {
        if (!GetSkyrimNetGateway().IsAvailable()) {
            return;
        }
        auto* faction = FormCache::GetSingleton().GetSkyrimNetFilterFaction();
        if (!faction) {
            return;
        }

        std::vector<Foundation::ActorId> muted;
        auto tryMute = [&](RE::Actor* actor) {
            if (!actor || actor->IsDeleted()) {
                return;
            }
            Utils::AddToFaction(actor, faction, 0);
            muted.emplace_back(actor->GetFormID());
        };

        tryMute(npc1);
        tryMute(npc2);
        if (shouldMutePartner && npc1Partner && !npc1Partner->IsPlayerRef()) {
            tryMute(npc1Partner);
        }

        if (muted.empty()) {
            return;
        }

        State::WorldStore::GetSingleton().Modify([&muted](State::WorldState& ws) {
            auto& list = ws.cooldowns.Write().skyrimNetMutedActors;
            for (const Foundation::ActorId aid : muted) {
                if (!aid.IsNull() && std::find(list.begin(), list.end(), aid) == list.end()) {
                    list.push_back(aid);
                }
            }
        });
    }

    void BanterSceneCoordinator::UnMuteBanterSceneParticipants() {
        std::vector<Foundation::ActorId> toUnmute;
        State::WorldStore::GetSingleton().Modify([&toUnmute](State::WorldState& ws) {
            auto& list = ws.cooldowns.Write().skyrimNetMutedActors;
            toUnmute = std::move(list);
            list.clear();
        });

        if (toUnmute.empty()) {
            return;
        }
        auto* faction = FormCache::GetSingleton().GetSkyrimNetFilterFaction();
        if (!faction) {
            return;
        }
        for (const Foundation::ActorId aid : toUnmute) {
            if (HasPhysicalActor(aid.raw())) {
                Utils::RemoveFromFaction(aid.raw(), faction);
            }
        }
    }

    void BanterSceneCoordinator::RecoverStaleMutesOnLoad() {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap || !snap->cooldowns || snap->cooldowns->skyrimNetMutedActors.empty()) {
            return;
        }

        const auto candidates = snap->cooldowns->skyrimNetMutedActors;
        const bool skyrimNetAvail = GetSkyrimNetGateway().IsAvailable();
        auto* faction = FormCache::GetSingleton().GetSkyrimNetFilterFaction();

        std::vector<Foundation::ActorId> stale;
        for (const Foundation::ActorId aid : candidates) {
            bool isStale = !skyrimNetAvail || !HasPhysicalActor(aid.raw());
            if (!isStale) {
                auto* actor = RE::TESForm::LookupByID<RE::Actor>(aid.raw());
                isStale = (!actor || actor->GetCurrentScene() == nullptr);
            }
            if (isStale) {
                stale.push_back(aid);
            }
        }

        if (stale.empty()) {
            return;
        }

        if (faction) {
            for (const Foundation::ActorId aid : stale) {
                if (HasPhysicalActor(aid.raw())) {
                    Utils::RemoveFromFaction(aid.raw(), faction);
                }
            }
        }

        State::WorldStore::GetSingleton().Modify([&stale](State::WorldState& ws) {
            auto& list = ws.cooldowns.Write().skyrimNetMutedActors;
            std::erase_if(list, [&stale](Foundation::ActorId id) {
                return std::find(stale.begin(), stale.end(), id) != stale.end();
            });
        });

        MARAS_LOG_INFO("[BanterSceneCoordinator] RecoverStaleMutesOnLoad cleared {} stale SkyrimNet mutes",
                       stale.size());
    }

    void BanterSceneCoordinator::RestoreMovedBanterSceneNpcs() {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (!snap || !snap->cooldowns || snap->cooldowns->movedSceneActors.empty()) {
            return;
        }

        const auto movedCopy = snap->cooldowns->movedSceneActors;
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* playerCell = player ? player->GetParentCell() : nullptr;

        std::vector<Foundation::ActorId> restoredIds;
        for (const auto& [actorId, markerId] : movedCopy) {
            RE::Actor* actor =
                HasPhysicalActor(actorId.raw()) ? RE::TESForm::LookupByID<RE::Actor>(actorId.raw()) : nullptr;
            if (actor && playerCell && actor->GetParentCell() == playerCell) {
                continue;
            }

            RE::TESObjectREFR* markerRef =
                (!markerId.IsNull()) ? RE::TESForm::LookupByID<RE::TESObjectREFR>(markerId.raw()) : nullptr;

            if (actor && !actor->IsDeleted() && markerRef && !markerRef->IsDeleted()) {
                actor->MoveTo(markerRef);
                actor->EvaluatePackage();
            }
            if (markerRef && !markerRef->IsDeleted()) {
                markerRef->Disable();
                markerRef->SetDelete(true);
            }
            restoredIds.push_back(actorId);
        }

        if (restoredIds.empty()) {
            return;
        }

        State::WorldStore::GetSingleton().Modify([&restoredIds](State::WorldState& ws) {
            auto& map = ws.cooldowns.Write().movedSceneActors;
            for (const Foundation::ActorId aid : restoredIds) {
                map.erase(aid);
            }
        });

        MARAS_LOG_DEBUG("[BanterSceneCoordinator] RestoreMovedBanterSceneNpcs restored {} actors",
                        restoredIds.size());
    }

}  // namespace MARAS::App
