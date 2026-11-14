#include "maras/domain/agency/EncounterTemplate.h"

#include "maras/foundation/StringUtils.h"

namespace MARAS::Domain::Agency {

    namespace {
        void ReplaceAll(std::string& text, std::string_view token, std::string_view replacement) {
            if (token.empty()) {
                return;
            }
            std::size_t pos = 0;
            while ((pos = text.find(token, pos)) != std::string::npos) {
                text.replace(pos, token.size(), replacement);
                pos += replacement.size();
            }
        }

        [[nodiscard]] std::string SubstituteTokens(
            std::string_view input,
            std::string_view partnerName,
            std::string_view playerName,
            std::string_view locationName,
            std::string_view loverName) {
            std::string out(input);
            ReplaceAll(out, "{partner}", partnerName.empty() ? "your partner" : partnerName);
            ReplaceAll(out, "{player}", playerName.empty() ? "Friend" : playerName);
            ReplaceAll(out, "{location}", locationName.empty() ? "nearby" : locationName);
            ReplaceAll(out, "{lover}", loverName.empty() ? "someone" : loverName);
            return out;
        }
    }  // namespace

    RenderedEncounterModal RenderEncounterTemplate(
        const EncounterTemplateSpec& tmpl,
        Temperament temperament,
        std::string_view partnerName,
        std::string_view playerName,
        std::string_view locationName,
        std::string_view loverName) {
        EncounterVariantFields merged = tmpl.defaults;
        if (const auto it = tmpl.variants.find(temperament); it != tmpl.variants.end()) {
            if (!it->second.salutation.empty()) merged.salutation = it->second.salutation;
            if (!it->second.premise.empty()) merged.premise = it->second.premise;
            if (!it->second.clue.empty()) merged.clue = it->second.clue;
            if (!it->second.signOff.empty()) merged.signOff = it->second.signOff;
        }

        const std::string_view effectiveLover =
            tmpl.allowLoverNameToken ? loverName : std::string_view("someone");

        std::string body;
        auto appendSection = [&](std::string_view raw) {
            if (raw.empty()) {
                return;
            }
            if (!body.empty()) {
                body += "\n\n";
            }
            body += SubstituteTokens(raw, partnerName, playerName, locationName, effectiveLover);
        };
        appendSection(merged.salutation);
        appendSection(merged.premise);
        appendSection(merged.clue);
        appendSection(merged.signOff);

        RenderedEncounterModal rendered{};
        rendered.modalTone = tmpl.modalTone;
        rendered.modalSize = tmpl.modalSize;
        rendered.defaultCancelButtonIndex = tmpl.defaultCancelButtonIndex;
        rendered.modalTitle = SubstituteTokens(tmpl.modalTitle, partnerName, playerName, locationName, effectiveLover);
        rendered.modalSubtitle = SubstituteTokens(tmpl.modalSubtitle, partnerName, playerName, locationName, effectiveLover);
        rendered.envelopeMessage = SubstituteTokens(tmpl.envelopeMessage, partnerName, playerName, locationName, effectiveLover);
        rendered.infoBoxId = tmpl.infoBoxId;
        rendered.infoBoxTitle = SubstituteTokens(tmpl.infoBoxTitle, partnerName, playerName, locationName, effectiveLover);
        rendered.letterBody = std::move(body);
        rendered.buttonId = tmpl.buttonId;
        rendered.buttonLabel = tmpl.buttonLabel;
        return rendered;
    }

    void EncounterTemplateCatalog::SetTemplate(EncounterTemplateSpec spec) {
        if (spec.id.empty()) {
            return;
        }
        const std::string key = Foundation::StringUtils::ToLowerStd(spec.id);
        templates_[key] = std::move(spec);
    }

    const EncounterTemplateSpec* EncounterTemplateCatalog::FindTemplate(std::string_view id) const noexcept {
        if (id.empty()) {
            return nullptr;
        }
        const auto it = templates_.find(Foundation::StringUtils::ToLowerStd(id));
        return (it != templates_.end()) ? &it->second : nullptr;
    }

    void EncounterTemplateCatalog::Clear() noexcept {
        templates_.clear();
    }

}  // namespace MARAS::Domain::Agency
