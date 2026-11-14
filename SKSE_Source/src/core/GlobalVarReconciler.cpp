#include "core/GlobalVarReconciler.h"

#include "core/DeferredActorResolver.h"
#include "core/FormCache.h"
#include "core/SessionEpoch.h"
#include "maras/state/EngineSyncPlan.h"
#include "maras/state/WorldStore.h"
#include "papyrus/PapyrusInterface.h"

#include <Windows.h>
#include <algorithm>

namespace MARAS {

    namespace {
        constexpr std::int32_t kMinStartDialGender = 0;
        constexpr std::int32_t kMaxStartDialGender = 2;
        constexpr std::int32_t kMinRelRankForDial = -4;
        constexpr std::int32_t kMaxRelRankForDial = 4;

        void SyncGlobalIfChanged(RE::TESGlobal* global, float desired, const char* name) {
            if (!global) {
                return;
            }
            if (global->value != desired) {
                global->value = desired;
                MARAS_LOG_DEBUG("[GlobalVarReconciler] Updated {} ({:08X}) to {}",
                                name, global->GetFormID(), desired);
            }
        }
    }  // namespace

    GlobalVarReconciler& GlobalVarReconciler::GetSingleton() {
        static GlobalVarReconciler instance;
        return instance;
    }

    bool GlobalVarReconciler::SyncExternalPolygamyGlobalToStore() {
        auto* global = FormCache::GetSingleton().GetEnablePolygamyGlobal();
        if (!global) {
            return false;
        }

        const bool globalPolygamy = (global->value != 0.0f);
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        if (globalPolygamy == snap->settings->enablePlayerPolygamy) {
            return false;
        }

        State::WorldStore::GetSingleton().Modify([globalPolygamy](State::WorldState& ws) {
            ws.settings.Write().enablePlayerPolygamy = globalPolygamy;
        });
        MARAS_LOG_INFO("[GlobalVarReconciler] Synced external polygamy global (0x000006) -> {} in WorldStore",
                       globalPolygamy);
        return true;
    }

    void GlobalVarReconciler::ReconcileGlobals() {
        if (!DeferredActorResolver::GetSingleton().CanSyncEngineInline()) {
            if (!pendingReconcile_.exchange(true, std::memory_order_acq_rel)) {
                if (!AddEpochTask(GetSessionEpoch(), "GlobalVarReconciler::ReconcileGlobals", []() {
                        std::lock_guard lock(PapyrusInterface::GetPapyrusMutex());
                        GlobalVarReconciler::GetSingleton().ReconcileGlobalsOnMainThread();
                    })) {
                    pendingReconcile_.store(false, std::memory_order_release);
                }
            }
            return;
        }

        ReconcileGlobalsOnMainThread();
    }

    void GlobalVarReconciler::ReconcileGlobalsOnMainThread() {
        pendingReconcile_.store(false, std::memory_order_release);

        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const bool skyrimNetLoaded = (GetModuleHandleW(L"SkyrimNet.dll") != nullptr);
        const auto plan = State::ComputeDesiredGlobals(*snap, skyrimNetLoaded);

        auto& fc = FormCache::GetSingleton();
        SyncGlobalIfChanged(fc.GetLoveInterestsCount(), static_cast<float>(plan.loveInterestsCount), "LoveInterestsCount");
        SyncGlobalIfChanged(fc.GetSpousesCount(), static_cast<float>(plan.spousesCount), "SpousesCount");
        SyncGlobalIfChanged(fc.GetPlayerHousesCount(), static_cast<float>(plan.playerHousesCount), "PlayerHousesCount");
        SyncGlobalIfChanged(fc.GetEnablePolygamyGlobal(), plan.enablePolygamy ? 1.0f : 0.0f, "EnablePolygamy");
        SyncGlobalIfChanged(fc.GetSkyrimNetInstalledGlobal(), plan.hasSkyrimNet ? 1.0f : 0.0f, "SkyrimNetInstalled");
    }

    std::int32_t GlobalVarReconciler::GetStartDialGender() const {
        auto* g = FormCache::GetSingleton().GetStartDialGenderGlobal();
        if (!g) {
            return 0;
        }
        return std::clamp(static_cast<std::int32_t>(g->value), kMinStartDialGender, kMaxStartDialGender);
    }

    void GlobalVarReconciler::SetStartDialGender(std::int32_t value) {
        const std::int32_t clamped = std::clamp(value, kMinStartDialGender, kMaxStartDialGender);
        const float target = static_cast<float>(clamped);
        if (!DeferredActorResolver::GetSingleton().CanSyncEngineInline()) {
            AddEpochTask(GetSessionEpoch(), "GlobalVarReconciler::SetStartDialGender", [target]() {
                std::lock_guard lock(PapyrusInterface::GetPapyrusMutex());
                SyncGlobalIfChanged(FormCache::GetSingleton().GetStartDialGenderGlobal(), target, "StartDialGender");
            });
            return;
        }
        SyncGlobalIfChanged(FormCache::GetSingleton().GetStartDialGenderGlobal(), target, "StartDialGender");
    }

    std::int32_t GlobalVarReconciler::GetMinRelRankForDial() const {
        auto* g = FormCache::GetSingleton().GetMinRelRankForDialGlobal();
        if (!g) {
            return kMinRelRankForDial;
        }
        return std::clamp(static_cast<std::int32_t>(g->value), kMinRelRankForDial, kMaxRelRankForDial);
    }

    void GlobalVarReconciler::SetMinRelRankForDial(std::int32_t value) {
        const std::int32_t clamped = std::clamp(value, kMinRelRankForDial, kMaxRelRankForDial);
        const float target = static_cast<float>(clamped);
        if (!DeferredActorResolver::GetSingleton().CanSyncEngineInline()) {
            AddEpochTask(GetSessionEpoch(), "GlobalVarReconciler::SetMinRelRankForDial", [target]() {
                std::lock_guard lock(PapyrusInterface::GetPapyrusMutex());
                SyncGlobalIfChanged(FormCache::GetSingleton().GetMinRelRankForDialGlobal(), target, "MinRelRankForDial");
            });
            return;
        }
        SyncGlobalIfChanged(FormCache::GetSingleton().GetMinRelRankForDialGlobal(), target, "MinRelRankForDial");
    }

    bool GlobalVarReconciler::GetPreferVanillaAudio() const {
        auto* g = FormCache::GetSingleton().GetPreferVanillaAudioGlobal();
        return g ? (g->value != 0.0f) : true;
    }

    void GlobalVarReconciler::SetPreferVanillaAudio(bool value) {
        const float target = value ? 1.0f : 0.0f;
        if (!DeferredActorResolver::GetSingleton().CanSyncEngineInline()) {
            AddEpochTask(GetSessionEpoch(), "GlobalVarReconciler::SetPreferVanillaAudio", [target]() {
                std::lock_guard lock(PapyrusInterface::GetPapyrusMutex());
                SyncGlobalIfChanged(FormCache::GetSingleton().GetPreferVanillaAudioGlobal(), target, "PreferVanillaAudio");
            });
            return;
        }
        SyncGlobalIfChanged(FormCache::GetSingleton().GetPreferVanillaAudioGlobal(), target, "PreferVanillaAudio");
    }

    bool GlobalVarReconciler::GetAllowAIDial() const {
        auto* g = FormCache::GetSingleton().GetAllowAIDialGlobal();
        return g ? (g->value != 0.0f) : true;
    }

    void GlobalVarReconciler::SetAllowAIDial(bool value) {
        const float target = value ? 1.0f : 0.0f;
        if (!DeferredActorResolver::GetSingleton().CanSyncEngineInline()) {
            AddEpochTask(GetSessionEpoch(), "GlobalVarReconciler::SetAllowAIDial", [target]() {
                std::lock_guard lock(PapyrusInterface::GetPapyrusMutex());
                SyncGlobalIfChanged(FormCache::GetSingleton().GetAllowAIDialGlobal(), target, "AllowAIDial");
            });
            return;
        }
        SyncGlobalIfChanged(FormCache::GetSingleton().GetAllowAIDialGlobal(), target, "AllowAIDial");
    }

    void GlobalVarReconciler::ResetSessionGlobalsToDefaults() {
        auto resetAll = []() {
            auto& fc = FormCache::GetSingleton();
            SyncGlobalIfChanged(fc.GetEnablePolygamyGlobal(), 0.0f, "EnablePolygamy");
            SyncGlobalIfChanged(fc.GetStartDialGenderGlobal(), 0.0f, "StartDialGender");
            SyncGlobalIfChanged(fc.GetMinRelRankForDialGlobal(), static_cast<float>(kMinRelRankForDial), "MinRelRankForDial");
            SyncGlobalIfChanged(fc.GetPreferVanillaAudioGlobal(), 1.0f, "PreferVanillaAudio");
            SyncGlobalIfChanged(fc.GetAllowAIDialGlobal(), 1.0f, "AllowAIDial");
        };
        if (!DeferredActorResolver::GetSingleton().CanSyncEngineInline()) {
            AddEpochTask(GetSessionEpoch(), "GlobalVarReconciler::ResetSessionGlobalsToDefaults", [resetAll]() {
                std::lock_guard lock(PapyrusInterface::GetPapyrusMutex());
                resetAll();
            });
            return;
        }
        resetAll();
    }

    void GlobalVarReconciler::Revert() noexcept {
        pendingReconcile_.store(false, std::memory_order_release);
    }

}  // namespace MARAS
