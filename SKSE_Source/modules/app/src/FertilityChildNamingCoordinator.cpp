#include "maras/app/FertilityChildNamingCoordinator.h"

#include "maras/app/AppContext.h"
#include "maras/app/FertilityLifecycleCoordinator.h"
#include "maras/domain/lineage/FertilityLifecycleRules.h"
#include "maras/engine/ActorService.h"
#include "maras/engine/EngineLogging.h"
#include "maras/engine/LocalizationService.h"
#include "maras/engine/NamePoolLoader.h"
#include "maras/engine/SpatialWorldIndex.h"
#include "maras/engine/TaskDispatcher.h"
#include "maras/foundation/StringUtils.h"
#include "maras/projection/PerspectiveView.h"
#include "maras/state/SpatialActorSnapshot.h"
#include "maras/state/WorldStore.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace MARAS::App {

    namespace {

        constexpr std::size_t kMaxSuggestedNames = 16;

        [[nodiscard]] bool ContainsCaseInsensitive(
            const std::vector<std::string>& list,
            std::string_view candidate) noexcept {
            for (const auto& entry : list) {
                if (Foundation::StringUtils::CaseInsensitiveEquals(entry, candidate)) {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] std::vector<std::string> BuildSuggestedNames(
            RE::Actor* motherActor,
            RE::Actor* perceivedFatherActor,
            bool isFemale,
            const std::vector<std::string>& existingNames) {
            auto& pool = Engine::NamePoolLoader::GetSingleton();
            if (!pool.IsChildLoaded()) {
                (void)pool.LoadChildNamesFromFile();
            }

            std::vector<std::string> suggestions;
            suggestions.reserve(kMaxSuggestedNames);

            auto appendFromActor = [&](RE::Actor* parentActor, std::size_t quota) {
                if (!parentActor || suggestions.size() >= kMaxSuggestedNames) {
                    return;
                }
                for (std::size_t attempt = 0; attempt < quota * 3 && suggestions.size() < kMaxSuggestedNames; ++attempt) {
                    std::string candidate = pool.GetRandomChildName(parentActor, isFemale, true);
                    if (candidate.empty() ||
                        ContainsCaseInsensitive(existingNames, candidate) ||
                        ContainsCaseInsensitive(suggestions, candidate)) {
                        continue;
                    }
                    suggestions.push_back(std::move(candidate));
                    if (suggestions.size() >= quota) {
                        break;
                    }
                }
            };

            const std::size_t motherQuota = perceivedFatherActor ? (kMaxSuggestedNames / 2) : kMaxSuggestedNames;
            appendFromActor(motherActor, motherQuota);
            appendFromActor(perceivedFatherActor, kMaxSuggestedNames);

            if (suggestions.size() < kMaxSuggestedNames) {
                for (std::size_t attempt = 0; attempt < 32 && suggestions.size() < kMaxSuggestedNames; ++attempt) {
                    std::string candidate = pool.GetRandomChildName("nord", isFemale, true);
                    if (candidate.empty() ||
                        ContainsCaseInsensitive(existingNames, candidate) ||
                        ContainsCaseInsensitive(suggestions, candidate)) {
                        continue;
                    }
                    suggestions.push_back(std::move(candidate));
                }
            }

            return suggestions;
        }

    }  // namespace

    FertilityChildNamingCoordinator& FertilityChildNamingCoordinator::GetSingleton() noexcept {
        static FertilityChildNamingCoordinator instance;
        return instance;
    }

    void FertilityChildNamingCoordinator::ShowChildNamingModal(
        Engine::IEngineEventHooks::FertilityChildNamingRequest request,
        std::function<void(const std::string&)> onResolved) {
        const auto worldSnap = State::WorldStore::GetSingleton().Snapshot();
        const auto spatialSnap = State::SpatialSnapshotStore::GetSingleton().Snapshot();

        RE::FormID motherId = request.motherId;
        RE::FormID perceivedFatherId = 0;

        if (worldSnap && spatialSnap) {
            const auto view = Projection::PerspectiveView::ForDashboard(worldSnap, spatialSnap);
            if (motherId != 0 && worldSnap->lineage) {
                const auto infantCid = Domain::Lineage::FindSyntheticChildByName(
                    *worldSnap->lineage,
                    State::ActorId{ motherId },
                    "Newborn",
                    Foundation::ChildStage::Infant);
                if (!infantCid.IsNull()) {
                    if (const auto perceived = view.GetPerceivedChild(infantCid)) {
                        if (!perceived->perceivedBioFather.IsNull() &&
                            !perceived->perceivedBioFather.IsUnknownFather()) {
                            perceivedFatherId = perceived->perceivedBioFather.raw();
                        }
                    }
                }
            }
            if (perceivedFatherId == 0 && motherId != 0 && worldSnap->relationships) {
                const auto primary = worldSnap->relationships->GetPrimaryPartner(State::ActorId{ motherId });
                if (!primary.IsNull()) {
                    perceivedFatherId = primary.raw();
                }
            }
        }

        auto* motherActor = motherId != 0 ? RE::TESForm::LookupByID<RE::Actor>(motherId) : nullptr;
        auto* perceivedFatherActor =
            perceivedFatherId != 0 ? RE::TESForm::LookupByID<RE::Actor>(perceivedFatherId) : nullptr;

        const std::string motherName = motherId != 0 ? ActorSvc::GetName(motherId) : std::string{};
        const std::string fatherName =
            perceivedFatherId != 0 ? ActorSvc::GetName(perceivedFatherId) : std::string{};

        std::vector<std::string> suggestions = BuildSuggestedNames(
            motherActor,
            perceivedFatherActor,
            request.isFemale,
            request.existingNames);

        const std::string defaultName = !suggestions.empty()
                                            ? suggestions.front()
                                            : (request.isFemale ? std::string("Freya") : std::string("Sven"));

        std::string randomFallback = (suggestions.size() > 1) ? suggestions[1] : defaultName;

        ModalRequest modalReq{};
        modalReq.tone = ModalTone::Default;
        modalReq.size = ModalSize::Md;
        modalReq.defaultCancelButtonIndex = 0;
        modalReq.title = request.isFemale
                             ? Tr("modal.child_naming.title_daughter")
                             : Tr("modal.child_naming.title_son");

        if (!motherName.empty() && !fatherName.empty()) {
            modalReq.subtitle = TrFmt(
                "modal.child_naming.sub_parents_two",
                { { "mother", motherName }, { "father", fatherName } });
        } else if (!motherName.empty()) {
            modalReq.subtitle = TrFmt(
                "modal.child_naming.sub_mother_only",
                { { "mother", motherName } });
        }

        modalReq.message = request.isFemale
                               ? Tr("modal.child_naming.message_daughter")
                               : Tr("modal.child_naming.message_son");

        ModalControl textCtrl = ModalControl::MakeTextInput(
            "child_name",
            Tr("modal.child_naming.input_label"),
            defaultName,
            Tr("modal.child_naming.input_placeholder"),
            Tr("modal.child_naming.input_desc"));
        textCtrl.required = true;
        textCtrl.disallowedValues = request.existingNames;
        textCtrl.disallowedMessage = Tr("modal.child_naming.err_duplicate");
        modalReq.controls.push_back(std::move(textCtrl));

        if (!suggestions.empty()) {
            std::vector<ModalSelectOption> selectOpts;
            selectOpts.reserve(suggestions.size());
            for (const auto& name : suggestions) {
                selectOpts.push_back(ModalSelectOption{
                    .value = name,
                    .label = name,
                });
            }
            ModalControl selectCtrl = ModalControl::MakeSelect(
                "suggested_name",
                Tr("modal.child_naming.suggested_label"),
                std::move(selectOpts),
                defaultName,
                Tr("modal.child_naming.suggested_desc"));
            selectCtrl.syncTargetControlId = "child_name";
            modalReq.controls.push_back(std::move(selectCtrl));
        }

        ModalButton confirmBtn = ModalButton::Make(
            "confirm",
            Tr("modal.child_naming.btn_confirm"),
            ModalButtonVariant::Primary);
        ModalButton randomBtn = ModalButton::Make(
            "random_name",
            Tr("modal.child_naming.btn_random"),
            ModalButtonVariant::Secondary);
        randomBtn.bypassValidation = true;

        modalReq.buttons = { std::move(confirmBtn), std::move(randomBtn) };

        const State::SessionEpoch expectedEpoch = State::GetSessionEpoch();
        std::vector<std::string> existingNamesCopy = std::move(request.existingNames);
        const bool isFemale = request.isFemale;
        const bool cameFromMcmForceGrowth = request.cameFromMcmForceGrowth;

        GetModalGateway().ShowModal(
            std::move(modalReq),
            [expectedEpoch,
             existingNamesCopy = std::move(existingNamesCopy),
             randomFallback = std::move(randomFallback),
             motherId,
             isFemale,
             cameFromMcmForceGrowth,
             onResolved = std::move(onResolved)](const ModalResponse& resp) mutable {
                if (resp.aborted || !State::IsSessionEpochCurrent(expectedEpoch)) {
                    return;
                }

                const std::string typedName = resp.GetString("child_name");
                const std::string suggestedName = resp.GetString("suggested_name");
                const std::string finalName = Domain::Lineage::ResolveSubmittedChildName(
                    resp.buttonId,
                    typedName,
                    suggestedName,
                    existingNamesCopy,
                    randomFallback);

                if (onResolved) {
                    onResolved(finalName);
                }

                // FMR's Debug MCM PLAYER_GROWTH / NPC_GROWTH omits SendChildGrewUpEvent;
                // promote the Newborn Infant record in M.A.R.A.S. directly so both mods stay in sync.
                if (cameFromMcmForceGrowth && motherId != 0) {
                    (void)AddEpochTask(
                        expectedEpoch,
                        "FertilityChildNamingCoordinator::SyncMcmForceGrowth",
                        [motherId, finalName, isFemale]() {
                            std::lock_guard lock(GetPapyrusMutex());
                            if (auto* mother = RE::TESForm::LookupByID<RE::Actor>(motherId)) {
                                FertilityLifecycleCoordinator::GetSingleton().OnChildGrewUp(
                                    mother,
                                    finalName,
                                    isFemale ? 1 : 0,
                                    false,
                                    nullptr);
                            }
                        });
                }
            });
    }

}  // namespace MARAS::App
