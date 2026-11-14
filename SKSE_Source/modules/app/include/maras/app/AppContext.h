#pragma once

#include "maras/app/ModalGateway.h"
#include "maras/app/SkyrimNetGateway.h"
#include "maras/app/UiBridgeGateway.h"

#include <functional>
#include <string>
#include <string_view>

namespace MARAS::App {

    void SetUiBridgeGateway(IUiBridgeGateway* gateway) noexcept;
    void SetModalGateway(IModalGateway* gateway) noexcept;
    void SetSkyrimNetGateway(ISkyrimNetGateway* gateway) noexcept;

    IUiBridgeGateway& GetUiBridgeGateway() noexcept;
    IModalGateway& GetModalGateway() noexcept;
    ISkyrimNetGateway& GetSkyrimNetGateway() noexcept;

    class ScopedMemorySuppression {
    public:
        ScopedMemorySuppression() { GetSkyrimNetGateway().PushSuppressMemories(); }
        ~ScopedMemorySuppression() { GetSkyrimNetGateway().PopSuppressMemories(); }
        ScopedMemorySuppression(const ScopedMemorySuppression&) = delete;
        ScopedMemorySuppression& operator=(const ScopedMemorySuppression&) = delete;
    };

    class ScopedMatchmakingApply {
    public:
        ScopedMatchmakingApply() { GetSkyrimNetGateway().PushMatchmakingApply(); }
        ~ScopedMatchmakingApply() { GetSkyrimNetGateway().PopMatchmakingApply(); }
        ScopedMatchmakingApply(const ScopedMatchmakingApply&) = delete;
        ScopedMatchmakingApply& operator=(const ScopedMatchmakingApply&) = delete;
    };

    class ScopedProfileSuppression {
    public:
        ScopedProfileSuppression() { GetSkyrimNetGateway().PushSuppressProfiles(); }
        ~ScopedProfileSuppression() { GetSkyrimNetGateway().PopSuppressProfiles(); }
        ScopedProfileSuppression(const ScopedProfileSuppression&) = delete;
        ScopedProfileSuppression& operator=(const ScopedProfileSuppression&) = delete;
    };

    void ShowLLMRetryModal(const std::string& evaluationType,
                           const std::string& description,
                           LLMFailureType failureType,
                           std::function<void()> onRetry,
                           std::function<void()> onIgnore,
                           std::string_view llmVariant = "gamemaster_evaluation");

}  // namespace MARAS::App
