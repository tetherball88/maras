#include "maras/domain/household/DeceasedQueries.h"

#include <algorithm>

namespace MARAS::Domain::Household {

    namespace {
        [[nodiscard]] bool MatchesPartnerEntry(
            const State::DeceasedPartnerEntry& p,
            ActorId partnerActor,
            BaseId partnerBase,
            RefId partnerRef) noexcept {
            if (p.isPlayer &&
                (partnerActor.IsPlayer() ||
                 partnerActor.raw() == Foundation::kPlayerBaseRawId ||
                 partnerBase.raw() == Foundation::kPlayerBaseRawId ||
                 partnerRef.IsPlayer())) {
                return true;
            }
            if (!p.partnerRefId.IsNull() &&
                (p.partnerRefId.raw() == partnerActor.raw() ||
                 (!partnerRef.IsNull() && p.partnerRefId == partnerRef))) {
                return true;
            }
            if (!p.partnerBaseId.IsNull() &&
                (p.partnerBaseId.raw() == partnerActor.raw() ||
                 (!partnerBase.IsNull() && p.partnerBaseId == partnerBase))) {
                return true;
            }
            return false;
        }

        [[nodiscard]] bool MatchesChildParentOrGuardian(
            const State::DeceasedChildMetadata& cInfo,
            ActorId parentActor,
            BaseId parentBase,
            RefId parentRef) noexcept {
            if (parentActor.IsNull() || parentActor.IsUnknownFather()) {
                return false;
            }
            if (parentActor.IsPlayer() ||
                parentActor.raw() == Foundation::kPlayerBaseRawId ||
                parentBase.raw() == Foundation::kPlayerRefRawId ||
                parentBase.raw() == Foundation::kPlayerBaseRawId) {
                auto isPlayerStored = [](std::uint32_t raw) noexcept {
                    return raw == Foundation::kPlayerRefRawId || raw == Foundation::kPlayerBaseRawId;
                };
                return isPlayerStored(cInfo.guardianARefId.raw()) ||
                       isPlayerStored(cInfo.guardianBRefId.raw()) ||
                       isPlayerStored(cInfo.guardianABaseId.raw()) ||
                       isPlayerStored(cInfo.guardianBBaseId.raw()) ||
                       isPlayerStored(cInfo.bioMotherBaseId.raw()) ||
                       isPlayerStored(cInfo.bioFatherBaseId.raw());
            }

            auto matchesId = [&](std::uint32_t stored) noexcept {
                if (stored == 0 || stored == Foundation::kUnknownFatherRawId) {
                    return false;
                }
                if (stored == parentActor.raw()) return true;
                if (!parentRef.IsNull() && stored == parentRef.raw()) return true;
                if (!parentBase.IsNull() && stored == parentBase.raw()) return true;
                return false;
            };

            return matchesId(cInfo.guardianARefId.raw()) ||
                   matchesId(cInfo.guardianBRefId.raw()) ||
                   matchesId(cInfo.guardianABaseId.raw()) ||
                   matchesId(cInfo.guardianBBaseId.raw()) ||
                   matchesId(cInfo.bioMotherBaseId.raw()) ||
                   matchesId(cInfo.bioFatherBaseId.raw());
        }

        template <typename Pred>
        [[nodiscard]] bool MatchesDeceasedPartnerPredicate(
            const State::WorldStateSnapshot& snap,
            ActorId deceasedActor,
            ActorId partnerActor,
            BaseId deceasedBase,
            BaseId partnerBase,
            RefId partnerRef,
            Pred&& pred) noexcept {
            if (!snap.deceased || deceasedActor.IsNull() || partnerActor.IsNull()) {
                return false;
            }
            const auto* rec = snap.deceased->FindByActor(deceasedActor, deceasedBase);
            if (!rec || rec->isChild) {
                return false;
            }
            for (const auto& p : rec->partnersAtDeath) {
                if (pred(p.statusAtDeath) && MatchesPartnerEntry(p, partnerActor, partnerBase, partnerRef)) {
                    return true;
                }
            }
            return false;
        }
    }  // namespace

    bool IsDeceased(
        const State::WorldStateSnapshot& snap,
        ActorId actorId,
        BaseId optionalKnownBase) noexcept {
        if (!snap.deceased || actorId.IsNull()) {
            return false;
        }
        return snap.deceased->IsDeceased(actorId, optionalKnownBase);
    }

    bool IsWidowOrWidower(
        const State::WorldStateSnapshot& snap,
        ActorId survivingActor) noexcept {
        if (!snap.deceased || survivingActor.IsNull()) {
            return false;
        }
        return snap.deceased->IsWidowOrWidower(survivingActor);
    }

    bool WasMarriedTo(
        const State::WorldStateSnapshot& snap,
        ActorId deceasedActor,
        ActorId partnerActor,
        BaseId deceasedBase,
        BaseId partnerBase,
        RefId partnerRef) noexcept {
        return MatchesDeceasedPartnerPredicate(
            snap, deceasedActor, partnerActor, deceasedBase, partnerBase, partnerRef,
            [](RelationshipStatus st) noexcept { return st == RelationshipStatus::Married; });
    }

    bool WasEngagedTo(
        const State::WorldStateSnapshot& snap,
        ActorId deceasedActor,
        ActorId partnerActor,
        BaseId deceasedBase,
        BaseId partnerBase,
        RefId partnerRef) noexcept {
        return MatchesDeceasedPartnerPredicate(
            snap, deceasedActor, partnerActor, deceasedBase, partnerBase, partnerRef,
            [](RelationshipStatus st) noexcept { return st == RelationshipStatus::Engaged; });
    }

    bool WasExOf(
        const State::WorldStateSnapshot& snap,
        ActorId deceasedActor,
        ActorId partnerActor,
        BaseId deceasedBase,
        BaseId partnerBase,
        RefId partnerRef) noexcept {
        return MatchesDeceasedPartnerPredicate(
            snap, deceasedActor, partnerActor, deceasedBase, partnerBase, partnerRef,
            [](RelationshipStatus st) noexcept { return Foundation::IsBreakupStatus(st); });
    }

    std::vector<BaseId> GetDeceasedSpousesOf(
        const State::WorldStateSnapshot& snap,
        ActorId survivingPartner,
        BaseId partnerBase,
        RefId partnerRef) {
        std::vector<BaseId> out;
        if (!snap.deceased || survivingPartner.IsNull()) {
            return out;
        }
        for (const auto& [key, rec] : snap.deceased->AllRecords()) {
            (void)key;
            if (rec.isChild) {
                continue;
            }
            for (const auto& p : rec.partnersAtDeath) {
                if (p.statusAtDeath == RelationshipStatus::Married &&
                    MatchesPartnerEntry(p, survivingPartner, partnerBase, partnerRef)) {
                    const BaseId id = !rec.baseId.IsNull() ? rec.baseId : BaseId{ rec.refId.raw() };
                    if (!id.IsNull() && std::find(out.begin(), out.end(), id) == out.end()) {
                        out.push_back(id);
                    }
                    break;
                }
            }
        }
        return out;
    }

    std::vector<BaseId> GetDeceasedPartnersOf(
        const State::WorldStateSnapshot& snap,
        ActorId survivingPartner,
        BaseId partnerBase,
        RefId partnerRef) {
        std::vector<BaseId> out;
        if (!snap.deceased || survivingPartner.IsNull()) {
            return out;
        }
        for (const auto& [key, rec] : snap.deceased->AllRecords()) {
            (void)key;
            if (rec.isChild) {
                continue;
            }
            for (const auto& p : rec.partnersAtDeath) {
                if (Foundation::IsActivePartnerStatus(p.statusAtDeath) &&
                    MatchesPartnerEntry(p, survivingPartner, partnerBase, partnerRef)) {
                    const BaseId id = !rec.baseId.IsNull() ? rec.baseId : BaseId{ rec.refId.raw() };
                    if (!id.IsNull() && std::find(out.begin(), out.end(), id) == out.end()) {
                        out.push_back(id);
                    }
                    break;
                }
            }
        }
        return out;
    }

    std::vector<ChildId> GetDeceasedChildrenOf(
        const State::WorldStateSnapshot& snap,
        ActorId parentActor,
        BaseId parentBase,
        RefId parentRef) {
        std::vector<ChildId> out;
        if (!snap.deceased || parentActor.IsNull()) {
            return out;
        }
        for (const auto& [key, rec] : snap.deceased->AllRecords()) {
            (void)key;
            if (!rec.childInfo.has_value()) {
                continue;
            }
            if (MatchesChildParentOrGuardian(*rec.childInfo, parentActor, parentBase, parentRef)) {
                const std::uint32_t raw = !rec.refId.IsNull() ? rec.refId.raw() : rec.baseId.raw();
                if (raw != 0) {
                    const ChildId cid{ raw };
                    if (std::find(out.begin(), out.end(), cid) == out.end()) {
                        out.push_back(cid);
                    }
                }
            }
        }
        return out;
    }

}  // namespace MARAS::Domain::Household
