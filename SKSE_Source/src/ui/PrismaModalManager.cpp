#include "ui/PrismaModalManager.h"

#include "ui/PrismaUIManager.h"

#include <charconv>

namespace MARAS {

    namespace {

        std::string SafeGetJsonString(const nlohmann::json& j, std::string_view key, std::string defaultVal = "") {
            if (!j.is_object()) return defaultVal;
            auto it = j.find(key);
            if (it == j.end() || it->is_null()) return defaultVal;
            if (it->is_string()) return it->get<std::string>();
            if (it->is_number_integer()) return std::to_string(it->get<int64_t>());
            if (it->is_number_unsigned()) return std::to_string(it->get<uint64_t>());
            if (it->is_boolean()) return it->get<bool>() ? "true" : "false";
            return defaultVal;
        }

        bool SafeGetJsonBool(const nlohmann::json& j, std::string_view key, bool defaultVal = false) {
            if (!j.is_object()) return defaultVal;
            auto it = j.find(key);
            if (it == j.end() || it->is_null()) return defaultVal;
            if (it->is_boolean()) return it->get<bool>();
            if (it->is_number()) return it->get<double>() != 0.0;
            if (it->is_string()) {
                const auto& s = it->get_ref<const std::string&>();
                if (s == "true" || s == "1") return true;
                if (s == "false" || s == "0") return false;
            }
            return defaultVal;
        }

        int32_t SafeGetJsonInt(const nlohmann::json& j, std::string_view key, int32_t defaultVal = 0) {
            if (!j.is_object()) return defaultVal;
            auto it = j.find(key);
            if (it == j.end() || it->is_null()) return defaultVal;
            if (it->is_number_integer() || it->is_number_unsigned()) return static_cast<int32_t>(it->get<int64_t>());
            if (it->is_number_float()) return static_cast<int32_t>(it->get<double>());
            if (it->is_boolean()) return it->get<bool>() ? 1 : 0;
            if (it->is_string()) {
                const auto& s = it->get_ref<const std::string&>();
                int32_t parsed = defaultVal;
                const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), parsed);
                if (ec == std::errc{}) return parsed;
            }
            return defaultVal;
        }

    }  // namespace

    // =========================================================================
    // Enum String Conversions
    // =========================================================================

    std::string_view PrismaModalManager::ModalToneToString(ModalTone tone) {
        switch (tone) {
            case ModalTone::Warning: return "warning";
            case ModalTone::Danger:  return "danger";
            case ModalTone::Success: return "success";
            case ModalTone::Ai:      return "ai";
            case ModalTone::Default:
            default:                 return "default";
        }
    }

    std::string_view PrismaModalManager::ModalSizeToString(ModalSize size) {
        switch (size) {
            case ModalSize::Sm: return "sm";
            case ModalSize::Lg: return "lg";
            case ModalSize::Xl: return "xl";
            case ModalSize::Md:
            default:            return "md";
        }
    }

    std::string_view PrismaModalManager::ModalButtonVariantToString(ModalButtonVariant variant) {
        switch (variant) {
            case ModalButtonVariant::Primary:   return "default";
            case ModalButtonVariant::Outline:   return "outline";
            case ModalButtonVariant::Ghost:     return "ghost";
            case ModalButtonVariant::Danger:    return "danger";
            case ModalButtonVariant::Secondary:
            default:                            return "secondary";
        }
    }

    std::string_view PrismaModalManager::ModalControlTypeToString(ModalControlType type) {
        switch (type) {
            case ModalControlType::Checkbox:  return "checkbox";
            case ModalControlType::Select:    return "select";
            case ModalControlType::TextInput: return "text";
            case ModalControlType::Checklist: return "checklist";
            case ModalControlType::InfoBox:   return "infobox";
            default:                          return "checkbox";
        }
    }

    // =========================================================================
    // Struct JSON Serialization & Builders
    // =========================================================================

    nlohmann::json ModalSelectOption::ToJson() const {
        nlohmann::json j;
        j["value"] = value;
        j["label"] = label;
        if (disabled) j["disabled"] = true;
        if (!tooltip.empty()) j["tooltip"] = tooltip;
        return j;
    }

    nlohmann::json ModalChecklistItem::ToJson() const {
        nlohmann::json j;
        j["id"] = id;
        j["label"] = label;
        if (!subtitle.empty()) j["subtitle"] = subtitle;
        if (!description.empty()) j["description"] = description;
        if (!badgeText.empty()) {
            j["badgeText"] = badgeText;
            j["badgeTone"] = badgeTone.empty() ? "default" : badgeTone;
        }
        j["checked"] = checked;
        if (checkboxDisabled) j["checkboxDisabled"] = true;

        if (!selectOptions.empty()) {
            if (!selectKey.empty()) j["selectKey"] = selectKey;
            if (!selectLabel.empty()) j["selectLabel"] = selectLabel;
            j["selectValue"] = selectValue;
            nlohmann::json opts = nlohmann::json::array();
            for (const auto& opt : selectOptions) {
                opts.push_back(opt.ToJson());
            }
            j["selectOptions"] = std::move(opts);
        }

        if (meta.is_object() && !meta.empty()) {
            j["meta"] = meta;
        }
        return j;
    }

    ModalControl ModalControl::MakeCheckbox(std::string id,
                                            std::string label,
                                            bool defaultChecked,
                                            std::string description,
                                            bool disabled) {
        ModalControl ctrl;
        ctrl.type = ModalControlType::Checkbox;
        ctrl.id = std::move(id);
        ctrl.label = std::move(label);
        ctrl.description = std::move(description);
        ctrl.defaultValue = defaultChecked;
        ctrl.disabled = disabled;
        return ctrl;
    }

    ModalControl ModalControl::MakeSelect(std::string id,
                                          std::string label,
                                          std::vector<ModalSelectOption> options,
                                          std::string defaultValue,
                                          std::string description,
                                          bool disabled) {
        ModalControl ctrl;
        ctrl.type = ModalControlType::Select;
        ctrl.id = std::move(id);
        ctrl.label = std::move(label);
        ctrl.description = std::move(description);
        ctrl.options = std::move(options);
        if (!defaultValue.empty()) {
            ctrl.defaultValue = std::move(defaultValue);
        } else if (!ctrl.options.empty()) {
            ctrl.defaultValue = ctrl.options.front().value;
        } else {
            ctrl.defaultValue = "";
        }
        ctrl.disabled = disabled;
        return ctrl;
    }

    ModalControl ModalControl::MakeTextInput(std::string id,
                                             std::string label,
                                             std::string defaultValue,
                                             std::string placeholder,
                                             std::string description,
                                             bool disabled) {
        ModalControl ctrl;
        ctrl.type = ModalControlType::TextInput;
        ctrl.id = std::move(id);
        ctrl.label = std::move(label);
        ctrl.defaultValue = std::move(defaultValue);
        ctrl.placeholder = std::move(placeholder);
        ctrl.description = std::move(description);
        ctrl.disabled = disabled;
        return ctrl;
    }

    ModalControl ModalControl::MakeChecklist(std::string id,
                                             std::string label,
                                             std::vector<ModalChecklistItem> items,
                                             bool showSelectAllToggle,
                                             std::string description,
                                             std::string emptyMessage) {
        ModalControl ctrl;
        ctrl.type = ModalControlType::Checklist;
        ctrl.id = std::move(id);
        ctrl.label = std::move(label);
        ctrl.items = std::move(items);
        ctrl.showSelectAllToggle = showSelectAllToggle;
        ctrl.description = std::move(description);
        ctrl.emptyMessage = std::move(emptyMessage);
        return ctrl;
    }

    ModalControl ModalControl::MakeInfoBox(std::string id,
                                           std::string label,
                                           std::string description,
                                           ModalTone tone) {
        ModalControl ctrl;
        ctrl.type = ModalControlType::InfoBox;
        ctrl.id = std::move(id);
        ctrl.label = std::move(label);
        ctrl.description = std::move(description);
        ctrl.tone = tone;
        return ctrl;
    }

    nlohmann::json ModalControl::ToJson() const {
        nlohmann::json j;
        j["type"] = PrismaModalManager::ModalControlTypeToString(type);
        j["id"] = id;
        if (!label.empty()) j["label"] = label;
        if (!description.empty()) j["description"] = description;
        if (required) j["required"] = true;
        if (disabled) j["disabled"] = true;
        if (!defaultValue.is_null()) j["defaultValue"] = defaultValue;
        if (!placeholder.empty()) j["placeholder"] = placeholder;

        if (type == ModalControlType::Select) {
            nlohmann::json opts = nlohmann::json::array();
            for (const auto& opt : options) {
                opts.push_back(opt.ToJson());
            }
            j["options"] = std::move(opts);
        } else if (type == ModalControlType::Checklist) {
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& item : items) {
                arr.push_back(item.ToJson());
            }
            j["items"] = std::move(arr);
            j["showSelectAllToggle"] = showSelectAllToggle;
            if (!emptyMessage.empty()) j["emptyMessage"] = emptyMessage;
        } else if (type == ModalControlType::InfoBox) {
            j["tone"] = PrismaModalManager::ModalToneToString(tone);
        }

        return j;
    }

    ModalButton ModalButton::Make(std::string id,
                                  std::string label,
                                  ModalButtonVariant variant,
                                  bool disabled,
                                  std::string tooltip) {
        ModalButton btn;
        btn.id = std::move(id);
        btn.label = std::move(label);
        btn.variant = variant;
        btn.disabled = disabled;
        btn.tooltip = std::move(tooltip);
        return btn;
    }

    nlohmann::json ModalButton::ToJson() const {
        nlohmann::json j;
        j["id"] = id;
        j["label"] = label;
        j["variant"] = PrismaModalManager::ModalButtonVariantToString(variant);
        if (disabled) j["disabled"] = true;
        if (!tooltip.empty()) j["tooltip"] = tooltip;
        if (!requireChecklistId.empty()) {
            j["requireChecklistId"] = requireChecklistId;
            j["minCheckedCount"] = minCheckedCount;
        }
        return j;
    }

    nlohmann::json ModalRequest::ToJson(const std::string& assignedModalId) const {
        nlohmann::json j;
        j["modalId"] = assignedModalId;
        j["modalType"] = modalType.empty() ? "generic" : modalType;
        j["title"] = title;
        if (!subtitle.empty()) j["subtitle"] = subtitle;
        if (!message.empty()) j["message"] = message;
        j["tone"] = PrismaModalManager::ModalToneToString(tone);
        j["size"] = PrismaModalManager::ModalSizeToString(size);

        if (!controls.empty()) {
            nlohmann::json ctrlArr = nlohmann::json::array();
            for (const auto& ctrl : controls) {
                ctrlArr.push_back(ctrl.ToJson());
            }
            j["controls"] = std::move(ctrlArr);
        }

        nlohmann::json btnArr = nlohmann::json::array();
        for (const auto& btn : buttons) {
            btnArr.push_back(btn.ToJson());
        }
        j["buttons"] = std::move(btnArr);

        if (customData.is_object() && !customData.empty()) {
            j["customData"] = customData;
        }

        return j;
    }

    // =========================================================================
    // ModalResponse Typed Helpers
    // =========================================================================

    bool ModalResponse::GetBool(std::string_view controlId, bool defaultVal) const {
        return SafeGetJsonBool(values, controlId, defaultVal);
    }

    std::string ModalResponse::GetString(std::string_view controlId, std::string defaultVal) const {
        return SafeGetJsonString(values, controlId, std::move(defaultVal));
    }

    int32_t ModalResponse::GetInt(std::string_view controlId, int32_t defaultVal) const {
        return SafeGetJsonInt(values, controlId, defaultVal);
    }

    std::vector<std::string> ModalResponse::GetCheckedItemIds(std::string_view checklistId) const {
        std::vector<std::string> out;
        if (!values.is_object()) return out;
        auto it = values.find(checklistId);
        if (it == values.end() || !it->is_object()) return out;

        auto checkedIt = it->find("checkedIds");
        if (checkedIt != it->end() && checkedIt->is_array()) {
            out.reserve(checkedIt->size());
            for (const auto& elem : *checkedIt) {
                if (elem.is_string()) {
                    out.push_back(elem.get<std::string>());
                }
            }
        }
        return out;
    }

    std::vector<std::string> ModalResponse::GetUncheckedItemIds(std::string_view checklistId) const {
        std::vector<std::string> out;
        if (!values.is_object()) return out;
        auto it = values.find(checklistId);
        if (it == values.end() || !it->is_object()) return out;

        auto uncheckedIt = it->find("uncheckedIds");
        if (uncheckedIt != it->end() && uncheckedIt->is_array()) {
            out.reserve(uncheckedIt->size());
            for (const auto& elem : *uncheckedIt) {
                if (elem.is_string()) {
                    out.push_back(elem.get<std::string>());
                }
            }
        }
        return out;
    }

    bool ModalResponse::IsItemChecked(std::string_view checklistId, std::string_view itemId, bool defaultVal) const {
        if (!values.is_object()) return defaultVal;
        auto it = values.find(checklistId);
        if (it == values.end() || !it->is_object()) return defaultVal;

        auto itemsIt = it->find("items");
        if (itemsIt == it->end() || !itemsIt->is_object()) return defaultVal;

        auto itemIt = itemsIt->find(itemId);
        if (itemIt == itemsIt->end() || !itemIt->is_object()) return defaultVal;

        return SafeGetJsonBool(*itemIt, "checked", defaultVal);
    }

    std::string ModalResponse::GetItemSelectValue(std::string_view checklistId,
                                                  std::string_view itemId,
                                                  std::string defaultVal) const {
        if (!values.is_object()) return defaultVal;
        auto it = values.find(checklistId);
        if (it == values.end() || !it->is_object()) return defaultVal;

        auto itemsIt = it->find("items");
        if (itemsIt == it->end() || !itemsIt->is_object()) return defaultVal;

        auto itemIt = itemsIt->find(itemId);
        if (itemIt == itemsIt->end() || !itemIt->is_object()) return defaultVal;

        return SafeGetJsonString(*itemIt, "selectValue", std::move(defaultVal));
    }

    // =========================================================================
    // PrismaModalManager Implementation
    // =========================================================================

    std::string PrismaModalManager::ShowModal(ModalRequest request, ModalCallback callback) {
        std::string modalId;
        {
            std::lock_guard lock(m_mutex);
            modalId = std::format("maras_modal_{}", m_nextModalCounter++);

            // Ensure at least one button exists so the user can always resolve the modal
            if (request.buttons.empty()) {
                request.buttons.push_back(ModalButton::Make("ok", "OK", ModalButtonVariant::Primary));
            }

            PendingModal pending;
            pending.modalId = modalId;
            pending.serializedPayload = request.ToJson(modalId);
            pending.defaultCancelButtonIndex = request.defaultCancelButtonIndex;
            pending.callback = std::move(callback);
            pending.dispatchedToUI = false;

            m_queue.push_back(std::move(pending));
        }

        MARAS_LOG_INFO("PrismaModalManager::ShowModal queued modalId='{}' title='{}'", modalId, request.title);

        if (auto* taskIF = SKSE::GetTaskInterface()) {
            taskIF->AddTask([]() {
                try {
                    PrismaModalManager::GetSingleton().ProcessQueueOnGameThread();
                } catch (const std::exception& e) {
                    MARAS_LOG_ERROR("PrismaModalManager: Exception in ProcessQueueOnGameThread task: {}", e.what());
                }
            });
        } else {
            MARAS_LOG_ERROR("PrismaModalManager::ShowModal: SKSE TaskInterface unavailable");
        }

        return modalId;
    }

    std::string PrismaModalManager::ShowRawJsonModal(const std::string& modalSpecJson, ModalCallback callback) {
        auto parsed = nlohmann::json::parse(modalSpecJson, nullptr, /*exceptions=*/false);
        if (!parsed.is_object()) {
            MARAS_LOG_ERROR("PrismaModalManager::ShowRawJsonModal: Invalid JSON specification");
            if (callback) {
                if (auto* taskIF = SKSE::GetTaskInterface()) {
                    taskIF->AddTask([cb = std::move(callback)]() {
                        try {
                            ModalResponse resp;
                            resp.buttonIndex = 1;
                            resp.aborted = true;
                            cb(resp);
                        } catch (const std::exception& e) {
                            MARAS_LOG_ERROR("PrismaModalManager: Exception in aborted modal callback: {}", e.what());
                        }
                    });
                }
            }
            return "";
        }

        std::string modalId;
        {
            std::lock_guard lock(m_mutex);
            modalId = std::format("maras_modal_{}", m_nextModalCounter++);
            parsed["modalId"] = modalId;
            if (!parsed.contains("modalType") || !parsed["modalType"].is_string()) {
                parsed["modalType"] = "generic";
            }
            if (!parsed.contains("buttons") || !parsed["buttons"].is_array() || parsed["buttons"].empty()) {
                parsed["buttons"] = nlohmann::json::array({
                    {{"id", "ok"}, {"label", "OK"}, {"variant", "default"}}
                });
            }

            PendingModal pending;
            pending.modalId = modalId;
            pending.defaultCancelButtonIndex = SafeGetJsonInt(parsed, "defaultCancelButtonIndex", 1);
            pending.serializedPayload = std::move(parsed);
            pending.callback = std::move(callback);
            pending.dispatchedToUI = false;

            m_queue.push_back(std::move(pending));
        }

        MARAS_LOG_INFO("PrismaModalManager::ShowRawJsonModal queued modalId='{}'", modalId);

        if (auto* taskIF = SKSE::GetTaskInterface()) {
            taskIF->AddTask([]() {
                try {
                    PrismaModalManager::GetSingleton().ProcessQueueOnGameThread();
                } catch (const std::exception& e) {
                    MARAS_LOG_ERROR("PrismaModalManager: Exception in ProcessQueueOnGameThread task: {}", e.what());
                }
            });
        }

        return modalId;
    }

    bool PrismaModalManager::HasActiveModal() const {
        std::lock_guard lock(m_mutex);
        return !m_queue.empty();
    }

    void PrismaModalManager::ProcessQueueOnGameThread() {
        auto& uiMgr = PrismaUIManager::GetSingleton();

        // If PrismaUI.dll is not available or view creation fails, abort pending modals safely
        if (!uiMgr.IsAvailable() || !uiMgr.EnsureViewCreated()) {
            MARAS_LOG_ERROR("PrismaModalManager::ProcessQueueOnGameThread: PrismaUI unavailable; aborting queued modals");
            std::deque<PendingModal> abortedModals;
            {
                std::lock_guard lock(m_mutex);
                abortedModals.swap(m_queue);
            }

            for (auto& pending : abortedModals) {
                if (pending.callback) {
                    try {
                        ModalResponse resp;
                        resp.modalId = pending.modalId;
                        resp.buttonIndex = pending.defaultCancelButtonIndex;
                        resp.aborted = true;
                        pending.callback(resp);
                    } catch (const std::exception& e) {
                        MARAS_LOG_ERROR("PrismaModalManager: Exception in aborted callback for '{}': {}",
                                        pending.modalId, e.what());
                    }
                }
            }
            return;
        }

        bool hasPending = false;
        {
            std::lock_guard lock(m_mutex);
            hasPending = !m_queue.empty();
        }

        if (!hasPending) {
            uiMgr.SyncViewVisibilityAndFocus();
            return;
        }

        if (uiMgr.IsModalDisplayBlocked()) {
            MARAS_LOG_INFO("PrismaModalManager::ProcessQueueOnGameThread: deferring modal display while blocking menu or character creation is active");
            uiMgr.SyncViewVisibilityAndFocus();
            return;
        }

        // If the DOM is ready, push the front modal to React immediately (which also syncs visibility and focus);
        // otherwise OnDomReady / OnUIReady will sync focus and send it once Ultralight finishes creating the view.
        if (uiMgr.IsDomReady()) {
            SendActiveModalToUI(/*forceResend=*/false);
        }
    }

    void PrismaModalManager::SendActiveModalToUI(bool forceResend) {
        auto& uiMgr = PrismaUIManager::GetSingleton();
        if (!uiMgr.IsViewValid() || !uiMgr.IsDomReady() || uiMgr.IsModalDisplayBlocked()) {
            return;
        }

        std::string payloadStr;
        std::string modalId;
        bool alreadyDispatched = false;
        {
            std::lock_guard lock(m_mutex);
            if (m_queue.empty()) {
                return;
            }
            auto& front = m_queue.front();
            alreadyDispatched = front.dispatchedToUI;
            front.dispatchedToUI = true;
            modalId = front.modalId;
            if (!alreadyDispatched || forceResend) {
                payloadStr = front.serializedPayload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            }
        }

        uiMgr.SyncViewVisibilityAndFocus();
        if (!alreadyDispatched || forceResend) {
            MARAS_LOG_INFO("PrismaModalManager::SendActiveModalToUI: dispatching modalId='{}' to PrismaUI", modalId);
            uiMgr.InteropCall("showMARASModal", payloadStr.c_str());
        }
    }

    void PrismaModalManager::HandleModalResponse(const nlohmann::json& payload) {
        if (!payload.is_object()) {
            MARAS_LOG_WARN("PrismaModalManager::HandleModalResponse: payload is not a JSON object");
            return;
        }

        const std::string modalId = SafeGetJsonString(payload, "modalId");
        const std::string buttonId = SafeGetJsonString(payload, "buttonId");
        const int32_t buttonIndex = SafeGetJsonInt(payload, "buttonIndex", 0);
        const nlohmann::json values = (payload.contains("values") && payload["values"].is_object())
            ? payload["values"]
            : nlohmann::json::object();

        ModalCallback callbackToInvoke;
        bool hasMoreModals = false;

        {
            std::lock_guard lock(m_mutex);
            if (m_queue.empty()) {
                MARAS_LOG_WARN("PrismaModalManager::HandleModalResponse: received response for '{}' but queue is empty",
                               modalId);
                return;
            }

            const auto& front = m_queue.front();
            if (!modalId.empty() && front.modalId != modalId) {
                MARAS_LOG_WARN("PrismaModalManager::HandleModalResponse: modalId mismatch (expected '{}', got '{}')",
                               front.modalId, modalId);
                return;
            }

            callbackToInvoke = std::move(m_queue.front().callback);
            m_queue.pop_front();
            hasMoreModals = !m_queue.empty();
        }

        MARAS_LOG_INFO("PrismaModalManager::HandleModalResponse: resolved modalId='{}' buttonId='{}' buttonIndex={} (remaining={})",
                       modalId, buttonId, buttonIndex, hasMoreModals ? 1 : 0);

        auto& uiMgr = PrismaUIManager::GetSingleton();
        if (hasMoreModals) {
            SendActiveModalToUI();
        } else {
            uiMgr.InteropCall("clearMARASModal", "");
            uiMgr.SyncViewVisibilityAndFocus();
        }

        if (callbackToInvoke) {
            try {
                ModalResponse response;
                response.modalId = modalId;
                response.buttonId = buttonId;
                response.buttonIndex = buttonIndex;
                response.aborted = false;
                response.values = values;
                callbackToInvoke(response);
            } catch (const std::exception& e) {
                MARAS_LOG_ERROR("PrismaModalManager::HandleModalResponse: exception in modal callback for '{}': {}",
                                modalId, e.what());
            }
        }
    }

    void PrismaModalManager::Revert() {
        std::deque<PendingModal> remaining;
        {
            std::lock_guard lock(m_mutex);
            remaining.swap(m_queue);
        }

        if (!remaining.empty()) {
            MARAS_LOG_INFO("PrismaModalManager::Revert: clearing {} pending modal(s)", remaining.size());
        }
    }

}  // namespace MARAS

