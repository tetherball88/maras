#include "TestHarness.h"

#include "maras/foundation/UIHostStateMachine.h"
#include "maras/state/ModalQueueStateMachine.h"
#include "maras/state/SettingsKeyParser.h"

namespace MARAS::Tests {

    using namespace MARAS::Foundation;
    using namespace MARAS::State;

    MARAS_TEST(UIHostStateMachine, FocusPauseReclaimAndPre110WaitLifecycle) {
        UIHostStateMachine sm;
        MARAS_CHECK(sm.CanDrainAfterFocusReleased());

        // 1. Sync opens view & requests UI mode with pause=true
        const auto p1 = sm.PlanSync(/*wantActive=*/true, /*wantPause=*/true, /*cancelSupported=*/true);
        MARAS_CHECK(p1.showView && p1.raiseView && p1.setEscapeCapture);
        MARAS_CHECK(p1.requestUIMode && p1.requestPause && !p1.requestReclaim);
        sm.OnRequestUIModeResult(/*ok=*/true, /*appliedPause=*/true, /*clearedReclaim=*/false);
        MARAS_CHECK(sm.IsUIModeRequested());
        MARAS_CHECK(!sm.CanDrainAfterFocusReleased());

        // 2. H30 CancelDenied race: Release while RequestUIMode in flight -> Cancel returns false -> MUST NOT drain early
        const auto rel1 = sm.PlanRelease(/*viewInitialized=*/true, /*cancelSupported=*/true);
        MARAS_CHECK(rel1.hideView && rel1.cancelPendingRequest && !rel1.drainAfterFocusReleased);
        MARAS_CHECK(!sm.OnCancelBeforeReleaseResult(/*cancelOk=*/false));
        MARAS_CHECK(sm.IsUIModeRequested() && sm.IsReleasePending());
        MARAS_CHECK(!sm.CanDrainAfterFocusReleased());

        // Entry arrives late with shouldKeep=false -> must immediately trigger ReleaseNow
        MARAS_CHECK_EQ(sm.OnUIModeEntered(/*shouldKeep=*/false), UIModeEnteredAction::ReleaseNow);
        const auto rel2 = sm.PlanRelease(/*viewInitialized=*/true, /*cancelSupported=*/true);
        MARAS_CHECK(rel2.releaseUIMode && !rel2.drainAfterFocusReleased);
        MARAS_CHECK(!sm.OnReleaseUIModeResult(/*ok=*/true));
        MARAS_CHECK(!sm.CanDrainAfterFocusReleased());

        // Exit completes cleanly -> now drains
        const auto ex1 = sm.OnUIModeExited(/*modalActive=*/false, /*dashboardVisible=*/false);
        MARAS_CHECK(ex1.drainAfterFocusReleased && !ex1.resyncDashboard && !ex1.scheduleModalReacquire);
        MARAS_CHECK(sm.CanDrainAfterFocusReleased());

        // 3. H30 Pre-1.1.0 reclaim: focusOwned -> Release -> Reopen before UIModeExited -> waits & resyncs on exit
        (void)sm.PlanSync(true, true, false);
        sm.OnRequestUIModeResult(true, true, false);
        (void)sm.OnUIModeEntered(true);
        const auto relPre110 = sm.PlanRelease(true, false);
        MARAS_CHECK(relPre110.releaseUIMode);
        const auto syncWait = sm.PlanSync(/*wantActive=*/true, /*wantPause=*/false, /*cancelSupported=*/false);
        MARAS_CHECK(syncWait.waitForExitToResync && !syncWait.requestUIMode);
        MARAS_CHECK(sm.IsReleasePending());

        const auto exResync = sm.OnUIModeExited(/*modalActive=*/false, /*dashboardVisible=*/true);
        MARAS_CHECK(exResync.resyncDashboard && !exResync.closeDashboard && !exResync.drainAfterFocusReleased);

        // 4. 1.1.0+ reclaim: focusOwned + releasePending -> RequestUIMode with kUIModeFlagReclaim clears releasePending
        (void)sm.PlanSync(true, false, true);
        sm.OnRequestUIModeResult(true, false, false);
        (void)sm.OnUIModeEntered(true);
        (void)sm.PlanRelease(true, true);
        const auto syncReclaim = sm.PlanSync(/*wantActive=*/true, /*wantPause=*/true, /*cancelSupported=*/true);
        MARAS_CHECK(syncReclaim.requestUIMode && syncReclaim.requestReclaim && syncReclaim.requestPause);
        sm.OnRequestUIModeResult(true, true, true);
        MARAS_CHECK(!sm.IsReleasePending() && sm.AppliedPause());
    }

    MARAS_TEST(UIHostStateMachine, ModalRecoveryRefuseBoundsAndRenderDeadReset) {
        UIHostStateMachine sm;
        (void)sm.PlanSync(true, true, true);
        sm.OnRequestUIModeResult(true, true, false);
        (void)sm.OnUIModeEntered(true);

        // C20: UIModeExited while modal active keeps view shown, never drains, schedules reacquire
        const auto exModal = sm.OnUIModeExited(/*modalActive=*/true, /*dashboardVisible=*/false);
        MARAS_CHECK(exModal.scheduleModalReacquire && !exModal.hideView && !exModal.drainAfterFocusReleased);
        MARAS_CHECK(sm.IsViewShown());

        // C20: UIModeRefused while modal active retries up to kMaxModalRefuseRetries (3), then stops spin-loop
        for (std::uint32_t i = 1; i <= UIHostStateMachine::kMaxModalRefuseRetries; ++i) {
            const auto ref = sm.OnUIModeRefused(/*modalActive=*/true, /*dashboardVisible=*/false);
            MARAS_CHECK(ref.scheduleModalRetry);
            MARAS_CHECK_EQ(ref.retryAttempt, i);
            MARAS_CHECK(!ref.drainAfterFocusReleased);
        }
        const auto refCap = sm.OnUIModeRefused(/*modalActive=*/true, /*dashboardVisible=*/false);
        MARAS_CHECK(!refCap.scheduleModalRetry && !refCap.drainAfterFocusReleased);
        MARAS_CHECK_EQ(refCap.retryAttempt, UIHostStateMachine::kMaxModalRefuseRetries + 1);

        // Entering UI mode resets retry counter
        (void)sm.OnUIModeEntered(true);
        MARAS_CHECK_EQ(sm.ModalRefuseRetryCount(), std::uint32_t{ 0 });

        // C19: ViewDestroyed / RenderDead clears all state and allows drain
        sm.ResetOnViewDestroyedOrRenderDead();
        MARAS_CHECK(!sm.IsViewShown() && !sm.IsFocusOwned() && !sm.IsUIModeRequested() && !sm.IsReleasePending());
        MARAS_CHECK(sm.CanDrainAfterFocusReleased());
    }

    MARAS_TEST(ModalQueueStateMachine, MultiModalSyncPromotionEpochPruningAndAbortAll) {
        ModalQueueStateMachine mq;
        MARAS_CHECK(!mq.HasActiveOrQueued());

        auto mkItem = [](std::uint64_t tok, const char* id, SessionEpoch ep, std::int32_t cancelIdx = 1) {
            return ModalQueueItem{
                .token = tok,
                .modalId = id,
                .specJson = std::string("{\"modalId\":\"") + id + "\"}",
                .buttonIds = { "confirm", "cancel", "explain" },
                .defaultCancelButtonIndex = cancelIdx,
                .expectedEpoch = ep
            };
        };

        // Enqueue 3 modals: first activates, next two queue
        const auto e1 = mq.Enqueue(mkItem(1, "m1", 5));
        const auto e2 = mq.Enqueue(mkItem(2, "m2_stale", 4));
        const auto e3 = mq.Enqueue(mkItem(3, "m3", 5, 2));
        MARAS_CHECK(e1.activatedImmediately && !e2.activatedImmediately && !e3.activatedImmediately);
        MARAS_CHECK(mq.HasActiveOrQueued());
        MARAS_CHECK_EQ(mq.GetActiveModalId(), std::string("m1"));

        // Send deduplication unless forceResend=true
        const auto s1 = mq.TakePayloadToSend(/*forceResend=*/false);
        MARAS_REQUIRE(s1.has_value());
        MARAS_CHECK_EQ(s1->modalId, std::string("m1"));
        MARAS_CHECK(!mq.TakePayloadToSend(/*forceResend=*/false).has_value());
        MARAS_CHECK(mq.TakePayloadToSend(/*forceResend=*/true).has_value());

        // Mismatched modal response rejected
        MARAS_CHECK(!mq.ResolveActive("wrong_id", "confirm", -1, false, 5).matched);

        // H29: Resolving m1 resolves buttonId->"explain"(2), drops stale m2 (epoch 4), and synchronously promotes m3
        const auto r1 = mq.ResolveActive("m1", "explain", -1, false, 5);
        MARAS_REQUIRE(r1.matched);
        MARAS_CHECK(r1.callbackEpochValid);
        MARAS_CHECK_EQ(r1.resolvedButtonIndex, 2);
        MARAS_CHECK(r1.hasMoreModals);
        MARAS_CHECK_EQ(r1.promotedModalId, std::string("m3"));
        MARAS_REQUIRE_EQ(r1.droppedStaleTokens.size(), std::size_t{ 1 });
        MARAS_CHECK_EQ(r1.droppedStaleTokens[0], std::uint64_t{ 2 });
        MARAS_CHECK(mq.HasActiveOrQueued());
        MARAS_CHECK_EQ(mq.GetActiveModalId(), std::string("m3"));

        // C19: AbortAll drains active + queued in FIFO order preserving defaultCancelButtonIndex
        (void)mq.Enqueue(mkItem(4, "m4", 5, 1));
        const auto aborted = mq.AbortAll();
        MARAS_REQUIRE_EQ(aborted.size(), std::size_t{ 2 });
        MARAS_CHECK_EQ(aborted[0].modalId, std::string("m3"));
        MARAS_CHECK_EQ(aborted[0].defaultCancelButtonIndex, 2);
        MARAS_CHECK_EQ(aborted[1].modalId, std::string("m4"));
        MARAS_CHECK(!mq.HasActiveOrQueued());

        // Stale epoch on active entry pruned cleanly by PruneAndPromote
        (void)mq.Enqueue(mkItem(5, "m_old", 5));
        (void)mq.Enqueue(mkItem(6, "m_new", 6));
        const auto promo = mq.PruneAndPromote(/*currentEpoch=*/6);
        MARAS_CHECK(promo.hasActive);
        MARAS_CHECK_EQ(promo.promotedModalId, std::string("m_new"));
        MARAS_REQUIRE_EQ(promo.droppedStaleTokens.size(), std::size_t{ 1 });
        MARAS_CHECK_EQ(promo.droppedStaleTokens[0], std::uint64_t{ 5 });
    }

    MARAS_TEST(SettingsKeyParser, HotkeyModifiersDisableScancodesAndPapyrusAliases) {
        SettingsData s{};

        // H12: Explicit disable tokens keep toggleUIKey == 0 without falling back to Alt+B
        for (const char* off : { "", "0", "0x0", "0x00", "none", "DISABLED" }) {
            const auto res = ParseKeyCombo(off, s);
            MARAS_CHECK(res.disabled && !res.fellBackToDefault);
            MARAS_CHECK_EQ(s.toggleUIKey, std::uint32_t{ 0 });
            MARAS_CHECK(!s.toggleUIModCtrl && !s.toggleUIModShift && !s.toggleUIModAlt);
        }

        // H12: L*/R* modifiers + hex & decimal scancodes + named keys
        const auto hexRes = ParseKeyCombo("LCtrl + RShift + 0x30", s);
        MARAS_CHECK(!hexRes.disabled && !hexRes.fellBackToDefault);
        MARAS_CHECK(s.toggleUIModCtrl && s.toggleUIModShift && !s.toggleUIModAlt);
        MARAS_CHECK_EQ(s.toggleUIKey, std::uint32_t{ 0x30 });

        const auto decRes = ParseKeyCombo("LAlt + 67", s);
        MARAS_CHECK(!decRes.disabled && !decRes.fellBackToDefault);
        MARAS_CHECK(!s.toggleUIModCtrl && !s.toggleUIModShift && s.toggleUIModAlt);
        MARAS_CHECK_EQ(s.toggleUIKey, std::uint32_t{ 67 });

        const auto namedRes = ParseKeyCombo("RCtrl + F9", s);
        MARAS_CHECK(!namedRes.disabled && !namedRes.fellBackToDefault);
        MARAS_CHECK(s.toggleUIModCtrl && !s.toggleUIModAlt);
        MARAS_CHECK_EQ(s.toggleUIKey, std::uint32_t{ 0x43 });

        // Invalid non-empty string falls back to Alt+B (0x30)
        const auto badRes = ParseKeyCombo("Ctrl + TotallyInvalidKey", s);
        MARAS_CHECK(!badRes.disabled && badRes.fellBackToDefault);
        MARAS_CHECK(!s.toggleUIModCtrl && !s.toggleUIModShift && s.toggleUIModAlt);
        MARAS_CHECK_EQ(s.toggleUIKey, std::uint32_t{ 0x30 });

        // H12: Legacy Papyrus bool/int/float aliases, clamps, and enum bounds validation
        MARAS_CHECK(TrySetBoolSetting(s, "alwaysSuccess", true));
        bool bVal = false;
        MARAS_CHECK(TryGetBoolSetting(s, "alwaysSuccessMarriage", bVal) && bVal);
        MARAS_CHECK(TrySetBoolSetting(s, "confirmAcceptProposal", false));
        MARAS_CHECK(TryGetBoolSetting(s, "confirmStatusChange_Marry", bVal) && !bVal);
        MARAS_CHECK(TrySetBoolSetting(s, "confirmBreakupEngagement", false));
        MARAS_CHECK(TryGetBoolSetting(s, "confirmBreakup_Engagement", bVal) && !bVal);
        MARAS_CHECK(TrySetBoolSetting(s, "confirmLowAffectionDivorce", false));
        MARAS_CHECK(TryGetBoolSetting(s, "confirmDivorceDuringLowAffectionQuest", bVal) && !bVal);
        MARAS_CHECK(TrySetBoolSetting(s, "globalBanCritter", true));
        MARAS_CHECK(TryGetBoolSetting(s, "bGlobalBanCritter", bVal) && bVal);
        MARAS_CHECK(TrySetBoolSetting(s, "globalBanPet", true));
        MARAS_CHECK(TryGetBoolSetting(s, "bGlobalBanPet", bVal) && bVal);

        float fVal = 0.0f;
        MARAS_CHECK_EQ(TrySetFloatSetting(s, "affectionDecayMult", 2.25f), SettingWriteStatus::Ok);
        MARAS_CHECK(TryGetFloatSetting(s, "affectionDecayMultiplier", fVal) && fVal > 2.2f);
        MARAS_CHECK_EQ(TrySetFloatSetting(s, "actionRadius", 1450.0f), SettingWriteStatus::Ok);
        MARAS_CHECK(TryGetFloatSetting(s, "skyrimnetActionRadius", fVal) && fVal > 1440.0f);
        MARAS_CHECK_EQ(TrySetFloatSetting(s, "uiScale", 4.0f), SettingWriteStatus::Ok);
        MARAS_CHECK(TryGetFloatSetting(s, "uiScale", fVal) && fVal <= 1.501f);

        std::int32_t iVal = 0;
        MARAS_CHECK_EQ(TrySetIntSetting(s, "matchmakingSuggestionsMax", 6), SettingWriteStatus::Ok);
        MARAS_CHECK(TryGetIntSetting(s, "matchmakingMaxSuggestions", iVal) && iVal == 6);
        MARAS_CHECK_EQ(TrySetIntSetting(s, "childTrainingDurationDays", 999), SettingWriteStatus::Ok);
        MARAS_CHECK(TryGetIntSetting(s, "childTrainingDurationDays", iVal) && iVal == 120);
        MARAS_CHECK_EQ(TrySetIntSetting(s, "childTrainingCost", -50), SettingWriteStatus::Ok);
        MARAS_CHECK(TryGetIntSetting(s, "childTrainingCost", iVal) && iVal == 0);
        MARAS_CHECK_EQ(TrySetIntSetting(s, "matchmakingOrientation", 99), SettingWriteStatus::OutOfRange);
        MARAS_CHECK_EQ(TrySetIntSetting(s, "nonexistentKey", 1), SettingWriteStatus::UnknownKey);
    }

}  // namespace MARAS::Tests
