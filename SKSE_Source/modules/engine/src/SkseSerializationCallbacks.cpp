#include "maras/engine/SkseSerializationCallbacks.h"

#include "maras/engine/ActorService.h"
#include "maras/engine/BuffReconciler.h"
#include "maras/engine/ChildVisualReconciler.h"
#include "maras/engine/ChildrenFleeService.h"
#include "maras/engine/DeferredActorResolver.h"
#include "maras/engine/DialogueEventSink.h"
#include "maras/engine/FertilityNamingMenuSink.h"
#include "maras/engine/GiftMenuEventSink.h"
#include "maras/engine/PlayerMiscStatsTracker.h"
#include "maras/engine/EngineEventHooks.h"
#include "maras/engine/EngineLogging.h"
#include "maras/engine/FactionReconciler.h"
#include "maras/engine/FormCache.h"
#include "maras/engine/GlobalVarReconciler.h"
#include "maras/engine/IntimateThreadService.h"
#include "maras/engine/PackageReconciler.h"
#include "maras/engine/PollingService.h"
#include "maras/engine/SpatialWorldIndex.h"
#include "maras/engine/TaskDispatcher.h"
#include "maras/foundation/IRng.h"
#include "maras/state/WorldStore.h"

#include <random>
#include <utility>

namespace MARAS::Serialization {

    bool SkseCoSaveReader::NextRecordInfo(
        std::uint32_t& outType,
        std::uint32_t& outVersion,
        std::uint32_t& outLength) {
        return serialization_ != nullptr && serialization_->GetNextRecordInfo(outType, outVersion, outLength);
    }

    bool SkseCoSaveReader::ReadBytes(void* dst, std::uint32_t numBytes) {
        if (numBytes == 0) {
            return true;
        }
        return serialization_ != nullptr && dst != nullptr && serialization_->ReadRecordData(dst, numBytes);
    }

    bool SkseCoSaveWriter::OpenRecord(std::uint32_t type, std::uint32_t version) {
        return serialization_ != nullptr && serialization_->OpenRecord(type, version);
    }

    bool SkseCoSaveWriter::WriteBytes(const void* src, std::uint32_t numBytes) {
        if (numBytes == 0) {
            return true;
        }
        return serialization_ != nullptr && src != nullptr && serialization_->WriteRecordData(src, numBytes);
    }

    bool SkseFormIdResolver::ResolveFormId(std::uint32_t oldRawId, std::uint32_t& outNewRawId) const noexcept {
        if (oldRawId == 0) {
            outNewRawId = 0;
            return false;
        }
        if (!serialization_) {
            outNewRawId = oldRawId;
            return true;
        }
        RE::FormID resolved = 0;
        if (!serialization_->ResolveFormID(oldRawId, resolved) || resolved == 0) {
            outNewRawId = 0;
            return false;
        }
        outNewRawId = resolved;
        return true;
    }

    State::ActorSex SkseFormIdResolver::GetSex(Foundation::ActorId actor) const noexcept {
        if (actor.IsNull() || actor.IsUnknownFather() || actor.IsSyntheticChild()) {
            return State::ActorSex::Unknown;
        }
        const std::uint8_t sex = ActorSvc::GetSex(actor.raw());
        if (sex == 0) {
            return State::ActorSex::Male;
        }
        if (sex == 1) {
            return State::ActorSex::Female;
        }
        return State::ActorSex::Unknown;
    }

    bool SkseFormIdResolver::IsAliveAndValid(Foundation::ActorId actor) const noexcept {
        if (actor.IsNull() || actor.IsUnknownFather() || actor.IsSyntheticChild()) {
            return false;
        }
        if (actor.IsPlayer()) {
            return true;
        }
        if (auto* loadedActor = SpatialWorldIndex::GetSingleton().GetActorFromFormId(actor.raw())) {
            return !loadedActor->IsDead() && !loadedActor->IsDeleted();
        }
        if (auto* form = RE::TESForm::LookupByID(actor.raw())) {
            if (form->IsDeleted()) {
                return false;
            }
            if (auto* act = form->As<RE::Actor>()) {
                return !act->IsDead();
            }
        }
        return ActorSvc::IsValid(actor.raw());
    }

    bool SkseFormIdResolver::IsDynamicActorPresent(Foundation::RefId refId) const noexcept {
        if (refId.IsNull()) {
            return false;
        }
        auto* form = RE::TESForm::LookupByID(refId.raw());
        if (!form || form->IsDeleted()) {
            return false;
        }
        return form->As<RE::Actor>() != nullptr;
    }

    std::string SkseFormIdResolver::GetActorName(Foundation::ActorId actor) const {
        if (actor.IsNull() || actor.IsUnknownFather() || actor.IsSyntheticChild()) {
            return {};
        }
        if (actor.IsPlayer()) {
            if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                const char* nm = player->GetName();
                if (nm && nm[0] != '\0') {
                    return nm;
                }
            }
            return "Player";
        }
        return ActorSvc::GetName(actor.raw());
    }

    Foundation::FormRawId SkseFormIdResolver::GetActorRaceId(Foundation::ActorId actor) const noexcept {
        if (actor.IsNull() || actor.IsUnknownFather() || actor.IsSyntheticChild()) {
            return {};
        }
        if (auto* race = ActorSvc::GetRace(actor.raw())) {
            return Foundation::FormRawId{ race->GetFormID() };
        }
        return {};
    }

    Foundation::BaseId SkseFormIdResolver::GetActorBaseId(Foundation::ActorId actor) const noexcept {
        if (actor.IsNull() || actor.IsUnknownFather() || actor.IsSyntheticChild()) {
            return {};
        }
        if (actor.IsPlayer()) {
            return Foundation::BaseId{ Foundation::kPlayerBaseRawId };
        }
        if (actor.IsDynamicRuntimeActor()) {
            return Foundation::BaseId{ actor.raw() };
        }
        const RE::FormID baseId = SpatialWorldIndex::GetSingleton().GetBaseID(actor.raw());
        if (baseId != 0) {
            return Foundation::BaseId{ baseId };
        }
        if (auto* npcBase = ActorSvc::GetActorBase(actor.raw())) {
            return Foundation::BaseId{ npcBase->GetFormID() };
        }
        return {};
    }

    void SaveCallback(SKSE::SerializationInterface* serialization) {
        if (!serialization) {
            MARAS_LOG_ERROR("SaveCallback: serialization interface is null");
            return;
        }

        const State::WorldState stateCopy = State::WorldStore::GetSingleton().ReadLive(
            [](const State::WorldState& ws) { return ws; });
        SkseCoSaveWriter writer(serialization);
        const std::string_view modVersion = Engine::GetInstalledModVersion();
        if (!State::SaveWorldState(stateCopy, writer, modVersion)) {
            MARAS_LOG_ERROR("SaveCallback: Failed to save MARAS WorldState co-save chunks");
        } else {
            MARAS_LOG_INFO("SaveCallback: Successfully saved MARAS WorldState (version: {})", modVersion);
        }
    }

    void LoadCallback(SKSE::SerializationInterface* serialization) {
        if (!serialization) {
            MARAS_LOG_ERROR("LoadCallback: serialization interface is null");
            return;
        }

        auto* hooks = Engine::GetEngineEventHooks();
        struct CoSaveLoadScopeGuard {
            Engine::IEngineEventHooks* hooks{ nullptr };
            bool finalized{ false };
            ~CoSaveLoadScopeGuard() {
                if (hooks && !finalized) {
                    hooks->AbortCoSaveLoad();
                }
            }
        };
        CoSaveLoadScopeGuard loadGuard{ hooks, false };
        if (hooks) {
            hooks->BeginCoSaveLoad();
        }

        State::WorldState staged{};
        auto initialSettings = State::WorldStore::GetSingleton().GetIniBaselineSettings();
        if (auto* polyGlobal = FormCache::GetSingleton().GetEnablePolygamyGlobal()) {
            initialSettings.enablePlayerPolygamy = (polyGlobal->value != 0.0f);
        } else {
            initialSettings.enablePlayerPolygamy = false;
        }
        staged.settings.Reset(initialSettings);

        SkseCoSaveReader reader(serialization);
        SkseFormIdResolver resolver(serialization);
        Foundation::DeterministicRng rng(std::random_device{}());
        const auto report = State::LoadWorldState(staged, reader, resolver, &rng);

        MARAS_LOG_INFO("LoadCallback: Co-save load completed (loaded={}, failed={}, skipped={}, allSucceeded={})",
                       report.loadedChunkCount, report.failedChunkCount, report.skippedChunkCount,
                       report.allChunksSucceeded);

        const bool loadedAny = report.loadedChunkCount > 0;
        if (loadedAny) {
            State::WorldStore::GetSingleton().ReplaceState(std::move(staged));
        }

        if (hooks) {
            loadGuard.finalized = true;
            hooks->FinalizeCoSaveLoad(loadedAny, report);
        }

        if (loadedAny) {
            GlobalVarReconciler::GetSingleton().SyncExternalPolygamyGlobalToStore();
            GlobalVarReconciler::GetSingleton().ReconcileGlobals();
            FactionReconciler::GetSingleton().ReconcileAllOnLoad();
            PackageReconciler::GetSingleton().ReconcileWorld();
        }
    }

    void RevertCallback(SKSE::SerializationInterface*) {
        BumpSessionEpoch("Serialization::RevertCallback");
        ResetPluginEventSinks();
        DeferredActorResolver::GetSingleton().Revert();
        GlobalVarReconciler::GetSingleton().Revert();
        FactionReconciler::GetSingleton().Revert();
        BuffReconciler::GetSingleton().Revert();
        PollingService::GetSingleton().Shutdown();
        State::WorldStore::GetSingleton().Revert();
        PackageReconciler::GetSingleton().Revert();
        ChildVisualReconciler::GetSingleton().Revert();
        SpatialWorldIndex::GetSingleton().ClearLoadedActors();
        IntimateThreadService::GetSingleton().ClearAll();
        ChildrenFleeService::GetSingleton().Reset();
        if (auto* dialogueSink = DialogueEventSink::GetSingleton()) {
            dialogueSink->Revert();
        }
        if (auto* giftSink = GiftMenuEventSink::GetSingleton()) {
            giftSink->Revert();
        }
        if (auto* namingSink = FertilityNamingMenuSink::GetSingleton()) {
            namingSink->Revert();
        }
        if (auto* statsTracker = PlayerMiscStatsTracker::GetSingleton()) {
            statsTracker->Revert();
        }

        if (auto* hooks = Engine::GetEngineEventHooks()) {
            hooks->OnCoSaveRevert();
        }

        MARAS_LOG_INFO("Reverted MARAS co-save runtime state");
    }

}  // namespace MARAS::Serialization
