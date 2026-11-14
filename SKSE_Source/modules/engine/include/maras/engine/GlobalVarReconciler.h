#pragma once

#include "maras/engine/EngineLogging.h"

#include <atomic>
#include <cstdint>

namespace MARAS {

    /**
     * GlobalVarReconciler
     * -------------------
     * Converges computed TESGlobal counters/mirrors from WorldStore::Snapshot() and
     * manages clamped accessors for .ess-persisted MCM globals (0x00004B, 0x0000E4,
     * 0x000018, 0x00001C) while marshaling off-thread writes onto the main thread.
     */
    class GlobalVarReconciler {
    public:
        static GlobalVarReconciler& GetSingleton();

        // Pulls external quest edits to TTM_EnablePolygamy (0x000006) into WorldStore.
        // Returns true if WorldStore was updated.
        bool SyncExternalPolygamyGlobalToStore();

        // Converges computed/mirrored globals (0x00004F, 0x000117, 0x0000C2, 0x000006, 0x0000C4).
        void ReconcileGlobals();

        // Clamped .ess-persisted global accessors
        [[nodiscard]] std::int32_t GetStartDialGender() const;
        void SetStartDialGender(std::int32_t value);

        [[nodiscard]] std::int32_t GetMinRelRankForDial() const;
        void SetMinRelRankForDial(std::int32_t value);

        [[nodiscard]] bool GetPreferVanillaAudio() const;
        void SetPreferVanillaAudio(bool value);

        [[nodiscard]] bool GetAllowAIDial() const;
        void SetAllowAIDial(bool value);

        void ResetSessionGlobalsToDefaults();
        void Revert() noexcept;

    private:
        GlobalVarReconciler() = default;
        ~GlobalVarReconciler() = default;
        GlobalVarReconciler(const GlobalVarReconciler&) = delete;
        GlobalVarReconciler& operator=(const GlobalVarReconciler&) = delete;

        void ReconcileGlobalsOnMainThread();

        std::atomic<bool> pendingReconcile_{ false };
    };

}  // namespace MARAS
