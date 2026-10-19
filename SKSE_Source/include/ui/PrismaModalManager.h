#pragma once

#include "PCH.h"
#include "utils/Common.h"

#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace MARAS {

    enum class ModalTone : uint8_t {
        Default = 0,
        Warning = 1,
        Danger = 2,
        Success = 3,
        Ai = 4
    };

    enum class ModalSize : uint8_t {
        Sm = 0,
        Md = 1,
        Lg = 2,
        Xl = 3
    };

    enum class ModalButtonVariant : uint8_t {
        Primary = 0,
        Secondary = 1,
        Outline = 2,
        Ghost = 3,
        Danger = 4
    };

    enum class ModalControlType : uint8_t {
        Checkbox = 0,
        Select = 1,
        TextInput = 2,
        Checklist = 3,
        InfoBox = 4
    };

    struct ModalSelectOption {
        std::string value;
        std::string label;
        bool disabled = false;
        std::string tooltip;

        nlohmann::json ToJson() const;
    };

    struct ModalChecklistItem {
        std::string id;
        std::string label;
        std::string subtitle;
        std::string description;
        std::string badgeText;
        std::string badgeTone = "default";  // "default" | "accent" | "warning" | "danger" | "success"
        bool checked = true;
        bool checkboxDisabled = false;

        // Optional per-item inline dropdown (e.g. target relationship status selector)
        std::string selectKey;
        std::string selectLabel;
        std::string selectValue;
        std::vector<ModalSelectOption> selectOptions;

        nlohmann::json meta = nlohmann::json::object();

        nlohmann::json ToJson() const;
    };

    struct ModalControl {
        ModalControlType type = ModalControlType::Checkbox;
        std::string id;
        std::string label;
        std::string description;
        bool required = false;
        bool disabled = false;

        nlohmann::json defaultValue = nullptr;
        std::string placeholder;

        // For Select control
        std::vector<ModalSelectOption> options;

        // For Checklist control
        std::vector<ModalChecklistItem> items;
        bool showSelectAllToggle = true;
        std::string emptyMessage;

        // For InfoBox control
        ModalTone tone = ModalTone::Default;

        static ModalControl MakeCheckbox(std::string id,
                                         std::string label,
                                         bool defaultChecked = false,
                                         std::string description = "",
                                         bool disabled = false);

        static ModalControl MakeSelect(std::string id,
                                       std::string label,
                                       std::vector<ModalSelectOption> options,
                                       std::string defaultValue = "",
                                       std::string description = "",
                                       bool disabled = false);

        static ModalControl MakeTextInput(std::string id,
                                          std::string label,
                                          std::string defaultValue = "",
                                          std::string placeholder = "",
                                          std::string description = "",
                                          bool disabled = false);

        static ModalControl MakeChecklist(std::string id,
                                          std::string label,
                                          std::vector<ModalChecklistItem> items,
                                          bool showSelectAllToggle = true,
                                          std::string description = "",
                                          std::string emptyMessage = "");

        static ModalControl MakeInfoBox(std::string id,
                                        std::string label,
                                        std::string description,
                                        ModalTone tone = ModalTone::Default);

        nlohmann::json ToJson() const;
    };

    struct ModalButton {
        std::string id;
        std::string label;
        ModalButtonVariant variant = ModalButtonVariant::Secondary;
        bool disabled = false;
        std::string tooltip;

        // Optional constraint: require at least minCheckedCount items checked in requireChecklistId
        std::string requireChecklistId;
        int32_t minCheckedCount = 0;

        static ModalButton Make(std::string id,
                                std::string label,
                                ModalButtonVariant variant = ModalButtonVariant::Secondary,
                                bool disabled = false,
                                std::string tooltip = "");

        nlohmann::json ToJson() const;
    };

    struct ModalRequest {
        std::string modalType = "generic";
        std::string title;
        std::string subtitle;
        std::string message;
        ModalTone tone = ModalTone::Default;
        ModalSize size = ModalSize::Md;
        std::vector<ModalControl> controls;
        std::vector<ModalButton> buttons;
        int32_t defaultCancelButtonIndex = 1;
        nlohmann::json customData = nlohmann::json::object();

        nlohmann::json ToJson(const std::string& assignedModalId) const;
    };

    struct ModalResponse {
        std::string modalId;
        std::string buttonId;
        int32_t buttonIndex = -1;
        bool aborted = false;
        nlohmann::json values = nlohmann::json::object();

        bool GetBool(std::string_view controlId, bool defaultVal = false) const;
        std::string GetString(std::string_view controlId, std::string defaultVal = "") const;
        int32_t GetInt(std::string_view controlId, int32_t defaultVal = 0) const;

        // Checklist value helpers
        std::vector<std::string> GetCheckedItemIds(std::string_view checklistId) const;
        std::vector<std::string> GetUncheckedItemIds(std::string_view checklistId) const;
        bool IsItemChecked(std::string_view checklistId, std::string_view itemId, bool defaultVal = false) const;
        std::string GetItemSelectValue(std::string_view checklistId,
                                       std::string_view itemId,
                                       std::string defaultVal = "") const;
    };

    class PrismaModalManager {
    public:
        using ModalCallback = std::function<void(const ModalResponse&)>;

        static PrismaModalManager& GetSingleton() {
            static PrismaModalManager singleton;
            return singleton;
        }

        // Enqueues a structured modal request and displays it via Prisma UI (game paused, focus locked).
        // Returns the generated modalId. Callback is always invoked on the SKSE main game thread.
        std::string ShowModal(ModalRequest request, ModalCallback callback);

        // Enqueues a modal defined by a raw JSON object string.
        std::string ShowRawJsonModal(const std::string& modalSpecJson, ModalCallback callback);

        // Processes an incoming modal button selection from the Prisma UI bridge (called on main game thread).
        void HandleModalResponse(const nlohmann::json& payload);

        // Returns true if at least one modal is queued or currently displayed.
        bool HasActiveModal() const;

        // Pushes the currently active front modal to the Prisma UI view (called when DOM/UI is ready or queue advances).
        void SendActiveModalToUI(bool forceResend = false);

        // Clears all pending/active modals and resets state on save revert or preload.
        void Revert();

        static std::string_view ModalToneToString(ModalTone tone);
        static std::string_view ModalSizeToString(ModalSize size);
        static std::string_view ModalButtonVariantToString(ModalButtonVariant variant);
        static std::string_view ModalControlTypeToString(ModalControlType type);

    private:
        struct PendingModal {
            std::string modalId;
            nlohmann::json serializedPayload;
            int32_t defaultCancelButtonIndex = 1;
            ModalCallback callback;
            bool dispatchedToUI = false;
        };

        void ProcessQueueOnGameThread();

        mutable std::mutex m_mutex;
        std::deque<PendingModal> m_queue;
        uint64_t m_nextModalCounter = 1;

        PrismaModalManager() = default;
        ~PrismaModalManager() = default;
        PrismaModalManager(const PrismaModalManager&) = delete;
        PrismaModalManager& operator=(const PrismaModalManager&) = delete;
    };

}  // namespace MARAS

