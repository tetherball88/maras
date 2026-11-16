#include "maras/bridge_ui/LocaleJsonBuilder.h"

#include "maras/engine/LocalizationService.h"

namespace MARAS::Bridge::Ui {

    nlohmann::json BuildLocalePayloadJson() {
        auto& svc = Engine::LocalizationService::GetSingleton();
        const auto snap = svc.Snapshot();
        const auto langs = svc.GetAvailableLanguages();

        nlohmann::json availableArr = nlohmann::json::array();
        for (const auto& meta : langs) {
            nlohmann::json item = nlohmann::json::object();
            item["code"] = meta.code;
            item["name"] = meta.name;
            item["nativeName"] = meta.nativeName;
            if (!meta.author.empty()) {
                item["author"] = meta.author;
            }
            if (!meta.marasVersion.empty()) {
                item["marasVersion"] = meta.marasVersion;
            }
            if (!meta.fontFamily.empty()) {
                item["fontFamily"] = meta.fontFamily;
            }
            availableArr.push_back(std::move(item));
        }

        nlohmann::json stringsObj = nlohmann::json::object();
        if (snap) {
            for (const auto& [k, v] : snap->UiEntries()) {
                stringsObj[k] = v;
            }
        }

        nlohmann::json payload = nlohmann::json::object();
        payload["selectedLanguage"] = snap ? snap->GetSelectedSetting() : "auto";
        payload["resolvedLanguage"] = snap ? snap->GetResolvedCode() : "en";
        payload["detectedSkyrimLanguage"] = Engine::LocalizationService::DetectSkyrimLanguageCode();
        payload["fontFamily"] = snap ? snap->GetMeta().fontFamily : "";
        payload["availableLanguages"] = std::move(availableArr);
        payload["strings"] = std::move(stringsObj);
        return payload;
    }

}  // namespace MARAS::Bridge::Ui
