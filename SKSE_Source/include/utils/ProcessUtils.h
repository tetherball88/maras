#pragma once

#include "utils/Common.h"

namespace MARAS::ProcessUtils {

    /**
     * Iterate all loaded NPC actors across all 4 process list tiers.
     *
     * @note Thread-Safety: MUST only be called on the main game thread. RE::ProcessLists arrays
     *       (highActorHandles, middleHighActorHandles, middleLowActorHandles, lowActorHandles)
     *       are mutated by the engine during frame updates and are not safe to iterate from
     *       background threads (e.g., async SkyrimNet decorator threads).
     * @note Player Exclusion: RE::ProcessLists only contains AI-driven NPCs and does NOT include
     *       RE::PlayerCharacter (0x14). Pass includePlayer = true if the player should also be
     *       visited first.
     * @note Re-Entrancy Safety: Handle arrays are snapshotted per tier before invoking `fn` so
     *       callbacks that trigger `EvaluatePackage` or process-level transitions cannot
     *       invalidate the active tier iterator.
     *
     * @param fn Callback receiving RE::Actor*. Return true to stop iteration early, false to continue.
     * @param includePlayer If true, visits RE::PlayerCharacter::GetSingleton() before process lists.
     */
    template <typename Func>
    void ForEachLoadedActor(Func&& fn, bool includePlayer = false) {
        if (includePlayer) {
            if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                if (fn(player)) return;
            }
        }

        auto* processLists = RE::ProcessLists::GetSingleton();
        if (!processLists) return;

        std::vector<RE::ActorHandle> snapshot;

        auto iterateTier = [&fn, &snapshot](const auto& handles) -> bool {
            snapshot.clear();
            snapshot.reserve(handles.size());
            for (const auto& handle : handles) {
                snapshot.push_back(handle);
            }
            for (const auto& handle : snapshot) {
                if (auto actorPtr = handle.get()) {
                    if (fn(actorPtr.get())) return true;
                }
            }
            return false;
        };

        if (iterateTier(processLists->highActorHandles)) return;
        if (iterateTier(processLists->middleHighActorHandles)) return;
        if (iterateTier(processLists->middleLowActorHandles)) return;
        iterateTier(processLists->lowActorHandles);
    }

}  // namespace MARAS::ProcessUtils
