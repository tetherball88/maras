#include "TestHarness.h"

#include "maras/domain/lineage/NamePool.h"
#include "maras/foundation/LocalizationCatalog.h"

namespace {

    using MARAS::Foundation::ExtractPlaceholders;
    using MARAS::Foundation::FormatTemplate;
    using MARAS::Foundation::LocalizationCatalog;
    using MARAS::Foundation::LocalizationMeta;
    using MARAS::Foundation::MapSkyrimIniLanguageToCode;
    using MARAS::Foundation::PluralCategory;
    using MARAS::Foundation::SelectPluralCategory;
    using MARAS::Foundation::StringDictionary;
    using MARAS::Foundation::ValidateOverlayAgainstBase;

    MARAS_TEST(Localization, SkyrimIniLanguageMapping) {
        MARAS_CHECK_EQ(MapSkyrimIniLanguageToCode("ENGLISH"), "en");
        MARAS_CHECK_EQ(MapSkyrimIniLanguageToCode("  russian "), "ru");
        MARAS_CHECK_EQ(MapSkyrimIniLanguageToCode("GERMAN"), "de");
        MARAS_CHECK_EQ(MapSkyrimIniLanguageToCode("FRENCH"), "fr");
        MARAS_CHECK_EQ(MapSkyrimIniLanguageToCode("SPANISH"), "es");
        MARAS_CHECK_EQ(MapSkyrimIniLanguageToCode("POLISH"), "pl");
        MARAS_CHECK_EQ(MapSkyrimIniLanguageToCode("JAPANESE"), "ja");
        MARAS_CHECK_EQ(MapSkyrimIniLanguageToCode("CHINESE"), "zh-CN");
        MARAS_CHECK_EQ(MapSkyrimIniLanguageToCode("TRADITIONAL CHINESE"), "zh-TW");
        MARAS_CHECK_EQ(MapSkyrimIniLanguageToCode("KOREAN"), "ko");
        MARAS_CHECK_EQ(MapSkyrimIniLanguageToCode(""), "en");
        MARAS_CHECK_EQ(MapSkyrimIniLanguageToCode("KLINGON"), "en");
    }

    MARAS_TEST(Localization, TemplateFormattingReorderingAndMalformedSafety) {
        // Reordered named arguments (e.g. SOV vs SVO word order)
        const std::string sv = FormatTemplate(
            "{spouse} shared their home - {home} - with you.",
            { { "spouse", "Ysolda" }, { "home", "Whiterun House" } });
        MARAS_CHECK_EQ(sv, "Ysolda shared their home - Whiterun House - with you.");

        const std::string reordered = FormatTemplate(
            "Дом «{home}» теперь делит с вами {spouse}.",
            { { "spouse", "Изольда" }, { "home", "Дом Теплых Ветров" } });
        MARAS_CHECK_EQ(reordered, "Дом «Дом Теплых Ветров» теперь делит с вами Изольда.");

        // Escaped double braces + unknown or malformed translator braces never throw
        const std::string safe = FormatTemplate(
            "Literal {{brace}} and {unknown_token} and {unclosed and {bad space} plus {ok}!",
            { { "ok", "fine" } });
        MARAS_CHECK_EQ(safe, "Literal {brace} and {unknown_token} and {unclosed and {bad space} plus fine!");
    }

    MARAS_TEST(Localization, CldrPluralSelectionAndCatalogPluralFormatting) {
        // English
        MARAS_CHECK_EQ(SelectPluralCategory("en", 0), PluralCategory::Other);
        MARAS_CHECK_EQ(SelectPluralCategory("en", 1), PluralCategory::One);
        MARAS_CHECK_EQ(SelectPluralCategory("en", 5), PluralCategory::Other);

        // French (0 and 1 are One)
        MARAS_CHECK_EQ(SelectPluralCategory("fr", 0), PluralCategory::One);
        MARAS_CHECK_EQ(SelectPluralCategory("fr", 1), PluralCategory::One);
        MARAS_CHECK_EQ(SelectPluralCategory("fr", 2), PluralCategory::Other);

        // Russian (1, 21 -> One; 2..4, 22..24 -> Few; 0, 5..20, 25 -> Many)
        MARAS_CHECK_EQ(SelectPluralCategory("ru", 1), PluralCategory::One);
        MARAS_CHECK_EQ(SelectPluralCategory("ru", 21), PluralCategory::One);
        MARAS_CHECK_EQ(SelectPluralCategory("ru", 11), PluralCategory::Many);
        MARAS_CHECK_EQ(SelectPluralCategory("ru", 2), PluralCategory::Few);
        MARAS_CHECK_EQ(SelectPluralCategory("ru", 24), PluralCategory::Few);
        MARAS_CHECK_EQ(SelectPluralCategory("ru", 14), PluralCategory::Many);
        MARAS_CHECK_EQ(SelectPluralCategory("ru", 5), PluralCategory::Many);

        // Polish (1 -> One; 2..4, 22 -> Few; 21, 5, 12 -> Many)
        MARAS_CHECK_EQ(SelectPluralCategory("pl", 1), PluralCategory::One);
        MARAS_CHECK_EQ(SelectPluralCategory("pl", 21), PluralCategory::Many);
        MARAS_CHECK_EQ(SelectPluralCategory("pl", 3), PluralCategory::Few);
        MARAS_CHECK_EQ(SelectPluralCategory("pl", 12), PluralCategory::Many);

        // Japanese (always Other)
        MARAS_CHECK_EQ(SelectPluralCategory("ja", 1), PluralCategory::Other);
        MARAS_CHECK_EQ(SelectPluralCategory("ja", 10), PluralCategory::Other);

        StringDictionary base{
            { "hud.married", "{actor} and I are now newlyweds." },
            { "hud.untranslated", "Fallback English line." },
            { "ui.nav.overview", "Overview" },
            { "ui.dates.days_ago_one", "1 day ago" },
            { "ui.dates.days_ago_other", "{count} days ago" }
        };
        StringDictionary ruOverlay{
            { "hud.married", "{actor} и я теперь молодожены." },
            { "ui.nav.overview", "Обзор" },
            { "ui.dates.days_ago_one", "{count} день назад" },
            { "ui.dates.days_ago_few", "{count} дня назад" },
            { "ui.dates.days_ago_many", "{count} дней назад" },
            { "ui.dates.days_ago_other", "{count} дней назад" }
        };

        LocalizationMeta ruMeta{ .code = "ru", .name = "Russian", .nativeName = "Русский" };
        LocalizationCatalog cat("ru", ruMeta, base, ruOverlay);

        MARAS_CHECK_EQ(cat.Get("hud.untranslated"), "Fallback English line.");
        MARAS_CHECK_EQ(cat.GetOr("bonuses.custom.name", "Custom Bonus"), "Custom Bonus");
        MARAS_CHECK_EQ(cat.Format("hud.married", { { "actor", "Изольда" } }), "Изольда и я теперь молодожены.");
        MARAS_CHECK_EQ(cat.FormatPlural("ui.dates.days_ago", 1), "1 день назад");
        MARAS_CHECK_EQ(cat.FormatPlural("ui.dates.days_ago", 3), "3 дня назад");
        MARAS_CHECK_EQ(cat.FormatPlural("ui.dates.days_ago", 14), "14 дней назад");

        // UI sub-dictionary strips "ui." prefix cleanly for frontend payload
        MARAS_REQUIRE(cat.UiEntries().contains("nav.overview"));
        MARAS_CHECK_EQ(cat.UiEntries().find("nav.overview")->second, "Обзор");
        MARAS_CHECK(!cat.UiEntries().contains("hud.married"));
    }

    MARAS_TEST(Localization, OverlayPlaceholderValidationAndSlavicPluralSiblings) {
        StringDictionary base{
            { "hud.shared_home", "{spouse} shared {home} with you." },
            { "hud.rank_up", "Promoted {spouse}." },
            { "ui.dates.days_ago_one", "1 day ago" },
            { "ui.dates.days_ago_other", "{count} days ago" }
        };
        StringDictionary overlay{
            { "hud.shared_home", "{spoues} поделился {home}." },  // typo: {spoues} instead of {spouse}
            { "ui.dates.days_ago_one", "Вчера" },                 // valid singular omitting {count}
            { "ui.dates.days_ago_few", "{count} дня назад" },     // valid Slavic plural sibling!
            { "ui.orphan_key", "Unknown key" }
        };

        const auto report = ValidateOverlayAgainstBase(base, overlay);
        MARAS_CHECK_EQ(report.baseKeyCount, 4u);
        MARAS_CHECK_EQ(report.translatedBaseKeyCount, 2u);
        MARAS_CHECK_EQ(report.fallbackKeyCount, 2u);
        MARAS_CHECK_EQ(report.unknownOverlayKeyCount, 1u);
        MARAS_REQUIRE_EQ(report.placeholderMismatches.size(), 1u);
        MARAS_CHECK_EQ(report.placeholderMismatches[0].key, "hud.shared_home");
        MARAS_REQUIRE_EQ(report.placeholderMismatches[0].missingPlaceholders.size(), 1u);
        MARAS_CHECK_EQ(report.placeholderMismatches[0].missingPlaceholders[0], "spouse");
        MARAS_REQUIRE_EQ(report.placeholderMismatches[0].extraPlaceholders.size(), 1u);
        MARAS_CHECK_EQ(report.placeholderMismatches[0].extraPlaceholders[0], "spoues");
    }

    MARAS_TEST(Localization, NamePoolPartialLanguageOverlayPreservesBaseRaces) {
        MARAS::Domain::Lineage::NamePool pool;
        pool.SetChildNames(
            { { "nord", { "Thorvik" } }, { "breton", { "Aubrin" } } },
            { { "nord", { "Alfrun" } }, { "breton", { "Colette" } } });
        pool.SetCritterNames({ { "fox", { "Ember" } }, { "hare", { "Clover" } } });

        // Partial overlay: overrides only Nord male and Fox critters; Breton and Hare remain English
        pool.OverlayChildNames({ { "nord", { "Торвик" } } }, {});
        pool.OverlayCritterNames({ { "fox", { "Огонёк" } } });

        const auto nordMale = pool.GetChildNames("NordRace", false);
        const auto nordFemale = pool.GetChildNames("NordRace", true);
        const auto bretonMale = pool.GetChildNames("BretonRace", false);
        const auto foxNames = pool.GetCritterNames("FoxRace");
        const auto hareNames = pool.GetCritterNames("HareRace");

        MARAS_REQUIRE_EQ(nordMale.size(), 1u);
        MARAS_CHECK_EQ(nordMale[0], "Торвик");
        MARAS_REQUIRE_EQ(nordFemale.size(), 1u);
        MARAS_CHECK_EQ(nordFemale[0], "Alfrun");
        MARAS_REQUIRE_EQ(bretonMale.size(), 1u);
        MARAS_CHECK_EQ(bretonMale[0], "Aubrin");
        MARAS_REQUIRE_EQ(foxNames.size(), 1u);
        MARAS_CHECK_EQ(foxNames[0], "Огонёк");
        MARAS_REQUIRE_EQ(hareNames.size(), 1u);
        MARAS_CHECK_EQ(hareNames[0], "Clover");
    }

    MARAS_TEST(Localization, CatalogOverlayPriorityRespectsLiveYamlAndAddonDropIns) {
        // Base `en/catalogs.json` contains stock Warrior description
        StringDictionary baseEn{
            { "training.warrior.name", "Shield-Brother Vanguard" },
            { "training.warrior.description", "Stock English Warrior description." }
        };

        // Case 1: Active language is English ("en"), no `localization/en/zz_*.json` addon.
        // If a modder overrides `warrior` directly inside `childTraining/WarriorTweak.yaml`,
        // `GetOverlayOr` returns the YAML's updated `fallback` instead of clobbering it with `baseEn`!
        LocalizationMeta enMeta{ .code = "en", .name = "English", .nativeName = "English" };
        LocalizationCatalog enCat("auto", enMeta, baseEn, {});
        MARAS_CHECK_EQ(
            enCat.GetOverlayOr("training.warrior.description", "Modder custom YAML Warrior description."),
            "Modder custom YAML Warrior description.");
        MARAS_CHECK_EQ(
            enCat.GetOverlayOr("training.morag_tong.name", "Morag Tong"),
            "Morag Tong");

        // Case 2: Active language is German ("de") with `de/catalogs.json` + addon `de/zz_morag_tong.json`.
        // Translated overlay entries win over the English YAML `fallback`, while untranslated addon entries
        // fall back to the addon YAML's English `fallback`.
        StringDictionary deOverlay{
            { "training.warrior.description", "Deutsche Beschreibung für Krieger." },
            { "training.morag_tong.name", "Morag-Tong-Vollstrecker" }
        };
        LocalizationMeta deMeta{ .code = "de", .name = "German", .nativeName = "Deutsch" };
        LocalizationCatalog deCat("de", deMeta, baseEn, deOverlay);
        MARAS_CHECK_EQ(
            deCat.GetOverlayOr("training.warrior.description", "Modder custom YAML Warrior description."),
            "Deutsche Beschreibung für Krieger.");
        MARAS_CHECK_EQ(
            deCat.GetOverlayOr("training.morag_tong.name", "Morag Tong"),
            "Morag-Tong-Vollstrecker");
        MARAS_CHECK_EQ(
            deCat.GetOverlayOr("training.morag_tong.location", "Raven Rock Sanctum"),
            "Raven Rock Sanctum");
    }

}  // namespace
