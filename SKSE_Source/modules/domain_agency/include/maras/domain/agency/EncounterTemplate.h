#pragma once

#include "maras/foundation/DomainEnums.h"

#include <string>
#include <string_view>
#include <unordered_map>

namespace MARAS::Domain::Agency {

    using Foundation::Temperament;

    struct EncounterVariantFields {
        std::string salutation{};
        std::string premise{};
        std::string clue{};
        std::string signOff{};
    };

    struct EncounterTemplateSpec {
        int schemaVersion{ 1 };
        std::string id{};
        std::string modalTone{ "default" };
        std::string modalSize{ "md" };
        int defaultCancelButtonIndex{ 0 };
        bool allowLoverNameToken{ false };
        std::string modalTitle{};
        std::string modalSubtitle{};
        std::string envelopeMessage{};
        std::string infoBoxId{ "partner_note" };
        std::string infoBoxTitle{ "Parchment Contents" };
        std::string buttonId{ "close" };
        std::string buttonLabel{ "Put Note Away" };

        EncounterVariantFields defaults{};
        std::unordered_map<Temperament, EncounterVariantFields> variants{};
    };

    struct RenderedEncounterModal {
        std::string modalTone{};
        std::string modalSize{};
        int defaultCancelButtonIndex{ 0 };
        std::string modalTitle{};
        std::string modalSubtitle{};
        std::string envelopeMessage{};
        std::string infoBoxId{};
        std::string infoBoxTitle{};
        std::string letterBody{};
        std::string buttonId{};
        std::string buttonLabel{};
    };

    [[nodiscard]] RenderedEncounterModal RenderEncounterTemplate(
        const EncounterTemplateSpec& tmpl,
        Temperament temperament,
        std::string_view partnerName,
        std::string_view playerName,
        std::string_view locationName,
        std::string_view loverName = {});

    class EncounterTemplateCatalog {
    public:
        void SetTemplate(EncounterTemplateSpec spec);
        [[nodiscard]] const EncounterTemplateSpec* FindTemplate(std::string_view id) const noexcept;
        void Clear() noexcept;
        [[nodiscard]] std::size_t Size() const noexcept { return templates_.size(); }

    private:
        std::unordered_map<std::string, EncounterTemplateSpec> templates_{};
    };

}  // namespace MARAS::Domain::Agency
