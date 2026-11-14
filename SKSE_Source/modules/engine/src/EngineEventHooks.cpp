#include "maras/engine/EngineEventHooks.h"

#include <atomic>

namespace MARAS::Engine {

    namespace {
        std::atomic<IEngineEventHooks*> g_engineEventHooks{ nullptr };
    }

    void SetEngineEventHooks(IEngineEventHooks* hooks) noexcept {
        g_engineEventHooks.store(hooks, std::memory_order_release);
    }

    IEngineEventHooks* GetEngineEventHooks() noexcept {
        return g_engineEventHooks.load(std::memory_order_acquire);
    }

}  // namespace MARAS::Engine
