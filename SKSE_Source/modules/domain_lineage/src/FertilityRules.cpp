#include "maras/domain/lineage/FertilityRules.h"

#include "maras/domain/lineage/LineageRules.h"
#include "maras/foundation/Logging.h"
#include "maras/foundation/StringUtils.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace MARAS::Domain::Lineage {

    namespace {
        using Foundation::ChildStage;
        using Foundation::StringUtils::Contains;
        using Foundation::StringUtils::ToLowerStd;

        [[nodiscard]] bool ContainsTriggerWord(std::string_view trigger, std::string_view word) {
            return Contains(trigger, word);
        }

        void MarkUnique(std::vector<ActorId>& list, ActorId id) {
            if (!id.IsNull() && !id.IsUnknownFather() &&
                std::find(list.begin(), list.end(), id) == list.end()) {
                list.push_back(id);
            }
        }

        void MarkUniqueChild(std::vector<ChildId>& list, ChildId id) {
            if (!id.IsNull() &&
                std::find(list.begin(), list.end(), id) == list.end()) {
                list.push_back(id);
            }
        }
    }  // namespace

    PhenotypeTier ClassifyPhenotypeTierFromEditorID(std::string_view raceEditorId) noexcept {
        if (raceEditorId.empty()) {
            return PhenotypeTier::Unknown;
        }
        const std::string lower = ToLowerStd(raceEditorId);
        if (lower.find("argonianrace") != std::string::npos) {
            return PhenotypeTier::Argonian;
        }
        if (lower.find("khajiitrace") != std::string::npos) {
            return PhenotypeTier::Khajiit;
        }
        if (lower.find("orcrace") != std::string::npos) {
            return PhenotypeTier::Orc;
        }
        if (lower.find("highelfrace") != std::string::npos ||
            lower.find("woodelfrace") != std::string::npos ||
            lower.find("darkelfrace") != std::string::npos ||
            lower.find("snowelfrace") != std::string::npos ||
            lower.find("falmerrace") != std::string::npos) {
            return PhenotypeTier::Mer;
        }
        if (lower.find("redguardrace") != std::string::npos) {
            return PhenotypeTier::Redguard;
        }
        if (lower.find("nordrace") != std::string::npos ||
            lower.find("bretonrace") != std::string::npos ||
            lower.find("imperialrace") != std::string::npos ||
            lower.find("elderrace") != std::string::npos) {
            return PhenotypeTier::PaleHuman;
        }
        return PhenotypeTier::Unknown;
    }

    bool HasPhenotypeMismatch(PhenotypeTier fatherA, PhenotypeTier fatherB) noexcept {
        if (fatherA == PhenotypeTier::Unknown || fatherB == PhenotypeTier::Unknown) {
            return false;
        }
        return fatherA != fatherB;
    }

    bool HasPhenotypeMismatch(std::string_view raceEditorA, std::string_view raceEditorB) noexcept {
        return HasPhenotypeMismatch(
            ClassifyPhenotypeTierFromEditorID(raceEditorA),
            ClassifyPhenotypeTierFromEditorID(raceEditorB));
    }

    std::string GetVisualDiscrepancyDescription(
        PhenotypeTier bioTier,
        PhenotypeTier presumedTier,
        std::string_view subjectName) {
        if (bioTier == PhenotypeTier::Unknown ||
            presumedTier == PhenotypeTier::Unknown ||
            bioTier == presumedTier) {
            return "";
        }
        const std::string_view subject = subjectName.empty() ? "the child" : subjectName;
        if (bioTier == PhenotypeTier::Argonian || presumedTier == PhenotypeTier::Argonian) {
            return std::format(
                "Reptilian scales, horns, or Argonian facial traits on {} do not match {}.",
                subject, presumedTier == PhenotypeTier::Argonian ? "a non-Argonian parent" : "the presumed father");
        }
        if (bioTier == PhenotypeTier::Khajiit || presumedTier == PhenotypeTier::Khajiit) {
            return std::format(
                "Feline fur, whiskers, or Khajiit features on {} conflict with the presumed father.",
                subject);
        }
        if (bioTier == PhenotypeTier::Orc || presumedTier == PhenotypeTier::Orc) {
            return std::format(
                "Orsimer skin tone, heavy brow, or tusks on {} starkly differ from the presumed father.",
                subject);
        }
        if (bioTier == PhenotypeTier::Mer || presumedTier == PhenotypeTier::Mer) {
            return std::format(
                "Elven features, ears, or complexion on {} clearly diverge from the presumed father.",
                subject);
        }
        return std::format(
            "Skin complexion and facial heritage of {} noticeably differ from the presumed father.",
            subject);
    }

    std::optional<State::PaternitySecretState> FindChildPaternitySecret(
        const State::WorldStateSnapshot& snap,
        ChildId childId) noexcept {
        if (!snap.lineage || childId.IsNull()) {
            return std::nullopt;
        }
        const auto* rec = snap.lineage->FindChild(childId);
        if (!rec || !rec->paternitySecret.HasActiveSecret()) {
            return std::nullopt;
        }
        return rec->paternitySecret;
    }

    UnbornChildRegistrationOutcome ApplyRegisterUnbornChild(
        State::WorldState& ws,
        const UnbornChildRegistrationInput& input,
        const State::IActorMetadataProvider& meta) {
        UnbornChildRegistrationOutcome out{};
        if (input.bioMother.IsNull()) {
            return out;
        }

        const std::size_t existingCount = ws.lineage.Read().GetUnbornOfMother(input.bioMother).size();
        const ActorId presumedPartner = ws.relationships.Read().GetPrimaryPartner(input.bioMother);
        const ActorId resolvedBioFather = !input.bioFather.IsNull()
                                              ? input.bioFather
                                              : (!presumedPartner.IsNull() ? presumedPartner : ActorId::UnknownFather());

        const auto synId = ws.lineage.Write().AllocateSyntheticChildId();
        const ChildId childId{ synId.raw() };
        out.childId = childId;
        out.presumedFather = presumedPartner;

        const std::string motherName = meta.GetActorName(input.bioMother);
        const std::string childName =
            std::format("{}'s Unborn Child #{}", motherName.empty() ? "Mother" : motherName, existingCount + 1);

        const auto ensureRegistered = [&](ActorId id) {
            if (id.IsNull() || id.IsUnknownFather() || id.IsPlayer() || id.IsSyntheticChild()) {
                return;
            }
            if (!ws.actors.Read().Find(id.AsRefId())) {
                State::ActorProfileRecord prof{};
                prof.refId = id.AsRefId();
                prof.baseId = meta.GetActorBaseId(id);
                prof.isRegisteredAdult = true;
                ws.actors.Write().Upsert(std::move(prof));
            }
        };

        ensureRegistered(input.bioMother);
        ensureRegistered(presumedPartner);
        ensureRegistered(resolvedBioFather);

        State::ChildRecord rec{};
        rec.childId = childId;
        rec.name = childName;
        rec.stage = ChildStage::Unborn;
        rec.isAdopted = false;
        rec.isBiologicalChild = true;
        rec.bioMother = input.bioMother;
        rec.bioFather = resolvedBioFather;
        rec.conceptionTimestamp = input.conceptionTimestamp;
        rec.gender = 2;

        const ActorId guardianB =
            (!presumedPartner.IsNull() && ws.IsMarried(input.bioMother, presumedPartner))
                ? presumedPartner
                : ActorId{};
        rec.guardians.Assign(input.bioMother, guardianB);

        if (!presumedPartner.IsNull() &&
            !resolvedBioFather.IsNull() &&
            presumedPartner != resolvedBioFather) {
            rec.presumedFather = presumedPartner;
            rec.paternitySecret.isSecret = true;
            rec.paternitySecret.isSecretForPartner = true;
            rec.paternitySecret.isSecretForBioFather = true;
            rec.paternitySecret.isPregnancySecretForPartner = true;
            rec.paternitySecret.lastCheckTimestamp = input.conceptionTimestamp;
            if (!resolvedBioFather.IsUnknownFather()) {
                rec.paternitySecret.bioFatherName =
                    input.bioFatherName.empty() ? meta.GetActorName(resolvedBioFather) : input.bioFatherName;
                rec.paternitySecret.bioFatherRaceId =
                    !input.bioFatherRaceId.IsNull() ? input.bioFatherRaceId : meta.GetActorRaceId(resolvedBioFather);
            }
            rec.paternitySecret.hasPhenotypeMismatch = HasPhenotypeMismatch(
                input.presumedFatherRaceEditorId,
                input.bioFatherRaceEditorId);
            out.hasSecret = true;
            out.hasPhenotypeMismatch = rec.paternitySecret.hasPhenotypeMismatch;
        }

        EvaluateChildBeastbloodInheritance(rec, input.motherIsWerewolf, input.bioFatherIsWerewolf);
        ws.lineage.Write().UpsertChild(std::move(rec));

        MarkUnique(out.dirtyActors, input.bioMother);
        MarkUnique(out.dirtyActors, guardianB);
        return out;
    }

    PromoteUnbornOutcome ApplyPromoteUnbornToInfant(
        State::WorldState& ws,
        ActorId motherId,
        float birthTime,
        std::string_view name,
        ActorId bioFatherId,
        bool motherIsWerewolf,
        bool fatherIsWerewolf,
        const State::IActorMetadataProvider& meta,
        Foundation::IRng& rng) {
        PromoteUnbornOutcome out{};
        if (motherId.IsNull()) {
            return out;
        }

        const auto unbornIds = ws.lineage.Read().GetUnbornOfMother(motherId);
        if (unbornIds.empty()) {
            return out;
        }

        ChildId chosenId{};
        if (!bioFatherId.IsNull()) {
            for (ChildId candidate : unbornIds) {
                const auto* rec = ws.lineage.Read().FindChild(candidate);
                if (rec && rec->bioFather == bioFatherId) {
                    chosenId = candidate;
                    break;
                }
            }
        }
        if (chosenId.IsNull()) {
            chosenId = unbornIds.front();
        }

        auto* rec = ws.lineage.Write().FindChildMutable(chosenId);
        if (!rec) {
            return out;
        }

        const ActorId prevCoGuardian = rec->guardians.OtherGuardian(motherId);
        rec->stage = ChildStage::Infant;
        rec->birthTimestamp = birthTime;
        rec->timestamp = static_cast<std::int32_t>(birthTime);
        rec->name = name.empty() ? "Newborn" : std::string(name);
        rec->paternitySecret.isPregnancySecretForPartner = false;

        if (!rec->paternitySecret.isSecret && !rec->paternitySecret.discovered) {
            rec->presumedFather = ActorId{};
        } else if (rec->presumedFather.IsNull() ||
                   rec->presumedFather == motherId ||
                   (!rec->bioFather.IsNull() && rec->presumedFather == rec->bioFather)) {
            const ActorId partnerId = ws.relationships.Read().GetPrimaryPartner(motherId);
            if (!partnerId.IsNull() &&
                partnerId != motherId &&
                (rec->bioFather.IsNull() || partnerId != rec->bioFather)) {
                rec->presumedFather = partnerId;
            } else {
                rec->presumedFather = ActorId{};
            }
        }

        const ActorId resolvedPresumedFather = rec->presumedFather;
        if (resolvedPresumedFather.IsNull() && rec->paternitySecret.isSecretForPartner) {
            rec->paternitySecret.isSecretForPartner = false;
            if (!rec->paternitySecret.isSecretForBioFather) {
                rec->paternitySecret.isSecret = false;
                rec->paternitySecret.discovered = true;
            }
        }

        const ActorId recBioFather = rec->bioFather;
        const bool mismatch = rec->paternitySecret.hasPhenotypeMismatch;
        const bool secretForPartner = rec->paternitySecret.isSecretForPartner;
        const bool secretForBioFather = rec->paternitySecret.isSecretForBioFather;

        EvaluateChildBeastbloodInheritance(*rec, motherIsWerewolf, fatherIsWerewolf);
        const auto birthMut = ws.ApplyBirthCustodyRules(
            chosenId, motherId, resolvedPresumedFather, recBioFather, meta, rng);

        out.childId = chosenId;
        out.promoted = true;
        out.bioFather = recBioFather;
        out.presumedFather = resolvedPresumedFather;
        out.isSecretForPartner = secretForPartner;
        out.isSecretForBioFather = secretForBioFather;
        MarkUnique(out.dirtyActors, motherId);
        MarkUnique(out.dirtyActors, prevCoGuardian);
        for (ActorId d : birthMut.dirtyActors) {
            MarkUnique(out.dirtyActors, d);
        }
        MarkUniqueChild(out.dirtyChildren, chosenId);
        for (ChildId d : birthMut.dirtyChildren) {
            MarkUniqueChild(out.dirtyChildren, d);
        }

        if (mismatch && secretForPartner) {
            const float birthChance =
                static_cast<float>(ws.settings.Read().paternityPhenotypeBirthChance);
            out.evaluateAtBirth = rng.RollPercent(birthChance);
        }

        return out;
    }

    std::vector<ChildId> ApplyRemoveUnbornChild(
        State::WorldState& ws,
        ActorId motherId,
        ActorId bioFatherId,
        std::vector<ActorId>* outAffectedGuardians) {
        std::vector<ChildId> removed;
        if (motherId.IsNull()) {
            return removed;
        }
        const auto unbornIds = ws.lineage.Read().GetUnbornOfMother(motherId);
        if (unbornIds.empty()) {
            return removed;
        }

        auto recordAffected = [&](const State::ChildRecord& rec) {
            if (!outAffectedGuardians) {
                return;
            }
            for (ActorId g : rec.guardians.AsArray()) {
                MarkUnique(*outAffectedGuardians, g);
            }
            MarkUnique(*outAffectedGuardians, rec.bioMother);
        };

        if (!bioFatherId.IsNull()) {
            for (ChildId uId : unbornIds) {
                const auto* rec = ws.lineage.Read().FindChild(uId);
                if (rec && rec->bioFather == bioFatherId) {
                    recordAffected(*rec);
                    if (ws.lineage.Write().EraseChild(uId)) {
                        removed.push_back(uId);
                    }
                    break;
                }
            }
            return removed;
        }

        for (ChildId uId : unbornIds) {
            if (const auto* rec = ws.lineage.Read().FindChild(uId)) {
                recordAffected(*rec);
            }
            if (ws.lineage.Write().EraseChild(uId)) {
                removed.push_back(uId);
            }
        }
        return removed;
    }

    PaternityRevealOutcome ApplyResolvePaternitySecret(
        State::WorldState& ws,
        ChildId childId,
        std::string_view trigger) {
        PaternityRevealOutcome out{};
        auto* rec = ws.lineage.Write().FindChildMutable(childId);
        if (!rec || !rec->paternitySecret.HasActiveSecret()) {
            return out;
        }

        const bool wasSecretBio = rec->paternitySecret.isSecretForBioFather;
        const bool wasSecretPartner = rec->paternitySecret.isSecretForPartner;
        const bool wasPregSecret = rec->paternitySecret.isPregnancySecretForPartner;
        const bool isUnborn = (rec->stage == ChildStage::Unborn);
        const ActorId motherId = rec->bioMother;
        const ActorId bioFatherId = rec->bioFather;

        rec->paternitySecret.isSecretForBioFather = false;
        rec->paternitySecret.isSecretForPartner = false;
        rec->paternitySecret.isPregnancySecretForPartner = false;
        rec->paternitySecret.isSecret = false;
        rec->paternitySecret.discovered = true;
        rec->paternitySecret.discoveredByPartner = true;
        rec->paternitySecret.discoveredByBioFather = true;
        MarkUniqueChild(out.affectedChildren, childId);

        if (!motherId.IsNull()) {
            for (ChildId sibId : ws.lineage.Read().GetUnbornOfMother(motherId)) {
                if (sibId == childId) {
                    continue;
                }
                auto* sib = ws.lineage.Write().FindChildMutable(sibId);
                if (!sib) {
                    continue;
                }
                bool changedSib = false;
                if (sib->paternitySecret.isPregnancySecretForPartner) {
                    sib->paternitySecret.isPregnancySecretForPartner = false;
                    changedSib = true;
                }
                if (sib->bioFather == bioFatherId) {
                    sib->paternitySecret.isSecretForBioFather = false;
                    sib->paternitySecret.isSecretForPartner = false;
                    sib->paternitySecret.isSecret = false;
                    sib->paternitySecret.discovered = true;
                    sib->paternitySecret.discoveredByPartner = true;
                    sib->paternitySecret.discoveredByBioFather = true;
                    changedSib = true;
                }
                if (changedSib) {
                    MarkUniqueChild(out.affectedChildren, sibId);
                }
            }
        }

        const bool isConfess = ContainsTriggerWord(trigger, "confess");
        std::string baseOutcome;
        if (wasSecretPartner && wasSecretBio) {
            baseOutcome = isConfess ? "mother_confessed_both" : "presumed_father_learnt";
            out.learnedPartner = true;
            out.learnedBioFather = true;
        } else if (wasSecretPartner) {
            baseOutcome = isConfess ? "mother_confessed_presumed" : "presumed_father_learnt";
            out.learnedPartner = true;
        } else {
            baseOutcome = isConfess ? "mother_confessed_bio" : "bio_father_learnt";
            out.learnedBioFather = true;
        }

        const std::string suffix =
            (isUnborn && wasSecretPartner && wasPregSecret) ? "_unannounced" : "";
        out.modEventOutcome = baseOutcome + suffix;
        out.memoryOutcome = baseOutcome;
        out.wasPregnancySecretForPartner = wasSecretPartner && wasPregSecret;
        out.updated = true;
        return out;
    }

    PaternityRevealOutcome ApplyResolvePaternitySecretForPartner(
        State::WorldState& ws,
        ChildId childId,
        std::string_view trigger) {
        PaternityRevealOutcome out{};
        auto* rec = ws.lineage.Write().FindChildMutable(childId);
        if (!rec || (!rec->paternitySecret.isSecretForPartner && !rec->paternitySecret.isPregnancySecretForPartner)) {
            return out;
        }

        const bool wasPregSecret = rec->paternitySecret.isPregnancySecretForPartner;
        const bool isUnborn = (rec->stage == ChildStage::Unborn);
        const ActorId motherId = rec->bioMother;
        const ActorId bioFatherId = rec->bioFather;

        rec->paternitySecret.isSecretForPartner = false;
        rec->paternitySecret.isPregnancySecretForPartner = false;
        rec->paternitySecret.discoveredByPartner = true;
        if (!rec->paternitySecret.isSecretForBioFather) {
            rec->paternitySecret.isSecret = false;
            rec->paternitySecret.discovered = true;
        }
        MarkUniqueChild(out.affectedChildren, childId);

        if (!motherId.IsNull()) {
            for (ChildId sibId : ws.lineage.Read().GetUnbornOfMother(motherId)) {
                if (sibId == childId) {
                    continue;
                }
                auto* sib = ws.lineage.Write().FindChildMutable(sibId);
                if (!sib) {
                    continue;
                }
                bool changedSib = false;
                if (sib->paternitySecret.isPregnancySecretForPartner) {
                    sib->paternitySecret.isPregnancySecretForPartner = false;
                    changedSib = true;
                }
                if (sib->bioFather == bioFatherId && sib->paternitySecret.isSecretForPartner) {
                    sib->paternitySecret.isSecretForPartner = false;
                    sib->paternitySecret.discoveredByPartner = true;
                    if (!sib->paternitySecret.isSecretForBioFather) {
                        sib->paternitySecret.isSecret = false;
                        sib->paternitySecret.discovered = true;
                    }
                    changedSib = true;
                }
                if (changedSib) {
                    MarkUniqueChild(out.affectedChildren, sibId);
                }
            }
        }

        const bool isConfess = ContainsTriggerWord(trigger, "confess");
        const std::string baseOutcome =
            isConfess ? "mother_confessed_presumed" : "presumed_father_learnt";
        const std::string suffix = (isUnborn && wasPregSecret) ? "_unannounced" : "";
        out.modEventOutcome = baseOutcome + suffix;
        out.memoryOutcome = baseOutcome;
        out.learnedPartner = true;
        out.wasPregnancySecretForPartner = wasPregSecret;
        out.updated = true;
        return out;
    }

    PaternityRevealOutcome ApplyRevealPregnancyToPartner(
        State::WorldState& ws,
        ChildId childId,
        std::string_view trigger) {
        PaternityRevealOutcome out{};
        auto* rec = ws.lineage.Write().FindChildMutable(childId);
        if (!rec || !rec->paternitySecret.isPregnancySecretForPartner) {
            return out;
        }

        const ActorId motherId = rec->bioMother;
        rec->paternitySecret.isPregnancySecretForPartner = false;
        MarkUniqueChild(out.affectedChildren, childId);

        if (!motherId.IsNull()) {
            for (ChildId sibId : ws.lineage.Read().GetUnbornOfMother(motherId)) {
                if (sibId == childId) {
                    continue;
                }
                if (auto* sib = ws.lineage.Write().FindChildMutable(sibId);
                    sib && sib->paternitySecret.isPregnancySecretForPartner) {
                    sib->paternitySecret.isPregnancySecretForPartner = false;
                    MarkUniqueChild(out.affectedChildren, sibId);
                }
            }
        }

        const bool isConfessOrShared =
            ContainsTriggerWord(trigger, "confess") || ContainsTriggerWord(trigger, "share");
        out.modEventOutcome =
            isConfessOrShared ? "mother_shared_pregnancy_presumed" : "pregnancy_visible_presumed";
        out.memoryOutcome = "mother_shared_pregnancy_presumed";
        out.revealedPregnancyOnly = true;
        out.updated = true;
        return out;
    }

    PaternityRevealOutcome ApplyResolvePaternitySecretForBioFather(
        State::WorldState& ws,
        ChildId childId,
        std::string_view trigger) {
        PaternityRevealOutcome out{};
        auto* rec = ws.lineage.Write().FindChildMutable(childId);
        if (!rec || !rec->paternitySecret.isSecretForBioFather) {
            return out;
        }

        const ActorId motherId = rec->bioMother;
        const ActorId bioFatherId = rec->bioFather;
        rec->paternitySecret.isSecretForBioFather = false;
        rec->paternitySecret.discoveredByBioFather = true;
        if (!rec->paternitySecret.isSecretForPartner) {
            rec->paternitySecret.isSecret = false;
            rec->paternitySecret.discovered = true;
        }
        MarkUniqueChild(out.affectedChildren, childId);

        if (!motherId.IsNull()) {
            for (ChildId sibId : ws.lineage.Read().GetUnbornOfMother(motherId)) {
                if (sibId == childId) {
                    continue;
                }
                auto* sib = ws.lineage.Write().FindChildMutable(sibId);
                if (sib && sib->bioFather == bioFatherId && sib->paternitySecret.isSecretForBioFather) {
                    sib->paternitySecret.isSecretForBioFather = false;
                    sib->paternitySecret.discoveredByBioFather = true;
                    if (!sib->paternitySecret.isSecretForPartner) {
                        sib->paternitySecret.isSecret = false;
                        sib->paternitySecret.discovered = true;
                    }
                    MarkUniqueChild(out.affectedChildren, sibId);
                }
            }
        }

        const bool isConfess = ContainsTriggerWord(trigger, "confess");
        const std::string baseOutcome = isConfess ? "mother_confessed_bio" : "bio_father_learnt";
        out.modEventOutcome = baseOutcome;
        out.memoryOutcome = baseOutcome;
        out.learnedBioFather = true;
        out.updated = true;
        return out;
    }

    PaternityRevealOutcome ApplySetPaternitySecretEdges(
        State::WorldState& ws,
        ChildId childId,
        std::optional<bool> secretForBioFather,
        std::optional<bool> secretForPartner,
        std::optional<bool> pregnancySecretForPartner,
        bool bypassCooldown) {
        PaternityRevealOutcome out{};
        auto* rec = ws.lineage.Write().FindChildMutable(childId);
        if (!rec) {
            return out;
        }

        const bool prevBio = rec->paternitySecret.isSecretForBioFather;
        const bool prevPartner = rec->paternitySecret.isSecretForPartner;
        const bool prevPreg = rec->paternitySecret.isPregnancySecretForPartner;
        const bool isUnborn = (rec->stage == ChildStage::Unborn);
        const ActorId motherId = rec->bioMother;
        const ActorId bioFatherId = rec->bioFather;

        if (secretForBioFather.has_value()) {
            rec->paternitySecret.isSecretForBioFather = *secretForBioFather;
            if (!*secretForBioFather && prevBio) {
                rec->paternitySecret.discoveredByBioFather = true;
            }
        }
        if (secretForPartner.has_value()) {
            rec->paternitySecret.isSecretForPartner = *secretForPartner;
            if (!*secretForPartner && prevPartner) {
                rec->paternitySecret.discoveredByPartner = true;
                rec->paternitySecret.isPregnancySecretForPartner = false;
            }
        }
        if (pregnancySecretForPartner.has_value()) {
            rec->paternitySecret.isPregnancySecretForPartner = *pregnancySecretForPartner;
        }

        const bool nowBio = rec->paternitySecret.isSecretForBioFather;
        const bool nowPartner = rec->paternitySecret.isSecretForPartner;
        const bool nowPreg = rec->paternitySecret.isPregnancySecretForPartner;
        rec->paternitySecret.isSecret = (nowBio || nowPartner || nowPreg);
        if (!rec->paternitySecret.isSecret) {
            rec->paternitySecret.discovered = true;
        }
        if (bypassCooldown) {
            rec->paternitySecret.lastCheckTimestamp = 0.0f;
        }

        const bool changed = (nowBio != prevBio) || (nowPartner != prevPartner) || (nowPreg != prevPreg);
        if (!changed) {
            return out;
        }

        MarkUniqueChild(out.affectedChildren, childId);
        out.learnedBioFather = (prevBio && !nowBio);
        out.learnedPartner = (prevPartner && !nowPartner);
        out.wasPregnancySecretForPartner = (prevPartner && prevPreg && !nowPartner);
        out.revealedPregnancyOnly = (!out.learnedPartner && !out.learnedBioFather && prevPreg && !nowPreg);
        out.updated = true;

        if (!motherId.IsNull() && (!nowPreg || !nowBio || !nowPartner)) {
            for (ChildId sibId : ws.lineage.Read().GetUnbornOfMother(motherId)) {
                if (sibId == childId) {
                    continue;
                }
                auto* sib = ws.lineage.Write().FindChildMutable(sibId);
                if (!sib) {
                    continue;
                }
                bool changedSib = false;
                if (!nowPreg && sib->paternitySecret.isPregnancySecretForPartner) {
                    sib->paternitySecret.isPregnancySecretForPartner = false;
                    changedSib = true;
                }
                if (sib->bioFather == bioFatherId) {
                    if (!nowBio && sib->paternitySecret.isSecretForBioFather) {
                        sib->paternitySecret.isSecretForBioFather = false;
                        sib->paternitySecret.discoveredByBioFather = true;
                        changedSib = true;
                    }
                    if (!nowPartner && sib->paternitySecret.isSecretForPartner) {
                        sib->paternitySecret.isSecretForPartner = false;
                        sib->paternitySecret.discoveredByPartner = true;
                        changedSib = true;
                    }
                    if (!sib->paternitySecret.isSecretForBioFather &&
                        !sib->paternitySecret.isSecretForPartner &&
                        !sib->paternitySecret.isPregnancySecretForPartner) {
                        sib->paternitySecret.isSecret = false;
                        sib->paternitySecret.discovered = true;
                    }
                }
                if (changedSib) {
                    MarkUniqueChild(out.affectedChildren, sibId);
                }
            }
        }

        if (out.learnedBioFather && out.learnedPartner) {
            out.memoryOutcome = "mother_confessed_both";
            out.modEventOutcome = out.wasPregnancySecretForPartner && isUnborn
                                      ? "mother_confessed_both_unannounced"
                                      : "mother_confessed_both";
        } else if (out.learnedBioFather) {
            out.memoryOutcome = "bio_father_learnt";
            out.modEventOutcome = "bio_father_learnt";
        } else if (out.learnedPartner) {
            out.memoryOutcome = "presumed_father_learnt";
            out.modEventOutcome = out.wasPregnancySecretForPartner && isUnborn
                                      ? "presumed_father_learnt_unannounced"
                                      : "presumed_father_learnt";
        } else if (out.revealedPregnancyOnly) {
            out.memoryOutcome = "mother_shared_pregnancy_presumed";
            out.modEventOutcome = "mother_shared_pregnancy_presumed";
        }

        return out;
    }

    std::vector<ActorId> CollectBioFatherStepInCandidates(
        const State::WorldStateSnapshot& snap,
        ActorId motherId,
        const State::IActorMetadataProvider& meta) {
        std::vector<ActorId> candidates;
        if (!snap.lineage || !snap.relationships || motherId.IsNull() || motherId.IsPlayer()) {
            return candidates;
        }
        if (!snap.relationships->GetPrimaryPartner(motherId).IsNull()) {
            return candidates;
        }

        for (ChildId childId : snap.lineage->GetUnbornOfMother(motherId)) {
            const auto* rec = snap.lineage->FindChild(childId);
            if (!rec) {
                continue;
            }
            const ActorId bioF = rec->bioFather;
            if (bioF.IsNull() || bioF.IsUnknownFather() || bioF.IsPlayer() || bioF == motherId) {
                continue;
            }
            if (!meta.IsAliveAndValid(bioF)) {
                continue;
            }
            if (!snap.relationships->GetPrimaryPartner(bioF).IsNull()) {
                continue;
            }
            const auto status = snap.relationships->GetStatus(motherId, bioF);
            if (status == Foundation::RelationshipStatus::Divorced ||
                status == Foundation::RelationshipStatus::Jilted ||
                status == Foundation::RelationshipStatus::ExDating ||
                status == Foundation::RelationshipStatus::ExLover ||
                Foundation::IsActivePartnerStatus(status)) {
                continue;
            }
            MarkUnique(candidates, bioF);
        }
        return candidates;
    }

    bool EvaluatePaternitySecretDailyRoll(
        const State::ChildRecord& child,
        const State::SettingsData& settings,
        float currentGameDay,
        Foundation::IRng& rng) noexcept {
        if (!child.paternitySecret.HasActiveSecret() || !child.paternitySecret.isSecretForPartner) {
            return false;
        }
        const float startDay =
            child.birthTimestamp > 0.0f ? child.birthTimestamp : child.conceptionTimestamp;
        const float daysElapsed = std::max(0.0f, currentGameDay - startDay);
        const bool mismatch = child.paternitySecret.hasPhenotypeMismatch;
        const float baseChance = static_cast<float>(
            mismatch ? settings.paternityPhenotypeBaseChance : settings.paternityBaseChance);
        const float dailyGrowth =
            mismatch ? settings.paternityPhenotypeDailyGrowth : settings.paternityDailyGrowth;
        const float maxChance = static_cast<float>(
            mismatch ? settings.paternityPhenotypeMaxChance : settings.paternityMaxChance);
        const float chance = std::min(maxChance, baseChance + daysElapsed * dailyGrowth);
        return rng.RollPercent(chance);
    }

    PaternityFallbackOutcome ApplyPaternitySecretFallback(
        State::WorldState& ws,
        ChildId childId,
        float currentGameDay,
        const State::IActorMetadataProvider& meta,
        Foundation::IRng& rng) {
        PaternityFallbackOutcome out{};
        const auto* rec = ws.lineage.Read().FindChild(childId);
        if (!rec) {
            return out;
        }

        const ActorId motherId = rec->bioMother;
        const ActorId partnerId = ws.GetMotherPartner(childId);
        const ActorId bioFatherId = rec->bioFather;
        if (motherId.IsNull() || partnerId.IsNull() || bioFatherId.IsNull()) {
            return out;
        }

        out.evaluated = true;
        out.motherId = motherId;
        out.partnerId = partnerId;
        out.bioFatherId = bioFatherId;

        const float revealChance =
            static_cast<float>(ws.settings.Read().paternityNonSkyrimNetRevealChance);
        const bool shouldReveal =
            rec->paternitySecret.hasPhenotypeMismatch ? true : rng.RollPercent(revealChance);

        if (!shouldReveal) {
            return out;
        }

        out.revealed = true;
        out.revealOutcome = ApplyResolvePaternitySecret(ws, childId, "fallback");
        out.modEventOutcome = out.revealOutcome.modEventOutcome;

        const bool isOpen = ws.relationships.Read().IsPairOpen(motherId, partnerId);
        if (!isOpen) {
            const float breakupChance =
                static_cast<float>(ws.settings.Read().paternityBreakupChance);
            if (rng.RollPercent(breakupChance)) {
                out.brokeUp = true;
                const auto curStatus = ws.relationships.Read().GetStatus(motherId, partnerId);
                Foundation::RelationshipStatus breakupTarget = Foundation::RelationshipStatus::ExLover;
                if (curStatus == Foundation::RelationshipStatus::Married) {
                    breakupTarget = Foundation::RelationshipStatus::Divorced;
                } else if (curStatus == Foundation::RelationshipStatus::Engaged) {
                    breakupTarget = Foundation::RelationshipStatus::Jilted;
                } else if (curStatus == Foundation::RelationshipStatus::Dating) {
                    breakupTarget = Foundation::RelationshipStatus::ExDating;
                }
                out.relationshipMutation = ws.BreakupPair(
                    motherId, partnerId, breakupTarget, currentGameDay, partnerId, meta, &rng);
            }
        }
        return out;
    }

}  // namespace MARAS::Domain::Lineage
