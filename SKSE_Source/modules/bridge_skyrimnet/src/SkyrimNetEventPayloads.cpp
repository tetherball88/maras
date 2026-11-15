#include "maras/bridge_skyrimnet/SkyrimNetEventPayloads.h"

#include "maras/engine/ActorEngineUtils.h"
#include "maras/engine/ActorService.h"
#include "maras/foundation/StringUtils.h"
#include "maras/foundation/StrongIds.h"
#include "maras/projection/PerspectiveView.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace MARAS::Bridge::SkyrimNet {

    namespace {

        [[nodiscard]] std::string SafeDump(const nlohmann::json& j) {
            return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        }

        void SetNonEmpty(nlohmann::json& j, std::string_view key, std::string_view value) {
            if (!value.empty()) {
                j[key] = value;
            }
        }

        [[nodiscard]] nlohmann::json MakeBaseEventJson(
            std::string_view eventType,
            std::string_view msg,
            bool skipTrigger) {
            nlohmann::json j;
            j["type"] = eventType;
            j["msg"] = msg;
            j["skipTrigger"] = skipTrigger;
            return j;
        }

        struct ChildDescriptor {
            std::string relation{ "child" };
            std::string gender{ "unknown" };
            std::string possPronoun{ "their" };
        };

        [[nodiscard]] ChildDescriptor DescribeChildGender(std::uint8_t gender) {
            if (gender == 0) {
                return { "son", "male", "his" };
            }
            if (gender == 1) {
                return { "daughter", "female", "her" };
            }
            return { "child", "unknown", "their" };
        }

        struct BirthChildItem {
            std::uint32_t childID{ 0 };
            std::string name{};
            std::uint8_t gender{ 2 };
            std::string relation{};
            bool isSecretForPartner{ false };
            bool isSecretForBio{ false };
        };

        struct BirthFatherGroup {
            std::uint32_t bioFatherID{ 0 };
            std::string name{};
            bool isPartner{ false };
            std::vector<BirthChildItem> children{};
        };

        [[nodiscard]] std::string FormatChildItem(const BirthChildItem& c, bool withArticle = true) {
            const std::string rel = c.relation.empty() ? "child" : c.relation;
            if (!withArticle) {
                return c.name.empty() ? rel : (rel + ", " + c.name);
            }
            const std::string base =
                (rel == "child" ? "a child" : (rel == "son" ? "a son" : "a daughter"));
            return c.name.empty() ? base : (base + ", " + c.name);
        }

        [[nodiscard]] std::string FormatChildrenGroup(const std::vector<BirthChildItem>& children) {
            if (children.empty()) {
                return {};
            }
            if (children.size() == 1) {
                return FormatChildItem(children[0], true);
            }
            if (children.size() == 2) {
                const bool bothGirls = (children[0].gender == 1 && children[1].gender == 1);
                const bool bothBoys = (children[0].gender == 0 && children[1].gender == 0);
                if (bothGirls) {
                    return (!children[0].name.empty() && !children[1].name.empty())
                               ? ("twin daughters, " + children[0].name + " and " + children[1].name)
                               : "twin daughters";
                }
                if (bothBoys) {
                    return (!children[0].name.empty() && !children[1].name.empty())
                               ? ("twin sons, " + children[0].name + " and " + children[1].name)
                               : "twin sons";
                }
                return FormatChildItem(children[0], true) + ", and " + FormatChildItem(children[1], true);
            }
            if (children.size() == 3) {
                const bool allGirls = (children[0].gender == 1 && children[1].gender == 1 && children[2].gender == 1);
                const bool allBoys = (children[0].gender == 0 && children[1].gender == 0 && children[2].gender == 0);
                if (allGirls) {
                    return (!children[0].name.empty() && !children[1].name.empty() && !children[2].name.empty())
                               ? ("triplet daughters, " + children[0].name + ", " + children[1].name + ", and " + children[2].name)
                               : "triplet daughters";
                }
                if (allBoys) {
                    return (!children[0].name.empty() && !children[1].name.empty() && !children[2].name.empty())
                               ? ("triplet sons, " + children[0].name + ", " + children[1].name + ", and " + children[2].name)
                               : "triplet sons";
                }
            }

            std::string res;
            for (std::size_t i = 0; i < children.size(); ++i) {
                if (i > 0) {
                    res += (i == children.size() - 1 ? ", and " : ", ");
                }
                res += FormatChildItem(children[i], true);
            }
            return res;
        }

    }  // namespace

    std::string BuildPolygamyBlessingPayload(
        std::string_view playerName,
        std::string_view spouseName,
        bool skipTrigger) {
        const std::string msg = std::format(
            "{} got Mara's blessing to enter polygamy marriage. {} agreed with {}'s new Dragonborn reality and new way of their marriage'.",
            playerName,
            spouseName,
            playerName);
        return SafeDump(MakeBaseEventJson("ttm_polygamy_blessing", msg, skipTrigger));
    }

    std::string BuildAffectionGiftPayload(
        std::string_view msg,
        bool skipTrigger) {
        return SafeDump(MakeBaseEventJson("ttm_affection_gift", msg, skipTrigger));
    }

    std::string BuildMaritalStatusChangePayload(
        std::string_view initiatorName,
        std::string_view acceptorName,
        std::string_view newStatus,
        bool skipTrigger) {
        const std::string lowerStatus = Foundation::StringUtils::ToLowerStd(newStatus);
        std::string msg = std::format("{} and {}", initiatorName, acceptorName);
        if (lowerStatus == "lover") {
            msg += " become long term lovers.";
        } else if (lowerStatus == "dating") {
            msg += " become dating couple.";
        } else {
            msg += std::format(" just got {}.", newStatus);
        }

        nlohmann::json j = MakeBaseEventJson("ttm_marital_status_change", msg, skipTrigger);
        SetNonEmpty(j, "newStatus", newStatus);
        return SafeDump(j);
    }

    std::string BuildOpenRelationshipChangePayload(
        std::string_view initiatorName,
        std::string_view partnerName,
        bool isOpen,
        bool skipTrigger) {
        const std::string msg = isOpen
            ? std::format("{} and {} opened their relationship to allow secondary romantic or casual partners.",
                          initiatorName, partnerName)
            : std::format("{} and {} closed their open relationship, returning to exclusive monogamy.",
                          initiatorName, partnerName);

        nlohmann::json j = MakeBaseEventJson("ttm_open_relationship_change", msg, skipTrigger);
        SetNonEmpty(j, "parentA", initiatorName);
        SetNonEmpty(j, "parentB", partnerName);
        j["isOpen"] = isOpen;
        return SafeDump(j);
    }

    std::string BuildSpouseHierarchyChangePayload(
        std::string_view playerName,
        std::string_view spouseName,
        bool isDemoted,
        bool skipTrigger) {
        const std::string_view keyword = isDemoted ? "demoted" : "promoted";
        const std::string msg = std::format(
            "{} has {} {} in the household hierarchy.",
            playerName, keyword, spouseName);

        nlohmann::json j = MakeBaseEventJson("ttm_spouse_hierarchy_change", msg, skipTrigger);
        j["isDemoted"] = isDemoted;
        return SafeDump(j);
    }

    std::string BuildSpouseAffectionChangePayload(
        std::string_view spouseName,
        std::string_view playerName,
        std::string_view affectionLevel,
        std::string_view direction,
        bool skipTrigger) {
        std::string msg = std::format(
            "{}'s affection with {} has {} and ",
            spouseName, playerName, direction);
        if (affectionLevel == "estranged") {
            msg += " their relationship is on the verge of being broken.";
        } else if (affectionLevel == "troubled") {
            msg += " their relationship is in bad shape.";
        } else if (affectionLevel == "content") {
            msg += " their relationship is in a good shape.";
        } else if (affectionLevel == "happy") {
            msg += " they can't wish for a better relationship.";
        }

        nlohmann::json j = MakeBaseEventJson("ttm_spouse_affection_change", msg, skipTrigger);
        SetNonEmpty(j, "affectionLevel", affectionLevel);
        SetNonEmpty(j, "affectionDirection", direction);
        return SafeDump(j);
    }

    std::string BuildProposalDecisionPayload(
        std::string_view initiatorName,
        std::string_view acceptorName,
        std::string_view status,
        std::string_view decision,
        bool skipTrigger) {
        std::string msg = std::format("{} proposed ", initiatorName);
        if (status == "dating") {
            msg += "a dating relationship";
        } else if (status == "engaged") {
            msg += "an engagement relationship";
        } else if (status == "lover") {
            msg += "a lover relationship";
        }
        msg += std::format(" to {}.", acceptorName);

        if (decision == "accept") {
            msg += std::format(" {} accepted the proposal.", acceptorName);
        } else if (decision == "reject") {
            msg += std::format(" {} rejected the proposal. ", acceptorName);
        }

        return SafeDump(MakeBaseEventJson("ttm_dating_proposal", msg, skipTrigger));
    }

    std::string BuildActionDeclinePayload(
        std::string_view speakerName,
        std::uint32_t initiatorFormId,
        std::uint32_t targetFormId,
        std::string_view declinedAction,
        std::string_view msg,
        bool skipTrigger) {
        nlohmann::json j = MakeBaseEventJson("ttm_action_decline", msg, skipTrigger);
        j["speaker"] = speakerName;
        SetNonEmpty(j, "declinedAction", declinedAction);
        if (initiatorFormId != 0) {
            j["initiator"] = std::format("{:08X}", initiatorFormId);
        }
        if (targetFormId != 0) {
            j["target"] = std::format("{:08X}", targetFormId);
        }
        return SafeDump(j);
    }

    std::string BuildChildAdoptedPayload(
        std::string_view childName,
        std::string_view parentAName,
        std::string_view parentBName,
        bool hasParentB,
        bool skipTrigger) {
        const std::string msg = hasParentB
            ? std::format("{} and {} have adopted {}. {} is now part of their family.",
                          parentAName, parentBName, childName, childName)
            : std::format("{} has adopted {}. {} is now part of {}'s family.",
                          parentAName, childName, childName, parentAName);

        nlohmann::json j = MakeBaseEventJson("ttm_child_adopted", msg, skipTrigger);
        SetNonEmpty(j, "child", childName);
        SetNonEmpty(j, "parentA", parentAName);
        SetNonEmpty(j, "parentB", parentBName);
        return SafeDump(j);
    }

    std::string BuildChildInheritedPayload(
        std::string_view childName,
        std::string_view deceasedName,
        std::string_view newParentName,
        std::string_view coParentName,
        bool hasDistinctCoParent,
        bool skipTrigger) {
        const std::string msg = hasDistinctCoParent
            ? std::format("Following the death of {}, {} has taken custody of {} and inherited parental responsibilities alongside {}.",
                          deceasedName, newParentName, childName, coParentName)
            : std::format("Following the death of {}, {} has become the sole guardian of {}.",
                          deceasedName, newParentName, childName);

        nlohmann::json j = MakeBaseEventJson("ttm_child_inherited", msg, skipTrigger);
        SetNonEmpty(j, "child", childName);
        SetNonEmpty(j, "parentA", newParentName);
        SetNonEmpty(j, "parentB", coParentName);
        j["reason"] = "spouse_death";
        return SafeDump(j);
    }

    std::string BuildChildParentsChangedPayload(
        std::string_view childName,
        std::string_view parentAName,
        std::string_view parentBName,
        bool hasParentB,
        std::string_view reason,
        bool skipTrigger) {
        std::string msg;
        if (reason == "marriage") {
            msg = std::format(
                "With {} and {} getting married, {} has become a parent to {}.",
                parentAName, parentBName, parentBName, childName);
        } else if (reason == "divorce") {
            msg = std::format(
                "Following the divorce, {} is now in the sole custody of {}.",
                childName, parentAName);
        } else {
            msg = std::format("The parental custody of {} has been updated to {}", childName, parentAName);
            if (hasParentB) {
                msg += std::format(" and {}.", parentBName);
            } else {
                msg += " as sole guardian.";
            }
        }

        nlohmann::json j = MakeBaseEventJson("ttm_child_parents_changed", msg, skipTrigger);
        SetNonEmpty(j, "child", childName);
        SetNonEmpty(j, "parentA", parentAName);
        SetNonEmpty(j, "parentB", parentBName);
        SetNonEmpty(j, "reason", reason);
        return SafeDump(j);
    }

    std::string BuildPregnancyAnnouncedPayload(
        std::shared_ptr<const State::WorldStateSnapshot> worldSnap,
        std::shared_ptr<const State::SpatialActorSnapshot> spatialSnap,
        std::uint32_t motherID,
        std::uint32_t partnerID,
        std::uint32_t fallbackBioID,
        std::string_view fallbackBioName,
        std::int32_t childCount,
        bool skipTrigger) {
        if (!worldSnap || !spatialSnap || motherID == 0) {
            return {};
        }

        std::string motherName = ResolveActorNameSafe(motherID, *worldSnap, *spatialSnap);
        if (motherName.empty()) {
            motherName = ActorSvc::GetName(motherID);
        }
        if (motherName.empty()) {
            motherName = "Mother";
        }

        if (partnerID == 0 && worldSnap->relationships) {
            partnerID = worldSnap->relationships->GetPrimaryPartner(State::ActorId{ motherID }).raw();
        }
        std::string partnerName{};
        if (partnerID != 0) {
            partnerName = ResolveActorNameSafe(partnerID, *worldSnap, *spatialSnap);
            if (partnerName.empty()) {
                partnerName = ActorSvc::GetName(partnerID);
            }
        }

        std::string bioNameFallback(fallbackBioName);
        if (bioNameFallback.empty() && fallbackBioID != 0 &&
            fallbackBioID != Foundation::kUnknownFatherRawId) {
            bioNameFallback = ResolveActorNameSafe(fallbackBioID, *worldSnap, *spatialSnap);
            if (bioNameFallback.empty()) {
                bioNameFallback = ActorSvc::GetName(fallbackBioID);
            }
        }

        const auto omniscient = Projection::PerspectiveView::ForDashboard(
            worldSnap, spatialSnap, /*revealSecretsOverride=*/true, &GetEngineWerewolfStateProvider());
        const auto motherView = Projection::PerspectiveView::ForActor(
            worldSnap, spatialSnap, State::ActorId{ motherID }, &GetEngineWerewolfStateProvider());
        const auto partnerView =
            (partnerID != 0)
                ? std::optional<Projection::PerspectiveView>(Projection::PerspectiveView::ForActor(
                      worldSnap, spatialSnap, State::ActorId{ partnerID }, &GetEngineWerewolfStateProvider()))
                : std::nullopt;

        std::vector<State::ChildId> unbornIDs{};
        for (const auto& entry : omniscient.GetVisibleChildren()) {
            if (entry.stage == State::ChildStage::Unborn && entry.bioMother.raw() == motherID) {
                unbornIDs.push_back(entry.childId);
            }
        }
        std::sort(unbornIDs.begin(), unbornIDs.end(), [](const auto a, const auto b) {
            return a.raw() < b.raw();
        });
        if (static_cast<std::int32_t>(unbornIDs.size()) > childCount) {
            childCount = static_cast<std::int32_t>(unbornIDs.size());
        }

        struct FatherGroup {
            std::uint32_t bioFatherID{ 0 };
            std::string name{};
            int count{ 0 };
            bool isPartner{ false };
            bool isSecret{ false };
        };
        std::vector<FatherGroup> groups{};

        for (const auto childKey : unbornIDs) {
            const auto perceivedMotherOpt = motherView.GetPerceivedChild(childKey);
            if (!perceivedMotherOpt.has_value()) {
                continue;
            }
            const auto& perceived = *perceivedMotherOpt;

            const std::uint32_t bID = perceived.perceivedBioFather.raw();
            std::string fName{};
            if (bID != 0 && bID != Foundation::kUnknownFatherRawId) {
                fName = ResolveActorNameSafe(bID, *worldSnap, *spatialSnap);
            }
            if (fName.empty()) {
                fName = perceived.perceivedBioFatherFallbackName;
            }
            if (fName.empty()) {
                fName = !bioNameFallback.empty() ? bioNameFallback : "an unknown father";
            }

            const bool isPart = (bID != 0 && partnerID != 0 && bID == partnerID) ||
                                (!partnerName.empty() && fName == partnerName);
            bool isSec = false;
            if (partnerView.has_value()) {
                isSec = !partnerView->CanViewerKnowTrueBioFather(childKey) ||
                        !partnerView->CanViewerKnowPregnancy(childKey);
            }

            auto it = std::find_if(groups.begin(), groups.end(), [&](const FatherGroup& g) {
                if (bID != 0 && bID != Foundation::kUnknownFatherRawId && g.bioFatherID != 0) {
                    return g.bioFatherID == bID;
                }
                return g.name == fName;
            });

            if (it != groups.end()) {
                it->count++;
                if (isSec) {
                    it->isSecret = true;
                }
            } else {
                FatherGroup fg;
                fg.bioFatherID = bID;
                fg.name = std::move(fName);
                fg.count = 1;
                fg.isPartner = isPart;
                fg.isSecret = isSec;
                groups.push_back(std::move(fg));
            }
        }

        if (groups.empty()) {
            std::string fallbackName = !bioNameFallback.empty() ? bioNameFallback : partnerName;
            if (fallbackName.empty()) {
                fallbackName = "an unknown father";
            }
            FatherGroup fg;
            fg.bioFatherID = fallbackBioID;
            fg.name = std::move(fallbackName);
            fg.count = childCount > 0 ? childCount : 1;
            fg.isPartner = (!partnerName.empty() && fg.name == partnerName);
            fg.isSecret = (partnerID != 0 && !fg.isPartner);
            groups.push_back(std::move(fg));
        }

        int totalChildren = 0;
        for (const auto& g : groups) {
            totalChildren += g.count;
        }
        if (totalChildren > childCount) {
            childCount = totalChildren;
        } else if (childCount > totalChildren && !groups.empty()) {
            groups.front().count += (childCount - totalChildren);
        }

        std::string message{};
        std::string bioFatherJson{};

        if (groups.size() > 1) {
            message = motherName + " is surprised to discover that she is expecting " +
                      std::to_string(childCount) + " children: ";

            bool hasPrevious = false;
            int partnerIdx = -1;

            for (std::size_t p = 0; p < groups.size(); ++p) {
                if (groups[p].isPartner) {
                    partnerIdx = static_cast<int>(p);
                    const int pNum = groups[p].count;
                    if (pNum == 1) {
                        message += "1 child is from " + partnerName + " (her current partner)";
                    } else {
                        message += std::to_string(pNum) + " children are from " +
                                   partnerName + " (her current partner)";
                    }
                    hasPrevious = true;
                    break;
                }
            }

            int secretOthers = 0;
            for (std::size_t o = 0; o < groups.size(); ++o) {
                if (static_cast<int>(o) != partnerIdx) {
                    if (hasPrevious) {
                        message += ", ";
                    }
                    const int oNum = groups[o].count;
                    const std::string& oName = groups[o].name;
                    if (oNum == 1) {
                        message += "1 child from " + oName;
                    } else {
                        message += std::to_string(oNum) + " children from " + oName;
                    }
                    hasPrevious = true;

                    if (groups[o].isSecret) {
                        secretOthers++;
                    }
                    if (!bioFatherJson.empty()) {
                        bioFatherJson += ", ";
                    }
                    bioFatherJson += oName;
                }
            }

            if (!partnerName.empty() && secretOthers > 0) {
                message += ", while her current partner " + partnerName +
                           " does not know she is pregnant yet and neither " +
                           partnerName +
                           " nor the biological father knows unless she chooses to tell them right now.";
            } else if (!partnerName.empty() && partnerIdx < 0) {
                message += ", though " + partnerName + " is her current partner.";
            } else {
                message += ".";
            }
        } else {
            if (childCount > 1) {
                message = motherName + " is surprised to discover that she is expecting " +
                          std::to_string(childCount) + " children";
            } else {
                message = motherName + " is surprised to discover that she is expecting a child";
            }

            const auto& single = groups.front();
            const std::string& singleName = single.name;
            const bool isPart = single.isPartner;
            const bool isSec = single.isSecret;
            bioFatherJson = singleName;

            if (!partnerName.empty() && isPart) {
                message += " with " + partnerName + ".";
            } else if (!partnerName.empty() && !singleName.empty() &&
                       singleName != "an unknown father" && singleName != "Unknown") {
                if (isSec) {
                    message += " fathered by " + singleName + " (while " +
                               partnerName + " is her current partner). Neither " +
                               partnerName + " nor " + singleName +
                               " knows that she is pregnant yet unless she chooses to tell them right now.";
                } else {
                    message += " fathered by " + singleName + ", though " +
                               partnerName + " is her partner.";
                }
            } else if (!partnerName.empty()) {
                if (isSec) {
                    message += " (while " + partnerName +
                               " is her current partner). " + partnerName +
                               " does not know that she is pregnant yet unless she chooses to tell him right now.";
                } else {
                    message += " with " + partnerName + ".";
                }
            } else if (!singleName.empty() &&
                       singleName != "an unknown father" && singleName != "Unknown") {
                message += ", knowing the biological father is " + singleName + ".";
            } else {
                message += ".";
            }
        }

        if (message.empty()) {
            return {};
        }

        nlohmann::json j = MakeBaseEventJson("ttm_pregnancy_announced", message, skipTrigger);
        SetNonEmpty(j, "mother", motherName);
        SetNonEmpty(j, "partner", partnerName);
        SetNonEmpty(j, "bioFather", bioFatherJson);
        return SafeDump(j);
    }

    std::string BuildPregnancyLostPayload(
        std::string_view motherName,
        std::string_view partnerName,
        std::string_view reason,
        bool skipTrigger) {
        const std::string msg = std::format(
            "{} has suffered a tragic pregnancy loss ({}).",
            motherName, reason);

        nlohmann::json j = MakeBaseEventJson("ttm_pregnancy_lost", msg, skipTrigger);
        SetNonEmpty(j, "parentA", motherName);
        SetNonEmpty(j, "parentB", partnerName);
        SetNonEmpty(j, "reason", reason);
        return SafeDump(j);
    }

    std::string BuildLaborStartedPayload(
        std::string_view motherName,
        std::string_view partnerName,
        std::int32_t childCount,
        bool skipTrigger) {
        std::string msg = std::format("{} has gone into labor and the delivery has begun!", motherName);
        if (childCount > 1) {
            msg += std::format(
                " She is carrying {} children and will deliver them one after another in this same labor.",
                childCount);
        }
        if (!partnerName.empty()) {
            msg += std::format(" {} is at her side supporting her.", partnerName);
        }

        nlohmann::json j = MakeBaseEventJson("ttm_labor_started", msg, skipTrigger);
        SetNonEmpty(j, "mother", motherName);
        SetNonEmpty(j, "partner", partnerName);
        return SafeDump(j);
    }

    std::string BuildLaborProgressPayload(
        std::string_view motherName,
        std::int32_t bornCount,
        std::int32_t totalCount) {
        const std::int32_t remaining = (totalCount > bornCount) ? (totalCount - bornCount) : 0;
        const std::string msg = std::format(
            "{}'s labor continues: {} out of {} children are born, {} to go.",
            motherName, bornCount, totalCount, remaining);

        nlohmann::json j = MakeBaseEventJson("ttm_labor_progress", msg, /*skipTrigger=*/true);
        SetNonEmpty(j, "mother", motherName);
        return SafeDump(j);
    }

    ChildBirthBatchPayloadResult BuildChildBirthBatchPayload(
        std::shared_ptr<const State::WorldStateSnapshot> worldSnap,
        std::shared_ptr<const State::SpatialActorSnapshot> spatialSnap,
        std::uint32_t motherID,
        const std::vector<std::uint32_t>& childIDs,
        bool maskSecrets,
        bool skipTrigger) {
        ChildBirthBatchPayloadResult out{};
        if (!worldSnap || !spatialSnap || motherID == 0) {
            return out;
        }

        std::string motherName = ResolveActorNameSafe(motherID, *worldSnap, *spatialSnap);
        if (motherName.empty()) {
            motherName = ActorSvc::GetName(motherID);
        }
        if (motherName.empty()) {
            motherName = "Mother";
        }

        const auto omniscientView = Projection::PerspectiveView::ForDashboard(
            worldSnap, spatialSnap, /*revealSecretsOverride=*/true, &GetEngineWerewolfStateProvider());
        const auto view = maskSecrets
            ? Projection::PerspectiveView::ForDialogueTarget(
                  worldSnap, spatialSnap, &GetEngineWerewolfStateProvider())
            : omniscientView;

        std::uint32_t partnerID = worldSnap->relationships
            ? worldSnap->relationships->GetPrimaryPartner(State::ActorId{ motherID }).raw()
            : 0;
        if (partnerID == 0) {
            for (const std::uint32_t cID : childIDs) {
                if (const auto pOpt = omniscientView.GetPerceivedChild(State::ChildId{ cID })) {
                    const std::uint32_t mpID = pOpt->motherPartner.raw();
                    if (mpID != 0 && !Utils::MatchesActor(mpID, motherID)) {
                        partnerID = mpID;
                        break;
                    }
                }
            }
        }
        std::string partnerName{};
        if (partnerID != 0) {
            partnerName = ResolveActorNameSafe(partnerID, *worldSnap, *spatialSnap);
            if (partnerName.empty()) {
                partnerName = ActorSvc::GetName(partnerID);
            }
        }

        std::vector<std::uint32_t> targetIDs = childIDs;
        if (targetIDs.empty()) {
            for (const auto& perceivedChild : view.GetVisibleChildren()) {
                if ((Utils::MatchesActor(perceivedChild.bioMother.raw(), motherID) ||
                     perceivedChild.guardians.HasGuardian(State::ActorId{ motherID })) &&
                    (perceivedChild.stage == State::ChildStage::Infant ||
                     perceivedChild.stage == State::ChildStage::Child)) {
                    targetIDs.push_back(perceivedChild.childId.raw());
                }
            }
            std::sort(targetIDs.begin(), targetIDs.end());
        }
        if (targetIDs.empty()) {
            return out;
        }
        out.resolvedChildIds = targetIDs;

        std::vector<BirthFatherGroup> groups{};
        nlohmann::json childrenArr = nlohmann::json::array();

        for (const std::uint32_t cID : targetIDs) {
            const State::ChildId childKey{ cID };
            const auto perceivedOpt = view.GetPerceivedChild(childKey);
            if (!perceivedOpt.has_value()) {
                continue;
            }
            const auto& perceived = *perceivedOpt;

            BirthChildItem item{};
            item.childID = cID;
            item.name = (perceived.name.empty() ||
                         Foundation::StringUtils::CaseInsensitiveEquals(perceived.name, "Newborn"))
                            ? ""
                            : perceived.name;
            item.gender = perceived.gender;
            const auto desc = DescribeChildGender(item.gender);
            item.relation = desc.relation;

            const std::uint32_t bID = maskSecrets
                ? perceived.publicBioFather.raw()
                : perceived.perceivedBioFather.raw();
            std::string fName{};
            if (bID != 0 && bID != Foundation::kUnknownFatherRawId) {
                fName = ResolveActorNameSafe(bID, *worldSnap, *spatialSnap);
            }
            if (fName.empty() && !maskSecrets) {
                fName = perceived.perceivedBioFatherFallbackName;
            }
            if (fName.empty()) {
                fName = "an unknown father";
            }

            const bool isPart = (bID != 0 && partnerID != 0 && bID == partnerID) ||
                                (!partnerName.empty() && fName == partnerName);
            bool isSecPartner = false;
            bool isSecBio = false;
            if (!maskSecrets) {
                if (partnerID != 0 && !isPart) {
                    const auto partnerView = Projection::PerspectiveView::ForActor(
                        worldSnap, spatialSnap, State::ActorId{ partnerID }, &GetEngineWerewolfStateProvider());
                    isSecPartner = !partnerView.CanViewerKnowTrueBioFather(childKey);
                }
                if (bID != 0 && bID != Foundation::kUnknownFatherRawId && !isPart) {
                    const auto bioView = Projection::PerspectiveView::ForActor(
                        worldSnap, spatialSnap, State::ActorId{ bID }, &GetEngineWerewolfStateProvider());
                    isSecBio = !bioView.CanViewerKnowTrueBioFather(childKey);
                }
            }
            item.isSecretForPartner = isSecPartner;
            item.isSecretForBio = isSecBio;

            nlohmann::json cJson;
            cJson["name"] = item.name.empty() ? "child" : item.name;
            cJson["gender"] = desc.gender;
            cJson["relation"] = item.relation;
            cJson["bioFather"] = fName;
            cJson["isSecret"] = isSecPartner;
            childrenArr.push_back(std::move(cJson));

            auto it = std::find_if(groups.begin(), groups.end(), [&](const BirthFatherGroup& g) {
                if (bID != 0 && bID != Foundation::kUnknownFatherRawId && g.bioFatherID != 0) {
                    return g.bioFatherID == bID;
                }
                return g.name == fName;
            });

            if (it != groups.end()) {
                it->children.push_back(std::move(item));
            } else {
                BirthFatherGroup fg;
                fg.bioFatherID = bID;
                fg.name = std::move(fName);
                fg.isPartner = isPart;
                fg.children.push_back(std::move(item));
                groups.push_back(std::move(fg));
            }
        }

        if (groups.empty()) {
            return out;
        }

        int totalChildren = 0;
        for (const auto& g : groups) {
            totalChildren += static_cast<int>(g.children.size());
        }

        std::string message{};
        if (groups.size() == 1) {
            const auto& fg = groups.front();
            const std::string childPhrase = FormatChildrenGroup(fg.children);

            if (totalChildren == 1) {
                const auto& c = fg.children.front();
                if (fg.isPartner) {
                    message = motherName + " gave birth to a healthy " + c.relation +
                              (c.name.empty() ? "" : ", " + c.name + ",") + " fathered by " + fg.name + ".";
                } else if (!partnerName.empty()) {
                    message = motherName + " gave birth to " +
                              (c.name.empty() ? ("a newborn " + c.relation) : c.name) + ". ";
                    if (c.isSecretForPartner) {
                        message += partnerName + " believes the newborn " + c.relation +
                                   " is his, unaware that the true biological father is " + fg.name + ".";
                    } else {
                        message += "The " + c.relation + " was fathered by " + fg.name +
                                   ", though " + partnerName + " is her partner.";
                    }
                    if (c.isSecretForBio) {
                        message += " Furthermore, " + fg.name +
                                   " has not been informed that he fathered the child.";
                    }
                } else {
                    if (c.isSecretForBio) {
                        message = motherName + " gave birth to " +
                                  (c.name.empty() ? ("a " + c.relation) : (c.name + ", a " + c.relation)) +
                                  " fathered by " + fg.name + ", though " + fg.name +
                                  " is unaware of the child.";
                    } else {
                        message = motherName + " gave birth to a healthy " + c.relation +
                                  (c.name.empty() ? "" : ", " + c.name + ",") + " fathered by " + fg.name + ".";
                    }
                }
            } else {
                bool hasSecretForPartner = false;
                bool hasSecretForBio = false;
                for (const auto& c : fg.children) {
                    if (c.isSecretForPartner) hasSecretForPartner = true;
                    if (c.isSecretForBio) hasSecretForBio = true;
                }

                if (fg.isPartner) {
                    message = motherName + " gave birth to " + childPhrase + ", fathered by " + fg.name + ".";
                } else if (!partnerName.empty()) {
                    message = motherName + " gave birth to " + childPhrase + ". ";
                    if (hasSecretForPartner) {
                        message += partnerName +
                                   " believes the newborn children are his, unaware that the true biological father is " +
                                   fg.name + ".";
                    } else {
                        message += "The children were fathered by " + fg.name + ", though " +
                                   partnerName + " is her partner.";
                    }
                    if (hasSecretForBio) {
                        message += " Furthermore, " + fg.name +
                                   " has not been informed that he fathered the children.";
                    }
                } else {
                    if (hasSecretForBio) {
                        message = motherName + " gave birth to " + childPhrase +
                                  ", fathered by " + fg.name + ", though " + fg.name +
                                  " is unaware of the children.";
                    } else {
                        message = motherName + " gave birth to " + childPhrase +
                                  ", fathered by " + fg.name + ".";
                    }
                }
            }
        } else {
            const std::string countWord =
                (totalChildren == 2 ? "twins: " : (std::to_string(totalChildren) + " children: "));
            message = motherName + " gave birth to " + countWord;

            int partnerGroupIdx = -1;
            for (std::size_t i = 0; i < groups.size(); ++i) {
                if (groups[i].isPartner) {
                    partnerGroupIdx = static_cast<int>(i);
                    break;
                }
            }

            if (partnerGroupIdx >= 0) {
                message += FormatChildrenGroup(groups[partnerGroupIdx].children) +
                           ", fathered by " + groups[partnerGroupIdx].name;
                if (!partnerName.empty()) {
                    message += " (her partner)";
                }
            }

            bool firstOther = (partnerGroupIdx < 0);
            for (std::size_t i = 0; i < groups.size(); ++i) {
                if (static_cast<int>(i) == partnerGroupIdx) {
                    continue;
                }
                if (!firstOther) {
                    message += "; and ";
                }
                message += FormatChildrenGroup(groups[i].children) + ", fathered by " + groups[i].name;
                firstOther = false;
            }
            message += ".";

            if (partnerID != 0 && partnerGroupIdx >= 0) {
                std::vector<std::string> secretOtherFathers{};
                std::string secretChildDesc{};
                bool anySecretForBio = false;

                for (std::size_t i = 0; i < groups.size(); ++i) {
                    if (static_cast<int>(i) == partnerGroupIdx) {
                        continue;
                    }
                    bool sec = false;
                    for (const auto& c : groups[i].children) {
                        if (c.isSecretForPartner) sec = true;
                        if (c.isSecretForBio) anySecretForBio = true;
                    }
                    if (sec) {
                        secretOtherFathers.push_back(groups[i].name);
                        if (groups[i].children.size() == 1) {
                            secretChildDesc = "the newborn " + groups[i].children[0].relation;
                        } else {
                            secretChildDesc = "the other children";
                        }
                    }
                }

                if (!secretOtherFathers.empty()) {
                    if (secretOtherFathers.size() == 1) {
                        message += " " + partnerName + " believes " + secretChildDesc +
                                   " is his, unaware that the true biological father is " +
                                   secretOtherFathers[0] + ".";
                        if (anySecretForBio) {
                            message += " Furthermore, " + secretOtherFathers[0] +
                                       " has not been informed that he fathered the child.";
                        }
                    } else {
                        message += " " + partnerName +
                                   " is unaware of the true biological paternity of the other children.";
                    }
                }
            } else if (partnerID != 0 && partnerGroupIdx < 0) {
                bool anySecretForPartner = false;
                for (const auto& g : groups) {
                    for (const auto& c : g.children) {
                        if (c.isSecretForPartner) {
                            anySecretForPartner = true;
                        }
                    }
                }
                if (anySecretForPartner) {
                    message += " " + partnerName +
                               " (her partner) is completely unaware of the true biological paternity.";
                } else {
                    message += " though " + partnerName + " is her partner.";
                }
            }
        }

        if (message.empty()) {
            return out;
        }

        nlohmann::json j = MakeBaseEventJson("ttm_child_birth", message, skipTrigger);
        SetNonEmpty(j, "mother", motherName);
        SetNonEmpty(j, "partner", partnerName);
        j["children"] = std::move(childrenArr);
        out.json = SafeDump(j);
        return out;
    }

    std::string BuildChildBirthSingleMotherlessPayload(
        const Projection::ResolvedChildSecrecy& sec,
        std::string_view childName,
        std::uint8_t childGender,
        bool hasGivenName,
        std::string_view motherName,
        std::string_view presumedFatherName,
        std::string_view bioFatherName,
        Projection::DispatchMaskMode maskMode,
        bool skipTrigger) {
        const auto desc = DescribeChildGender(childGender);
        const std::string nameClause = hasGivenName ? std::format(", {},", childName) : "";

        if (maskMode == Projection::DispatchMaskMode::MaskForPartner) {
            const std::string maskedMsg = std::format(
                "{} gave birth to a healthy {}{} fathered by {}.",
                motherName, desc.relation, nameClause, presumedFatherName);
            nlohmann::json j = MakeBaseEventJson("ttm_child_birth", maskedMsg, skipTrigger);
            SetNonEmpty(j, "child", childName);
            SetNonEmpty(j, "parentA", motherName);
            SetNonEmpty(j, "parentB", presumedFatherName);
            SetNonEmpty(j, "bioFather", presumedFatherName);
            SetNonEmpty(j, "presumedFather", presumedFatherName);
            SetNonEmpty(j, "gender", desc.gender);
            SetNonEmpty(j, "relation", desc.relation);
            return SafeDump(j);
        }

        const std::string birthTarget =
            hasGivenName ? std::string(childName) : ("a newborn " + desc.relation);
        std::string msg{};

        if (sec.isSecretForAssumed && sec.isSecretForBio && !sec.presumedFather.IsNull()) {
            msg = std::format(
                "{} gave birth to {}. {} believes the newborn {} is his, unaware that the true biological father is {}. Furthermore, {} has not been informed that he fathered the child.",
                motherName, birthTarget, presumedFatherName, desc.relation, bioFatherName, bioFatherName);
        } else if (sec.isSecretForAssumed && !sec.presumedFather.IsNull()) {
            msg = std::format(
                "{} gave birth to {}. {} believes the newborn {} is his, unaware that the true biological father is {}.",
                motherName, birthTarget, presumedFatherName, desc.relation, bioFatherName);
        } else if (sec.isSecretForBio && !bioFatherName.empty()) {
            if (hasGivenName) {
                msg = std::format(
                    "{} gave birth to {}, a {} fathered by {}, though {} is unaware of the child.",
                    motherName, childName, desc.relation, bioFatherName, bioFatherName);
            } else {
                msg = std::format(
                    "{} gave birth to a {} fathered by {}, though {} is unaware of the child.",
                    motherName, desc.relation, bioFatherName, bioFatherName);
            }
        } else if (!bioFatherName.empty()) {
            msg = std::format(
                "{} gave birth to a healthy {}{} fathered by {}.",
                motherName, desc.relation, nameClause, bioFatherName);
        } else {
            if (hasGivenName) {
                msg = std::format(
                    "{} gave birth to a newborn {}, {}.",
                    motherName, desc.relation, childName);
            } else {
                msg = std::format(
                    "{} gave birth to a newborn {}.",
                    motherName, desc.relation);
            }
        }

        nlohmann::json j = MakeBaseEventJson("ttm_child_birth", msg, skipTrigger);
        SetNonEmpty(j, "child", childName);
        SetNonEmpty(j, "parentA", motherName);
        SetNonEmpty(j, "parentB", presumedFatherName);
        SetNonEmpty(j, "bioFather", bioFatherName);
        SetNonEmpty(j, "presumedFather", presumedFatherName);
        SetNonEmpty(j, "gender", desc.gender);
        SetNonEmpty(j, "relation", desc.relation);
        return SafeDump(j);
    }

    std::string BuildChildGrewUpPayload(
        const Projection::ResolvedChildSecrecy& sec,
        std::string_view childName,
        std::uint8_t childGender,
        std::string_view motherName,
        std::string_view presumedFatherName,
        std::string_view bioFatherName,
        Projection::DispatchMaskMode maskMode,
        bool skipTrigger) {
        const auto desc = DescribeChildGender(childGender);

        std::string msg{};
        std::string_view bioField = bioFatherName;

        if (maskMode == Projection::DispatchMaskMode::MaskForPartner) {
            bioField = presumedFatherName;
            if (!motherName.empty() && !presumedFatherName.empty()) {
                msg = std::format(
                    "{} and {}'s {}, {}, has grown from infancy into a young child.",
                    motherName, presumedFatherName, desc.relation, childName);
            } else {
                msg = std::format(
                    "{} has grown from infancy into a young {}.",
                    childName, desc.relation);
            }
        } else if (maskMode == Projection::DispatchMaskMode::MaskForChildOnly) {
            bioField = presumedFatherName;
            if (!motherName.empty() && !presumedFatherName.empty()) {
                msg = std::format(
                    "{} and {}'s {}, {}, has grown from infancy into a young child.",
                    motherName, presumedFatherName, desc.relation, childName);
            } else if (!motherName.empty()) {
                msg = std::format(
                    "{}'s {}, {}, has grown from infancy into a young child.",
                    motherName, desc.relation, childName);
            } else if (!presumedFatherName.empty()) {
                msg = std::format(
                    "{}'s {}, {}, has grown from infancy into a young child.",
                    presumedFatherName, desc.relation, childName);
            } else {
                msg = std::format(
                    "{} has grown from infancy into a young {}.",
                    childName, desc.relation);
            }
        } else {
            if (sec.isSecretForAssumed && sec.isSecretForBio && !sec.presumedFather.IsNull()) {
                msg = std::format(
                    "{} has grown from infancy into a young {}. {} continues to raise {} believing the {} is his, unaware that the true biological father is {}. Furthermore, {} has not been informed that he fathered the child.",
                    childName, desc.relation, presumedFatherName, childName, desc.relation, bioFatherName, bioFatherName);
            } else if (sec.isSecretForAssumed && !sec.presumedFather.IsNull()) {
                msg = std::format(
                    "{} has grown from infancy into a young {}. {} continues to raise {} as his own {}, completely unaware that {} is the true biological father.",
                    childName, desc.relation, presumedFatherName, childName, desc.relation, bioFatherName);
            } else if (sec.isSecretForBio && !bioFatherName.empty()) {
                msg = std::format(
                    "{} has grown from infancy into a young {}, raised by {}, while the biological father {} remains unaware of the {}.",
                    childName, desc.relation, motherName, bioFatherName, desc.relation);
            } else if (!motherName.empty() && !presumedFatherName.empty()) {
                msg = std::format(
                    "{} and {}'s {}, {}, has grown from infancy into a young child.",
                    motherName, presumedFatherName, desc.relation, childName);
            } else if (!motherName.empty()) {
                msg = std::format(
                    "{}'s {}, {}, has grown from infancy into a young child.",
                    motherName, desc.relation, childName);
            } else {
                msg = std::format(
                    "{} has grown from infancy into a young {}.",
                    childName, desc.relation);
            }
        }

        nlohmann::json j = MakeBaseEventJson("ttm_child_grew_up", msg, skipTrigger);
        SetNonEmpty(j, "child", childName);
        SetNonEmpty(j, "parentA", motherName);
        SetNonEmpty(j, "parentB", presumedFatherName);
        SetNonEmpty(j, "bioFather", bioField);
        SetNonEmpty(j, "presumedFather", presumedFatherName);
        SetNonEmpty(j, "gender", desc.gender);
        SetNonEmpty(j, "relation", desc.relation);
        return SafeDump(j);
    }

    std::string BuildChildSentToTrainingPayload(
        const Projection::ResolvedChildSecrecy& sec,
        std::string_view childName,
        std::uint8_t childGender,
        std::string_view trainingLocation,
        std::string_view motherName,
        std::string_view presumedFatherName,
        std::string_view bioFatherName,
        Projection::DispatchMaskMode maskMode,
        bool skipTrigger) {
        const auto desc = DescribeChildGender(childGender);
        const std::string locText =
            !trainingLocation.empty() ? std::format(" at {}", trainingLocation) : "";

        std::string msg{};
        std::string_view bioField = bioFatherName;

        if (maskMode == Projection::DispatchMaskMode::MaskForPartner) {
            bioField = presumedFatherName;
            if (!motherName.empty() && !presumedFatherName.empty()) {
                msg = std::format(
                    "{} and {} have sent their {}, {}, away from home for training{}.",
                    motherName, presumedFatherName, desc.relation, childName, locText);
            } else if (!presumedFatherName.empty()) {
                msg = std::format(
                    "{} has sent his {}, {}, away from home for training{}.",
                    presumedFatherName, desc.relation, childName, locText);
            } else {
                msg = std::format(
                    "{} has been sent away from home for training{}.",
                    childName, locText);
            }
        } else {
            if (sec.isSecretForAssumed && sec.isSecretForBio && !sec.presumedFather.IsNull()) {
                msg = std::format(
                    "{} has been sent away from home for training{}. {} continues to support {} {} believing the child is his, unaware that the true biological father is {}. Furthermore, {} has not been informed that he fathered the child.",
                    childName, locText, presumedFatherName, desc.possPronoun, desc.relation, bioFatherName, bioFatherName);
            } else if (sec.isSecretForAssumed && !sec.presumedFather.IsNull()) {
                msg = std::format(
                    "{} has been sent away from home for training{}. {} continues to support {} {} as his own, completely unaware that {} is the true biological father.",
                    childName, locText, presumedFatherName, desc.possPronoun, desc.relation, bioFatherName);
            } else if (sec.isSecretForBio && !bioFatherName.empty()) {
                msg = std::format(
                    "{} has been sent away from home for training{}, arranged by {}, while the biological father {} remains unaware of his {}.",
                    childName, locText, motherName, bioFatherName, desc.relation);
            } else if (!motherName.empty() && !presumedFatherName.empty()) {
                msg = std::format(
                    "{} and {} have sent their {}, {}, away from home for training{}.",
                    motherName, presumedFatherName, desc.relation, childName, locText);
            } else if (!motherName.empty()) {
                msg = std::format(
                    "{} has sent her {}, {}, away from home for training{}.",
                    motherName, desc.relation, childName, locText);
            } else if (!presumedFatherName.empty()) {
                msg = std::format(
                    "{} has sent his {}, {}, away from home for training{}.",
                    presumedFatherName, desc.relation, childName, locText);
            } else {
                msg = std::format(
                    "{} has been sent away from home for training{}.",
                    childName, locText);
            }
        }

        nlohmann::json j = MakeBaseEventJson("ttm_child_sent_to_training", msg, skipTrigger);
        SetNonEmpty(j, "child", childName);
        SetNonEmpty(j, "parentA", motherName);
        SetNonEmpty(j, "parentB", presumedFatherName);
        SetNonEmpty(j, "bioFather", bioField);
        SetNonEmpty(j, "presumedFather", presumedFatherName);
        SetNonEmpty(j, "trainingLocation", trainingLocation);
        SetNonEmpty(j, "gender", desc.gender);
        SetNonEmpty(j, "relation", desc.relation);
        return SafeDump(j);
    }

    std::string BuildChildBecameAdultPayload(
        const Projection::ResolvedChildSecrecy& sec,
        std::string_view childName,
        std::uint8_t childGender,
        std::string_view motherName,
        std::string_view presumedFatherName,
        std::string_view bioFatherName,
        Projection::DispatchMaskMode maskMode,
        bool skipTrigger) {
        const auto desc = DescribeChildGender(childGender);

        std::string msg{};
        std::string_view bioField = bioFatherName;

        if (maskMode == Projection::DispatchMaskMode::MaskForPartner) {
            bioField = presumedFatherName;
            if (!motherName.empty() && !presumedFatherName.empty()) {
                msg = std::format(
                    "{} and {}'s {}, {}, has completed {} training and come of age as an adult.",
                    motherName, presumedFatherName, desc.relation, childName, desc.possPronoun);
            } else if (!presumedFatherName.empty()) {
                msg = std::format(
                    "{}'s {}, {}, has completed {} training and come of age as an adult.",
                    presumedFatherName, desc.relation, childName, desc.possPronoun);
            } else {
                msg = std::format(
                    "{} has completed {} training and come of age as an adult.",
                    childName, desc.possPronoun);
            }
        } else if (maskMode == Projection::DispatchMaskMode::MaskForChildOnly) {
            bioField = presumedFatherName;
            if (!motherName.empty() && !presumedFatherName.empty()) {
                msg = std::format(
                    "{} and {}'s {}, {}, has completed {} training and come of age as an adult.",
                    motherName, presumedFatherName, desc.relation, childName, desc.possPronoun);
            } else if (!motherName.empty()) {
                msg = std::format(
                    "{}'s {}, {}, has completed {} training and come of age as an adult.",
                    motherName, desc.relation, childName, desc.possPronoun);
            } else if (!presumedFatherName.empty()) {
                msg = std::format(
                    "{}'s {}, {}, has completed {} training and come of age as an adult.",
                    presumedFatherName, desc.relation, childName, desc.possPronoun);
            } else {
                msg = std::format(
                    "{} has completed {} training and come of age as an adult.",
                    childName, desc.possPronoun);
            }
        } else {
            if (sec.isSecretForAssumed && sec.isSecretForBio && !sec.presumedFather.IsNull()) {
                msg = std::format(
                    "{} has completed {} training and grown into an adult. {} believes {} is his adult {}, unaware that the true biological father is {}. Furthermore, {} has not been informed that he fathered the child.",
                    childName, desc.possPronoun, presumedFatherName, childName, desc.relation, bioFatherName, bioFatherName);
            } else if (sec.isSecretForAssumed && !sec.presumedFather.IsNull()) {
                msg = std::format(
                    "{} has completed {} training and grown into an adult. {} proudly sees {} come of age as his own {}, completely unaware that {} is the true biological father.",
                    childName, desc.possPronoun, presumedFatherName, childName, desc.relation, bioFatherName);
            } else if (sec.isSecretForBio && !bioFatherName.empty()) {
                msg = std::format(
                    "{} has completed {} training and grown into an adult, raised by {}, while the biological father {} remains unaware of his adult {}.",
                    childName, desc.possPronoun, motherName, bioFatherName, desc.relation);
            } else if (!motherName.empty() && !presumedFatherName.empty()) {
                msg = std::format(
                    "{} and {}'s {}, {}, has completed {} training and come of age as an adult.",
                    motherName, presumedFatherName, desc.relation, childName, desc.possPronoun);
            } else if (!motherName.empty()) {
                msg = std::format(
                    "{}'s {}, {}, has completed {} training and come of age as an adult.",
                    motherName, desc.relation, childName, desc.possPronoun);
            } else if (!presumedFatherName.empty()) {
                msg = std::format(
                    "{}'s {}, {}, has completed {} training and come of age as an adult.",
                    presumedFatherName, desc.relation, childName, desc.possPronoun);
            } else {
                msg = std::format(
                    "{} has completed {} training and come of age as an adult.",
                    childName, desc.possPronoun);
            }
        }

        nlohmann::json j = MakeBaseEventJson("ttm_child_became_adult", msg, skipTrigger);
        SetNonEmpty(j, "child", childName);
        SetNonEmpty(j, "parentA", motherName);
        SetNonEmpty(j, "parentB", presumedFatherName);
        SetNonEmpty(j, "bioFather", bioField);
        SetNonEmpty(j, "presumedFather", presumedFatherName);
        SetNonEmpty(j, "gender", desc.gender);
        SetNonEmpty(j, "relation", desc.relation);
        return SafeDump(j);
    }

    std::string BuildPaternitySecretResolvedPayload(
        const Projection::ResolvedChildSecrecy& sec,
        std::string_view canonicalOutcome,
        bool wasPregUnannounced,
        std::string_view rawChildName,
        std::string_view motherName,
        std::string_view rawPresumedFatherName,
        std::string_view rawBioFatherName,
        Projection::DispatchMaskMode maskMode,
        bool skipTrigger) {
        const bool isUnborn = (sec.stage == State::ChildStage::Unborn);
        std::string childName(rawChildName);
        if (isUnborn) {
            childName = "unborn child";
        } else if (childName.empty() || childName == "Newborn") {
            childName = "the child";
        }

        std::string presumedFatherName(rawPresumedFatherName);
        if (sec.presumedFather.IsNull() &&
            (presumedFatherName.empty() || presumedFatherName == motherName)) {
            presumedFatherName = "her former husband";
        }

        std::string bioFatherName(rawBioFatherName);
        if (bioFatherName.empty()) {
            bioFatherName = "another man";
        }

        const bool stillSecretAssumed = sec.isSecretForAssumed;
        const bool stillSecretBio = sec.isSecretForBio;
        const bool stillPregSecretAssumed = sec.isPregnancySecretForAssumed;

        std::string msg{};
        std::string_view bioField = bioFatherName;

        if (maskMode == Projection::DispatchMaskMode::MaskForPartner) {
            bioField = presumedFatherName;
            if (canonicalOutcome == "mother_shared_pregnancy_presumed") {
                msg = std::format(
                    "{} shared the news of her pregnancy with her partner {}, telling him that she is expecting his child.",
                    motherName, presumedFatherName);
            } else if (canonicalOutcome == "pregnancy_visible_presumed") {
                msg = std::format(
                    "{}'s pregnancy has become visibly rounded. Her partner {} now knows she is pregnant and naturally assumes the unborn baby is his.",
                    motherName, presumedFatherName);
            }
        } else {
            if (canonicalOutcome == "mother_confessed_bio") {
                if (isUnborn) {
                    if (stillSecretAssumed) {
                        if (stillPregSecretAssumed) {
                            msg = std::format(
                                "{} confessed to {} that she is pregnant with his child. However, her partner {} does not even know she is pregnant yet.",
                                motherName, bioFatherName, presumedFatherName);
                        } else {
                            msg = std::format(
                                "{} confessed to {} that she is pregnant with his child. However, her partner {} does not know the truth and believes the unborn baby is his.",
                                motherName, bioFatherName, presumedFatherName);
                        }
                    } else if (!sec.presumedFather.IsNull()) {
                        msg = std::format(
                            "{} confessed to {} that she is pregnant with his child. Both {} and {} now know the truth.",
                            motherName, bioFatherName, presumedFatherName, bioFatherName);
                    } else {
                        msg = std::format(
                            "{} confessed to {} that she is pregnant with his child.",
                            motherName, bioFatherName);
                    }
                } else {
                    if (stillSecretAssumed) {
                        msg = std::format(
                            "{} confessed to {} that he is the true biological father of {}. However, {} does not know and still believes the child is his.",
                            motherName, bioFatherName, childName, presumedFatherName);
                    } else {
                        msg = std::format(
                            "{} confessed to {} that he is the true biological father of {}. Both {} and {} now know the truth.",
                            motherName, bioFatherName, childName, presumedFatherName, bioFatherName);
                    }
                }
            } else if (canonicalOutcome == "mother_confessed_presumed") {
                if (isUnborn) {
                    if (wasPregUnannounced) {
                        if (stillSecretBio) {
                            msg = std::format(
                                "{} confessed to her partner {} that she is pregnant, and that the baby was fathered by {}, not him ({} did not previously know she was pregnant). However, {} has not been told yet.",
                                motherName, presumedFatherName, bioFatherName, presumedFatherName, bioFatherName);
                        } else {
                            msg = std::format(
                                "{} confessed to her partner {} that she is pregnant, and that the baby was fathered by {}, not him ({} did not previously know she was pregnant). Both {} and {} now know the truth.",
                                motherName, presumedFatherName, bioFatherName, presumedFatherName, presumedFatherName, bioFatherName);
                        }
                    } else {
                        if (stillSecretBio) {
                            msg = std::format(
                                "{} confessed to {} that the unborn baby she is carrying (which he believed was his) was actually fathered by {}, not him. However, {} does not know that he is the biological father.",
                                motherName, presumedFatherName, bioFatherName, bioFatherName);
                        } else {
                            msg = std::format(
                                "{} confessed to {} that the unborn baby she is carrying (which he believed was his) was actually fathered by {}, not him. Both {} and {} now know the truth.",
                                motherName, presumedFatherName, bioFatherName, presumedFatherName, bioFatherName);
                        }
                    }
                } else {
                    if (stillSecretBio) {
                        msg = std::format(
                            "{} confessed to {} that {} was biologically fathered by {}, not him. However, {} does not know that he is the biological father.",
                            motherName, presumedFatherName, childName, bioFatherName, bioFatherName);
                    } else {
                        msg = std::format(
                            "{} confessed to {} that {} was biologically fathered by {}, not him. Both {} and {} now know the truth.",
                            motherName, presumedFatherName, childName, bioFatherName, presumedFatherName, bioFatherName);
                    }
                }
            } else if (canonicalOutcome == "mother_confessed_both") {
                if (isUnborn) {
                    if (wasPregUnannounced) {
                        msg = std::format(
                            "{} revealed in front of both her partner {} and {} that she is pregnant with {}'s child. {} did not previously know she was pregnant and has just learned both that she is expecting and that {} is the father.",
                            motherName, presumedFatherName, bioFatherName, bioFatherName, presumedFatherName, bioFatherName);
                    } else {
                        msg = std::format(
                            "{} revealed to both her partner {} and {} that the unborn baby she is carrying was fathered by {}, not {}.",
                            motherName, presumedFatherName, bioFatherName, bioFatherName, presumedFatherName);
                    }
                } else {
                    msg = std::format(
                        "{} has revealed the truth of {}'s biological paternity: both {} and {} now know that {} is the true biological father.",
                        motherName, childName, presumedFatherName, bioFatherName, bioFatherName);
                }
            } else if (canonicalOutcome == "bio_father_learnt") {
                if (isUnborn) {
                    if (stillSecretAssumed) {
                        if (stillPregSecretAssumed) {
                            msg = std::format(
                                "{} has learned that {} is pregnant with his child. However, her partner {} does not even know she is pregnant yet.",
                                bioFatherName, motherName, presumedFatherName);
                        } else {
                            msg = std::format(
                                "{} has learned that {} is pregnant with his child. However, her partner {} does not know and still believes the unborn baby is his.",
                                bioFatherName, motherName, presumedFatherName);
                        }
                    } else {
                        msg = std::format(
                            "{} has learned that {} is pregnant with his child. Both {} and {} are now aware of the true biological paternity.",
                            bioFatherName, motherName, presumedFatherName, bioFatherName);
                    }
                } else {
                    if (stillSecretAssumed) {
                        msg = std::format(
                            "{} has learned that he is the true biological father of {} with {}. However, {} does not know and still believes the child is his.",
                            bioFatherName, childName, motherName, presumedFatherName);
                    } else {
                        msg = std::format(
                            "{} has learned that he is the true biological father of {} with {}. Both {} and {} are now aware of the true biological paternity.",
                            bioFatherName, childName, motherName, presumedFatherName, bioFatherName);
                    }
                }
            } else if (canonicalOutcome == "presumed_father_learnt") {
                if (isUnborn) {
                    if (wasPregUnannounced) {
                        if (stillSecretBio) {
                            msg = std::format(
                                "{} has discovered that his partner {} is pregnant with a child fathered by {}, not him ({} did not previously know she was pregnant). However, {} does not know yet.",
                                presumedFatherName, motherName, bioFatherName, presumedFatherName, bioFatherName);
                        } else {
                            msg = std::format(
                                "{} has discovered that his partner {} is pregnant with a child fathered by {}, not him ({} did not previously know she was pregnant). Both {} and {} are now aware of the truth.",
                                presumedFatherName, motherName, bioFatherName, presumedFatherName, presumedFatherName, bioFatherName);
                        }
                    } else {
                        if (stillSecretBio) {
                            msg = std::format(
                                "{} has learned that the unborn baby {} is carrying (which he believed was his) was actually fathered by {}, not him. However, {} does not know that he is the biological father.",
                                presumedFatherName, motherName, bioFatherName, bioFatherName);
                        } else {
                            msg = std::format(
                                "{} has learned that the unborn baby {} is carrying (which he believed was his) was actually fathered by {}, not him. Both {} and {} are now aware of the truth.",
                                presumedFatherName, motherName, bioFatherName, presumedFatherName, bioFatherName);
                        }
                    }
                } else {
                    if (stillSecretBio) {
                        msg = std::format(
                            "{} has learned that {} was biologically fathered by {}, not him. However, {} does not know that he is the biological father.",
                            presumedFatherName, childName, bioFatherName, bioFatherName);
                    } else {
                        msg = std::format(
                            "{} has learned that {} was biologically fathered by {}, not him. Both {} and {} are now aware of the true biological paternity.",
                            presumedFatherName, childName, bioFatherName, presumedFatherName, bioFatherName);
                    }
                }
            } else if (canonicalOutcome == "mother_shared_pregnancy_presumed") {
                if (stillSecretBio) {
                    msg = std::format(
                        "{} shared the news of her pregnancy with her partner {}, letting him believe the unborn baby is his while concealing that {} is the true biological father. {} still does not know about the pregnancy.",
                        motherName, presumedFatherName, bioFatherName, bioFatherName);
                } else {
                    msg = std::format(
                        "{} shared the news of her pregnancy with her partner {}, letting him believe the unborn baby is his while concealing that {} is the true biological father (though {} already knows the truth).",
                        motherName, presumedFatherName, bioFatherName, bioFatherName);
                }
            } else if (canonicalOutcome == "pregnancy_visible_presumed") {
                if (stillSecretBio) {
                    msg = std::format(
                        "{}'s pregnancy has become visibly rounded and can no longer be hidden. Because she never confessed the affair, her partner {} now knows she is pregnant and naturally assumes the unborn baby is his, unaware that {} is the true biological father.",
                        motherName, presumedFatherName, bioFatherName);
                } else {
                    msg = std::format(
                        "{}'s pregnancy has become visibly rounded and can no longer be hidden. Her partner {} now knows she is pregnant and assumes the unborn baby is his, unaware that {} is the true biological father (while {} already knows the truth).",
                        motherName, presumedFatherName, bioFatherName, bioFatherName);
                }
            } else if (canonicalOutcome == "divorce_revealed") {
                msg = std::format(
                    "Following {}'s divorce, the paternity secret of {} has been resolved: {} is recognized as the true biological father.",
                    motherName, childName, bioFatherName);
            } else {
                msg = std::format(
                    "Both {} and {} now know that {} is the true biological father of {}.",
                    presumedFatherName, bioFatherName, bioFatherName, childName);
            }
        }

        nlohmann::json j = MakeBaseEventJson("ttm_paternity_secret_resolved", msg, skipTrigger);
        SetNonEmpty(j, "child", childName);
        SetNonEmpty(j, "parentA", motherName);
        SetNonEmpty(j, "parentB", presumedFatherName);
        SetNonEmpty(j, "bioFather", bioField);
        SetNonEmpty(j, "presumedFather", presumedFatherName);
        SetNonEmpty(j, "paternityOutcome", canonicalOutcome);
        return SafeDump(j);
    }

    std::string BuildAffairConfessedPayload(
        std::string_view cheaterName,
        std::string_view victimName,
        std::string_view partnerName,
        bool skipTrigger) {
        std::string msg = std::format(
            "{} confessed to their partner {} that they have been having an affair",
            cheaterName, victimName);
        if (!partnerName.empty()) {
            msg += std::format(" with {}", partnerName);
        }
        msg += ".";

        nlohmann::json j = MakeBaseEventJson("ttm_affair_confessed", msg, skipTrigger);
        SetNonEmpty(j, "cheater", cheaterName);
        SetNonEmpty(j, "victim", victimName);
        SetNonEmpty(j, "affairPartner", partnerName);
        return SafeDump(j);
    }

    std::string BuildAffairExposedPayload(
        std::string_view partnerName,
        std::string_view victimName,
        std::string_view cheaterName,
        bool skipTrigger) {
        std::string msg = std::format(
            "{} exposed the secret affair they have been having",
            partnerName);
        if (!cheaterName.empty()) {
            msg += std::format(" with {}", cheaterName);
        }
        msg += std::format(" directly to {}.", victimName);

        nlohmann::json j = MakeBaseEventJson("ttm_affair_exposed", msg, skipTrigger);
        SetNonEmpty(j, "cheater", cheaterName);
        SetNonEmpty(j, "victim", victimName);
        SetNonEmpty(j, "affairPartner", partnerName);
        return SafeDump(j);
    }

    std::string BuildAffairResolvedPayload(
        std::string_view initName,
        std::string_view otherName,
        std::string_view partnerName,
        std::string_view resolution,
        bool skipTrigger) {
        std::string msg{};
        if (resolution == "confronted") {
            msg = std::format(
                "{} and {} resolved the affair: the affair is ended and they chose to stay together.",
                initName, otherName);
        } else if (resolution == "allowed") {
            msg = std::format(
                "{} and {} resolved the affair: they agreed to open their relationship and allow the connection.",
                initName, otherName);
        } else if (resolution == "breakup") {
            msg = std::format(
                "{} and {} separated and ended their relationship over the affair.",
                initName, otherName);
        } else {
            msg = std::format(
                "{} and {} resolved their situation ({}).",
                initName, otherName, resolution);
        }

        nlohmann::json j = MakeBaseEventJson("ttm_affair_resolved", msg, skipTrigger);
        SetNonEmpty(j, "cheater", initName);
        SetNonEmpty(j, "victim", otherName);
        SetNonEmpty(j, "affairPartner", partnerName);
        SetNonEmpty(j, "resolution", resolution);
        return SafeDump(j);
    }

    std::string BuildCheatingCaughtPayload(
        std::string_view victimName,
        std::string_view cheaterName,
        std::string_view partnerName,
        bool isWalkIn,
        bool skipTrigger) {
        const std::string msg = isWalkIn
            ? std::format("{} walked in and caught {} in the act with {}!",
                          victimName, cheaterName, partnerName)
            : std::format("{} discovered and caught {} cheating with {}!",
                          victimName, cheaterName, partnerName);

        nlohmann::json j = MakeBaseEventJson("ttm_cheating_caught", msg, skipTrigger);
        SetNonEmpty(j, "cheater", cheaterName);
        SetNonEmpty(j, "victim", victimName);
        SetNonEmpty(j, "affairPartner", partnerName);
        j["isWalkIn"] = isWalkIn;
        return SafeDump(j);
    }

    std::string BuildOpenRelationshipWalkInPayload(
        std::string_view witnessName,
        std::string_view partnerName,
        std::string_view sexPartnerName,
        bool isWalkIn,
        bool skipTrigger) {
        const std::string msg = std::format(
            "{} walked in on {} being intimate with {} under their open relationship arrangement.",
            witnessName, partnerName, sexPartnerName);

        nlohmann::json j = MakeBaseEventJson("ttm_open_relationship_walkin", msg, skipTrigger);
        SetNonEmpty(j, "partner", partnerName);
        SetNonEmpty(j, "victim", witnessName);
        SetNonEmpty(j, "affairPartner", sexPartnerName);
        j["isOpen"] = true;
        j["isWalkIn"] = isWalkIn;
        return SafeDump(j);
    }

}  // namespace MARAS::Bridge::SkyrimNet
