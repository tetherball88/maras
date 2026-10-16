#include "ui/ConfirmationModal.h"

#include "core/SkyrimNetIntegration.h"
#include "services/ActorService.h"
#include "utils/Settings.h"

namespace MARAS {

class ConfirmationModal::ModalCallback : public RE::IMessageBoxCallback {
public:
    ModalCallback(RE::BSScript::Internal::VirtualMachine* vm,
                  RE::VMStackID stackID,
                  ConfirmationModal* owner,
                  std::vector<std::string> keys,
                  int normalSec,
                  int declineSec,
                  ModalAction action,
                  std::string actionTypeStr,
                  RE::FormID originatorID,
                  RE::FormID targetID,
                  std::string mainActorList,
                  std::string secondaryActorList,
                  std::string activity,
                  bool skyrimNetAvailable)
        : _vm(vm),
          _stackID(stackID),
          _owner(owner),
          _keys(std::move(keys)),
          _normalSec(normalSec),
          _declineSec(declineSec),
          _action(action),
          _actionTypeStr(std::move(actionTypeStr)),
          _originatorID(originatorID),
          _targetID(targetID),
          _mainActorList(std::move(mainActorList)),
          _secondaryActorList(std::move(secondaryActorList)),
          _activity(std::move(activity)),
          _skyrimNetAvailable(skyrimNetAvailable) {}

    void Run(std::uint8_t a_msg) override {
        int result = static_cast<int>(a_msg);
        MARAS_LOG_INFO("ConfirmationModal::ModalCallback::Run - result={}", result);

        if (result == 0) {
            _owner->SetCooldownAll(_keys, _normalSec);
        } else if (result == 1) {
            _owner->SetCooldownAll(_keys, _declineSec);
        } else if (result == 2 && _skyrimNetAvailable) {
            _owner->SetCooldownAll(_keys, _declineSec);
            FireTriggerTextInput();
            FireDeclineEvent();
        }

        _vm->ReturnLatentResult(_stackID, result);
    }

private:
    RE::BSScript::Internal::VirtualMachine* _vm;
    RE::VMStackID _stackID;
    ConfirmationModal* _owner;
    std::vector<std::string> _keys;
    int _normalSec;
    int _declineSec;
    ModalAction _action;
    std::string _actionTypeStr;
    RE::FormID _originatorID;
    RE::FormID _targetID;
    std::string _mainActorList;
    std::string _secondaryActorList;
    std::string _activity;
    bool _skyrimNetAvailable;

    static std::string BuildDeclineMsg(ModalAction action,
                                       const std::string& actionTypeRaw,
                                       const std::string& mainActorList,
                                       const std::string& secondaryActorList,
                                       const std::string& activity) {
        auto* playerActor = RE::PlayerCharacter::GetSingleton();
        std::string playerName =
            (playerActor && playerActor->GetDisplayFullName() && playerActor->GetDisplayFullName()[0] != '\0')
                ? playerActor->GetDisplayFullName()
                : "the player";

        switch (action) {
            case ModalAction::Dating:
                return mainActorList + " attempted to start dating with " + secondaryActorList + ", " + playerName +
                       " did not go through with it.";
            case ModalAction::Engaged:
                return mainActorList + " attempted an engagement with " + secondaryActorList + ", " + playerName +
                       " did not go through with it.";
            case ModalAction::Lover:
                return mainActorList + " attempted to become lovers with " + secondaryActorList + ", " + playerName +
                       " did not go through with it.";
            case ModalAction::Married:
                return mainActorList + " attempted to marry " + secondaryActorList + ", " + playerName +
                       " did not go through with it.";
            case ModalAction::ExDating:
                return mainActorList + " attempted to break up dating with " + secondaryActorList + ", " + playerName +
                       " did not go through with it.";
            case ModalAction::Jilted:
                return mainActorList + " attempted to break off the engagement with " + secondaryActorList + ", " +
                       playerName + " did not go through with it.";
            case ModalAction::ExLover:
                return mainActorList + " attempted to end the lover relationship with " + secondaryActorList + ", " +
                       playerName + " did not go through with it.";
            case ModalAction::Divorced:
                return mainActorList + " attempted to divorce " + secondaryActorList + ", " + playerName +
                       " did not go through with it.";
            case ModalAction::OpenRelationship:
                return mainActorList + " attempted to open the relationship with " + secondaryActorList + ", " +
                       playerName + " did not go through with it.";
            case ModalAction::CloseRelationship:
                return mainActorList + " attempted to close the relationship with " + secondaryActorList + ", " +
                       playerName + " did not go through with it.";
            default:
                break;
        }
        if (!activity.empty()) {
            return mainActorList + " attempted " + activity + " with " + secondaryActorList + ", " + playerName +
                   " did not go through with it.";
        }
        return mainActorList + " attempted " + (!actionTypeRaw.empty() ? actionTypeRaw : "an interaction") + " with " +
               secondaryActorList + ", " + playerName + " did not go through with it.";
    }

    static std::string BuildDeclineJson(ModalAction action,
                                        const std::string& actionTypeRaw,
                                        const std::string& mainActorList,
                                        const std::string& secondaryActorList,
                                        const std::string& activity,
                                        RE::FormID originatorID,
                                        RE::FormID targetID,
                                        bool skipTrigger = true) {
        std::string msg = BuildDeclineMsg(action, actionTypeRaw, mainActorList, secondaryActorList, activity);
        auto* playerActor = RE::PlayerCharacter::GetSingleton();
        std::string playerName =
            (playerActor && playerActor->GetDisplayFullName() && playerActor->GetDisplayFullName()[0] != '\0')
                ? playerActor->GetDisplayFullName()
                : "the player";

        std::string actionName(ModalActionToString(action));
        if (actionName.empty()) actionName = actionTypeRaw;

        nlohmann::json j;
        j["type"] = "ttm_action_decline";
        j["declinedAction"] = actionName;
        j["msg"] = msg;
        j["skipTrigger"] = skipTrigger;
        j["mainActorNames"] = mainActorList;
        j["secondaryActorNames"] = secondaryActorList;
        if (originatorID != 0) j["initiator"] = originatorID;
        if (targetID != 0) j["target"] = targetID;
        j["speaker"] = playerName;
        return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }

    void FireDeclineEvent() const {
        auto* player = RE::PlayerCharacter::GetSingleton();
        std::string payload = BuildDeclineJson(_action, _actionTypeStr, _mainActorList, _secondaryActorList, _activity,
                                               _originatorID, _targetID, true);

        MARAS_LOG_INFO("ConfirmationModal::FireDeclineEvent - firing maras_decline: {}", payload);

        if (auto* modEventSource = SKSE::GetModCallbackEventSource()) {
            SKSE::ModCallbackEvent event{
                "maras_decline",
                payload,
                0.0f,
                player
            };
            modEventSource->SendEvent(&event);
        }
    }

    static void FireTriggerTextInput() {
        if (auto* taskIF = SKSE::GetTaskInterface()) {
            taskIF->AddTask([]() {
                auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
                if (!vm) return;
                RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> nullCallback;
                vm->DispatchStaticCall("SkyrimNetApi", "TriggerTextInput", RE::MakeFunctionArguments(), nullCallback);
            });
        }
    }
};

ConfirmationModal& ConfirmationModal::GetSingleton() {
    static ConfirmationModal instance;
    return instance;
}

ConfirmationModal::ModalAction ConfirmationModal::StringToModalAction(std::string_view actionStr) {
    std::string lower(actionStr);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower == "dating") return ModalAction::Dating;
    if (lower == "engaged") return ModalAction::Engaged;
    if (lower == "lover") return ModalAction::Lover;
    if (lower == "married") return ModalAction::Married;
    if (lower == "exdating") return ModalAction::ExDating;
    if (lower == "jilted") return ModalAction::Jilted;
    if (lower == "exlover") return ModalAction::ExLover;
    if (lower == "divorced") return ModalAction::Divorced;
    if (lower == "open_relationship") return ModalAction::OpenRelationship;
    if (lower == "close_relationship") return ModalAction::CloseRelationship;
    return ModalAction::Custom;
}

std::string_view ConfirmationModal::ModalActionToString(ModalAction action) {
    switch (action) {
        case ModalAction::Dating: return "dating";
        case ModalAction::Engaged: return "engaged";
        case ModalAction::Lover: return "lover";
        case ModalAction::Married: return "married";
        case ModalAction::ExDating: return "exdating";
        case ModalAction::Jilted: return "jilted";
        case ModalAction::ExLover: return "exlover";
        case ModalAction::Divorced: return "divorced";
        case ModalAction::OpenRelationship: return "open_relationship";
        case ModalAction::CloseRelationship: return "close_relationship";
        default: return "";
    }
}

bool ConfirmationModal::IsOnCooldown(const char* actionType, RE::Actor* originator) {
    if (IsPlayer(originator)) return false;
    std::string key = BuildCooldownKey(actionType, originator ? originator->GetFormID() : 0);
    const auto now = std::chrono::steady_clock::now();
    std::shared_lock lock(_cooldownMutex);
    auto it = _cooldowns.find(key);
    bool onCooldown = it != _cooldowns.end() && now < it->second;
    if (onCooldown) {
        auto remaining = std::chrono::duration_cast<std::chrono::seconds>(it->second - now).count();
        MARAS_LOG_DEBUG("ConfirmationModal::IsOnCooldown - key='{}' ON COOLDOWN ({}s remaining)", key, remaining);
    } else {
        MARAS_LOG_DEBUG("ConfirmationModal::IsOnCooldown - key='{}' not on cooldown", key);
    }
    return onCooldown;
}

int32_t ConfirmationModal::CheckAndSet(const char* actionType, const std::vector<RE::Actor*>& actors) {
    const int normalSec = Settings::GetSingleton().actionsCooldown;
    std::vector<std::string> keys;
    keys.reserve(actors.size());
    for (RE::Actor* actor : actors) {
        if (actor && !IsPlayer(actor)) {
            std::string key = BuildCooldownKey(actionType, actor->GetFormID());
            if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
                keys.push_back(std::move(key));
            }
        }
    }
    if (keys.empty()) return 0;
    const auto now = std::chrono::steady_clock::now();
    auto expiry = now + std::chrono::seconds(normalSec);
    std::unique_lock lock(_cooldownMutex);
    for (const auto& key : keys) {
        auto it = _cooldowns.find(key);
        if (it != _cooldowns.end() && now < it->second) {
            auto remaining = std::chrono::duration_cast<std::chrono::seconds>(it->second - now).count();
            MARAS_LOG_INFO("ConfirmationModal::CheckAndSet - key='{}' blocked by cooldown ({}s remaining)", key, remaining);
            return 1;
        }
    }
    for (const auto& key : keys) {
        _cooldowns[key] = expiry;
        MARAS_LOG_INFO("ConfirmationModal::CheckAndSet - key='{}' cooldown set ({}s)", key, normalSec);
    }
    return 0;
}

RE::BSScript::LatentStatus ConfirmationModal::Show(RE::BSScript::Internal::VirtualMachine* vm,
                                                   RE::VMStackID stackID,
                                                   const char* actionType,
                                                   const std::vector<RE::Actor*>& mainActors,
                                                   const std::vector<RE::Actor*>& secondaryActors,
                                                   const char* paramsJson) {
    std::string mainActorList      = mainActors.empty()      ? "Someone" : FormatActorNames(mainActors);
    std::string secondaryActorList = secondaryActors.empty() ? "Someone" : FormatActorNames(secondaryActors);
    std::string actionTypeStr      = actionType ? actionType : "";
    ModalAction action             = StringToModalAction(actionTypeStr);

    MARAS_LOG_INFO("ConfirmationModal::Show - actionType='{}' (actionEnum={}) mainActors={} secondaryActors={} paramsJson='{}'",
                   actionTypeStr, static_cast<int>(action), mainActorList, secondaryActorList, paramsJson ? paramsJson : "");

    const int normalSec  = Settings::GetSingleton().actionsCooldown;
    const int declineSec = Settings::GetSingleton().actionsDeclineCooldown;

    std::vector<std::string> keys;
    keys.reserve(mainActors.size() + secondaryActors.size());
    auto appendKeys = [&](const std::vector<RE::Actor*>& actors) {
        for (RE::Actor* actor : actors) {
            if (!actor || IsPlayer(actor)) continue;
            std::string key = BuildCooldownKey(actionTypeStr.c_str(), actor->GetFormID());
            if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
                keys.push_back(std::move(key));
            }
        }
    };
    appendKeys(mainActors);
    appendKeys(secondaryActors);

    auto ReturnNow = [vm, stackID](int code) -> RE::BSScript::LatentStatus {
        MARAS_LOG_INFO("ConfirmationModal::Show - early return {}", code);
        if (auto* taskIF = SKSE::GetTaskInterface()) {
            taskIF->AddTask([vm, stackID, code]() {
                vm->ReturnLatentResult(stackID, code);
            });
            return RE::BSScript::LatentStatus::kStarted;
        }
        return RE::BSScript::LatentStatus::kFailed;
    };

    // Active cooldown check
    if (!keys.empty()) {
        const auto now = std::chrono::steady_clock::now();
        std::shared_lock lock(_cooldownMutex);
        for (const auto& key : keys) {
            auto it = _cooldowns.find(key);
            if (it != _cooldowns.end() && now < it->second) {
                auto remaining = std::chrono::duration_cast<std::chrono::seconds>(it->second - now).count();
                MARAS_LOG_INFO("ConfirmationModal::Show - key='{}' blocked by active cooldown ({}s remaining)", key, remaining);
                return ReturnNow(3);
            }
        }
    }

    // Master switch
    if (!Settings::GetSingleton().enableConfirmationModal) {
        MARAS_LOG_INFO("ConfirmationModal::Show - auto-accepting (master confirmation modal setting enableConfirmationModal is false)");
        SetCooldownAll(keys, normalSec);
        return ReturnNow(5);
    }

    // Configuration check: Per-action switch
    if (!IsActionTypeEnabled(action)) {
        MARAS_LOG_INFO("ConfirmationModal::Show - auto-accepting (actionType='{}' confirmation setting is false)", actionTypeStr);
        SetCooldownAll(keys, normalSec);
        return ReturnNow(5);
    }

    // Player involvement check: Participant-type switches
    bool hasPlayer = false;
    for (RE::Actor* actor : mainActors) {
        if (IsPlayer(actor)) {
            hasPlayer = true;
            break;
        }
    }
    if (!hasPlayer) {
        for (RE::Actor* actor : secondaryActors) {
            if (IsPlayer(actor)) {
                hasPlayer = true;
                break;
            }
        }
    }

    if (hasPlayer && !Settings::GetSingleton().enableConfirmPlayerNpc) {
        MARAS_LOG_INFO("ConfirmationModal::Show - auto-accepting (Player-NPC modals disabled: enableConfirmPlayerNpc=false)");
        SetCooldownAll(keys, normalSec);
        return ReturnNow(4);
    }

    if (!hasPlayer && !Settings::GetSingleton().enableConfirmNpcNpc) {
        MARAS_LOG_INFO("ConfirmationModal::Show - auto-accepting (NPC-NPC modals disabled: enableConfirmNpcNpc=false)");
        SetCooldownAll(keys, normalSec);
        return ReturnNow(4);
    }

    RE::FormID originatorID = mainActors.empty() || !mainActors[0] ? 0 : mainActors[0]->GetFormID();
    RE::FormID targetID     = secondaryActors.empty() || !secondaryActors[0] ? 0 : secondaryActors[0]->GetFormID();

    std::string activity;
    std::string details;
    if (paramsJson && paramsJson[0] != '\0') {
        auto j = nlohmann::json::parse(paramsJson, nullptr, /*exceptions=*/false);
        if (j.is_object()) {
            if (j.contains("activity") && j["activity"].is_string()) {
                activity = j["activity"].get<std::string>();
            }
            if (j.contains("details") && j["details"].is_string()) {
                details = j["details"].get<std::string>();
            }
        }
    }

    std::string message = BuildMessage(action, actionTypeStr, mainActorList, secondaryActorList, activity, details);

    MARAS_LOG_INFO("ConfirmationModal::Show - queuing modal message: '{}'", message);

    if (auto* taskIF = SKSE::GetTaskInterface()) {
        taskIF->AddTask([this, vm, stackID, message, keys, normalSec, declineSec,
                         action, actionTypeStr, originatorID, targetID, mainActorList, secondaryActorList, activity]() {
            MARAS_LOG_INFO("ConfirmationModal - game thread: creating MessageBoxData");
            auto* factory = RE::MessageDataFactoryManager::GetSingleton();
            auto* ifStr   = RE::InterfaceStrings::GetSingleton();
            auto* creator = (factory && ifStr) ? factory->GetCreator<RE::MessageBoxData>(ifStr->messageBoxData) : nullptr;
            if (!creator) {
                MARAS_LOG_ERROR("ConfirmationModal - null creator");
                vm->ReturnLatentResult(stackID, 1);
                return;
            }
            auto* msgData = creator->Create();
            if (!msgData) {
                MARAS_LOG_ERROR("ConfirmationModal - null msgData");
                vm->ReturnLatentResult(stackID, 1);
                return;
            }

            msgData->bodyText = message.c_str();
            msgData->buttonText.push_back(RE::BSString("Accept"));
            msgData->buttonText.push_back(RE::BSString("Cancel"));

            // Conditionally add "Decline (Explain)" button ONLY if SkyrimNet is loaded
            bool skyrimNetAvailable = MARAS::IsSkyrimNetAvailable();
            if (skyrimNetAvailable) {
                msgData->buttonText.push_back(RE::BSString("Decline (Explain)"));
            }

            msgData->cancelButtonIndex = 1;
            msgData->callback = RE::make_smart<ModalCallback>(
                vm, stackID, this, keys, normalSec, declineSec,
                action, actionTypeStr, originatorID, targetID, mainActorList, secondaryActorList, activity, skyrimNetAvailable);

            MARAS_LOG_INFO("ConfirmationModal - queuing UI message (SkyrimNet available: {})", skyrimNetAvailable);
            RE::MessageBoxMenu::QueueMessage(msgData);
        });
    } else {
        MARAS_LOG_ERROR("ConfirmationModal::Show - SKSE task interface unavailable");
        return RE::BSScript::LatentStatus::kFailed;
    }

    return RE::BSScript::LatentStatus::kStarted;
}

void ConfirmationModal::SetCooldown(const std::string& key, int seconds) {
    auto expiry = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    std::unique_lock lock(_cooldownMutex);
    _cooldowns[key] = expiry;
    MARAS_LOG_INFO("ConfirmationModal::SetCooldown - key='{}' set ({}s)", key, seconds);
}

void ConfirmationModal::SetCooldownAll(const std::vector<std::string>& keys, int seconds) {
    auto expiry = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    std::unique_lock lock(_cooldownMutex);
    for (const auto& key : keys) {
        _cooldowns[key] = expiry;
        MARAS_LOG_INFO("ConfirmationModal::SetCooldownAll - key='{}' set ({}s)", key, seconds);
    }
}

void ConfirmationModal::Revert() {
    std::unique_lock lock(_cooldownMutex);
    _cooldowns.clear();
}

bool ConfirmationModal::IsPlayer(RE::Actor* actor) {
    if (!actor) return false;
    auto* player = RE::PlayerCharacter::GetSingleton();
    return player && actor->GetFormID() == player->GetFormID();
}

std::string ConfirmationModal::BuildCooldownKey(const char* actionType, RE::FormID formID) {
    std::string lower = actionType ? actionType : "";
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::ostringstream oss;
    oss << lower << ":" << std::hex << formID;
    return oss.str();
}

std::string ConfirmationModal::FormatActorNames(const std::vector<RE::Actor*>& actors) {
    std::string result;
    for (size_t i = 0; i < actors.size(); ++i) {
        if (!actors[i]) continue;
        std::string name = ActorSvc::GetName(actors[i]->GetFormID());
        if (name.empty()) name = "Someone";
        if (!result.empty()) {
            if (i == actors.size() - 1) result += " and ";
            else result += ", ";
        }
        result += name;
    }
    return result.empty() ? "Someone" : result;
}

bool ConfirmationModal::IsActionTypeEnabled(ModalAction action) {
    const auto& s = Settings::GetSingleton();
    switch (action) {
        case ModalAction::Dating: return s.confirmStatusChange_Dating;
        case ModalAction::Engaged: return s.confirmStatusChange_Engagement;
        case ModalAction::Lover: return s.confirmStatusChange_Lover;
        case ModalAction::Married: return s.confirmStatusChange_Marry;
        case ModalAction::ExDating: return s.confirmBreakup_Dating;
        case ModalAction::Jilted: return s.confirmBreakup_Engagement;
        case ModalAction::ExLover: return s.confirmBreakup_Lover;
        case ModalAction::Divorced: return s.confirmDivorce;
        case ModalAction::OpenRelationship: return s.confirmOpenRelationship;
        case ModalAction::CloseRelationship: return s.confirmCloseRelationship;
        default: return true;
    }
}

std::string ConfirmationModal::BuildMessage(ModalAction action,
                                            const std::string& actionTypeRaw,
                                            const std::string& mainActorList,
                                            const std::string& secondaryActorList,
                                            const std::string& activity,
                                            const std::string& details) {
    switch (action) {
        case ModalAction::Married:
            return mainActorList + " wants to marry " + secondaryActorList + ".\nDo you accept the marriage proposal?";
        case ModalAction::Divorced:
            return mainActorList + " wants to end the relationship with " + secondaryActorList +
                   ".\nDo you want to proceed with the divorce?";
        case ModalAction::Dating:
            return mainActorList + " wants to start dating " + secondaryActorList + ".\nDo you allow this?";
        case ModalAction::Engaged:
            return mainActorList + " wants to get engaged to " + secondaryActorList + ".\nDo you allow this?";
        case ModalAction::Lover:
            return mainActorList + " wants to become lovers with " + secondaryActorList + ".\nDo you allow this?";
        case ModalAction::ExDating:
            return mainActorList + " wants to break up dating with " + secondaryActorList + ".\nDo you allow this?";
        case ModalAction::Jilted:
            return mainActorList + " wants to break off the engagement with " + secondaryActorList +
                   ".\nDo you allow this?";
        case ModalAction::ExLover:
            return mainActorList + " wants to end the lover relationship with " + secondaryActorList +
                   ".\nDo you allow this?";
        case ModalAction::OpenRelationship:
            return mainActorList + " wants to open the relationship with " + secondaryActorList + ".\nDo you allow this?";
        case ModalAction::CloseRelationship:
            return mainActorList + " wants to close the open relationship with " + secondaryActorList +
                   ".\nDo you allow this?";
        default:
            break;
    }

    // Generic fallback for any present or future custom action type
    std::string msg = mainActorList + " wants to start ";
    msg += !actionTypeRaw.empty() ? actionTypeRaw : "an interaction";
    msg += " with " + secondaryActorList + ".";
    if (!activity.empty()) {
        msg += "\nActivity: " + activity;
    }
    if (!details.empty()) {
        msg += "\nDetails: " + details;
    }
    msg += "\nDo you accept?";
    return msg;
}

}  // namespace MARAS

