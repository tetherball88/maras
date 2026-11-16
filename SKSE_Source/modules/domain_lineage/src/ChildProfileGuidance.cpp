#include "maras/domain/lineage/ChildProfileGuidance.h"

#include "maras/foundation/StringUtils.h"

#include <cctype>
#include <cmath>
#include <sstream>

namespace MARAS::Domain::Lineage {

    namespace {

        [[nodiscard]] std::string_view StripTrailingPunctuation(std::string_view s) noexcept {
            while (!s.empty() && (s.back() == '.' || s.back() == ';' || s.back() == ',')) {
                s.remove_suffix(1);
                s = Foundation::StringUtils::TrimView(s);
            }
            return s;
        }

        [[nodiscard]] std::string LowercaseLeadingAscii(std::string_view s) {
            std::string out(s);
            if (!out.empty()) {
                out[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[0])));
            }
            return out;
        }

    }  // namespace

    std::string FormatTrainingDeparturePhrase(
        std::string_view trainingContext,
        std::string_view trainingLocation) {
        const std::string_view ctx = StripTrailingPunctuation(Foundation::StringUtils::TrimView(trainingContext));
        const std::string_view loc = StripTrailingPunctuation(Foundation::StringUtils::TrimView(trainingLocation));

        if (!ctx.empty()) {
            if (Foundation::StringUtils::StartsWith(ctx, "to ") ||
                Foundation::StringUtils::StartsWith(ctx, "for ")) {
                return LowercaseLeadingAscii(ctx);
            }
            if (Foundation::StringUtils::StartsWith(ctx, "at ") ||
                Foundation::StringUtils::StartsWith(ctx, "in ") ||
                Foundation::StringUtils::StartsWith(ctx, "with ") ||
                Foundation::StringUtils::StartsWith(ctx, "under ") ||
                Foundation::StringUtils::StartsWith(ctx, "beneath ")) {
                return "for training " + LowercaseLeadingAscii(ctx);
            }
            if (!loc.empty() && !Foundation::StringUtils::Contains(ctx, loc)) {
                return "for training at " + std::string(loc) + " (" + std::string(ctx) + ")";
            }
            return "for training (" + std::string(ctx) + ")";
        }

        if (!loc.empty()) {
            return "for training at " + std::string(loc);
        }
        return "for training";
    }

    std::string JoinNamesForProse(const std::vector<std::string>& names) {
        if (names.empty()) {
            return "";
        }
        if (names.size() == 1) {
            return names[0];
        }
        if (names.size() == 2) {
            return names[0] + " and " + names[1];
        }
        std::string out;
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (i > 0) {
                out += (i + 1 == names.size()) ? ", and " : ", ";
            }
            out += names[i];
        }
        return out;
    }

    std::string ComputeBioFatherRoleLabel(
        bool bioFatherIsGuardian,
        bool hasPaternitySecret,
        bool isSingleMotherSecretFromBioFather,
        bool partnerKnowsPaternity,
        bool bioFatherKnowsPaternity) {
        if (hasPaternitySecret) {
            if (!partnerKnowsPaternity && !bioFatherKnowsPaternity) {
                return "True Biological Father (Paternity Secret — Unaware of Paternity; Presumed Father Also Unaware)";
            }
            if (!partnerKnowsPaternity && bioFatherKnowsPaternity) {
                return "True Biological Father (Paternity Secret — Secretly Knows Truth; Presumed Father Unaware)";
            }
            if (partnerKnowsPaternity && !bioFatherKnowsPaternity) {
                return "True Biological Father (Paternity Secret — Unaware of Paternity; Presumed Father Knows)";
            }
            return bioFatherIsGuardian
                ? "True Biological Father & Household Guardian (Paternity Known to Both Fathers)"
                : "True Biological Father (Paternity Known to Both Fathers)";
        }
        if (isSingleMotherSecretFromBioFather) {
            return "True Biological Father (Paternity Secret — Unaware of Paternity)";
        }
        return bioFatherIsGuardian
            ? "Biological Father & Household Guardian"
            : "Biological Father (Non-Custodial)";
    }

    std::string ComputePresumedFatherRoleLabel(
        bool presumedFatherIsGuardian,
        bool partnerKnowsPaternity,
        bool bioFatherKnowsPaternity) {
        if (!partnerKnowsPaternity && !bioFatherKnowsPaternity) {
            return presumedFatherIsGuardian
                ? "Presumed Father & Household Guardian (Believes Child Is Biologically His; Bio Father Also Unaware)"
                : "Presumed Father (Believes Child Is Biologically His; Bio Father Also Unaware)";
        }
        if (!partnerKnowsPaternity && bioFatherKnowsPaternity) {
            return presumedFatherIsGuardian
                ? "Presumed Father & Household Guardian (Believes Child Is Biologically His; Bio Father Secretly Knows)"
                : "Presumed Father (Believes Child Is Biologically His; Bio Father Secretly Knows)";
        }
        if (partnerKnowsPaternity && !bioFatherKnowsPaternity) {
            return presumedFatherIsGuardian
                ? "Presumed Father & Household Guardian (Knows Child Was Fathered by Bio Father; Bio Father Unaware)"
                : "Former Presumed Father / Mother's Partner (Knows Child Was Fathered by Bio Father; Non-Custodial; Bio Father Unaware)";
        }
        return presumedFatherIsGuardian
            ? "Presumed Father & Household Guardian / Step-Father Figure (Both Fathers Know True Paternity)"
            : "Former Presumed Father / Mother's Partner (Both Fathers Know True Paternity; Non-Custodial)";
    }

    std::int64_t ComputeChildRecentEventsMaxAgeSeconds(
        bool isBiologicalChild,
        bool isAdopted,
        float birthTimestampDays,
        std::int32_t registeredTimestampDays,
        float currentGameDays) noexcept {
        (void)isBiologicalChild;
        constexpr double kSecondsPerDay = 86400.0;
        constexpr std::int64_t kEventBoundaryBufferSeconds = 120;

        if (birthTimestampDays > 0.0f && currentGameDays >= birthTimestampDays) {
            return std::llround(
                       (static_cast<double>(currentGameDays) - static_cast<double>(birthTimestampDays)) *
                       kSecondsPerDay) +
                   kEventBoundaryBufferSeconds;
        }
        if (isAdopted && registeredTimestampDays > 0 &&
            currentGameDays >= static_cast<float>(registeredTimestampDays)) {
            return std::llround(
                       (static_cast<double>(currentGameDays) - static_cast<double>(registeredTimestampDays)) *
                       kSecondsPerDay) +
                   kEventBoundaryBufferSeconds;
        }
        return 0;
    }

    std::string BuildChildProfileGenerationGuidance(const ChildProfileGuidanceInput& d) {
        const std::string relWord = d.isFemale ? "daughter" : "son";
        const std::string pronounSubj = d.isFemale ? "she" : "he";
        const std::string pronounObj = d.isFemale ? "her" : "him";
        const std::string pronounPoss = d.isFemale ? "her" : "his";
        std::ostringstream ss;

        if (d.stage == Foundation::ChildStage::Adult) {
            ss << "IMPORTANT: " << d.childName << " is a GROWN YOUNG ADULT " << relWord
               << " who was born and raised in this playthrough and has now completed training and come of age as an adult follower/citizen in Skyrim. "
               << "Do NOT describe " << d.childName << " as a small child.\n";
        } else {
            ss << "IMPORTANT: " << d.childName << " is a YOUNG CHILD (" << relWord
               << ", roughly 8-11 years old) who was born in this playthrough and has just grown from infancy into a young child living at home. "
               << "Write the profile, occupation, skills, aspirations, and speech_style specifically for a young child in Skyrim, NOT an adult.\n";
        }

        if (d.stage == Foundation::ChildStage::Adult && d.hasInheritedChildhoodIdentity) {
            ss << "\n## Inherited Childhood Profile Evolution (CRITICAL)\n"
               << "- `Current Dynamic Bio` is " << d.childName
               << "'s established childhood profile from before leaving for training. "
               << "You MUST evolve `summary`, `interject_summary`, `appearance`, `occupation`, `skills`, "
               << "`speech_style`, `aspirations`, `background`, `personality`, and `relationships` from a young child "
               << "into a grown young adult while preserving " << d.childName
               << "'s childhood memories, core personality traits, inherited physical features, and family relationships.\n";
        }

        ss << "\n## Narrative Time & Gameplay Time-Compression Rules (CRITICAL)\n"
           << "- Short Tamrielic calendar intervals in `Recent Events` between pregnancy, birth, growing from infancy into a young child, or completing adolescent training (whether hours, a single day, days, or months in-game) are **gameplay time-scale abstractions** for playability—NOT literal rapid aging in-universe.\n"
           << "- **NEVER** state or imply that " << d.childName << " grew up rapidly in a single day or a short period, **NEVER** invent a magical or supernatural growth anomaly, accelerated aging, or time distortion to explain `Recent Events` dates, and **NEVER** state that " << d.childName << " lacks memories of infancy or early childhood or suddenly appeared at " << pronounPoss << " current age.\n"
           << "- Portray " << d.childName << " as having lived a natural multi-year childhood ("
           << (d.stage == Foundation::ChildStage::Adult
                   ? "a full childhood followed by years of adolescent training into young adulthood"
                   : "roughly 8-11 winters of normal upbringing")
           << ") raised by " << pronounPoss << " custodial household guardian(s).\n";

        ss << "\n## Involved Parties at the Moment of Bio Generation/Update\n";
        if (!d.motherName.empty()) {
            ss << "- **Biological Mother**: " << d.motherName;
            if (!d.motherRace.empty()) ss << " (" << d.motherRace << ")";
            ss << (d.motherIsGuardian ? " — Custodial Household Guardian raising " + d.childName : " — Non-custodial biological mother") << ".\n";
        }
        if (!d.bioFatherName.empty()) {
            ss << "- **True Biological Father**: " << d.bioFatherName;
            if (!d.bioFatherRace.empty()) ss << " (" << d.bioFatherRace << ")";
            if (d.hasPaternitySecret) {
                ss << " — Genetic sire of " << d.childName << " ("
                   << (d.bioFatherKnowsPaternity ? "KNOWS he is the biological father" : "DOES NOT KNOW he is the biological father")
                   << (d.bioFatherIsGuardian ? "; also Custodial Household Guardian" : "") << ").\n";
            } else if (d.isSingleMotherSecretFromBioFather) {
                ss << " — Genetic sire of " << d.childName << " (DOES NOT KNOW he is the biological father).\n";
            } else {
                ss << (d.bioFatherIsGuardian ? " — Custodial Household Guardian raising " + d.childName : " — Non-custodial biological father") << ".\n";
            }
        }
        if (d.hasPresumedFather && !d.presumedFatherName.empty()) {
            ss << "- **Presumed Father (Mother's Partner)**: " << d.presumedFatherName;
            if (!d.presumedFatherRace.empty()) ss << " (" << d.presumedFatherRace << ")";
            if (!d.motherPresumedRelStatus.empty() && d.motherPresumedRelStatus != "Unknown") {
                ss << " [Relationship with " << d.motherName << ": " << d.motherPresumedRelStatus << "]";
            }
            ss << " — "
               << (d.partnerKnowsPaternity
                       ? "KNOWS that " + d.childName + " is NOT his biological child and was fathered by " + (d.bioFatherName.empty() ? "another man" : d.bioFatherName)
                       : "BELIEVES " + d.childName + " is his own biological " + relWord)
               << (d.presumedFatherIsGuardian
                       ? "; actively raising " + d.childName + " as a Household Guardian"
                       : "; NOT a current household guardian")
               << ".\n";
        }

        if (!d.guardianAName.empty() || !d.guardianBName.empty()) {
            std::vector<std::string> gNames;
            if (!d.guardianAName.empty()) gNames.push_back(d.guardianAName);
            if (!d.guardianBName.empty() && d.guardianBName != d.guardianAName) gNames.push_back(d.guardianBName);
            ss << "- **Custodial Household Guardians (Upbringing)**: " << JoinNamesForProse(gNames) << ".\n";
        }

        if (!d.siblingNames.empty()) {
            ss << "- **Siblings**: " << JoinNamesForProse(d.siblingNames) << ".\n";
        }
        if (!d.homeLocationName.empty()) {
            ss << "- **Family Home / Upbringing Location**: " << d.homeLocationName << ".\n";
        }
        if (d.stage == Foundation::ChildStage::Adult &&
            (!d.trainingLocation.empty() || !d.trainingClassName.empty() || !d.trainingContext.empty())) {
            ss << "- **Completed Adolescent Training**: ";
            if (!d.trainingClassName.empty() && !d.trainingLocation.empty()) {
                ss << d.trainingClassName << " training at " << d.trainingLocation;
            } else if (!d.trainingLocation.empty()) {
                ss << "At " << d.trainingLocation;
            } else if (!d.trainingClassName.empty()) {
                ss << d.trainingClassName;
            } else {
                ss << "Adolescent apprenticeship";
            }
            const std::string_view trimmedContext = Foundation::StringUtils::TrimView(d.trainingContext);
            if (!trimmedContext.empty()) {
                ss << " — sent away during adolescence "
                   << FormatTrainingDeparturePhrase(trimmedContext, d.trainingLocation);
            } else if (!d.trainingDescription.empty()) {
                ss << " — " << d.trainingDescription;
            }
            ss << ". Reflect how this setting, mentorship, daily regimen, and institutional culture shaped their skills, personality, mannerisms, worldview, background, and occupation.\n";
        }

        std::vector<std::string> involvedNames;
        auto pushUniqueName = [&](const std::string& name) {
            if (name.empty()) {
                return;
            }
            for (const auto& existing : involvedNames) {
                if (existing == name) {
                    return;
                }
            }
            involvedNames.push_back(name);
        };
        pushUniqueName(d.motherName);
        pushUniqueName(d.bioFatherName);
        if (d.hasPresumedFather) {
            pushUniqueName(d.presumedFatherName);
        }
        pushUniqueName(d.guardianAName);
        pushUniqueName(d.guardianBName);

        ss << "\n## Scope of Involved Parties & Exclusion of Uninvolved Ex-Partners (CRITICAL)\n";
        if (!involvedNames.empty()) {
            ss << "- **Authoritative Family Scope**: **ONLY** the adults listed under `## Involved Parties at the Moment of Bio Generation/Update` ("
               << JoinNamesForProse(involvedNames)
               << ") have any parental, step-parental, or household role in " << d.childName << "'s life.\n";
        } else {
            ss << "- **Authoritative Family Scope**: **ONLY** the adults explicitly listed under `## Involved Parties at the Moment of Bio Generation/Update` have any parental, step-parental, or household role in " << d.childName << "'s life.\n";
        }
        ss << "- **Exclude Uninvolved Ex-Partners**: Any other former spouse, ex-partner, or past lover of a parent mentioned in pre-birth `Recent Events` (such as arguments, breakups, or divorces before " << d.childName << " was born) or in a parent's un-regenerated character bio prose who is **NOT** listed under `## Involved Parties at the Moment of Bio Generation/Update` had **ZERO** role in " << d.childName << "'s birth or upbringing, was **NEVER** " << d.childName << "'s stepfather, stepmother, or guardian, and must **NOT** be mentioned in " << d.childName << "'s `relationships`, `summary`, `interject_summary`, or `background`.\n"
           << "- **Stale Parent Bio Warning**: A parent's stored `Summary`, `Background`, `Personality`, or `Occupation` blocks below may not yet have regenerated after a recent marriage or divorce and may still mention an outdated spouse, ex-partner, or prior faction ties. Always treat `## Involved Parties at the Moment of Bio Generation/Update` and each parent's `Current Family & Household Status` as authoritative, ignoring conflicting marital claims or uninvolved ex-partners' factions.\n";

        ss << "\n## Biological Inheritance vs. Household Upbringing Rules\n";
        ss << "1. **Genetic & Supernatural Inheritance (Strictly from True Biological Parents)**:\n";
        if (!d.motherName.empty() || !d.bioFatherName.empty()) {
            ss << "   - " << d.childName << "'s biological, physical, and innate heritage comes strictly from ";
            if (!d.motherName.empty() && !d.bioFatherName.empty()) {
                ss << "**" << d.motherName << "** (Biological Mother) and **" << d.bioFatherName << "** (True Biological Father)";
            } else if (!d.motherName.empty()) {
                ss << "**" << d.motherName << "** (Biological Mother)";
            } else {
                ss << "**" << d.bioFatherName << "** (True Biological Father)";
            }
            if (d.hasPresumedFather && !d.presumedFatherName.empty()) {
                ss << " — NEVER genetically from the non-biological presumed father (" << d.presumedFatherName << ")";
            }
            ss << ".\n";
        }
        ss << "   - Carefully inspect the full rendered character profiles of the biological parents below—especially their **Background**, **Appearance**, **Occupation**, **Skills & Abilities**, and **Personality** sections (do NOT rely solely on a parent's brief `Summary`, which conceals hidden traits such as vampirism, lycanthropy, or secret practices due to the Stranger Test; account for any supernatural nature, bloodline, or magical/martial lineage present in their Background, Occupation, and Skills).\n";
        ss << "   - Reflect inherited physical traits (eye color, hair, complexion, facial structure, build, subtle vampiric/elven/beast/bloodline traits), innate temperament, and latent magical or martial affinities in " << d.childName << "'s `appearance`, `personality`, `background`, and `skills`.\n";
        if (d.hasPhenotypeMismatch && !d.visualDiscrepancyDescription.empty()) {
            ss << "   - **CRITICAL PHENOTYPE DISCREPANCY**: " << d.visualDiscrepancyDescription
               << " Ensure " << d.childName << "'s `appearance` accurately reflects this biological heritage.\n";
        }
        if (d.hasBeastblood) {
            if (d.stage == Foundation::ChildStage::Adult) {
                ss << "   - **LYCANTHROPY / HIRCINE'S BEASTBLOOD**: " << d.childName
                   << " carries Hircine's Beastblood (inherited genetically from werewolf biological parentage"
                   << (d.trainedInWerewolfPack && !d.trainingLocation.empty()
                           ? " and honed through pack training at " + d.trainingLocation
                           : "")
                   << "). As an adult, " << d.childName
                   << " possesses complete immunity to disease, heightened predatory senses, lupine endurance, and the ability to assume Beast Form (`Werewolf`), "
                   << "though in daily life and towns " << d.childName << " walks in human form and keeps the Beastblood guarded from outsiders (do NOT reveal lycanthropy in `summary` due to the Stranger Test; weave it into `background`, `personality`, and `skills`).\n";
            } else {
                ss << "   - **INHERITED BEASTBLOOD (LATENT LYCANTHROPY)**: " << d.childName
                   << " inherited Hircine's Beastblood genetically from a werewolf biological parent. As a young child, " << d.childName
                   << " cannot yet transform into a full werewolf beast, but the dormant Beastblood manifests as unnaturally sharp senses (hearing and smell), "
                   << "boundless physical stamina, immunity to common sickness, and a restless, wolfish instinct (keep this subtle in `summary` and reflect it in `appearance`, `personality`, and `skills`).\n";
            }
        }
        const std::string bioFatherLabel = d.bioFatherName.empty() ? "the biological father" : d.bioFatherName;
        const std::string bioMotherLabel = d.motherName.empty() ? "the biological mother" : d.motherName;
        const bool bioFatherUnaware = !d.bioFatherKnowsPaternity || d.isSingleMotherSecretFromBioFather;

        ss << "2. **Biological Father's Impact (" << bioFatherLabel << ")**:\n"
           << "   - **Innate Traits**: " << d.childName << " inherits genetic appearance, bloodline/supernatural traits, and innate temperament tendencies from "
           << bioFatherLabel << " and " << bioMotherLabel << ".\n";
        if (bioFatherUnaware) {
            ss << "   - **Behavioral & Mentorship Impact**: Because " << bioFatherLabel << " does **NOT** know he is " << d.childName << "'s father, he has **NOT** mentored or shaped " << d.childName << "'s learned habits, speech style, or upbringing; his contribution is strictly genetic and innate.\n";
        } else if (d.hasPaternitySecret && !d.partnerKnowsPaternity) {
            ss << "   - **Behavioral & Mentorship Impact**: Because " << bioFatherLabel << " secretly knows " << d.childName << " is his biological " << relWord << " while " << d.presumedFatherName << " remains unaware, " << bioFatherLabel << " subtly influences " << d.childName << " from the sidelines (quiet protectiveness, gifts, occasional advice, or watchful interest consistent with his rendered personality) without exposing the secret.\n";
        } else if (d.hasPaternitySecret) {
            ss << "   - **Behavioral & Mentorship Impact**: Because " << bioFatherLabel << " is known to be " << d.childName << "'s biological father"
               << (d.bioFatherIsGuardian ? " and is raising " + d.childName + " as a household guardian" : "")
               << ", his personality, mentorship, and example in his rendered profile directly shape " << d.childName << "'s behavior, learned skills, speech style, aspirations, and self-identity.\n";
        } else if (d.bioFatherIsGuardian) {
            ss << "   - **Behavioral & Mentorship Impact**: " << bioFatherLabel << " is actively raising " << d.childName << " in the household; his personality, mentorship, trade, and speech patterns in his rendered profile directly shape " << d.childName << "'s behavior, learned skills, speech style, aspirations, and values.\n";
        } else {
            ss << "   - **Behavioral & Mentorship Impact**: " << bioFatherLabel << " is " << d.childName << "'s known non-custodial biological father; reflect how his identity and example in his rendered profile influence " << d.childName << "'s self-identity, aspirations, and relationship with him.\n";
        }

        ss << "3. **Custodial Guardians & Presumed Father's Impact on Raising " << d.childName << "**:\n"
           << "   - " << d.childName << "'s daily habits, manners, speech style, childhood memories, values, and household relationships are shaped by the adults who actually raised "
           << pronounObj << " and by post-birth household interactions with " << d.childName << " and " << pronounPoss << " guardians.\n";
        if (d.hasPresumedFather && !d.presumedFatherName.empty()) {
            ss << "   - **Presumed Father (" << d.presumedFatherName << ") Upbringing Impact**: ";
            if (!d.partnerKnowsPaternity) {
                ss << d.presumedFatherName << " **does NOT know** the paternity truth and believes " << d.childName << " is his own biological " << relWord << ". He raises " << d.childName << " wholeheartedly as his own flesh and blood; " << d.childName << " absorbs his habits, values, trade/interests, and speech patterns from his rendered profile, viewing him with uncomplicated filial devotion.\n";
            } else if (d.presumedFatherIsGuardian) {
                ss << d.presumedFatherName << " **KNOWS** that " << d.childName << " is not biologically his (fathered by " << bioFatherLabel << ") while raising " << d.childName << " as a household guardian. Examine " << d.presumedFatherName << "'s personality and temperament in his rendered profile to determine how knowing the child is not biologically his shapes the way he raises or treats " << d.childName << " (devoted, forgiving father figure vs. wounded pride, sternness, emotional distance, or household tension with " << d.motherName << "), and reflect that impact on " << d.childName << "'s behavior, emotional outlook, and relationship with him.\n";
            } else {
                ss << d.presumedFatherName << " **KNOWS** that " << d.childName << " is not biologically his (fathered by " << bioFatherLabel << ") and is not a current household guardian. Reflect how the history of him knowing the truth (and any resulting separation or tension with " << d.motherName << " in his rendered profile and recent events) shapes " << d.childName << "'s family background and view of him.\n";
            }
        }

        if (d.hasPaternitySecret && !d.presumedFatherName.empty() && !d.bioFatherName.empty()) {
            ss << "\n## Paternity Secret & Knowledge Permutation Rules (CRITICAL)\n";
            if (!d.partnerKnowsPaternity && !d.bioFatherKnowsPaternity) {
                ss << "- **Active Permutation — NEITHER Presumed Father (" << d.presumedFatherName << ") NOR Biological Father (" << d.bioFatherName << ") Knows the Truth**:\n"
                   << "  - Only " << d.motherName << " knows that " << d.bioFatherName << " is " << d.childName << "'s true biological father.\n"
                   << "  - **Presumed Father (" << d.presumedFatherName << ")**: Completely unaware; genuinely believes " << d.childName << " is his own biological " << relWord << " and raises " << pronounObj << " wholeheartedly as his own child.\n"
                   << "  - **Biological Father (" << d.bioFatherName << ")**: Completely unaware that " << d.childName << " is his biological " << relWord << "; he contributes ONLY genetic/innate traits (never paternal mentorship or learned behavior).\n"
                   << "  - **Child (" << d.childName << ")**: Genuinely believes " << d.presumedFatherName << " is " << pronounPoss << " biological father, emulates " << d.presumedFatherName << "'s learned habits and values, and has NO idea that " << d.bioFatherName << " is " << pronounPoss << " sire.\n"
                   << "  - **How to write the bio blocks**:\n"
                   << "    - `summary` & `interject_summary`: Present " << d.childName << " as the " << relWord << " of " << d.motherName << " and " << d.presumedFatherName << ". Do NOT mention " << d.bioFatherName << " or the secret in `summary`.\n"
                   << "    - `appearance`, `personality`, & `skills`: " << d.childName << " STILL biologically inherits physical features, innate temperament, and latent affinities from **" << d.motherName << "** and **" << d.bioFatherName << "** (describe these traits naturally as part of " << d.childName << " without " << d.childName << " knowing they come from " << d.bioFatherName << "), while learned habits and childhood lessons come from **" << d.motherName << "** and **" << d.presumedFatherName << "**.\n"
                   << "    - `background`, `relationships`, & `aspirations`: Write from " << d.childName << "'s lived experience believing " << d.presumedFatherName << " is " << pronounPoss << " father. In `relationships`, list " << d.presumedFatherName << " as Father; do NOT list " << d.bioFatherName << " as biological father (or note that " << d.childName << " is completely unaware of any blood tie to " << d.bioFatherName << ").\n";
            } else if (!d.partnerKnowsPaternity && d.bioFatherKnowsPaternity) {
                ss << "- **Active Permutation — ONLY Biological Father (" << d.bioFatherName << ") Knows; Presumed Father (" << d.presumedFatherName << ") Does NOT Know**:\n"
                   << "  - Both " << d.motherName << " and " << d.bioFatherName << " secretly know that " << d.bioFatherName << " is " << d.childName << "'s true biological father.\n"
                   << "  - **Presumed Father (" << d.presumedFatherName << ") & Child (" << d.childName << ")**: Remain unaware; " << d.presumedFatherName << " raises " << d.childName << " believing " << pronounSubj << " is his own biological child, and " << d.childName << " believes " << d.presumedFatherName << " is " << pronounPoss << " biological father.\n"
                   << "  - **Biological Father (" << d.bioFatherName << ")**: Contributes genetic/innate traits AND, because he secretly knows " << d.childName << " is his child, may subtly influence " << d.childName << " from the sidelines (e.g., quiet protectiveness, gifts, occasional advice, or watchful interest consistent with " << d.bioFatherName << "'s rendered personality) without exposing the secret.\n"
                   << "  - **How to write the bio blocks**:\n"
                   << "    - `summary` & `interject_summary`: Present " << d.childName << " publicly as the " << relWord << " of " << d.motherName << " and " << d.presumedFatherName << ". Do NOT expose the secret in `summary`.\n"
                   << "    - `appearance`, `personality`, & `skills`: Biologically inherited from **" << d.motherName << "** and **" << d.bioFatherName << "**, combined with household upbringing from **" << d.motherName << "** and **" << d.presumedFatherName << "**.\n"
                   << "    - `background` & `relationships`: " << d.childName << " views " << d.presumedFatherName << " as " << pronounPoss << " biological father. However, because " << d.bioFatherName << " knows the truth, `background` and `relationships` may reflect that " << d.bioFatherName << " has shown a quiet, watchful fondness or special interest toward " << d.childName << " (while " << d.childName << " and " << d.presumedFatherName << " remain unaware that " << d.bioFatherName << " is the biological sire).\n";
            } else if (d.partnerKnowsPaternity && !d.bioFatherKnowsPaternity) {
                ss << "- **Active Permutation — ONLY Presumed Father (" << d.presumedFatherName << ") Knows; Biological Father (" << d.bioFatherName << ") Does NOT Know**:\n"
                   << "  - Both " << d.motherName << " and " << d.presumedFatherName << " know that " << d.childName << " is NOT " << d.presumedFatherName << "'s biological child and was fathered by " << d.bioFatherName << ".\n"
                   << "  - **Biological Father (" << d.bioFatherName << ")**: Has never been told and remains completely unaware that " << d.childName << " is his biological " << relWord << "; he contributes genetic/innate traits, but has NOT actively mentored or parented " << d.childName << ".\n"
                   << "  - **How to write the bio blocks**:\n"
                   << "    - `appearance`, `personality`, & `skills`: Genetic appearance and innate affinities come from **" << d.motherName << "** and **" << d.bioFatherName << "**.\n"
                   << "    - `background`, `personality`, `aspirations`, & `relationships`: ";
                if (d.presumedFatherIsGuardian) {
                    ss << d.presumedFatherName << " continues raising " << d.childName << " in the household alongside " << d.motherName << " while knowing " << d.childName << " was biologically fathered by " << d.bioFatherName << ". Based on " << d.presumedFatherName << "'s rendered profile, reflect how his knowledge of the affair/paternity shapes his parenting (whether he raises " << d.childName << " with forgiving warmth as a step-father/guardian or with guarded pride/tension) and how that shapes " << d.childName << "'s behavior. Also reflect that while the household knows " << d.bioFatherName << " is the biological father, " << d.bioFatherName << " himself remains unaware.\n";
                } else {
                    ss << d.presumedFatherName << " was initially presumed to be " << d.childName << "'s father and knows the truth, but is not a current household guardian; meanwhile, " << d.bioFatherName << " still does not know he is " << d.childName << "'s biological father.\n";
                }
                ss << "    - `summary`: Keep `summary` focused on publicly observable facts (age, race, mother, and current household guardians), keeping private paternity details in `background` and `relationships`.\n";
            } else {
                ss << "- **Active Permutation — BOTH Presumed Father (" << d.presumedFatherName << ") AND Biological Father (" << d.bioFatherName << ") Know the Truth**:\n"
                   << "  - The paternity truth is known to both " << d.presumedFatherName << " and " << d.bioFatherName << " (as well as " << d.motherName << " and the family): " << d.bioFatherName << " is openly known to be " << d.childName << "'s true biological father, while " << d.presumedFatherName << " was initially presumed to be the father.\n"
                   << "  - **How to write the bio blocks**:\n"
                   << "    - `appearance`, `personality`, & `skills`: Biologically inherited from **" << d.motherName << "** and **" << d.bioFatherName << "**.\n"
                   << "    - `background`, `personality`, `speech_style`, `aspirations`, & `relationships`: Because the truth is known to both fathers and the family, **" << d.bioFatherName << "** impacts not only " << d.childName << "'s innate traits but also " << pronounPoss << " behavior, self-identity, and aspirations (e.g., emulating, seeking approval from, or reacting to " << d.bioFatherName << "). ";
                if (d.presumedFatherIsGuardian) {
                    ss << "Meanwhile, in the household, **" << d.presumedFatherName << "** raises " << d.childName << " as a guardian and father figure alongside " << d.motherName << " while knowing the truth; reflect how both " << d.presumedFatherName << "'s day-to-day raising (shaped by his personality and knowledge of the truth) and " << d.bioFatherName << "'s known biological fatherhood shape " << d.childName << "'s behavior and distinct bonds with both men.\n";
                } else if (d.bioFatherIsGuardian) {
                    ss << d.motherName << " and " << d.bioFatherName << " are raising " << d.childName << " as custodial guardians (so " << d.bioFatherName << " shapes both genetic traits and daily upbringing), with " << d.presumedFatherName << " as " << d.motherName << "'s former partner who was once presumed to be the father.\n";
                } else {
                    ss << "Reflect " << d.childName << "'s upbringing by " << pronounPoss << " household guardian(s) and how knowing both biological father " << d.bioFatherName << " and former presumed father " << d.presumedFatherName << " shapes " << d.childName << "'s behavior and relationships.\n";
                }
                ss << "    - `summary`: Keep `summary` focused on publicly observable facts, placing detailed family history in `background` and `relationships`.\n";
            }
        } else if (d.isSingleMotherSecretFromBioFather && !d.bioFatherName.empty()) {
            ss << "\n## Paternity Secret Rules\n"
               << "- **" << d.motherName << " is keeping " << d.childName << "'s paternity a secret from Biological Father (" << d.bioFatherName << ")**:\n"
               << "  - " << d.bioFatherName << " does not know that " << d.childName << " is his biological " << relWord << ", so he contributes ONLY genetic/physical/supernatural traits and innate temperament (not paternal mentorship or learned behavior).\n"
               << "  - " << d.childName << "'s upbringing, learned habits, and daily behavior are shaped by " << d.motherName << " (and any household guardian).\n";
        }

        return ss.str();
    }

    ChildSeedMemorySpec BuildChildStageSeedMemory(const ChildProfileGuidanceInput& d) {
        ChildSeedMemorySpec spec{};

        if (d.stage == Foundation::ChildStage::Child) {
            if (d.isAdopted || !d.isBiologicalChild) {
                return spec;
            }

            const bool hideBioFather =
                !d.bioFatherKnowsPaternity ||
                (d.hasPaternitySecret && !d.partnerKnowsPaternity) ||
                d.isSingleMotherSecretFromBioFather;

            std::vector<std::string> raisedBy;
            auto pushUnique = [&](const std::string& name) {
                if (name.empty()) {
                    return;
                }
                for (const auto& existing : raisedBy) {
                    if (existing == name) {
                        return;
                    }
                }
                raisedBy.push_back(name);
            };

            if (!d.guardianAName.empty() && !(hideBioFather && d.guardianAName == d.bioFatherName)) {
                pushUnique(d.guardianAName);
            }
            if (!d.guardianBName.empty() && !(hideBioFather && d.guardianBName == d.bioFatherName)) {
                pushUnique(d.guardianBName);
            }
            pushUnique(d.motherName);

            bool mentionPresumedFatherAsFather = false;
            if (d.hasPaternitySecret && !d.partnerKnowsPaternity && !d.presumedFatherName.empty()) {
                bool inRaisedBy = false;
                for (const auto& existing : raisedBy) {
                    if (existing == d.presumedFatherName) {
                        inRaisedBy = true;
                        break;
                    }
                }
                if (!inRaisedBy) {
                    mentionPresumedFatherAsFather = true;
                }
            }

            bool mentionBioFatherAsFather = false;
            if (!hideBioFather && !d.bioFatherName.empty()) {
                if (d.bioFatherIsGuardian) {
                    pushUnique(d.bioFatherName);
                } else {
                    bool inRaisedBy = false;
                    for (const auto& existing : raisedBy) {
                        if (existing == d.bioFatherName) {
                            inRaisedBy = true;
                            break;
                        }
                    }
                    if (!inRaisedBy) {
                        mentionBioFatherAsFather = true;
                    }
                }
            }

            const std::string homeLoc = d.homeLocationName.empty() ? "Skyrim" : d.homeLocationName;
            const std::string caretakers = raisedBy.empty() ? "my family" : JoinNamesForProse(raisedBy);

            std::ostringstream out;
            out << "I spent my early childhood growing up from infancy in " << homeLoc
                << ", raised by " << caretakers;
            if (mentionPresumedFatherAsFather) {
                out << ", knowing " << d.presumedFatherName << " as my father";
            } else if (mentionBioFatherAsFather) {
                out << ", knowing " << d.bioFatherName << " as my father";
            }
            out << ".";
            if (!d.siblingNames.empty()) {
                out << " Growing up alongside " << JoinNamesForProse(d.siblingNames)
                    << " shaped my earliest days at home.";
            }

            spec.shouldCreate = true;
            spec.content = out.str();
            spec.importance = 0.75f;
            spec.memoryType = "RELATIONSHIP";
            spec.emotion = "content";
            spec.location = homeLoc;
            spec.tags = { "childhood", "family", "upbringing" };
            return spec;
        }

        if (d.stage == Foundation::ChildStage::Adult) {
            if (d.trainingClassName.empty() && d.trainingLocation.empty() && d.trainingContext.empty()) {
                return spec;
            }

            std::string trainingPhrase;
            if (!d.trainingClassName.empty() && !d.trainingLocation.empty()) {
                trainingPhrase = d.trainingClassName + " training at " + d.trainingLocation;
            } else if (!d.trainingClassName.empty()) {
                trainingPhrase = d.trainingClassName + " training";
            } else if (!d.trainingLocation.empty()) {
                trainingPhrase = "training at " + d.trainingLocation;
            } else {
                trainingPhrase = "adolescent training";
            }

            std::ostringstream out;
            const std::string_view trimmedContext = Foundation::StringUtils::TrimView(d.trainingContext);
            if (!trimmedContext.empty()) {
                out << "During my youth, I was sent away from home "
                    << FormatTrainingDeparturePhrase(trimmedContext, d.trainingLocation)
                    << ". Having completed my " << trainingPhrase
                    << " and come of age as a young adult, I set out across Skyrim carrying that upbringing alongside the memories of my childhood";
            } else {
                out << "After completing my " << trainingPhrase
                    << ", I came of age as a young adult and set out across Skyrim, carrying the memories of my childhood";
            }
            if (!d.homeLocationName.empty()) {
                out << " in " << d.homeLocationName;
            }
            out << " and my family.";

            spec.shouldCreate = true;
            spec.content = out.str();
            spec.importance = 0.8f;
            spec.memoryType = "SKILL";
            spec.emotion = "proud";
            spec.location = !d.trainingLocation.empty()
                ? d.trainingLocation
                : (!d.homeLocationName.empty() ? d.homeLocationName : "Skyrim");
            spec.tags = { "training", "adulthood", "graduation" };
            return spec;
        }

        return spec;
    }

}  // namespace MARAS::Domain::Lineage
