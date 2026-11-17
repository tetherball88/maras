#include "maras/state/CoSaveCodec.h"

#include "maras/foundation/Logging.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace MARAS::State {

    namespace {

        constexpr std::uint32_t kMaxSerializedEntries = 100000u;
        constexpr std::uint32_t kMaxHouses = 1000u;
        constexpr std::uint32_t kMaxTenants = 10000u;
        constexpr std::uint32_t kMaxSharedCells = 10000u;
        constexpr std::uint32_t kMaxChildStringLen = 512u;
        constexpr std::uint32_t kMaxDeceasedStringLen = 4096u;
        constexpr std::uint32_t kMaxVersionStringLen = 64u;
        constexpr std::uint32_t kMaxTypesPerActor = 256u;
        constexpr float kMinAdultChildStayDays = 2.0f;
        constexpr float kMaxAdultChildStayDays = 4.0f;

        class TrackingFormIdResolver final : public IFormIdResolver {
        public:
            explicit TrackingFormIdResolver(const IFormIdResolver& inner) noexcept
                : inner_(inner) {}

            [[nodiscard]] bool ResolveFormId(std::uint32_t oldRawId, std::uint32_t& outNewRawId) const noexcept override {
                if (oldRawId == 0) {
                    outNewRawId = 0;
                    return false;
                }
                const bool ok = inner_.ResolveFormId(oldRawId, outNewRawId);
                if (ok && outNewRawId != 0) {
                    ++resolvedCount_;
                    if (outNewRawId != oldRawId) {
                        ++remappedCount_;
                    }
                } else {
                    ++unresolvedCount_;
                }
                return ok;
            }

            [[nodiscard]] ActorSex GetSex(ActorId actor) const noexcept override { return inner_.GetSex(actor); }
            [[nodiscard]] bool IsAliveAndValid(ActorId actor) const noexcept override { return inner_.IsAliveAndValid(actor); }
            [[nodiscard]] bool IsDynamicActorPresent(RefId refId) const noexcept override { return inner_.IsDynamicActorPresent(refId); }
            [[nodiscard]] std::string GetActorName(ActorId actor) const override { return inner_.GetActorName(actor); }
            [[nodiscard]] FormRawId GetActorRaceId(ActorId actor) const noexcept override { return inner_.GetActorRaceId(actor); }
            [[nodiscard]] BaseId GetActorBaseId(ActorId actor) const noexcept override { return inner_.GetActorBaseId(actor); }

            [[nodiscard]] std::uint32_t ResolvedCount() const noexcept { return resolvedCount_; }
            [[nodiscard]] std::uint32_t RemappedCount() const noexcept { return remappedCount_; }
            [[nodiscard]] std::uint32_t UnresolvedCount() const noexcept { return unresolvedCount_; }

        private:
            const IFormIdResolver& inner_;
            mutable std::uint32_t resolvedCount_{ 0 };
            mutable std::uint32_t remappedCount_{ 0 };
            mutable std::uint32_t unresolvedCount_{ 0 };
        };

        class BoundedChunkReader {
        public:
            BoundedChunkReader(ICoSaveReader& reader, std::uint32_t chunkLength) noexcept
                : reader_(reader), chunkLength_(chunkLength) {}

            [[nodiscard]] bool HasRemaining(std::uint32_t needed) const noexcept {
                return bytesRead_ <= chunkLength_ && needed <= (chunkLength_ - bytesRead_);
            }

            [[nodiscard]] std::uint32_t BytesRead() const noexcept { return bytesRead_; }
            [[nodiscard]] std::uint32_t ChunkLength() const noexcept { return chunkLength_; }

            template <typename T>
                requires std::is_trivially_copyable_v<T>
            bool ReadValue(T& outVal) {
                constexpr auto needed = static_cast<std::uint32_t>(sizeof(T));
                if (!HasRemaining(needed)) {
                    return false;
                }
                if (!reader_.ReadBytes(&outVal, needed)) {
                    return false;
                }
                bytesRead_ += needed;
                return true;
            }

            bool ReadBuffer(void* dst, std::uint32_t len) {
                if (len == 0) {
                    return true;
                }
                if (!HasRemaining(len)) {
                    return false;
                }
                if (!reader_.ReadBytes(dst, len)) {
                    return false;
                }
                bytesRead_ += len;
                return true;
            }

            bool ReadLengthPrefixedString(std::string& outStr, std::uint32_t maxLen) {
                std::uint32_t len = 0;
                if (!ReadValue(len) || len > maxLen || !HasRemaining(len)) {
                    return false;
                }
                if (len > 0) {
                    outStr.resize(len);
                    return ReadBuffer(outStr.data(), len);
                }
                outStr.clear();
                return true;
            }

        private:
            ICoSaveReader& reader_;
            std::uint32_t chunkLength_{ 0 };
            std::uint32_t bytesRead_{ 0 };
        };

        [[nodiscard]] bool WriteLengthPrefixedString(ICoSaveWriter& writer, std::string_view str) {
            const auto len = static_cast<std::uint32_t>(str.size());
            if (!writer.WritePod(len)) {
                return false;
            }
            if (len > 0 && !writer.WriteBytes(str.data(), len)) {
                return false;
            }
            return true;
        }

        [[nodiscard]] bool ResolveActorOrSpecialRaw(
            const IFormIdResolver& resolver,
            std::uint32_t oldRaw,
            std::uint32_t& outNewRaw,
            bool allowZero = false,
            bool allowUnknownFather = false,
            bool allowSyntheticChild = false,
            bool allowDynamicRuntime = true) noexcept {
            if (oldRaw == 0) {
                outNewRaw = 0;
                return allowZero;
            }
            if (oldRaw == Foundation::kPlayerRefRawId || oldRaw == Foundation::kPlayerBaseRawId) {
                outNewRaw = oldRaw;
                return true;
            }
            if (oldRaw == Foundation::kUnknownFatherRawId) {
                outNewRaw = oldRaw;
                return allowUnknownFather;
            }
            if (Foundation::IsSyntheticChildRawId(oldRaw)) {
                outNewRaw = oldRaw;
                return allowSyntheticChild;
            }
            if (Foundation::IsDynamicPrefixRawId(oldRaw)) {
                if (!allowDynamicRuntime || oldRaw == 0xFFFFFFFFu) {
                    outNewRaw = 0;
                    return false;
                }
                outNewRaw = oldRaw;
                return true;
            }
            return resolver.ResolveFormId(oldRaw, outNewRaw) && outNewRaw != 0;
        }

        // Rebuilds HouseholdTable keeping all slices except the houses/tenants slice
        [[nodiscard]] HouseholdTable CloneHouseholdWithoutHouses(const HouseholdTable& src) {
            HouseholdTable dst{};
            for (const auto& [cellId, cellRec] : src.AllSharedCells()) {
                dst.UpsertSharedCell(cellId, cellRec);
            }
            for (const auto& [bedRef, ownerId] : src.AllOriginalBedOwners()) {
                dst.SetOriginalBedOwner(bedRef, ownerId);
            }
            dst.SetChildChestIntroduced(src.IsChildChestIntroduced());
            dst.SetPoorCounter(src.GetPoorCounter());
            dst.SetNameCallingUsedLast(src.IsNameCallingUsedLast());
            for (const auto& [cId, cState] : src.AllChildStates()) {
                dst.UpsertChildState(cState);
            }
            for (const auto& [cId, snap] : src.AllChildFactionSnapshots()) {
                dst.UpsertChildFactionSnapshot(cId, snap);
            }
            for (const auto& [cId, travel] : src.AllAdultChildTravel()) {
                dst.UpsertAdultChildTravel(cId, travel);
            }
            return dst;
        }

        // Rebuilds HouseholdTable keeping all slices except the SpouseAssets (SPAS) slice
        [[nodiscard]] HouseholdTable CloneHouseholdWithoutSpouseAssets(const HouseholdTable& src) {
            HouseholdTable dst{};
            for (const auto& h : src.AllHouses()) {
                dst.RegisterHouse(h.houseId, h.markerId, h.initialMoveDone);
                for (const std::uint32_t tid : h.tenantOrder) {
                    dst.AssignTenant(tid, h.houseId, src.GetTenantStayAtHome(tid), false);
                }
            }
            dst.SetChildChestIntroduced(src.IsChildChestIntroduced());
            dst.SetPoorCounter(src.GetPoorCounter());
            dst.SetNameCallingUsedLast(src.IsNameCallingUsedLast());
            for (const auto& [cId, cState] : src.AllChildStates()) {
                dst.UpsertChildState(cState);
            }
            for (const auto& [cId, snap] : src.AllChildFactionSnapshots()) {
                dst.UpsertChildFactionSnapshot(cId, snap);
            }
            for (const auto& [cId, travel] : src.AllAdultChildTravel()) {
                dst.UpsertAdultChildTravel(cId, travel);
            }
            return dst;
        }

        // =====================================================================
        // SAVE CHUNK HELPERS (v7 Canonical Format)
        // =====================================================================

        bool SaveChunkNPCR(const WorldState& state, ICoSaveWriter& writer) {
            if (!writer.OpenRecord(CoSaveConstants::kChunkNPCR, CoSaveConstants::kDataVersion)) {
                return false;
            }
            if (!writer.WritePod(CoSaveConstants::kMagicNumber)) {
                return false;
            }

            const ActorProfileTable& actors = state.actors.Read();
            const RelationshipEdgeTable& rels = state.relationships.Read();
            const AffairLedger& affairs = state.affairs.Read();
            const LineageTable& lineage = state.lineage.Read();

            // 1. Profiles (sorted by RefId for deterministic binary output)
            std::vector<RefId> profileIds;
            profileIds.reserve(actors.Size());
            for (const auto& [refId, prof] : actors.AllProfiles()) {
                (void)prof;
                if (!refId.IsNull()) {
                    profileIds.push_back(refId);
                }
            }
            std::sort(profileIds.begin(), profileIds.end());

            if (!writer.WritePod(static_cast<std::uint32_t>(profileIds.size()))) {
                return false;
            }
            for (const RefId refId : profileIds) {
                const auto* prof = actors.Find(refId);
                if (!prof) {
                    continue;
                }
                const bool hasOrig = prof->legacyOriginalHome.has_value();
                const bool hasCurr = prof->legacyCurrentHome.has_value();
                const bool hasMarker = prof->legacyHomeMarker.has_value();

                if (!writer.WritePod(refId.raw()) ||
                    !writer.WritePod(static_cast<std::uint8_t>(prof->socialClass)) ||
                    !writer.WritePod(static_cast<std::uint8_t>(prof->skillType)) ||
                    !writer.WritePod(static_cast<std::uint8_t>(prof->temperament)) ||
                    !writer.WritePod(hasOrig) ||
                    (hasOrig && !writer.WritePod(prof->legacyOriginalHome->raw())) ||
                    !writer.WritePod(hasCurr) ||
                    (hasCurr && !writer.WritePod(prof->legacyCurrentHome->raw())) ||
                    !writer.WritePod(hasMarker) ||
                    (hasMarker && !writer.WritePod(prof->legacyHomeMarker->raw()))) {
                    return false;
                }

                std::vector<FormRawId> kws(prof->addedKeywords.begin(), prof->addedKeywords.end());
                std::sort(kws.begin(), kws.end());
                if (!writer.WritePod(static_cast<std::uint32_t>(kws.size()))) {
                    return false;
                }
                for (const FormRawId kw : kws) {
                    if (!writer.WritePod(kw.raw())) {
                        return false;
                    }
                }
            }

            // 2. Relationship Pairs + Status History + Cheating Records
            // Collect union of pairs in RelationshipEdgeTable and AffairLedger so no affair is ever orphaned
            std::vector<CanonicalPairKey> allPairs;
            {
                std::unordered_set<CanonicalPairKey, CanonicalPairKeyHash> pairSet;
                for (const auto& [pair, edge] : rels.AllEdges()) {
                    (void)edge;
                    if (pair.IsValid()) {
                        pairSet.insert(pair);
                    }
                }
                for (const auto& [affId, affRec] : affairs.AllRecords()) {
                    (void)affId;
                    if (affRec.CouplePair().IsValid()) {
                        pairSet.insert(affRec.CouplePair());
                    }
                }
                allPairs.assign(pairSet.begin(), pairSet.end());
                std::sort(allPairs.begin(), allPairs.end());
            }

            if (!writer.WritePod(static_cast<std::uint32_t>(allPairs.size()))) {
                return false;
            }
            for (const CanonicalPairKey& pair : allPairs) {
                const auto* edge = rels.FindEdge(pair);
                const RelationshipStatus st = edge ? edge->status : RelationshipStatus::Candidate;
                const RelationshipStatus origSt = edge ? edge->originalStatus : RelationshipStatus::Unknown;
                const bool isOpen = edge ? edge->isOpen : false;

                if (!writer.WritePod(pair.first.raw()) ||
                    !writer.WritePod(pair.second.raw()) ||
                    !writer.WritePod(static_cast<std::uint8_t>(st)) ||
                    !writer.WritePod(static_cast<std::uint8_t>(origSt)) ||
                    !writer.WritePod(isOpen)) {
                    return false;
                }

                const auto& history = edge ? edge->statusHistory : std::vector<StatusTransitionEntry>{};
                if (!writer.WritePod(static_cast<std::uint32_t>(history.size()))) {
                    return false;
                }
                for (const auto& h : history) {
                    if (!writer.WritePod(static_cast<std::uint8_t>(h.fromStatus)) ||
                        !writer.WritePod(static_cast<std::uint8_t>(h.toStatus)) ||
                        !writer.WritePod(h.timestamp) ||
                        !writer.WritePod(h.initiator.raw())) {
                        return false;
                    }
                }

                const auto pairAffairs = affairs.GetForCouplePair(pair);
                if (!writer.WritePod(static_cast<std::uint32_t>(pairAffairs.size()))) {
                    return false;
                }
                for (const AffairRecord& rec : pairAffairs) {
                    const auto rawLastTs = static_cast<std::uint32_t>(rec.lastTimestamp);
                    if (!writer.WritePod(rec.cheater.raw()) ||
                        !writer.WritePod(rec.deceivedPartner.raw()) ||
                        !writer.WritePod(rec.paramour.raw()) ||
                        !writer.WritePod(rec.incidentCount) ||
                        !writer.WritePod(rawLastTs) ||
                        !writer.WritePod(rec.isSecret) ||
                        !writer.WritePod(static_cast<std::uint8_t>(rec.affairType)) ||
                        !writer.WritePod(static_cast<std::uint8_t>(rec.affairStatus))) {
                        return false;
                    }
                }
            }

            // 3. Pending Deferred Relationships
            std::vector<BaseId> pendingBases;
            pendingBases.reserve(actors.AllPendingRelationships().size());
            for (const auto& [baseId, list] : actors.AllPendingRelationships()) {
                (void)list;
                pendingBases.push_back(baseId);
            }
            std::sort(pendingBases.begin(), pendingBases.end());

            if (!writer.WritePod(static_cast<std::uint32_t>(pendingBases.size()))) {
                return false;
            }
            for (const BaseId baseId : pendingBases) {
                const auto& list = actors.AllPendingRelationships().at(baseId);
                if (!writer.WritePod(baseId.raw()) ||
                    !writer.WritePod(static_cast<std::uint32_t>(list.size()))) {
                    return false;
                }
                for (const auto& p : list) {
                    if (!writer.WritePod(p.partnerBaseId.raw()) ||
                        !writer.WritePod(p.partnerRefId.raw()) ||
                        !writer.WritePod(static_cast<std::uint8_t>(p.targetStatus)) ||
                        !writer.WritePod(static_cast<std::uint8_t>(p.originalStatus))) {
                        return false;
                    }
                }
            }

            // 4. Pending Deferred Children
            std::vector<BaseId> pendingChildBases;
            pendingChildBases.reserve(lineage.AllPendingChildren().size());
            for (const auto& [cBase, pd] : lineage.AllPendingChildren()) {
                (void)pd;
                pendingChildBases.push_back(cBase);
            }
            std::sort(pendingChildBases.begin(), pendingChildBases.end());

            if (!writer.WritePod(static_cast<std::uint32_t>(pendingChildBases.size()))) {
                return false;
            }
            for (const BaseId cBase : pendingChildBases) {
                const auto& pd = lineage.AllPendingChildren().at(cBase);
                if (!writer.WritePod(cBase.raw()) ||
                    !writer.WritePod(pd.childRefId.raw()) ||
                    !writer.WritePod(pd.parentABaseId.raw()) ||
                    !writer.WritePod(pd.parentARefId.raw()) ||
                    !writer.WritePod(pd.parentBBaseId.raw()) ||
                    !writer.WritePod(pd.parentBRefId.raw()) ||
                    !writer.WritePod(pd.isAdopted)) {
                    return false;
                }
            }

            // 5. Child Registry (v7 schema)
            std::vector<ChildId> sortedChildren;
            sortedChildren.reserve(lineage.Size());
            for (const auto& [cId, rec] : lineage.AllChildren()) {
                (void)rec;
                sortedChildren.push_back(cId);
            }
            std::sort(sortedChildren.begin(), sortedChildren.end());

            if (!writer.WritePod(static_cast<std::uint32_t>(sortedChildren.size()))) {
                return false;
            }
            for (const ChildId cId : sortedChildren) {
                const ChildRecord& c = *lineage.FindChild(cId);
                if (!writer.WritePod(c.childId.raw()) ||
                    !writer.WritePod(c.guardians.slotA.raw()) ||
                    !writer.WritePod(c.guardians.slotB.raw()) ||
                    !writer.WritePod(c.timestamp) ||
                    !writer.WritePod(c.isAdopted) ||
                    !writer.WritePod(c.bioMother.raw()) ||
                    !writer.WritePod(c.bioFather.raw()) ||
                    !WriteLengthPrefixedString(writer, c.name) ||
                    !writer.WritePod(c.isBiologicalChild) ||
                    !writer.WritePod(c.conceptionTimestamp) ||
                    !writer.WritePod(c.birthTimestamp) ||
                    !writer.WritePod(c.paternitySecret.isSecret) ||
                    !writer.WritePod(c.paternitySecret.discovered) ||
                    !writer.WritePod(c.paternitySecret.discoveredByPartner) ||
                    !writer.WritePod(c.paternitySecret.hasPhenotypeMismatch) ||
                    !writer.WritePod(c.paternitySecret.lastCheckTimestamp) ||
                    !writer.WritePod(static_cast<std::uint8_t>(c.stage)) ||
                    !WriteLengthPrefixedString(writer, c.trainingLocation) ||
                    !writer.WritePod(c.paternitySecret.isSecretForBioFather) ||
                    !writer.WritePod(c.paternitySecret.isSecretForPartner) ||
                    !writer.WritePod(c.paternitySecret.discoveredByBioFather) ||
                    !writer.WritePod(c.gender) ||
                    !writer.WritePod(c.presumedFather.raw()) ||
                    !writer.WritePod(c.trainingClassIndex) ||
                    !writer.WritePod(c.trainingStartGameDay) ||
                    !WriteLengthPrefixedString(writer, c.savedBioTemplateName) ||
                    !writer.WritePod(c.savedRaceIndex) ||
                    !WriteLengthPrefixedString(writer, c.paternitySecret.bioFatherName) ||
                    !writer.WritePod(c.paternitySecret.bioFatherRaceId.raw())) {
                    return false;
                }
            }

            // 6a. Inline Extension Block 'AGN1' (AffairStage + PendingPartnerIntents)
            if (!writer.WritePod(CoSaveConstants::kExtAgencyAGN1)) {
                return false;
            }
            std::uint32_t stageRecCount = 0;
            for (const CanonicalPairKey& pair : allPairs) {
                stageRecCount += static_cast<std::uint32_t>(affairs.GetForCouplePair(pair).size());
            }
            if (!writer.WritePod(stageRecCount)) {
                return false;
            }
            for (const CanonicalPairKey& pair : allPairs) {
                for (const AffairRecord& rec : affairs.GetForCouplePair(pair)) {
                    if (!writer.WritePod(pair.first.raw()) ||
                        !writer.WritePod(pair.second.raw()) ||
                        !writer.WritePod(rec.cheater.raw()) ||
                        !writer.WritePod(rec.deceivedPartner.raw()) ||
                        !writer.WritePod(rec.paramour.raw()) ||
                        !writer.WritePod(static_cast<std::uint8_t>(rec.affairStage)) ||
                        !writer.WritePod(rec.stageStartDay)) {
                        return false;
                    }
                }
            }

            std::vector<ActorId> intentActors;
            intentActors.reserve(rels.AllPendingIntents().size());
            for (const auto& [partnerId, intent] : rels.AllPendingIntents()) {
                (void)intent;
                intentActors.push_back(partnerId);
            }
            std::sort(intentActors.begin(), intentActors.end());

            if (!writer.WritePod(static_cast<std::uint32_t>(intentActors.size()))) {
                return false;
            }
            for (const ActorId partnerId : intentActors) {
                const auto& intent = rels.AllPendingIntents().at(partnerId);
                if (!writer.WritePod(partnerId.raw()) ||
                    !writer.WritePod(static_cast<std::uint8_t>(intent.type)) ||
                    !writer.WritePod(intent.createdGameDay) ||
                    !writer.WritePod(intent.relatedLover.raw())) {
                    return false;
                }
            }

            // 6b. Inline Extension Block 'PRG1'
            if (!writer.WritePod(CoSaveConstants::kExtPregnancySecretPRG1) ||
                !writer.WritePod(static_cast<std::uint32_t>(sortedChildren.size()))) {
                return false;
            }
            for (const ChildId cId : sortedChildren) {
                const ChildRecord& c = *lineage.FindChild(cId);
                if (!writer.WritePod(c.childId.raw()) ||
                    !writer.WritePod(c.paternitySecret.isPregnancySecretForPartner)) {
                    return false;
                }
            }

            // 6c. Inline Extension Block 'CHD1'
            if (!writer.WritePod(CoSaveConstants::kExtChildVisualRpgCHD1) ||
                !writer.WritePod(static_cast<std::uint32_t>(sortedChildren.size()))) {
                return false;
            }
            for (const ChildId cId : sortedChildren) {
                const ChildRecord& c = *lineage.FindChild(cId);
                if (!writer.WritePod(c.childId.raw()) ||
                    !writer.WritePod(c.hairColorFormId.raw()) ||
                    !writer.WritePod(c.eyesPartFormId.raw()) ||
                    !writer.WritePod(c.hairPartFormId.raw()) ||
                    !writer.WritePod(c.facialHairPartFormId.raw()) ||
                    !writer.WritePod(c.heightScale) ||
                    !writer.WritePod(c.rpgApplied)) {
                    return false;
                }
            }

            // 6d. Inline Extension Block 'LYC1'
            if (!writer.WritePod(CoSaveConstants::kExtChildBeastbloodLYC1) ||
                !writer.WritePod(static_cast<std::uint32_t>(sortedChildren.size()))) {
                return false;
            }
            for (const ChildId cId : sortedChildren) {
                const ChildRecord& c = *lineage.FindChild(cId);
                if (!writer.WritePod(c.childId.raw()) ||
                    !writer.WritePod(c.hasBeastblood) ||
                    !writer.WritePod(c.beastbloodEvaluated)) {
                    return false;
                }
            }

            // 6e. Inline Extension Block 'CHD2'
            if (!writer.WritePod(CoSaveConstants::kExtChildLineageCHD2) ||
                !writer.WritePod(static_cast<std::uint32_t>(sortedChildren.size()))) {
                return false;
            }
            for (const ChildId cId : sortedChildren) {
                const ChildRecord& c = *lineage.FindChild(cId);
                if (!writer.WritePod(c.childId.raw()) ||
                    !writer.WritePod(c.bioLineageEvaluated) ||
                    !writer.WritePod(c.guardianLineageEvaluated) ||
                    !writer.WriteBytes(c.bioSkillBonuses.data(), static_cast<std::uint32_t>(c.bioSkillBonuses.size())) ||
                    !writer.WriteBytes(c.guardianSkillBonuses.data(), static_cast<std::uint32_t>(c.guardianSkillBonuses.size())) ||
                    !writer.WritePod(c.lastAppliedLevel) ||
                    !WriteLengthPrefixedString(writer, c.daedricBlessingId)) {
                    return false;
                }
            }

            // 6f. Optional Inline Extension Block 'FRT1' (omitted when activeDeliveryBatches is empty)
            if (!lineage.AllActiveDeliveryBatches().empty()) {
                std::vector<ActorId> sortedBatchMothers;
                sortedBatchMothers.reserve(lineage.AllActiveDeliveryBatches().size());
                for (const auto& [motherId, batch] : lineage.AllActiveDeliveryBatches()) {
                    (void)batch;
                    sortedBatchMothers.push_back(motherId);
                }
                std::sort(sortedBatchMothers.begin(), sortedBatchMothers.end());

                if (!writer.WritePod(CoSaveConstants::kExtFertilityBatchFRT1) ||
                    !writer.WritePod(static_cast<std::uint32_t>(sortedBatchMothers.size()))) {
                    return false;
                }
                for (const ActorId motherId : sortedBatchMothers) {
                    const ActiveDeliveryBatch* batch = lineage.FindActiveDeliveryBatch(motherId);
                    if (!batch) {
                        return false;
                    }
                    if (!writer.WritePod(motherId.raw()) ||
                        !writer.WritePod(batch->laborTotal) ||
                        !writer.WritePod(batch->laborStartGameDay) ||
                        !writer.WritePod(static_cast<std::uint32_t>(batch->bornChildIds.size()))) {
                        return false;
                    }
                    for (const ChildId cid : batch->bornChildIds) {
                        if (!writer.WritePod(cid.raw())) {
                            return false;
                        }
                    }
                }
            }

            // 6g. Optional Inline Extension Block 'SNU1' (omitted when no children carry skyrimNetUuid)
            std::uint32_t snuCount = 0;
            for (const ChildId cId : sortedChildren) {
                if (lineage.FindChild(cId)->skyrimNetUuid != 0) {
                    ++snuCount;
                }
            }
            if (snuCount > 0) {
                if (!writer.WritePod(CoSaveConstants::kExtChildSkyrimNetUuidSNU1) ||
                    !writer.WritePod(snuCount)) {
                    return false;
                }
                for (const ChildId cId : sortedChildren) {
                    const ChildRecord& c = *lineage.FindChild(cId);
                    if (c.skyrimNetUuid != 0) {
                        if (!writer.WritePod(c.childId.raw()) ||
                            !writer.WritePod(c.skyrimNetUuid)) {
                            return false;
                        }
                    }
                }
            }

            return true;
        }

        bool SaveChunkSPHR(const WorldState& state, ICoSaveWriter& writer) {
            if (!writer.OpenRecord(CoSaveConstants::kChunkSPHR, CoSaveConstants::kDataVersion)) {
                return false;
            }
            const auto& slots = state.hierarchy->Slots();
            if (!writer.WritePod(static_cast<std::uint32_t>(slots.size()))) {
                return false;
            }
            for (const ActorId id : slots) {
                if (!writer.WritePod(id.raw())) {
                    return false;
                }
            }
            return true;
        }

        bool SaveChunkAFCT(const WorldState& state, ICoSaveWriter& writer) {
            if (!writer.OpenRecord(CoSaveConstants::kChunkAFCT, CoSaveConstants::kDataVersion)) {
                return false;
            }
            const AffectionTable& aff = state.affection.Read();

            std::vector<ActorId> permIds;
            permIds.reserve(aff.AllPermanent().size());
            for (const auto& [actor, score] : aff.AllPermanent()) {
                (void)score;
                permIds.push_back(actor);
            }
            std::sort(permIds.begin(), permIds.end());

            if (!writer.WritePod(static_cast<std::uint32_t>(permIds.size()))) {
                return false;
            }
            for (const ActorId actor : permIds) {
                const std::int32_t score = aff.AllPermanent().at(actor);
                if (!writer.WritePod(actor.raw()) || !writer.WritePod(score)) {
                    return false;
                }
            }

            std::vector<ActorId> dayIds;
            dayIds.reserve(aff.AllLastDays().size());
            for (const auto& [actor, day] : aff.AllLastDays()) {
                (void)day;
                dayIds.push_back(actor);
            }
            std::sort(dayIds.begin(), dayIds.end());

            if (!writer.WritePod(static_cast<std::uint32_t>(dayIds.size()))) {
                return false;
            }
            for (const ActorId actor : dayIds) {
                const float day = aff.AllLastDays().at(actor);
                if (!writer.WritePod(actor.raw()) || !writer.WritePod(day)) {
                    return false;
                }
            }

            std::vector<ActorId> dailyIds;
            dailyIds.reserve(aff.AllDaily().size());
            for (const auto& [actor, typeMap] : aff.AllDaily()) {
                if (!typeMap.empty()) {
                    dailyIds.push_back(actor);
                }
            }
            std::sort(dailyIds.begin(), dailyIds.end());

            if (!writer.WritePod(static_cast<std::uint32_t>(dailyIds.size()))) {
                return false;
            }
            for (const ActorId actor : dailyIds) {
                const auto& typeMap = aff.AllDaily().at(actor);
                std::vector<std::string> keys;
                keys.reserve(typeMap.size());
                for (const auto& [k, v] : typeMap) {
                    (void)v;
                    keys.push_back(k);
                }
                std::sort(keys.begin(), keys.end());

                if (!writer.WritePod(actor.raw()) ||
                    !writer.WritePod(static_cast<std::uint32_t>(keys.size()))) {
                    return false;
                }
                for (const std::string& k : keys) {
                    const float amount = typeMap.at(k);
                    if (!WriteLengthPrefixedString(writer, k) || !writer.WritePod(amount)) {
                        return false;
                    }
                }
            }
            return true;
        }

        bool SaveChunkPHOU(const WorldState& state, ICoSaveWriter& writer) {
            if (!writer.OpenRecord(CoSaveConstants::kChunkPHOU, CoSaveConstants::kDataVersion)) {
                return false;
            }
            const HouseholdTable& hh = state.household.Read();
            const auto& houses = hh.AllHouses();

            if (!writer.WritePod(static_cast<std::uint32_t>(houses.size()))) {
                return false;
            }
            for (const PlayerHouseRecord& house : houses) {
                if (!writer.WritePod(house.houseId.raw()) ||
                    !writer.WritePod(house.markerId.raw()) ||
                    !writer.WritePod(house.initialMoveDone) ||
                    !writer.WritePod(static_cast<std::uint32_t>(house.tenantOrder.size()))) {
                    return false;
                }
                for (const std::uint32_t tid : house.tenantOrder) {
                    const bool stayAtHome = hh.GetTenantStayAtHome(tid);
                    if (!writer.WritePod(tid) || !writer.WritePod(stayAtHome)) {
                        return false;
                    }
                }
            }
            return true;
        }

        bool SaveChunkSPAS(const WorldState& state, ICoSaveWriter& writer) {
            if (!writer.OpenRecord(CoSaveConstants::kChunkSPAS, CoSaveConstants::kDataVersion)) {
                return false;
            }
            const HouseholdTable& hh = state.household.Read();
            const auto& sharedCells = hh.AllSharedCells();

            if (!writer.WritePod(static_cast<std::uint32_t>(sharedCells.size()))) {
                return false;
            }
            for (const auto& [cellId, rec] : sharedCells) {
                const std::uint8_t sharedFlag = rec.sharedWithPlayer ? 1u : 0u;
                const std::uint8_t recordedFlag = rec.originalPublicRecorded ? 1u : 0u;
                const std::uint8_t origStateFlag = rec.originalPublicState ? 1u : 0u;

                if (!writer.WritePod(cellId.raw()) ||
                    !writer.WritePod(sharedFlag) ||
                    !writer.WritePod(recordedFlag) ||
                    !writer.WritePod(origStateFlag) ||
                    !writer.WritePod(static_cast<std::uint32_t>(rec.sharingSpouses.size()))) {
                    return false;
                }
                for (const ActorId spouse : rec.sharingSpouses) {
                    if (!writer.WritePod(spouse.raw())) {
                        return false;
                    }
                }
            }

            std::vector<RefId> bedIds;
            bedIds.reserve(hh.AllOriginalBedOwners().size());
            for (const auto& [bedRef, owner] : hh.AllOriginalBedOwners()) {
                (void)owner;
                bedIds.push_back(bedRef);
            }
            std::sort(bedIds.begin(), bedIds.end());

            if (!writer.WritePod(static_cast<std::uint32_t>(bedIds.size()))) {
                return false;
            }
            for (const RefId bedRef : bedIds) {
                const FormRawId owner = hh.AllOriginalBedOwners().at(bedRef);
                if (!writer.WritePod(bedRef.raw()) || !writer.WritePod(owner.raw())) {
                    return false;
                }
            }
            return true;
        }

        bool SaveChunkPLCH(const WorldState& state, ICoSaveWriter& writer) {
            if (!writer.OpenRecord(CoSaveConstants::kChunkPLCH, CoSaveConstants::kDataVersion)) {
                return false;
            }
            const HouseholdTable& hh = state.household.Read();
            if (!writer.WritePod(hh.IsChildChestIntroduced())) {
                return false;
            }

            std::vector<ChildId> childIds;
            childIds.reserve(hh.AllChildStates().size());
            for (const auto& [cId, st] : hh.AllChildStates()) {
                (void)st;
                childIds.push_back(cId);
            }
            std::sort(childIds.begin(), childIds.end());

            if (!writer.WritePod(static_cast<std::uint32_t>(childIds.size()))) {
                return false;
            }
            for (const ChildId cId : childIds) {
                const ChildHouseholdRecord& st = hh.AllChildStates().at(cId);
                if (!writer.WritePod(st.childId.raw()) ||
                    !writer.WritePod(st.currentHouseId.raw()) ||
                    !writer.WritePod(st.homeMarkerId.raw()) ||
                    !writer.WritePod(st.petId.raw()) ||
                    !writer.WritePod(st.critterId.raw()) ||
                    !writer.WritePod(st.currentOrder) ||
                    !writer.WritePod(st.orderExpiryGameDay) ||
                    !writer.WritePod(st.forcegreetPending) ||
                    !writer.WritePod(st.critterBanned) ||
                    !writer.WritePod(st.petBanned) ||
                    !writer.WritePod(st.isNewlyAdopted) ||
                    !writer.WritePod(st.critterRetryTimestamp) ||
                    !writer.WritePod(st.petRetryTimestamp) ||
                    !writer.WritePod(st.playedScene1) ||
                    !writer.WritePod(st.playedScene2) ||
                    !writer.WritePod(st.lastInteractionTimestamp)) {
                    return false;
                }
            }

            // Optional trailing 'PLC1' extension — omitted when all Phase 7 child/forcegreet state is default/empty
            if (hh.HasAnyPlc1State()) {
                std::vector<ChildId> transientChildIds;
                for (const ChildId cId : childIds) {
                    if (!hh.AllChildStates().at(cId).transientCritterId.IsNull()) {
                        transientChildIds.push_back(cId);
                    }
                }
                if (!writer.WritePod(CoSaveConstants::kExtPlayerChildrenStatePLC1) ||
                    !writer.WritePod(hh.GetPoorCounter()) ||
                    !writer.WritePod(hh.IsNameCallingUsedLast()) ||
                    !writer.WritePod(static_cast<std::uint32_t>(transientChildIds.size()))) {
                    return false;
                }
                for (const ChildId cId : transientChildIds) {
                    const RefId critterId = hh.AllChildStates().at(cId).transientCritterId;
                    if (!writer.WritePod(cId.raw()) || !writer.WritePod(critterId.raw())) {
                        return false;
                    }
                }
            }
            return true;
        }

        bool SaveChunkCFSN(const WorldState& state, ICoSaveWriter& writer) {
            if (!writer.OpenRecord(CoSaveConstants::kChunkCFSN, CoSaveConstants::kDataVersion)) {
                return false;
            }
            const auto& snaps = state.household->AllChildFactionSnapshots();
            std::vector<ChildId> childIds;
            childIds.reserve(snaps.size());
            for (const auto& [cId, snap] : snaps) {
                (void)snap;
                childIds.push_back(cId);
            }
            std::sort(childIds.begin(), childIds.end());

            if (!writer.WritePod(static_cast<std::uint32_t>(childIds.size()))) {
                return false;
            }
            for (const ChildId cId : childIds) {
                const auto& snap = snaps.at(cId);
                if (!writer.WritePod(cId.raw()) ||
                    !writer.WritePod(snap.crimeFactionId.raw()) ||
                    !writer.WritePod(static_cast<std::uint32_t>(snap.factions.size()))) {
                    return false;
                }
                for (const auto& fEntry : snap.factions) {
                    if (!writer.WritePod(fEntry.factionId.raw()) || !writer.WritePod(fEntry.rank)) {
                        return false;
                    }
                }
            }
            return true;
        }

        bool SaveChunkMKMK(const WorldState& state, ICoSaveWriter& writer) {
            if (!writer.OpenRecord(CoSaveConstants::kChunkMKMK, CoSaveConstants::kDataVersion)) {
                return false;
            }
            const CooldownTable& cd = state.cooldowns.Read();
            if (!writer.WritePod(cd.lastMatchmakingDay)) {
                return false;
            }

            std::vector<ActorId> npcIds;
            npcIds.reserve(cd.npcLastMatchmakingChangeDay.size());
            for (const auto& [id, day] : cd.npcLastMatchmakingChangeDay) {
                (void)day;
                npcIds.push_back(id);
            }
            std::sort(npcIds.begin(), npcIds.end());

            if (!writer.WritePod(static_cast<std::uint32_t>(npcIds.size()))) {
                return false;
            }
            for (const ActorId id : npcIds) {
                if (!writer.WritePod(id.raw()) || !writer.WritePod(cd.npcLastMatchmakingChangeDay.at(id))) {
                    return false;
                }
            }

            if (!writer.WritePod(cd.lastPlayerPartnerCheatingDay)) {
                return false;
            }

            std::vector<LocationId> locIds;
            locIds.reserve(cd.lastHomeArrivalEventDayByLoc.size());
            for (const auto& [locId, day] : cd.lastHomeArrivalEventDayByLoc) {
                (void)day;
                locIds.push_back(locId);
            }
            std::sort(locIds.begin(), locIds.end());

            if (!writer.WritePod(static_cast<std::uint32_t>(locIds.size()))) {
                return false;
            }
            for (const LocationId locId : locIds) {
                if (!writer.WritePod(locId.raw()) || !writer.WritePod(cd.lastHomeArrivalEventDayByLoc.at(locId))) {
                    return false;
                }
            }

            if (cd.HasAnyPhase3RelState()) {
                if (!writer.WritePod(CoSaveConstants::kExtRelationshipStateREL1) ||
                    !writer.WritePod(cd.lastTimePaidDivorceFee) ||
                    !writer.WritePod(cd.playerKiller) ||
                    !writer.WritePod(cd.playerHadWedding) ||
                    !writer.WritePod(cd.dontShowSkipWeddingMessage)) {
                    return false;
                }

                std::vector<ActorId> brokeupIds;
                brokeupIds.reserve(cd.npcBrokeupTime.size());
                for (const auto& [id, day] : cd.npcBrokeupTime) {
                    (void)day;
                    brokeupIds.push_back(id);
                }
                std::sort(brokeupIds.begin(), brokeupIds.end());
                if (!writer.WritePod(static_cast<std::uint32_t>(brokeupIds.size()))) {
                    return false;
                }
                for (const ActorId id : brokeupIds) {
                    if (!writer.WritePod(id.raw()) || !writer.WritePod(cd.npcBrokeupTime.at(id))) {
                        return false;
                    }
                }

                std::vector<ActorId> incomeIds;
                incomeIds.reserve(cd.npcLastTimeSharedIncome.size());
                for (const auto& [id, day] : cd.npcLastTimeSharedIncome) {
                    (void)day;
                    incomeIds.push_back(id);
                }
                std::sort(incomeIds.begin(), incomeIds.end());
                if (!writer.WritePod(static_cast<std::uint32_t>(incomeIds.size()))) {
                    return false;
                }
                for (const ActorId id : incomeIds) {
                    if (!writer.WritePod(id.raw()) || !writer.WritePod(cd.npcLastTimeSharedIncome.at(id))) {
                        return false;
                    }
                }
            }
            return true;
        }

        bool SaveChunkDCSD(const WorldState& state, ICoSaveWriter& writer) {
            if (!writer.OpenRecord(CoSaveConstants::kChunkDCSD, CoSaveConstants::kDataVersion)) {
                return false;
            }
            const auto& records = state.deceased->AllRecords();
            std::vector<std::uint32_t> keys;
            keys.reserve(records.size());
            for (const auto& [k, rec] : records) {
                (void)rec;
                keys.push_back(k);
            }
            std::sort(keys.begin(), keys.end());

            if (!writer.WritePod(static_cast<std::uint32_t>(keys.size()))) {
                return false;
            }

            auto writeChildInfo = [&](const std::optional<DeceasedChildMetadata>& ciOpt) -> bool {
                const bool hasCi = ciOpt.has_value();
                if (!writer.WritePod(hasCi)) {
                    return false;
                }
                if (!hasCi) {
                    return true;
                }
                const DeceasedChildMetadata& ci = *ciOpt;
                return writer.WritePod(ci.guardianABaseId.raw()) &&
                       writer.WritePod(ci.guardianARefId.raw()) &&
                       WriteLengthPrefixedString(writer, ci.guardianAName) &&
                       writer.WritePod(ci.guardianBBaseId.raw()) &&
                       writer.WritePod(ci.guardianBRefId.raw()) &&
                       WriteLengthPrefixedString(writer, ci.guardianBName) &&
                       writer.WritePod(ci.bioMotherBaseId.raw()) &&
                       writer.WritePod(ci.bioFatherBaseId.raw()) &&
                       writer.WritePod(ci.isAdopted) &&
                       writer.WritePod(ci.houseId.raw());
            };

            for (const std::uint32_t k : keys) {
                const DeceasedActorRecord& rec = records.at(k);
                if (!writer.WritePod(rec.baseId.raw()) ||
                    !writer.WritePod(rec.refId.raw()) ||
                    !WriteLengthPrefixedString(writer, rec.name) ||
                    !writer.WritePod(rec.deathTimestamp) ||
                    !writer.WritePod(rec.isChild) ||
                    !writer.WritePod(rec.wasKilledByPlayer) ||
                    !writer.WritePod(rec.killerBaseId.raw()) ||
                    !writer.WritePod(rec.killerRefId.raw()) ||
                    !WriteLengthPrefixedString(writer, rec.killerName)) {
                    return false;
                }

                if (!rec.isChild) {
                    if (!writer.WritePod(static_cast<std::uint8_t>(rec.socialClass)) ||
                        !writer.WritePod(static_cast<std::uint8_t>(rec.skillType)) ||
                        !writer.WritePod(static_cast<std::uint8_t>(rec.temperament)) ||
                        !writer.WritePod(static_cast<std::uint32_t>(rec.partnersAtDeath.size()))) {
                        return false;
                    }
                    for (const auto& p : rec.partnersAtDeath) {
                        if (!writer.WritePod(p.partnerBaseId.raw()) ||
                            !writer.WritePod(p.partnerRefId.raw()) ||
                            !WriteLengthPrefixedString(writer, p.partnerName) ||
                            !writer.WritePod(static_cast<std::uint8_t>(p.statusAtDeath)) ||
                            !writer.WritePod(p.isPlayer)) {
                            return false;
                        }
                    }
                    if (!writeChildInfo(rec.childInfo)) {
                        return false;
                    }
                } else {
                    if (!writeChildInfo(rec.childInfo)) {
                        return false;
                    }
                }
            }

            // Optional trailing 'DCS1' secrecy extension — omitted when no deceased secrecy flags exist
            std::vector<std::uint32_t> secretKeys;
            for (const std::uint32_t k : keys) {
                const DeceasedActorRecord& rec = records.at(k);
                const bool hasSecretPartner = std::any_of(
                    rec.partnersAtDeath.begin(),
                    rec.partnersAtDeath.end(),
                    [](const DeceasedPartnerEntry& p) { return p.isSecretAffair; });
                const bool hasSecretPaternity =
                    rec.childInfo.has_value() && rec.childInfo->hasUndiscoveredPaternitySecret;
                if (hasSecretPartner || hasSecretPaternity) {
                    secretKeys.push_back(k);
                }
            }

            if (!secretKeys.empty()) {
                if (!writer.WritePod(CoSaveConstants::kExtDeceasedSecrecyDCS1) ||
                    !writer.WritePod(static_cast<std::uint32_t>(secretKeys.size()))) {
                    return false;
                }
                for (const std::uint32_t k : secretKeys) {
                    const DeceasedActorRecord& rec = records.at(k);
                    std::vector<const DeceasedPartnerEntry*> secretPartners;
                    for (const auto& p : rec.partnersAtDeath) {
                        if (p.isSecretAffair) {
                            secretPartners.push_back(&p);
                        }
                    }
                    const bool hasSecretPaternity =
                        rec.childInfo.has_value() && rec.childInfo->hasUndiscoveredPaternitySecret;
                    if (!writer.WritePod(rec.baseId.raw()) ||
                        !writer.WritePod(rec.refId.raw()) ||
                        !writer.WritePod(static_cast<std::uint32_t>(secretPartners.size()))) {
                        return false;
                    }
                    for (const DeceasedPartnerEntry* p : secretPartners) {
                        if (!writer.WritePod(p->partnerBaseId.raw()) ||
                            !writer.WritePod(p->partnerRefId.raw()) ||
                            !writer.WritePod(static_cast<std::uint8_t>(p->publicStatusAtDeath))) {
                            return false;
                        }
                    }
                    if (!writer.WritePod(hasSecretPaternity)) {
                        return false;
                    }
                    if (hasSecretPaternity) {
                        if (!writer.WritePod(rec.childInfo->publicBioFatherBaseId.raw())) {
                            return false;
                        }
                    }
                }
            }

            return true;
        }

        bool SaveChunkBNTR(const WorldState& state, ICoSaveWriter& writer) {
            if (!writer.OpenRecord(CoSaveConstants::kChunkBNTR, CoSaveConstants::kDataVersion)) {
                return false;
            }
            const CooldownTable& cd = state.cooldowns.Read();
            if (!writer.WritePod(cd.savedBanterCooldownHours)) {
                return false;
            }

            std::vector<CanonicalPairKey> pairs;
            pairs.reserve(cd.banterCoupleCooldowns.size());
            for (const auto& [pair, ts] : cd.banterCoupleCooldowns) {
                (void)ts;
                pairs.push_back(pair);
            }
            std::sort(pairs.begin(), pairs.end());

            if (!writer.WritePod(static_cast<std::uint32_t>(pairs.size()))) {
                return false;
            }
            for (const CanonicalPairKey& pair : pairs) {
                if (!writer.WritePod(pair.first.raw()) ||
                    !writer.WritePod(pair.second.raw()) ||
                    !writer.WritePod(cd.banterCoupleCooldowns.at(pair))) {
                    return false;
                }
            }
            if (!writer.WritePod(cd.lastBanterSceneDay)) {
                return false;
            }

            // Optional trailing 'BNT1' extension — omitted when no moved actors or muted SkyrimNet participants exist
            if (cd.HasAnyBnt1State()) {
                std::vector<ActorId> movedActors;
                movedActors.reserve(cd.movedSceneActors.size());
                for (const auto& [actorId, markerId] : cd.movedSceneActors) {
                    if (!actorId.IsNull() && !actorId.IsPlayer() && !markerId.IsNull()) {
                        movedActors.push_back(actorId);
                    }
                }
                std::sort(movedActors.begin(), movedActors.end());

                std::vector<ActorId> mutedActors;
                mutedActors.reserve(cd.skyrimNetMutedActors.size());
                for (const ActorId actorId : cd.skyrimNetMutedActors) {
                    if (!actorId.IsNull() && !actorId.IsPlayer()) {
                        mutedActors.push_back(actorId);
                    }
                }
                std::sort(mutedActors.begin(), mutedActors.end());
                mutedActors.erase(std::unique(mutedActors.begin(), mutedActors.end()), mutedActors.end());

                if (!writer.WritePod(CoSaveConstants::kExtBanterSceneStateBNT1) ||
                    !writer.WritePod(static_cast<std::uint32_t>(movedActors.size()))) {
                    return false;
                }
                for (const ActorId actorId : movedActors) {
                    const RefId markerId = cd.movedSceneActors.at(actorId);
                    if (!writer.WritePod(actorId.raw()) || !writer.WritePod(markerId.raw())) {
                        return false;
                    }
                }
                if (!writer.WritePod(static_cast<std::uint32_t>(mutedActors.size()))) {
                    return false;
                }
                for (const ActorId actorId : mutedActors) {
                    if (!writer.WritePod(actorId.raw())) {
                        return false;
                    }
                }
            }
            return true;
        }

        bool SaveChunkACTR(const WorldState& state, ICoSaveWriter& writer) {
            if (!writer.OpenRecord(CoSaveConstants::kChunkACTR, CoSaveConstants::kDataVersion)) {
                return false;
            }
            if (!writer.WritePod(CoSaveConstants::kMagicNumber)) {
                return false;
            }
            const auto& travels = state.household->AllAdultChildTravel();
            std::vector<ChildId> childIds;
            childIds.reserve(travels.size());
            for (const auto& [cId, tr] : travels) {
                (void)tr;
                childIds.push_back(cId);
            }
            std::sort(childIds.begin(), childIds.end());

            if (!writer.WritePod(static_cast<std::uint32_t>(childIds.size()))) {
                return false;
            }
            for (const ChildId cId : childIds) {
                const AdultChildTravelRecord& tr = travels.at(cId);
                if (!writer.WritePod(cId.raw()) ||
                    !writer.WritePod(tr.targetMarkerId.raw()) ||
                    !writer.WritePod(tr.targetLocationId.raw()) ||
                    !writer.WritePod(tr.targetCellId.raw()) ||
                    !writer.WritePod(tr.assignedGameDay) ||
                    !writer.WritePod(tr.arrivedGameDay) ||
                    !writer.WritePod(tr.stayDurationDays) ||
                    !writer.WritePod(tr.isFamilyVisit) ||
                    !WriteLengthPrefixedString(writer, tr.destinationName)) {
                    return false;
                }
            }
            return true;
        }

        bool SaveChunkCNFG(
            const WorldState& state,
            ICoSaveWriter& writer,
            std::string_view savedModVersion) {
            if (!writer.OpenRecord(CoSaveConstants::kChunkCNFG, CoSaveConstants::kDataVersion)) {
                return false;
            }
            if (!writer.WritePod(CoSaveConstants::kSettingsVersionMarker) ||
                !writer.WritePod(CoSaveConstants::kSettingsSubVersion)) {
                return false;
            }

            const SettingsData& s = state.settings.Read();
            bool ok = true;
            ForEachPersistedSettingField([&](std::string_view, auto memberPtr, bool) {
                if (!ok) {
                    return;
                }
                const auto& field = s.*memberPtr;
                using FieldType = std::decay_t<decltype(field)>;

                if constexpr (std::is_same_v<FieldType, bool>) {
                    const std::uint8_t raw = field ? 1u : 0u;
                    ok = writer.WritePod(raw);
                } else if constexpr (std::is_enum_v<FieldType>) {
                    const auto raw = static_cast<std::uint8_t>(field);
                    ok = writer.WritePod(raw);
                } else {
                    ok = writer.WritePod(field);
                }
            });
            if (!ok) {
                return false;
            }
            if (!savedModVersion.empty()) {
                const std::string_view bounded =
                    savedModVersion.substr(0, std::min<std::size_t>(savedModVersion.size(), kMaxVersionStringLen));
                if (!writer.WritePod(CoSaveConstants::kExtSaveMetadataVER1) ||
                    !WriteLengthPrefixedString(writer, bounded)) {
                    return false;
                }
            }
            return true;
        }

        // =====================================================================
        // LOAD CHUNK HELPERS (v1–v7 Lossless Deserialization + Atomic Rollback)
        // =====================================================================

        bool LoadChunkNPCR(
            WorldState& state,
            BoundedChunkReader& chunk,
            std::uint32_t version,
            const IFormIdResolver& resolver,
            std::uint32_t& outInferredPresumedFathers) {
            outInferredPresumedFathers = 0;
            std::uint32_t magic = 0;
            if (!chunk.ReadValue(magic) || magic != CoSaveConstants::kMagicNumber) {
                Foundation::LogError("[CoSaveCodec] NPCR: Invalid magic header 0x{:08X}", magic);
                return false;
            }

            ActorProfileTable stagedActors{};
            RelationshipEdgeTable stagedEdges{};
            AffairLedger stagedAffairs{};
            LineageTable stagedLineage{};
            std::uint32_t stagedInferredPresumed = 0;

            if (version < 4) {
                // Legacy v1–v3 format migration
                std::uint32_t npcCount = 0;
                if (!chunk.ReadValue(npcCount) || npcCount > kMaxSerializedEntries) {
                    return false;
                }
                constexpr std::uint8_t kLegacyDeceasedStatus = 5u;

                for (std::uint32_t i = 0; i < npcCount; ++i) {
                    std::uint32_t oldFormId = 0;
                    std::uint32_t newFormId = 0;
                    if (!chunk.ReadValue(oldFormId)) {
                        return false;
                    }
                    const bool formValid =
                        ResolveActorOrSpecialRaw(resolver, oldFormId, newFormId, false, false, false, true);

                    std::uint8_t rawSc = 0;
                    std::uint8_t rawSt = 0;
                    std::uint8_t rawTp = 0;
                    std::uint8_t rawStatus = 0;
                    if (!chunk.ReadValue(rawSc) || !chunk.ReadValue(rawSt) ||
                        !chunk.ReadValue(rawTp) || !chunk.ReadValue(rawStatus)) {
                        return false;
                    }

                    ActorProfileRecord prof{};
                    prof.socialClass = static_cast<SocialClass>(rawSc);
                    prof.skillType = static_cast<SkillType>(rawSt);
                    prof.temperament = static_cast<Temperament>(rawTp);

                    bool hasOrigHome = false;
                    if (!chunk.ReadValue(hasOrigHome)) {
                        return false;
                    }
                    if (hasOrigHome) {
                        std::uint32_t oldHome = 0;
                        std::uint32_t newHome = 0;
                        if (!chunk.ReadValue(oldHome)) {
                            return false;
                        }
                        if (oldHome != 0 && resolver.ResolveFormId(oldHome, newHome) && newHome != 0) {
                            prof.legacyOriginalHome = FormRawId{ newHome };
                        }
                    }

                    bool hasCurrHome = false;
                    if (!chunk.ReadValue(hasCurrHome)) {
                        return false;
                    }
                    if (hasCurrHome) {
                        std::uint32_t oldHome = 0;
                        std::uint32_t newHome = 0;
                        if (!chunk.ReadValue(oldHome)) {
                            return false;
                        }
                        if (oldHome != 0 && resolver.ResolveFormId(oldHome, newHome) && newHome != 0) {
                            prof.legacyCurrentHome = FormRawId{ newHome };
                        }
                    }

                    std::uint32_t engDate = 0;
                    std::uint32_t marrDate = 0;
                    if (!chunk.ReadValue(engDate) || !chunk.ReadValue(marrDate)) {
                        return false;
                    }

                    if (version >= 2) {
                        bool hasMarker = false;
                        if (!chunk.ReadValue(hasMarker)) {
                            return false;
                        }
                        if (hasMarker) {
                            std::uint32_t oldMarker = 0;
                            std::uint32_t newMarker = 0;
                            if (!chunk.ReadValue(oldMarker)) {
                                return false;
                            }
                            if (oldMarker != 0 && resolver.ResolveFormId(oldMarker, newMarker) && newMarker != 0) {
                                prof.legacyHomeMarker = RefId{ newMarker };
                            }
                        }
                    }

                    if (version >= 3) {
                        std::uint32_t kwCount = 0;
                        if (!chunk.ReadValue(kwCount) || kwCount > kMaxSerializedEntries) {
                            return false;
                        }
                        for (std::uint32_t k = 0; k < kwCount; ++k) {
                            std::uint32_t oldKw = 0;
                            std::uint32_t newKw = 0;
                            if (!chunk.ReadValue(oldKw)) {
                                return false;
                            }
                            if (oldKw != 0 && resolver.ResolveFormId(oldKw, newKw) && newKw != 0) {
                                prof.addedKeywords.insert(FormRawId{ newKw });
                            }
                        }
                    }

                    if (!formValid || rawStatus == kLegacyDeceasedStatus || newFormId == Foundation::kPlayerRefRawId) {
                        continue;
                    }
                    const ActorId npcActor{ newFormId };
                    if (!resolver.IsAliveAndValid(npcActor)) {
                        continue;
                    }

                    prof.refId = RefId{ newFormId };
                    prof.baseId = resolver.GetActorBaseId(npcActor);
                    prof.isRegisteredAdult = true;
                    stagedActors.Upsert(std::move(prof));

                    RelationshipEdgeRecord edge{};
                    edge.pair = CanonicalPairKey{ npcActor, ActorId::Player() };
                    edge.status = static_cast<RelationshipStatus>(rawStatus);
                    edge.originalStatus = RelationshipStatus::Unknown;
                    if (engDate != 0) {
                        edge.statusHistory.push_back(StatusTransitionEntry{
                            .fromStatus = RelationshipStatus::Candidate,
                            .toStatus = RelationshipStatus::Engaged,
                            .timestamp = static_cast<float>(engDate),
                            .initiator = ActorId::Player(),
                        });
                    }
                    if (marrDate != 0) {
                        edge.statusHistory.push_back(StatusTransitionEntry{
                            .fromStatus = RelationshipStatus::Engaged,
                            .toStatus = RelationshipStatus::Married,
                            .timestamp = static_cast<float>(marrDate),
                            .initiator = ActorId::Player(),
                        });
                    }
                    stagedEdges.UpsertEdge(std::move(edge));
                }
            } else {
                // Unified v4–v7 format
                const bool hasPresumedFather = (version >= 5);
                const bool hasTrainingLifecycle = (version >= 6);
                const bool hasPaternityBioFatherMeta = (version >= 7);

                // 1. Profiles
                std::uint32_t profileCount = 0;
                if (!chunk.ReadValue(profileCount) || profileCount > kMaxSerializedEntries) {
                    return false;
                }
                for (std::uint32_t i = 0; i < profileCount; ++i) {
                    std::uint32_t oldFormId = 0;
                    std::uint32_t newFormId = 0;
                    if (!chunk.ReadValue(oldFormId)) {
                        return false;
                    }
                    const bool valid =
                        ResolveActorOrSpecialRaw(resolver, oldFormId, newFormId, false, false, false, true);

                    std::uint8_t rawSc = 0;
                    std::uint8_t rawSt = 0;
                    std::uint8_t rawTp = 0;
                    if (!chunk.ReadValue(rawSc) || !chunk.ReadValue(rawSt) || !chunk.ReadValue(rawTp)) {
                        return false;
                    }

                    ActorProfileRecord prof{};
                    prof.socialClass = static_cast<SocialClass>(rawSc);
                    prof.skillType = static_cast<SkillType>(rawSt);
                    prof.temperament = static_cast<Temperament>(rawTp);

                    bool hasOrig = false;
                    if (!chunk.ReadValue(hasOrig)) {
                        return false;
                    }
                    if (hasOrig) {
                        std::uint32_t oldH = 0;
                        std::uint32_t newH = 0;
                        if (!chunk.ReadValue(oldH)) {
                            return false;
                        }
                        if (oldH != 0 && resolver.ResolveFormId(oldH, newH) && newH != 0) {
                            prof.legacyOriginalHome = FormRawId{ newH };
                        }
                    }

                    bool hasCurr = false;
                    if (!chunk.ReadValue(hasCurr)) {
                        return false;
                    }
                    if (hasCurr) {
                        std::uint32_t oldH = 0;
                        std::uint32_t newH = 0;
                        if (!chunk.ReadValue(oldH)) {
                            return false;
                        }
                        if (oldH != 0 && resolver.ResolveFormId(oldH, newH) && newH != 0) {
                            prof.legacyCurrentHome = FormRawId{ newH };
                        }
                    }

                    bool hasMarker = false;
                    if (!chunk.ReadValue(hasMarker)) {
                        return false;
                    }
                    if (hasMarker) {
                        std::uint32_t oldM = 0;
                        std::uint32_t newM = 0;
                        if (!chunk.ReadValue(oldM)) {
                            return false;
                        }
                        if (oldM != 0 && resolver.ResolveFormId(oldM, newM) && newM != 0) {
                            prof.legacyHomeMarker = RefId{ newM };
                        }
                    }

                    std::uint32_t kwCount = 0;
                    if (!chunk.ReadValue(kwCount) || kwCount > kMaxSerializedEntries) {
                        return false;
                    }
                    for (std::uint32_t k = 0; k < kwCount; ++k) {
                        std::uint32_t oldKw = 0;
                        std::uint32_t newKw = 0;
                        if (!chunk.ReadValue(oldKw)) {
                            return false;
                        }
                        if (oldKw != 0 && resolver.ResolveFormId(oldKw, newKw) && newKw != 0) {
                            prof.addedKeywords.insert(FormRawId{ newKw });
                        }
                    }

                    if (valid && newFormId != 0) {
                        prof.refId = RefId{ newFormId };
                        prof.baseId = resolver.GetActorBaseId(ActorId{ newFormId });
                        prof.isRegisteredAdult = true;
                        stagedActors.Upsert(std::move(prof));
                    }
                }

                // 2. Relationship Pairs + Status History + Cheating Records
                std::uint32_t pairCount = 0;
                if (!chunk.ReadValue(pairCount) || pairCount > kMaxSerializedEntries) {
                    return false;
                }
                for (std::uint32_t i = 0; i < pairCount; ++i) {
                    std::uint32_t oldA = 0;
                    std::uint32_t oldB = 0;
                    std::uint32_t newA = 0;
                    std::uint32_t newB = 0;
                    if (!chunk.ReadValue(oldA) || !chunk.ReadValue(oldB)) {
                        return false;
                    }
                    const bool validA = ResolveActorOrSpecialRaw(resolver, oldA, newA, false, false, false, true);
                    const bool validB = ResolveActorOrSpecialRaw(resolver, oldB, newB, false, false, false, true);

                    std::uint8_t rawSt = 0;
                    std::uint8_t rawOrigSt = static_cast<std::uint8_t>(RelationshipStatus::Unknown);
                    bool isOpen = false;
                    if (!chunk.ReadValue(rawSt) || !chunk.ReadValue(rawOrigSt) || !chunk.ReadValue(isOpen)) {
                        return false;
                    }

                    RelationshipEdgeRecord edge{};
                    edge.pair = CanonicalPairKey{ ActorId{ newA }, ActorId{ newB } };
                    edge.status = static_cast<RelationshipStatus>(rawSt);
                    edge.originalStatus = static_cast<RelationshipStatus>(rawOrigSt);
                    edge.isOpen = isOpen;

                    std::uint32_t historyCount = 0;
                    if (!chunk.ReadValue(historyCount) || historyCount > kMaxSerializedEntries) {
                        return false;
                    }
                    for (std::uint32_t h = 0; h < historyCount; ++h) {
                        std::uint8_t fromVal = 0;
                        std::uint8_t toVal = 0;
                        float ts = 0.0f;
                        std::uint32_t oldInit = 0;
                        std::uint32_t newInit = 0;
                        if (!chunk.ReadValue(fromVal) || !chunk.ReadValue(toVal) ||
                            !chunk.ReadValue(ts) || !chunk.ReadValue(oldInit)) {
                            return false;
                        }
                        if (ResolveActorOrSpecialRaw(resolver, oldInit, newInit, /*allowZero=*/true, false, false, true)) {
                            edge.statusHistory.push_back(StatusTransitionEntry{
                                .fromStatus = static_cast<RelationshipStatus>(fromVal),
                                .toStatus = static_cast<RelationshipStatus>(toVal),
                                .timestamp = ts,
                                .initiator = ActorId{ newInit },
                            });
                        }
                    }

                    std::uint32_t cheatCount = 0;
                    if (!chunk.ReadValue(cheatCount) || cheatCount > kMaxSerializedEntries) {
                        return false;
                    }
                    for (std::uint32_t c = 0; c < cheatCount; ++c) {
                        std::uint32_t oldCheater = 0;
                        std::uint32_t oldVictim = 0;
                        std::uint32_t oldAffair = 0;
                        std::uint32_t recCount = 1;
                        std::uint32_t recTs = 0;
                        bool isSecret = false;
                        std::uint8_t rawType = 0;
                        std::uint8_t rawAffStatus = 0;

                        if (!chunk.ReadValue(oldCheater) || !chunk.ReadValue(oldVictim) ||
                            !chunk.ReadValue(oldAffair) || !chunk.ReadValue(recCount) ||
                            !chunk.ReadValue(recTs) || !chunk.ReadValue(isSecret) ||
                            !chunk.ReadValue(rawType) || !chunk.ReadValue(rawAffStatus)) {
                            return false;
                        }

                        std::uint32_t newCheater = 0;
                        std::uint32_t newVictim = 0;
                        std::uint32_t newAffair = 0;
                        const bool okCheater = ResolveActorOrSpecialRaw(resolver, oldCheater, newCheater, false, false, false, true);
                        const bool okVictim = ResolveActorOrSpecialRaw(resolver, oldVictim, newVictim, false, false, false, true);
                        const bool okAffair = ResolveActorOrSpecialRaw(resolver, oldAffair, newAffair, false, false, false, true);

                        if (validA && validB && edge.pair.IsValid() && okCheater && okVictim && okAffair) {
                            stagedAffairs.ImportSerializedRecord(
                                edge.pair,
                                ActorId{ newCheater },
                                ActorId{ newVictim },
                                ActorId{ newAffair },
                                recCount,
                                static_cast<std::int32_t>(recTs),
                                isSecret,
                                static_cast<AffairType>(rawType),
                                static_cast<AffairStatus>(rawAffStatus));
                        }
                    }

                    if (validA && validB && edge.pair.IsValid()) {
                        stagedEdges.UpsertEdge(std::move(edge));
                    }
                }

                // 3. Pending Deferred Relationships
                if (version >= 5 || chunk.ChunkLength() == 0 || chunk.BytesRead() < chunk.ChunkLength()) {
                    std::uint32_t pendingMapCount = 0;
                    if (!chunk.ReadValue(pendingMapCount) || pendingMapCount > kMaxSerializedEntries) {
                        return false;
                    }
                    for (std::uint32_t pIdx = 0; pIdx < pendingMapCount; ++pIdx) {
                        std::uint32_t oldBase = 0;
                        std::uint32_t newBase = 0;
                        if (!chunk.ReadValue(oldBase)) {
                            return false;
                        }
                        const bool validBase = ResolveActorOrSpecialRaw(resolver, oldBase, newBase, false, false, false, true);

                        std::uint32_t listCount = 0;
                        if (!chunk.ReadValue(listCount) || listCount > kMaxSerializedEntries) {
                            return false;
                        }
                        for (std::uint32_t j = 0; j < listCount; ++j) {
                            std::uint32_t oldPBase = 0;
                            std::uint32_t oldPRef = 0;
                            std::uint8_t targetSt = 0;
                            std::uint8_t origSt = 0;
                            if (!chunk.ReadValue(oldPBase) || !chunk.ReadValue(oldPRef) ||
                                !chunk.ReadValue(targetSt) || !chunk.ReadValue(origSt)) {
                                return false;
                            }
                            std::uint32_t newPBase = 0;
                            std::uint32_t newPRef = 0;
                            const bool validPBase = ResolveActorOrSpecialRaw(resolver, oldPBase, newPBase, false, false, false, true);
                            const bool validPRef = ResolveActorOrSpecialRaw(resolver, oldPRef, newPRef, /*allowZero=*/true, false, false, true);

                            if (validBase && validPBase && validPRef) {
                                stagedActors.AddPendingRelationship(
                                    BaseId{ newBase },
                                    PendingRelationshipEntry{
                                        .partnerBaseId = BaseId{ newPBase },
                                        .partnerRefId = RefId{ newPRef },
                                        .targetStatus = static_cast<RelationshipStatus>(targetSt),
                                        .originalStatus = static_cast<RelationshipStatus>(origSt),
                                    });
                            }
                        }
                    }
                }

                // 4. Pending Deferred Children
                if (version >= 5 || chunk.ChunkLength() == 0 || chunk.BytesRead() < chunk.ChunkLength()) {
                    std::uint32_t pendingChildMapCount = 0;
                    if (!chunk.ReadValue(pendingChildMapCount) || pendingChildMapCount > kMaxSerializedEntries) {
                        return false;
                    }
                    for (std::uint32_t cIdx = 0; cIdx < pendingChildMapCount; ++cIdx) {
                        std::uint32_t oldCBase = 0;
                        std::uint32_t oldCRef = 0;
                        std::uint32_t oldPABase = 0;
                        std::uint32_t oldPARef = 0;
                        std::uint32_t oldPBBase = 0;
                        std::uint32_t oldPBRef = 0;
                        bool isAdopted = true;
                        if (!chunk.ReadValue(oldCBase) || !chunk.ReadValue(oldCRef) ||
                            !chunk.ReadValue(oldPABase) || !chunk.ReadValue(oldPARef) ||
                            !chunk.ReadValue(oldPBBase) || !chunk.ReadValue(oldPBRef) ||
                            !chunk.ReadValue(isAdopted)) {
                            return false;
                        }
                        std::uint32_t newCBase = 0;
                        std::uint32_t newCRef = 0;
                        std::uint32_t newPABase = 0;
                        std::uint32_t newPARef = 0;
                        std::uint32_t newPBBase = 0;
                        std::uint32_t newPBRef = 0;
                        if (ResolveActorOrSpecialRaw(resolver, oldCBase, newCBase, false, false, false, true) &&
                            ResolveActorOrSpecialRaw(resolver, oldCRef, newCRef, true, false, false, true) &&
                            ResolveActorOrSpecialRaw(resolver, oldPABase, newPABase, true, false, false, true) &&
                            ResolveActorOrSpecialRaw(resolver, oldPARef, newPARef, true, false, false, true) &&
                            ResolveActorOrSpecialRaw(resolver, oldPBBase, newPBBase, true, false, false, true) &&
                            ResolveActorOrSpecialRaw(resolver, oldPBRef, newPBRef, true, false, false, true)) {
                            stagedLineage.AddPendingChild(
                                BaseId{ newCBase },
                                PendingChildEntry{
                                    .childBaseId = BaseId{ newCBase },
                                    .childRefId = RefId{ newCRef },
                                    .parentABaseId = BaseId{ newPABase },
                                    .parentARefId = RefId{ newPARef },
                                    .parentBBaseId = BaseId{ newPBBase },
                                    .parentBRefId = RefId{ newPBRef },
                                    .isAdopted = isAdopted,
                                });
                        }
                    }
                }

                // 5. Child Registry
                if (version >= 5 || chunk.ChunkLength() == 0 || chunk.BytesRead() < chunk.ChunkLength()) {
                    std::uint32_t childRegCount = 0;
                    if (!chunk.ReadValue(childRegCount) || childRegCount > kMaxSerializedEntries) {
                        return false;
                    }
                    for (std::uint32_t cIdx = 0; cIdx < childRegCount; ++cIdx) {
                        std::uint32_t oldChild = 0;
                        std::uint32_t oldGA = 0;
                        std::uint32_t oldGB = 0;
                        std::int32_t ts = 0;
                        bool isAdopted = true;
                        std::uint32_t oldBioM = 0;
                        std::uint32_t oldBioF = 0;

                        if (!chunk.ReadValue(oldChild) || !chunk.ReadValue(oldGA) || !chunk.ReadValue(oldGB) ||
                            !chunk.ReadValue(ts) || !chunk.ReadValue(isAdopted) ||
                            !chunk.ReadValue(oldBioM) || !chunk.ReadValue(oldBioF)) {
                            return false;
                        }

                        std::string childName;
                        if (!chunk.ReadLengthPrefixedString(childName, kMaxChildStringLen)) {
                            return false;
                        }

                        bool isBioChild = !isAdopted;
                        float concTs = 0.0f;
                        float birthTs = 0.0f;
                        bool isSec = false;
                        bool disc = false;
                        bool byPartner = false;
                        bool mismatch = false;
                        float lastCheck = 0.0f;
                        if (!chunk.ReadValue(isBioChild) || !chunk.ReadValue(concTs) || !chunk.ReadValue(birthTs) ||
                            !chunk.ReadValue(isSec) || !chunk.ReadValue(disc) || !chunk.ReadValue(byPartner) ||
                            !chunk.ReadValue(mismatch) || !chunk.ReadValue(lastCheck)) {
                            return false;
                        }

                        std::uint8_t rawStage = 0;
                        std::string trainingLoc;
                        if (!chunk.ReadValue(rawStage) ||
                            !chunk.ReadLengthPrefixedString(trainingLoc, kMaxChildStringLen)) {
                            return false;
                        }

                        ChildStage stage = static_cast<ChildStage>(rawStage);
                        if (stage == ChildStage::Unborn && birthTs > 0.0f) {
                            stage = ChildStage::Infant;
                        }

                        bool secBio = false;
                        bool secPartner = false;
                        bool byBio = false;
                        std::uint8_t childGender = 2;
                        if (!chunk.ReadValue(secBio) || !chunk.ReadValue(secPartner) ||
                            !chunk.ReadValue(byBio) || !chunk.ReadValue(childGender)) {
                            return false;
                        }

                        std::uint32_t oldPresumedFather = 0;
                        if (hasPresumedFather && !chunk.ReadValue(oldPresumedFather)) {
                            return false;
                        }

                        std::int32_t trainClassIdx = -1;
                        float trainStartDay = 0.0f;
                        std::string savedBioTpl;
                        std::uint8_t savedRaceIdx = 255;
                        if (hasTrainingLifecycle) {
                            if (!chunk.ReadValue(trainClassIdx) || !chunk.ReadValue(trainStartDay) ||
                                !chunk.ReadLengthPrefixedString(savedBioTpl, kMaxChildStringLen) ||
                                !chunk.ReadValue(savedRaceIdx)) {
                                return false;
                            }
                        }

                        std::string bioFatherName;
                        FormRawId bioFatherRaceId{};
                        if (hasPaternityBioFatherMeta) {
                            std::uint32_t oldRaceId = 0;
                            if (!chunk.ReadLengthPrefixedString(bioFatherName, kMaxChildStringLen) ||
                                !chunk.ReadValue(oldRaceId)) {
                                return false;
                            }
                            std::uint32_t resolvedRaceId = 0;
                            if (oldRaceId != 0 && resolver.ResolveFormId(oldRaceId, resolvedRaceId) && resolvedRaceId != 0) {
                                bioFatherRaceId = FormRawId{ resolvedRaceId };
                            }
                        }

                        std::uint32_t newChild = 0;
                        // Synthetic child IDs (0xFF700000..0xFF7FFFFF) advance nextSyntheticChildRaw_;
                        // dynamic runtime actors (0xFF800000+, etc.) MUST NOT advance nextSyntheticChildRaw_!
                        const bool validChild = ResolveActorOrSpecialRaw(
                            resolver, oldChild, newChild,
                            /*allowZero=*/false,
                            /*allowUnknownFather=*/false,
                            /*allowSyntheticChild=*/true,
                            /*allowDynamicRuntime=*/true);

                        if (!validChild || newChild == 0) {
                            continue;
                        }

                        std::uint32_t newGA = 0;
                        std::uint32_t newGB = 0;
                        std::uint32_t newBioM = 0;
                        std::uint32_t newBioF = 0;
                        std::uint32_t newPresumed = 0;
                        (void)ResolveActorOrSpecialRaw(resolver, oldGA, newGA, true, false, false, true);
                        (void)ResolveActorOrSpecialRaw(resolver, oldGB, newGB, true, false, false, true);
                        (void)ResolveActorOrSpecialRaw(resolver, oldBioM, newBioM, true, false, false, true);
                        (void)ResolveActorOrSpecialRaw(resolver, oldBioF, newBioF, true, /*allowUnknownFather=*/true, false, true);
                        (void)ResolveActorOrSpecialRaw(resolver, oldPresumedFather, newPresumed, true, false, false, true);

                        if (newPresumed != 0 &&
                            ((!isBioChild && newBioM == 0) || newPresumed == newBioM ||
                             (newBioF != 0 && newPresumed == newBioF))) {
                            newPresumed = 0;
                        }

                        if (!hasPaternityBioFatherMeta && (isSec || disc || byPartner || byBio) &&
                            newBioF != 0 && newBioF != Foundation::kUnknownFatherRawId) {
                            if (bioFatherName.empty()) {
                                bioFatherName = resolver.GetActorName(ActorId{ newBioF });
                            }
                            if (bioFatherRaceId.IsNull()) {
                                bioFatherRaceId = resolver.GetActorRaceId(ActorId{ newBioF });
                            }
                        }

                        ChildRecord rec{};
                        rec.childId = ChildId{ newChild };
                        rec.guardians.slotA = ActorId{ newGA };
                        rec.guardians.slotB = ActorId{ newGB };
                        rec.name = std::move(childName);
                        rec.timestamp = ts;
                        rec.isAdopted = isAdopted;
                        rec.bioMother = ActorId{ newBioM };
                        rec.bioFather = ActorId{ newBioF };
                        rec.presumedFather = ActorId{ newPresumed };
                        rec.isBiologicalChild = isBioChild;
                        rec.conceptionTimestamp = concTs;
                        rec.birthTimestamp = birthTs;
                        rec.paternitySecret.isSecret = isSec;
                        rec.paternitySecret.discovered = disc;
                        rec.paternitySecret.discoveredByPartner = byPartner;
                        rec.paternitySecret.hasPhenotypeMismatch = mismatch;
                        rec.paternitySecret.lastCheckTimestamp = lastCheck;
                        rec.paternitySecret.isSecretForBioFather = secBio;
                        rec.paternitySecret.isSecretForPartner = secPartner;
                        rec.paternitySecret.discoveredByBioFather = byBio;
                        rec.paternitySecret.bioFatherName = std::move(bioFatherName);
                        rec.paternitySecret.bioFatherRaceId = bioFatherRaceId;
                        rec.stage = stage;
                        rec.trainingLocation = std::move(trainingLoc);
                        rec.gender = childGender;
                        rec.trainingClassIndex = trainClassIdx;
                        rec.trainingStartGameDay = trainStartDay;
                        rec.savedBioTemplateName = std::move(savedBioTpl);
                        rec.savedRaceIndex = savedRaceIdx;

                        // UpsertChild calls ObserveLoadedSyntheticId ONLY when AsSynthetic() is valid (0xFF700000..0xFF7FFFFF)
                        stagedLineage.UpsertChild(std::move(rec));

                        // Young children are NOT registered romance adults; Stage 4 Adult Children with profiles remain registered
                        if (stage != ChildStage::Adult) {
                            if (auto* maybeProf = stagedActors.FindMutable(RefId{ newChild })) {
                                maybeProf->isRegisteredAdult = false;
                            }
                        } else if (!Foundation::IsSyntheticChildRawId(newChild) &&
                                   stagedActors.Find(RefId{ newChild }) != nullptr) {
                            stagedActors.SetRegisteredAdult(RefId{ newChild }, true);
                        }
                    }
                }

                // v4 migration: infer presumedFather when absent from stream
                if (!hasPresumedFather) {
                    for (auto& [cId, entry] : stagedLineage.MutableChildren()) {
                        (void)cId;
                        if (entry.presumedFather.IsNull() &&
                            (entry.paternitySecret.isSecret ||
                             entry.paternitySecret.discovered ||
                             entry.paternitySecret.discoveredByPartner ||
                             entry.paternitySecret.discoveredByBioFather) &&
                            !entry.bioMother.IsNull()) {
                            ActorId candidate{};
                            if (entry.guardians.slotA == entry.bioMother && !entry.guardians.slotB.IsNull()) {
                                candidate = entry.guardians.slotB;
                            } else if (entry.guardians.slotB == entry.bioMother && !entry.guardians.slotA.IsNull()) {
                                candidate = entry.guardians.slotA;
                            } else {
                                candidate = stagedEdges.GetPrimaryPartner(entry.bioMother);
                            }
                            if (!candidate.IsNull() && candidate != entry.bioFather && candidate != entry.bioMother) {
                                entry.presumedFather = candidate;
                                ++stagedInferredPresumed;
                            }
                        }
                    }
                }

                // 6. Optional Inline Extension Blocks ('AGN1' -> 'PRG1' -> 'CHD1' -> 'LYC1' -> 'CHD2')
                std::uint32_t firstTrailingWord = 0;
                if (chunk.HasRemaining(sizeof(std::uint32_t)) && chunk.ReadValue(firstTrailingWord)) {
                    std::uint32_t extMagic = firstTrailingWord;
                    if (extMagic != CoSaveConstants::kExtAgencyAGN1 && chunk.HasRemaining(sizeof(std::uint32_t))) {
                        if (!chunk.ReadValue(extMagic) && chunk.ChunkLength() > 0) {
                            return false;
                        }
                    }
                    if (extMagic == CoSaveConstants::kExtAgencyAGN1) {
                        std::uint32_t stageRecCount = 0;
                        if (!chunk.ReadValue(stageRecCount) || stageRecCount > kMaxSerializedEntries) {
                            return false;
                        }
                        for (std::uint32_t sIdx = 0; sIdx < stageRecCount; ++sIdx) {
                            std::uint32_t oldA = 0;
                            std::uint32_t oldB = 0;
                            std::uint32_t oldCheater = 0;
                            std::uint32_t oldVictim = 0;
                            std::uint32_t oldAffair = 0;
                            std::uint8_t rawStage = 0;
                            float stageStartDay = 0.0f;
                            if (!chunk.ReadValue(oldA) || !chunk.ReadValue(oldB) ||
                                !chunk.ReadValue(oldCheater) || !chunk.ReadValue(oldVictim) ||
                                !chunk.ReadValue(oldAffair) || !chunk.ReadValue(rawStage) ||
                                !chunk.ReadValue(stageStartDay)) {
                                return false;
                            }
                            std::uint32_t newA = 0;
                            std::uint32_t newB = 0;
                            std::uint32_t newCheater = 0;
                            std::uint32_t newVictim = 0;
                            std::uint32_t newAffair = 0;
                            if (ResolveActorOrSpecialRaw(resolver, oldA, newA, false, false, false, true) &&
                                ResolveActorOrSpecialRaw(resolver, oldB, newB, false, false, false, true) &&
                                ResolveActorOrSpecialRaw(resolver, oldCheater, newCheater, false, false, false, true) &&
                                ResolveActorOrSpecialRaw(resolver, oldVictim, newVictim, false, false, false, true) &&
                                ResolveActorOrSpecialRaw(resolver, oldAffair, newAffair, false, false, false, true)) {
                                const AffairStage stg =
                                    rawStage < static_cast<std::uint8_t>(AffairStage::_Count)
                                        ? static_cast<AffairStage>(rawStage)
                                        : AffairStage::Physical;
                                stagedAffairs.UpdateStageMetadata(
                                    CanonicalPairKey{ ActorId{ newA }, ActorId{ newB } },
                                    ActorId{ newCheater },
                                    ActorId{ newVictim },
                                    ActorId{ newAffair },
                                    stg,
                                    stageStartDay);
                            }
                        }

                        std::uint32_t intentCount = 0;
                        if (!chunk.ReadValue(intentCount) || intentCount > kMaxSerializedEntries) {
                            return false;
                        }
                        for (std::uint32_t iIdx = 0; iIdx < intentCount; ++iIdx) {
                            std::uint32_t oldPartner = 0;
                            std::uint8_t rawIntent = 0;
                            float createdDay = -1.0f;
                            std::uint32_t oldLover = 0;
                            if (!chunk.ReadValue(oldPartner) || !chunk.ReadValue(rawIntent) ||
                                !chunk.ReadValue(createdDay) || !chunk.ReadValue(oldLover)) {
                                return false;
                            }
                            std::uint32_t newPartner = 0;
                            std::uint32_t newLover = 0;
                            const bool okPartner = ResolveActorOrSpecialRaw(resolver, oldPartner, newPartner, false, false, false, true);
                            (void)ResolveActorOrSpecialRaw(resolver, oldLover, newLover, true, false, false, true);
                            if (okPartner && newPartner != 0 &&
                                rawIntent > 0 && rawIntent < static_cast<std::uint8_t>(PartnerIntentType::_Count)) {
                                stagedEdges.SetPendingIntent(
                                    ActorId{ newPartner },
                                    PendingPartnerIntentRecord{
                                        .type = static_cast<PartnerIntentType>(rawIntent),
                                        .createdGameDay = createdDay,
                                        .relatedLover = ActorId{ newLover },
                                    });
                            }
                        }

                        // 'PRG1'
                        if (chunk.HasRemaining(sizeof(std::uint32_t))) {
                            std::uint32_t prgMagic = 0;
                            if (!chunk.ReadValue(prgMagic)) {
                                return false;
                            }
                            if (prgMagic == CoSaveConstants::kExtPregnancySecretPRG1) {
                                std::uint32_t pregSecretCount = 0;
                                if (!chunk.ReadValue(pregSecretCount) || pregSecretCount > kMaxSerializedEntries) {
                                    return false;
                                }
                                for (std::uint32_t pIdx = 0; pIdx < pregSecretCount; ++pIdx) {
                                    std::uint32_t oldChild = 0;
                                    bool isPregSecret = false;
                                    if (!chunk.ReadValue(oldChild) || !chunk.ReadValue(isPregSecret)) {
                                        return false;
                                    }
                                    std::uint32_t newChild = 0;
                                    if (ResolveActorOrSpecialRaw(resolver, oldChild, newChild, false, false, true, true)) {
                                        if (auto* c = stagedLineage.FindChildMutable(ChildId{ newChild })) {
                                            c->paternitySecret.isPregnancySecretForPartner =
                                                (c->stage == ChildStage::Unborn && c->paternitySecret.isSecretForPartner)
                                                    ? isPregSecret
                                                    : false;
                                        }
                                    }
                                }

                                // 'CHD1'
                                if (chunk.HasRemaining(sizeof(std::uint32_t))) {
                                    std::uint32_t chdMagic = 0;
                                    if (!chunk.ReadValue(chdMagic)) {
                                        return false;
                                    }
                                    if (chdMagic == CoSaveConstants::kExtChildVisualRpgCHD1) {
                                        std::uint32_t chdCount = 0;
                                        if (!chunk.ReadValue(chdCount) || chdCount > kMaxSerializedEntries) {
                                            return false;
                                        }
                                        for (std::uint32_t cIdx = 0; cIdx < chdCount; ++cIdx) {
                                            std::uint32_t oldChild = 0;
                                            std::uint32_t oldHairColor = 0;
                                            std::uint32_t oldEyes = 0;
                                            std::uint32_t oldHair = 0;
                                            std::uint32_t oldBeard = 0;
                                            float heightScale = 1.0f;
                                            bool rpgApplied = false;
                                            if (!chunk.ReadValue(oldChild) || !chunk.ReadValue(oldHairColor) ||
                                                !chunk.ReadValue(oldEyes) || !chunk.ReadValue(oldHair) ||
                                                !chunk.ReadValue(oldBeard) || !chunk.ReadValue(heightScale) ||
                                                !chunk.ReadValue(rpgApplied)) {
                                                return false;
                                            }
                                            std::uint32_t newChild = 0;
                                            if (ResolveActorOrSpecialRaw(resolver, oldChild, newChild, false, false, true, true)) {
                                                if (auto* c = stagedLineage.FindChildMutable(ChildId{ newChild })) {
                                                    std::uint32_t resolvedPart = 0;
                                                    if (oldHairColor != 0 && resolver.ResolveFormId(oldHairColor, resolvedPart)) {
                                                        c->hairColorFormId = FormRawId{ resolvedPart };
                                                    }
                                                    if (oldEyes != 0 && resolver.ResolveFormId(oldEyes, resolvedPart)) {
                                                        c->eyesPartFormId = FormRawId{ resolvedPart };
                                                    }
                                                    if (oldHair != 0 && resolver.ResolveFormId(oldHair, resolvedPart)) {
                                                        c->hairPartFormId = FormRawId{ resolvedPart };
                                                    }
                                                    if (oldBeard != 0 && resolver.ResolveFormId(oldBeard, resolvedPart)) {
                                                        c->facialHairPartFormId = FormRawId{ resolvedPart };
                                                    }
                                                    c->heightScale = (heightScale > 0.5f && heightScale < 1.5f) ? heightScale : 1.0f;
                                                    c->rpgApplied = rpgApplied;
                                                }
                                            }
                                        }

                                        // 'LYC1'
                                        if (chunk.HasRemaining(sizeof(std::uint32_t))) {
                                            std::uint32_t lycMagic = 0;
                                            if (!chunk.ReadValue(lycMagic)) {
                                                return false;
                                            }
                                            if (lycMagic == CoSaveConstants::kExtChildBeastbloodLYC1) {
                                                std::uint32_t lycCount = 0;
                                                if (!chunk.ReadValue(lycCount) || lycCount > kMaxSerializedEntries) {
                                                    return false;
                                                }
                                                for (std::uint32_t lIdx = 0; lIdx < lycCount; ++lIdx) {
                                                    std::uint32_t oldChild = 0;
                                                    bool hasBeastblood = false;
                                                    bool beastbloodEvaluated = false;
                                                    if (!chunk.ReadValue(oldChild) || !chunk.ReadValue(hasBeastblood) ||
                                                        !chunk.ReadValue(beastbloodEvaluated)) {
                                                        return false;
                                                    }
                                                    std::uint32_t newChild = 0;
                                                    if (ResolveActorOrSpecialRaw(resolver, oldChild, newChild, false, false, true, true)) {
                                                        if (auto* c = stagedLineage.FindChildMutable(ChildId{ newChild })) {
                                                            c->hasBeastblood = hasBeastblood;
                                                            c->beastbloodEvaluated = beastbloodEvaluated;
                                                        }
                                                    }
                                                }

                                                // 'CHD2'
                                                if (chunk.HasRemaining(sizeof(std::uint32_t))) {
                                                    std::uint32_t chd2Magic = 0;
                                                    if (!chunk.ReadValue(chd2Magic)) {
                                                        return false;
                                                    }
                                                    if (chd2Magic == CoSaveConstants::kExtChildLineageCHD2) {
                                                        std::uint32_t chd2Count = 0;
                                                        if (!chunk.ReadValue(chd2Count) || chd2Count > kMaxSerializedEntries) {
                                                            return false;
                                                        }
                                                        for (std::uint32_t c2Idx = 0; c2Idx < chd2Count; ++c2Idx) {
                                                            std::uint32_t oldChild = 0;
                                                            bool bioEval = false;
                                                            bool guardEval = false;
                                                            std::array<std::uint8_t, 18> bioSkills{};
                                                            std::array<std::uint8_t, 18> guardSkills{};
                                                            std::uint16_t lastLevel = 0;
                                                            std::string blessingId;

                                                            if (!chunk.ReadValue(oldChild) ||
                                                                !chunk.ReadValue(bioEval) ||
                                                                !chunk.ReadValue(guardEval) ||
                                                                !chunk.ReadBuffer(bioSkills.data(), static_cast<std::uint32_t>(bioSkills.size())) ||
                                                                !chunk.ReadBuffer(guardSkills.data(), static_cast<std::uint32_t>(guardSkills.size())) ||
                                                                !chunk.ReadValue(lastLevel) ||
                                                                !chunk.ReadLengthPrefixedString(blessingId, 256u)) {
                                                                return false;
                                                            }
                                                            std::uint32_t newChild = 0;
                                                            if (ResolveActorOrSpecialRaw(resolver, oldChild, newChild, false, false, true, true)) {
                                                                if (auto* c = stagedLineage.FindChildMutable(ChildId{ newChild })) {
                                                                    c->bioLineageEvaluated = bioEval;
                                                                    c->guardianLineageEvaluated = guardEval;
                                                                    c->bioSkillBonuses = bioSkills;
                                                                    c->guardianSkillBonuses = guardSkills;
                                                                    c->lastAppliedLevel = lastLevel;
                                                                    c->daedricBlessingId = std::move(blessingId);
                                                                }
                                                            }
                                                        }

                                                        // 'FRT1' (optional active fertility delivery batches) and/or 'SNU1' (optional SkyrimNet child UUIDs)
                                                        std::uint32_t nextExtMagic = 0;
                                                        if (chunk.HasRemaining(sizeof(std::uint32_t))) {
                                                            if (!chunk.ReadValue(nextExtMagic)) {
                                                                return false;
                                                            }
                                                        }
                                                        if (nextExtMagic == CoSaveConstants::kExtFertilityBatchFRT1) {
                                                            std::uint32_t frtCount = 0;
                                                            if (!chunk.ReadValue(frtCount) || frtCount > kMaxSerializedEntries) {
                                                                return false;
                                                            }
                                                            for (std::uint32_t fIdx = 0; fIdx < frtCount; ++fIdx) {
                                                                std::uint32_t oldMother = 0;
                                                                std::int32_t laborTotal = 0;
                                                                float laborStartDay = 0.0f;
                                                                std::uint32_t bornCount = 0;
                                                                if (!chunk.ReadValue(oldMother) ||
                                                                    !chunk.ReadValue(laborTotal) ||
                                                                    !chunk.ReadValue(laborStartDay) ||
                                                                    !chunk.ReadValue(bornCount) ||
                                                                    bornCount > kMaxSerializedEntries) {
                                                                    return false;
                                                                }
                                                                std::vector<ChildId> resolvedBorn;
                                                                resolvedBorn.reserve(bornCount);
                                                                for (std::uint32_t bIdx = 0; bIdx < bornCount; ++bIdx) {
                                                                    std::uint32_t oldChild = 0;
                                                                    if (!chunk.ReadValue(oldChild)) {
                                                                        return false;
                                                                    }
                                                                    std::uint32_t newChild = 0;
                                                                    if (ResolveActorOrSpecialRaw(resolver, oldChild, newChild, false, false, true, true)) {
                                                                        resolvedBorn.emplace_back(newChild);
                                                                    }
                                                                }
                                                                std::uint32_t newMother = 0;
                                                                if (ResolveActorOrSpecialRaw(resolver, oldMother, newMother, false, false, false, true)) {
                                                                    ActiveDeliveryBatch batch{};
                                                                    batch.laborTotal = laborTotal;
                                                                    batch.laborStartGameDay = laborStartDay;
                                                                    batch.bornChildIds = std::move(resolvedBorn);
                                                                    stagedLineage.UpsertActiveDeliveryBatch(ActorId{ newMother }, std::move(batch));
                                                                }
                                                            }

                                                            nextExtMagic = 0;
                                                            if (chunk.HasRemaining(sizeof(std::uint32_t))) {
                                                                if (!chunk.ReadValue(nextExtMagic)) {
                                                                    return false;
                                                                }
                                                            }
                                                        }
                                                        if (nextExtMagic != 0) {
                                                            if (nextExtMagic != CoSaveConstants::kExtChildSkyrimNetUuidSNU1) {
                                                                return false;
                                                            }
                                                            std::uint32_t snuCount = 0;
                                                            if (!chunk.ReadValue(snuCount) || snuCount > kMaxSerializedEntries) {
                                                                return false;
                                                            }
                                                            for (std::uint32_t sIdx = 0; sIdx < snuCount; ++sIdx) {
                                                                std::uint32_t oldChild = 0;
                                                                std::uint64_t snUuid = 0;
                                                                if (!chunk.ReadValue(oldChild) || !chunk.ReadValue(snUuid)) {
                                                                    return false;
                                                                }
                                                                std::uint32_t newChild = 0;
                                                                if (ResolveActorOrSpecialRaw(resolver, oldChild, newChild, false, false, true, true)) {
                                                                    if (auto* c = stagedLineage.FindChildMutable(ChildId{ newChild }); c && snUuid != 0) {
                                                                        c->skyrimNetUuid = snUuid;
                                                                    }
                                                                }
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            if (chunk.HasRemaining(1u)) {
                return false;
            }

            outInferredPresumedFathers = stagedInferredPresumed;
            state.actors.Reset(std::move(stagedActors));
            state.relationships.Reset(std::move(stagedEdges));
            state.affairs.Reset(std::move(stagedAffairs));
            state.lineage.Reset(std::move(stagedLineage));
            return true;
        }

        bool LoadChunkSPHR(
            WorldState& state,
            BoundedChunkReader& chunk,
            const IFormIdResolver& resolver) {
            std::uint32_t slotCount = 0;
            if (!chunk.ReadValue(slotCount) || slotCount > 64u) {
                return false;
            }
            SpouseHierarchyTable staged{};
            for (std::uint32_t i = 0; i < slotCount; ++i) {
                std::uint32_t oldId = 0;
                if (!chunk.ReadValue(oldId)) {
                    return false;
                }
                std::uint32_t newId = 0;
                if (i < SpouseHierarchyTable::kSlotCount && oldId != 0 &&
                    ResolveActorOrSpecialRaw(resolver, oldId, newId, false, false, false, true)) {
                    staged.SetRawSlot(i, ActorId{ newId });
                }
            }
            state.hierarchy.Reset(std::move(staged));
            return true;
        }

        bool LoadChunkAFCT(
            WorldState& state,
            BoundedChunkReader& chunk,
            std::uint32_t version,
            const IFormIdResolver& resolver) {
            AffectionTable staged{};
            for (const auto& [typeKey, bounds] : state.affection->AllMinMaxByType()) {
                staged.SetMinMaxForType(typeKey, bounds.first, bounds.second);
            }

            std::uint32_t permCount = 0;
            if (!chunk.ReadValue(permCount) || permCount > kMaxSerializedEntries) {
                return false;
            }
            for (std::uint32_t i = 0; i < permCount; ++i) {
                std::uint32_t oldId = 0;
                std::int32_t amount = 0;
                if (!chunk.ReadValue(oldId) || !chunk.ReadValue(amount)) {
                    return false;
                }
                std::uint32_t newId = 0;
                if (ResolveActorOrSpecialRaw(resolver, oldId, newId, false, false, false, true) && newId != 0) {
                    staged.SetPermanent(ActorId{ newId }, amount);
                }
            }

            // Optional last-interaction days block
            if (!chunk.HasRemaining(sizeof(std::uint32_t))) {
                state.affection.Reset(std::move(staged));
                return true;
            }
            std::uint32_t dayCount = 0;
            if (!chunk.ReadValue(dayCount)) {
                state.affection.Reset(std::move(staged));
                return true;
            }
            if (dayCount > kMaxSerializedEntries) {
                return false;
            }
            for (std::uint32_t i = 0; i < dayCount; ++i) {
                std::uint32_t oldId = 0;
                float day = 0.0f;
                if (!chunk.ReadValue(oldId) || !chunk.ReadValue(day)) {
                    return false;
                }
                std::uint32_t newId = 0;
                if (ResolveActorOrSpecialRaw(resolver, oldId, newId, false, false, false, true) && newId != 0) {
                    staged.SetLastInteractionDay(ActorId{ newId }, day);
                }
            }

            // v1–v3 legacy AffectionService::Save appended float decayMultiplier_ at the end of 'AFCT'
            float migratedV3DecayMultiplier = -1.0f;
            if (version < 4u && chunk.HasRemaining(sizeof(float))) {
                float rawDecay = 1.0f;
                if (!chunk.ReadValue(rawDecay)) {
                    return false;
                }
                if (!std::isnan(rawDecay) && rawDecay >= 0.0f) {
                    migratedV3DecayMultiplier = rawDecay;
                }
            }

            // v7+ daily accumulation block
            if (version >= 7 && chunk.HasRemaining(sizeof(std::uint32_t))) {
                std::uint32_t dailyActorCount = 0;
                if (!chunk.ReadValue(dailyActorCount) || dailyActorCount > kMaxSerializedEntries) {
                    return false;
                }
                for (std::uint32_t i = 0; i < dailyActorCount; ++i) {
                    std::uint32_t oldId = 0;
                    std::uint32_t typeCount = 0;
                    if (!chunk.ReadValue(oldId) || !chunk.ReadValue(typeCount) || typeCount > kMaxTypesPerActor) {
                        return false;
                    }
                    std::uint32_t newId = 0;
                    const bool validActor =
                        ResolveActorOrSpecialRaw(resolver, oldId, newId, false, false, false, true) && newId != 0;
                    for (std::uint32_t t = 0; t < typeCount; ++t) {
                        std::string typeName;
                        float amount = 0.0f;
                        if (!chunk.ReadLengthPrefixedString(typeName, kMaxChildStringLen) ||
                            !chunk.ReadValue(amount)) {
                            return false;
                        }
                        if (validActor) {
                            staged.SetDailyAccumulation(ActorId{ newId }, typeName, amount);
                        }
                    }
                }
            }

            if (migratedV3DecayMultiplier >= 0.0f) {
                state.settings.Write().affectionDecayMultiplier = migratedV3DecayMultiplier;
            }
            state.affection.Reset(std::move(staged));
            return true;
        }

        bool LoadChunkPHOU(
            WorldState& state,
            BoundedChunkReader& chunk,
            std::uint32_t version,
            const IFormIdResolver& resolver) {
            std::uint32_t houseCount = 0;
            if (!chunk.ReadValue(houseCount) || houseCount > kMaxHouses) {
                return false;
            }

            const auto preExistingChildStates = state.household->AllChildStates();
            HouseholdTable staged = CloneHouseholdWithoutHouses(state.household.Read());

            for (std::uint32_t i = 0; i < houseCount; ++i) {
                std::uint32_t savedHouse = 0;
                std::uint32_t savedMarker = 0;
                bool moveDone = false;
                if (!chunk.ReadValue(savedHouse) || !chunk.ReadValue(savedMarker)) {
                    return false;
                }
                if (version >= 4 && !chunk.ReadValue(moveDone)) {
                    return false;
                }

                std::uint32_t tenantCount = 0;
                if (!chunk.ReadValue(tenantCount) || tenantCount > kMaxTenants) {
                    return false;
                }

                std::vector<std::pair<std::uint32_t, bool>> rawTenants;
                rawTenants.reserve(tenantCount);
                for (std::uint32_t j = 0; j < tenantCount; ++j) {
                    std::uint32_t savedTid = 0;
                    bool stayAtHome = true;
                    if (!chunk.ReadValue(savedTid)) {
                        return false;
                    }
                    if (version >= 4 && !chunk.ReadValue(stayAtHome)) {
                        return false;
                    }
                    rawTenants.emplace_back(savedTid, stayAtHome);
                }

                std::uint32_t resolvedHouse = 0;
                if (savedHouse == 0 || !resolver.ResolveFormId(savedHouse, resolvedHouse) || resolvedHouse == 0) {
                    continue;
                }
                std::uint32_t resolvedMarker = 0;
                if (savedMarker != 0) {
                    (void)resolver.ResolveFormId(savedMarker, resolvedMarker);
                }

                const HouseId houseId{ resolvedHouse };
                staged.RegisterHouse(houseId, RefId{ resolvedMarker }, moveDone);

                for (const auto& [savedTid, stayAtHome] : rawTenants) {
                    std::uint32_t resolvedTid = 0;
                    if (!ResolveActorOrSpecialRaw(resolver, savedTid, resolvedTid, false, false, /*allowSynthetic=*/true, true)) {
                        continue;
                    }
                    if (resolvedTid == 0 || resolvedTid == Foundation::kPlayerRefRawId) {
                        continue;
                    }
                    const bool isSynChild = Foundation::IsSyntheticChildRawId(resolvedTid);
                    staged.AssignTenant(
                        resolvedTid,
                        houseId,
                        isSynChild ? true : stayAtHome,
                        isSynChild || preExistingChildStates.contains(ChildId{ resolvedTid }));
                }
            }

            // Preserve any richer ChildHouseholdRecord fields loaded if PLCH arrived prior to PHOU
            for (const auto& [cId, prevState] : preExistingChildStates) {
                ChildHouseholdRecord merged = prevState;
                if (merged.currentHouseId.IsNull()) {
                    merged.currentHouseId = staged.GetTenantHouse(cId.raw());
                }
                staged.UpsertChildState(std::move(merged));
            }

            state.household.Reset(std::move(staged));
            return true;
        }

        bool LoadChunkSPAS(
            WorldState& state,
            BoundedChunkReader& chunk,
            std::uint32_t version,
            const IFormIdResolver& resolver) {
            std::uint32_t cellCount = 0;
            if (!chunk.ReadValue(cellCount) || cellCount > kMaxSharedCells) {
                return false;
            }

            HouseholdTable staged = CloneHouseholdWithoutSpouseAssets(state.household.Read());

            for (std::uint32_t i = 0; i < cellCount; ++i) {
                std::uint32_t savedCell = 0;
                std::uint8_t sharedFlag = 0;
                std::uint8_t recordedFlag = 0;
                std::uint8_t origStateFlag = 0;
                std::uint32_t sharingCount = 0;

                if (!chunk.ReadValue(savedCell) ||
                    !chunk.ReadValue(sharedFlag) ||
                    !chunk.ReadValue(recordedFlag) ||
                    !chunk.ReadValue(origStateFlag) ||
                    !chunk.ReadValue(sharingCount) ||
                    sharingCount > kMaxSharedCells) {
                    return false;
                }

                std::uint32_t resolvedCell = 0;
                const bool validCell = (savedCell != 0 && resolver.ResolveFormId(savedCell, resolvedCell) && resolvedCell != 0);

                SharedCellRecord cellRec{};
                cellRec.sharedWithPlayer = (sharedFlag != 0);
                cellRec.originalPublicRecorded = (recordedFlag != 0);
                cellRec.originalPublicState = (origStateFlag != 0);

                for (std::uint32_t j = 0; j < sharingCount; ++j) {
                    std::uint32_t oldSpouse = 0;
                    if (!chunk.ReadValue(oldSpouse)) {
                        return false;
                    }
                    std::uint32_t newSpouse = 0;
                    if (validCell && ResolveActorOrSpecialRaw(resolver, oldSpouse, newSpouse, false, false, false, true) && newSpouse != 0) {
                        cellRec.sharingSpouses.insert(ActorId{ newSpouse });
                    }
                }

                if (validCell) {
                    staged.UpsertSharedCell(CellId{ resolvedCell }, std::move(cellRec));
                }
            }

            if (version >= 7 && chunk.HasRemaining(sizeof(std::uint32_t))) {
                std::uint32_t bedOwnerCount = 0;
                if (!chunk.ReadValue(bedOwnerCount) || bedOwnerCount > kMaxSharedCells) {
                    return false;
                }
                for (std::uint32_t i = 0; i < bedOwnerCount; ++i) {
                    std::uint32_t savedBed = 0;
                    std::uint32_t savedOwner = 0;
                    if (!chunk.ReadValue(savedBed) || !chunk.ReadValue(savedOwner)) {
                        return false;
                    }
                    std::uint32_t resolvedBed = 0;
                    if (savedBed == 0 || !resolver.ResolveFormId(savedBed, resolvedBed) || resolvedBed == 0) {
                        continue;
                    }
                    // Owner 0 == originally unowned bed and MUST be preserved verbatim!
                    std::uint32_t resolvedOwner = 0;
                    if (savedOwner != 0) {
                        if (!ResolveActorOrSpecialRaw(resolver, savedOwner, resolvedOwner, false, false, false, true)) {
                            continue;
                        }
                    }
                    staged.SetOriginalBedOwner(RefId{ resolvedBed }, FormRawId{ resolvedOwner });
                }
            }

            state.household.Reset(std::move(staged));
            return true;
        }

        bool LoadChunkPLCH(
            WorldState& state,
            BoundedChunkReader& chunk,
            const IFormIdResolver& resolver) {
            bool chestIntroduced = false;
            std::uint32_t count = 0;
            if (!chunk.ReadValue(chestIntroduced) || !chunk.ReadValue(count) || count > kMaxTenants) {
                return false;
            }

            HouseholdTable staged = state.household.Read();
            staged.ClearOnlyPlayerChildrenRecords();
            staged.SetChildChestIntroduced(chestIntroduced);

            for (std::uint32_t i = 0; i < count; ++i) {
                std::uint32_t oldCid = 0;
                std::uint32_t oldHouse = 0;
                std::uint32_t oldHomeMarker = 0;
                std::uint32_t oldPet = 0;
                std::uint32_t oldCritter = 0;
                ChildHouseholdRecord rec{};

                if (!chunk.ReadValue(oldCid) ||
                    !chunk.ReadValue(oldHouse) ||
                    !chunk.ReadValue(oldHomeMarker) ||
                    !chunk.ReadValue(oldPet) ||
                    !chunk.ReadValue(oldCritter) ||
                    !chunk.ReadValue(rec.currentOrder) ||
                    !chunk.ReadValue(rec.orderExpiryGameDay) ||
                    !chunk.ReadValue(rec.forcegreetPending) ||
                    !chunk.ReadValue(rec.critterBanned) ||
                    !chunk.ReadValue(rec.petBanned) ||
                    !chunk.ReadValue(rec.isNewlyAdopted) ||
                    !chunk.ReadValue(rec.critterRetryTimestamp) ||
                    !chunk.ReadValue(rec.petRetryTimestamp) ||
                    !chunk.ReadValue(rec.playedScene1) ||
                    !chunk.ReadValue(rec.playedScene2) ||
                    !chunk.ReadValue(rec.lastInteractionTimestamp)) {
                    return false;
                }

                std::uint32_t newCid = 0;
                if (!ResolveActorOrSpecialRaw(resolver, oldCid, newCid, false, false, /*allowSynthetic=*/true, true) ||
                    newCid == 0) {
                    continue;
                }
                rec.childId = ChildId{ newCid };

                std::uint32_t resolvedId = 0;
                if (oldHouse != 0 && resolver.ResolveFormId(oldHouse, resolvedId) && resolvedId != 0) {
                    rec.currentHouseId = HouseId{ resolvedId };
                } else {
                    rec.currentHouseId = staged.GetTenantHouse(newCid);
                }
                if (oldHomeMarker != 0 && resolver.ResolveFormId(oldHomeMarker, resolvedId) && resolvedId != 0) {
                    rec.homeMarkerId = RefId{ resolvedId };
                }
                if (oldPet != 0 && ResolveActorOrSpecialRaw(resolver, oldPet, resolvedId, false, false, false, true)) {
                    rec.petId = RefId{ resolvedId };
                }
                if (oldCritter != 0 && ResolveActorOrSpecialRaw(resolver, oldCritter, resolvedId, false, false, false, true)) {
                    rec.critterId = RefId{ resolvedId };
                }

                staged.UpsertChildState(std::move(rec));
            }

            if (chunk.HasRemaining(1u)) {
                std::uint32_t extTag = 0;
                if (!chunk.ReadValue(extTag) || extTag != CoSaveConstants::kExtPlayerChildrenStatePLC1) {
                    return false;
                }
                std::int32_t poorCounter = 0;
                bool nameCallingUsedLast = false;
                std::uint32_t transientCount = 0;
                if (!chunk.ReadValue(poorCounter) ||
                    !chunk.ReadValue(nameCallingUsedLast) ||
                    !chunk.ReadValue(transientCount) ||
                    transientCount > kMaxTenants) {
                    return false;
                }
                staged.SetPoorCounter(poorCounter);
                staged.SetNameCallingUsedLast(nameCallingUsedLast);

                for (std::uint32_t t = 0; t < transientCount; ++t) {
                    std::uint32_t oldChild = 0;
                    std::uint32_t oldCritter = 0;
                    if (!chunk.ReadValue(oldChild) || !chunk.ReadValue(oldCritter)) {
                        return false;
                    }
                    std::uint32_t newChild = 0;
                    std::uint32_t newCritter = 0;
                    if (!Foundation::IsSyntheticChildRawId(oldChild) &&
                        ResolveActorOrSpecialRaw(resolver, oldChild, newChild, false, false, false, true) &&
                        ResolveActorOrSpecialRaw(resolver, oldCritter, newCritter, false, false, false, true) &&
                        newChild != 0 && newCritter != 0) {
                        if (auto* childState = staged.FindChildStateMutable(ChildId{ newChild })) {
                            childState->transientCritterId = RefId{ newCritter };
                        }
                    }
                }
            }

            if (chunk.HasRemaining(1u)) {
                return false;
            }

            state.household.Reset(std::move(staged));
            return true;
        }

        bool LoadChunkCFSN(
            WorldState& state,
            BoundedChunkReader& chunk,
            const IFormIdResolver& resolver) {
            std::uint32_t count = 0;
            if (!chunk.ReadValue(count) || count > kMaxTenants) {
                return false;
            }

            HouseholdTable staged = state.household.Read();
            staged.ClearChildFactionSnapshots();

            for (std::uint32_t i = 0; i < count; ++i) {
                std::uint32_t oldChild = 0;
                std::uint32_t oldCrimeFac = 0;
                std::uint32_t fCount = 0;
                if (!chunk.ReadValue(oldChild) || !chunk.ReadValue(oldCrimeFac) ||
                    !chunk.ReadValue(fCount) || fCount > 1000u) {
                    return false;
                }

                std::uint32_t newChild = 0;
                const bool validChild =
                    !Foundation::IsSyntheticChildRawId(oldChild) &&
                    ResolveActorOrSpecialRaw(resolver, oldChild, newChild, false, false, false, true) &&
                    newChild != 0;

                ChildFactionSnapshotRecord snap{};
                std::uint32_t newCrimeFac = 0;
                if (oldCrimeFac != 0 && resolver.ResolveFormId(oldCrimeFac, newCrimeFac)) {
                    snap.crimeFactionId = FormRawId{ newCrimeFac };
                }

                for (std::uint32_t j = 0; j < fCount; ++j) {
                    std::uint32_t oldFac = 0;
                    std::int8_t rank = 0;
                    if (!chunk.ReadValue(oldFac) || !chunk.ReadValue(rank)) {
                        return false;
                    }
                    std::uint32_t newFac = 0;
                    if (validChild && oldFac != 0 && resolver.ResolveFormId(oldFac, newFac) && newFac != 0) {
                        snap.factions.push_back(FactionRankSnapshotEntry{ FormRawId{ newFac }, rank });
                    }
                }

                if (validChild) {
                    staged.UpsertChildFactionSnapshot(ChildId{ newChild }, std::move(snap));
                }
            }

            state.household.Reset(std::move(staged));
            return true;
        }

        bool LoadChunkMKMK(
            WorldState& state,
            BoundedChunkReader& chunk,
            const IFormIdResolver& resolver) {
            CooldownTable staged = state.cooldowns.Read();
            staged.ClearMatchmaking();

            if (!chunk.ReadValue(staged.lastMatchmakingDay)) {
                return false;
            }

            if (chunk.HasRemaining(1u)) {
                std::uint32_t count = 0;
                if (!chunk.ReadValue(count) || count > kMaxSerializedEntries) {
                    return false;
                }
                for (std::uint32_t i = 0; i < count; ++i) {
                    std::uint32_t savedId = 0;
                    float day = 0.0f;
                    if (!chunk.ReadValue(savedId) || !chunk.ReadValue(day)) {
                        return false;
                    }
                    std::uint32_t resolvedId = 0;
                    if (!Foundation::IsSyntheticChildRawId(savedId) &&
                        ResolveActorOrSpecialRaw(resolver, savedId, resolvedId, false, false, false, true) &&
                        resolvedId != 0) {
                        staged.npcLastMatchmakingChangeDay[ActorId{ resolvedId }] = day;
                    }
                }

                if (chunk.HasRemaining(1u)) {
                    float partnerCheatDay = -1.0f;
                    if (!chunk.ReadValue(partnerCheatDay)) {
                        return false;
                    }
                    staged.lastPlayerPartnerCheatingDay = partnerCheatDay;

                    if (chunk.HasRemaining(1u)) {
                        std::uint32_t locCount = 0;
                        if (!chunk.ReadValue(locCount) || locCount > kMaxSerializedEntries) {
                            return false;
                        }
                        for (std::uint32_t i = 0; i < locCount; ++i) {
                            std::uint32_t savedLoc = 0;
                            float locDay = 0.0f;
                            if (!chunk.ReadValue(savedLoc) || !chunk.ReadValue(locDay)) {
                                return false;
                            }
                            std::uint32_t resolvedLoc = 0;
                            if (savedLoc != 0 && resolver.ResolveFormId(savedLoc, resolvedLoc) && resolvedLoc != 0) {
                                staged.lastHomeArrivalEventDayByLoc[LocationId{ resolvedLoc }] = locDay;
                            }
                        }
                    }
                }
            }

            if (chunk.HasRemaining(1u)) {
                std::uint32_t relMagic = 0;
                if (!chunk.ReadValue(relMagic)) {
                    return false;
                }
                if (relMagic == CoSaveConstants::kExtRelationshipStateREL1) {
                    if (!chunk.ReadValue(staged.lastTimePaidDivorceFee) ||
                        !chunk.ReadValue(staged.playerKiller) ||
                        !chunk.ReadValue(staged.playerHadWedding) ||
                        !chunk.ReadValue(staged.dontShowSkipWeddingMessage)) {
                        return false;
                    }

                    std::uint32_t brokeupCount = 0;
                    if (!chunk.ReadValue(brokeupCount) || brokeupCount > kMaxSerializedEntries) {
                        return false;
                    }
                    for (std::uint32_t i = 0; i < brokeupCount; ++i) {
                        std::uint32_t savedId = 0;
                        float day = -1.0f;
                        if (!chunk.ReadValue(savedId) || !chunk.ReadValue(day)) {
                            return false;
                        }
                        std::uint32_t resolvedId = 0;
                        if (!Foundation::IsSyntheticChildRawId(savedId) &&
                            ResolveActorOrSpecialRaw(resolver, savedId, resolvedId, false, false, false, true) &&
                            resolvedId != 0) {
                            staged.npcBrokeupTime[ActorId{ resolvedId }] = day;
                        }
                    }

                    std::uint32_t incomeCount = 0;
                    if (!chunk.ReadValue(incomeCount) || incomeCount > kMaxSerializedEntries) {
                        return false;
                    }
                    for (std::uint32_t i = 0; i < incomeCount; ++i) {
                        std::uint32_t savedId = 0;
                        float day = -1.0f;
                        if (!chunk.ReadValue(savedId) || !chunk.ReadValue(day)) {
                            return false;
                        }
                        std::uint32_t resolvedId = 0;
                        if (!Foundation::IsSyntheticChildRawId(savedId) &&
                            ResolveActorOrSpecialRaw(resolver, savedId, resolvedId, false, false, false, true) &&
                            resolvedId != 0) {
                            staged.npcLastTimeSharedIncome[ActorId{ resolvedId }] = day;
                        }
                    }
                } else {
                    return false;
                }
            }

            if (chunk.HasRemaining(1u)) {
                return false;
            }

            state.cooldowns.Reset(std::move(staged));
            return true;
        }

        bool LoadChunkDCSD(
            WorldState& state,
            BoundedChunkReader& chunk,
            std::uint32_t version,
            const IFormIdResolver& resolver) {
            std::uint32_t count = 0;
            if (!chunk.ReadValue(count) || count > kMaxSerializedEntries) {
                return false;
            }

            auto resolveOrDrop = [&](std::uint32_t oldId) -> std::uint32_t {
                std::uint32_t resolved = 0;
                if (ResolveActorOrSpecialRaw(resolver, oldId, resolved, true, true, true, true)) {
                    return resolved;
                }
                return 0;
            };

            auto readChildInfo = [&](std::optional<DeceasedChildMetadata>& outCi) -> bool {
                bool hasCi = false;
                if (!chunk.ReadValue(hasCi)) {
                    return false;
                }
                if (!hasCi) {
                    outCi.reset();
                    return true;
                }
                std::uint32_t gABase = 0;
                std::uint32_t gARef = 0;
                std::string gAName;
                std::uint32_t gBBase = 0;
                std::uint32_t gBRef = 0;
                std::string gBName;
                std::uint32_t bioMBase = 0;
                std::uint32_t bioFBase = 0;
                bool isAdopted = true;
                std::uint32_t houseRaw = 0;

                if (!chunk.ReadValue(gABase) ||
                    !chunk.ReadValue(gARef) ||
                    !chunk.ReadLengthPrefixedString(gAName, kMaxDeceasedStringLen) ||
                    !chunk.ReadValue(gBBase) ||
                    !chunk.ReadValue(gBRef) ||
                    !chunk.ReadLengthPrefixedString(gBName, kMaxDeceasedStringLen) ||
                    !chunk.ReadValue(bioMBase) ||
                    !chunk.ReadValue(bioFBase) ||
                    !chunk.ReadValue(isAdopted) ||
                    !chunk.ReadValue(houseRaw)) {
                    return false;
                }

                DeceasedChildMetadata ci{};
                ci.guardianARefId = RefId{ resolveOrDrop(gARef) };
                ci.guardianABaseId = ci.guardianARefId.IsDynamicRuntimeRef()
                                         ? BaseId{ ci.guardianARefId.raw() }
                                         : BaseId{ resolveOrDrop(gABase) };
                ci.guardianAName = std::move(gAName);

                ci.guardianBRefId = RefId{ resolveOrDrop(gBRef) };
                ci.guardianBBaseId = ci.guardianBRefId.IsDynamicRuntimeRef()
                                         ? BaseId{ ci.guardianBRefId.raw() }
                                         : BaseId{ resolveOrDrop(gBBase) };
                ci.guardianBName = std::move(gBName);

                ci.bioMotherBaseId = BaseId{ resolveOrDrop(bioMBase) };
                ci.bioFatherBaseId = BaseId{ resolveOrDrop(bioFBase) };
                ci.isAdopted = isAdopted;
                std::uint32_t resolvedHouse = 0;
                if (houseRaw != 0 && resolver.ResolveFormId(houseRaw, resolvedHouse)) {
                    ci.houseId = HouseId{ resolvedHouse };
                }
                outCi = std::move(ci);
                return true;
            };

            std::vector<DeceasedActorRecord> loadedRecords;
            loadedRecords.reserve(count);
            std::unordered_map<std::uint32_t, std::size_t> keyToIndex;
            for (std::uint32_t i = 0; i < count; ++i) {
                std::uint32_t oldBase = 0;
                std::uint32_t oldRef = 0;
                DeceasedActorRecord rec{};
                std::uint32_t oldKillerBase = 0;
                std::uint32_t oldKillerRef = 0;

                if (!chunk.ReadValue(oldBase) ||
                    !chunk.ReadValue(oldRef) ||
                    !chunk.ReadLengthPrefixedString(rec.name, kMaxDeceasedStringLen) ||
                    !chunk.ReadValue(rec.deathTimestamp) ||
                    !chunk.ReadValue(rec.isChild) ||
                    !chunk.ReadValue(rec.wasKilledByPlayer) ||
                    !chunk.ReadValue(oldKillerBase) ||
                    !chunk.ReadValue(oldKillerRef) ||
                    !chunk.ReadLengthPrefixedString(rec.killerName, kMaxDeceasedStringLen)) {
                    return false;
                }

                rec.refId = RefId{ resolveOrDrop(oldRef) };
                rec.baseId = rec.refId.IsDynamicRuntimeRef()
                                 ? BaseId{ rec.refId.raw() }
                                 : BaseId{ resolveOrDrop(oldBase) };
                rec.killerRefId = RefId{ resolveOrDrop(oldKillerRef) };
                rec.killerBaseId = rec.killerRefId.IsDynamicRuntimeRef()
                                       ? BaseId{ rec.killerRefId.raw() }
                                       : BaseId{ resolveOrDrop(oldKillerBase) };

                if (!rec.isChild) {
                    std::uint8_t sc = 0;
                    std::uint8_t st = 0;
                    std::uint8_t tp = 0;
                    std::uint32_t partnerCount = 0;
                    if (!chunk.ReadValue(sc) || !chunk.ReadValue(st) || !chunk.ReadValue(tp) ||
                        !chunk.ReadValue(partnerCount) || partnerCount > kMaxTenants) {
                        return false;
                    }
                    rec.socialClass = static_cast<SocialClass>(sc);
                    rec.skillType = static_cast<SkillType>(st);
                    rec.temperament = static_cast<Temperament>(tp);

                    for (std::uint32_t p = 0; p < partnerCount; ++p) {
                        std::uint32_t pBase = 0;
                        std::uint32_t pRef = 0;
                        std::string pName;
                        std::uint8_t relSt = 0;
                        bool isPlayer = false;
                        if (!chunk.ReadValue(pBase) ||
                            !chunk.ReadValue(pRef) ||
                            !chunk.ReadLengthPrefixedString(pName, kMaxDeceasedStringLen) ||
                            !chunk.ReadValue(relSt) ||
                            !chunk.ReadValue(isPlayer)) {
                            return false;
                        }
                        DeceasedPartnerEntry pe{};
                        pe.isPlayer = isPlayer;
                        pe.partnerRefId = isPlayer ? RefId::Player() : RefId{ resolveOrDrop(pRef) };
                        pe.partnerBaseId = isPlayer
                                               ? BaseId{ Foundation::kPlayerRefRawId }
                                               : (pe.partnerRefId.IsDynamicRuntimeRef()
                                                      ? BaseId{ pe.partnerRefId.raw() }
                                                      : BaseId{ resolveOrDrop(pBase) });
                        pe.partnerName = std::move(pName);
                        pe.statusAtDeath = static_cast<RelationshipStatus>(relSt);
                        if (pe.isPlayer || !pe.partnerBaseId.IsNull() || !pe.partnerRefId.IsNull()) {
                            rec.partnersAtDeath.push_back(std::move(pe));
                        }
                    }

                    if (version >= 7) {
                        if (!readChildInfo(rec.childInfo)) {
                            return false;
                        }
                    }
                } else {
                    if (!readChildInfo(rec.childInfo)) {
                        return false;
                    }
                }

                if (!rec.baseId.IsNull() || !rec.refId.IsNull()) {
                    const std::uint32_t pKey = DeceasedTable::ComputePrimaryKey(rec.baseId, rec.refId);
                    keyToIndex[pKey] = loadedRecords.size();
                    loadedRecords.push_back(std::move(rec));
                }
            }

            if (chunk.HasRemaining(1u)) {
                std::uint32_t dcsMagic = 0;
                if (!chunk.ReadValue(dcsMagic)) {
                    return false;
                }
                if (dcsMagic == CoSaveConstants::kExtDeceasedSecrecyDCS1) {
                    std::uint32_t secretCount = 0;
                    if (!chunk.ReadValue(secretCount) || secretCount > kMaxSerializedEntries) {
                        return false;
                    }
                    for (std::uint32_t sIdx = 0; sIdx < secretCount; ++sIdx) {
                        std::uint32_t oldBase = 0;
                        std::uint32_t oldRef = 0;
                        std::uint32_t secPartnerCount = 0;
                        if (!chunk.ReadValue(oldBase) ||
                            !chunk.ReadValue(oldRef) ||
                            !chunk.ReadValue(secPartnerCount) ||
                            secPartnerCount > kMaxTenants) {
                            return false;
                        }
                        const RefId resRef{ resolveOrDrop(oldRef) };
                        const BaseId resBase = resRef.IsDynamicRuntimeRef()
                                                   ? BaseId{ resRef.raw() }
                                                   : BaseId{ resolveOrDrop(oldBase) };
                        DeceasedActorRecord* targetRec = nullptr;
                        if (!resBase.IsNull() || !resRef.IsNull()) {
                            const std::uint32_t pKey = DeceasedTable::ComputePrimaryKey(resBase, resRef);
                            if (const auto idxIt = keyToIndex.find(pKey); idxIt != keyToIndex.end()) {
                                targetRec = &loadedRecords[idxIt->second];
                            }
                        }

                        for (std::uint32_t sp = 0; sp < secPartnerCount; ++sp) {
                            std::uint32_t oldPBase = 0;
                            std::uint32_t oldPRef = 0;
                            std::uint8_t rawPubStatus = 0;
                            if (!chunk.ReadValue(oldPBase) ||
                                !chunk.ReadValue(oldPRef) ||
                                !chunk.ReadValue(rawPubStatus)) {
                                return false;
                            }
                            if (!targetRec) {
                                continue;
                            }
                            const RefId pRef{ resolveOrDrop(oldPRef) };
                            const BaseId pBase = pRef.IsDynamicRuntimeRef()
                                                     ? BaseId{ pRef.raw() }
                                                     : BaseId{ resolveOrDrop(oldPBase) };
                            const auto pubStatus =
                                rawPubStatus <= static_cast<std::uint8_t>(RelationshipStatus::ExLover)
                                    ? static_cast<RelationshipStatus>(rawPubStatus)
                                    : RelationshipStatus::Unknown;
                            for (auto& pEntry : targetRec->partnersAtDeath) {
                                if ((!pRef.IsNull() && pEntry.partnerRefId == pRef) ||
                                    (!pBase.IsNull() && pEntry.partnerBaseId == pBase)) {
                                    pEntry.isSecretAffair = true;
                                    pEntry.publicStatusAtDeath = pubStatus;
                                    break;
                                }
                            }
                        }

                        bool hasSecretPaternity = false;
                        if (!chunk.ReadValue(hasSecretPaternity)) {
                            return false;
                        }
                        if (hasSecretPaternity) {
                            std::uint32_t oldPubFatherBase = 0;
                            if (!chunk.ReadValue(oldPubFatherBase)) {
                                return false;
                            }
                            if (targetRec && targetRec->childInfo.has_value()) {
                                targetRec->childInfo->hasUndiscoveredPaternitySecret = true;
                                targetRec->childInfo->publicBioFatherBaseId =
                                    BaseId{ resolveOrDrop(oldPubFatherBase) };
                            }
                        }
                    }
                } else {
                    return false;
                }
            }

            if (chunk.HasRemaining(1u)) {
                return false;
            }

            DeceasedTable staged{};
            for (auto& rec : loadedRecords) {
                staged.UpsertRecord(std::move(rec));
            }

            state.deceased.Reset(std::move(staged));
            return true;
        }

        bool LoadChunkBNTR(
            WorldState& state,
            BoundedChunkReader& chunk,
            std::uint32_t version,
            const IFormIdResolver& resolver) {
            float cooldownHours = 24.0f;
            std::uint32_t count = 0;
            if (!chunk.ReadValue(cooldownHours) || !chunk.ReadValue(count) || count > kMaxSerializedEntries) {
                return false;
            }

            CooldownTable staged = state.cooldowns.Read();
            staged.ClearBanter(cooldownHours);
            staged.ClearBanterSceneRuntimeState();

            for (std::uint32_t i = 0; i < count; ++i) {
                std::uint32_t oldA = 0;
                std::uint32_t oldB = 0;
                float ts = 0.0f;
                if (!chunk.ReadValue(oldA) || !chunk.ReadValue(oldB) || !chunk.ReadValue(ts)) {
                    return false;
                }
                if (Foundation::IsSyntheticChildRawId(oldA) || Foundation::IsSyntheticChildRawId(oldB)) {
                    continue;
                }
                std::uint32_t newA = 0;
                std::uint32_t newB = 0;
                if (ResolveActorOrSpecialRaw(resolver, oldA, newA, false, false, false, true) &&
                    ResolveActorOrSpecialRaw(resolver, oldB, newB, false, false, false, true) &&
                    newA != 0 && newB != 0 && newA != newB) {
                    staged.banterCoupleCooldowns[CanonicalPairKey{ ActorId{ newA }, ActorId{ newB } }] = ts;
                }
            }

            if (version >= 2 && chunk.HasRemaining(1u)) {
                float lastSceneDay = -1.0f;
                if (!chunk.ReadValue(lastSceneDay)) {
                    return false;
                }
                staged.lastBanterSceneDay = lastSceneDay;

                if (chunk.HasRemaining(1u)) {
                    std::uint32_t extTag = 0;
                    std::uint32_t movedCount = 0;
                    if (!chunk.ReadValue(extTag) ||
                        extTag != CoSaveConstants::kExtBanterSceneStateBNT1 ||
                        !chunk.ReadValue(movedCount) ||
                        movedCount > kMaxSerializedEntries) {
                        return false;
                    }
                    for (std::uint32_t m = 0; m < movedCount; ++m) {
                        std::uint32_t oldActor = 0;
                        std::uint32_t oldMarker = 0;
                        if (!chunk.ReadValue(oldActor) || !chunk.ReadValue(oldMarker)) {
                            return false;
                        }
                        std::uint32_t newActor = 0;
                        if (Foundation::IsSyntheticChildRawId(oldActor) ||
                            !ResolveActorOrSpecialRaw(resolver, oldActor, newActor, false, false, false, true) ||
                            newActor == 0 || newActor == Foundation::kPlayerRefRawId) {
                            continue;
                        }
                        std::uint32_t newMarker = 0;
                        if (Foundation::IsDynamicPrefixRawId(oldMarker)) {
                            if (oldMarker != 0xFFFFFFFFu) {
                                newMarker = oldMarker;
                            }
                        } else if (oldMarker != 0) {
                            (void)resolver.ResolveFormId(oldMarker, newMarker);
                        }
                        if (newMarker != 0) {
                            staged.movedSceneActors[ActorId{ newActor }] = RefId{ newMarker };
                        }
                    }

                    std::uint32_t mutedCount = 0;
                    if (!chunk.ReadValue(mutedCount) || mutedCount > kMaxSerializedEntries) {
                        return false;
                    }
                    std::unordered_set<ActorId> seenMuted;
                    for (std::uint32_t u = 0; u < mutedCount; ++u) {
                        std::uint32_t oldActor = 0;
                        if (!chunk.ReadValue(oldActor)) {
                            return false;
                        }
                        std::uint32_t newActor = 0;
                        if (!Foundation::IsSyntheticChildRawId(oldActor) &&
                            ResolveActorOrSpecialRaw(resolver, oldActor, newActor, false, false, false, true) &&
                            newActor != 0 && newActor != Foundation::kPlayerRefRawId) {
                            const ActorId actor{ newActor };
                            if (seenMuted.insert(actor).second) {
                                staged.skyrimNetMutedActors.push_back(actor);
                            }
                        }
                    }
                }
            }

            if (chunk.HasRemaining(1u)) {
                return false;
            }

            state.cooldowns.Reset(std::move(staged));
            return true;
        }

        bool LoadChunkACTR(
            WorldState& state,
            BoundedChunkReader& chunk,
            const IFormIdResolver& resolver) {
            std::uint32_t magic = 0;
            std::uint32_t count = 0;
            if (!chunk.ReadValue(magic) || magic != CoSaveConstants::kMagicNumber ||
                !chunk.ReadValue(count) || count > kMaxTenants) {
                return false;
            }

            HouseholdTable staged = state.household.Read();
            staged.ClearAdultChildTravel();

            for (std::uint32_t i = 0; i < count; ++i) {
                std::uint32_t oldChild = 0;
                std::uint32_t oldMarker = 0;
                std::uint32_t oldLoc = 0;
                std::uint32_t oldCell = 0;
                float assignedDay = -1.0f;
                float arrivedDay = -1.0f;
                float stayDays = 3.0f;
                bool isFamVisit = false;
                std::string destName;

                if (!chunk.ReadValue(oldChild) ||
                    !chunk.ReadValue(oldMarker) ||
                    !chunk.ReadValue(oldLoc) ||
                    !chunk.ReadValue(oldCell) ||
                    !chunk.ReadValue(assignedDay) ||
                    !chunk.ReadValue(arrivedDay) ||
                    !chunk.ReadValue(stayDays) ||
                    !chunk.ReadValue(isFamVisit) ||
                    !chunk.ReadLengthPrefixedString(destName, kMaxChildStringLen)) {
                    return false;
                }

                std::uint32_t newChild = 0;
                std::uint32_t newMarker = 0;
                std::uint32_t newLoc = 0;
                std::uint32_t newCell = 0;
                if (ResolveActorOrSpecialRaw(resolver, oldChild, newChild, false, false, false, true) &&
                    ResolveActorOrSpecialRaw(resolver, oldMarker, newMarker, false, false, false, true) &&
                    newChild != 0 && newMarker != 0) {
                    (void)ResolveActorOrSpecialRaw(resolver, oldLoc, newLoc, true, false, false, true);
                    (void)ResolveActorOrSpecialRaw(resolver, oldCell, newCell, true, false, false, true);
                    staged.UpsertAdultChildTravel(
                        ChildId{ newChild },
                        AdultChildTravelRecord{
                            .targetMarkerId = RefId{ newMarker },
                            .targetLocationId = LocationId{ newLoc },
                            .targetCellId = CellId{ newCell },
                            .destinationName = std::move(destName),
                            .assignedGameDay = assignedDay,
                            .arrivedGameDay = arrivedDay,
                            .stayDurationDays = std::clamp(stayDays, kMinAdultChildStayDays, kMaxAdultChildStayDays),
                            .isFamilyVisit = isFamVisit,
                        });
                }
            }

            state.household.Reset(std::move(staged));
            return true;
        }

        bool LoadChunkCNFG(
            WorldState& state,
            BoundedChunkReader& chunk,
            std::uint32_t version,
            CoSaveLoadReport& report) {
            // Pre-v2 (v1–v3, <= v0.11.1): 'CNFG' was written by LoggingService::Save as a single 4-byte int32 logLevel.
            if (version < 4u) {
                std::int32_t storedLogLevel = 2;
                if (!chunk.ReadValue(storedLogLevel) ||
                    storedLogLevel < 0 || storedLogLevel > 5 ||
                    chunk.HasRemaining(1u)) {
                    return false;
                }
                SettingsData staged = state.settings.Read();
                staged.logLevel = storedLogLevel;
                report.cnfgPresent = true;
                report.cnfgHasVersionHeader = false;
                report.cnfgSubVersion = 0;
                report.savedModVersion.clear();
                state.settings.Reset(std::move(staged));
                return true;
            }

            std::uint8_t firstByte = 0;
            if (!chunk.ReadValue(firstByte)) {
                return false;
            }

            const bool hasVersionHeader = (firstByte == CoSaveConstants::kSettingsVersionMarker);
            std::uint32_t subVersion = 0;
            if (hasVersionHeader) {
                if (!chunk.ReadValue(subVersion) || subVersion < 1u || subVersion > CoSaveConstants::kSettingsSubVersion) {
                    return false;
                }
            }

            // Start from clean defaults while preserving INI-only hotkey/modal fields from current SettingsData
            SettingsData staged{};
            {
                const SettingsData& prev = state.settings.Read();
                staged.toggleUIKey = prev.toggleUIKey;
                staged.toggleUIModCtrl = prev.toggleUIModCtrl;
                staged.toggleUIModShift = prev.toggleUIModShift;
                staged.toggleUIModAlt = prev.toggleUIModAlt;
                staged.enableConfirmationModal = prev.enableConfirmationModal;
                staged.actionsCooldown = prev.actionsCooldown;
                staged.actionsDeclineCooldown = prev.actionsDeclineCooldown;
                staged.defaultLogLevel = prev.defaultLogLevel;
                staged.enablePlayerPolygamy = prev.enablePlayerPolygamy;
                staged.language = prev.language;
            }

            bool isFirstField = true;
            bool requiredReadFailed = false;
            bool streamEnded = false;

            ForEachPersistedSettingField([&](std::string_view key, auto memberPtr, bool required) {
                if (requiredReadFailed || streamEnded) {
                    return;
                }
                if (subVersion < 4u && key == "matchmakingUseLLM") {
                    std::uint8_t legacyExcludePlayerActivePartners = 0;
                    if (!chunk.ReadValue(legacyExcludePlayerActivePartners)) {
                        streamEnded = true;
                        return;
                    }
                }
                if (subVersion < 2u && key == "playerPartnerCheatingChanceMultiplier") {
                    std::int32_t legacyIntervalDays = 0;
                    if (!chunk.ReadValue(legacyIntervalDays)) {
                        streamEnded = true;
                        return;
                    }
                }

                auto& field = staged.*memberPtr;
                using FieldType = std::decay_t<decltype(field)>;

                bool readOk = false;
                if (isFirstField && !hasVersionHeader) {
                    isFirstField = false;
                    if constexpr (std::is_same_v<FieldType, bool>) {
                        field = (firstByte != 0);
                        readOk = true;
                    }
                } else {
                    isFirstField = false;
                    if constexpr (std::is_same_v<FieldType, bool>) {
                        std::uint8_t raw = 0;
                        if (chunk.ReadValue(raw)) {
                            field = (raw != 0);
                            readOk = true;
                        }
                    } else if constexpr (std::is_same_v<FieldType, ChildCustodyGenderPolicy>) {
                        std::uint8_t raw = 0;
                        if (chunk.ReadValue(raw)) {
                            if (raw <= static_cast<std::uint8_t>(ChildCustodyGenderPolicy::StayWithFather)) {
                                field = static_cast<ChildCustodyGenderPolicy>(raw);
                            }
                            readOk = true;
                        }
                    } else if constexpr (std::is_same_v<FieldType, ChildCustodyPlayerPolicy>) {
                        std::uint8_t raw = 0;
                        if (chunk.ReadValue(raw)) {
                            if (raw <= static_cast<std::uint8_t>(ChildCustodyPlayerPolicy::NeverWithPlayer)) {
                                field = static_cast<ChildCustodyPlayerPolicy>(raw);
                            }
                            readOk = true;
                        }
                    } else if constexpr (std::is_same_v<FieldType, MatchmakingOrientation>) {
                        std::uint8_t raw = 0;
                        if (chunk.ReadValue(raw)) {
                            if (raw <= static_cast<std::uint8_t>(MatchmakingOrientation::HeteroAndLesbian)) {
                                field = static_cast<MatchmakingOrientation>(raw);
                            }
                            readOk = true;
                        }
                    } else if constexpr (std::is_same_v<FieldType, float>) {
                        float val = 0.0f;
                        if (chunk.ReadValue(val)) {
                            if (key == "uiScale") {
                                if (!std::isnan(val) && val >= 0.5f && val <= 2.5f) {
                                    field = val;
                                }
                            } else if (key != "skyrimnetActionRadius" || val > 0.0f) {
                                field = val;
                            }
                            readOk = true;
                        }
                    } else {
                        FieldType val{};
                        if (chunk.ReadValue(val)) {
                            field = val;
                            readOk = true;
                        }
                    }
                }

                if (!readOk) {
                    if (required) {
                        requiredReadFailed = true;
                    } else {
                        streamEnded = true;
                    }
                }
            });

            if (requiredReadFailed) {
                return false;
            }

            std::string stagedModVersion{};
            if (!streamEnded && chunk.HasRemaining(1u)) {
                std::uint32_t extTag = 0;
                if (!chunk.ReadValue(extTag) ||
                    extTag != CoSaveConstants::kExtSaveMetadataVER1 ||
                    !chunk.ReadLengthPrefixedString(stagedModVersion, kMaxVersionStringLen)) {
                    return false;
                }
            }
            if (chunk.HasRemaining(1u)) {
                return false;
            }

            if (subVersion < 3u) {
                staged.childTrainingRequireFaction = true;
            }
            if (staged.logLevel < 0 || staged.logLevel > 5) {
                staged.logLevel = staged.defaultLogLevel;
            }

            report.cnfgPresent = true;
            report.cnfgHasVersionHeader = hasVersionHeader;
            report.cnfgSubVersion = subVersion;
            report.savedModVersion = std::move(stagedModVersion);
            state.settings.Reset(std::move(staged));
            return true;
        }

    }  // namespace

    // =========================================================================
    // Public Codec Entry Points
    // =========================================================================

    bool SaveWorldState(
        const WorldState& state,
        ICoSaveWriter& writer,
        std::string_view savedModVersion) {
        return SaveChunkNPCR(state, writer) &&
               SaveChunkSPHR(state, writer) &&
               SaveChunkAFCT(state, writer) &&
               SaveChunkPHOU(state, writer) &&
               SaveChunkSPAS(state, writer) &&
               SaveChunkPLCH(state, writer) &&
               SaveChunkCFSN(state, writer) &&
               SaveChunkMKMK(state, writer) &&
               SaveChunkDCSD(state, writer) &&
               SaveChunkBNTR(state, writer) &&
               SaveChunkACTR(state, writer) &&
               SaveChunkCNFG(state, writer, savedModVersion);
    }

    WorldMutationResult FinalizeWorldStateAfterLoad(
        WorldState& state,
        const IFormIdResolver& resolver,
        Foundation::IRng* optionalRng,
        CoSaveNormalizationStats* outStats) {
        Foundation::DeterministicRng fallbackRng{};
        Foundation::IRng& rng = optionalRng ? *optionalRng : fallbackRng;

        WorldMutationResult result{};

        auto isOrphanedDynamicRaw = [&](std::uint32_t rawId) -> bool {
            if (!Foundation::IsDynamicRuntimeActorRawId(rawId)) {
                return false;
            }
            return !resolver.IsDynamicActorPresent(RefId{ rawId });
        };

        // 1. Prune orphaned 0xFFxxxxxx dynamic runtime children & adults (NEVER synthetic 0xFF700000..0xFF7FFFFF)
        {
            std::vector<ChildId> orphanedChildren;
            for (const auto& [cId, rec] : state.lineage->AllChildren()) {
                (void)rec;
                if (isOrphanedDynamicRaw(cId.raw())) {
                    orphanedChildren.push_back(cId);
                }
            }
            for (const ChildId cId : orphanedChildren) {
                state.lineage.Write().EraseChild(cId);
                state.household.Write().EvictTenant(cId.raw());
                state.household.Write().EraseChildState(cId);
                state.household.Write().EraseChildFactionSnapshot(cId);
                state.household.Write().EraseAdultChildTravel(cId);
                result.changed = true;
                result.MarkChildDirty(cId);
            }

            std::unordered_set<ActorId> orphanedAdults;
            for (const auto& [refId, prof] : state.actors->AllProfiles()) {
                (void)prof;
                if (isOrphanedDynamicRaw(refId.raw())) {
                    orphanedAdults.insert(ActorId{ refId });
                }
            }
            for (const auto& [pair, edge] : state.relationships->AllEdges()) {
                (void)edge;
                if (isOrphanedDynamicRaw(pair.first.raw())) {
                    orphanedAdults.insert(pair.first);
                }
                if (isOrphanedDynamicRaw(pair.second.raw())) {
                    orphanedAdults.insert(pair.second);
                }
            }
            for (const ActorId dynAdult : orphanedAdults) {
                result.Merge(state.UnregisterActor(dynAdult));
            }
            if (outStats) {
                outStats->prunedOrphanedDynamicChildren += static_cast<std::uint32_t>(orphanedChildren.size());
                outStats->prunedOrphanedDynamicAdults += static_cast<std::uint32_t>(orphanedAdults.size());
            }
        }

        // 2. Reconcile dead-on-load / already-deceased adults & physical children across all tables
        {
            std::unordered_set<ActorId> deadAdults;
            for (const auto& [refId, prof] : state.actors->AllProfiles()) {
                if (refId.IsNull() || refId.IsPlayer()) {
                    continue;
                }
                const ActorId actor{ refId };
                if (state.deceased->IsDeceased(actor, prof.baseId) || !resolver.IsAliveAndValid(actor)) {
                    deadAdults.insert(actor);
                }
            }
            for (const auto& [pair, edge] : state.relationships->AllEdges()) {
                (void)edge;
                for (const ActorId actor : { pair.first, pair.second }) {
                    if (!actor.IsNull() && !actor.IsPlayer()) {
                        if (state.deceased->IsDeceased(actor) || !resolver.IsAliveAndValid(actor)) {
                            deadAdults.insert(actor);
                        }
                    }
                }
            }
            for (const ActorId deadActor : deadAdults) {
                result.Merge(state.RecordActorDeath(deadActor, 0.0f, false, ActorId{}, resolver));
            }

            std::vector<ChildId> deadChildren;
            for (const auto& [cId, rec] : state.lineage->AllChildren()) {
                (void)rec;
                if (cId.IsNull() || cId.IsSynthetic()) {
                    continue;
                }
                const ActorId childActor = cId.AsActorId();
                if (state.deceased->IsDeceased(childActor) || !resolver.IsAliveAndValid(childActor)) {
                    deadChildren.push_back(cId);
                }
            }
            for (const ChildId cId : deadChildren) {
                result.Merge(state.RecordActorDeath(cId.AsActorId(), 0.0f, false, ActorId{}, resolver));
            }

            std::unordered_set<ActorId> deadGuardians;
            for (const auto& [cId, rec] : state.lineage->AllChildren()) {
                (void)cId;
                for (const ActorId g : rec.guardians.AsArray()) {
                    if (!g.IsNull() && !g.IsPlayer() &&
                        (state.deceased->IsDeceased(g) || !resolver.IsAliveAndValid(g))) {
                        deadGuardians.insert(g);
                    }
                }
            }
            for (const ActorId deadG : deadGuardians) {
                result.Merge(state.EvaluateCustodyOnDeath(deadG, resolver));
            }
            if (outStats) {
                outStats->reconciledDeadAdults += static_cast<std::uint32_t>(deadAdults.size());
                outStats->reconciledDeadChildren += static_cast<std::uint32_t>(deadChildren.size());
            }
        }

        // 3. Ensure adult registration invariants (young children false; Stage 4 Adult Children & adults preserved)
        {
            auto ensureAdultRegistered = [&](ActorId id) {
                if (id.IsNull() || id.IsPlayer() || id.IsUnknownFather() || id.IsSyntheticChild()) {
                    return;
                }
                if (state.deceased->IsDeceased(id)) {
                    return;
                }
                if (const auto* cRec = state.lineage->FindChild(ChildId{ id.raw() });
                    cRec != nullptr && cRec->stage != ChildStage::Adult) {
                    return;
                }
                if (!state.actors->IsRegisteredAdult(id.AsRefId())) {
                    state.actors.Write().SetRegisteredAdult(id.AsRefId(), true);
                }
            };

            for (const auto& [pair, edge] : state.relationships->AllEdges()) {
                (void)edge;
                ensureAdultRegistered(pair.first);
                ensureAdultRegistered(pair.second);
            }
            for (const auto& [cId, rec] : state.lineage->AllChildren()) {
                (void)cId;
                ensureAdultRegistered(rec.guardians.slotA);
                ensureAdultRegistered(rec.guardians.slotB);
                ensureAdultRegistered(rec.bioMother);
                ensureAdultRegistered(rec.bioFather);
                ensureAdultRegistered(rec.presumedFather);
                if (rec.stage != ChildStage::Adult) {
                    if (const auto* prof = state.actors->Find(RefId{ rec.childId.raw() });
                        prof != nullptr && prof->isRegisteredAdult) {
                        state.actors.Write().SetRegisteredAdult(RefId{ rec.childId.raw() }, false);
                    }
                }
            }
        }

        // 4. Order-independent cross-chunk Household & Affection reconciliation
        //    (Preserves unloaded placed NPCs while pruning deceased / orphaned dynamic IDs!)
        {
            std::vector<std::uint32_t> tenantsToEvict;
            std::vector<std::uint32_t> youngChildTenantsToPinHome;
            for (const auto& [tid, assignment] : state.household->AllTenants()) {
                if (tid == 0 || tid == Foundation::kPlayerRefRawId || isOrphanedDynamicRaw(tid)) {
                    tenantsToEvict.push_back(tid);
                    continue;
                }
                if (Foundation::IsSyntheticChildRawId(tid)) {
                    const auto* cRec = state.lineage->FindChild(ChildId{ tid });
                    if (!cRec || cRec->stage == ChildStage::Adult) {
                        tenantsToEvict.push_back(tid);
                    } else if (!assignment.stayAtHome) {
                        youngChildTenantsToPinHome.push_back(tid);
                    }
                    continue;
                }
                const ActorId tActor{ tid };
                if (state.deceased->IsDeceased(tActor) || !resolver.IsAliveAndValid(tActor)) {
                    tenantsToEvict.push_back(tid);
                    continue;
                }
                if (const auto* cRec = state.lineage->FindChild(ChildId{ tid });
                    cRec != nullptr && cRec->stage != ChildStage::Adult && !assignment.stayAtHome) {
                    youngChildTenantsToPinHome.push_back(tid);
                }
            }
            for (const std::uint32_t tid : tenantsToEvict) {
                state.household.Write().EvictTenant(tid);
                result.changed = true;
            }
            if (outStats) {
                outStats->evictedStaleTenants += static_cast<std::uint32_t>(tenantsToEvict.size());
            }
            for (const std::uint32_t tid : youngChildTenantsToPinHome) {
                state.household.Write().SetTenantStayAtHome(tid, true);
                result.changed = true;
            }

            // Reconcile PLCH childStates <-> PHOU tenantOrder regardless of arrival order
            // Matches PlayerChildrenService::FinalizeLoad: preserves pre-v5 physical PLCH children (cRec == nullptr)
            // and never evicts adult/step-child PHOU tenants when pruning a stale PLCH child record.
            std::vector<ChildId> staleChildStates;
            std::vector<std::pair<ChildId, HouseId>> childHouseRepairs;
            for (const auto& [cId, cState] : state.household->AllChildStates()) {
                const auto* cRec = state.lineage->FindChild(cId);
                bool shouldPrune = false;
                if (cId.IsNull() || cId.raw() == Foundation::kPlayerRefRawId) {
                    shouldPrune = true;
                } else if (cId.IsSynthetic()) {
                    shouldPrune = (cRec == nullptr || cRec->stage == ChildStage::Adult);
                } else if (isOrphanedDynamicRaw(cId.raw()) ||
                           state.deceased->IsDeceased(cId.AsActorId()) ||
                           !resolver.IsAliveAndValid(cId.AsActorId()) ||
                           (cRec != nullptr && (cRec->stage == ChildStage::Adult || cRec->stage == ChildStage::Unborn))) {
                    shouldPrune = true;
                }

                if (shouldPrune) {
                    staleChildStates.push_back(cId);
                    continue;
                }
                const HouseId tenantHouse = state.household->GetTenantHouse(cId.raw());
                if (!tenantHouse.IsNull() && cState.currentHouseId != tenantHouse) {
                    childHouseRepairs.emplace_back(cId, tenantHouse);
                } else if (tenantHouse.IsNull() && !cState.currentHouseId.IsNull() &&
                           state.household->FindHouse(cState.currentHouseId) != nullptr) {
                    childHouseRepairs.emplace_back(cId, cState.currentHouseId);
                } else if (!cState.currentHouseId.IsNull() &&
                           state.household->FindHouse(cState.currentHouseId) == nullptr) {
                    childHouseRepairs.emplace_back(cId, HouseId{});
                }
            }
            for (const ChildId cId : staleChildStates) {
                state.household.Write().EraseChildState(cId);
                result.changed = true;
            }
            for (const auto& [cId, houseId] : childHouseRepairs) {
                if (houseId.IsNull()) {
                    if (auto* mutableState = state.household.Write().FindChildStateMutable(cId)) {
                        mutableState->currentHouseId = HouseId{};
                    }
                } else {
                    state.household.Write().AssignTenant(cId.raw(), houseId, true, true);
                }
                result.changed = true;
            }

            // Clear orphaned dynamic or deceased transient critters from surviving child states ('PLC1')
            std::vector<ChildId> staleTransientCritterChildren;
            for (const auto& [cId, cState] : state.household->AllChildStates()) {
                if (cState.transientCritterId.IsNull()) {
                    continue;
                }
                const ActorId critterActor{ cState.transientCritterId.raw() };
                if (isOrphanedDynamicRaw(cState.transientCritterId.raw()) ||
                    state.deceased->IsDeceased(critterActor) ||
                    !resolver.IsAliveAndValid(critterActor)) {
                    staleTransientCritterChildren.push_back(cId);
                }
            }
            for (const ChildId cId : staleTransientCritterChildren) {
                if (auto* mutableState = state.household.Write().FindChildStateMutable(cId)) {
                    mutableState->transientCritterId = RefId{};
                    result.changed = true;
                }
            }

            // Prune stale CFSN snapshots & ACTR travel entries
            std::vector<ChildId> staleSnapshots;
            for (const auto& [cId, snap] : state.household->AllChildFactionSnapshots()) {
                (void)snap;
                if (cId.IsSynthetic() || isOrphanedDynamicRaw(cId.raw()) ||
                    state.deceased->IsDeceased(cId.AsActorId())) {
                    staleSnapshots.push_back(cId);
                }
            }
            for (const ChildId cId : staleSnapshots) {
                state.household.Write().EraseChildFactionSnapshot(cId);
            }

            std::vector<ChildId> staleTravel;
            for (const auto& [cId, tr] : state.household->AllAdultChildTravel()) {
                (void)tr;
                const auto* cRec = state.lineage->FindChild(cId);
                if (!cRec || cRec->stage != ChildStage::Adult || cId.IsSynthetic() ||
                    isOrphanedDynamicRaw(cId.raw()) || state.deceased->IsDeceased(cId.AsActorId())) {
                    staleTravel.push_back(cId);
                }
            }
            for (const ChildId cId : staleTravel) {
                state.household.Write().EraseAdultChildTravel(cId);
            }

            // Prune deceased / unmarried spouses from SPAS sharedCells (preserving unloaded living married spouses
            // and inherited homes from deceased spouses not killed by the player)
            std::vector<ActorId> spousesToUnshare;
            for (const auto& [cellId, cellRec] : state.household->AllSharedCells()) {
                (void)cellId;
                for (const ActorId sp : cellRec.sharingSpouses) {
                    const auto* decRec = state.deceased->FindByActor(sp);
                    const bool isInheritedDeceasedSpouse =
                        decRec != nullptr && !decRec->wasKilledByPlayer &&
                        std::any_of(decRec->partnersAtDeath.begin(), decRec->partnersAtDeath.end(),
                                    [](const DeceasedPartnerEntry& p) {
                                        return p.isPlayer && p.statusAtDeath == RelationshipStatus::Married;
                                    });
                    if (sp.IsNull() ||
                        (!isInheritedDeceasedSpouse &&
                         (isOrphanedDynamicRaw(sp.raw()) || state.deceased->IsDeceased(sp) ||
                          !state.IsMarried(ActorId::Player(), sp)))) {
                        spousesToUnshare.push_back(sp);
                    }
                }
            }
            for (const ActorId sp : spousesToUnshare) {
                state.household.Write().StopSharingForSpouse(sp);
            }

            // Prune deceased / orphaned actors from AffectionTable (across permanent, lastDays, and daily maps)
            std::unordered_set<ActorId> deadAffectionActors;
            auto isStaleAffectionActor = [&](ActorId actor) -> bool {
                return isOrphanedDynamicRaw(actor.raw()) || state.deceased->IsDeceased(actor) ||
                       !resolver.IsAliveAndValid(actor);
            };
            for (const auto& [actor, score] : state.affection->AllPermanent()) {
                (void)score;
                if (isStaleAffectionActor(actor)) {
                    deadAffectionActors.insert(actor);
                }
            }
            for (const auto& [actor, day] : state.affection->AllLastDays()) {
                (void)day;
                if (isStaleAffectionActor(actor)) {
                    deadAffectionActors.insert(actor);
                }
            }
            for (const auto& [actor, types] : state.affection->AllDaily()) {
                (void)types;
                if (isStaleAffectionActor(actor)) {
                    deadAffectionActors.insert(actor);
                }
            }
            for (const ActorId actor : deadAffectionActors) {
                state.affection.Write().EraseActor(actor);
                result.changed = true;
            }

            // Prune deceased / orphaned actors from CooldownTable actor maps & 'BNT1' runtime tracking
            // Note: movedSceneActors's marker RefId may be a dynamic 0xFFxxxxxx XMarker TESObjectREFR
            // (not an Actor), so we validate movedSceneActors keys only and leave marker validation to engine lookup.
            std::unordered_set<ActorId> deadCooldownActors;
            auto isStaleCooldownActor = [&](ActorId actor) -> bool {
                return actor.IsNull() ||
                       actor.IsPlayer() ||
                       actor.IsSyntheticChild() ||
                       isOrphanedDynamicRaw(actor.raw()) ||
                       state.deceased->IsDeceased(actor) ||
                       !resolver.IsAliveAndValid(actor);
            };
            auto collectDeadCooldownActors = [&](const std::unordered_map<ActorId, float>& map) {
                for (const auto& [actor, day] : map) {
                    (void)day;
                    if (isStaleCooldownActor(actor)) {
                        deadCooldownActors.insert(actor);
                    }
                }
            };
            collectDeadCooldownActors(state.cooldowns->npcLastMatchmakingChangeDay);
            collectDeadCooldownActors(state.cooldowns->npcBrokeupTime);
            collectDeadCooldownActors(state.cooldowns->npcLastTimeSharedIncome);
            for (const auto& [actor, marker] : state.cooldowns->movedSceneActors) {
                if (isStaleCooldownActor(actor)) {
                    deadCooldownActors.insert(actor);
                    if (!marker.IsNull() &&
                        std::find(result.retiredSceneMarkers.begin(), result.retiredSceneMarkers.end(), marker) ==
                            result.retiredSceneMarkers.end()) {
                        result.retiredSceneMarkers.push_back(marker);
                    }
                }
            }
            for (const ActorId actor : state.cooldowns->skyrimNetMutedActors) {
                if (isStaleCooldownActor(actor)) {
                    deadCooldownActors.insert(actor);
                }
            }
            for (const ActorId actor : deadCooldownActors) {
                state.cooldowns.Write().EraseActor(actor);
                result.changed = true;
            }
            if (!state.cooldowns->skyrimNetMutedActors.empty()) {
                std::unordered_set<ActorId> seenMuted;
                bool hasDup = false;
                for (const ActorId actor : state.cooldowns->skyrimNetMutedActors) {
                    if (!seenMuted.insert(actor).second) {
                        hasDup = true;
                        break;
                    }
                }
                if (hasDup) {
                    std::unordered_set<ActorId> dedup;
                    std::vector<ActorId> cleaned;
                    cleaned.reserve(state.cooldowns->skyrimNetMutedActors.size());
                    for (const ActorId actor : state.cooldowns->skyrimNetMutedActors) {
                        if (dedup.insert(actor).second) {
                            cleaned.push_back(actor);
                        }
                    }
                    state.cooldowns.Write().skyrimNetMutedActors = std::move(cleaned);
                    result.changed = true;
                }
            }

            // Prune orphaned / deceased mothers and missing child references from ActiveDeliveryBatches ('FRT1')
            std::vector<ActorId> deadBatchMothers;
            std::vector<std::pair<ActorId, std::vector<ChildId>>> updatedBatchBorn;
            for (const auto& [motherId, batch] : state.lineage->AllActiveDeliveryBatches()) {
                if (isOrphanedDynamicRaw(motherId.raw()) || state.deceased->IsDeceased(motherId) ||
                    !resolver.IsAliveAndValid(motherId)) {
                    deadBatchMothers.push_back(motherId);
                    continue;
                }
                bool anyMissingChild = false;
                for (const ChildId cid : batch.bornChildIds) {
                    if (!state.lineage->FindChild(cid)) {
                        anyMissingChild = true;
                        break;
                    }
                }
                if (anyMissingChild) {
                    std::vector<ChildId> validBorn;
                    validBorn.reserve(batch.bornChildIds.size());
                    for (const ChildId cid : batch.bornChildIds) {
                        if (state.lineage->FindChild(cid)) {
                            validBorn.push_back(cid);
                        }
                    }
                    updatedBatchBorn.emplace_back(motherId, std::move(validBorn));
                }
            }
            if (!deadBatchMothers.empty() || !updatedBatchBorn.empty()) {
                auto& lineageWrite = state.lineage.Write();
                for (const ActorId motherId : deadBatchMothers) {
                    lineageWrite.EraseActiveDeliveryBatch(motherId);
                }
                for (auto& [motherId, validBorn] : updatedBatchBorn) {
                    if (auto* batch = lineageWrite.FindActiveDeliveryBatchMutable(motherId)) {
                        batch->bornChildIds = std::move(validBorn);
                    }
                }
            }
        }

        // 5. Synchronize married couples' child custody & SpouseHierarchyTable
        result.Merge(state.SyncAllMarriedCouplesChildCustody(resolver, rng));
        state.hierarchy.Write().FillGaps(
            state.GetPlayerSpousesInMarriageOrder(),
            ActorId{},
            [&](ActorId id) { return state.deceased->IsDeceased(id); });

        return result;
    }

    CoSaveLoadReport LoadWorldState(
        WorldState& state,
        ICoSaveReader& reader,
        const IFormIdResolver& resolver,
        Foundation::IRng* optionalRng) {
        CoSaveLoadReport report{};
        TrackingFormIdResolver trackedResolver(resolver);

        std::uint32_t type = 0;
        std::uint32_t version = 0;
        std::uint32_t length = 0;

        // Phase 1: Atomic per-chunk deserialization (order-independent)
        while (reader.NextRecordInfo(type, version, length)) {
            if (version < 1u || version > CoSaveConstants::kDataVersion) {
                const std::string detail = std::format(
                    "Unsupported chunk version {} (expected 1..{})",
                    version,
                    CoSaveConstants::kDataVersion);
                Foundation::LogError(
                    "[CoSaveCodec] Unsupported record version {} for chunk '{}' (0x{:08X}, expected 1-{})",
                    version, FourCCToString(type), type, CoSaveConstants::kDataVersion);
                report.allChunksSucceeded = false;
                ++report.failedChunkCount;
                report.failedChunkTypes.push_back(type);
                report.chunks.push_back(CoSaveChunkDiagnostic{
                    .type = type,
                    .version = version,
                    .byteLength = length,
                    .bytesRead = 0,
                    .succeeded = false,
                    .detail = detail,
                });
                continue;
            }

            BoundedChunkReader chunk(reader, length);
            bool ok = false;
            bool recognized = true;
            std::uint32_t inferredPresumed = 0;

            switch (type) {
                case CoSaveConstants::kChunkNPCR:
                    ok = LoadChunkNPCR(state, chunk, version, trackedResolver, inferredPresumed);
                    break;
                case CoSaveConstants::kChunkSPHR:
                    ok = LoadChunkSPHR(state, chunk, trackedResolver);
                    break;
                case CoSaveConstants::kChunkAFCT:
                    ok = LoadChunkAFCT(state, chunk, version, trackedResolver);
                    break;
                case CoSaveConstants::kChunkPHOU:
                    ok = LoadChunkPHOU(state, chunk, version, trackedResolver);
                    break;
                case CoSaveConstants::kChunkSPAS:
                    ok = LoadChunkSPAS(state, chunk, version, trackedResolver);
                    break;
                case CoSaveConstants::kChunkPLCH:
                    ok = LoadChunkPLCH(state, chunk, trackedResolver);
                    break;
                case CoSaveConstants::kChunkCFSN:
                    ok = LoadChunkCFSN(state, chunk, trackedResolver);
                    break;
                case CoSaveConstants::kChunkMKMK:
                    ok = LoadChunkMKMK(state, chunk, trackedResolver);
                    break;
                case CoSaveConstants::kChunkDCSD:
                    ok = LoadChunkDCSD(state, chunk, version, trackedResolver);
                    break;
                case CoSaveConstants::kChunkBNTR:
                    ok = LoadChunkBNTR(state, chunk, version, trackedResolver);
                    break;
                case CoSaveConstants::kChunkACTR:
                    ok = LoadChunkACTR(state, chunk, trackedResolver);
                    break;
                case CoSaveConstants::kChunkCNFG:
                    ok = LoadChunkCNFG(state, chunk, version, report);
                    break;
                default:
                    recognized = false;
                    break;
            }

            if (!recognized) {
                ++report.skippedChunkCount;
                continue;
            }

            if (report.minChunkVersion == 0 || version < report.minChunkVersion) {
                report.minChunkVersion = version;
            }
            if (version > report.maxChunkVersion) {
                report.maxChunkVersion = version;
            }
            if (version < CoSaveConstants::kDataVersion) {
                ++report.upgradedChunkCount;
            }

            if (ok) {
                ++report.loadedChunkCount;
                report.normalization.inferredLegacyPresumedFathers += inferredPresumed;
                std::string detail = (version < CoSaveConstants::kDataVersion)
                                         ? std::format("Migrated v{} -> v{}", version, CoSaveConstants::kDataVersion)
                                         : std::string("OK");
                if (type == CoSaveConstants::kChunkCNFG) {
                    if (version < 4u) {
                        detail = std::format("Migrated pre-v2 LoggingService CNFG (v{}) -> sub-v{}",
                                             version,
                                             CoSaveConstants::kSettingsSubVersion);
                    } else if (!report.cnfgHasVersionHeader) {
                        detail = std::format("Migrated headerless legacy CNFG -> sub-v{}", CoSaveConstants::kSettingsSubVersion);
                    } else if (report.cnfgSubVersion < CoSaveConstants::kSettingsSubVersion) {
                        detail = std::format("Migrated CNFG sub-v{} -> sub-v{}",
                                             report.cnfgSubVersion,
                                             CoSaveConstants::kSettingsSubVersion);
                    }
                }
                report.chunks.push_back(CoSaveChunkDiagnostic{
                    .type = type,
                    .version = version,
                    .byteLength = length,
                    .bytesRead = chunk.BytesRead(),
                    .succeeded = true,
                    .detail = std::move(detail),
                });
            } else {
                report.allChunksSucceeded = false;
                ++report.failedChunkCount;
                report.failedChunkTypes.push_back(type);
                const std::string detail = std::format(
                    "Decode failed at byte {} of {} (chunk v{}) — rolled back cleanly",
                    chunk.BytesRead(),
                    length,
                    version);
                report.chunks.push_back(CoSaveChunkDiagnostic{
                    .type = type,
                    .version = version,
                    .byteLength = length,
                    .bytesRead = chunk.BytesRead(),
                    .succeeded = false,
                    .detail = detail,
                });
                Foundation::LogError(
                    "[CoSaveCodec] Failed to decode chunk '{}' (0x{:08X}, version {}, byte {}/{}) — rolled back chunk cleanly",
                    FourCCToString(type), type, version, chunk.BytesRead(), length);
            }
        }

        // Phase 2: Post-chunk cross-table normalization
        if (report.loadedChunkCount > 0) {
            const WorldMutationResult normResult =
                FinalizeWorldStateAfterLoad(state, trackedResolver, optionalRng, &report.normalization);
            report.retiredSceneMarkers = normResult.retiredSceneMarkers;
        }

        report.resolvedFormIdCount = trackedResolver.ResolvedCount();
        report.remappedFormIdCount = trackedResolver.RemappedCount();
        report.unresolvedFormIdCount = trackedResolver.UnresolvedCount();

        return report;
    }

}  // namespace MARAS::State
