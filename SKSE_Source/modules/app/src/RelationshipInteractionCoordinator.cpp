#include "maras/app/RelationshipInteractionCoordinator.h"

#include "maras/app/AppContext.h"
#include "maras/app/ModalTypes.h"
#include "maras/app/RelationshipUseCases.h"
#include "maras/domain/relationships/MarriageDifficultyRules.h"
#include "maras/domain/relationships/RelationshipSettlementRules.h"
#include "maras/engine/ActorService.h"
#include "maras/engine/EngineLogging.h"
#include "maras/engine/FormCache.h"
#include "maras/engine/PlayerMiscStatsTracker.h"
#include "maras/engine/RelationshipEngineReconciler.h"
#include "maras/engine/TaskDispatcher.h"
#include "maras/foundation/StrongIds.h"
#include "maras/state/EngineSyncPlan.h"
#include "maras/state/SessionEpoch.h"
#include "maras/state/WorldStore.h"

#include <format>
#include <vector>

namespace MARAS::App {

    namespace {
        [[nodiscard]] float GetCurrentGameDays() noexcept {
            if (auto* calendar = RE::Calendar::GetSingleton()) {
                return calendar->GetDaysPassed();
            }
            return 0.0f;
        }

        void ComputeAndApplyDialogueChances(RE::FormID npcFormID, float ttllBonus, bool& outAlwaysSuccess) {
            outAlwaysSuccess = false;
            if (npcFormID == 0 || npcFormID == Foundation::kPlayerRefRawId) {
                return;
            }
            auto* npc = RE::TESForm::LookupByID<RE::Actor>(npcFormID);
            if (!npc || npc->IsDeleted()) {
                return;
            }

            const auto snap = State::WorldStore::GetSingleton().Snapshot();
            outAlwaysSuccess = snap->settings->alwaysSuccessMarriage;
            if (outAlwaysSuccess) {
                const Domain::Relationships::NoAiDialogueChances allMax{ 100.0f, 100.0f, 100.0f };
                RelationshipEngineReconciler::GetSingleton().ApplyNoAiDialogueChanceGlobals(allMax);
                return;
            }

            const State::ActorId npcActorId{ npcFormID };
            State::ActorId spousePartner{};
            State::ActorId engagedPartner{};
            std::vector<State::ActorId> loverPartners;
            for (const auto& pair : snap->relationships->GetAdjacentPairs(npcActorId)) {
                const auto* edge = snap->relationships->FindEdge(pair);
                if (!edge) {
                    continue;
                }
                const auto other = pair.Other(npcActorId);
                if (edge->status == Foundation::RelationshipStatus::Married && spousePartner.IsNull()) {
                    spousePartner = other;
                } else if (edge->status == Foundation::RelationshipStatus::Engaged && engagedPartner.IsNull()) {
                    engagedPartner = other;
                } else if (edge->status == Foundation::RelationshipStatus::Lover) {
                    loverPartners.push_back(other);
                }
            }

            bool isSpouseAlsoLover = false;
            bool isFianceAlsoLover = false;
            for (const auto loverId : loverPartners) {
                if (!spousePartner.IsNull() && loverId == spousePartner) {
                    isSpouseAlsoLover = true;
                } else if (!engagedPartner.IsNull() && loverId == engagedPartner) {
                    isFianceAlsoLover = true;
                }
            }

            const float intimacyAdj = Domain::Relationships::ComputeIntimacyAdjustment(
                !spousePartner.IsNull(),
                isSpouseAlsoLover,
                !engagedPartner.IsNull(),
                isFianceAlsoLover,
                !loverPartners.empty(),
                ttllBonus);

            const auto stats = PlayerMiscStatsTracker::GetSingleton()->GetStats();
            const bool playerKiller = snap->cooldowns->playerKiller;

            const auto diffResult = RelationshipUseCases::GetSingleton().EvaluateMarriageDifficulty(
                npc,
                intimacyAdj,
                static_cast<float>(stats.mostGold),
                static_cast<float>(stats.housesOwned),
                static_cast<float>(stats.horsesOwned),
                static_cast<float>(stats.questsCompleted),
                static_cast<float>(stats.dungeonsCleared),
                static_cast<float>(stats.dragonSoulsCollected),
                playerKiller);

            const auto playerNpcStatus =
                snap->relationships->GetStatus(State::ActorId::Player(), npcActorId);
            const bool npcHasClosedMainPair =
                State::ComputeAdultActorFactionPlan(*snap, npcActorId).canFormNonCheatingMainRank ==
                std::int8_t{ 0 };
            const bool playerHasClosedMainPair =
                State::ComputePlayerFactionPlan(*snap).canFormNonCheatingMainRank == std::int8_t{ 0 };

            const auto chances = Domain::Relationships::ComputeNoAiDialogueChances(
                diffResult.chance,
                false,
                playerNpcStatus,
                npcHasClosedMainPair,
                playerHasClosedMainPair);

            RelationshipEngineReconciler::GetSingleton().ApplyNoAiDialogueChanceGlobals(chances);
        }
    }  // namespace

    RelationshipInteractionCoordinator& RelationshipInteractionCoordinator::GetSingleton() {
        static RelationshipInteractionCoordinator instance;
        return instance;
    }

    void RelationshipInteractionCoordinator::OnDialogueOpened(RE::FormID npcFormID) {
        bool alwaysSuccess = false;
        ComputeAndApplyDialogueChances(npcFormID, /*ttllBonus=*/0.0f, alwaysSuccess);
        if (alwaysSuccess || npcFormID == 0 || npcFormID == Foundation::kPlayerRefRawId) {
            return;
        }

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler || dataHandler->LookupModByName("TT_LoversLedger.esp") == nullptr) {
            return;
        }

        DispatchPostLock(
            GetSessionEpoch(),
            [npcFormID]() {
                auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
                auto* npcActor = RE::TESForm::LookupByID<RE::Actor>(npcFormID);
                if (!vm || !npcActor || npcActor->IsDeleted()) {
                    return;
                }
                RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> emptyCallback;
                auto* args = RE::MakeFunctionArguments(std::move(npcActor));
                if (!vm->DispatchStaticCall(
                        RE::BSFixedString("TTM_ServiceIntimateMoments"),
                        RE::BSFixedString("RefreshDialogueLoversLedger"),
                        args,
                        emptyCallback)) {
                    MARAS_LOG_DEBUG(
                        "[RelationshipInteractionCoordinator] RefreshDialogueLoversLedger dispatch skipped/failed for {:08X}",
                        npcFormID);
                }
            },
            "RelationshipInteractionCoordinator::RefreshDialogueLoversLedger");
    }

    void RelationshipInteractionCoordinator::ApplyDialogueLoversLedgerBonus(
        RE::FormID npcFormID,
        int exclusiveSex,
        int groupSex,
        int playerTopLoverRank) {
        const float ttllBonus = Domain::Relationships::ComputeLoversLedgerIntimacyBonus(
            exclusiveSex, groupSex, playerTopLoverRank);
        bool alwaysSuccess = false;
        ComputeAndApplyDialogueChances(npcFormID, ttllBonus, alwaysSuccess);
    }

    void RelationshipInteractionCoordinator::ShareSpouseIncome(RE::FormID spouseFormID) {
        if (spouseFormID == 0 || spouseFormID == Foundation::kPlayerRefRawId) {
            return;
        }

        const float currentDay = GetCurrentGameDays();
        Domain::Relationships::SpouseIncomeShareResult shareResult{};

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            const float lastShared =
                ws.cooldowns.Read().GetLastTimeSharedIncome(State::ActorId{ spouseFormID });
            shareResult = Domain::Relationships::ComputeSpouseIncomeShare(currentDay, lastShared);
            if (shareResult.awarded) {
                ws.cooldowns.Write().SetLastTimeSharedIncome(
                    State::ActorId{ spouseFormID }, shareResult.updatedLastSharedDay);
            }
        });

        if (!shareResult.awarded || shareResult.goldAmount <= 0) {
            return;
        }

        const int goldAmount = shareResult.goldAmount;
        DispatchPostLock(
            GetSessionEpoch(),
            [spouseFormID, goldAmount]() {
                auto* player = RE::PlayerCharacter::GetSingleton();
                auto* goldObj = FormCache::GetSingleton().GetGoldBaseObject();
                if (player && goldObj) {
                    player->AddObjectToContainer(goldObj, nullptr, goldAmount, nullptr);
                    MARAS_LOG_INFO("[RelationshipInteractionCoordinator] Spouse {:08X} shared {} gold income",
                                   spouseFormID, goldAmount);
                }
            },
            "RelationshipInteractionCoordinator::ShareSpouseIncome");
    }

    void RelationshipInteractionCoordinator::ProcessDivorceSettlement(
        RE::FormID exSpouseFormID,
        RE::TESQuest* divorceQuest) {
        const float currentDay = GetCurrentGameDays();
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        const float lastPaidDay = snap->cooldowns->lastTimePaidDivorceFee;
        const auto stats = PlayerMiscStatsTracker::GetSingleton()->GetStats();

        auto* player = RE::PlayerCharacter::GetSingleton();
        const int playerGold = player ? static_cast<int>(player->GetGoldAmount()) : 0;

        const auto settlement = Domain::Relationships::ComputeDivorceSettlement(
            currentDay,
            lastPaidDay,
            playerGold,
            stats.housesOwned,
            stats.horsesOwned,
            stats.questsCompleted,
            stats.dungeonsCleared);

        if (settlement.onCooldown) {
            MARAS_LOG_INFO("[RelationshipInteractionCoordinator] Divorce settlement skipped (7-day cooldown active)");
            return;
        }

        State::WorldStore::GetSingleton().Modify([&](State::WorldState& ws) {
            ws.cooldowns.Write().lastTimePaidDivorceFee = currentDay;
        });

        if (const RE::FormID questID = divorceQuest ? divorceQuest->GetFormID() : 0; questID != 0) {
            DispatchPostLock(
                GetSessionEpoch(),
                [questID]() {
                    if (auto* q = RE::TESForm::LookupByID<RE::TESQuest>(questID)) {
                        q->Stop();
                    }
                },
                "RelationshipInteractionCoordinator::StopDivorceQuest");
        }

        const std::string spouseName = (exSpouseFormID != 0) ? ActorSvc::GetName(exSpouseFormID) : "";

        ModalRequest req;
        req.title = "Divorce Settlement Notice";
        req.subtitle = !spouseName.empty() ? spouseName : "Hold Court";
        req.message = !spouseName.empty()
            ? ("A hold court has issued a divorce settlement ruling following your divorce from " + spouseName + ".")
            : "A hold court has issued a divorce settlement ruling.";
        req.tone = ModalTone::Danger;
        req.size = ModalSize::Md;
        req.defaultCancelButtonIndex = settlement.canPayDirectly ? 1 : 0;

        const std::string breakdownText = std::format(
            "Court Filing Fee: 100 Gold\n"
            "Liquid Wealth Share: {} Gold\n"
            "Property & Renown Assessment: {} Gold\n"
            "Total Settlement Due: {} Gold",
            settlement.playerWealthShare, settlement.assetsShare, settlement.totalFee);

        req.controls.push_back(ModalControl::MakeInfoBox(
            "fee_breakdown",
            "Settlement Breakdown",
            breakdownText,
            ModalTone::Warning));

        if (settlement.canPayDirectly) {
            req.buttons.push_back(ModalButton::Make(
                "pay_now",
                std::format("Pay Settlement Now ({} Gold)", settlement.totalFee),
                ModalButtonVariant::Primary));
            req.buttons.push_back(ModalButton::Make(
                "refuse_bounty",
                "Refuse / Add to Hold Bounty",
                ModalButtonVariant::Danger));
        } else {
            req.buttons.push_back(ModalButton::Make(
                "accept_bounty",
                std::format("Accept Hold Bounty ({} Gold)", settlement.totalFee),
                ModalButtonVariant::Danger));
        }

        const SessionEpoch expectedEpoch = GetSessionEpoch();
        const int totalFee = settlement.totalFee;
        const bool canPayDirectly = settlement.canPayDirectly;

        MARAS_LOG_INFO("[RelationshipInteractionCoordinator] Queuing divorce settlement modal for '{}' ({:08X}, fee={}, canPayDirectly={})",
                       req.subtitle, exSpouseFormID, totalFee, canPayDirectly);

        GetModalGateway().ShowModal(
            std::move(req),
            [expectedEpoch, exSpouseFormID, totalFee, canPayDirectly](const ModalResponse& resp) {
                if (!IsSessionEpochCurrent(expectedEpoch)) {
                    return;
                }
                const bool chosePayNow =
                    !resp.aborted && canPayDirectly &&
                    (resp.buttonIndex == 0 || resp.buttonId == "pay_now");

                AddEpochTask(expectedEpoch, "RelationshipInteractionCoordinator::ApplyDivorceSettlement",
                    [expectedEpoch, exSpouseFormID, totalFee, chosePayNow]() {
                        std::lock_guard lock(GetPapyrusMutex());
                        if (!IsSessionEpochCurrent(expectedEpoch)) {
                            return;
                        }
                        auto* innerPlayer = RE::PlayerCharacter::GetSingleton();
                        auto* goldObj = FormCache::GetSingleton().GetGoldBaseObject();
                        if (chosePayNow && innerPlayer && goldObj &&
                            innerPlayer->GetGoldAmount() >= totalFee) {
                            innerPlayer->RemoveItem(goldObj, totalFee, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
                            MARAS_LOG_INFO("[RelationshipInteractionCoordinator] Paid {} gold divorce settlement directly", totalFee);
                            return;
                        }

                        RE::TESFaction* crimeFaction = nullptr;
                        if (exSpouseFormID != 0) {
                            if (auto* spouseActor = RE::TESForm::LookupByID<RE::Actor>(exSpouseFormID)) {
                                crimeFaction = spouseActor->GetCrimeFaction();
                            }
                        }
                        if (!crimeFaction) {
                            crimeFaction = FormCache::GetSingleton().GetDefaultCrimeFaction();
                        }
                        if (crimeFaction) {
                            crimeFaction->SetCrimeGold(static_cast<std::int32_t>(totalFee));
                            MARAS_LOG_INFO("[RelationshipInteractionCoordinator] Applied {} gold divorce bounty to faction {:08X}",
                                           totalFee, crimeFaction->GetFormID());
                        } else {
                            MARAS_LOG_WARN("[RelationshipInteractionCoordinator] No crime faction available for {} gold divorce bounty",
                                           totalFee);
                        }
                    });
            });
    }

    float RelationshipInteractionCoordinator::GetBrokeupTime(RE::FormID npcFormID) const {
        if (npcFormID == 0) {
            return -1.0f;
        }
        return State::WorldStore::GetSingleton().Snapshot()->cooldowns->GetBrokeupTime(State::ActorId{ npcFormID });
    }

    bool RelationshipInteractionCoordinator::GetPlayerKiller() const {
        return State::WorldStore::GetSingleton().Snapshot()->cooldowns->playerKiller;
    }

    void RelationshipInteractionCoordinator::SetPlayerKiller(bool value) {
        State::WorldStore::GetSingleton().Modify([value](State::WorldState& ws) {
            ws.cooldowns.Write().playerKiller = value;
        });
    }

    bool RelationshipInteractionCoordinator::GetPlayerHadWedding() const {
        return State::WorldStore::GetSingleton().Snapshot()->cooldowns->playerHadWedding;
    }

    void RelationshipInteractionCoordinator::SetPlayerHadWedding(bool value) {
        State::WorldStore::GetSingleton().Modify([value](State::WorldState& ws) {
            ws.cooldowns.Write().playerHadWedding = value;
        });
    }

    bool RelationshipInteractionCoordinator::ShouldPromptSkipWedding() const {
        const auto snap = State::WorldStore::GetSingleton().Snapshot();
        return snap->cooldowns->playerHadWedding &&
               !snap->settings->skipWedding &&
               !snap->cooldowns->dontShowSkipWeddingMessage;
    }

    void RelationshipInteractionCoordinator::SetDontShowSkipWeddingMessage(bool value) {
        State::WorldStore::GetSingleton().Modify([value](State::WorldState& ws) {
            ws.cooldowns.Write().dontShowSkipWeddingMessage = value;
        });
    }

    void RelationshipInteractionCoordinator::NotifyEngagementCeremonyStarted(RE::FormID npcFormID) const {
        RelationshipEngineReconciler::GetSingleton().NotifyEngagementCeremonyStarted(npcFormID);
    }

}  // namespace MARAS::App
